#include "pch.h"
#include "USlateManager.h"

#include <imgui_node_editor_internal.h>

#include "CameraActor.h"
#include "Windows/SWindow.h"
#include "Windows/SSplitterV.h"
#include "Windows/SDetailsWindow.h"
#include "Windows/SControlPanel.h"
#include "Windows/ControlPanelWindow.h"
#include "Windows/SViewportWindow.h"
#include "Windows/SSkeletalMeshViewerWindow.h"
#include "Windows/ConsoleWindow.h"
#include "Windows/ContentBrowserWindow.h"
#include "Widgets/MainToolbarWidget.h"
#include "Widgets/ConsoleWidget.h"
#include "FViewportClient.h"
#include "UIManager.h"
#include "GlobalConsole.h"
#include "ThumbnailManager.h"
#include "BlueprintGraph/AnimationGraph.h"
#include "BlueprintGraph/BlueprintActionDatabase.h"
#include "BlueprintGraph/EdGraph.h"
#include "Windows/AnimGraph/SAnimGraphEditorWindow.h"

IMPLEMENT_CLASS(USlateManager)

USlateManager& USlateManager::GetInstance()
{
    static USlateManager* Instance = nullptr;
    if (Instance == nullptr)
    {
        Instance = NewObject<USlateManager>();
    }
    return *Instance;
}
#include "FViewportClient.h"

extern float CLIENTWIDTH;
extern float CLIENTHEIGHT;

SViewportWindow* USlateManager::ActiveViewport;

void USlateManager::SaveSplitterConfig()
{
    if (!TopPanel) return;

    EditorINI["TopPanel"] = std::to_string(TopPanel->SplitRatio);
    EditorINI["LeftTop"] = std::to_string(LeftTop->SplitRatio);
    EditorINI["LeftBottom"] = std::to_string(LeftBottom->SplitRatio);
    EditorINI["LeftPanel"] = std::to_string(LeftPanel->SplitRatio);
    EditorINI["RightPanel"] = std::to_string(RightPanel->SplitRatio);
}

void USlateManager::LoadSplitterConfig()
{
    if (!TopPanel) return;

    if (EditorINI.Contains("TopPanel"))
        TopPanel->SplitRatio = std::stof(EditorINI["TopPanel"]);
    if (EditorINI.Contains("LeftTop"))
        LeftTop->SplitRatio = std::stof(EditorINI["LeftTop"]);
    if (EditorINI.Contains("LeftBottom"))
        LeftBottom->SplitRatio = std::stof(EditorINI["LeftBottom"]);
    if (EditorINI.Contains("LeftPanel"))
        LeftPanel->SplitRatio = std::stof(EditorINI["LeftPanel"]);
    if (EditorINI.Contains("RightPanel"))
        RightPanel->SplitRatio = std::stof(EditorINI["RightPanel"]);
}

USlateManager::USlateManager()
{
    for (auto& Viewport : Viewports)
        Viewport = nullptr;
}

USlateManager::~USlateManager()
{
    Shutdown();
}

// 뷰포트 윈도우 체크 전역 함수 (InputManager 콜백용)
static SSkeletalMeshViewerWindow* g_SkeletalViewerWindow = nullptr;
static SParticleViewerWindow* g_ParticleViewerWindow = nullptr;

static bool IsMouseInViewportWindow(const FVector2D& MousePos)
{
    // 스켈레탈 메시 뷰어의 뷰포트 영역 체크
    if (g_SkeletalViewerWindow && g_SkeletalViewerWindow->IsHover(MousePos))
    {
        // GetViewportRect()으로 뷰포트 영역 가져오기
        return g_SkeletalViewerWindow->GetViewportRect().Contains(MousePos);
    }

    // 파티클 뷰어의 뷰포트 영역 체크
    if (g_ParticleViewerWindow && g_ParticleViewerWindow->IsHover(MousePos))
    {
        // GetViewportRect()으로 뷰포트 영역 가져오기
        return g_ParticleViewerWindow->GetViewportRect().Contains(MousePos);
    }

    return false;
}

void USlateManager::Initialize(ID3D11Device* InDevice, UWorld* InWorld, const FRect& InRect)
{
    // MainToolbar 생성
    MainToolbar = NewObject<UMainToolbarWidget>();
    MainToolbar->Initialize();

    Device = InDevice;
    World = InWorld;
    Rect = InRect;

    // InputManager에 뷰포트 체크 콜백 등록
    UInputManager::GetInstance().SetViewportCheckCallback(IsMouseInViewportWindow);

    // === 전체 화면: 좌(4뷰포트) + 우(Control + Details) ===
    TopPanel = new SSplitterH();  // 수평 분할 (좌우)
    TopPanel->SetSplitRatio(0.7f);  // 70% 뷰포트, 30% UI
    TopPanel->SetRect(Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y);

    // 왼쪽: 4분할 뷰포트 영역
    LeftPanel = new SSplitterH();  // 수평 분할 (좌우)
    LeftTop = new SSplitterV();    // 수직 분할 (상하)
    LeftBottom = new SSplitterV(); // 수직 분할 (상하)
    LeftPanel->SideLT = LeftTop;
    LeftPanel->SideRB = LeftBottom;

    // 오른쪽: Control + Details (상하 분할)
    RightPanel = new SSplitterV();  // 수직 분할 (상하)
    RightPanel->SetSplitRatio(0.5f);  // 50-50 분할

    ControlPanel = new SControlPanel();
    DetailPanel = new SDetailsWindow();

    RightPanel->SideLT = ControlPanel;   // 위쪽: ControlPanel
    RightPanel->SideRB = DetailPanel;    // 아래쪽: DetailsWindow

    // TopPanel 좌우 배치
    TopPanel->SideLT = LeftPanel;
    TopPanel->SideRB = RightPanel;

    // === 뷰포트 생성 ===
    Viewports[0] = new SViewportWindow();
    Viewports[1] = new SViewportWindow();
    Viewports[2] = new SViewportWindow();
    Viewports[3] = new SViewportWindow();
    MainViewport = Viewports[0];

    Viewports[0]->Initialize(0, 0,
        Rect.GetWidth() / 2, Rect.GetHeight() / 2,
        World, Device, EViewportType::Perspective);

    Viewports[1]->Initialize(Rect.GetWidth() / 2, 0,
        Rect.GetWidth(), Rect.GetHeight() / 2,
        World, Device, EViewportType::Orthographic_Front);

    Viewports[2]->Initialize(0, Rect.GetHeight() / 2,
        Rect.GetWidth() / 2, Rect.GetHeight(),
        World, Device, EViewportType::Orthographic_Left);

    Viewports[3]->Initialize(Rect.GetWidth() / 2, Rect.GetHeight() / 2,
        Rect.GetWidth(), Rect.GetHeight(),
        World, Device, EViewportType::Orthographic_Top);

    World->SetEditorCameraActor(MainViewport->GetViewportClient()->GetCamera());

    // 뷰포트들을 2x2로 연결
    LeftTop->SideLT = Viewports[0];
    LeftTop->SideRB = Viewports[1];
    LeftBottom->SideLT = Viewports[2];
    LeftBottom->SideRB = Viewports[3];

    SwitchLayout(EViewportLayoutMode::SingleMain);

    LoadSplitterConfig();

    // === Console Overlay 생성 ===
    ConsoleWindow = new UConsoleWindow();
    if (ConsoleWindow)
    {
        UE_LOG("USlateManager: ConsoleWindow created successfully");
        UGlobalConsole::SetConsoleWidget(ConsoleWindow->GetConsoleWidget());
        UE_LOG("USlateManager: GlobalConsole connected to ConsoleWidget");
    }
    else
    {
        UE_LOG("ERROR: Failed to create ConsoleWindow");
    }

    // === Thumbnail Manager 초기화 ===
    FThumbnailManager::GetInstance().Initialize(Device, nullptr);
    UE_LOG("USlateManager: ThumbnailManager initialized");

    // === Content Browser 생성 ===
    ContentBrowserWindow = new UContentBrowserWindow();
    if (ContentBrowserWindow)
    {
        ContentBrowserWindow->Initialize();
        UE_LOG("USlateManager: ContentBrowserWindow created successfully");
    }
    else
    {
        UE_LOG("ERROR: Failed to create ContentBrowserWindow");
    }
}

void USlateManager::OpenSkeletalMeshViewer()
{
    if (SkeletalViewerWindow)
        return;

    SkeletalViewerWindow = new SSkeletalMeshViewerWindow();
    g_SkeletalViewerWindow = SkeletalViewerWindow; // 전역 포인터 업데이트

    // @todo 테스트용으로 삽입함. 나중에 무조건 지울 것 
    // AnimGraphEditorWindow = new SAnimGraphEditorWindow();
    // AnimGraphEditorWindow->Initialize();

    // Open as a detached window at a default size and position
    const float toolbarHeight = 50.0f;
    const float availableHeight = Rect.GetHeight() - toolbarHeight;
    const float w = Rect.GetWidth() * 0.85f;
    const float h = availableHeight * 0.85f;
    const float x = Rect.Left + (Rect.GetWidth() - w) * 0.5f;
    const float y = Rect.Top + toolbarHeight + (availableHeight - h) * 0.5f;
    SkeletalViewerWindow->Initialize(x, y, w, h, World, Device);
}

void USlateManager::OpenSkeletalMeshViewerWithFile(const char* FilePath)
{
    // 뷰어가 이미 열려있으면 그냥 사용, 아니면 새로 열기
    if (!SkeletalViewerWindow)
    {
        OpenSkeletalMeshViewer();
    }

    // Load the skeletal mesh into the viewer
    if (SkeletalViewerWindow && FilePath && FilePath[0] != '\0')
    {
        SkeletalViewerWindow->LoadSkeletalMesh(FilePath);
        UE_LOG("Opening SkeletalMeshViewer with file: %s", FilePath);
    }
}

void USlateManager::OpenAnimationGraphEditor(UAnimationGraph* AnimGraph)
{
    if (AnimationGraphEditorWindow)
    {
        return;
    }

    AnimationGraphEditorWindow = new SGraphEditorWindow();

    if (!AnimGraph)
    {
        AnimGraph = NewObject<UAnimationGraph>();
    }
    AnimationGraphEditorWindow->Initialize(AnimGraph);
}

void USlateManager::CloseSkeletalMeshViewer()
{
    if (!SkeletalViewerWindow) return;
    delete SkeletalViewerWindow;
    SkeletalViewerWindow = nullptr;
    g_SkeletalViewerWindow = nullptr; // 전역 포인터 업데이트
}

// ============================================================
// Particle Viewer
// ============================================================
void USlateManager::OpenParticleViewer()
{
    if (ParticleViewerWindow)
        return;

    ParticleViewerWindow = new SParticleViewerWindow();
    g_ParticleViewerWindow = ParticleViewerWindow; // 전역 포인터 업데이트

    // 창 크기/위치 설정
    const float toolbarHeight = 50.0f;
    const float availableHeight = Rect.GetHeight() - toolbarHeight;
    const float w = Rect.GetWidth() * 0.85f;
    const float h = availableHeight * 0.85f;
    const float x = Rect.Left + (Rect.GetWidth() - w) * 0.5f;
    const float y = Rect.Top + toolbarHeight + (availableHeight - h) * 0.5f;

    ParticleViewerWindow->Initialize(x, y, w, h, World, Device);

    // 빈 ParticleSystem 자동 생성
    ParticleViewerWindow->CreateParticleSystem();
}

void USlateManager::OpenParticleViewerWithSystem(UParticleSystem* ParticleSystem, const FString& SavePath)
{
    if (!ParticleViewerWindow)
    {
        ParticleViewerWindow = new SParticleViewerWindow();
        g_ParticleViewerWindow = ParticleViewerWindow;

        const float toolbarHeight = 50.0f;
        const float availableHeight = Rect.GetHeight() - toolbarHeight;
        const float w = Rect.GetWidth() * 0.85f;
        const float h = availableHeight * 0.85f;
        const float x = Rect.Left + (Rect.GetWidth() - w) * 0.5f;
        const float y = Rect.Top + toolbarHeight + (availableHeight - h) * 0.5f;

        ParticleViewerWindow->Initialize(x, y, w, h, World, Device);
    }

    if (ParticleViewerWindow)
    {
        // ParticleSystem이 nullptr이어도 LoadParticleSystem이 새로 생성함
        ParticleViewerWindow->LoadParticleSystem(ParticleSystem);
        if (!SavePath.empty()) { ParticleViewerWindow->SetSavePath(SavePath); }
    }
}

void USlateManager::CloseParticleViewer()
{
    if (!ParticleViewerWindow) return;
    delete ParticleViewerWindow;
    ParticleViewerWindow = nullptr;
    g_ParticleViewerWindow = nullptr; // 전역 포인터 업데이트
}

void USlateManager::CloseAnimationGraphEditor()
{
    if (!AnimationGraphEditorWindow)
    {
        return;
    }
    delete AnimationGraphEditorWindow;
    AnimationGraphEditorWindow = nullptr;
}

void USlateManager::SwitchLayout(EViewportLayoutMode NewMode)
{
    if (NewMode == CurrentMode) return;

    if (NewMode == EViewportLayoutMode::FourSplit)
    {
        TopPanel->SideLT = LeftPanel;
    }
    else if (NewMode == EViewportLayoutMode::SingleMain)
    {
        TopPanel->SideLT = MainViewport;
    }

    CurrentMode = NewMode;
}

void USlateManager::SwitchPanel(SWindow* SwitchPanel)
{
    if (TopPanel->SideLT != SwitchPanel) {
        TopPanel->SideLT = SwitchPanel;
        CurrentMode = EViewportLayoutMode::SingleMain;
    }
    else {
        TopPanel->SideLT = LeftPanel;
        CurrentMode = EViewportLayoutMode::FourSplit;
    }
}

void USlateManager::Render()
{
    // 메인 툴바 렌더링 (항상 최상단에)
    MainToolbar->RenderWidget();
    if (TopPanel)
    {
        TopPanel->OnRender();
    }

    // Content Browser 오버레이 렌더링 (하단에서 슬라이드 업)
    if (ContentBrowserWindow && ContentBrowserAnimationProgress > 0.0f)
    {
        extern float CLIENTWIDTH;
        extern float CLIENTHEIGHT;

        // 부드러운 감속을 위한 ease-out 곡선 적용
        float EasedProgress = 1.0f - (1.0f - ContentBrowserAnimationProgress) * (1.0f - ContentBrowserAnimationProgress);

        // 좌우 여백을 포함한 Content Browser 크기 계산
        float ContentBrowserHeight = CLIENTHEIGHT * ContentBrowserHeightRatio;
        float ContentBrowserWidth = CLIENTWIDTH - (ContentBrowserHorizontalMargin * 2.0f);
        float ContentBrowserXPos = ContentBrowserHorizontalMargin;

        // Y 위치 계산 (하단에서 슬라이드 업)
        float YPosWhenHidden = CLIENTHEIGHT; // 화면 밖 (하단)
        float YPosWhenVisible = CLIENTHEIGHT - ContentBrowserHeight; // 화면 내 (하단)
        float CurrentYPos = YPosWhenHidden + (YPosWhenVisible - YPosWhenHidden) * EasedProgress;

        // 둥근 모서리 스타일 적용
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.6f, 0.8f, 0.8f));

        // 윈도우 위치 및 크기 설정
        ImGui::SetNextWindowPos(ImVec2(ContentBrowserXPos, CurrentYPos));
        ImGui::SetNextWindowSize(ImVec2(ContentBrowserWidth, ContentBrowserHeight));

        // 윈도우 플래그
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoTitleBar;

        // Content Browser 렌더링
        bool isWindowOpen = true;
        if (ImGui::Begin("ContentBrowserOverlay", &isWindowOpen, flags))
        {
            // 포커스를 잃으면 닫기
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                bIsContentBrowserVisible &&
                !bIsContentBrowserAnimating)
            {
                ToggleContentBrowser(); // Content Browser 닫기
            }

            // 둥근 모서리가 있는 반투명 배경 추가
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 WindowPos = ImGui::GetWindowPos();
            ImVec2 WindowSize = ImGui::GetWindowSize();
            DrawList->AddRectFilled(
                WindowPos,
                ImVec2(WindowPos.x + WindowSize.x, WindowPos.y + WindowSize.y),
                IM_COL32(25, 25, 30, 240), // 높은 불투명도의 어두운 배경
                12.0f // 둥근 정도
            );

            // Content Browser 내용 렌더링
            if (ContentBrowserWindow)
            {
                ContentBrowserWindow->RenderContent();
            }
        }
        ImGui::End();

        // 스타일 변수 및 색상 복원
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(3);
    }

    // 콘솔 오버레이 렌더링 (모든 것 위에 표시)
    if (ConsoleWindow && ConsoleAnimationProgress > 0.0f)
    {
        extern float CLIENTWIDTH;
        extern float CLIENTHEIGHT;

        // 부드러운 감속을 위한 ease-out 곡선 적용
        float EasedProgress = 1.0f - (1.0f - ConsoleAnimationProgress) * (1.0f - ConsoleAnimationProgress);

        // 좌우 여백을 포함한 콘솔 크기 계산
        float ConsoleHeight = CLIENTHEIGHT * ConsoleHeightRatio;
        float ConsoleWidth = CLIENTWIDTH - (ConsoleHorizontalMargin * 2.0f);
        float ConsoleXPos = ConsoleHorizontalMargin;

        // Y 위치 계산 (하단에서 슬라이드 업)
        float YPosWhenHidden = CLIENTHEIGHT; // 화면 밖 (하단)
        float YPosWhenVisible = CLIENTHEIGHT - ConsoleHeight; // 화면 내 (하단)
        float CurrentYPos = YPosWhenHidden + (YPosWhenVisible - YPosWhenHidden) * EasedProgress;

        // 둥근 모서리 스타일 적용
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.4f, 0.8f));

        // 윈도우 위치 및 크기 설정
        ImGui::SetNextWindowPos(ImVec2(ConsoleXPos, CurrentYPos));
        ImGui::SetNextWindowSize(ImVec2(ConsoleWidth, ConsoleHeight));

        // 윈도우 플래그
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse;

        // 처음 열렸을 때 콘솔에 포커스
        if (bConsoleShouldFocus)
        {
            ImGui::SetNextWindowFocus();
            bConsoleShouldFocus = false;
        }

        // 콘솔 렌더링
        bool isWindowOpen = true;
        if (ImGui::Begin("ConsoleOverlay", &isWindowOpen, flags))
        {
            UConsoleWidget* ConsoleWidget = ConsoleWindow->GetConsoleWidget();
            bool bIsPinned = false;
            if (ConsoleWidget)
            {
                bIsPinned = ConsoleWidget->IsWindowPinned();
            }

            // 2. '핀'이 활성화되지 않았을 때만 포커스를 잃으면 닫기
            if (!bIsPinned &&
                !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                bIsConsoleVisible &&
                !bIsConsoleAnimating)
            {
                ToggleConsole(); // 콘솔 닫기
            }
            
            // 둥근 모서리가 있는 반투명 배경 추가
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 WindowPos = ImGui::GetWindowPos();
            ImVec2 WindowSize = ImGui::GetWindowSize();
            DrawList->AddRectFilled(
                WindowPos,
                ImVec2(WindowPos.x + WindowSize.x, WindowPos.y + WindowSize.y),
                IM_COL32(20, 20, 20, 240), // 높은 불투명도의 어두운 배경
                12.0f // 둥근 정도
            );

            // 콘솔 위젯 렌더링
            ConsoleWindow->RenderWidget();
        }
        ImGui::End();

        // 스타일 변수 및 색상 복원
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(3);
    }
    
    // Render detached viewer on top
    if (SkeletalViewerWindow)
    {
        SkeletalViewerWindow->OnRender();
    }

    if (ParticleViewerWindow)
    {
        ParticleViewerWindow->OnRender();
    }

    if (AnimationGraphEditorWindow)
    {
        AnimationGraphEditorWindow->OnRender();
    }
}

void USlateManager::RenderAfterUI()
{
    if (SkeletalViewerWindow)
    {
        SkeletalViewerWindow->OnRenderViewport();
    }

    if (ParticleViewerWindow)
    {
        ParticleViewerWindow->OnRenderViewport();
    }
}

void USlateManager::Update(float DeltaSeconds)
{
    ProcessInput();
    // MainToolbar 업데이트
    MainToolbar->Update();

    if (TopPanel)
    {
        // 툴바 높이만큼 아래로 이동 (50px)
        const float toolbarHeight = 50.0f;
        TopPanel->Rect = FRect(0, toolbarHeight, CLIENTWIDTH, CLIENTHEIGHT);
        TopPanel->OnUpdate(DeltaSeconds);
    }

    if (SkeletalViewerWindow)
    {
        SkeletalViewerWindow->OnUpdate(DeltaSeconds);
    }

    if (ParticleViewerWindow)
    {
        ParticleViewerWindow->OnUpdate(DeltaSeconds);

        // 창이 닫혔으면 정리
        if (!ParticleViewerWindow->IsOpen())
        {
            CloseParticleViewer();
        }
    }

    // 콘솔 애니메이션 업데이트
    if (bIsConsoleAnimating)
    {
        if (bIsConsoleVisible)
        {
            // 애니메이션 인 (나타남)
            ConsoleAnimationProgress += DeltaSeconds / ConsoleAnimationDuration;
            if (ConsoleAnimationProgress >= 1.0f)
            {
                ConsoleAnimationProgress = 1.0f;
                bIsConsoleAnimating = false;
            }
        }
        else
        {
            // 애니메이션 아웃 (사라짐)
            ConsoleAnimationProgress -= DeltaSeconds / ConsoleAnimationDuration;
            if (ConsoleAnimationProgress <= 0.0f)
            {
                ConsoleAnimationProgress = 0.0f;
                bIsConsoleAnimating = false;
            }
        }
    }

    // Content Browser 애니메이션 업데이트
    if (bIsContentBrowserAnimating)
    {
        if (bIsContentBrowserVisible)
        {
            // 애니메이션 인 (나타남)
            ContentBrowserAnimationProgress += DeltaSeconds / ContentBrowserAnimationDuration;
            if (ContentBrowserAnimationProgress >= 1.0f)
            {
                ContentBrowserAnimationProgress = 1.0f;
                bIsContentBrowserAnimating = false;
            }
        }
        else
        {
            // 애니메이션 아웃 (사라짐)
            ContentBrowserAnimationProgress -= DeltaSeconds / ContentBrowserAnimationDuration;
            if (ContentBrowserAnimationProgress <= 0.0f)
            {
                ContentBrowserAnimationProgress = 0.0f;
                bIsContentBrowserAnimating = false;
            }
        }
    }

    // ConsoleWindow 업데이트
    if (ConsoleWindow && ConsoleAnimationProgress > 0.0f)
    {
        ConsoleWindow->Update();
    }
}

void USlateManager::ProcessInput()
{
    const FVector2D MousePosition = INPUT.GetMousePosition();

    if (SkeletalViewerWindow && SkeletalViewerWindow->Rect.Contains(MousePosition))
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            OnMouseDown(MousePosition, 0);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            OnMouseDown(MousePosition, 1);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            OnMouseUp(MousePosition, 0);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            OnMouseUp(MousePosition, 1);
        }
    }

    if (INPUT.IsMouseButtonPressed(LeftButton))
    {
        const FVector2D MousePosition = INPUT.GetMousePosition();
        {
            OnMouseDown(MousePosition, 0);
        }
    }
    if (INPUT.IsMouseButtonPressed(RightButton))
    {
        const FVector2D MousePosition = INPUT.GetMousePosition();
        {
            OnMouseDown(MousePosition, 1);
        }
    }
    if (INPUT.IsMouseButtonReleased(LeftButton))
    {
        const FVector2D MousePosition = INPUT.GetMousePosition();
        {
            OnMouseUp(MousePosition, 0);
        }
    }
    if (INPUT.IsMouseButtonReleased(RightButton))
    {
        const FVector2D MousePosition = INPUT.GetMousePosition();
        {
            OnMouseUp(MousePosition, 1);
        }
    }
    OnMouseMove(MousePosition);

    // Alt + ` (억음 부호 키)로 콘솔 토글
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent) && ImGui::GetIO().KeyAlt)
    {
        ToggleConsole();
    }

    // Ctrl + Space로 Content Browser 토글
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && ImGui::GetIO().KeyCtrl)
    {
        ToggleContentBrowser();
    }

    // ESC closes the Skeletal Mesh Viewer if open
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && SkeletalViewerWindow)
    {
        CloseSkeletalMeshViewer();
    }

    // 단축키로 기즈모 모드 변경
    if (World->GetGizmoActor())
        World->GetGizmoActor()->ProcessGizmoModeSwitch();
}

void USlateManager::OnMouseMove(FVector2D MousePos)
{
    // Route to detached viewer if hovered
    if (SkeletalViewerWindow && SkeletalViewerWindow->IsHover(MousePos))
    {
        SkeletalViewerWindow->OnMouseMove(MousePos);
        return;
    }

    if (ParticleViewerWindow && ParticleViewerWindow->IsHover(MousePos))
    {
        ParticleViewerWindow->OnMouseMove(MousePos);
        return;
    }

    if (ActiveViewport)
    {
        ActiveViewport->OnMouseMove(MousePos);
    }
    else if (TopPanel)
    {
        TopPanel->OnMouseMove(MousePos);
    }
}

void USlateManager::OnMouseDown(FVector2D MousePos, uint32 Button)
{
    if (SkeletalViewerWindow && SkeletalViewerWindow->Rect.Contains(MousePos))
    {
        SkeletalViewerWindow->OnMouseDown(MousePos, Button);
        return;
    }

    if (ParticleViewerWindow && ParticleViewerWindow->Rect.Contains(MousePos))
    {
        ParticleViewerWindow->OnMouseDown(MousePos, Button);
        return;
    }

    if (ActiveViewport)
    {
    }
    else if (TopPanel)
    {
        TopPanel->OnMouseDown(MousePos, Button);

        // 어떤 뷰포트 안에서 눌렸는지 확인
        for (auto* VP : Viewports)
        {
            if (VP && VP->Rect.Contains(MousePos))
            {
                ActiveViewport = VP; // 고정

                // 우클릭인 경우 커서 숨김 및 잠금
                if (Button == 1)
                {
                    INPUT.SetCursorVisible(false);
                    INPUT.LockCursor();
                }
                break;
            }
        }
    }
}

void USlateManager::OnMouseUp(FVector2D MousePos, uint32 Button)
{
    // 우클릭 해제 시 커서 복원 (ActiveViewport와 무관하게 처리)
    if (Button == 1 && INPUT.IsCursorLocked())
    {
        INPUT.SetCursorVisible(true);
        INPUT.ReleaseCursor();
    }

    if (SkeletalViewerWindow && SkeletalViewerWindow->Rect.Contains(MousePos))
    {
        SkeletalViewerWindow->OnMouseUp(MousePos, Button);
        // do not return; still allow panels to finish mouse up
    }

    if (ParticleViewerWindow)
    {
        // 뷰포트 밖에서 마우스를 놓아도 드래그가 해제되도록 항상 처리
        ParticleViewerWindow->OnMouseUp(MousePos, Button);
        // do not return; still allow panels to finish mouse up
    }

    if (ActiveViewport)
    {
        ActiveViewport->OnMouseUp(MousePos, Button);
        ActiveViewport = nullptr; // 드래그 끝나면 해제
    }
    // NOTE: ActiveViewport가 있더라도 Up 이벤트는 항상 보내주어 드래그 관련 버그를 제거
    if (TopPanel)
    {
        TopPanel->OnMouseUp(MousePos, Button);
    }
}

void USlateManager::OnShutdown()
{
    SaveSplitterConfig();
}

void USlateManager::Shutdown()
{
    if (bIsShutdown) { return; }
    bIsShutdown = true;
    // 레이아웃/설정 저장
    SaveSplitterConfig();

    // 콘솔 윈도우 삭제
    if (ConsoleWindow)
    {
        delete ConsoleWindow;
        ConsoleWindow = nullptr;
        UE_LOG("USlateManager: ConsoleWindow destroyed");
    }

    // Content Browser 윈도우 삭제
    if (ContentBrowserWindow)
    {
        delete ContentBrowserWindow;
        ContentBrowserWindow = nullptr;
        UE_LOG("USlateManager: ContentBrowserWindow destroyed");
    }

    // Thumbnail Manager 종료
    FThumbnailManager::GetInstance().Shutdown();
    UE_LOG("USlateManager: ThumbnailManager shutdown");

    // D3D 컨텍스트를 해제하기 위해 UI 패널과 뷰포트를 명시적으로 삭제
    if (TopPanel) { delete TopPanel; TopPanel = nullptr; }
    if (LeftTop) { delete LeftTop; LeftTop = nullptr; }
    if (LeftBottom) { delete LeftBottom; LeftBottom = nullptr; }
    if (LeftPanel) { delete LeftPanel; LeftPanel = nullptr; }
    if (RightPanel) { delete RightPanel; RightPanel = nullptr; }

    if (ControlPanel) { delete ControlPanel; ControlPanel = nullptr; }
    if (DetailPanel) { delete DetailPanel; DetailPanel = nullptr; }

    for (int i = 0; i < 4; ++i)
    {
        if (Viewports[i]) { delete Viewports[i]; Viewports[i] = nullptr; }
    }
    MainViewport = nullptr;
    ActiveViewport = nullptr;

    if (SkeletalViewerWindow)
    {
        delete SkeletalViewerWindow;
        SkeletalViewerWindow = nullptr;
    }

    if (AnimationGraphEditorWindow)
    {
        delete AnimationGraphEditorWindow;
        AnimationGraphEditorWindow = nullptr;
    }

    if (ParticleViewerWindow)
    {
        delete ParticleViewerWindow;
        ParticleViewerWindow = nullptr;
    }
}

void USlateManager::SetPIEWorld(UWorld* InWorld)
{
    MainViewport->SetVClientWorld(InWorld);
    // PIE에도 Main Camera Set
    InWorld->SetEditorCameraActor(MainViewport->GetViewportClient()->GetCamera());
}

void USlateManager::ToggleConsole()
{
    bIsConsoleVisible = !bIsConsoleVisible;
    bIsConsoleAnimating = true;

    // 콘솔을 열 때 포커스 플래그 설정
    if (bIsConsoleVisible)
    {
        bConsoleShouldFocus = true;
    }
}

void USlateManager::ForceOpenConsole()
{
    if (!bIsConsoleVisible)
    {
        // 2. 토글 함수를 호출하여 열기 상태(true)로 전환하고 애니메이션 시작
        ToggleConsole();
    }
}

void USlateManager::ToggleContentBrowser()
{
    bIsContentBrowserVisible = !bIsContentBrowserVisible;
    bIsContentBrowserAnimating = true;
}

bool USlateManager::IsContentBrowserVisible() const
{
    return bIsContentBrowserVisible;
}
