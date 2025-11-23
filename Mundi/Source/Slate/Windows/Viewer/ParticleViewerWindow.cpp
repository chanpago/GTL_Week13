#include "pch.h"
#include "ParticleViewerWindow.h"
#include "Source/Runtime/Renderer/FViewport.h"
#include "Source/Runtime/Renderer/FViewportClient.h"
#include "Source/Runtime/Engine/Particle/ParticleSystem.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitter.h"
#include "Source/Runtime/Engine/Particle/ParticleLODLevel.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModule.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleRequired.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleSpawn.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleLifetime.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleSize.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleVelocity.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleColor.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleLocation.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleColorOverLife.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleSizeMultiplyLife.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleRotation.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleRotationRate.h"
#include "Source/Runtime/Core/Object/ObjectFactory.h"
#include "Source/Runtime/AssetManagement/ResourceManager.h"
#include "Source/Runtime/Engine/Components/ParticleSystemComponent.h"
#include "Texture.h"
#include "World.h"
#include "Actor.h"
#include "CameraActor.h"
#include "imgui.h"
#include "PlatformProcess.h"

SParticleViewerWindow::SParticleViewerWindow()
{
    ParticlePath = std::filesystem::path(GDataDir) / "Particle";
    if (!std::filesystem::exists(ParticlePath))
    {
        std::filesystem::create_directories(ParticlePath);
    }
}

SParticleViewerWindow::~SParticleViewerWindow()
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

    if (PreviewWorld)
    {
        ObjectFactory::DeleteObject(PreviewWorld);
        PreviewWorld = nullptr;
    }

    // 임시 객체일 경우 삭제
    if (CurrentParticleSystem && SavePath.empty())
    {
        ObjectFactory::DeleteObject(CurrentParticleSystem);
    }
    
    CurrentParticleSystem = nullptr;
}

bool SParticleViewerWindow::Initialize(float StartX, float StartY, float Width, float Height, UWorld* InWorld, ID3D11Device* InDevice)
{
    Device = InDevice;
    if (!Device) return false;

    SetRect(StartX, StartY, StartX + Width, StartY + Height);

    // 1. Preview World 생성
    PreviewWorld = NewObject<UWorld>();
    PreviewWorld->SetWorldType(EWorldType::PreviewMinimal);
    PreviewWorld->Initialize();
    PreviewWorld->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_EditorIcon);

    PreviewWorld->GetGizmoActor()->SetSpace(EGizmoSpace::Local);

    // InWorld의 설정 복사
    if (InWorld)
    {
        PreviewWorld->GetRenderSettings().SetShowFlags(InWorld->GetRenderSettings().GetShowFlags());
        PreviewWorld->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_EditorIcon);
    }

    // 2. Viewport 생성
    Viewport = new FViewport();
    Viewport->Initialize(0, 0, 1, 1, InDevice);

    // 3. ViewportClient 생성
    ViewportClient = new FViewportClient();
    ViewportClient->SetWorld(PreviewWorld);
    ViewportClient->SetViewportType(EViewportType::Perspective);
    ViewportClient->SetViewMode(EViewMode::VMI_Lit_Phong);

    // 카메라 설정
    ACameraActor* Camera = ViewportClient->GetCamera();
    Camera->SetActorLocation(FVector(3, 0, 2));
    Camera->SetActorRotation(FVector(0, 30, -180));

    Viewport->SetViewportClient(ViewportClient);
    PreviewWorld->SetEditorCameraActor(Camera);

    return true;
}

void SParticleViewerWindow::OnRender()
{
    if (!bIsOpen)
        return;

    // ImGui 창 설정
    ImGui::SetNextWindowSize(ImVec2(1400, 900), ImGuiCond_FirstUseEver);
    if (!bInitialPlacementDone)
    {
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        bInitialPlacementDone = true;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin("Particle Editor", &bIsOpen, flags))
    {
        ImGui::End();
        return;
    }

    // 1. 메뉴바
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Create"))
            {
                CreateParticleSystem();
            }
            if (ImGui::MenuItem("Save"))
            {
                SaveParticleSystem();
            }
            if (ImGui::MenuItem("Load")) { LoadParticleSystem(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo")) {}
            if (ImGui::MenuItem("Redo")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Asset"))
        {
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window"))
        {
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // 2. 툴바
    const float toolbarHeight = 40.0f;
    ImGui::BeginChild("Toolbar", ImVec2(0, toolbarHeight), true);
    {
        if (ImGui::Button("Save"))
        {
            SaveParticleSystem();
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart Sim")) {}
        ImGui::SameLine();
        if (ImGui::Button("Undo")) {}
        ImGui::SameLine();
        if (ImGui::Button("Redo")) {}
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        if (ImGui::Button("Thumbnail")) {}
        ImGui::SameLine();
        if (ImGui::Button("Bounds")) {}
    }
    ImGui::EndChild();

    // 3. 메인 컨텐츠 영역
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float curveEditorHeight = contentSize.y * 0.3f;
    const float mainContentHeight = contentSize.y - curveEditorHeight;
    const float leftPanelWidth = contentSize.x * 0.4f;
    const float rightPanelWidth = contentSize.x - leftPanelWidth;

    // 좌측: 뷰포트 + Properties
    ImGui::BeginChild("LeftMain", ImVec2(leftPanelWidth, mainContentHeight), false);
    {
        // 좌측 패널 클릭 시 이미터 선택 해제
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            SelectedEmitter = nullptr;
        }

        const float viewportHeight = mainContentHeight * 0.6f;
        const float propertiesHeight = mainContentHeight - viewportHeight;

        // 뷰포트 (헤더 + 렌더링 영역)
        ImGui::BeginChild("ViewportContainer", ImVec2(0, viewportHeight), false);
        {
            // 헤더
            const float headerHeight = 30.0f;
            ImGui::BeginChild("ViewportHeader", ImVec2(0, headerHeight), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::SetCursorPosY(5.0f);
                ImGui::Text("Preview");
            }
            ImGui::EndChild();

            // 뷰포트 렌더링 영역
            ImGui::BeginChild("Viewport", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
            {
                
             // 뷰포트에서 마우스 입력을 허용하도록 설정
             if (ImGui::IsWindowHovered())
             {
                 ImGui::GetIO().WantCaptureMouse = false;
             }
                // 뷰포트 영역 계산 (전체 Child 윈도우 영역)
                ImVec2 childPos = ImGui::GetWindowPos();
                ImVec2 childSize = ImGui::GetWindowSize();
                ImVec2 rectMin = childPos;
                ImVec2 rectMax(childPos.x + childSize.x, childPos.y + childSize.y);
                CenterRect.Left = rectMin.x;
                CenterRect.Top = rectMin.y;
                CenterRect.Right = rectMax.x;
                CenterRect.Bottom = rectMax.y;
                CenterRect.UpdateMinMax();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // Properties
        ImGui::BeginChild("Particle System", ImVec2(0, propertiesHeight), true);
        {
            ImGui::Text("디테일");
            ImGui::Separator();

            if (SelectedModule)
            {
                ImGui::Text("Selected Module: %s", SelectedModule->GetClass()->Name);
                ImGui::Separator();

                // 모듈 타입별로 속성 표시
                if (auto* RequiredModule = dynamic_cast<UParticleModuleRequired*>(SelectedModule))
                {
                    ImGui::Spacing();

                    // 2열 레이아웃 시작
                    ImGui::Columns(2, "RequiredModuleColumns", false);
                    ImGui::SetColumnWidth(0, 150.0f);

                    // Material
                    {
                        ImGui::Text("Material");
                        ImGui::NextColumn();

                        // 머터리얼 텍스처 가져오기
                        UTexture* DiffuseTexture = RequiredModule->Material ? RequiredModule->Material->GetTexture(EMaterialTextureSlot::Diffuse) : nullptr;
                        const char* currentMaterialName = RequiredModule->Material ? RequiredModule->Material->GetName().c_str() : "None";

                        // 미리보기 정사각형 (50x50)
                        if (DiffuseTexture && DiffuseTexture->GetShaderResourceView())
                        {
                            ImGui::Image((void*)DiffuseTexture->GetShaderResourceView(), ImVec2(50, 50));
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                            ImGui::Button("##matpreview", ImVec2(50, 50));
                            ImGui::PopStyleColor();
                        }

                        ImGui::SameLine();

                        // 머터리얼 이름 + 콤보박스
                        ImGui::BeginGroup();
                        ImGui::Text("%s", currentMaterialName);
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##MaterialCombo", ""))
                        {
                            // None 옵션
                            if (ImGui::Selectable("None##MatNone", RequiredModule->Material == nullptr))
                            {
                                RequiredModule->Material = nullptr;
                            }

                            ImGui::Separator();

                            // 모든 머터리얼 가져오기
                            TArray<UMaterial*> AllMaterials = UResourceManager::GetInstance().GetAll<UMaterial>();

                            if (AllMaterials.Num() > 0)
                            {
                                for (int i = 0; i < AllMaterials.Num(); i++)
                                {
                                    UMaterial* Mat = AllMaterials[i];
                                    if (Mat)
                                    {
                                        ImGui::PushID(i);
                                        bool isSelected = (RequiredModule->Material == Mat);

                                        // 텍스처 미리보기 + 이름
                                        UTexture* MatTexture = Mat->GetTexture(EMaterialTextureSlot::Diffuse);
                                        if (MatTexture && MatTexture->GetShaderResourceView())
                                        {
                                            ImGui::Image((void*)MatTexture->GetShaderResourceView(), ImVec2(30, 30));
                                            ImGui::SameLine();
                                        }

                                        if (ImGui::Selectable(Mat->GetName().c_str(), isSelected))
                                        {
                                            RequiredModule->Material = Mat;
                                        }
                                        ImGui::PopID();
                                    }
                                }
                            }
                            else
                            {
                                ImGui::TextDisabled("No materials found");
                            }

                            ImGui::EndCombo();
                        }
                        ImGui::EndGroup();

                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Blend Mode
                    {
                        ImGui::Text("Blend Mode");
                        ImGui::NextColumn();

                        const char* blendModes[] = { "Opaque", "Masked", "Translucent", "Additive", "Modulate", "Alpha" };
                        int currentBlendMode = (int)RequiredModule->BlendMode;

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##BlendModeCombo", blendModes[currentBlendMode]))
                        {
                            for (int i = 0; i < IM_ARRAYSIZE(blendModes); i++)
                            {
                                bool isSelected = (currentBlendMode == i);
                                if (ImGui::Selectable(blendModes[i], isSelected))
                                {
                                    RequiredModule->BlendMode = (EBlendMode)i;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Screen Alignment
                    {
                        ImGui::Text("Screen Alignment");
                        ImGui::NextColumn();

                        const char* screenAlignments[] = { "Camera Facing", "Velocity", "Local Space" };
                        int currentAlignment = (int)RequiredModule->ScreenAlignment;

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##ScreenAlignmentCombo", screenAlignments[currentAlignment]))
                        {
                            for (int i = 0; i < IM_ARRAYSIZE(screenAlignments); i++)
                            {
                                bool isSelected = (currentAlignment == i);
                                if (ImGui::Selectable(screenAlignments[i], isSelected))
                                {
                                    RequiredModule->ScreenAlignment = (EScreenAlignment)i;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Sort Mode
                    {
                        ImGui::Text("Sort Mode");
                        ImGui::NextColumn();

                        const char* sortModes[] = { "None", "By Distance", "By Age", "View Depth" };
                        int currentSortMode = (int)RequiredModule->SortMode;

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##SortModeCombo", sortModes[currentSortMode]))
                        {
                            for (int i = 0; i < IM_ARRAYSIZE(sortModes); i++)
                            {
                                bool isSelected = (currentSortMode == i);
                                if (ImGui::Selectable(sortModes[i], isSelected))
                                {
                                    RequiredModule->SortMode = (ESortMode)i;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Max Particles
                    {
                        ImGui::Text("Max Particles");
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragInt("##MaxParticles", &RequiredModule->MaxParticles, 1.0f, 1, 10000);
                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Emitter Duration
                    {
                        ImGui::Text("Emitter Duration");
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragFloat("##EmitterDuration", &RequiredModule->EmitterDuration, 0.01f, 0.0f, 100.0f);
                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Emitter Delay
                    {
                        ImGui::Text("Emitter Delay");
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragFloat("##EmitterDelay", &RequiredModule->EmitterDelay, 0.01f, 0.0f, 10.0f);
                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Emitter Loops
                    {
                        ImGui::Text("Emitter Loops");
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragInt("##EmitterLoops", &RequiredModule->EmitterLoops, 1.0f, 0, 100);
                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Spawn Rate Base
                    {
                        ImGui::Text("Spawn Rate Base");
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragFloat("##SpawnRateBase", &RequiredModule->SpawnRateBase, 0.1f, 0.0f, 1000.0f);
                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Use Local Space
                    {
                        ImGui::Text("Use Local Space");
                        ImGui::NextColumn();
                        ImGui::Checkbox("##UseLocalSpace", &RequiredModule->bUseLocalSpace);
                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Kill On Deactivate
                    {
                        ImGui::Text("Kill On Deactivate");
                        ImGui::NextColumn();
                        ImGui::Checkbox("##KillOnDeactivate", &RequiredModule->bKillOnDeactivate);
                        ImGui::NextColumn();
                    }

                    ImGui::Spacing();

                    // Kill On Completed
                    {
                        ImGui::Text("Kill On Completed");
                        ImGui::NextColumn();
                        ImGui::Checkbox("##KillOnCompleted", &RequiredModule->bKillOnCompleted);
                        ImGui::NextColumn();
                    }

                    // 2열 레이아웃 종료
                    ImGui::Columns(1);
                }
                else if (auto* SpawnModule = dynamic_cast<UParticleModuleSpawn*>(SelectedModule))
                {
                    ImGui::Text("Spawn Settings");
                    ImGui::DragFloat("Spawn Rate Min", &SpawnModule->SpawnRate.MinValue, 0.1f, 0.0f, 1000.0f);
                    ImGui::DragFloat("Spawn Rate Max", &SpawnModule->SpawnRate.MaxValue, 0.1f, 0.0f, 1000.0f);
                    ImGui::Checkbox("Use Range", &SpawnModule->SpawnRate.bUseRange);
                }
                else if (auto* LifetimeModule = dynamic_cast<UParticleModuleLifetime*>(SelectedModule))
                {
                    ImGui::Text("Lifetime Settings");
                    ImGui::DragFloat("Lifetime Min", &LifetimeModule->Lifetime.MinValue, 0.01f, 0.0f, 100.0f);
                    ImGui::DragFloat("Lifetime Max", &LifetimeModule->Lifetime.MaxValue, 0.01f, 0.0f, 100.0f);
                    ImGui::Checkbox("Use Range", &LifetimeModule->Lifetime.bUseRange);
                }
                else if (auto* SizeModule = dynamic_cast<UParticleModuleSize*>(SelectedModule))
                {
                    ImGui::Text("Size Settings");
                    ImGui::DragFloat3("Start Size Min", &SizeModule->StartSize.MinValue.X, 1.0f, 0.0f, 1000.0f);
                    ImGui::DragFloat3("Start Size Max", &SizeModule->StartSize.MaxValue.X, 1.0f, 0.0f, 1000.0f);
                    ImGui::Checkbox("Use Range", &SizeModule->StartSize.bUseRange);
                }
                else if (auto* VelocityModule = dynamic_cast<UParticleModuleVelocity*>(SelectedModule))
                {
                    ImGui::Text("Velocity Settings");
                    ImGui::DragFloat3("Start Velocity Min", &VelocityModule->StartVelocity.MinValue.X, 1.0f, -1000.0f, 1000.0f);
                    ImGui::DragFloat3("Start Velocity Max", &VelocityModule->StartVelocity.MaxValue.X, 1.0f, -1000.0f, 1000.0f);
                    ImGui::Checkbox("Use Range", &VelocityModule->StartVelocity.bUseRange);
                    ImGui::DragFloat3("Gravity", &VelocityModule->Gravity.X, 1.0f, -10000.0f, 10000.0f);
                }
                else if (auto* ColorModule = dynamic_cast<UParticleModuleColor*>(SelectedModule))
                {
                    ImGui::Text("Color Settings");
                    ImGui::ColorEdit3("Start Color Min", &ColorModule->StartColor.MinValue.R);
                    ImGui::ColorEdit3("Start Color Max", &ColorModule->StartColor.MaxValue.R);
                    ImGui::Checkbox("Use Range", &ColorModule->StartColor.bUseRange);
                    ImGui::DragFloat("Start Alpha Min", &ColorModule->StartAlpha.MinValue, 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat("Start Alpha Max", &ColorModule->StartAlpha.MaxValue, 0.01f, 0.0f, 1.0f);
                }
                else if (auto* ColorOverLifeModule = dynamic_cast<UParticleModuleColorOverLife*>(SelectedModule))
                {
                    ImGui::Text("Color Over Life Settings");
                    ImGui::Separator();

                    ImGui::Checkbox("Use Color Over Life", &ColorOverLifeModule->bUseColorOverLife);
                    if (ColorOverLifeModule->bUseColorOverLife)
                    {
                        ImGui::ColorEdit3("Color Min", &ColorOverLifeModule->ColorOverLife.MinValue.R);
                        ImGui::ColorEdit3("Color Max", &ColorOverLifeModule->ColorOverLife.MaxValue.R);
                        ImGui::Checkbox("Color Use Range", &ColorOverLifeModule->ColorOverLife.bUseRange);
                    }

                    ImGui::Spacing();
                    ImGui::Checkbox("Use Alpha Over Life", &ColorOverLifeModule->bUseAlphaOverLife);
                    if (ColorOverLifeModule->bUseAlphaOverLife)
                    {
                        ImGui::DragFloat("Alpha Min", &ColorOverLifeModule->AlphaOverLife.MinValue, 0.01f, 0.0f, 1.0f);
                        ImGui::DragFloat("Alpha Max", &ColorOverLifeModule->AlphaOverLife.MaxValue, 0.01f, 0.0f, 1.0f);
                        ImGui::Checkbox("Alpha Use Range", &ColorOverLifeModule->AlphaOverLife.bUseRange);
                    }
                }
                else if (auto* SizeMultiplyLifeModule = dynamic_cast<UParticleModuleSizeMultiplyLife*>(SelectedModule))
                {
                    ImGui::Text("Size Multiply Life Settings");
                    ImGui::Separator();

                    ImGui::Text("Curve Control Points");

                    ImGui::Text("Point 1:");
                    ImGui::DragFloat("Time##P1", &SizeMultiplyLifeModule->Point1Time, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat3("Value##P1", &SizeMultiplyLifeModule->Point1Value.X, 0.1f, 0.0f, 100.0f);

                    ImGui::Spacing();
                    ImGui::Text("Point 2:");
                    ImGui::DragFloat("Time##P2", &SizeMultiplyLifeModule->Point2Time, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat3("Value##P2", &SizeMultiplyLifeModule->Point2Value.X, 0.1f, 0.0f, 100.0f);

                    ImGui::Spacing();
                    ImGui::Text("Multiply Axes:");
                    ImGui::Checkbox("Multiply X", &SizeMultiplyLifeModule->bMultiplyX);
                    ImGui::Checkbox("Multiply Y", &SizeMultiplyLifeModule->bMultiplyY);
                    ImGui::Checkbox("Multiply Z", &SizeMultiplyLifeModule->bMultiplyZ);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tip: Use curve editor to adjust visually");
                }
                else if (auto* RotationModule = dynamic_cast<UParticleModuleRotation*>(SelectedModule))
                {
                    ImGui::Text("Rotation Settings");
                    ImGui::Separator();

                    ImGui::DragFloat("Start Rotation Min (Radians)", &RotationModule->StartRotation.MinValue, 0.01f, -6.28f, 6.28f);
                    ImGui::DragFloat("Start Rotation Max (Radians)", &RotationModule->StartRotation.MaxValue, 0.01f, -6.28f, 6.28f);
                    ImGui::Checkbox("Use Range", &RotationModule->StartRotation.bUseRange);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tip: PI = 3.14159, 2*PI = 6.28318");
                }
                else if (auto* RotationRateModule = dynamic_cast<UParticleModuleRotationRate*>(SelectedModule))
                {
                    ImGui::Text("Rotation Rate Settings");
                    ImGui::Separator();

                    ImGui::Text("Initial Rotation");
                    ImGui::DragFloat("Initial Rotation Min (Rad)", &RotationRateModule->InitialRotation.MinValue, 0.01f, 0.0f, 6.28318f);
                    ImGui::DragFloat("Initial Rotation Max (Rad)", &RotationRateModule->InitialRotation.MaxValue, 0.01f, 0.0f, 6.28318f);
                    ImGui::Checkbox("Use Initial Rotation Range", &RotationRateModule->InitialRotation.bUseRange);

                    ImGui::Spacing();
                    ImGui::Text("Rotation Speed");
                    ImGui::DragFloat("Start Rotation Rate Min (Rad/s)", &RotationRateModule->StartRotationRate.MinValue, 0.01f, -10.0f, 10.0f);
                    ImGui::DragFloat("Start Rotation Rate Max (Rad/s)", &RotationRateModule->StartRotationRate.MaxValue, 0.01f, -10.0f, 10.0f);
                    ImGui::Checkbox("Use Rotation Rate Range", &RotationRateModule->StartRotationRate.bUseRange);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tip: PI = 3.14159, 2*PI = 6.28318");
                    ImGui::TextDisabled("Tip: 1 rad/s = ~57 degrees/s");
                }
            }
            else if (CurrentParticleSystem)
            {
                ImGui::TextDisabled("No module selected");
            }
            else
            {
                ImGui::TextDisabled("No particle system loaded");
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // 우측: Emitter/모듈 패널
    ImGui::BeginChild("EmitterPanel", ImVec2(rightPanelWidth, mainContentHeight), true, ImGuiWindowFlags_HorizontalScrollbar);
    {
        ImGui::Text("이미터");
        ImGui::Separator();

        // 빈 영역 클릭 감지
        bool bShowContextMenu = false;
        if (ImGui::IsWindowHovered())
        {
            // 우클릭 - 컨텍스트 메뉴
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                bShowContextMenu = true;
            }
            // 좌클릭 - 선택 해제
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                // 이 플래그는 이미터 블럭을 클릭했는지 추적
                // 아래에서 이미터 블럭 클릭 시 false로 설정됨
                SelectedEmitter = nullptr;
            }
        }

        // Delete 키 입력 감지 (EmitterPanel에 포커스가 있을 때)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                // 선택된 모듈이 있으면 모듈 삭제
                if (SelectedModule)
                {
                    UE_LOG("Delete key pressed with selected module: %s", SelectedModule->GetClass()->Name);

                    // Required 모듈은 삭제 불가
                    if (Cast<UParticleModuleRequired>(SelectedModule))
                    {
                        UE_LOG("Required 모듈은 삭제할 수 없습니다.");
                    }
                    else
                    {
                        // 모듈이 속한 LOD 찾기
                        UParticleLODLevel* OwnerLOD = nullptr;
                        if (CurrentParticleSystem)
                        {
                            UE_LOG("Searching for owner LOD in %d emitters", CurrentParticleSystem->Emitters.Num());
                            for (UParticleEmitter* Emitter : CurrentParticleSystem->Emitters)
                            {
                                if (Emitter && Emitter->LODLevels.Num() > 0)
                                {
                                    UParticleLODLevel* LOD = Emitter->LODLevels[0];
                                    if (LOD)
                                    {
                                        UE_LOG("Checking LOD with %d modules", LOD->AllModulesCache.Num());
                                        if (LOD->AllModulesCache.Contains(SelectedModule))
                                        {
                                            OwnerLOD = LOD;
                                            UE_LOG("Found owner LOD!");
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        if (OwnerLOD)
                        {
                            UE_LOG("Calling RemoveModule on owner LOD");
                            // 모듈 삭제
                            OwnerLOD->RemoveModule(SelectedModule);
                            SelectedModule = nullptr;
                        }
                        else
                        {
                            UE_LOG("ERROR: Could not find owner LOD for module!");
                        }
                    }
                }
                // 선택된 이미터가 있으면 이미터 삭제
                else if (SelectedEmitter)
                {
                    DeleteSelectedEmitter();
                }
            }
        }

        if (CurrentParticleSystem && CurrentParticleSystem->Emitters.Num() > 0)
        {
            // 각 이미터를 블럭 형태로 표시
            for (int i = 0; i < CurrentParticleSystem->Emitters.Num(); i++)
            {
                UParticleEmitter* Emitter = CurrentParticleSystem->Emitters[i];
                if (!Emitter) continue;

                ImGui::PushID(i);

                // 이미터 블럭 (가로만 고정, 세로는 전체)
                const float emitterBlockWidth = 200.0f;
                ImGui::BeginChild("EmitterBlock", ImVec2(emitterBlockWidth, 0), true);
                {
                    // 선택된 이미터인지 확인
                    bool bIsSelected = (SelectedEmitter == Emitter);

                    // 이미터 헤더 (Selectable로 호버링 가능하게)
                    const float headerHeight = 50.0f;

                    // 선택된 이미터면 주황색 배경
                    if (bIsSelected)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0f, 0.5f, 0.0f, 0.8f)); // 주황색
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0f, 0.6f, 0.1f, 0.9f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
                    }

                    ImGui::PushID("emitter_header");
                    if (ImGui::Selectable("##emitterheader", bIsSelected, 0, ImVec2(0, headerHeight)))
                    {
                        // 이미터 헤더 클릭 시 선택
                        SelectedEmitter = Emitter;
                    }
                    ImGui::PopID();

                    if (bIsSelected)
                    {
                        ImGui::PopStyleColor(3);
                    }

                    // 헤더 위에 내용 그리기
                    ImVec2 headerMin = ImGui::GetItemRectMin();
                    ImVec2 headerMax = ImGui::GetItemRectMax();

                    // 이미터 이름
                    ImGui::SetCursorScreenPos(ImVec2(headerMin.x + 5, headerMin.y + 5));
                    ImGui::Text("%s", Emitter->GetName().c_str());

                    // 머터리얼 미리보기 박스 (우측 하단)
                    ImGui::SetCursorScreenPos(ImVec2(headerMax.x - 45, headerMin.y + 5));

                    // 머터리얼 텍스처 가져오기
                    UTexture* EmitterMatTexture = nullptr;
                    const char* matTooltip = "No Material";

                    if (Emitter->LODLevels.Num() > 0 && Emitter->LODLevels[0]->RequiredModule && Emitter->LODLevels[0]->RequiredModule->Material)
                    {
                        UMaterial* mat = Emitter->LODLevels[0]->RequiredModule->Material;
                        EmitterMatTexture = mat->GetTexture(EMaterialTextureSlot::Diffuse);
                        matTooltip = mat->GetName().c_str();
                    }

                    // 텍스처가 있으면 Image로 표시, 없으면 검은 버튼
                    if (EmitterMatTexture && EmitterMatTexture->GetShaderResourceView())
                    {
                        ImGui::Image((void*)EmitterMatTexture->GetShaderResourceView(), ImVec2(40, 40));
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                        ImGui::Button("##preview", ImVec2(40, 40));
                        ImGui::PopStyleColor(3);
                    }

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", matTooltip);
                    }

                    // 원래 커서 위치 복구
                    ImGui::SetCursorScreenPos(ImVec2(headerMin.x, headerMax.y));

                    ImGui::Separator();

                    // 모듈 리스트
                    if (Emitter->LODLevels.Num() > 0)
                    {
                        UParticleLODLevel* LOD = Emitter->LODLevels[0];
                        if (LOD)
                        {
                            // 모듈 리스트를 표시하고, 마우스가 모듈 위에 있는지 추적
                            bool bMouseOverModule = false;

                            for (int m = 0; m < LOD->AllModulesCache.Num(); m++)
                            {
                                if (UParticleModule* Module = LOD->AllModulesCache[m])
                                {
                                    ImGui::PushID(m + 1000);
                                    bool isSelected = (SelectedModule == Module);

                                    // 모듈 이름에서 "UParticleModule" 접두사 제거
                                    const char* fullName = Module->GetClass()->Name;
                                    const char* displayName = fullName;
                                    const char* prefix = "UParticleModule";
                                    size_t prefixLen = strlen(prefix);
                                    if (strncmp(fullName, prefix, prefixLen) == 0)
                                    {
                                        displayName = fullName + prefixLen;
                                    }

                                    // Required 모듈 체크
                                    bool isRequired = (strcmp(displayName, "Required") == 0);

                                    // 모듈 이름 (왼쪽 정렬, 버튼 공간 확보)
                                    float itemWidth = ImGui::GetContentRegionAvail().x;
                                    float buttonWidth = 20.0f;
                                    float rightMargin = 10.0f;  // 오른쪽 여백
                                    // Required든 아니든 체크박스 + C버튼 공간 확보 (정렬 맞추기)
                                    float nameWidth = itemWidth - buttonWidth * 2 - rightMargin - 8;

                                    if (ImGui::Selectable(displayName, isSelected, 0, ImVec2(nameWidth, 20)))
                                    {
                                        SelectedModule = Module;
                                    }

                                    // 마우스가 이 모듈 위에 있는지 확인
                                    if (ImGui::IsItemHovered())
                                    {
                                        bMouseOverModule = true;
                                    }

                                    // 버튼들 (같은 라인에 배치)
                                    ImGui::SameLine();

                                    // Required가 아니면 활성화/비활성화 버튼 표시
                                    if (!isRequired)
                                    {
                                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

                                        // 활성화 상태에 따라 색상과 텍스트 변경
                                        if (Module->bEnabled)
                                        {
                                            // 체크 표시 (초록색)
                                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
                                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.7f, 0.0f, 1.0f));
                                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
                                            if (ImGui::Button("V", ImVec2(buttonWidth, 20)))
                                            {
                                                Module->bEnabled = false;
                                            }
                                            ImGui::PopStyleColor(3);
                                        }
                                        else
                                        {
                                            // X 표시 (빨간색)
                                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
                                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.0f, 0.0f, 1.0f));
                                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                                            if (ImGui::Button("X", ImVec2(buttonWidth, 20)))
                                            {
                                                Module->bEnabled = true;
                                            }
                                            ImGui::PopStyleColor(3);
                                        }

                                        ImGui::PopStyleVar();
                                    }
                                    else
                                    {
                                        // Required는 빈 공간
                                        ImGui::Dummy(ImVec2(buttonWidth, 20));
                                    }

                                    ImGui::SameLine();

                                    // 커브 버튼 (모든 모듈)
                                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                                    if (ImGui::Button("C", ImVec2(buttonWidth, 20)))
                                    {
                                        // 커브 에디터에서 이 모듈 선택
                                        SelectedModule = Module;
                                    }
                                    ImGui::PopStyleVar();

                                    ImGui::PopID();
                                }
                            }

                            // 이미터 블록의 빈 공간에서 우클릭 감지
                            // (모듈 위가 아니고, EmitterBlock 내부인 경우)
                            if (!bMouseOverModule &&
                                ImGui::IsWindowHovered() &&
                                ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                            {
                                // 빈 공간에 우클릭한 경우
                                ImGui::OpenPopup("AddModuleContextMenu");
                            }

                            // 모듈 추가 컨텍스트 메뉴
                            if (ImGui::BeginPopup("AddModuleContextMenu"))
                            {
                                ImGui::TextDisabled("모듈 추가");
                                ImGui::Separator();

                                if (ImGui::MenuItem("Lifetime"))
                                {
                                    LOD->AddModule(UParticleModuleLifetime::StaticClass());
                                }
                                if (ImGui::MenuItem("Velocity"))
                                {
                                    LOD->AddModule(UParticleModuleVelocity::StaticClass());
                                }
                                if (ImGui::MenuItem("Size"))
                                {
                                    LOD->AddModule(UParticleModuleSize::StaticClass());
                                }
                                if (ImGui::MenuItem("Color"))
                                {
                                    LOD->AddModule(UParticleModuleColor::StaticClass());
                                }
                                if (ImGui::MenuItem("Location"))
                                {
                                    LOD->AddModule(UParticleModuleLocation::StaticClass());
                                }

                                ImGui::Separator();
                                ImGui::TextDisabled("라이프타임 기반");
                                ImGui::Separator();

                                if (ImGui::MenuItem("Color Over Life"))
                                {
                                    LOD->AddModule(UParticleModuleColorOverLife::StaticClass());
                                }
                                if (ImGui::MenuItem("Size Multiply Life"))
                                {
                                    LOD->AddModule(UParticleModuleSizeMultiplyLife::StaticClass());
                                }

                                ImGui::Separator();
                                ImGui::TextDisabled("회전");
                                ImGui::Separator();

                                if (ImGui::MenuItem("Rotation"))
                                {
                                    LOD->AddModule(UParticleModuleRotation::StaticClass());
                                }
                                if (ImGui::MenuItem("Rotation Rate"))
                                {
                                    LOD->AddModule(UParticleModuleRotationRate::StaticClass());
                                }

                                ImGui::EndPopup();
                            }
                        }
                    }
                }
                ImGui::EndChild();

                ImGui::PopID();

                // 다음 이미터를 옆에 배치 (가로로 계속 나열)
                if (i + 1 < CurrentParticleSystem->Emitters.Num())
                {
                    ImGui::SameLine();
                }
            }
        }
        else
        {
            ImGui::TextDisabled("No emitters");
        }

        // 컨텍스트 메뉴 (빈 영역 우클릭 시)
        if (bShowContextMenu && CurrentParticleSystem)
        {
            ImGui::OpenPopup("EmitterContextMenu");
        }

        if (ImGui::BeginPopup("EmitterContextMenu"))
        {
            if (ImGui::MenuItem("새 파티클 스프라이트 이미터"))
            {
                CreateNewEmitter();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    // 4. 하단: Curve Editor
    ImGui::BeginChild("CurveEditor", ImVec2(0, curveEditorHeight), true);
    {
        // Curve Editor 클릭 시 이미터 선택 해제
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            SelectedEmitter = nullptr;
        }

        ImGui::Text("Curve Editor");
        ImGui::Separator();

        // 툴바
        if (ImGui::Button("Horizontal")) {}
        ImGui::SameLine();
        if (ImGui::Button("Vertical")) {}
        ImGui::SameLine();
        if (ImGui::Button("All")) {}
        ImGui::SameLine();
        if (ImGui::Button("Selected")) {}
        ImGui::SameLine();
        if (ImGui::Button("Pan")) {}
        ImGui::SameLine();
        if (ImGui::Button("Zoom")) {}

        ImGui::Separator();

        // 커브 에디터 레이아웃: 왼쪽(모듈 목록) + 오른쪽(그래프)
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        const float moduleListWidth = 200.0f;
        const float graphWidth = availSize.x - moduleListWidth;

        // 왼쪽: 밝은 회색 - 커브를 사용하는 모듈 목록
        ImGui::BeginChild("ModuleList", ImVec2(moduleListWidth, 0), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 listPos = ImGui::GetCursorScreenPos();
            ImVec2 listSize = ImGui::GetContentRegionAvail();

            // 밝은 회색 배경
            draw_list->AddRectFilled(listPos, ImVec2(listPos.x + listSize.x, listPos.y + listSize.y),
                                    IM_COL32(120, 120, 120, 255));

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255)); // 검은색 텍스트

            ImGui::Text("Curve Modules:");
            ImGui::Separator();

            // 커브를 사용하는 모듈들 나열
            if (CurrentParticleSystem && SelectedEmitterIndex >= 0 &&
                SelectedEmitterIndex < CurrentParticleSystem->Emitters.Num())
            {
                auto* Emitter = CurrentParticleSystem->Emitters[SelectedEmitterIndex];
                if (Emitter && Emitter->LODLevels.Num() > 0)
                {
                    auto* LOD = Emitter->LODLevels[0];
                    for (auto* Module : LOD->AllModulesCache)
                    {
                        // SizeMultiplyLife, ColorOverLife 등 커브를 사용하는 모듈만 표시
                        if (auto* SizeModule = dynamic_cast<UParticleModuleSizeMultiplyLife*>(Module))
                        {
                            if (ImGui::Selectable("SizeMultiplyLife", SelectedModule == Module))
                            {
                                SelectedModule = Module;
                            }
                        }
                        else if (auto* ColorModule = dynamic_cast<UParticleModuleColorOverLife*>(Module))
                        {
                            if (ImGui::Selectable("ColorOverLife", SelectedModule == Module))
                            {
                                SelectedModule = Module;
                            }
                        }
                    }
                }
            }

            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // 오른쪽: 짙은 회색 모눈종이 - 커브 그래프
        ImGui::BeginChild("CurveGraph", ImVec2(graphWidth, 0), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 graphPos = ImGui::GetCursorScreenPos();
            ImVec2 graphSize = ImGui::GetContentRegionAvail();

            // 축 여백 (왼쪽, 아래)
            const float axisMarginLeft = 60.0f;
            const float axisMarginBottom = 30.0f;

            // 실제 그래프 영역 (먼저 선언)
            ImVec2 graphAreaPos = ImVec2(graphPos.x + axisMarginLeft, graphPos.y);
            ImVec2 graphAreaSize = ImVec2(graphSize.x - axisMarginLeft, graphSize.y - axisMarginBottom);

            // 마우스 휠 줌 처리
            if (ImGui::IsWindowHovered())
            {
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    float zoomFactor = 1.0f + wheel * 0.1f;
                    CurveZoom *= zoomFactor;
                    CurveZoom = FMath::Clamp(CurveZoom, 1.0f, 20.0f); // 1.0배(기본) ~ 20배 확대
                }
            }

            // 짙은 회색 배경 (전체)
            draw_list->AddRectFilled(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y),
                                    IM_COL32(50, 50, 50, 255));

            // 좌표계: X축(시간) 0~100, Y축(값) 0~100
            const float timeMin = 0.0f;
            const float timeMax = 100.0f;
            const float valueMin = 0.0f;
            const float valueMax = 100.0f;
            const float fullRangeX = timeMax - timeMin;
            const float fullRangeY = valueMax - valueMin;

            // 줌/팬 적용된 가시 범위
            float visibleRangeX = fullRangeX / CurveZoom;
            float visibleRangeY = fullRangeY / CurveZoom;

            // 중심점 (팬 오프셋 적용)
            float centerX = CurvePan.X;
            float centerY = CurvePan.Y;

            // 가시 영역 계산
            float viewMinX = centerX - visibleRangeX * 0.5f;
            float viewMaxX = centerX + visibleRangeX * 0.5f;
            float viewMinY = centerY - visibleRangeY * 0.5f;
            float viewMaxY = centerY + visibleRangeY * 0.5f;

            // 경계 제한 (범위를 벗어나지 않도록)
            if (viewMinX < timeMin)
            {
                float offset = timeMin - viewMinX;
                viewMinX += offset;
                viewMaxX += offset;
            }
            if (viewMaxX > timeMax)
            {
                float offset = viewMaxX - timeMax;
                viewMinX -= offset;
                viewMaxX -= offset;
            }
            if (viewMinY < valueMin)
            {
                float offset = valueMin - viewMinY;
                viewMinY += offset;
                viewMaxY += offset;
            }
            if (viewMaxY > valueMax)
            {
                float offset = viewMaxY - valueMax;
                viewMinY -= offset;
                viewMaxY -= offset;
            }

            // 최종 가시 범위
            visibleRangeX = viewMaxX - viewMinX;
            visibleRangeY = viewMaxY - viewMinY;

            // World to Screen 변환
            auto WorldToScreen = [&](float worldX, float worldY) -> ImVec2 {
                float normalizedX = (worldX - viewMinX) / visibleRangeX;
                float normalizedY = (worldY - viewMinY) / visibleRangeY;
                float screenX = graphAreaPos.x + normalizedX * graphAreaSize.x;
                float screenY = graphAreaPos.y + graphAreaSize.y - normalizedY * graphAreaSize.y;
                return ImVec2(screenX, screenY);
            };

            // Screen to World 변환 (포인트 드래그에 사용)
            auto ScreenToWorld = [&](ImVec2 screenPos) -> FVector2D {
                float normalizedX = (screenPos.x - graphAreaPos.x) / graphAreaSize.x;
                float normalizedY = 1.0f - (screenPos.y - graphAreaPos.y) / graphAreaSize.y;
                float worldX = viewMinX + normalizedX * visibleRangeX;
                float worldY = viewMinY + normalizedY * visibleRangeY;
                return FVector2D(worldX, worldY);
            };

            // 마우스 드래그로 팬 이동 또는 포인트 드래그
            if (ImGui::IsWindowHovered())
            {
                ImGuiIO& io = ImGui::GetIO();

                // 왼쪽 버튼 클릭 시작
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    // 포인트를 클릭했는지 확인 (선택된 모듈이 있을 때만)
                    if (SelectedModule)
                    {
                        if (auto* SizeModule = dynamic_cast<UParticleModuleSizeMultiplyLife*>(SelectedModule))
                        {
                            // 키포인트 위치 계산
                            ImVec2 p1Screen = WorldToScreen(SizeModule->Point1Time, SizeModule->Point1Value.X);
                            ImVec2 p2Screen = WorldToScreen(SizeModule->Point2Time, SizeModule->Point2Value.X);

                            // 마우스 위치와의 거리 계산
                            float dist1 = sqrtf((io.MousePos.x - p1Screen.x) * (io.MousePos.x - p1Screen.x) +
                                              (io.MousePos.y - p1Screen.y) * (io.MousePos.y - p1Screen.y));
                            float dist2 = sqrtf((io.MousePos.x - p2Screen.x) * (io.MousePos.x - p2Screen.x) +
                                              (io.MousePos.y - p2Screen.y) * (io.MousePos.y - p2Screen.y));

                            const float clickRadius = 10.0f; // 클릭 가능 반경

                            // 가장 가까운 포인트 찾기
                            if (dist1 < clickRadius && dist1 <= dist2)
                            {
                                DraggingPointIndex = 0; // 점1
                                bDraggingPoint = true;
                            }
                            else if (dist2 < clickRadius)
                            {
                                DraggingPointIndex = 1; // 점2
                                bDraggingPoint = true;
                            }
                            else
                            {
                                // 포인트를 클릭하지 않았으면 팬 모드
                                bCurvePanning = true;
                                CurvePanStart = FVector2D(io.MousePos.x, io.MousePos.y);
                            }
                        }
                        else
                        {
                            // 다른 모듈이면 팬만
                            bCurvePanning = true;
                            CurvePanStart = FVector2D(io.MousePos.x, io.MousePos.y);
                        }
                    }
                    else
                    {
                        // 선택된 모듈이 없으면 팬만
                        bCurvePanning = true;
                        CurvePanStart = FVector2D(io.MousePos.x, io.MousePos.y);
                    }
                }

                // 드래그 중
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    if (bDraggingPoint && SelectedModule)
                    {
                        // 포인트 드래그 중
                        if (auto* SizeModule = dynamic_cast<UParticleModuleSizeMultiplyLife*>(SelectedModule))
                        {
                            FVector2D worldPos = ScreenToWorld(io.MousePos);

                            // X: 시간 (0~100)
                            float time = FMath::Clamp(worldPos.X, 0.0f, 100.0f);
                            // Y: 값 (0~100)
                            float value = FMath::Clamp(worldPos.Y, 0.0f, 100.0f);

                            if (DraggingPointIndex == 0)
                            {
                                // 점1: 시간과 값 모두 변경 가능
                                // 단, 점2보다 왼쪽에 있어야 함
                                SizeModule->Point1Time = FMath::Min(time, SizeModule->Point2Time - 0.1f);
                                SizeModule->Point1Value = FVector(value, value, value);
                            }
                            else if (DraggingPointIndex == 1)
                            {
                                // 점2: 시간과 값 모두 변경 가능
                                // 단, 점1보다 오른쪽에 있어야 함
                                SizeModule->Point2Time = FMath::Max(time, SizeModule->Point1Time + 0.1f);
                                SizeModule->Point2Value = FVector(value, value, value);
                            }
                        }
                    }
                    else if (bCurvePanning)
                    {
                        // 팬 드래그 중
                        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

                        // 스크린 공간 delta를 월드 공간으로 변환
                        float worldDeltaX = -(delta.x / graphAreaSize.x) * visibleRangeX;
                        float worldDeltaY = (delta.y / graphAreaSize.y) * visibleRangeY;

                        CurvePan.X += worldDeltaX;
                        CurvePan.Y += worldDeltaY;

                        // 팬 범위 제한
                        float halfRangeX = visibleRangeX * 0.5f;
                        float halfRangeY = visibleRangeY * 0.5f;
                        CurvePan.X = FMath::Clamp(CurvePan.X, timeMin + halfRangeX, timeMax - halfRangeX);
                        CurvePan.Y = FMath::Clamp(CurvePan.Y, valueMin + halfRangeY, valueMax - halfRangeY);
                    }
                }

                // 마우스 버튼 놓음
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    bCurvePanning = false;
                    bDraggingPoint = false;
                    DraggingPointIndex = -1;
                }
            }

            // 그리드 간격 (줌에 따라 동적 조정)
            float gridStepX = 10.0f;
            float gridStepY = 10.0f;

            // 줌 레벨에 따라 그리드 세분화
            if (CurveZoom >= 2.0f) { gridStepX = 5.0f; gridStepY = 5.0f; }
            if (CurveZoom >= 4.0f) { gridStepX = 2.0f; gridStepY = 2.0f; }
            if (CurveZoom >= 8.0f) { gridStepX = 1.0f; gridStepY = 1.0f; }
            if (CurveZoom >= 16.0f) { gridStepX = 0.5f; gridStepY = 0.5f; }

            // 그리드 시작점 (가시 영역 기준)
            float gridStartX = floor(viewMinX / gridStepX) * gridStepX;
            float gridStartY = floor(viewMinY / gridStepY) * gridStepY;

            // 세로 그리드 라인 (X축 - 시간)
            for (float x = gridStartX; x <= viewMaxX; x += gridStepX)
            {
                ImVec2 p0 = WorldToScreen(x, viewMinY);
                ImVec2 p1 = WorldToScreen(x, viewMaxY);

                if (p0.x >= graphAreaPos.x && p0.x <= graphAreaPos.x + graphAreaSize.x)
                {
                    // 정수 시간은 더 진하게
                    bool isInteger = FMath::Abs(x - floor(x)) < 0.01f;
                    draw_list->AddLine(p0, p1, isInteger ? IM_COL32(100, 100, 100, 255) : IM_COL32(70, 70, 70, 255),
                                      isInteger ? 1.5f : 1.0f);

                    // X축 레이블 (정수만 표시)
                    if (isInteger)
                    {
                        char label[32];
                        snprintf(label, sizeof(label), "%.1f", x);
                        ImVec2 labelSize = ImGui::CalcTextSize(label);
                        ImVec2 labelPos = ImVec2(p0.x - labelSize.x * 0.5f, graphAreaPos.y + graphAreaSize.y + 5);
                        draw_list->AddText(labelPos, IM_COL32(200, 200, 200, 255), label);
                    }
                }
            }

            // 가로 그리드 라인 (Y축 - 값)
            for (float y = gridStartY; y <= viewMaxY; y += gridStepY)
            {
                ImVec2 p0 = WorldToScreen(viewMinX, y);
                ImVec2 p1 = WorldToScreen(viewMaxX, y);

                if (p0.y >= graphAreaPos.y && p0.y <= graphAreaPos.y + graphAreaSize.y)
                {
                    // 정수 값은 더 진하게
                    bool isInteger = FMath::Abs(y - floor(y)) < 0.01f;
                    draw_list->AddLine(p0, p1, isInteger ? IM_COL32(100, 100, 100, 255) : IM_COL32(70, 70, 70, 255),
                                      isInteger ? 1.5f : 1.0f);

                    // Y축 레이블 (정수만 표시)
                    if (isInteger)
                    {
                        char label[32];
                        snprintf(label, sizeof(label), "%.1f", y);
                        ImVec2 labelSize = ImGui::CalcTextSize(label);
                        ImVec2 labelPos = ImVec2(graphAreaPos.x - labelSize.x - 5, p0.y - labelSize.y * 0.5f);
                        draw_list->AddText(labelPos, IM_COL32(200, 200, 200, 255), label);
                    }
                }
            }

            // 축 경계선
            draw_list->AddRect(graphAreaPos, ImVec2(graphAreaPos.x + graphAreaSize.x, graphAreaPos.y + graphAreaSize.y),
                              IM_COL32(100, 100, 100, 255), 0.0f, 0, 2.0f);

            // 선택된 모듈의 커브 그리기
            if (SelectedModule)
            {
                if (auto* SizeModule = dynamic_cast<UParticleModuleSizeMultiplyLife*>(SelectedModule))
                {
                    float p1Time = SizeModule->Point1Time;
                    float p1Value = SizeModule->Point1Value.X;
                    float p2Time = SizeModule->Point2Time;
                    float p2Value = SizeModule->Point2Value.X;

                    // 1. 점1 이전: 왼쪽 끝부터 점1까지 수평선
                    ImVec2 leftStart = WorldToScreen(timeMin, p1Value);
                    ImVec2 leftEnd = WorldToScreen(p1Time, p1Value);
                    draw_list->AddLine(leftStart, leftEnd, IM_COL32(255, 100, 100, 255), 2.0f);

                    // 2. 점1에서 점2까지 선형 보간
                    ImVec2 p1Screen = WorldToScreen(p1Time, p1Value);
                    ImVec2 p2Screen = WorldToScreen(p2Time, p2Value);
                    draw_list->AddLine(p1Screen, p2Screen, IM_COL32(255, 100, 100, 255), 2.0f);

                    // 3. 점2 이후: 점2부터 오른쪽 끝까지 수평선
                    ImVec2 rightStart = WorldToScreen(p2Time, p2Value);
                    ImVec2 rightEnd = WorldToScreen(timeMax, p2Value);
                    draw_list->AddLine(rightStart, rightEnd, IM_COL32(255, 100, 100, 255), 2.0f);

                    // 키포인트 그리기
                    float radius1 = (DraggingPointIndex == 0) ? 8.0f : 6.0f;
                    float radius2 = (DraggingPointIndex == 1) ? 8.0f : 6.0f;

                    // 점1 (빨강)
                    draw_list->AddCircleFilled(p1Screen, radius1, IM_COL32(255, 100, 100, 255));
                    draw_list->AddCircle(p1Screen, radius1 + 1.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);

                    // 점2 (노랑)
                    draw_list->AddCircleFilled(p2Screen, radius2, IM_COL32(255, 255, 100, 255));
                    draw_list->AddCircle(p2Screen, radius2 + 1.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
                }
            }
            else
            {
                // 선택된 모듈 없을 때
                ImVec2 textSize = ImGui::CalcTextSize("Select a curve module from the list");
                ImVec2 textPos = ImVec2(graphAreaPos.x + (graphAreaSize.x - textSize.x) * 0.5f,
                                        graphAreaPos.y + (graphAreaSize.y - textSize.y) * 0.5f);
                draw_list->AddText(textPos, IM_COL32(150, 150, 150, 255), "Select a curve module from the list");
            }

            // 줌 정보 표시
            char zoomText[64];
            snprintf(zoomText, sizeof(zoomText), "Zoom: %.1fx", CurveZoom);
            ImVec2 zoomTextPos = ImVec2(graphPos.x + 10, graphPos.y + 10);
            draw_list->AddText(zoomTextPos, IM_COL32(200, 200, 200, 255), zoomText);

            // 더미 아이템으로 영역 차지
            ImGui::Dummy(graphSize);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::End();
}

void SParticleViewerWindow::OnUpdate(float DeltaSeconds)
{
    if (!Viewport || !ViewportClient)
        return;

    // ViewportClient 업데이트
    if (ViewportClient)
    {
        ViewportClient->Tick(DeltaSeconds);
    }

    // Preview World 업데이트
    if (PreviewWorld)
    {
        PreviewWorld->Tick(DeltaSeconds);
    }
}

void SParticleViewerWindow::OnMouseMove(FVector2D MousePos)
{
    if (!Viewport) return;

    if (CenterRect.Contains(MousePos))
    {
        FVector2D LocalPos = MousePos - FVector2D(CenterRect.Left, CenterRect.Top);
        Viewport->ProcessMouseMove((int32)LocalPos.X, (int32)LocalPos.Y);
    }
}

void SParticleViewerWindow::OnMouseDown(FVector2D MousePos, uint32 Button)
{
    if (!Viewport) return;

    if (CenterRect.Contains(MousePos))
    {
        FVector2D LocalPos = MousePos - FVector2D(CenterRect.Left, CenterRect.Top);
        Viewport->ProcessMouseButtonDown((int32)LocalPos.X, (int32)LocalPos.Y, (int32)Button);
    }
}

void SParticleViewerWindow::OnMouseUp(FVector2D MousePos, uint32 Button)
{
    if (!Viewport) return;

    if (CenterRect.Contains(MousePos))
    {
        FVector2D LocalPos = MousePos - FVector2D(CenterRect.Left, CenterRect.Top);
        Viewport->ProcessMouseButtonUp((int32)LocalPos.X, (int32)LocalPos.Y, (int32)Button);
    }
}

void SParticleViewerWindow::OnRenderViewport()
{
    if (Viewport && CenterRect.GetWidth() > 0 && CenterRect.GetHeight() > 0)
    {
        const uint32 NewStartX = static_cast<uint32>(CenterRect.Left);
        const uint32 NewStartY = static_cast<uint32>(CenterRect.Top);
        const uint32 NewWidth  = static_cast<uint32>(CenterRect.Right - CenterRect.Left);
        const uint32 NewHeight = static_cast<uint32>(CenterRect.Bottom - CenterRect.Top);

        Viewport->Resize(NewStartX, NewStartY, NewWidth, NewHeight);

        // 뷰포트 렌더링 (ImGui보다 먼저)
        Viewport->Render();
    }
}

void SParticleViewerWindow::CreateParticleSystem()
{
    // 빈 ParticleSystem 생성
    UParticleSystem* NewSystem = NewObject<UParticleSystem>();
    SavePath.clear();
    LoadParticleSystem(NewSystem);
}

void SParticleViewerWindow::LoadParticleSystem()
{
    // 다이얼로그로 로드
    FWideString WideInitialPath = UTF8ToWide(ParticlePath.string());
    std::filesystem::path WidePath = FPlatformProcess::OpenLoadFileDialog(WideInitialPath, L"particle",L"Particle Files");
    FString PathStr = WidePath.string();
    
    UParticleSystem* LoadedSystem = RESOURCE.Load<UParticleSystem>(PathStr);
    if (LoadedSystem)
    {
        LoadParticleSystem(LoadedSystem);
        SavePath = PathStr;

        UE_LOG("ParticleSystem loaded from: %s", PathStr.c_str());
    }
}

void SParticleViewerWindow::LoadParticleSystem(UParticleSystem* ParticleSystem)
{
    if (!PreviewWorld || !ParticleSystem) { return; }

    // 별도로 저장되지 않을 때만 Delete (ResourceManager가 관리하고 있지 않은 상태)
    if (SavePath.empty() && CurrentParticleSystem)
    {
        ObjectFactory::DeleteObject(CurrentParticleSystem);
        CurrentParticleSystem = nullptr;
    }
    
    CurrentParticleSystem = ParticleSystem;

    // 기존 PreviewActor가 있으면 제거
    if (PreviewActor)
    {
        // World의 Actor 리스트에서 제거하고 삭제
        PreviewActor->Destroy();
        PreviewActor = nullptr;
        PreviewComponent = nullptr;
    }

    // 새 Actor 생성
    PreviewActor = PreviewWorld->SpawnActor<AActor>();
    PreviewActor->ObjectName = FName("ParticlePreviewActor");
    PreviewActor->SetActorLocation(FVector(0, 0, 0));
    PreviewActor->SetTickInEditor(true);  // 에디터 모드에서도 Tick 활성화

    // ParticleSystemComponent 생성 및 추가
    PreviewComponent = Cast<UParticleSystemComponent>(PreviewActor->AddNewComponent(UParticleSystemComponent::StaticClass()));
    PreviewComponent->SetTemplate(ParticleSystem);

    // Actor의 BeginPlay 호출 (InitializeComponent 호출)
    PreviewActor->BeginPlay();

    // 컴포넌트 초기화 및 활성화 (BeginPlay 이후에!)
    PreviewComponent->InitParticles();
    PreviewComponent->ActivateSystem();
    PreviewComponent->SetActive(true);  // bIsActive를 명시적으로 true로 설정

    UE_LOG("Particle system loaded and spawned in preview world");
}

void SParticleViewerWindow::SaveParticleSystem()
{
    if (!CurrentParticleSystem) { return; }

    // SavePath가 설정되어 있으면 SavePath에 저장
    if (!SavePath.empty())
    {
        if (CurrentParticleSystem->SaveToFile(SavePath))
        {
            UE_LOG("Saved to: %s", SavePath.c_str());
        }
        return; 
    }

    // Create 이후 처음 저장하는 경우
    FWideString WideInitialPath = UTF8ToWide(ParticlePath.string());
    std::filesystem::path WidePath = FPlatformProcess::OpenSaveFileDialog(WideInitialPath, L"particle",L"Particle Files");
    FString PathStr = WidePath.string();
    
    if (!WidePath.empty())
    {
        if (CurrentParticleSystem->SaveToFile(PathStr))
        {
            RESOURCE.Add<UParticleSystem>(PathStr, CurrentParticleSystem);
            SavePath = PathStr;
            UE_LOG("Particle system saved to: %s", SavePath.c_str());
        }
    }
}

void SParticleViewerWindow::CreateNewEmitter()
{
    if (!CurrentParticleSystem)
    {
        UE_LOG("No particle system loaded. Cannot create emitter.");
        return;
    }

    // 새 이미터 생성 (생성자에서 기본 모듈들이 자동으로 생성됨)
    UParticleEmitter* NewEmitter = NewObject<UParticleEmitter>();

    // 파티클 시스템에 이미터 추가
    CurrentParticleSystem->Emitters.Add(NewEmitter);

    // 런타임 캐시 재구축
    CurrentParticleSystem->BuildRuntimeCache();

    // PreviewComponent가 있으면 다시 초기화
    if (PreviewComponent)
    {
        PreviewComponent->InitParticles();
        PreviewComponent->ActivateSystem();
        PreviewComponent->SetActive(true);
    }

    UE_LOG("New emitter created successfully");
}

void SParticleViewerWindow::DeleteSelectedEmitter()
{
    if (!CurrentParticleSystem || !SelectedEmitter)
    {
        UE_LOG("No emitter selected for deletion");
        return;
    }

    // 파티클 시스템에서 이미터 찾기
    int32 EmitterIndex = -1;
    for (int32 i = 0; i < CurrentParticleSystem->Emitters.Num(); i++)
    {
        if (CurrentParticleSystem->Emitters[i] == SelectedEmitter)
        {
            EmitterIndex = i;
            break;
        }
    }

    if (EmitterIndex == -1)
    {
        UE_LOG("Selected emitter not found in particle system");
        SelectedEmitter = nullptr;
        return;
    }

    // 이미터 삭제
    UParticleEmitter* EmitterToDelete = CurrentParticleSystem->Emitters[EmitterIndex];
    CurrentParticleSystem->Emitters.RemoveAt(EmitterIndex);

    // 메모리 해제
    ObjectFactory::DeleteObject(EmitterToDelete);

    // 선택 해제
    SelectedEmitter = nullptr;
    SelectedModule = nullptr;

    // 런타임 캐시 재구축
    CurrentParticleSystem->BuildRuntimeCache();

    // PreviewComponent가 있으면 다시 초기화
    if (PreviewComponent)
    {
        PreviewComponent->InitParticles();
        PreviewComponent->ActivateSystem();
        PreviewComponent->SetActive(true);
    }

    UE_LOG("Emitter deleted successfully");
}