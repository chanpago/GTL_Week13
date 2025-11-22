// Copyright Epic Games, Inc. All Rights Reserved.
#include "pch.h"
#include "ContentBrowserWindow.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "USlateManager.h"
#include "ThumbnailManager.h"
#include "Source/Runtime/Engine/Particle/ParticleSystem.h"
#include "Source/Runtime/Core/Object/ObjectFactory.h"
#include <algorithm>

IMPLEMENT_CLASS(UContentBrowserWindow)

UContentBrowserWindow::UContentBrowserWindow()
    : SelectedFile(nullptr)
    , SelectedIndex(-1)
    , LastClickTime(0.0)
    , LastClickedIndex(-1)
    , ThumbnailSize(80.0f)
    , ColumnsCount(4)
    , LeftPanelWidth(250.0f)
    , SplitterWidth(4.0f)
    , bIsDraggingSplitter(false)
    , bShowFolders(true)
    , bShowFiles(true)
    , bUseThumbnails(true)
{
    FUIWindowConfig Config;
    Config.WindowTitle = "Content Browser";
    Config.DefaultSize = ImVec2(800, 400);
    Config.DefaultPosition = ImVec2(100, 500);
    Config.MinSize = ImVec2(400, 300);
    Config.bResizable = true;
    Config.bMovable = true;
    Config.bCollapsible = true;
    Config.DockDirection = EUIDockDirection::Bottom;

    Config.UpdateWindowFlags();
    SetConfig(Config);

    // GDataDir(FString)을 WideString으로 변환하여 safe하게 path 생성
    RootPath = std::filesystem::path(UTF8ToWide(GDataDir));
    CurrentPath = RootPath;
}

UContentBrowserWindow::~UContentBrowserWindow()
{
    Cleanup();
}

void UContentBrowserWindow::Initialize()
{
    UE_LOG("ContentBrowserWindow: Successfully Initialized");
    UUIWindow::Initialize();
    RefreshCurrentDirectory();
}

void UContentBrowserWindow::RenderWindow()
{
    if (!IsVisible()) return;

    ImGui::SetNextWindowSize(GetConfig().DefaultSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(GetConfig().DefaultPosition, ImGuiCond_FirstUseEver);

    bool bIsOpen = true;
    if (ImGui::Begin(GetConfig().WindowTitle.c_str(), &bIsOpen, GetConfig().WindowFlags))
    {
        RenderContent(); // RenderPathBar + RenderContentGrid 통합 호출
    }
    ImGui::End();

    if (!bIsOpen)
    {
        SetWindowState(EUIWindowState::Hidden);
    }
}

void UContentBrowserWindow::Cleanup()
{
    DisplayedFiles.clear();
    SelectedFile = nullptr;
}

void UContentBrowserWindow::NavigateToPath(const std::filesystem::path& NewPath)
{
    // filesystem::path 자체는 이미 safe함
    if (std::filesystem::exists(NewPath) && std::filesystem::is_directory(NewPath))
    {
        CurrentPath = NewPath;
        RefreshCurrentDirectory();
        SelectedIndex = -1;
        SelectedFile = nullptr;
    }
}

void UContentBrowserWindow::RefreshCurrentDirectory()
{
    DisplayedFiles.clear();

    if (!std::filesystem::exists(CurrentPath))
    {
        // 로그 출력 시 path -> wstring -> UTF8 변환
        FString PathUTF8 = WideToUTF8(CurrentPath.wstring());
        UE_LOG("ContentBrowserWindow: Path does not exist: %s", PathUTF8.c_str());
        CurrentPath = RootPath;
        return;
    }

    try
    {
        if (bShowFiles)
        {
            for (const auto& entry : std::filesystem::directory_iterator(CurrentPath))
            {
                if (entry.is_regular_file())
                {
                    FFileEntry FileEntry;
                    FileEntry.Path = entry.path();
                    
                    // [핵심] 여기서 .wstring()을 사용하여 원본 유니코드 이름을 가져옴
                    FileEntry.FileNameW = entry.path().filename().wstring(); 
                    
                    // [핵심] ImGui 출력을 위해 UTF-8로 미리 변환하여 캐싱
                    FileEntry.FileNameUTF8 = WideToUTF8(FileEntry.FileNameW); 
                    
                    // 확장자 처리 (비교를 위해 소문자 변환)
                    FWideString ExtW = entry.path().extension().wstring();
                    FileEntry.Extension = WideToUTF8(ExtW); // UTF-8로 변환
                    std::transform(FileEntry.Extension.begin(), FileEntry.Extension.end(), FileEntry.Extension.begin(), ::tolower);
                    
                    FileEntry.bIsDirectory = false;
                    FileEntry.FileSize = std::filesystem::file_size(entry.path());
                    
                    DisplayedFiles.push_back(FileEntry);
                }
            }
        }
        
        FString CurrentPathUTF8 = WideToUTF8(CurrentPath.wstring());
        UE_LOG("ContentBrowserWindow: Loaded %d items from %s", DisplayedFiles.size(), CurrentPathUTF8.c_str());
    }
    catch (const std::exception& e)
    {
        UE_LOG("ContentBrowserWindow: Error reading directory: %s", e.what());
    }
}

void UContentBrowserWindow::RenderContent()
{
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    ImGuiStyle& style = ImGui::GetStyle();

    // Left Panel
    ImGui::BeginChild("LeftPanel", ImVec2(LeftPanelWidth, contentSize.y), true);
    RenderFolderTree();
    ImGui::EndChild();

    ImGui::SameLine();

    // Splitter
    ImGui::Button("##Splitter", ImVec2(SplitterWidth, contentSize.y));
    if (ImGui::IsItemActive()) bIsDraggingSplitter = true;
    if (bIsDraggingSplitter)
    {
        float delta = ImGui::GetIO().MouseDelta.x;
        LeftPanelWidth += delta;
        LeftPanelWidth = std::max(150.0f, std::min(LeftPanelWidth, contentSize.x - 300.0f));
        if (!ImGui::IsMouseDown(0)) bIsDraggingSplitter = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImGui::SameLine();

    // Right Panel
    float rightPanelWidth = contentSize.x - LeftPanelWidth - SplitterWidth - style.ItemSpacing.x * 2;
    ImGui::BeginChild("RightPanel", ImVec2(rightPanelWidth, contentSize.y), true);
    RenderPathBar();
    ImGui::Separator();
    RenderContentGrid();
    ImGui::EndChild();
}

void UContentBrowserWindow::RenderFolderTree()
{
    ImGui::Text("Folders");
    ImGui::Separator();

    if (std::filesystem::exists(RootPath))
    {
        RenderFolderTreeNode(RootPath);
    }
}

void UContentBrowserWindow::RenderFolderTreeNode(const std::filesystem::path& FolderPath)
{
    try
    {
        // [핵심] 폴더 이름도 wstring -> UTF8 변환
        FWideString WName = (FolderPath == RootPath) ? L"Data" : FolderPath.filename().wstring();
        FString FolderNameUTF8 = WideToUTF8(WName);

        bool hasSubFolders = false;
        for (const auto& entry : std::filesystem::directory_iterator(FolderPath))
        {
            if (entry.is_directory())
            {
                hasSubFolders = true;
                break;
            }
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (!hasSubFolders) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (FolderPath == CurrentPath) flags |= ImGuiTreeNodeFlags_Selected;

        // ImGui에는 항상 UTF-8 문자열 전달
        bool nodeOpen = ImGui::TreeNodeEx(FolderNameUTF8.c_str(), flags);

        if (ImGui::IsItemClicked())
        {
            NavigateToPath(FolderPath);
        }

        if (nodeOpen && hasSubFolders)
        {
            for (const auto& entry : std::filesystem::directory_iterator(FolderPath))
            {
                if (entry.is_directory())
                {
                    RenderFolderTreeNode(entry.path());
                }
            }
            ImGui::TreePop();
        }
    }
    catch (const std::exception& e)
    {
        UE_LOG("Error rendering folder tree: %s", e.what());
    }
}

void UContentBrowserWindow::RenderPathBar()
{
    ImGui::Text("Path: ");
    ImGui::SameLine();

    if (CurrentPath != RootPath)
    {
        if (ImGui::Button("<- Back"))
        {
            NavigateToPath(CurrentPath.parent_path());
        }
        ImGui::SameLine();
    }

    // [핵심] 현재 경로 표시 시 wstring -> UTF8 변환
    FString PathStrUTF8 = WideToUTF8(CurrentPath.wstring());
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", PathStrUTF8.c_str());

    ImGui::SameLine(ImGui::GetWindowWidth() - 100);
    if (ImGui::Button("Refresh"))
    {
        RefreshCurrentDirectory();
    }
    ImGui::Separator();
}

void UContentBrowserWindow::RenderContentGrid()
{
    ImGuiStyle& style = ImGui::GetStyle();
    float windowWidth = ImGui::GetContentRegionAvail().x;
    float cellSize = ThumbnailSize + style.ItemSpacing.x * 2;

    ColumnsCount = std::max(2, (int)(windowWidth / cellSize));

    ImGui::BeginChild("ContentArea", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // 우클릭 컨텍스트 메뉴 (빈 공간)
    // BeginPopupContextWindow 대신 수동으로 처리하여 마우스 캡처 문제 방지
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("ContentBrowserContextMenu");
    }

    if (ImGui::BeginPopup("ContentBrowserContextMenu"))
    {
        if (ImGui::MenuItem("Create Particle System"))
        {
            CreateNewParticleSystem();
        }
        ImGui::EndPopup();
    }

    for (int i = 0; i < DisplayedFiles.size(); ++i)
    {
        FFileEntry& entry = DisplayedFiles[i];
        ImGui::PushID(i);

        if (i > 0)
        {
            int columnIndex = i % ColumnsCount;
            if (columnIndex != 0) ImGui::SameLine();
        }

        RenderFileItem(entry, i, bUseThumbnails);
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void UContentBrowserWindow::RenderFileItem(FFileEntry& Entry, int Index, bool bUseThumbnails)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 buttonSize(ThumbnailSize, ThumbnailSize);

    ImGui::BeginGroup();

    bool isSelected = (Index == SelectedIndex);
    if (isSelected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    }

    bool clicked = false;
    if (bUseThumbnails)
    {
        // [핵심] ThumbnailManager에 전달할 때 UTF-8 문자열 사용 (Manager가 UTF-8을 기대한다고 가정)
        // 만약 Manager가 WideChar를 원하면 Entry.Path.wstring().c_str() 사용
        std::string pathUTF8 = WideToUTF8(Entry.Path.wstring());
        ID3D11ShaderResourceView* thumbnailSRV = FThumbnailManager::GetInstance().GetThumbnail(pathUTF8);

        if (thumbnailSRV)
        {
            char buttonID[32];
            sprintf_s(buttonID, "##thumbnail_%d", Index);
            clicked = ImGui::ImageButton(buttonID, (void*)thumbnailSRV, buttonSize);
        }
        else
        {
            const char* icon = GetIconForFile(Entry);
            clicked = ImGui::Button(icon, buttonSize);
        }
    }
    else
    {
        const char* icon = GetIconForFile(Entry);
        clicked = ImGui::Button(icon, buttonSize);
    }

    if (clicked)
    {
        SelectedIndex = Index;
        SelectedFile = &Entry;

        double currentTime = ImGui::GetTime();
        if (LastClickedIndex == Index && (currentTime - LastClickTime) < 0.3)
        {
            HandleDoubleClick(Entry);
            LastClickTime = 0.0;
        }
        else
        {
            LastClickTime = currentTime;
            LastClickedIndex = Index;
        }
    }

    if (isSelected) ImGui::PopStyleColor(3);

    // [핵심] 파일 이름 출력 시 미리 변환해둔 UTF-8 문자열 사용
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ThumbnailSize);
    ImGui::TextWrapped("%s", Entry.FileNameUTF8.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();

    HandleDragSource(Entry);
}

void UContentBrowserWindow::HandleDragSource(FFileEntry& Entry)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        // [핵심] 드래그 드롭 페이로드도 UTF-8로 전달
        std::string pathStr = WideToUTF8(Entry.Path.wstring());
        ImGui::SetDragDropPayload("ASSET_FILE", pathStr.c_str(), pathStr.size() + 1);

        ImGui::Text("Move %s", Entry.FileNameUTF8.c_str());
        if (!Entry.Extension.empty())
        {
            ImGui::Text("Type: %s", Entry.Extension.c_str());
        }

        ImGui::EndDragDropSource();
    }
}

void UContentBrowserWindow::HandleDoubleClick(FFileEntry& Entry)
{
    // [핵심] 로그 출력 시 UTF-8 사용
    UE_LOG("ContentBrowserWindow: Double-clicked file: %s", Entry.FileNameUTF8.c_str());

    std::string ext = Entry.Extension; 
    // 이미 소문자로 변환되어 있음 (RefreshCurrentDirectory에서 처리)

    // [핵심] 외부 매니저 함수 호출 시 UTF-8 경로 전달
    std::string pathUTF8 = WideToUTF8(Entry.Path.wstring());

    if (ext == ".fbx")
    {
        USlateManager::GetInstance().OpenSkeletalMeshViewerWithFile(pathUTF8.c_str());
        UE_LOG("Opening SkeletalMeshViewer for: %s", Entry.FileNameUTF8.c_str());
    }
    else if (ext == ".obj")
    {
        UE_LOG("StaticMesh file clicked: %s", Entry.FileNameUTF8.c_str());
    }
    else
    {
        UE_LOG("Unsupported file type: %s", ext.c_str());
    }
}

const char* UContentBrowserWindow::GetIconForFile(const FFileEntry& Entry) const
{
    if (Entry.bIsDirectory) return "[DIR]";

    const std::string& ext = Entry.Extension;

    if (ext == ".prefab") return "[PREFAB]";
    else if (ext == ".fbx" || ext == ".obj") return "[MESH]";
    else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga") return "[IMG]";
    else if (ext == ".hlsl" || ext == ".glsl" || ext == ".fx") return "[SHDR]";
    else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") return "[SND]";
    else if (ext == ".mat") return "[MAT]";
    else if (ext == ".level" || ext == ".json") return "[DATA]";

    return "[FILE]";
}

FString UContentBrowserWindow::FormatFileSize(uintmax_t Size) const
{
    const char* units[] = { "B", "KB", "MB", "GB" };
    int unitIndex = 0;
    double size = static_cast<double>(Size);

    while (size >= 1024.0 && unitIndex < 3)
    {
        size /= 1024.0;
        unitIndex++;
    }

    char buffer[64];
    sprintf_s(buffer, "%.2f %s", size, units[unitIndex]);
    return FString(buffer);
}

void UContentBrowserWindow::CreateNewParticleSystem()
{
    // 빈 ParticleSystem 생성
    UParticleSystem* NewSystem = NewObject<UParticleSystem>();
    NewSystem->ObjectName = FName("NewParticleSystem");

    // 저장 경로 생성 (중복 이름 체크)
    std::filesystem::path ParticlePath = CurrentPath / "NewParticleSystem.particle";
    int Counter = 1;
    while (std::filesystem::exists(ParticlePath))
    {
        std::string FileName = "NewParticleSystem_" + std::to_string(Counter) + ".particle";
        ParticlePath = CurrentPath / FileName;
        Counter++;
    }

    // ParticleViewerWindow를 열고 저장 경로 전달
    FString SavePath = WideToUTF8(ParticlePath.wstring());
    USlateManager::GetInstance().OpenParticleViewerWithSystem(NewSystem, SavePath);
}