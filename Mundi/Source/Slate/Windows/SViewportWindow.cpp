#include "pch.h"
#include "SViewportWindow.h"
#include "World.h"
#include "ImGui/imgui.h"
#include "USlateManager.h"

#include "FViewport.h"
#include "FViewportClient.h"
#include "Texture.h"
#include "Gizmo/GizmoActor.h"

#include "CameraComponent.h"
#include "CameraActor.h"
#include "StatsOverlayD2D.h"

#include "StaticMeshActor.h"
//#include "SkeletalMeshActor.h"
#include "ResourceManager.h"
#include <filesystem>

extern float CLIENTWIDTH;
extern float CLIENTHEIGHT;

SViewportWindow::SViewportWindow()
{
	ViewportType = EViewportType::Perspective;
	bIsActive = false;
	bIsMouseDown = false;
}

SViewportWindow::~SViewportWindow()
{
	if (Viewport)
	{
		delete Viewport;
		Viewport = nullptr;
	}

	if (ViewportClient)
	{
		delete ViewportClient;
		ViewportClient = nullptr;
	}

	IconSelect = nullptr;
	IconMove = nullptr;
	IconRotate = nullptr;
	IconScale = nullptr;
	IconWorldSpace = nullptr;
	IconLocalSpace = nullptr;

	IconCamera = nullptr;
	IconPerspective = nullptr;
	IconTop = nullptr;
	IconBottom = nullptr;
	IconLeft = nullptr;
	IconRight = nullptr;
	IconFront = nullptr;
	IconBack = nullptr;

	IconSpeed = nullptr;
	IconFOV = nullptr;
	IconNearClip = nullptr;
	IconFarClip = nullptr;

	IconViewMode_Lit = nullptr;
	IconViewMode_Unlit = nullptr;
	IconViewMode_Wireframe = nullptr;
	IconViewMode_BufferVis = nullptr;

	IconShowFlag = nullptr;
	IconRevert = nullptr;
	IconStats = nullptr;
	IconHide = nullptr;
	IconBVH = nullptr;
	IconGrid = nullptr;
	IconDecal = nullptr;
	IconStaticMesh = nullptr;
	IconBillboard = nullptr;
	IconEditorIcon = nullptr;
	IconFog = nullptr;
	IconCollision = nullptr;
	IconAntiAliasing = nullptr;
	IconTile = nullptr;

	IconSingleToMultiViewport = nullptr;
	IconMultiToSingleViewport = nullptr;
}

bool SViewportWindow::Initialize(float StartX, float StartY, float Width, float Height, UWorld* World, ID3D11Device* Device, EViewportType InViewportType)
{
	ViewportType = InViewportType;

	// 이름 설정
	switch (ViewportType)
	{
	case EViewportType::Perspective:		ViewportName = "원근"; break;
	case EViewportType::Orthographic_Front: ViewportName = "정면"; break;
	case EViewportType::Orthographic_Left:  ViewportName = "왼쪽"; break;
	case EViewportType::Orthographic_Top:   ViewportName = "상단"; break;
	case EViewportType::Orthographic_Back:	ViewportName = "후면"; break;
	case EViewportType::Orthographic_Right:  ViewportName = "오른쪽"; break;
	case EViewportType::Orthographic_Bottom:   ViewportName = "하단"; break;
	}

	// FViewport 생성
	Viewport = new FViewport();
	if (!Viewport->Initialize(StartX, StartY, Width, Height, Device))
	{
		delete Viewport;
		Viewport = nullptr;
		return false;
	}

	// FViewportClient 생성
	ViewportClient = new FViewportClient();
	ViewportClient->SetViewportType(ViewportType);
	ViewportClient->SetWorld(World); // 전역 월드 연결 (이미 있다고 가정)

	// 양방향 연결
	Viewport->SetViewportClient(ViewportClient);

	// 툴바 아이콘 로드
	LoadToolbarIcons(Device);

	return true;
}

void SViewportWindow::OnRender()
{
	// Slate(UI)만 처리하고 렌더는 FViewport에 위임
	RenderToolbar();

	if (Viewport)
		Viewport->Render();

	// 드래그 앤 드롭 타겟 영역 (뷰포트 전체)
	HandleDropTarget();
}

void SViewportWindow::OnUpdate(float DeltaSeconds)
{
	if (!Viewport)
		return;

	if (!Viewport) return;

	// 툴바 높이만큼 뷰포트 영역 조정

	uint32 NewStartX = static_cast<uint32>(Rect.Left);
	uint32 NewStartY = static_cast<uint32>(Rect.Top);
	uint32 NewWidth = static_cast<uint32>(Rect.Right - Rect.Left);
	uint32 NewHeight = static_cast<uint32>(Rect.Bottom - Rect.Top);

	Viewport->Resize(NewStartX, NewStartY, NewWidth, NewHeight);
	ViewportClient->Tick(DeltaSeconds);
}

void SViewportWindow::OnMouseMove(FVector2D MousePos)
{
	if (!Viewport) return;

	// 툴바 영역 아래에서만 마우스 이벤트 처리
	FVector2D LocalPos = MousePos - FVector2D(Rect.Left, Rect.Top);
	Viewport->ProcessMouseMove((int32)LocalPos.X, (int32)LocalPos.Y);
}

void SViewportWindow::OnMouseDown(FVector2D MousePos, uint32 Button)
{
	if (!Viewport) return;

	// 툴바 영역 아래에서만 마우스 이벤트 처리s
	bIsMouseDown = true;
	FVector2D LocalPos = MousePos - FVector2D(Rect.Left, Rect.Top);
	Viewport->ProcessMouseButtonDown((int32)LocalPos.X, (int32)LocalPos.Y, Button);

}

void SViewportWindow::OnMouseUp(FVector2D MousePos, uint32 Button)
{
	if (!Viewport) return;

	bIsMouseDown = false;
	FVector2D LocalPos = MousePos - FVector2D(Rect.Left, Rect.Top);
	Viewport->ProcessMouseButtonUp((int32)LocalPos.X, (int32)LocalPos.Y, Button);
}

void SViewportWindow::SetVClientWorld(UWorld* InWorld)
{
	if (ViewportClient && InWorld)
	{
		ViewportClient->SetWorld(InWorld);
	}
}

void SViewportWindow::RenderToolbar()
{
	if (!Viewport) return;

	// 툴바 영역 크기
	float ToolbarHeight = 35.0f;
	ImVec2 ToolbarPosition(Rect.Left, Rect.Top);
	ImVec2 ToolbarSize(Rect.Right - Rect.Left, ToolbarHeight);

	// 툴바 위치 지정
	ImGui::SetNextWindowPos(ToolbarPosition);
	ImGui::SetNextWindowSize(ToolbarSize);

	// 뷰포트별 고유한 윈도우 ID
	char WindowId[64];
	sprintf_s(WindowId, "ViewportToolbar_%p", this);

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (ImGui::Begin(WindowId, nullptr, WindowFlags))
	{
		// 기즈모 버튼 스타일 설정
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));      // 간격 좁히기
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);            // 모서리 둥글게
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));        // 배경 투명
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f)); // 호버 배경

		// 기즈모 모드 버튼들 렌더링
		RenderGizmoModeButtons();

		// 구분선
		ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "|");
		ImGui::SameLine();

		// 기즈모 스페이스 버튼 렌더링
		RenderGizmoSpaceButton();

		// 기즈모 버튼 스타일 복원
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);

		// === 오른쪽 정렬 버튼들 ===
		// Switch, ShowFlag, ViewMode는 오른쪽 고정, Camera는 ViewMode 너비에 따라 왼쪽으로 밀림

		const float SwitchButtonWidth = 33.0f;  // Switch 버튼
		const float ShowFlagButtonWidth = 50.0f;  // ShowFlag 버튼 (대략)
		const float ButtonSpacing = 8.0f;

		// 현재 ViewMode 이름으로 실제 너비 계산
		const char* CurrentViewModeName = "뷰모드";
		if (ViewportClient)
		{
			EViewMode CurrentViewMode = ViewportClient->GetViewMode();
			switch (CurrentViewMode)
			{
			case EViewMode::VMI_Lit_Gouraud:
			case EViewMode::VMI_Lit_Lambert:
			case EViewMode::VMI_Lit_Phong:
				CurrentViewModeName = "라이팅 포함";
				break;
			case EViewMode::VMI_Unlit:
				CurrentViewModeName = "언릿";
				break;
			case EViewMode::VMI_Wireframe:
				CurrentViewModeName = "와이어프레임";
				break;
			case EViewMode::VMI_WorldNormal:
				CurrentViewModeName = "월드 노멀";
				break;
			case EViewMode::VMI_SceneDepth:
				CurrentViewModeName = "씬 뎁스";
				break;
			}
		}

		// ViewMode 버튼의 실제 너비 계산
		char viewModeText[64];
		sprintf_s(viewModeText, "%s %s", CurrentViewModeName, "∨");
		ImVec2 viewModeTextSize = ImGui::CalcTextSize(viewModeText);
		const float ViewModeButtonWidth = 17.0f + 4.0f + viewModeTextSize.x + 16.0f;

		// Camera 버튼 너비 계산
		char cameraText[64];
		sprintf_s(cameraText, "%s %s", ViewportName.ToString().c_str(), "∨");
		ImVec2 cameraTextSize = ImGui::CalcTextSize(cameraText);
		const float CameraButtonWidth = 17.0f + 4.0f + cameraTextSize.x + 16.0f;
		// 사용 가능한 전체 너비와 현재 커서 위치
		float AvailableWidth = ImGui::GetContentRegionAvail().x;
		float CursorStartX = ImGui::GetCursorPosX();
		ImVec2 CurrentCursor = ImGui::GetCursorPos();


		// 오른쪽부터 역순으로 위치 계산
		// Switch는 오른쪽 끝
		float SwitchX = CursorStartX + AvailableWidth - SwitchButtonWidth;

		// ShowFlag는 Switch 왼쪽
		float ShowFlagX = SwitchX - ButtonSpacing - ShowFlagButtonWidth;

		// ViewMode는 ShowFlag 왼쪽 (실제 너비 사용)
		float ViewModeX = ShowFlagX - ButtonSpacing - ViewModeButtonWidth;

		// Camera는 ViewMode 왼쪽 (ViewMode 너비에 따라 위치 변동)
		float CameraX = ViewModeX - ButtonSpacing - CameraButtonWidth;

		// 버튼들을 순서대로 그리기 (Y 위치는 동일하게 유지)
		ImGui::SetCursorPos(ImVec2(CameraX, CurrentCursor.y));
		RenderCameraOptionDropdownMenu();

		ImGui::SetCursorPos(ImVec2(ViewModeX, CurrentCursor.y));
		RenderViewModeDropdownMenu();

		ImGui::SetCursorPos(ImVec2(ShowFlagX, CurrentCursor.y));
		RenderShowFlagDropdownMenu();

		ImGui::SetCursorPos(ImVec2(SwitchX, CurrentCursor.y));
		RenderViewportLayoutSwitchButton();
	}
	ImGui::End();
}

void SViewportWindow::LoadToolbarIcons(ID3D11Device* Device)
{
	if (!Device) return;

	// 기즈모 아이콘 텍스처 생성 및 로드
	IconSelect = NewObject<UTexture>();
	IconSelect->Load(GDataDir + "/Icon/Viewport_Toolbar_Select.png", Device);

	IconMove = NewObject<UTexture>();
	IconMove->Load(GDataDir + "/Icon/Viewport_Toolbar_Move.png", Device);

	IconRotate = NewObject<UTexture>();
	IconRotate->Load(GDataDir + "/Icon/Viewport_Toolbar_Rotate.png", Device);

	IconScale = NewObject<UTexture>();
	IconScale->Load(GDataDir + "/Icon/Viewport_Toolbar_Scale.png", Device);

	IconWorldSpace = NewObject<UTexture>();
	IconWorldSpace->Load(GDataDir + "/Icon/Viewport_Toolbar_WorldSpace.png", Device);

	IconLocalSpace = NewObject<UTexture>();
	IconLocalSpace->Load(GDataDir + "/Icon/Viewport_Toolbar_LocalSpace.png", Device);

	// 뷰포트 모드 아이콘 텍스처 로드
	IconCamera = NewObject<UTexture>();
	IconCamera->Load(GDataDir + "/Icon/Viewport_Mode_Camera.png", Device);

	IconPerspective = NewObject<UTexture>();
	IconPerspective->Load(GDataDir + "/Icon/Viewport_Mode_Perspective.png", Device);

	IconTop = NewObject<UTexture>();
	IconTop->Load(GDataDir + "/Icon/Viewport_Mode_Top.png", Device);

	IconBottom = NewObject<UTexture>();
	IconBottom->Load(GDataDir + "/Icon/Viewport_Mode_Bottom.png", Device);

	IconLeft = NewObject<UTexture>();
	IconLeft->Load(GDataDir + "/Icon/Viewport_Mode_Left.png", Device);

	IconRight = NewObject<UTexture>();
	IconRight->Load(GDataDir + "/Icon/Viewport_Mode_Right.png", Device);

	IconFront = NewObject<UTexture>();
	IconFront->Load(GDataDir + "/Icon/Viewport_Mode_Front.png", Device);

	IconBack = NewObject<UTexture>();
	IconBack->Load(GDataDir + "/Icon/Viewport_Mode_Back.png", Device);

	// 뷰포트 설정 아이콘 텍스처 로드
	IconSpeed = NewObject<UTexture>();
	IconSpeed->Load(GDataDir + "/Icon/Viewport_Mode_Camera.png", Device);

	IconFOV = NewObject<UTexture>();
	IconFOV->Load(GDataDir + "/Icon/Viewport_Setting_FOV.png", Device);

	IconNearClip = NewObject<UTexture>();
	IconNearClip->Load(GDataDir + "/Icon/Viewport_Setting_NearClip.png", Device);

	IconFarClip = NewObject<UTexture>();
	IconFarClip->Load(GDataDir + "/Icon/Viewport_Setting_FarClip.png", Device);

	// 뷰모드 아이콘 텍스처 로드
	IconViewMode_Lit = NewObject<UTexture>();
	IconViewMode_Lit->Load(GDataDir + "/Icon/Viewport_ViewMode_Lit.png", Device);

	IconViewMode_Unlit = NewObject<UTexture>();
	IconViewMode_Unlit->Load(GDataDir + "/Icon/Viewport_ViewMode_Unlit.png", Device);

	IconViewMode_Wireframe = NewObject<UTexture>();
	IconViewMode_Wireframe->Load(GDataDir + "/Icon/Viewport_Toolbar_WorldSpace.png", Device);

	IconViewMode_BufferVis = NewObject<UTexture>();
	IconViewMode_BufferVis->Load(GDataDir + "/Icon/Viewport_ViewMode_BufferVis.png", Device);

	// ShowFlag 아이콘 텍스처 로드
	IconShowFlag = NewObject<UTexture>();
	IconShowFlag->Load(GDataDir + "/Icon/Viewport_ShowFlag.png", Device);

	IconRevert = NewObject<UTexture>();
	IconRevert->Load(GDataDir + "/Icon/Viewport_Revert.png", Device);

	IconStats = NewObject<UTexture>();
	IconStats->Load(GDataDir + "/Icon/Viewport_Stats.png", Device);

	IconHide = NewObject<UTexture>();
	IconHide->Load(GDataDir + "/Icon/Viewport_Hide.png", Device);

	IconBVH = NewObject<UTexture>();
	IconBVH->Load(GDataDir + "/Icon/Viewport_BVH.png", Device);

	IconGrid = NewObject<UTexture>();
	IconGrid->Load(GDataDir + "/Icon/Viewport_Grid.png", Device);

	IconDecal = NewObject<UTexture>();
	IconDecal->Load(GDataDir + "/Icon/Viewport_Decal.png", Device);

	IconStaticMesh = NewObject<UTexture>();
	IconStaticMesh->Load(GDataDir + "/Icon/Viewport_StaticMesh.png", Device);

	IconSkeletalMesh = NewObject<UTexture>();
	IconSkeletalMesh->Load(GDataDir + "/Icon/Viewport_SkeletalMesh.png", Device);

	IconBillboard = NewObject<UTexture>();
	IconBillboard->Load(GDataDir + "/Icon/Viewport_Billboard.png", Device);

	IconEditorIcon = NewObject<UTexture>();
	IconEditorIcon->Load(GDataDir + "/Icon/Viewport_EditorIcon.png", Device);

	IconFog = NewObject<UTexture>();
	IconFog->Load(GDataDir + "/Icon/Viewport_Fog.png", Device);

	IconCollision = NewObject<UTexture>();
	IconCollision->Load(GDataDir + "/Icon/Viewport_Collision.png", Device);

	IconAntiAliasing = NewObject<UTexture>();
	IconAntiAliasing->Load(GDataDir + "/Icon/Viewport_AntiAliasing.png", Device);

	IconTile = NewObject<UTexture>();
	IconTile->Load(GDataDir + "/Icon/Viewport_Tile.png", Device);

	IconShadow = NewObject<UTexture>();
	IconShadow->Load(GDataDir + "/Icon/Viewport_Shadow.png", Device);
	
	IconParticle = NewObject<UTexture>();
	IconParticle->Load(GDataDir + "/Icon/Viewport_Particle.png", Device);

	IconShadowAA = NewObject<UTexture>();
	IconShadowAA->Load(GDataDir + "/Icon/Viewport_ShadowAA.png", Device);

	IconSkinning = NewObject<UTexture>();
	IconSkinning->Load(GDataDir + "/Icon/Viewport_Skinning.png", Device);

	// 뷰포트 레이아웃 전환 아이콘 로드
	IconSingleToMultiViewport = NewObject<UTexture>();
	IconSingleToMultiViewport->Load(GDataDir + "/Icon/Viewport_SingleToMultiViewport.png", Device);
	IconMultiToSingleViewport = NewObject<UTexture>();
	IconMultiToSingleViewport->Load(GDataDir + "/Icon/Viewport_MultiToSingleViewport.png", Device);
}

void SViewportWindow::RenderGizmoModeButtons()
{
	ImVec2 switchCursorPos = ImGui::GetCursorPos();
	ImGui::SetCursorPosY(switchCursorPos.y - 2.0f);

	const ImVec2 IconSize(17, 17);

	// GizmoActor에서 직접 현재 모드 가져오기
	EGizmoMode CurrentGizmoMode = EGizmoMode::Select;
	AGizmoActor* GizmoActor = nullptr;
	if (ViewportClient && ViewportClient->GetWorld())
	{
		GizmoActor = ViewportClient->GetWorld()->GetGizmoActor();
		if (GizmoActor)
		{
			CurrentGizmoMode = GizmoActor->GetMode();
		}
	}

	// Select 버튼
	bool bIsSelectActive = (CurrentGizmoMode == EGizmoMode::Select);
	ImVec4 SelectTintColor = bIsSelectActive ? ImVec4(0.3f, 0.6f, 1.0f, 1.0f) : ImVec4(1, 1, 1, 1);

	if (IconSelect && IconSelect->GetShaderResourceView())
	{
		if (ImGui::ImageButton("##SelectBtn", (void*)IconSelect->GetShaderResourceView(), IconSize,
			ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), SelectTintColor))
		{
			if (GizmoActor)
			{
				GizmoActor->SetMode(EGizmoMode::Select);
			}
		}
	}
	else
	{
		if (ImGui::Button("Select", ImVec2(60, 0)))
		{
			if (GizmoActor)
			{
				GizmoActor->SetMode(EGizmoMode::Select);
			}
		}
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("오브젝트를 선택합니다. [Q]");
	}
	ImGui::SameLine();

	// Move 버튼
	bool bIsMoveActive = (CurrentGizmoMode == EGizmoMode::Translate);
	ImVec4 MoveTintColor = bIsMoveActive ? ImVec4(0.3f, 0.6f, 1.0f, 1.0f) : ImVec4(1, 1, 1, 1);

	if (IconMove && IconMove->GetShaderResourceView())
	{
		if (ImGui::ImageButton("##MoveBtn", (void*)IconMove->GetShaderResourceView(), IconSize,
			ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), MoveTintColor))
		{
			if (GizmoActor)
			{
				GizmoActor->SetMode(EGizmoMode::Translate);
			}
		}
	}
	else
	{
		if (ImGui::Button("Move", ImVec2(60, 0)))
		{
			if (GizmoActor)
			{
				GizmoActor->SetMode(EGizmoMode::Translate);
			}
		}
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("오브젝트를 선택하고 이동시킵니다. [W]");
	}
	ImGui::SameLine();

	// Rotate 버튼
	bool bIsRotateActive = (CurrentGizmoMode == EGizmoMode::Rotate);
	ImVec4 RotateTintColor = bIsRotateActive ? ImVec4(0.3f, 0.6f, 1.0f, 1.0f) : ImVec4(1, 1, 1, 1);

	if (IconRotate && IconRotate->GetShaderResourceView())
	{
		if (ImGui::ImageButton("##RotateBtn", (void*)IconRotate->GetShaderResourceView(), IconSize,
			ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), RotateTintColor))
		{
			if (GizmoActor)
			{
				GizmoActor->SetMode(EGizmoMode::Rotate);
			}
		}
	}
	else
	{
		if (ImGui::Button("Rotate", ImVec2(60, 0)))
		{
			if (GizmoActor)
			{
				GizmoActor->SetMode(EGizmoMode::Rotate);
			}
		}
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("오브젝트를 선택하고 회전시킵니다. [E]");
	}
	ImGui::SameLine();

	// Scale 버튼
	bool bIsScaleActive = (CurrentGizmoMode == EGizmoMode::Scale);
	ImVec4 ScaleTintColor = bIsScaleActive ? ImVec4(0.3f, 0.6f, 1.0f, 1.0f) : ImVec4(1, 1, 1, 1);

	if (IconScale && IconScale->GetShaderResourceView())
	{
		if (ImGui::ImageButton("##ScaleBtn", (void*)IconScale->GetShaderResourceView(), IconSize,
			ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ScaleTintColor))
		{
			if (GizmoActor)
			{
				GizmoActor->SetMode(EGizmoMode::Scale);
			}
		}
	}
	else
	{
		if (ImGui::Button("Scale", ImVec2(60, 0)))
		{
			if (GizmoActor)
			{
				GizmoActor->SetMode(EGizmoMode::Scale);
			}
		}
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("오브젝트를 선택하고 스케일을 조절합니다. [R]");
	}

	ImGui::SameLine();
}

void SViewportWindow::RenderGizmoSpaceButton()
{
	const ImVec2 IconSize(17, 17);

	// GizmoActor에서 직접 현재 스페이스 가져오기
	EGizmoSpace CurrentGizmoSpace = EGizmoSpace::World;
	AGizmoActor* GizmoActor = nullptr;
	if (ViewportClient && ViewportClient->GetWorld())
	{
		GizmoActor = ViewportClient->GetWorld()->GetGizmoActor();
		if (GizmoActor)
		{
			CurrentGizmoSpace = GizmoActor->GetSpace();
		}
	}

	// 현재 스페이스에 따라 적절한 아이콘 표시
	bool bIsWorldSpace = (CurrentGizmoSpace == EGizmoSpace::World);
	UTexture* CurrentIcon = bIsWorldSpace ? IconWorldSpace : IconLocalSpace;
	const char* TooltipText = bIsWorldSpace ? "월드 스페이스 좌표 [Tab]" : "로컬 스페이스 좌표 [Tab]";

	// 선택 상태 tint (월드/로컬 모두 동일하게 흰색)
	ImVec4 TintColor = ImVec4(1, 1, 1, 1);

	if (CurrentIcon && CurrentIcon->GetShaderResourceView())
	{
		if (ImGui::ImageButton("##GizmoSpaceBtn", (void*)CurrentIcon->GetShaderResourceView(), IconSize,
			ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), TintColor))
		{
			// 버튼 클릭 시 스페이스 전환
			if (GizmoActor)
			{
				EGizmoSpace NewSpace = bIsWorldSpace ? EGizmoSpace::Local : EGizmoSpace::World;
				GizmoActor->SetSpace(NewSpace);
			}
		}
	}
	else
	{
		// 아이콘이 없는 경우 텍스트 버튼
		const char* ButtonText = bIsWorldSpace ? "World" : "Local";
		if (ImGui::Button(ButtonText, ImVec2(60, 0)))
		{
			if (GizmoActor)
			{
				EGizmoSpace NewSpace = bIsWorldSpace ? EGizmoSpace::Local : EGizmoSpace::World;
				GizmoActor->SetSpace(NewSpace);
			}
		}
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s", TooltipText);
	}

	ImGui::SameLine();
}

void SViewportWindow::RenderCameraOptionDropdownMenu()
{
	ImVec2 cursorPos = ImGui::GetCursorPos();
	ImGui::SetCursorPosY(cursorPos.y - 0.7f);

	const ImVec2 IconSize(17, 17);

	// 드롭다운 버튼 텍스트 준비
	char ButtonText[64];
	sprintf_s(ButtonText, "%s %s", ViewportName.ToString().c_str(), "∨");

	// 버튼 너비 계산 (아이콘 크기 + 간격 + 텍스트 크기 + 좌우 패딩)
	ImVec2 TextSize = ImGui::CalcTextSize(ButtonText);
	const float HorizontalPadding = 8.0f;
	const float CameraDropdownWidth = IconSize.x + 4.0f + TextSize.x + HorizontalPadding * 2.0f;

	// 드롭다운 버튼 스타일 적용
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.16f, 0.16f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.21f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.28f, 0.26f, 1.00f));

	// 드롭다운 버튼 생성 (카메라 아이콘 + 현재 모드명 + 화살표)
	ImVec2 ButtonSize(CameraDropdownWidth, ImGui::GetFrameHeight());
	ImVec2 ButtonCursorPos = ImGui::GetCursorPos();

	// 버튼 클릭 영역
	if (ImGui::Button("##ViewportModeBtn", ButtonSize))
	{
		ImGui::OpenPopup("ViewportModePopup");
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("카메라 옵션");
	}

	// 버튼 위에 내용 렌더링 (아이콘 + 텍스트, 가운데 정렬)
	float ButtonContentWidth = IconSize.x + 4.0f + TextSize.x;
	float ButtonContentStartX = ButtonCursorPos.x + (ButtonSize.x - ButtonContentWidth) * 0.5f;
	ImVec2 ButtonContentCursorPos = ImVec2(ButtonContentStartX, ButtonCursorPos.y + (ButtonSize.y - IconSize.y) * 0.5f);
	ImGui::SetCursorPos(ButtonContentCursorPos);

	// 현재 뷰포트 모드에 따라 아이콘 선택
	UTexture* CurrentModeIcon = nullptr;
	switch (ViewportType)
	{
	case EViewportType::Perspective:
		CurrentModeIcon = IconCamera;
		break;
	case EViewportType::Orthographic_Top:
		CurrentModeIcon = IconTop;
		break;
	case EViewportType::Orthographic_Bottom:
		CurrentModeIcon = IconBottom;
		break;
	case EViewportType::Orthographic_Left:
		CurrentModeIcon = IconLeft;
		break;
	case EViewportType::Orthographic_Right:
		CurrentModeIcon = IconRight;
		break;
	case EViewportType::Orthographic_Front:
		CurrentModeIcon = IconFront;
		break;
	case EViewportType::Orthographic_Back:
		CurrentModeIcon = IconBack;
		break;
	default:
		CurrentModeIcon = IconCamera;
		break;
	}

	if (CurrentModeIcon && CurrentModeIcon->GetShaderResourceView())
	{
		ImGui::Image((void*)CurrentModeIcon->GetShaderResourceView(), IconSize);
		ImGui::SameLine(0, 4);
	}

	ImGui::Text("%s", ButtonText);

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(1);

	// ===== 뷰포트 모드 드롭다운 팝업 =====
	if (ImGui::BeginPopup("ViewportModePopup", ImGuiWindowFlags_NoMove))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

		// 선택된 항목의 파란 배경 제거
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.3f, 0.3f, 0.6f));

		// --- 섹션 1: 원근 ---
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "원근");
		ImGui::Separator();

		bool bIsPerspective = (ViewportType == EViewportType::Perspective);
		const char* RadioIcon = bIsPerspective ? "●" : "○";

		// 원근 모드 선택 항목 (라디오 버튼 + 아이콘 + 텍스트 통합)
		ImVec2 SelectableSize(180, 20);
		ImVec2 SelectableCursorPos = ImGui::GetCursorPos();

		if (ImGui::Selectable("##Perspective", bIsPerspective, 0, SelectableSize))
		{
			ViewportType = EViewportType::Perspective;
			ViewportName = "원근";
			if (ViewportClient)
			{
				ViewportClient->SetViewportType(ViewportType);
				ViewportClient->SetupCameraMode();
			}
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("뷰포트를 원근 보기로 전환합니다.");
		}

		// Selectable 위에 내용 렌더링
		ImVec2 ContentPos = ImVec2(SelectableCursorPos.x + 4, SelectableCursorPos.y + (SelectableSize.y - IconSize.y) * 0.5f);
		ImGui::SetCursorPos(ContentPos);

		ImGui::Text("%s", RadioIcon);
		ImGui::SameLine(0, 4);

		if (IconPerspective && IconPerspective->GetShaderResourceView())
		{
			ImGui::Image((void*)IconPerspective->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		ImGui::Text("원근");

		// --- 섹션 2: 직교 ---
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "직교");
		ImGui::Separator();

		// 직교 모드 목록
		struct ViewportModeEntry {
			EViewportType type;
			const char* koreanName;
			UTexture** icon;
			const char* tooltip;
		};

		ViewportModeEntry orthographicModes[] = {
			{ EViewportType::Orthographic_Top, "상단", &IconTop, "뷰포트를 상단 보기로 전환합니다." },
			{ EViewportType::Orthographic_Bottom, "하단", &IconBottom, "뷰포트를 하단 보기로 전환합니다." },
			{ EViewportType::Orthographic_Left, "왼쪽", &IconLeft, "뷰포트를 왼쪽 보기로 전환합니다." },
			{ EViewportType::Orthographic_Right, "오른쪽", &IconRight, "뷰포트를 오른쪽 보기로 전환합니다." },
			{ EViewportType::Orthographic_Front, "정면", &IconFront, "뷰포트를 정면 보기로 전환합니다." },
			{ EViewportType::Orthographic_Back, "후면", &IconBack, "뷰포트를 후면 보기로 전환합니다." }
		};

		for (int i = 0; i < 6; i++)
		{
			const auto& mode = orthographicModes[i];
			bool bIsSelected = (ViewportType == mode.type);
			const char* RadioIcon = bIsSelected ? "●" : "○";

			// 직교 모드 선택 항목 (라디오 버튼 + 아이콘 + 텍스트 통합)
			char SelectableID[32];
			sprintf_s(SelectableID, "##Ortho%d", i);

			ImVec2 OrthoSelectableCursorPos = ImGui::GetCursorPos();

			if (ImGui::Selectable(SelectableID, bIsSelected, 0, SelectableSize))
			{
				ViewportType = mode.type;
				ViewportName = mode.koreanName;
				if (ViewportClient)
				{
					ViewportClient->SetViewportType(ViewportType);
					ViewportClient->SetupCameraMode();
				}
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", mode.tooltip);
			}

			// Selectable 위에 내용 렌더링
			ImVec2 OrthoContentPos = ImVec2(OrthoSelectableCursorPos.x + 4, OrthoSelectableCursorPos.y + (SelectableSize.y - IconSize.y) * 0.5f);
			ImGui::SetCursorPos(OrthoContentPos);

			ImGui::Text("%s", RadioIcon);
			ImGui::SameLine(0, 4);

			if (*mode.icon && (*mode.icon)->GetShaderResourceView())
			{
				ImGui::Image((void*)(*mode.icon)->GetShaderResourceView(), IconSize);
				ImGui::SameLine(0, 4);
			}

			ImGui::Text("%s", mode.koreanName);
		}

		// --- 섹션 3: 이동 ---
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "이동");
		ImGui::Separator();

		ACameraActor* Camera = ViewportClient ? ViewportClient->GetCamera() : nullptr;
		if (Camera)
		{
			if (IconSpeed && IconSpeed->GetShaderResourceView())
			{
				ImGui::Image((void*)IconSpeed->GetShaderResourceView(), IconSize);
				ImGui::SameLine();
			}
			ImGui::Text("카메라 이동 속도");

			float speed = Camera->GetCameraSpeed();
			ImGui::SetNextItemWidth(180);
			if (ImGui::SliderFloat("##CameraSpeed", &speed, 1.0f, 100.0f, "%.1f"))
			{
				Camera->SetCameraSpeed(speed);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("WASD 키로 카메라를 이동할 때의 속도 (1-100)");
			}
		}

		// --- 섹션 4: 뷰 ---
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "뷰");
		ImGui::Separator();

		if (Camera && Camera->GetCameraComponent())
		{
			UCameraComponent* camComp = Camera->GetCameraComponent();

			// FOV
			if (IconFOV && IconFOV->GetShaderResourceView())
			{
				ImGui::Image((void*)IconFOV->GetShaderResourceView(), IconSize);
				ImGui::SameLine();
			}
			ImGui::Text("필드 오브 뷰");

			float fov = camComp->GetFOV();
			ImGui::SetNextItemWidth(180);
			if (ImGui::SliderFloat("##FOV", &fov, 30.0f, 120.0f, "%.1f"))
			{
				camComp->SetFOV(fov);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("카메라 시야각 (30-120도)\n값이 클수록 넓은 범위가 보입니다");
			}

			// 근평면
			if (IconNearClip && IconNearClip->GetShaderResourceView())
			{
				ImGui::Image((void*)IconNearClip->GetShaderResourceView(), IconSize);
				ImGui::SameLine();
			}
			ImGui::Text("근평면");

			float nearClip = camComp->GetNearClip();
			ImGui::SetNextItemWidth(180);
			if (ImGui::SliderFloat("##NearClip", &nearClip, 0.01f, 10.0f, "%.2f"))
			{
				camComp->SetClipPlanes(nearClip, camComp->GetFarClip());
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("카메라에서 가장 가까운 렌더링 거리 (0.01-10)\n이 값보다 가까운 오브젝트는 보이지 않습니다");
			}

			// 원평면
			if (IconFarClip && IconFarClip->GetShaderResourceView())
			{
				ImGui::Image((void*)IconFarClip->GetShaderResourceView(), IconSize);
				ImGui::SameLine();
			}
			ImGui::Text("원평면");

			float farClip = camComp->GetFarClip();
			ImGui::SetNextItemWidth(180);
			if (ImGui::SliderFloat("##FarClip", &farClip, 10.0f, 1000.0f, "%.0f"))
			{
				camComp->SetClipPlanes(camComp->GetNearClip(), farClip);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("카메라에서 가장 먼 렌더링 거리 (100-10000)\n이 값보다 먼 오브젝트는 보이지 않습니다");
			}
		}

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		ImGui::EndPopup();
	}
}

void SViewportWindow::RenderViewModeDropdownMenu()
{
	if (!ViewportClient) return;

	ImVec2 cursorPos = ImGui::GetCursorPos();
	ImGui::SetCursorPosY(cursorPos.y - 1.0f);

	const ImVec2 IconSize(17, 17);

	// 현재 뷰모드 이름 및 아이콘 가져오기
	EViewMode CurrentViewMode = ViewportClient->GetViewMode();
	const char* CurrentViewModeName = "뷰모드";
	UTexture* CurrentViewModeIcon = nullptr;

	switch (CurrentViewMode)
	{
	case EViewMode::VMI_Lit_Gouraud:
	case EViewMode::VMI_Lit_Lambert:
	case EViewMode::VMI_Lit_Phong:
		CurrentViewModeName = "라이팅 포함";
		CurrentViewModeIcon = IconViewMode_Lit;
		break;
	case EViewMode::VMI_Unlit:
		CurrentViewModeName = "언릿";
		CurrentViewModeIcon = IconViewMode_Unlit;
		break;
	case EViewMode::VMI_Wireframe:
		CurrentViewModeName = "와이어프레임";
		CurrentViewModeIcon = IconViewMode_Wireframe;
		break;
	case EViewMode::VMI_WorldNormal:
		CurrentViewModeName = "월드 노멀";
		CurrentViewModeIcon = IconViewMode_BufferVis;
		break;
	case EViewMode::VMI_SceneDepth:
		CurrentViewModeName = "씬 뎁스";
		CurrentViewModeIcon = IconViewMode_BufferVis;
		break;
	}

	// 드롭다운 버튼 텍스트 준비
	char ButtonText[64];
	sprintf_s(ButtonText, "%s %s", CurrentViewModeName, "∨");

	// 버튼 너비 계산 (아이콘 크기 + 간격 + 텍스트 크기 + 좌우 패딩)
	ImVec2 TextSize = ImGui::CalcTextSize(ButtonText);
	const float Padding = 8.0f;
	const float DropdownWidth = IconSize.x + 4.0f + TextSize.x + Padding * 2.0f;

	// 스타일 적용
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.16f, 0.16f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.21f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.28f, 0.26f, 1.00f));

	// 드롭다운 버튼 생성 (아이콘 + 텍스트)
	ImVec2 ButtonSize(DropdownWidth, ImGui::GetFrameHeight());
	ImVec2 ButtonCursorPos = ImGui::GetCursorPos();

	// 버튼 클릭 영역
	if (ImGui::Button("##ViewModeBtn", ButtonSize))
	{
		ImGui::OpenPopup("ViewModePopup");
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("뷰모드 선택");
	}

	// 버튼 위에 내용 렌더링 (아이콘 + 텍스트, 가운데 정렬)
	float ButtonContentWidth = IconSize.x + 4.0f + TextSize.x;
	float ButtonContentStartX = ButtonCursorPos.x + (ButtonSize.x - ButtonContentWidth) * 0.5f;
	ImVec2 ButtonContentCursorPos = ImVec2(ButtonContentStartX, ButtonCursorPos.y + (ButtonSize.y - IconSize.y) * 0.5f);
	ImGui::SetCursorPos(ButtonContentCursorPos);

	// 현재 뷰모드 아이콘 표시
	if (CurrentViewModeIcon && CurrentViewModeIcon->GetShaderResourceView())
	{
		ImGui::Image((void*)CurrentViewModeIcon->GetShaderResourceView(), IconSize);
		ImGui::SameLine(0, 4);
	}

	ImGui::Text("%s", ButtonText);

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(1);

	// ===== 뷰모드 드롭다운 팝업 =====
	if (ImGui::BeginPopup("ViewModePopup", ImGuiWindowFlags_NoMove))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

		// 선택된 항목의 파란 배경 제거
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.3f, 0.3f, 0.6f));

		// --- 섹션: 뷰모드 ---
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "뷰모드");
		ImGui::Separator();

		// ===== Lit 메뉴 (서브메뉴 포함) =====
		bool bIsLitMode = (CurrentViewMode == EViewMode::VMI_Lit_Phong ||
			CurrentViewMode == EViewMode::VMI_Lit_Gouraud ||
			CurrentViewMode == EViewMode::VMI_Lit_Lambert);

		const char* LitRadioIcon = bIsLitMode ? "●" : "○";

		// Selectable로 감싸서 전체 호버링 영역 확보
		ImVec2 LitCursorPos = ImGui::GetCursorScreenPos();
		ImVec2 LitSelectableSize(180, IconSize.y);

		// 호버 감지용 투명 Selectable
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		bool bLitHovered = ImGui::Selectable("##LitHoverArea", false, ImGuiSelectableFlags_AllowItemOverlap, LitSelectableSize);
		ImGui::PopStyleColor();

		// Selectable 위에 내용 렌더링 (순서: ● + [텍스처] + 텍스트)
		ImGui::SetCursorScreenPos(LitCursorPos);

		ImGui::Text("%s", LitRadioIcon);
		ImGui::SameLine(0, 4);

		if (IconViewMode_Lit && IconViewMode_Lit->GetShaderResourceView())
		{
			ImGui::Image((void*)IconViewMode_Lit->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		if (ImGui::BeginMenu("라이팅포함"))
		{
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "셰이딩 모델");
			ImGui::Separator();

			// PHONG
			bool bIsPhong = (CurrentViewMode == EViewMode::VMI_Lit_Phong);
			const char* PhongIcon = bIsPhong ? "●" : "○";
			char PhongLabel[32];
			sprintf_s(PhongLabel, "%s PHONG", PhongIcon);
			if (ImGui::MenuItem(PhongLabel))
			{
				ViewportClient->SetViewMode(EViewMode::VMI_Lit_Phong);
				CurrentLitSubMode = 3;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("픽셀 단위 셰이딩 (Per-Pixel)\n스페큘러 하이라이트 포함");
			}

			// GOURAUD
			bool bIsGouraud = (CurrentViewMode == EViewMode::VMI_Lit_Gouraud);
			const char* GouraudIcon = bIsGouraud ? "●" : "○";
			char GouraudLabel[32];
			sprintf_s(GouraudLabel, "%s GOURAUD", GouraudIcon);
			if (ImGui::MenuItem(GouraudLabel))
			{
				ViewportClient->SetViewMode(EViewMode::VMI_Lit_Gouraud);
				CurrentLitSubMode = 1;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("정점 단위 셰이딩 (Per-Vertex)\n부드러운 색상 보간");
			}

			// LAMBERT
			bool bIsLambert = (CurrentViewMode == EViewMode::VMI_Lit_Lambert);
			const char* LambertIcon = bIsLambert ? "●" : "○";
			char LambertLabel[32];
			sprintf_s(LambertLabel, "%s LAMBERT", LambertIcon);
			if (ImGui::MenuItem(LambertLabel))
			{
				ViewportClient->SetViewMode(EViewMode::VMI_Lit_Lambert);
				CurrentLitSubMode = 2;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Diffuse만 계산하는 간단한 셰이딩\n스페큘러 하이라이트 없음");
			}

			ImGui::EndMenu();
		}

		// ===== Unlit 메뉴 =====
		bool bIsUnlit = (CurrentViewMode == EViewMode::VMI_Unlit);
		const char* UnlitRadioIcon = bIsUnlit ? "●" : "○";

		// Selectable로 감싸서 전체 호버링 영역 확보
		ImVec2 UnlitCursorPos = ImGui::GetCursorScreenPos();
		ImVec2 UnlitSelectableSize(180, IconSize.y);

		if (ImGui::Selectable("##UnlitSelectableArea", false, 0, UnlitSelectableSize))
		{
			ViewportClient->SetViewMode(EViewMode::VMI_Unlit);
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("조명 계산 없이 텍스처와 색상만 표시");
		}

		// Selectable 위에 내용 렌더링 (순서: ● + [텍스처] + 텍스트)
		ImGui::SetCursorScreenPos(UnlitCursorPos);

		ImGui::Text("%s", UnlitRadioIcon);
		ImGui::SameLine(0, 4);

		if (IconViewMode_Unlit && IconViewMode_Unlit->GetShaderResourceView())
		{
			ImGui::Image((void*)IconViewMode_Unlit->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		ImGui::Text("언릿");

		// ===== Wireframe 메뉴 =====
		bool bIsWireframe = (CurrentViewMode == EViewMode::VMI_Wireframe);
		const char* WireframeRadioIcon = bIsWireframe ? "●" : "○";

		// Selectable로 감싸서 전체 호버링 영역 확보
		ImVec2 WireframeCursorPos = ImGui::GetCursorScreenPos();
		ImVec2 WireframeSelectableSize(180, IconSize.y);

		if (ImGui::Selectable("##WireframeSelectableArea", false, 0, WireframeSelectableSize))
		{
			ViewportClient->SetViewMode(EViewMode::VMI_Wireframe);
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("메시의 외곽선(에지)만 표시");
		}

		// Selectable 위에 내용 렌더링 (순서: ● + [텍스처] + 텍스트)
		ImGui::SetCursorScreenPos(WireframeCursorPos);

		ImGui::Text("%s", WireframeRadioIcon);
		ImGui::SameLine(0, 4);

		if (IconViewMode_Wireframe && IconViewMode_Wireframe->GetShaderResourceView())
		{
			ImGui::Image((void*)IconViewMode_Wireframe->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		ImGui::Text("와이어프레임");

		// ===== Buffer Visualization 메뉴 (서브메뉴 포함) =====
		bool bIsBufferVis = (CurrentViewMode == EViewMode::VMI_WorldNormal ||
			CurrentViewMode == EViewMode::VMI_SceneDepth);

		const char* BufferVisRadioIcon = bIsBufferVis ? "●" : "○";

		// Selectable로 감싸서 전체 호버링 영역 확보
		ImVec2 BufferVisCursorPos = ImGui::GetCursorScreenPos();
		ImVec2 BufferVisSelectableSize(180, IconSize.y);

		// 호버 감지용 투명 Selectable
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		bool bBufferVisHovered = ImGui::Selectable("##BufferVisHoverArea", false, ImGuiSelectableFlags_AllowItemOverlap, BufferVisSelectableSize);
		ImGui::PopStyleColor();

		// Selectable 위에 내용 렌더링 (순서: ● + [텍스처] + 텍스트)
		ImGui::SetCursorScreenPos(BufferVisCursorPos);

		ImGui::Text("%s", BufferVisRadioIcon);
		ImGui::SameLine(0, 4);

		if (IconViewMode_BufferVis && IconViewMode_BufferVis->GetShaderResourceView())
		{
			ImGui::Image((void*)IconViewMode_BufferVis->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		if (ImGui::BeginMenu("버퍼 시각화"))
		{
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "버퍼 시각화");
			ImGui::Separator();

			// Scene Depth
			bool bIsSceneDepth = (CurrentViewMode == EViewMode::VMI_SceneDepth);
			const char* SceneDepthIcon = bIsSceneDepth ? "●" : "○";
			char SceneDepthLabel[32];
			sprintf_s(SceneDepthLabel, "%s 씬 뎁스", SceneDepthIcon);
			if (ImGui::MenuItem(SceneDepthLabel))
			{
				ViewportClient->SetViewMode(EViewMode::VMI_SceneDepth);
				CurrentBufferVisSubMode = 0;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("씬의 깊이 정보를 그레이스케일로 표시\n어두울수록 카메라에 가까움");
			}

			// World Normal
			bool bIsWorldNormal = (CurrentViewMode == EViewMode::VMI_WorldNormal);
			const char* WorldNormalIcon = bIsWorldNormal ? "●" : "○";
			char WorldNormalLabel[32];
			sprintf_s(WorldNormalLabel, "%s 월드 노멀", WorldNormalIcon);
			if (ImGui::MenuItem(WorldNormalLabel))
			{
				ViewportClient->SetViewMode(EViewMode::VMI_WorldNormal);
				CurrentBufferVisSubMode = 1;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("월드 공간의 노멀 벡터를 RGB로 표시\nR=X, G=Y, B=Z 축 방향");
			}

			ImGui::EndMenu();
		}

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		ImGui::EndPopup();
	}
}

void SViewportWindow::RenderShowFlagDropdownMenu()
{
	if (!ViewportClient) return;

	ImVec2 cursorPos = ImGui::GetCursorPos();
	ImGui::SetCursorPosY(cursorPos.y - 1.0f);

	const ImVec2 IconSize(20, 20);

	// 드롭다운 버튼 텍스트 준비
	char ButtonText[64];
	sprintf_s(ButtonText, "%s", "∨");

	// 버튼 너비 계산 (아이콘 크기 + 간격 + 텍스트 크기 + 좌우 패딩)
	ImVec2 TextSize = ImGui::CalcTextSize(ButtonText);
	const float Padding = 8.0f;
	const float DropdownWidth = IconSize.x + 4.0f + TextSize.x + Padding * 2.0f;

	// 스타일 적용
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.16f, 0.16f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.21f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.28f, 0.26f, 1.00f));

	// 드롭다운 버튼 생성 (아이콘 + 텍스트)
	ImVec2 ButtonSize(DropdownWidth, ImGui::GetFrameHeight());
	ImVec2 ButtonCursorPos = ImGui::GetCursorPos();

	// 버튼 클릭 영역
	if (ImGui::Button("##ShowFlagBtn", ButtonSize))
	{
		ImGui::OpenPopup("ShowFlagPopup");
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Show Flag 설정");
	}

	// 버튼 위에 내용 렌더링 (아이콘 + 텍스트, 가운데 정렬)
	float ButtonContentWidth = IconSize.x + 4.0f + TextSize.x;
	float ButtonContentStartX = ButtonCursorPos.x + (ButtonSize.x - ButtonContentWidth) * 0.5f;
	ImVec2 ButtonContentCursorPos = ImVec2(ButtonContentStartX, ButtonCursorPos.y + (ButtonSize.y - IconSize.y) * 0.5f);
	ImGui::SetCursorPos(ButtonContentCursorPos);

	// ShowFlag 아이콘 표시
	if (IconShowFlag && IconShowFlag->GetShaderResourceView())
	{
		ImGui::Image((void*)IconShowFlag->GetShaderResourceView(), IconSize);
		ImGui::SameLine(0, 4);
	}

	ImGui::Text("%s", ButtonText);

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(1);

	// ===== ShowFlag 드롭다운 팝업 =====
	if (ImGui::BeginPopup("ShowFlagPopup", ImGuiWindowFlags_NoMove))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

		// 선택된 항목의 파란 배경 제거
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.3f, 0.3f, 0.6f));

		URenderSettings& RenderSettings = ViewportClient->GetWorld()->GetRenderSettings();

		// --- 디폴트 사용 (Reset) ---
		ImVec2 ResetCursorPos = ImGui::GetCursorScreenPos();
		if (ImGui::Selectable("##ResetDefault", false, 0, ImVec2(0, IconSize.y)))
		{
			// 기본 설정으로 복원
			RenderSettings.SetShowFlags(EEngineShowFlags::SF_DefaultEnabled);
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("모든 Show Flag를 기본 설정으로 복원합니다.");
		}

		// Selectable 위에 아이콘과 텍스트 그리기
		ImGui::SetCursorScreenPos(ResetCursorPos);
		if (IconRevert && IconRevert->GetShaderResourceView())
		{
			ImGui::Image((void*)IconRevert->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text("디폴트 사용");

		ImGui::Separator();

		// --- 뷰포트 통계 (Viewport Stats with Submenu) ---
		// 아이콘
		if (IconStats && IconStats->GetShaderResourceView())
		{
			ImGui::Image((void*)IconStats->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		if (ImGui::BeginMenu("  뷰포트 통계"))
		{
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "뷰포트 통계");
			ImGui::Separator();

			// 모두 숨김
			ImVec2 HideAllCursorPos = ImGui::GetCursorScreenPos();
			if (ImGui::Selectable("##HideAllStats", false, 0, ImVec2(0, IconSize.y)))
			{
				UStatsOverlayD2D::Get().SetShowFPS(false);
				UStatsOverlayD2D::Get().SetShowMemory(false);
				UStatsOverlayD2D::Get().SetShowPicking(false);
				UStatsOverlayD2D::Get().SetShowDecal(false);
				UStatsOverlayD2D::Get().SetShowTileCulling(false);
				UStatsOverlayD2D::Get().SetShowLights(false);
				UStatsOverlayD2D::Get().SetShowShadow(false);
				UStatsOverlayD2D::Get().SetShowSkinning(false);
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("모든 뷰포트 통계를 숨깁니다.");
			}

			// Selectable 위에 아이콘과 텍스트 그리기
			ImGui::SetCursorScreenPos(HideAllCursorPos);
			if (IconHide && IconHide->GetShaderResourceView())
			{
				ImGui::Image((void*)IconHide->GetShaderResourceView(), IconSize);
				ImGui::SameLine(0, 4);
			}
			ImGui::Text(" 모두 숨김");

			ImGui::Separator();

			// Individual stats checkboxes
			bool bFPS = UStatsOverlayD2D::Get().IsFPSVisible();
			if (ImGui::Checkbox(" FPS", &bFPS))
			{
				UStatsOverlayD2D::Get().ToggleFPS();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("프레임 속도와 프레임 시간을 표시합니다.");
			}

			bool bMemory = UStatsOverlayD2D::Get().IsMemoryVisible();
			if (ImGui::Checkbox(" MEMORY", &bMemory))
			{
				UStatsOverlayD2D::Get().ToggleMemory();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("메모리 사용량과 할당 횟수를 표시합니다.");
			}

			bool bPicking = UStatsOverlayD2D::Get().IsPickingVisible();
			if (ImGui::Checkbox(" PICKING", &bPicking))
			{
				UStatsOverlayD2D::Get().TogglePicking();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("오브젝트 선택 성능 통계를 표시합니다.");
			}

			bool bDecalStats = UStatsOverlayD2D::Get().IsDecalVisible();
			if (ImGui::Checkbox(" DECAL", &bDecalStats))
			{
				UStatsOverlayD2D::Get().ToggleDecal();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("데칼 렌더링 성능 통계를 표시합니다.");
			}

			bool bTileCullingStats = UStatsOverlayD2D::Get().IsTileCullingVisible();
			if (ImGui::Checkbox(" TILE CULLING", &bTileCullingStats))
			{
				UStatsOverlayD2D::Get().ToggleTileCulling();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("타일 기반 라이트 컵링 통계를 표시합니다.");
			}

			bool bLightStats = UStatsOverlayD2D::Get().IsLightsVisible();
			if (ImGui::Checkbox(" LIGHTS", &bLightStats))
			{
				UStatsOverlayD2D::Get().ToggleLights();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("라이트 타입별 개수를 표시합니다.");
			}

			bool bShadowStats = UStatsOverlayD2D::Get().IsShadowVisible();
			if (ImGui::Checkbox(" SHADOWS", &bShadowStats))
			{
				UStatsOverlayD2D::Get().ToggleShadow();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("섀도우 맵 통계를 표시합니다. (섀도우 라이트 개수, 아틀라스 크기, 메모리 사용량)");
			}

			bool bSkinningStats = UStatsOverlayD2D::Get().IsSkinningVisible();
			if (ImGui::Checkbox(" SKINNING", &bSkinningStats))
			{
				UStatsOverlayD2D::Get().ToggleSkinning();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("스키닝 통계를 표시합니다.");
			}

			bool bParticleStats = UStatsOverlayD2D::Get().IsParticleVisible();
			if (ImGui::Checkbox(" PARTICLE", &bParticleStats))
			{
				UStatsOverlayD2D::Get().ToggleParticle();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("파티클 통계를 표시합니다.");
			}

			ImGui::EndMenu();
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("뷰포트 성능 통계 표시 설정");
		}

		// --- 섹션: 일반 표시 플래그 ---
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "일반 표시 플래그");
		ImGui::Separator();

		// BVH
		bool bBVH = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_BVHDebug);
		if (ImGui::Checkbox("##BVH", &bBVH))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_BVHDebug);
		}
		ImGui::SameLine();
		if (IconBVH && IconBVH->GetShaderResourceView())
		{
			ImGui::Image((void*)IconBVH->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" BVH");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("BVH(Bounding Volume Hierarchy) 디버그 시각화를 표시합니다.");
		}

		// Grid
		bool bGrid = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_Grid);
		if (ImGui::Checkbox("##Grid", &bGrid))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_Grid);
		}
		ImGui::SameLine();
		if (IconGrid && IconGrid->GetShaderResourceView())
		{
			ImGui::Image((void*)IconGrid->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 그리드");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("월드 그리드를 표시합니다.");
		}

		// Decal
		bool bDecal = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_Decals);
		if (ImGui::Checkbox("##Decal", &bDecal))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_Decals);
		}
		ImGui::SameLine();
		if (IconDecal && IconDecal->GetShaderResourceView())
		{
			ImGui::Image((void*)IconDecal->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 데칼");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("데칼 렌더링을 표시합니다.");
		}

		// Static Mesh
		bool bStaticMesh = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_StaticMeshes);
		if (ImGui::Checkbox("##StaticMesh", &bStaticMesh))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_StaticMeshes);
		}
		ImGui::SameLine();
		if (IconStaticMesh && IconStaticMesh->GetShaderResourceView())
		{
			ImGui::Image((void*)IconStaticMesh->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 스태틱 메시");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("스태틱 메시 렌더링을 표시합니다.");
		}

		// Skeletal Mesh
		bool bSkeletalMesh = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_SkeletalMeshes);
		if (ImGui::Checkbox("##SkeletalMesh", &bSkeletalMesh))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_SkeletalMeshes);
		}
		ImGui::SameLine();
		if (IconSkeletalMesh && IconSkeletalMesh->GetShaderResourceView())
		{
			ImGui::Image((void*)IconSkeletalMesh->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 스켈레탈 메시");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("스텔레탈 메시 렌더링을 표시합니다.");
		}

		// Billboard
		bool bBillboard = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_Billboard);
		if (ImGui::Checkbox("##Billboard", &bBillboard))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_Billboard);
		}
		ImGui::SameLine();
		if (IconBillboard && IconBillboard->GetShaderResourceView())
		{
			ImGui::Image((void*)IconBillboard->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 빌보드");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("빌보드 텍스트를 표시합니다.");
		}
		
		// EditorIcon
		bool bIcon = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_EditorIcon);
		if (ImGui::Checkbox("##Icon", &bIcon))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_EditorIcon);
		}
		ImGui::SameLine();
		if (IconEditorIcon && IconEditorIcon->GetShaderResourceView())
		{
			ImGui::Image((void*)IconEditorIcon->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 아이콘");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("에디터 전용 아이콘을 표시합니다.");
		}

		// Fog
		bool bFog = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_Fog);
		if (ImGui::Checkbox("##Fog", &bFog))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_Fog);
		}
		ImGui::SameLine();
		if (IconFog && IconFog->GetShaderResourceView())
		{
			ImGui::Image((void*)IconFog->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 포그");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("포그 효과를 표시합니다.");
		}

		// Bounds
		bool bBounds = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_BoundingBoxes);
		if (ImGui::Checkbox("##Bounds", &bBounds))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_BoundingBoxes);
		}
		ImGui::SameLine();
		if (IconCollision && IconCollision->GetShaderResourceView())
		{
			ImGui::Image((void*)IconCollision->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 바운딩 박스");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("바운딩 박스를 표시합니다.");
		}

		// 그림자
		bool bShadows = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_Shadows);
		if (ImGui::Checkbox("##Shadows", &bShadows))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_Shadows);
		}
		ImGui::SameLine();
		if (IconShadow && IconShadow->GetShaderResourceView())
		{
			ImGui::Image((void*)IconShadow->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 그림자");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("그림자를 표시합니다.");
		}
		
		// ===== 파티클 =====
		bool bParticle = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_Particle);
		if (ImGui::Checkbox("##Particle", &bParticle))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_Particle);
		}
		ImGui::SameLine();
		if (IconParticle && IconParticle->GetShaderResourceView())
		{
			ImGui::Image((void*)IconParticle->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" 파티클");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("파티클을 표시합니다.");
		}

		// --- 섹션: 그래픽스 기능 ---
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "그래픽스 기능");
		ImGui::Separator();

		// FXAA (Anti-Aliasing)
		bool bFXAA = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_FXAA);
		if (ImGui::Checkbox("##FXAA", &bFXAA))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_FXAA);
		}
		ImGui::SameLine();
		if (IconAntiAliasing && IconAntiAliasing->GetShaderResourceView())
		{
			ImGui::Image((void*)IconAntiAliasing->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		ImGui::Text(" 안티 에일리어싱");

		// Tile-Based Light Culling
		bool bTileCulling = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_TileCulling);
		if (ImGui::Checkbox("##TileCulling", &bTileCulling))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_TileCulling);
		}
		ImGui::SameLine();
		if (IconTile && IconTile->GetShaderResourceView())
		{
			ImGui::Image((void*)IconTile->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		// 서브메뉴
		if (ImGui::BeginMenu(" 타일 기반 라이트 컬링"))
		{
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "타일 기반 라이트 컬링");
			ImGui::Separator();

			// 디버그 시각화 체크박스
			bool bDebugVis = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_TileCullingDebug);
			if (ImGui::Checkbox(" 디버그 시각화", &bDebugVis))
			{
				RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_TileCullingDebug);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("타일 컬링 결과를 화면에 색상으로 시각화합니다.");
			}

			ImGui::Separator();

			// 타일 크기 입력
			static int tempTileSize = RenderSettings.GetTileSize();
			ImGui::Text("타일 크기 (픽셀)");
			ImGui::SetNextItemWidth(100);
			ImGui::InputInt("##TileSize", &tempTileSize, 1, 8);

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("타일의 크기를 픽셀 단위로 설정합니다. (일반적: 8, 16, 32)");
			}

			// 적용 버튼
			ImGui::SameLine();
			if (ImGui::Button("적용"))
			{
				if (tempTileSize >= 4 && tempTileSize <= 64)
				{
					RenderSettings.SetTileSize(tempTileSize);
					// TileLightCuller는 매 프레임 생성되므로 다음 프레임에 자동 적용됨
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("타일 크기를 적용합니다. (4~64 사이 값)");
			}

			// 현재 설정값 표시
			ImGui::Text("현재 타일 크기: %d x %d", RenderSettings.GetTileSize(), RenderSettings.GetTileSize());

			ImGui::EndMenu();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("타일 기반 라이트 컬링 설정");
		}

		// ===== 그림자 안티 에일리어싱 =====
		bool bShadowAA = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_ShadowAntiAliasing);
		if (ImGui::Checkbox("##ShadowAA", &bShadowAA))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_ShadowAntiAliasing);
		}
		ImGui::SameLine();
		if (IconShadowAA && IconShadowAA->GetShaderResourceView())
		{
			ImGui::Image((void*)IconShadowAA->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}

		// 서브메뉴
		if (ImGui::BeginMenu(" 그림자 안티 에일리어싱"))
		{
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "그림자 안티 에일리어싱");
			ImGui::Separator();

			// RadioButton과 바인딩할 int 변수 준비
			EShadowAATechnique currentTechnique = RenderSettings.GetShadowAATechnique();
			int techniqueInt = static_cast<int>(currentTechnique);
			const int oldTechniqueInt = techniqueInt; // 변경 감지를 위한 원본 값

			// RadioButton으로 변경 (int 값 0에 바인딩)
			ImGui::RadioButton(" PCF (Percentage-Closer Filtering)", &techniqueInt, static_cast<int>(EShadowAATechnique::PCF));
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("부드러운 그림자 가장자리를 생성합니다. (기본값)");
			}

			// RadioButton으로 변경 (int 값 1에 바인딩)
			ImGui::RadioButton(" VSM (Variance Shadow Maps)", &techniqueInt, static_cast<int>(EShadowAATechnique::VSM));
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("매우 부드러운 그림자를 생성하지만, 라이트 블리딩 문제가 발생할 수 있습니다.");
			}

			// RadioButton 클릭으로 int 값이 변경되었다면 RenderSettings 업데이트
			if (techniqueInt != oldTechniqueInt)
			{
				RenderSettings.SetShadowAATechnique(static_cast<EShadowAATechnique>(techniqueInt));
			}

			ImGui::EndMenu();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("그림자 안티 에일리어싱 기술 설정");
		}
		
		// ===== 스키닝 모드 =====
		bool bIsGpuSkinning = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_GPUSkinning);
		if (ImGui::Checkbox("##GPUSkinning", &bIsGpuSkinning))
		{
			RenderSettings.ToggleShowFlag(EEngineShowFlags::SF_GPUSkinning);
		}
		ImGui::SameLine();
		if (IconSkinning && IconSkinning->GetShaderResourceView())
		{
			ImGui::Image((void*)IconSkinning->GetShaderResourceView(), IconSize);
			ImGui::SameLine(0, 4);
		}
		ImGui::Text(" GPU 스키닝");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("CPU 또는 GPU 스키닝 방식을 선택합니다.");
		}

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		ImGui::EndPopup();
	}
}

void SViewportWindow::RenderViewportLayoutSwitchButton()
{
	ImVec2 switchCursorPos = ImGui::GetCursorPos();
	ImGui::SetCursorPosY(switchCursorPos.y - 0.7f);

	const ImVec2 IconSize(17, 17);

	// 현재 레이아웃 모드에 따라 아이콘 선택
	// FourSplit일 때 → SingleViewport 아이콘 표시 (클릭하면 단일로 전환)
	// SingleMain일 때 → MultiViewport 아이콘 표시 (클릭하면 멀티로 전환)
	EViewportLayoutMode CurrentMode = SLATE.GetCurrentLayoutMode();
	UTexture* SwitchIcon = (CurrentMode == EViewportLayoutMode::FourSplit) ? IconMultiToSingleViewport : IconSingleToMultiViewport;
	const char* TooltipText = (CurrentMode == EViewportLayoutMode::FourSplit) ? "단일 뷰포트로 전환" : "멀티 뷰포트로 전환";

	// 버튼 너비 계산 (아이콘 + 패딩)
	const float Padding = 8.0f;
	const float ButtonWidth = IconSize.x + Padding * 2.0f;

	// 스타일 적용
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.16f, 0.16f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.21f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.28f, 0.26f, 1.00f));

	// 버튼
	ImVec2 ButtonSize(ButtonWidth, ImGui::GetFrameHeight());
	ImVec2 ButtonCursorPos = ImGui::GetCursorPos();

	if (ImGui::Button("##SwitchLayout", ButtonSize))
	{
		SLATE.SwitchPanel(this);
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(TooltipText);
	}

	// 버튼 위에 아이콘 렌더링 (중앙 정렬)
	float ButtonContentStartX = ButtonCursorPos.x + (ButtonSize.x - IconSize.x) * 0.5f;
	ImVec2 ButtonContentCursorPos = ImVec2(ButtonContentStartX, ButtonCursorPos.y + (ButtonSize.y - IconSize.y) * 0.5f);
	ImGui::SetCursorPos(ButtonContentCursorPos);

	// 아이콘 표시
	if (SwitchIcon && SwitchIcon->GetShaderResourceView())
	{
		ImGui::Image((void*)SwitchIcon->GetShaderResourceView(), IconSize);
	}

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(1);
}

void SViewportWindow::HandleDropTarget()
{
	// 드래그 중일 때만 드롭 타겟 윈도우를 표시
	const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
	bool isDragging = (dragPayload != nullptr && dragPayload->IsDataType("ASSET_FILE"));

	if (isDragging)
	{
		// 뷰포트 영역에 Invisible Button을 만들어 드롭 타겟으로 사용
		ImVec2 ViewportPos(Rect.Left, Rect.Top + 35.0f); // 툴바 높이(35) 제외
		ImVec2 ViewportSize(Rect.GetWidth(), Rect.GetHeight() - 35.0f);

		ImGui::SetNextWindowPos(ViewportPos);
		ImGui::SetNextWindowSize(ViewportSize);

		// Invisible overlay window for drop target
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoBackground;

		char WindowId[64];
		sprintf_s(WindowId, "ViewportDropTarget_%p", this);

		ImGui::Begin(WindowId, nullptr, flags);

		// Invisible button을 전체 영역에 만들어서 드롭 타겟으로 사용
		ImGui::InvisibleButton("ViewportDropArea", ViewportSize);

		// 드롭 타겟 처리
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const char* filePath = (const char*)payload->Data;

				// 마우스 위치를 월드 좌표로 변환
				ImVec2 mousePos = ImGui::GetMousePos();

				// 뷰포트 내부의 상대 좌표 계산 (ViewportPos 기준)
				FVector2D screenPos(mousePos.x - ViewportPos.x, mousePos.y - ViewportPos.y);

				// 카메라 방향 기반으로 월드 좌표 계산
				FVector worldLocation = FVector(0, 0, 0);

				if (ViewportClient && ViewportClient->GetCamera())
				{
					ACameraActor* camera = ViewportClient->GetCamera();

					// 뷰포트 크기
					float viewportWidth = ViewportSize.x;
					float viewportHeight = ViewportSize.y;

					// 스크린 좌표를 NDC(Normalized Device Coordinates)로 변환
					// NDC 범위: X [-1, 1], Y [-1, 1]
					float ndcX = (screenPos.X / viewportWidth) * 2.0f - 1.0f;
					float ndcY = 1.0f - (screenPos.Y / viewportHeight) * 2.0f; // Y축 반전

					// 카메라 정보
					FVector cameraPos = camera->GetActorLocation();
					FVector cameraForward = camera->GetForward();
					FVector cameraRight = camera->GetRight();
					FVector cameraUp = camera->GetUp();

					// FOV를 고려한 레이 방향 계산
					float fov = 60.0f;
					float aspectRatio = viewportWidth / viewportHeight;
					float tanHalfFov = tan(fov * 0.5f * 3.14159f / 180.0f);

					// 마우스 스크린 좌표에 해당하는 월드 방향 벡터 계산
					FVector rayDir = cameraForward
						+ cameraRight * (ndcX * tanHalfFov * aspectRatio)
						+ cameraUp * (ndcY * tanHalfFov);
					rayDir.Normalize();

					// 카메라가 바라보는 방향(rayDir)으로 일정 거리(500 units) 앞에 소환
					float spawnDistance = 10.0f;
					worldLocation = cameraPos + rayDir * spawnDistance;

					UE_LOG("Viewport: MousePos(%f, %f), ScreenPos(%f, %f), NDC(%f, %f)",
						mousePos.x, mousePos.y, screenPos.X, screenPos.Y, ndcX, ndcY);
					UE_LOG("Viewport: RayDir(%f, %f, %f), SpawnPos(%f, %f, %f)",
						rayDir.X, rayDir.Y, rayDir.Z, worldLocation.X, worldLocation.Y, worldLocation.Z);
				}

				// 액터 생성
				SpawnActorFromFile(filePath, worldLocation);

				UE_LOG("Viewport: Dropped asset '%s' at world location (%f, %f, %f)",
					filePath, worldLocation.X, worldLocation.Y, worldLocation.Z);
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::End();
	}
}

void SViewportWindow::SpawnActorFromFile(const char* FilePath, const FVector& WorldLocation)
{
	if (!ViewportClient || !ViewportClient->GetWorld())
	{
		UE_LOG("ERROR: ViewportClient or World is null");
		return;
	}

	UWorld* world = ViewportClient->GetWorld();
	std::filesystem::path path(FilePath);
	std::string extension = path.extension().string();

	// 소문자로 변환
	std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

	UE_LOG("Spawning actor from file: %s (extension: %s)", FilePath, extension.c_str());

	if (extension == ".prefab")
	{
		// Prefab 액터 생성
		FWideString widePath = path.wstring();
		AActor* actor = world->SpawnPrefabActor(widePath);
		if (actor)
		{
			// 드롭한 위치로 액터 이동
			actor->SetActorLocation(WorldLocation);
			UE_LOG("Prefab actor spawned successfully at (%f, %f, %f)",
				WorldLocation.X, WorldLocation.Y, WorldLocation.Z);
		}
		else
		{
			UE_LOG("ERROR: Failed to spawn prefab actor from %s", FilePath);
		}
	}
	//else if (extension == ".fbx")
	//{
	//	// SkeletalMesh 액터 생성
	//	ASkeletalMeshActor* actor = world->SpawnActor<ASkeletalMeshActor>(FTransform(WorldLocation));
	//	if (actor)
	//	{
	//		actor->GetSkeletalMeshComponent()->SetSkeletalMesh(FilePath);
	//		actor->SetActorLabel(path.filename().string().c_str());
	//		UE_LOG("SkeletalMeshActor spawned successfully at (%f, %f, %f)",
	//			WorldLocation.X, WorldLocation.Y, WorldLocation.Z);
	//	}
	//	else
	//	{
	//		UE_LOG("ERROR: Failed to spawn SkeletalMeshActor");
	//	}
	//}
	//else if (extension == ".obj")
	//{
	//	// StaticMesh 액터 생성
	//	AStaticMeshActor* actor = world->SpawnActor<AStaticMeshActor>(FTransform(WorldLocation));
	//	if (actor)
	//	{
	//		actor->GetStaticMeshComponent()->SetStaticMesh(FilePath);
	//		actor->SetActorLabel(path.filename().string().c_str());
	//		UE_LOG("StaticMeshActor spawned successfully at (%f, %f, %f)",
	//			WorldLocation.X, WorldLocation.Y, WorldLocation.Z);
	//	}
	//	else
	//	{
	//		UE_LOG("ERROR: Failed to spawn StaticMeshActor");
	//	}
	//}
	else
	{
		UE_LOG("WARNING: Unsupported file type: %s", extension.c_str());
	}
}
