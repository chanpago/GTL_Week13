#include "pch.h"
#include "ParticleViewerWindow.h"
#include "Source/Runtime/Renderer/FViewport.h"
#include "Source/Runtime/Renderer/FViewportClient.h"
#include "Source/Runtime/Renderer/Material.h"
#include "Source/Runtime/Renderer/Shader.h"
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
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleMesh.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleBeam.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleRibbon.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleLocation.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleColorOverLife.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleSizeMultiplyLife.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleRotation.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleRotationRate.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleSubUV.h"
#include "Source/Runtime/Core/Object/ObjectFactory.h"
#include "Source/Runtime/AssetManagement/ResourceManager.h"
#include "Source/Runtime/Engine/Components/ParticleSystemComponent.h"
#include "Texture.h"
#include "World.h"
#include "Actor.h"
#include "CameraActor.h"
#include "imgui.h"
#include "PlatformProcess.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleCollision.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleEventReceiverSpawn.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleVelocityCone.h"

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
	PreviewWorld->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Grid);

	PreviewWorld->GetGizmoActor()->SetSpace(EGizmoSpace::Local);

	// InWorld의 설정 복사
	if (InWorld)
	{
		PreviewWorld->GetRenderSettings().SetShowFlags(InWorld->GetRenderSettings().GetShowFlags());
		PreviewWorld->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_EditorIcon);
		PreviewWorld->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Grid);
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
    Camera->SetRotationFromEulerAngles(FVector(0, 30, -180));

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
	RenderMenuBar();

	// 2. 툴바
	RenderToolbar();

	// 3. 메인 컨텐츠 영역 - 4분할 십자 레이아웃
	ImVec2 contentSize = ImGui::GetContentRegionAvail();
	const float splitterSize = 4.0f;

	// 스플리터 비율 기반 크기 계산
	const float leftPanelWidth = contentSize.x * LeftPanelRatio;
	const float rightPanelWidth = contentSize.x - leftPanelWidth - splitterSize;

	// 좌측: 상단(뷰포트) / 하단(Properties)
	const float leftTopHeight = contentSize.y * (1.0f - LeftBottomRatio) - splitterSize;
	const float leftBottomHeight = contentSize.y * LeftBottomRatio;

	// 우측: 상단(이미터) / 하단(커브에디터)
	const float rightTopHeight = contentSize.y * (1.0f - RightBottomRatio) - splitterSize;
	const float rightBottomHeight = contentSize.y * RightBottomRatio;

	// ===== 좌측 컬럼 (뷰포트 + Properties) =====
	ImGui::BeginChild("LeftColumn", ImVec2(leftPanelWidth, contentSize.y), false);
	{
		// 좌측 패널 클릭 시 이미터 선택 해제
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			SelectedEmitter = nullptr;
		}

		// 뷰포트 (상단)
		RenderViewportPanel(0, leftTopHeight);

		// 좌측 수평 스플리터
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
		ImGui::Button("##LeftHSplitter", ImVec2(-1, splitterSize));
		if (ImGui::IsItemActive())
		{
			float delta = ImGui::GetIO().MouseDelta.y;
			LeftBottomRatio -= delta / contentSize.y;
			LeftBottomRatio = FMath::Clamp(LeftBottomRatio, 0.15f, 0.7f);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		ImGui::PopStyleColor(3);

		// Properties (하단)
		ImGui::BeginChild("Particle System", ImVec2(0, leftBottomHeight), true);
		{
			ImGui::Text("디테일");
			ImGui::Separator();

			if (SelectedModule)
			{
				ImGui::Text("Selected Module: %s", SelectedModule->GetClass()->Name);
				ImGui::Separator();

				// 모듈 타입별로 속성 표시
				if (auto* RequiredModule = Cast<UParticleModuleRequired>(SelectedModule))
				{
					ImGui::Spacing();

					// Materials 섹션 (스태틱메쉬 디테일 패널 스타일)
					if (ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_DefaultOpen))
					{
						// 머터리얼 텍스처 가져오기
						UTexture* DiffuseTexture = RequiredModule->Material ? RequiredModule->Material->GetTexture(EMaterialTextureSlot::Diffuse) : nullptr;

						// 머터리얼 선택 콤보박스 (텍스처 미리보기 포함)
						ImGui::Columns(2, "MaterialSelectColumns", false);
						ImGui::SetColumnWidth(0, 50.0f);

						// 미리보기 정사각형 (40x40)
						if (DiffuseTexture && DiffuseTexture->GetShaderResourceView())
						{
							ImGui::Image((void*)DiffuseTexture->GetShaderResourceView(), ImVec2(40, 40));
						}
						else
						{
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
							ImGui::Button("##matpreview", ImVec2(40, 40));
							ImGui::PopStyleColor();
						}
						ImGui::NextColumn();

						FString currentMaterialPath = RequiredModule->Material ? RequiredModule->Material->GetFilePath() : "None";
						ImGui::SetNextItemWidth(-1);
						if (ImGui::BeginCombo("##MaterialCombo", currentMaterialPath.c_str()))
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

										if (ImGui::Selectable(Mat->GetFilePath().c_str(), isSelected))
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
						ImGui::Columns(1);

						// MaterialSlots[0] TreeNode
						if (ImGui::TreeNodeEx("MaterialSlots [0]", ImGuiTreeNodeFlags_DefaultOpen))
						{
							if (RequiredModule->Material)
							{
								UMaterialInterface* CurrentMaterial = RequiredModule->Material;

								ImGui::Columns(2, "MaterialDetailsColumns", false);
								ImGui::SetColumnWidth(0, 150.0f);

								// Shader (읽기 전용)
								ImGui::Text("Shader");
								ImGui::NextColumn();
								UShader* CurrentShader = CurrentMaterial->GetShader();
								FString ShaderPath = CurrentShader ? CurrentShader->GetFilePath() : "None";
								ImGui::SetNextItemWidth(-1);
								ImGui::BeginDisabled(true);
								char shaderBuffer[512];
								strncpy_s(shaderBuffer, sizeof(shaderBuffer), ShaderPath.c_str(), _TRUNCATE);
								ImGui::InputText("##Shader", shaderBuffer, sizeof(shaderBuffer), ImGuiInputTextFlags_ReadOnly);
								ImGui::EndDisabled();
								ImGui::NextColumn();

								// MacroKey (읽기 전용)
								ImGui::Text("MacroKey");
								ImGui::NextColumn();
								FString MacroKey = UShader::GenerateMacrosToString(CurrentMaterial->GetShaderMacros());
								char macroBuffer[512];
								strncpy_s(macroBuffer, sizeof(macroBuffer), MacroKey.c_str(), _TRUNCATE);
								ImGui::SetNextItemWidth(-1);
								ImGui::BeginDisabled(true);
								ImGui::InputText("##MacroKey", macroBuffer, sizeof(macroBuffer), ImGuiInputTextFlags_ReadOnly);
								ImGui::EndDisabled();
								ImGui::NextColumn();

								// 모든 텍스처 가져오기 (Diffuse/Normal 선택용)
								TArray<UTexture*> AllTextures = UResourceManager::GetInstance().GetAll<UTexture>();

								// MID 확인 및 필요시 생성하는 람다
								auto EnsureMID = [&]() -> UMaterialInstanceDynamic* {
									UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(RequiredModule->Material);
									if (!MID)
									{
										// 기존 Material을 부모로 MID 생성
										MID = UMaterialInstanceDynamic::Create(RequiredModule->Material);
										if (MID)
										{
											RequiredModule->Material = MID;
											CurrentMaterial = MID;
										}
									}
									return MID;
									};

								// Diffuse Texture
								ImGui::Text("Diffuse Texture");
								ImGui::NextColumn();
								{
									UTexture* DiffTex = CurrentMaterial->GetTexture(EMaterialTextureSlot::Diffuse);
									if (DiffTex && DiffTex->GetShaderResourceView())
									{
										ImGui::Image((void*)DiffTex->GetShaderResourceView(), ImVec2(30, 30));
										ImGui::SameLine();
									}
									FString DiffTexPath = DiffTex ? DiffTex->GetFilePath() : "None";
									ImGui::SetNextItemWidth(-1);
									if (ImGui::BeginCombo("##DiffuseTexture", DiffTexPath.c_str()))
									{
										// None 옵션
										if (ImGui::Selectable("None##DiffNone", DiffTex == nullptr))
										{
											if (UMaterialInstanceDynamic* MID = EnsureMID())
											{
												MID->SetTextureParameterValue(EMaterialTextureSlot::Diffuse, nullptr);
											}
										}
										ImGui::Separator();

										for (int i = 0; i < AllTextures.Num(); i++)
										{
											UTexture* Tex = AllTextures[i];
											if (Tex)
											{
												ImGui::PushID(i);
												bool isSelected = (DiffTex == Tex);

												if (Tex->GetShaderResourceView())
												{
													ImGui::Image((void*)Tex->GetShaderResourceView(), ImVec2(24, 24));
													ImGui::SameLine();
												}

												if (ImGui::Selectable(Tex->GetFilePath().c_str(), isSelected))
												{
													if (UMaterialInstanceDynamic* MID = EnsureMID())
													{
														MID->SetTextureParameterValue(EMaterialTextureSlot::Diffuse, Tex);
													}
												}
												ImGui::PopID();
											}
										}
										ImGui::EndCombo();
									}
								}
								ImGui::NextColumn();

								// Normal Texture
								ImGui::Text("Normal Texture");
								ImGui::NextColumn();
								{
									UTexture* NormTex = CurrentMaterial->GetTexture(EMaterialTextureSlot::Normal);
									if (NormTex && NormTex->GetShaderResourceView())
									{
										ImGui::Image((void*)NormTex->GetShaderResourceView(), ImVec2(30, 30));
										ImGui::SameLine();
									}
									FString NormTexPath = NormTex ? NormTex->GetFilePath() : "None";
									ImGui::SetNextItemWidth(-1);
									if (ImGui::BeginCombo("##NormalTexture", NormTexPath.c_str()))
									{
										// None 옵션
										if (ImGui::Selectable("None##NormNone", NormTex == nullptr))
										{
											if (UMaterialInstanceDynamic* MID = EnsureMID())
											{
												MID->SetTextureParameterValue(EMaterialTextureSlot::Normal, nullptr);
											}
										}
										ImGui::Separator();

										for (int i = 0; i < AllTextures.Num(); i++)
										{
											UTexture* Tex = AllTextures[i];
											if (Tex)
											{
												ImGui::PushID(1000 + i); // Diffuse와 ID 충돌 방지
												bool isSelected = (NormTex == Tex);

												if (Tex->GetShaderResourceView())
												{
													ImGui::Image((void*)Tex->GetShaderResourceView(), ImVec2(24, 24));
													ImGui::SameLine();
												}

												if (ImGui::Selectable(Tex->GetFilePath().c_str(), isSelected))
												{
													if (UMaterialInstanceDynamic* MID = EnsureMID())
													{
														MID->SetTextureParameterValue(EMaterialTextureSlot::Normal, Tex);
													}
												}
												ImGui::PopID();
											}
										}
										ImGui::EndCombo();
									}
								}
								ImGui::NextColumn();

								ImGui::Separator();

								// FMaterialInfo 속성들 (편집 가능)
								FMaterialInfo Info = CurrentMaterial->GetMaterialInfo();
								bool bInfoChanged = false;

								// Colors
								FLinearColor TempColor;

								// Diffuse Color
								TempColor = FLinearColor(Info.DiffuseColor);
								ImGui::Text("Diffuse Color");
								ImGui::NextColumn();
								if (ImGui::ColorEdit3("##DiffuseColor", &TempColor.R))
								{
									Info.DiffuseColor = FVector(TempColor.R, TempColor.G, TempColor.B);
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Ambient Color
								TempColor = FLinearColor(Info.AmbientColor);
								ImGui::Text("Ambient Color");
								ImGui::NextColumn();
								if (ImGui::ColorEdit3("##AmbientColor", &TempColor.R))
								{
									Info.AmbientColor = FVector(TempColor.R, TempColor.G, TempColor.B);
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Specular Color
								TempColor = FLinearColor(Info.SpecularColor);
								ImGui::Text("Specular Color");
								ImGui::NextColumn();
								if (ImGui::ColorEdit3("##SpecularColor", &TempColor.R))
								{
									Info.SpecularColor = FVector(TempColor.R, TempColor.G, TempColor.B);
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Emissive Color
								TempColor = FLinearColor(Info.EmissiveColor);
								ImGui::Text("Emissive Color");
								ImGui::NextColumn();
								if (ImGui::ColorEdit3("##EmissiveColor", &TempColor.R))
								{
									Info.EmissiveColor = FVector(TempColor.R, TempColor.G, TempColor.B);
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Transmission Filter
								TempColor = FLinearColor(Info.TransmissionFilter);
								ImGui::Text("Transmission Filter");
								ImGui::NextColumn();
								if (ImGui::ColorEdit3("##TransmissionFilter", &TempColor.R))
								{
									Info.TransmissionFilter = FVector(TempColor.R, TempColor.G, TempColor.B);
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Scalar 값들

								// Specular Exponent
								ImGui::Text("Specular Exponent");
								ImGui::NextColumn();
								ImGui::SetNextItemWidth(-1);
								if (ImGui::DragFloat("##SpecularExponent", &Info.SpecularExponent, 1.0f, 0.0f, 1024.0f))
								{
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Transparency
								ImGui::Text("Transparency");
								ImGui::NextColumn();
								ImGui::SetNextItemWidth(-1);
								if (ImGui::DragFloat("##Transparency", &Info.Transparency, 0.01f, 0.0f, 1.0f))
								{
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Optical Density
								ImGui::Text("Optical Density");
								ImGui::NextColumn();
								ImGui::SetNextItemWidth(-1);
								if (ImGui::DragFloat("##OpticalDensity", &Info.OpticalDensity, 0.01f, 0.0f, 10.0f))
								{
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Bump Multiplier
								ImGui::Text("Bump Multiplier");
								ImGui::NextColumn();
								ImGui::SetNextItemWidth(-1);
								if (ImGui::DragFloat("##BumpMultiplier", &Info.BumpMultiplier, 0.01f, 0.0f, 5.0f))
								{
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// Illum Model
								ImGui::Text("Illum Model");
								ImGui::NextColumn();
								ImGui::SetNextItemWidth(-1);
								if (ImGui::DragInt("##IllumModel", &Info.IlluminationModel, 1, 0, 10))
								{
									bInfoChanged = true;
								}
								ImGui::NextColumn();

								// 값이 변경되었으면 MaterialInfo 업데이트 (UMaterial인 경우만)
								if (bInfoChanged)
								{
									if (UMaterial* Mat = Cast<UMaterial>(CurrentMaterial))
									{
										Mat->SetMaterialInfo(Info);
									}
								}

								ImGui::Columns(1);
							}
							else
							{
								ImGui::TextDisabled("No Material Selected");
							}
							ImGui::TreePop();
						}
						ImGui::TreePop();
					}

					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Spacing();

					// 2열 레이아웃 시작 (나머지 Required 모듈 속성들)
					ImGui::Columns(2, "RequiredModuleColumns", false);
					ImGui::SetColumnWidth(0, 150.0f);

                    // Screen Alignment
                    {
                        ImGui::Text("Screen Alignment");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("파티클의 화면 정렬 방식\n- Camera Facing: 항상 카메라를 향함 (빌보드)\n- Velocity: 이동 방향으로 정렬 (연기 꼬리 등)");
                        }
                        ImGui::NextColumn();

                        const char* screenAlignments[] = { "Camera Facing", "Velocity" };
                        int currentAlignment = static_cast<int>(RequiredModule->ScreenAlignment);

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##ScreenAlignmentCombo", screenAlignments[currentAlignment]))
                        {
                            for (int i = 0; i < IM_ARRAYSIZE(screenAlignments); i++)
                            {
                                bool isSelected = (currentAlignment == i);
                                if (ImGui::Selectable(screenAlignments[i], isSelected))
                                {
                                    RequiredModule->ScreenAlignment = static_cast<EScreenAlignment>(i);
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
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("파티클 정렬 방식 (반투명 렌더링 순서)\n- None: 정렬 안함 (빠름)\n- By Distance: 카메라와의 거리순\n- By Age: 생성 순서 (오래된 순)\n- By View Depth: 뷰 깊이 순");
                        }
                        ImGui::NextColumn();

						// EParticleSortMode 순서와 맞출 것!
						const char* sortModes[] = { "None", "By Distance", "By Age", "By View Depth" };
						int currentSortMode = (int)RequiredModule->SortMode;

						ImGui::SetNextItemWidth(-1);
						if (ImGui::BeginCombo("##SortModeCombo", sortModes[currentSortMode]))
						{
							for (int i = 0; i < IM_ARRAYSIZE(sortModes); i++)
							{
								bool isSelected = (currentSortMode == i);
								if (ImGui::Selectable(sortModes[i], isSelected))
								{
									RequiredModule->SortMode = (EParticleSortMode)i;
								}
							}
							ImGui::EndCombo();
						}

						ImGui::NextColumn();
					}

					ImGui::Spacing();

                    // Emitter 별 Priority
                    {
                        ImGui::Text("Emitter Sort Priority");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("에미터 렌더링 우선순위\n낮은 값이 먼저 렌더링됩니다.\n여러 에미터 간의 렌더링 순서 조절에 사용.");
                        }
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragInt("##SortPriority", &RequiredModule->SortPriority, 1, 1, 10000);
                        ImGui::NextColumn();
                    }

					ImGui::Spacing();

                    // Max Particles
                    {
                        ImGui::Text("Max Particles");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("동시에 존재할 수 있는 최대 파티클 수\n성능과 메모리 사용량에 직접적인 영향.\n너무 높으면 성능 저하, 너무 낮으면 파티클이 잘림.");
                        }
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragInt("##MaxParticles", &RequiredModule->MaxParticles, 1, 1, 10000);
                        ImGui::NextColumn();
                    }

					ImGui::Spacing();

                    // Emitter Duration
                    {
                        ImGui::Text("Emitter Duration");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("에미터의 총 재생 시간 (초)\n이 시간 동안 파티클을 생성합니다.\n0 = 무한 재생 (수동으로 정지할 때까지)");
                        }
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragFloat("##EmitterDuration", &RequiredModule->EmitterDuration, 0.01f, 0.0f, 100.0f);
                        ImGui::NextColumn();
                    }

					ImGui::Spacing();

                    // Emitter Delay
                    {
                        ImGui::Text("Emitter Delay");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("에미터 시작 지연 시간 (초)\n파티클 시스템 활성화 후\n이 시간이 지나야 파티클 생성 시작.");
                        }
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragFloat("##EmitterDelay", &RequiredModule->EmitterDelay, 0.01f, 0.0f, 10.0f);
                        ImGui::NextColumn();
                    }

					ImGui::Spacing();

                    // Emitter Loops
                    {
                        ImGui::Text("Emitter Loops");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("에미터 반복 횟수\n0 = 무한 반복\n1 = 한 번만 재생 (One-Shot 효과)\nN = N번 반복 후 정지");
                        }
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragInt("##EmitterLoops", &RequiredModule->EmitterLoops, 1.0f, 0, 100);
                        ImGui::NextColumn();
                    }

					ImGui::Spacing();

                    // Spawn Rate Base
                    {
                        ImGui::Text("Spawn Rate Base");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("기본 스폰 비율 (초당 파티클 수)\nSpawn 모듈의 Rate와 곱해져서 최종 스폰률 결정.\n기본 베이스 라인으로 사용됩니다.");
                        }
                        ImGui::NextColumn();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragFloat("##SpawnRateBase", &RequiredModule->SpawnRateBase, 0.1f, 0.0f, 1000.0f);
                        ImGui::NextColumn();
                    }

					ImGui::Spacing();

                    // Use Local Space
                    {
                        ImGui::Text("Use Local Space");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("파티클 좌표계 설정\n- Local: 액터를 따라다님 (로켓 엔진, 꼬리 효과)\n- World: 생성 위치에 고정 (폭발, 연기)");
                        }
                        ImGui::NextColumn();

						if (ImGui::Checkbox("##UseLocalSpace", &RequiredModule->bUseLocalSpace))
						{
							// 값이 변경되면 파티클 시스템 재시작
							if (CurrentParticleSystem && PreviewComponent)
							{
								CurrentParticleSystem->BuildRuntimeCache();
								PreviewComponent->ResetAndActivate();
							}
						}

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(RequiredModule->bUseLocalSpace
                                ? "Local Space: 파티클이 액터를 따라다닙니다 (예: 로켓 엔진)"
                                : "World Space: 파티클이 생성 위치에 고정됩니다 (예: 폭발)");
                        }

						ImGui::NextColumn();
					}

					ImGui::Spacing();

                    // SubUV Settings (스프라이트 시트 애니메이션)
                    {
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "SubUV (Sprite Sheet)");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("스프라이트 시트 애니메이션 설정\n하나의 텍스처에 여러 프레임을 배치하여\n애니메이션 효과를 만듭니다.\nSubUV 모듈과 함께 사용됩니다.");
                        }
                        ImGui::NextColumn();
                        ImGui::NextColumn();
                    }

					ImGui::Spacing();

                    {
                        ImGui::Text("SubImages Horizontal");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("스프라이트 시트의 가로 분할 수\n텍스처가 가로로 몇 개의 프레임으로 나뉘는지 설정.");
                        }
                        ImGui::NextColumn();
                        ImGui::DragInt("##SubImagesH", &RequiredModule->SubImages_Horizontal, 1.0f, 1, 16);
                        ImGui::NextColumn();
                    }

                    {
                        ImGui::Text("SubImages Vertical");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("스프라이트 시트의 세로 분할 수\n텍스처가 세로로 몇 개의 프레임으로 나뉘는지 설정.\n총 프레임 수 = Horizontal x Vertical");
                        }
                        ImGui::NextColumn();
                        ImGui::DragInt("##SubImagesV", &RequiredModule->SubImages_Vertical, 1.0f, 1, 16);
                        ImGui::NextColumn();
                    }

                    // 2열 레이아웃 종료
                    ImGui::Columns(1);
                }
                else if (auto* SpawnModule = Cast<UParticleModuleSpawn>(SelectedModule))
                {
                    ImGui::Text("Spawn Settings");
                    ImGui::Separator();

                    // --- 1. Spawn Mode Selection ---
                    ImGui::Text("Rate Type");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클 생성 방식\n- Constant: 초당 일정 수 연속 생성\n- Curve (Over Time): 시간에 따라 생성률 변화\n- Burst: 특정 시점에 한꺼번에 생성");
                    }
                    ImGui::SameLine();
                    const char* SpawnTypes[] = {"Constant (Continuous)", "Curve (Over Time)", "Burst"};
                    int CurrentType = static_cast<int>(SpawnModule->SpawnRateType);
                    if (ImGui::Combo("##RateType", &CurrentType, SpawnTypes, IM_ARRAYSIZE(SpawnTypes)))
                    {
                        SpawnModule->SpawnRateType = static_cast<ESpawnRateType>(CurrentType);
                    }

                    ImGui::Spacing();

                    // --- Helper Lambda: Distribution 그리기 (중복 제거용) ---
                    auto DrawFloatDist = [](const char* Label, FRawDistributionFloat& Dist)
                    {
                        ImGui::PushID(Label); // ID 충돌 방지
                        ImGui::Text("%s", Label);

                        if (Dist.bUseRange)
                        {
                            // Range 모드: Min ~ Max
                            float MinVal = Dist.MinValue;
                            float MaxVal = Dist.MaxValue;

                            // 한 줄에 보기 좋게 배치
                            ImGui::PushItemWidth(100);
                            if (ImGui::DragFloat("Min", &MinVal, 0.1f)) Dist.MinValue = MinVal;
                            ImGui::SameLine();
                            if (ImGui::DragFloat("Max", &MaxVal, 0.1f)) Dist.MaxValue = MaxVal;
                            ImGui::PopItemWidth();
                        }
                        else
                        {
                            // Constant 모드: Value 하나
                            float Val = Dist.MinValue;
                            if (ImGui::DragFloat("Value", &Val, 0.1f))
                            {
                                Dist.MinValue = Val;
                                Dist.MaxValue = Val; // Range 안 쓸 땐 Max도 같이 맞춰줌 (안전빵)
                            }
                        }

                        ImGui::SameLine();
                        ImGui::Checkbox("Range", &Dist.bUseRange);
                        ImGui::PopID();
                    };

                    // --- 2. Rate Configuration (타입에 따라 다른 변수 노출) ---
                    if (SpawnModule->SpawnRateType == ESpawnRateType::Constant)
                    {
                        ImGui::Text("Spawn Rate (Per Second)");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("초당 생성할 파티클 수\n예: 10 = 초당 10개 파티클 생성\nRange 체크 시 Min~Max 사이 랜덤 값 사용");
                        }
                        DrawFloatDist("Spawn Rate (Per Second)", SpawnModule->SpawnRate);
                    }
                    else if (SpawnModule->SpawnRateType == ESpawnRateType::OverTime) // OverTime
                    {
                        ImGui::Text("Spawn Rate (Scale Over Life)");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("시간에 따른 스폰 비율 스케일\n에미터 Duration 동안 생성률 변화\n기본 SpawnRateBase에 곱해짐");
                        }
                        DrawFloatDist("Spawn Rate (Scale Over Life)", SpawnModule->SpawnRateOverTime);
                        ImGui::TextDisabled("Note: Multiplies base rate by curve over emitter duration.");
                    }
                    else
                    {
                        // --- 3. Burst List (배열 관리) ---
                        ImGui::Text("Burst List");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("버스트 스폰 목록\n특정 시점에 한꺼번에 파티클 생성\n폭발, 총 발사, 충돌 효과 등에 적합");
                        }
                        ImGui::SameLine();
                        // (+) 버튼으로 항목 추가
                        if (ImGui::Button("Add##Burst"))
                        {
                            SpawnModule->BurstList.Add(UParticleModuleSpawn::FBurstEntry(0.0f, 10, 0));
                        }

                        ImGui::Spacing();

                        if (SpawnModule->BurstList.Num() == 0)
                        {
                            ImGui::TextDisabled("No burst entries.");
                        }
                        else
                        {
                            // 테이블 헤더 느낌
                            ImGui::Columns(4, "BurstColumns", false);
                            ImGui::SetColumnWidth(0, 60); // Time
                            ImGui::SetColumnWidth(1, 80); // Count
                            ImGui::SetColumnWidth(2, 80); // Range

                            ImGui::Text("Time");
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("버스트 발생 시점 (0~1)\n0.0 = 시작, 0.5 = 중간, 1.0 = 끝");
                            ImGui::NextColumn();
                            ImGui::Text("Count");
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("기본 생성 파티클 수");
                            ImGui::NextColumn();
                            ImGui::Text("Range");
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 수 랜덤 범위\n실제 생성 수 = Count ± Range");
                            ImGui::NextColumn();
                            ImGui::Text("Del");
                            ImGui::NextColumn();
                            ImGui::Separator();

                            int IndexToRemove = -1;

                            for (int i = 0; i < SpawnModule->BurstList.Num(); ++i)
                            {
                                UParticleModuleSpawn::FBurstEntry& Entry = SpawnModule->BurstList[i];

                                ImGui::PushID(i); // Loop 안에서는 ID 필수

                                // 1. Time
                                ImGui::DragFloat("##Time", &Entry.Time, 0.01f, 0.0f, 1.0f, "%.2f");
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("버스트 발생 시점\n0.0 = Duration 시작\n1.0 = Duration 끝");
                                ImGui::NextColumn();

                                // 2. Count
                                ImGui::DragInt("##Count", &Entry.Count, 1, 0, 1000);
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("이 버스트에서 생성할 파티클 수");
                                ImGui::NextColumn();

                                // 3. Range (Variance)
                                ImGui::DragInt("##Range", &Entry.CountRange, 1, 0, 1000);
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 수 랜덤 변동 범위\n최종 생성 수 = Count ± Range");
                                ImGui::NextColumn();

                                // 4. Delete Button
                                if (ImGui::Button("X"))
                                {
                                    IndexToRemove = i;
                                }
                                ImGui::NextColumn();

                                ImGui::PopID();
                            }

                            ImGui::Columns(1); // 컬럼 복구

                            // 삭제 처리 (Loop 밖에서 안전하게)
                            if (IndexToRemove != -1)
                            {
                                SpawnModule->BurstList.RemoveAt(IndexToRemove);
                            }
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                }
                else if (auto* LifetimeModule = Cast<UParticleModuleLifetime>(SelectedModule))
                {
                    ImGui::Text("Lifetime Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("개별 파티클의 수명 설정\n파티클이 생성된 후 소멸될 때까지의 시간(초)");
                    }
                    ImGui::Separator();
                    if (LifetimeModule->Lifetime.bUseRange)
                    {
                        ImGui::DragFloat("Lifetime Min", &LifetimeModule->Lifetime.MinValue, 0.01f, 0.0f, 100.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 최소 수명 (초)");
                        ImGui::DragFloat("Lifetime Max", &LifetimeModule->Lifetime.MaxValue, 0.01f, 0.0f, 100.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 최대 수명 (초)\n실제 수명은 Min~Max 사이 랜덤");
                    }
                    else
                    {
                        ImGui::DragFloat("Lifetime", &LifetimeModule->Lifetime.MinValue, 0.01f, 0.0f, 100.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 수명 (초)\n이 시간이 지나면 파티클 소멸");
                    }
                    ImGui::Checkbox("Use Range", &LifetimeModule->Lifetime.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 수명 범위 사용\n체크 시 Min~Max 범위 내 랜덤 수명");
                }
                else if (auto* SizeModule = Cast<UParticleModuleSize>(SelectedModule))
                {
                    ImGui::Text("Size Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클의 초기 크기 설정\nX, Y, Z 축 개별 스케일 가능");
                    }
                    ImGui::Separator();
                    if (SizeModule->StartSize.bUseRange)
                    {
                        ImGui::DragFloat3("Start Size Min", &SizeModule->StartSize.MinValue.X, 1.0f, 0.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 최소 크기 (X, Y, Z)\n빌보드는 X, Y만 사용, 메시는 XYZ 모두 사용");
                        ImGui::DragFloat3("Start Size Max", &SizeModule->StartSize.MaxValue.X, 1.0f, 0.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 최대 크기 (X, Y, Z)\n실제 크기는 Min~Max 사이 랜덤");
                    }
                    else
                    {
                        ImGui::DragFloat3("Start Size", &SizeModule->StartSize.MinValue.X, 1.0f, 0.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 크기 (X, Y, Z)\n빌보드는 X=가로, Y=세로\n메시는 3D 스케일");
                    }
                    ImGui::Checkbox("Use Range", &SizeModule->StartSize.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 크기 범위 사용\n다양한 크기의 파티클 생성");
                }
                else if (auto* LocationModule = Cast<UParticleModuleLocation>(SelectedModule))
                {
                    ImGui::Text("Location Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클 생성 위치 설정\n에미터 원점 기준 상대 위치");
                    }
                    ImGui::Separator();

                    // Distribution Type
                    ImGui::Text("Distribution Type");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("위치 분포 방식\n- Point: 특정 점/범위\n- Box: 박스 영역 내 랜덤\n- Sphere: 구 영역 내 랜덤\n- Cylinder: 원기둥 영역 내 랜덤");
                    }
                    ImGui::SameLine();
                    const char* DistTypes[] = { "Point", "Box", "Sphere", "Cylinder" };
                    int CurrentDistType = (int)LocationModule->DistributionType;
                    if (ImGui::Combo("##DistributionType", &CurrentDistType, DistTypes, IM_ARRAYSIZE(DistTypes)))
                    {
                        LocationModule->DistributionType = (ELocationDistributionType)CurrentDistType;
                    }

					ImGui::Spacing();

                    // 타입별 파라미터
                    switch (LocationModule->DistributionType)
                    {
                    case ELocationDistributionType::Point:
                        ImGui::DragFloat3("Start Location Min", &LocationModule->StartLocation.MinValue.X, 1.0f, -1000.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 생성 최소 위치 (X, Y, Z)");
                        ImGui::DragFloat3("Start Location Max", &LocationModule->StartLocation.MaxValue.X, 1.0f, -1000.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 생성 최대 위치 (X, Y, Z)\n실제 위치는 Min~Max 사이 랜덤");
                        ImGui::Checkbox("Use Range", &LocationModule->StartLocation.bUseRange);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 위치 범위 사용");
                        break;

                    case ELocationDistributionType::Box:
                        ImGui::DragFloat3("Box Extent", &LocationModule->BoxExtent.X, 1.0f, 0.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("박스 반경 (X, Y, Z)\n원점 중심 ±Extent 범위 내 랜덤 생성");
                        break;

                    case ELocationDistributionType::Sphere:
                        ImGui::DragFloat("Sphere Radius", &LocationModule->SphereRadius, 1.0f, 0.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("구 반지름\n원점 중심 구 영역 내 랜덤 생성\n폭발 효과 등에 적합");
                        break;

                    case ELocationDistributionType::Cylinder:
                        ImGui::DragFloat("Cylinder Radius", &LocationModule->CylinderRadius, 1.0f, 0.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("원기둥 반지름\nXY 평면의 원형 영역");
                        ImGui::DragFloat("Cylinder Height", &LocationModule->CylinderHeight, 1.0f, 0.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("원기둥 높이\nZ축 방향 범위");
                        break;
                    }
                }
                else if (auto* VelocityModule = Cast<UParticleModuleVelocity>(SelectedModule))
                {
                    ImGui::Text("Velocity Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클의 초기 속도와 중력 설정\n방향 + 속력으로 이동 방향 결정");
                    }
                    ImGui::Separator();
                    if (VelocityModule->StartVelocity.bUseRange)
                    {
                        ImGui::DragFloat3("Start Velocity Min", &VelocityModule->StartVelocity.MinValue.X, 1.0f,
                                          -1000.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 속도 최소값 (X, Y, Z)\n단위: 유닛/초");
                        ImGui::DragFloat3("Start Velocity Max", &VelocityModule->StartVelocity.MaxValue.X, 1.0f,
                                          -1000.0f, 1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 속도 최대값 (X, Y, Z)\n실제 속도는 Min~Max 사이 랜덤");
                    }
                    else
                    {
                        ImGui::DragFloat3("Start Velocity", &VelocityModule->StartVelocity.MinValue.X, 1.0f, -1000.0f,
                                          1000.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 속도 (X, Y, Z)\n예: (0, 0, 100) = 위로 상승");
                    }
                    ImGui::Checkbox("Use Range", &VelocityModule->StartVelocity.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 속도 범위 사용\n퍼지는 효과 생성에 유용");
                    ImGui::DragFloat3("Gravity", &VelocityModule->Gravity.X, 1.0f, -10000.0f, 10000.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("중력 가속도 (X, Y, Z)\n예: (0, 0, -980) = 지구 중력\n파티클이 포물선 운동");
                }
                else if (auto* ConeModule = Cast<UParticleModuleVelocityCone>(SelectedModule))
                {
                    ImGui::Text("Velocity Cone Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("원뿔 형태로 퍼지는 속도 설정\n분사, 폭발, 스프레이 효과에 적합");
                    }
                    ImGui::Separator();

                    // 1. 속도 (세기)
                    ImGui::Text("Velocity (Speed)");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 이동 속력 (유닛/초)");
                    ImGui::DragFloat("Min##Vel", &ConeModule->Velocity.MinValue, 1.0f, 0.0f, 10000.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("최소 속력");
                    ImGui::DragFloat("Max##Vel", &ConeModule->Velocity.MaxValue, 1.0f, 0.0f, 10000.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("최대 속력\n실제 속력은 Min~Max 사이 랜덤");
                    ImGui::Checkbox("Range##Vel", &ConeModule->Velocity.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 속력 범위 사용");

                    ImGui::Spacing();

                    // 2. 각도 (퍼짐 정도)
                    ImGui::Text("Cone Angle (Degrees)");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("원뿔 퍼짐 각도 (도)\n0 = 직선, 90 = 반구, 180 = 구");
                    ImGui::DragFloat("Angle##Cone", &ConeModule->Angle.MinValue, 0.5f, 0.0f, 180.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0° = 직선 발사\n45° = 좁은 원뿔\n90° = 반구형 퍼짐\n180° = 모든 방향 (구형)");

                    ImGui::Spacing();

                    // 3. 방향 (Direction)
                    ImGui::Text("Cone Direction");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("원뿔 중심 방향 벡터\n(0,0,1) = 위로, (1,0,0) = X+ 방향");
                    float Dir[3] = { ConeModule->Direction.X, ConeModule->Direction.Y, ConeModule->Direction.Z };
                    if (ImGui::DragFloat3("Dir", Dir, 0.01f, -1.0f, 1.0f))
                    {
                        ConeModule->Direction = FVector(Dir[0], Dir[1], Dir[2]);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("방향 벡터 (X, Y, Z)\n정규화 버튼으로 단위 벡터 변환 권장");
                    if (ImGui::Button("Normalize Direction"))
                    {
                        ConeModule->Direction.Normalize();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("방향 벡터를 단위 벡터로 정규화\n길이 1로 만들어 일관된 동작 보장");
                }
                else if (auto* ColorModule = Cast<UParticleModuleColor>(SelectedModule))
                {
                    ImGui::Text("Color Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클의 초기 색상과 투명도 설정\n머터리얼 색상에 곱해집니다");
                    }
                    ImGui::Separator();
                    if (ColorModule->StartColor.bUseRange)
                    {
                        ImGui::ColorEdit3("Start Color Min", &ColorModule->StartColor.MinValue.R);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("시작 색상 최소값 (RGB)");
                        ImGui::ColorEdit3("Start Color Max", &ColorModule->StartColor.MaxValue.R);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("시작 색상 최대값 (RGB)\n실제 색상은 Min~Max 사이 랜덤");
                    }
                    else
                    {
                        ImGui::ColorEdit3("Start Color", &ColorModule->StartColor.MinValue.R);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 시작 색상 (RGB)\n머터리얼 텍스처 색상에 곱해짐");
                    }
                    ImGui::Checkbox("Use Range", &ColorModule->StartColor.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 색상 범위 사용\n다양한 색상의 파티클 생성");

                    ImGui::Spacing();
                    if (ColorModule->StartAlpha.bUseRange)
                    {
                        ImGui::DragFloat("Start Alpha Min", &ColorModule->StartAlpha.MinValue, 0.01f, 0.0f, 1.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("시작 투명도 최소값 (0=투명, 1=불투명)");
                        ImGui::DragFloat("Start Alpha Max", &ColorModule->StartAlpha.MaxValue, 0.01f, 0.0f, 1.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("시작 투명도 최대값\n실제 투명도는 Min~Max 사이 랜덤");
                    }
                    else
                    {
                        ImGui::DragFloat("Start Alpha", &ColorModule->StartAlpha.MinValue, 0.01f, 0.0f, 1.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 시작 투명도\n0.0 = 완전 투명\n1.0 = 완전 불투명");
                    }
                }
                else if (auto* ColorOverLifeModule = Cast<UParticleModuleColorOverLife>(SelectedModule))
                {
                    ImGui::Text("Color Over Life Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클 수명에 따른 색상/투명도 변화\n페이드 인/아웃, 색상 전환 효과");
                    }
                    ImGui::Separator();

                    ImGui::Checkbox("Use Color Over Life", &ColorOverLifeModule->bUseColorOverLife);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("수명에 따른 색상 변화 활성화\n시작 색상에서 이 색상으로 변화");
                    if (ColorOverLifeModule->bUseColorOverLife)
                    {
                        if (ColorOverLifeModule->ColorOverLife.bUseRange)
                        {
                            ImGui::ColorEdit3("Color Min", &ColorOverLifeModule->ColorOverLife.MinValue.R);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("수명 끝 색상 최소값");
                            ImGui::ColorEdit3("Color Max", &ColorOverLifeModule->ColorOverLife.MaxValue.R);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("수명 끝 색상 최대값");
                        }
                        else
                        {
                            ImGui::ColorEdit3("Color", &ColorOverLifeModule->ColorOverLife.MinValue.R);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("수명 끝의 목표 색상\n시작 색상에서 이 색상으로 서서히 변화");
                        }
                        ImGui::Checkbox("Color Use Range", &ColorOverLifeModule->ColorOverLife.bUseRange);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("색상 랜덤 범위 사용");
                    }

                    ImGui::Spacing();
                    ImGui::Checkbox("Use Alpha Over Life", &ColorOverLifeModule->bUseAlphaOverLife);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("수명에 따른 투명도 변화 활성화\n페이드 아웃 효과에 필수");
                    if (ColorOverLifeModule->bUseAlphaOverLife)
                    {
                        if (ColorOverLifeModule->AlphaOverLife.bUseRange)
                        {
                            ImGui::DragFloat("Alpha Min", &ColorOverLifeModule->AlphaOverLife.MinValue, 0.01f, 0.0f,
                                             1.0f);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("수명 끝 투명도 최소값");
                            ImGui::DragFloat("Alpha Max", &ColorOverLifeModule->AlphaOverLife.MaxValue, 0.01f, 0.0f,
                                             1.0f);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("수명 끝 투명도 최대값");
                        }
                        else
                        {
                            ImGui::DragFloat("Alpha", &ColorOverLifeModule->AlphaOverLife.MinValue, 0.01f, 0.0f, 1.0f);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("수명 끝의 목표 투명도\n0.0으로 설정하면 페이드 아웃 효과");
                        }
                        ImGui::Checkbox("Alpha Use Range", &ColorOverLifeModule->AlphaOverLife.bUseRange);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("투명도 랜덤 범위 사용");
                    }
                }
                else if (auto* SizeMultiplyLifeModule = Cast<UParticleModuleSizeMultiplyLife>(SelectedModule))
                {
                    ImGui::Text("Size Multiply Life Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클 수명에 따른 크기 변화\n커브로 시간별 크기 비율 설정\n폭발(커졌다 작아짐), 성장 효과 등");
                    }
                    ImGui::Separator();

                    ImGui::Text("Curve Control Points");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("시간에 따른 크기 곱셈 값\n시작(0) -> Point1 -> Point2 -> 끝으로 보간");

                    ImGui::Text("Point 1:");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("첫 번째 제어점\n수명 초기 구간의 크기 비율");
                    ImGui::DragFloat("Time##P1", &SizeMultiplyLifeModule->Point1Time, 0.1f, 0.0f, 100.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Point1이 적용되는 시간 (초)\n이 시간에 Point1 Value 크기가 됨");
                    ImGui::DragFloat3("Value##P1", &SizeMultiplyLifeModule->Point1Value.X, 0.1f, 0.0f, 100.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Point1의 크기 곱셈 값 (X, Y, Z)\n1.0 = 원래 크기, 2.0 = 2배, 0.5 = 절반");

                    ImGui::Spacing();
                    ImGui::Text("Point 2:");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("두 번째 제어점\n수명 후기 구간의 크기 비율");
                    ImGui::DragFloat("Time##P2", &SizeMultiplyLifeModule->Point2Time, 0.1f, 0.0f, 100.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Point2가 적용되는 시간 (초)\nPoint1 이후에 이 값으로 변화");
                    ImGui::DragFloat3("Value##P2", &SizeMultiplyLifeModule->Point2Value.X, 0.1f, 0.0f, 100.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Point2의 크기 곱셈 값 (X, Y, Z)\n예: 폭발=2.0->0.1, 성장=0.5->1.5");

                    ImGui::Spacing();
                    ImGui::Text("Multiply Axes:");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("크기 변화를 적용할 축 선택\n선택하지 않은 축은 변화 없음");
                    ImGui::Checkbox("Multiply X", &SizeMultiplyLifeModule->bMultiplyX);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("X축 크기 변화 활성화");
                    ImGui::Checkbox("Multiply Y", &SizeMultiplyLifeModule->bMultiplyY);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Y축 크기 변화 활성화");
                    ImGui::Checkbox("Multiply Z", &SizeMultiplyLifeModule->bMultiplyZ);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Z축 크기 변화 활성화");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tip: 커브 에디터에서 시각적으로 조절 가능");
                }
                else if (auto* RotationModule = Cast<UParticleModuleRotation>(SelectedModule))
                {
                    ImGui::Text("Rotation Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클의 초기 회전 각도 설정\n빌보드 스프라이트의 2D 회전");
                    }
                    ImGui::Separator();

                    if (RotationModule->StartRotation.bUseRange)
                    {
                        ImGui::DragFloat("Start Rotation Min (Radians)", &RotationModule->StartRotation.MinValue, 0.01f,
                                         -6.28f, 6.28f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 회전 최소값 (라디안)\n0 = 회전 없음, PI = 180도");
                        ImGui::DragFloat("Start Rotation Max (Radians)", &RotationModule->StartRotation.MaxValue, 0.01f,
                                         -6.28f, 6.28f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 회전 최대값 (라디안)\n0~2PI 범위로 완전 랜덤 회전");
                    }
                    else
                    {
                        ImGui::DragFloat("Start Rotation (Radians)", &RotationModule->StartRotation.MinValue, 0.01f,
                                         -6.28f, 6.28f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 회전 각도 (라디안)\n모든 파티클이 동일한 각도로 시작");
                    }
                    ImGui::Checkbox("Use Range", &RotationModule->StartRotation.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 회전 범위 사용\n자연스러운 랜덤 회전 효과");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tip: PI = 3.14159 (180도), 2*PI = 6.28318 (360도)");
                }
                else if (auto* RotationRateModule = Cast<UParticleModuleRotationRate>(SelectedModule))
                {
                    ImGui::Text("Rotation Rate Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클의 회전 속도 설정\n시간에 따라 계속 회전하는 효과");
                    }
                    ImGui::Separator();

                    ImGui::Text("Initial Rotation");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 생성 시 초기 회전 각도");
                    if (RotationRateModule->InitialRotation.bUseRange)
                    {
                        ImGui::DragFloat("Initial Rotation Min (Rad)", &RotationRateModule->InitialRotation.MinValue,
                                         0.01f, 0.0f, 6.28318f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 회전 최소값 (라디안)");
                        ImGui::DragFloat("Initial Rotation Max (Rad)", &RotationRateModule->InitialRotation.MaxValue,
                                         0.01f, 0.0f, 6.28318f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 회전 최대값 (라디안)");
                    }
                    else
                    {
                        ImGui::DragFloat("Initial Rotation (Rad)", &RotationRateModule->InitialRotation.MinValue, 0.01f,
                                         0.0f, 6.28318f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 회전 각도 (라디안)");
                    }
                    ImGui::Checkbox("Use Initial Rotation Range", &RotationRateModule->InitialRotation.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("초기 회전 랜덤 범위 사용");

                    ImGui::Spacing();
                    ImGui::Text("Rotation Speed");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("초당 회전 속도 (라디안/초)\n양수=시계방향, 음수=반시계방향");
                    if (RotationRateModule->StartRotationRate.bUseRange)
                    {
                        ImGui::DragFloat("Start Rotation Rate Min (Rad/s)",
                                         &RotationRateModule->StartRotationRate.MinValue, 0.01f, -10.0f, 10.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("회전 속도 최소값 (rad/s)");
                        ImGui::DragFloat("Start Rotation Rate Max (Rad/s)",
                                         &RotationRateModule->StartRotationRate.MaxValue, 0.01f, -10.0f, 10.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("회전 속도 최대값 (rad/s)\n다양한 속도로 회전하는 효과");
                    }
                    else
                    {
                        ImGui::DragFloat("Start Rotation Rate (Rad/s)", &RotationRateModule->StartRotationRate.MinValue,
                                         0.01f, -10.0f, 10.0f);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("회전 속도 (rad/s)\n1.0 ≈ 초당 57도 회전");
                    }
                    ImGui::Checkbox("Use Rotation Rate Range", &RotationRateModule->StartRotationRate.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("회전 속도 랜덤 범위 사용\n음수~양수 범위로 양방향 회전");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tip: PI = 3.14159 (180도), 2*PI = 6.28318 (360도)");
                    ImGui::TextDisabled("Tip: 1 rad/s ≈ 57 degrees/s");
                }
                else if (auto* SubUVModule = Cast<UParticleModuleSubUV>(SelectedModule))
                {
                    ImGui::Text("SubUV Animation Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("스프라이트 시트 애니메이션 제어\n텍스처를 격자로 나눠 프레임 애니메이션 재생\n폭발, 불꽃, 연기 애니메이션에 사용");
                    }
                    ImGui::Separator();

                    // SubImageIndex 커브 (0~1 범위, 실제로는 곱하기 TotalFrames-1)
                    ImGui::Text("SubImage Index (0~1)");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("애니메이션 진행 범위\n0.0 = 첫 프레임, 1.0 = 마지막 프레임\n수명에 따라 0->1로 진행");
                    ImGui::DragFloat("Index Min", &SubUVModule->SubImageIndex.MinValue, 0.01f, 0.0f, 1.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("시작 인덱스 (0~1)\n파티클 생성 시 시작 프레임");
                    ImGui::DragFloat("Index Max", &SubUVModule->SubImageIndex.MaxValue, 0.01f, 0.0f, 1.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("끝 인덱스 (0~1)\n파티클 소멸 시 도달할 프레임");
                    ImGui::Checkbox("Use Range##SubUV", &SubUVModule->SubImageIndex.bUseRange);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 인덱스 범위 사용");

					ImGui::Spacing();

                    // 보간 방식
                    ImGui::Text("Interpolation Method");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("프레임 전환 보간 방식");
                    const char* InterpMethods[] = { "None", "Linear Blend", "Random", "Random Blend" };
                    int CurrentMethod = (int)SubUVModule->InterpMethod;
                    if (ImGui::Combo("##InterpMethod", &CurrentMethod, InterpMethods, IM_ARRAYSIZE(InterpMethods)))
                    {
                        SubUVModule->InterpMethod = (ESubUVInterpMethod)CurrentMethod;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("- None: 프레임 즉시 전환 (픽셀 느낌)\n- Linear Blend: 부드러운 프레임 블렌딩\n- Random: 랜덤 프레임 선택\n- Random Blend: 랜덤 + 블렌딩");
                    }

                    ImGui::Spacing();
                    ImGui::Checkbox("Use Real Time", &SubUVModule->bUseRealTime);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("실제 시간 기반 애니메이션\n체크: 게임 시간 무관하게 실시간 재생\n해제: 파티클 수명 기반 재생");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tip: Required 모듈에서 SubImages 설정 필요");
                }
                else if (auto* MeshModule = Cast<UParticleModuleMesh>(SelectedModule))
                {
                    ImGui::Text("Mesh TypeData Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("메시 타입 파티클 설정\n스프라이트 대신 3D 메시로 파티클 렌더링\n파편, 돌, 나뭇잎 등 3D 오브젝트에 사용");
                    }
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Columns(2, "MeshModuleColumns", false);
                    ImGui::SetColumnWidth(0, 150.0f);

                    // Mesh
                    {
                        ImGui::Text("Mesh");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클로 사용할 3D 메시\nStaticMesh 에셋 선택");
                        ImGui::NextColumn();

						// 현재 선택된 메쉬의 파일명만 추출
						FString currentMeshName = "None";
						if (MeshModule->Mesh)
						{
							FString fullPath = MeshModule->Mesh->GetAssetPathFileName();
							size_t lastSlash = fullPath.find_last_of("/\\");
							if (lastSlash != FString::npos)
							{
								currentMeshName = fullPath.substr(lastSlash + 1);
							}
							else
							{
								currentMeshName = fullPath;
							}
						}

						ImGui::SetNextItemWidth(-1);
						if (ImGui::BeginCombo("##MeshCombo", currentMeshName.c_str()))
						{
							// None
							if (ImGui::Selectable("None##MeshNone", MeshModule->Mesh == nullptr))
							{
								MeshModule->SetMesh(nullptr, SelectedEmitter);
							}

							ImGui::Separator();

							// 모든 StaticMesh 리소스 가져오기
							TArray<UStaticMesh*> AllMeshes = UResourceManager::GetInstance().GetAll<UStaticMesh>();

							for (int i = 0; i < AllMeshes.Num(); ++i)
							{
								UStaticMesh* Mesh = AllMeshes[i];
								if (!Mesh) continue;

								ImGui::PushID(i);

								bool isSelected = (MeshModule->Mesh == Mesh);

								// 파일 경로에서 파일명만 추출
								FString fullPath = Mesh->GetAssetPathFileName();
								FString displayName = fullPath;
								size_t lastSlash = fullPath.find_last_of("/\\");
								if (lastSlash != FString::npos)
								{
									displayName = fullPath.substr(lastSlash + 1);
								}

								if (ImGui::Selectable(displayName.c_str(), isSelected))
								{
									MeshModule->SetMesh(Mesh, SelectedEmitter);

									// PreviewComponent 재시작 (EmitterInstance를 다시 초기화)
									if (PreviewComponent && CurrentParticleSystem)
									{
										CurrentParticleSystem->BuildRuntimeCache();
										PreviewComponent->ResetAndActivate();
									}
								}

								// 툴팁에 전체 경로 표시
								if (ImGui::IsItemHovered())
								{
									ImGui::SetTooltip("%s", fullPath.c_str());
								}

								ImGui::PopID();
							}

							ImGui::EndCombo();
						}

						ImGui::NextColumn();
					}

                    // Mesh material 연동 여부
                    {
                        ImGui::Text("Use Mesh Materials");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("메시 원본 머터리얼 사용\n체크: 메시 자체 머터리얼 사용\n해제: Override Material 사용 가능");
                        ImGui::NextColumn();

						bool bUseMeshMat = MeshModule->bUseMeshMaterials;
						if (ImGui::Checkbox("##UseMeshMaterials", &bUseMeshMat))
						{
							MeshModule->bUseMeshMaterials = bUseMeshMat;

							// 체크 켰고, Mesh도 있고, Required도 있으면 즉시 동기화
							if (bUseMeshMat && MeshModule->Mesh && SelectedEmitter)
							{
								MeshModule->SetMesh(MeshModule->Mesh, SelectedEmitter);
							}
						}

						ImGui::NextColumn();
					}

					ImGui::Columns(1);

					// Override Material (Use Mesh Materials가 false일 때만 표시)
					if (!MeshModule->bUseMeshMaterials)
					{
						ImGui::Spacing();
						ImGui::Separator();
						ImGui::Spacing();

						// Materials 섹션 (스태틱메쉬 디테일 패널 스타일)
						if (ImGui::TreeNodeEx("Materials##MeshModule", ImGuiTreeNodeFlags_DefaultOpen))
						{
							// 머터리얼 텍스처 가져오기
							UTexture* DiffuseTexture = MeshModule->OverrideMaterial ? MeshModule->OverrideMaterial->GetTexture(EMaterialTextureSlot::Diffuse) : nullptr;

							// 머터리얼 선택 콤보박스 (텍스처 미리보기 포함)
							ImGui::Columns(2, "MeshMaterialSelectColumns", false);
							ImGui::SetColumnWidth(0, 50.0f);

							// 미리보기 정사각형 (40x40)
							if (DiffuseTexture && DiffuseTexture->GetShaderResourceView())
							{
								ImGui::Image((void*)DiffuseTexture->GetShaderResourceView(), ImVec2(40, 40));
							}
							else
							{
								ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
								ImGui::Button("##meshmatpreview", ImVec2(40, 40));
								ImGui::PopStyleColor();
							}
							ImGui::NextColumn();

							FString currentMaterialPath = MeshModule->OverrideMaterial ? MeshModule->OverrideMaterial->GetFilePath() : "None";
							ImGui::SetNextItemWidth(-1);
							if (ImGui::BeginCombo("##MeshMaterialCombo", currentMaterialPath.c_str()))
							{
								// None 옵션
								if (ImGui::Selectable("None##MeshMatNone", MeshModule->OverrideMaterial == nullptr))
								{
									MeshModule->SetOverrideMaterial(nullptr, SelectedEmitter);
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
											ImGui::PushID(i + 2000); // ID 충돌 방지
											bool isSelected = (MeshModule->OverrideMaterial == Mat);

											// 텍스처 미리보기 + 이름
											UTexture* MatTexture = Mat->GetTexture(EMaterialTextureSlot::Diffuse);
											if (MatTexture && MatTexture->GetShaderResourceView())
											{
												ImGui::Image((void*)MatTexture->GetShaderResourceView(), ImVec2(30, 30));
												ImGui::SameLine();
											}

											if (ImGui::Selectable(Mat->GetFilePath().c_str(), isSelected))
											{
												MeshModule->SetOverrideMaterial(Mat, SelectedEmitter);

												// PreviewComponent 재시작
												if (PreviewComponent && CurrentParticleSystem)
												{
													CurrentParticleSystem->BuildRuntimeCache();
													PreviewComponent->ResetAndActivate();
												}
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
							ImGui::Columns(1);

							// MaterialSlots[0] TreeNode
							if (ImGui::TreeNodeEx("MaterialSlots [0]##MeshModule", ImGuiTreeNodeFlags_DefaultOpen))
							{
								UMaterialInterface* CurrentMaterial = MeshModule->OverrideMaterial;
								if (CurrentMaterial)
								{

									ImGui::Columns(2, "MeshMaterialDetailsColumns", false);
									ImGui::SetColumnWidth(0, 150.0f);

									// Shader (읽기 전용)
									ImGui::Text("Shader");
									ImGui::NextColumn();
									UShader* CurrentShader = CurrentMaterial->GetShader();
									FString ShaderPath = CurrentShader ? CurrentShader->GetFilePath() : "None";
									ImGui::SetNextItemWidth(-1);
									ImGui::BeginDisabled(true);
									char shaderBuffer[512];
									strncpy_s(shaderBuffer, sizeof(shaderBuffer), ShaderPath.c_str(), _TRUNCATE);
									ImGui::InputText("##MeshShader", shaderBuffer, sizeof(shaderBuffer), ImGuiInputTextFlags_ReadOnly);
									ImGui::EndDisabled();
									ImGui::NextColumn();

									// MacroKey (읽기 전용)
									ImGui::Text("MacroKey");
									ImGui::NextColumn();
									FString MacroKey = UShader::GenerateMacrosToString(CurrentMaterial->GetShaderMacros());
									char macroBuffer[512];
									strncpy_s(macroBuffer, sizeof(macroBuffer), MacroKey.c_str(), _TRUNCATE);
									ImGui::SetNextItemWidth(-1);
									ImGui::BeginDisabled(true);
									ImGui::InputText("##MeshMacroKey", macroBuffer, sizeof(macroBuffer), ImGuiInputTextFlags_ReadOnly);
									ImGui::EndDisabled();
									ImGui::NextColumn();

									// 모든 텍스처 가져오기 (Diffuse/Normal 선택용)
									TArray<UTexture*> AllTextures = UResourceManager::GetInstance().GetAll<UTexture>();

									// MID 확인 및 필요시 생성하는 람다
									auto EnsureMeshMID = [&]() -> UMaterialInstanceDynamic* {
										UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MeshModule->OverrideMaterial);
										if (!MID && MeshModule->OverrideMaterial)
										{
											MID = UMaterialInstanceDynamic::Create(MeshModule->OverrideMaterial);
											if (MID)
											{
												MeshModule->SetOverrideMaterial(MID, SelectedEmitter);
												CurrentMaterial = MID;
											}
										}
										return MID;
										};

									// Diffuse Texture
									ImGui::Text("Diffuse Texture");
									ImGui::NextColumn();
									{
										UTexture* DiffTex = CurrentMaterial->GetTexture(EMaterialTextureSlot::Diffuse);
										if (DiffTex && DiffTex->GetShaderResourceView())
										{
											ImGui::Image((void*)DiffTex->GetShaderResourceView(), ImVec2(30, 30));
											ImGui::SameLine();
										}
										FString DiffTexPath = DiffTex ? DiffTex->GetFilePath() : "None";
										ImGui::SetNextItemWidth(-1);
										if (ImGui::BeginCombo("##MeshDiffuseTexture", DiffTexPath.c_str()))
										{
											// None 옵션
											if (ImGui::Selectable("None##MeshDiffNone", DiffTex == nullptr))
											{
												if (UMaterialInstanceDynamic* MID = EnsureMeshMID())
												{
													MID->SetTextureParameterValue(EMaterialTextureSlot::Diffuse, nullptr);
												}
											}
											ImGui::Separator();

											for (int i = 0; i < AllTextures.Num(); i++)
											{
												UTexture* Tex = AllTextures[i];
												if (Tex)
												{
													ImGui::PushID(i + 3000);
													bool isSelected = (DiffTex == Tex);

													if (Tex->GetShaderResourceView())
													{
														ImGui::Image((void*)Tex->GetShaderResourceView(), ImVec2(24, 24));
														ImGui::SameLine();
													}

													if (ImGui::Selectable(Tex->GetFilePath().c_str(), isSelected))
													{
														if (UMaterialInstanceDynamic* MID = EnsureMeshMID())
														{
															MID->SetTextureParameterValue(EMaterialTextureSlot::Diffuse, Tex);
														}
													}
													ImGui::PopID();
												}
											}
											ImGui::EndCombo();
										}
									}
									ImGui::NextColumn();

									// Normal Texture
									ImGui::Text("Normal Texture");
									ImGui::NextColumn();
									{
										UTexture* NormTex = CurrentMaterial->GetTexture(EMaterialTextureSlot::Normal);
										if (NormTex && NormTex->GetShaderResourceView())
										{
											ImGui::Image((void*)NormTex->GetShaderResourceView(), ImVec2(30, 30));
											ImGui::SameLine();
										}
										FString NormTexPath = NormTex ? NormTex->GetFilePath() : "None";
										ImGui::SetNextItemWidth(-1);
										if (ImGui::BeginCombo("##MeshNormalTexture", NormTexPath.c_str()))
										{
											// None 옵션
											if (ImGui::Selectable("None##MeshNormNone", NormTex == nullptr))
											{
												if (UMaterialInstanceDynamic* MID = EnsureMeshMID())
												{
													MID->SetTextureParameterValue(EMaterialTextureSlot::Normal, nullptr);
												}
											}
											ImGui::Separator();

											for (int i = 0; i < AllTextures.Num(); i++)
											{
												UTexture* Tex = AllTextures[i];
												if (Tex)
												{
													ImGui::PushID(i + 4000);
													bool isSelected = (NormTex == Tex);

													if (Tex->GetShaderResourceView())
													{
														ImGui::Image((void*)Tex->GetShaderResourceView(), ImVec2(24, 24));
														ImGui::SameLine();
													}

													if (ImGui::Selectable(Tex->GetFilePath().c_str(), isSelected))
													{
														if (UMaterialInstanceDynamic* MID = EnsureMeshMID())
														{
															MID->SetTextureParameterValue(EMaterialTextureSlot::Normal, Tex);
														}
													}
													ImGui::PopID();
												}
											}
											ImGui::EndCombo();
										}
									}
									ImGui::NextColumn();

									ImGui::Separator();

									// FMaterialInfo 속성들 (편집 가능)
									FMaterialInfo Info = CurrentMaterial->GetMaterialInfo();
									bool bInfoChanged = false;

									// Colors
									FLinearColor TempColor;

									// Diffuse Color
									TempColor = FLinearColor(Info.DiffuseColor);
									ImGui::Text("Diffuse Color");
									ImGui::NextColumn();
									if (ImGui::ColorEdit3("##MeshDiffuseColor", &TempColor.R))
									{
										Info.DiffuseColor = FVector(TempColor.R, TempColor.G, TempColor.B);
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Ambient Color
									TempColor = FLinearColor(Info.AmbientColor);
									ImGui::Text("Ambient Color");
									ImGui::NextColumn();
									if (ImGui::ColorEdit3("##MeshAmbientColor", &TempColor.R))
									{
										Info.AmbientColor = FVector(TempColor.R, TempColor.G, TempColor.B);
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Specular Color
									TempColor = FLinearColor(Info.SpecularColor);
									ImGui::Text("Specular Color");
									ImGui::NextColumn();
									if (ImGui::ColorEdit3("##MeshSpecularColor", &TempColor.R))
									{
										Info.SpecularColor = FVector(TempColor.R, TempColor.G, TempColor.B);
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Emissive Color
									TempColor = FLinearColor(Info.EmissiveColor);
									ImGui::Text("Emissive Color");
									ImGui::NextColumn();
									if (ImGui::ColorEdit3("##MeshEmissiveColor", &TempColor.R))
									{
										Info.EmissiveColor = FVector(TempColor.R, TempColor.G, TempColor.B);
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Transmission Filter
									TempColor = FLinearColor(Info.TransmissionFilter);
									ImGui::Text("Transmission Filter");
									ImGui::NextColumn();
									if (ImGui::ColorEdit3("##MeshTransmissionFilter", &TempColor.R))
									{
										Info.TransmissionFilter = FVector(TempColor.R, TempColor.G, TempColor.B);
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Scalar 값들

									// Specular Exponent
									ImGui::Text("Specular Exponent");
									ImGui::NextColumn();
									ImGui::SetNextItemWidth(-1);
									if (ImGui::DragFloat("##MeshSpecularExponent", &Info.SpecularExponent, 1.0f, 0.0f, 1024.0f))
									{
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Transparency
									ImGui::Text("Transparency");
									ImGui::NextColumn();
									ImGui::SetNextItemWidth(-1);
									if (ImGui::DragFloat("##MeshTransparency", &Info.Transparency, 0.01f, 0.0f, 1.0f))
									{
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Optical Density
									ImGui::Text("Optical Density");
									ImGui::NextColumn();
									ImGui::SetNextItemWidth(-1);
									if (ImGui::DragFloat("##MeshOpticalDensity", &Info.OpticalDensity, 0.01f, 0.0f, 10.0f))
									{
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Bump Multiplier
									ImGui::Text("Bump Multiplier");
									ImGui::NextColumn();
									ImGui::SetNextItemWidth(-1);
									if (ImGui::DragFloat("##MeshBumpMultiplier", &Info.BumpMultiplier, 0.01f, 0.0f, 5.0f))
									{
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// Illum Model
									ImGui::Text("Illum Model");
									ImGui::NextColumn();
									ImGui::SetNextItemWidth(-1);
									if (ImGui::DragInt("##MeshIllumModel", &Info.IlluminationModel, 1, 0, 10))
									{
										bInfoChanged = true;
									}
									ImGui::NextColumn();

									// 값이 변경되었으면 MaterialInfo 업데이트 (UMaterial인 경우만)
									if (bInfoChanged)
									{
										if (UMaterial* Mat = Cast<UMaterial>(CurrentMaterial))
										{
											Mat->SetMaterialInfo(Info);
										}
									}

                                    ImGui::Columns(1);
                                }
                                else
                                {
                                    ImGui::TextDisabled("No Material Selected");
                                }
                                ImGui::TreePop();
                            }
                            ImGui::TreePop();
                        }
                    }
                    }
                else if (auto* BeamModule = Cast<UParticleModuleBeam>(SelectedModule))
                {
                    ImGui::Text("Beam TypeData Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("빔 타입 파티클 설정\n두 점 사이를 연결하는 선형 렌더링\n번개, 레이저, 전기 효과에 사용");
                    }
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Columns(2, "BeamModuleColumns", false);
                    ImGui::SetColumnWidth(0, 150.0f);

                    // Source Point
                    {
                        ImGui::Text("Source Point");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("빔 시작점 위치 (로컬 좌표)");
                        ImGui::NextColumn();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::DragFloat3("##BeamSourcePoint", &BeamModule->SourcePoint.X, 1.0f))
                        {
                            if (PreviewComponent && CurrentParticleSystem)
                            {
                                CurrentParticleSystem->BuildRuntimeCache();
                                PreviewComponent->ResetAndActivate();
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("빔이 시작되는 위치 (X, Y, Z)");
                        ImGui::NextColumn();
                    }

                    // Target Point
                    {
                        ImGui::Text("Target Point");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("빔 끝점 위치 (로컬 좌표)");
                        ImGui::NextColumn();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::DragFloat3("##BeamTargetPoint", &BeamModule->TargetPoint.X, 1.0f))
                        {
                            if (PreviewComponent && CurrentParticleSystem)
                            {
                                CurrentParticleSystem->BuildRuntimeCache();
                                PreviewComponent->ResetAndActivate();
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("빔이 끝나는 위치 (X, Y, Z)");
                        ImGui::NextColumn();
                    }

                    // Tessellation Factor
                    {
                        ImGui::Text("Tessellation Factor");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("빔 세분화 수준\n값이 클수록 부드러운 곡선");
                        ImGui::NextColumn();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::DragInt("##BeamTessellation", &BeamModule->TessellationFactor, 1, 1, 100))
                        {
                            if (PreviewComponent && CurrentParticleSystem)
                            {
                                CurrentParticleSystem->BuildRuntimeCache();
                                PreviewComponent->ResetAndActivate();
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("빔을 구성하는 세그먼트 수\n높을수록 노이즈가 더 세밀하게 표현됨");
                        ImGui::NextColumn();
                    }

                    // Noise Frequency
                    {
                        ImGui::Text("Noise Frequency");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("노이즈 주파수\n번개의 지글지글 정도");
                        ImGui::NextColumn();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::DragFloat("##BeamNoiseFreq", &BeamModule->NoiseFrequency, 0.01f, 0.0f, 10.0f))
                        {
                            if (PreviewComponent && CurrentParticleSystem)
                            {
                                CurrentParticleSystem->BuildRuntimeCache();
                                PreviewComponent->ResetAndActivate();
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("노이즈 변화 속도\n높을수록 빠르게 지글거림");
                        ImGui::NextColumn();
                    }

                    // Noise Amplitude
                    {
                        ImGui::Text("Noise Amplitude");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("노이즈 진폭\n빔이 흔들리는 범위");
                        ImGui::NextColumn();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::DragFloat("##BeamNoiseAmp", &BeamModule->NoiseAmplitude, 0.1f, 0.0f, 100.0f))
                        {
                            if (PreviewComponent && CurrentParticleSystem)
                            {
                                CurrentParticleSystem->BuildRuntimeCache();
                                PreviewComponent->ResetAndActivate();
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("노이즈 크기 (유닛)\n높을수록 크게 흔들림");
                        ImGui::NextColumn();
                    }

                    // Use Random Offset (번개 효과)
                    {
                        ImGui::Text("Use Random Offset");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("랜덤 시작/끝점 오프셋\n번개처럼 매번 다른 위치에서 발사");
                        ImGui::NextColumn();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::Checkbox("##BeamUseRandomOffset", &BeamModule->bUseRandomOffset))
                        {
                            if (PreviewComponent && CurrentParticleSystem)
                            {
                                CurrentParticleSystem->BuildRuntimeCache();
                                PreviewComponent->ResetAndActivate();
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("체크: 랜덤 오프셋 적용\n해제: 고정 위치 사용");
                        ImGui::NextColumn();
                    }

                    // Source Offset (랜덤 범위)
                    if (BeamModule->bUseRandomOffset)
                    {
                        ImGui::Text("Source Offset");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("시작점 랜덤 오프셋 범위");
                        ImGui::NextColumn();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::DragFloat3("##BeamSourceOffset", &BeamModule->SourceOffset.X, 1.0f, 0.0f, 1000.0f))
                        {
                            if (PreviewComponent && CurrentParticleSystem)
                            {
                                CurrentParticleSystem->BuildRuntimeCache();
                                PreviewComponent->ResetAndActivate();
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("시작점 ± 오프셋 범위 (X, Y, Z)");
                        ImGui::NextColumn();

                        // Target Offset (랜덤 범위)
                        ImGui::Text("Target Offset");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("끝점 랜덤 오프셋 범위");
                        ImGui::NextColumn();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::DragFloat3("##BeamTargetOffset", &BeamModule->TargetOffset.X, 1.0f, 0.0f, 1000.0f))
                        {
                            if (PreviewComponent && CurrentParticleSystem)
                            {
                                CurrentParticleSystem->BuildRuntimeCache();
                                PreviewComponent->ResetAndActivate();
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("끝점 ± 오프셋 범위 (X, Y, Z)");
                        ImGui::NextColumn();
                    }

                    ImGui::Columns(1);
                }
				else if (auto* RibbonModule = Cast<UParticleModuleRibbon>(SelectedModule))
				{
					ImGui::Text("Ribbon Settings");
					ImGui::Separator();
					ImGui::Spacing();

					// 두 컬럼 레이아웃 (레이블 / 값)
					ImGui::Columns(2, "RibbonModuleColumns", false);
					ImGui::SetColumnWidth(0, 150.0f);

					// ─────────────────────────────────────
					// Width
					// ─────────────────────────────────────
					ImGui::Text("Width");
					ImGui::NextColumn();
					{
						ImGui::SetNextItemWidth(-1);
						// 월드 유닛 기준, 너무 크게는 막아두고 싶은 경우 범위 적당히
						ImGui::DragFloat("##RibbonWidth",
							&RibbonModule->Width,
							0.1f,   // step
							0.0f,   // min
							10000.0f, // max
							"%.2f");
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("리본의 전체 폭 (world units).\n예: 10.0이면 좌우로 5.0씩 퍼짐.");
						}
					}
					ImGui::NextColumn();

					// ─────────────────────────────────────
					// Tiling Distance
					// ─────────────────────────────────────
					ImGui::Text("Tiling Distance");
					ImGui::NextColumn();
					{
						ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##RibbonTilingDistance",
							&RibbonModule->TilingDistance,
							1.0f,
							0.0f,
							100000.0f,
							"%.1f");
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip(
								"리본 길이 방향 UV 타일링 거리.\n"
								"0.0  : 전체 길이를 0~1로 Stretch\n"
								">0.0: 실제 거리 / TilingDistance = V 좌표 (타일 반복)\n"
								"예: TilingDistance=100이면 100 unit마다 1 타일 반복."
							);
						}
					}
					ImGui::NextColumn();

					// ─────────────────────────────────────
					// Trail Lifetime
					// ─────────────────────────────────────
					ImGui::Text("Trail Lifetime");
					ImGui::NextColumn();
					{
						ImGui::SetNextItemWidth(-1);
						ImGui::DragFloat("##RibbonTrailLifetime",
							&RibbonModule->TrailLifetime,
							0.1f,
							0.0f,
							60.0f,
							"%.2f");
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip(
								"리본 트레일 전체 수명 (초 단위).\n"
								"시뮬레이션 단계에서 이 값을 기준으로 오래된 Spine 포인트를 잘라내거나\n"
								"알파를 줄이는 데 사용할 수 있음."
							);
						}
					}
					ImGui::NextColumn();

					// ─────────────────────────────────────
					// Camera Facing
					// ─────────────────────────────────────
					ImGui::Text("Camera Facing");
					ImGui::NextColumn();
					{
						bool bFacing = RibbonModule->bUseCameraFacing;
						if (ImGui::Checkbox("##RibbonUseCameraFacing", &bFacing))
						{
							RibbonModule->bUseCameraFacing = bFacing;
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip(
								"켜면 리본의 폭 방향이 항상 카메라를 향하도록 보정.\n"
								"끄면 월드 업(또는 다른 규칙)을 기준으로 폭 방향을 계산."
							);
						}
					}
					ImGui::NextColumn();

					ImGui::Columns(1);
					ImGui::Spacing();
					ImGui::TextDisabled("Tip: Width, TilingDistance, TrailLifetime 값은 FDynamicRibbonEmitterReplayData로 복사되어");
					ImGui::TextDisabled("BuildRibbonParticleBatch()에서 Spine 포인트를 리본 메쉬로 전개할 때 사용됩니다.");
					}
                else if (auto* CollisionModule = Cast<UParticleModuleCollision>(SelectedModule))
                {
                    ImGui::Text("Collision Settings");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("파티클 충돌 처리 설정\n지면, 벽 등과의 충돌 반응 정의\n물리 기반 파티클 효과에 사용");
                    }
                    ImGui::Separator();

                    ImGui::Text("Response");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("충돌 시 파티클 반응 방식");
                    ImGui::SameLine();
                    const char* ResponseItems[] = { "Bounce", "Stop", "Kill" };
                    int CurrentResponse = (int)CollisionModule->CollisionResponse;

                    if (ImGui::Combo("##Response", &CurrentResponse, ResponseItems, IM_ARRAYSIZE(ResponseItems)))
                    {
                        CollisionModule->CollisionResponse = static_cast<EParticleCollisionResponse>(CurrentResponse);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("- Bounce: 튕김 (반사)\n- Stop: 충돌 지점에서 정지\n- Kill: 충돌 시 즉시 소멸");
                    }
                    ImGui::Spacing();

                    ImGui::Text("Physics Properties");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("충돌 물리 속성");
                    // Restitution은 Bounce 모드일 때만 유효
                    if (CollisionModule->CollisionResponse != EParticleCollisionResponse::Bounce)
                    {
                        ImGui::BeginDisabled(); // UI 비활성화 시작
                    }

                    // 1.0을 넘으면 에너지가 증폭
                    ImGui::DragFloat("Restitution (Bounciness)", &CollisionModule->Restitution, 0.01f, 0.0f, 2.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("반발 계수 (탄성)\n0.0 = 튕기지 않음\n1.0 = 완전 탄성 충돌\n>1.0 = 에너지 증폭 (비현실적)");

                    if (CollisionModule->CollisionResponse != EParticleCollisionResponse::Bounce)
                    {
                        ImGui::EndDisabled(); // UI 비활성화 끝
                    }

                    // 마찰 계수 (0.0 ~ 1.0)
                    ImGui::DragFloat("Friction", &CollisionModule->Friction, 0.01f, 0.0f, 1.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("마찰 계수\n0.0 = 미끄러움 (얼음)\n1.0 = 거친 표면");

                    // 파티클 반지름 스케일 (충돌체 크기 보정)
                    ImGui::DragFloat("Radius Scale", &CollisionModule->RadiusScale, 0.05f, 0.01f, 10.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("파티클 충돌 반지름 스케일\n파티클 크기 대비 충돌 영역 크기\n1.0 = 파티클 크기와 동일");

                    ImGui::Spacing();

                	// Events Section
                	ImGui::Text("Events");
                	if (ImGui::IsItemHovered()) ImGui::SetTooltip("충돌 이벤트 설정");
                	ImGui::Checkbox("Write Collision Events", &CollisionModule->bWriteEvent);
                	if (ImGui::IsItemHovered()) ImGui::SetTooltip("충돌 이벤트 발생 시 델리게이트 브로드캐스트\n게임 로직과 연동 시 활성화");

                	if (CollisionModule->bWriteEvent)
                	{
                		ImGui::SameLine();
                		ImGui::TextDisabled("(Delegate Broadcast)");
        
                		// Event Name 입력 필드
                		ImGui::Text("Event Name");
                		if (ImGui::IsItemHovered()) ImGui::SetTooltip("충돌 이벤트의 이름\n다른 모듈에서 수신할 이벤트 이름");
                		ImGui::SameLine();
        
                		static char CollisionEventNameBuffer[256] = "";
                		if (ImGui::InputText("##CollisionEventName", CollisionEventNameBuffer, IM_ARRAYSIZE(CollisionEventNameBuffer)))
                		{
                			CollisionModule->EventName = FString(CollisionEventNameBuffer);
                		}
                	}
				}
				else if (auto* EventReceiverModule = Cast<UParticleModuleEventReceiverSpawn>(SelectedModule))
				{
					ImGui::Text("Event Receiver Spawn Settings");
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("충돌 파티클 모듈의 이벤트 발생 시\n자동으로 파티클 스폰\n이벤트 기반 연쇄 파티클 효과 생성");
					}
					ImGui::Separator();

					// Event Name
					ImGui::Text("Event Name");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("수신할 이벤트의 이름\n충돌 모듈의 이벤트 이름과 일치해야 함");
					ImGui::SameLine();

					static char EventNameBuffer[256] = "";
					if (ImGui::InputText("##EventName", EventNameBuffer, IM_ARRAYSIZE(EventNameBuffer)))
					{
						EventReceiverModule->EventName = FName(EventNameBuffer);
					}
					ImGui::Spacing();

					// Spawn Count
					ImGui::Text("Spawn Count");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("이벤트 발생 시 스폰할 파티클의 수");
					ImGui::SameLine();
					ImGui::DragInt("##Count", &EventReceiverModule->Count, 1, 0, 100);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("기본 스폰 개수");
					ImGui::Spacing();

					// Count Range
					ImGui::Text("Count Range");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("스폰 개수의 랜덤 범위\n실제 스폰 수 = Count ~ Count + CountRange");
					ImGui::SameLine();
					ImGui::DragInt("##CountRange", &EventReceiverModule->CountRange, 1, 0, 100);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("예: Count=5, CountRange=3 → 5~8개 스폰");
					ImGui::Spacing();

					// Initial Speed
					ImGui::Text("Initial Speed");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("스폰된 파티클의 초기 속도");
					ImGui::SameLine();
					ImGui::DragFloat("##InitialSpeed", &EventReceiverModule->InitialSpeed, 0.5f, 0.0f, 1000.0f, "%.2f");
					ImGui::Spacing();

					ImGui::Text("Direction & Location");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("스폰 위치 및 방향 설정");
					ImGui::Separator();

					// Use Emitter Direction
					ImGui::Checkbox("Use Event Direction", &EventReceiverModule->bUseEmitterDirection);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("활성화: 스폰된 파티클의 속도가 이벤트 방향을 반영\n비활성화: 일정한 방향으로 스폰");
					}
					ImGui::Spacing();

					// Use Emitter Location
					ImGui::Checkbox("Use Event Location", &EventReceiverModule->bUseEmitterLocation);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("활성화: 스폰 위치가 이벤트 발생 위치\n비활성화: 이미터 기본 위치에서 스폰");
					}
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

	// ===== 수직 스플리터 (좌우 분할) =====
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
	ImGui::Button("##VSplitter", ImVec2(splitterSize, contentSize.y));
	if (ImGui::IsItemActive())
	{
		float delta = ImGui::GetIO().MouseDelta.x;
		LeftPanelRatio += delta / contentSize.x;
		LeftPanelRatio = FMath::Clamp(LeftPanelRatio, 0.15f, 0.5f);
	}
	if (ImGui::IsItemHovered())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	ImGui::PopStyleColor(3);

	ImGui::SameLine();

	// ===== 우측 컬럼 (이미터 패널 + 커브 에디터) =====
	ImGui::BeginChild("RightColumn", ImVec2(rightPanelWidth, contentSize.y), false);
	{
		// 이미터 패널 (상단)
		RenderEmitterPanel(rightPanelWidth, rightTopHeight);

		// 우측 수평 스플리터
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
		ImGui::Button("##RightHSplitter", ImVec2(-1, splitterSize));
		if (ImGui::IsItemActive())
		{
			float delta = ImGui::GetIO().MouseDelta.y;
			RightBottomRatio -= delta / contentSize.y;
			RightBottomRatio = FMath::Clamp(RightBottomRatio, 0.15f, 0.7f);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		ImGui::PopStyleColor(3);

		// 커브 에디터 (하단)
		RenderCurveEditor(rightPanelWidth, rightBottomHeight);
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
		const uint32 NewWidth = static_cast<uint32>(CenterRect.Right - CenterRect.Left);
		const uint32 NewHeight = static_cast<uint32>(CenterRect.Bottom - CenterRect.Top);

		Viewport->Resize(NewStartX, NewStartY, NewWidth, NewHeight);

		// 뷰포트 렌더링 (ImGui보다 먼저)
		Viewport->Render();
	}
}

void SParticleViewerWindow::CreateParticleSystem()
{
	if (!PreviewWorld) { return; }

	// 기존 시스템 정리
	if (SavePath.empty() && CurrentParticleSystem)
	{
		ObjectFactory::DeleteObject(CurrentParticleSystem);
		CurrentParticleSystem = nullptr;
	}

	// 빈 ParticleSystem 생성
	UParticleSystem* NewSystem = NewObject<UParticleSystem>();
	CurrentParticleSystem = NewSystem;
	SavePath.clear();

	// 기존 PreviewActor가 있으면 제거
	if (PreviewActor)
	{
		PreviewActor->EndPlay();
		PreviewActor->Destroy();
		PreviewActor = nullptr;
		PreviewComponent = nullptr;
	}

	// 새 Actor 생성
	PreviewActor = PreviewWorld->SpawnActor<AActor>();
	PreviewActor->ObjectName = FName("ParticlePreviewActor");
	PreviewActor->SetActorLocation(FVector(0, 0, 0));

	// ParticleSystemComponent 생성 및 추가
	PreviewComponent = Cast<UParticleSystemComponent>(PreviewActor->AddNewComponent(UParticleSystemComponent::StaticClass()));
	PreviewComponent->SetTemplate(NewSystem);

	// Actor의 BeginPlay 호출
	PreviewActor->BeginPlay();

	// 컴포넌트 초기화 및 활성화
	PreviewComponent->ResetAndActivate();

	UE_LOG("New particle system created");
}

void SParticleViewerWindow::LoadParticleSystem()
{
	// 다이얼로그로 로드
	FWideString WideInitialPath = UTF8ToWide(ParticlePath.string());
	std::filesystem::path WidePath = FPlatformProcess::OpenLoadFileDialog(WideInitialPath, L"particle", L"Particle Files");
	FString PathStr = ResolveAssetRelativePath(WidePath.string(), ParticlePath.string());

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
	if (!PreviewWorld) { return; }

	// ParticleSystem이 nullptr이면 새로 생성
	if (!ParticleSystem)
	{
		CreateParticleSystem();
		return;
	}

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
		PreviewActor->EndPlay();
		PreviewActor->Destroy();
		PreviewActor = nullptr;
		PreviewComponent = nullptr;
	}

	// 새 Actor 생성
	PreviewActor = PreviewWorld->SpawnActor<AActor>();
	PreviewActor->ObjectName = FName("ParticlePreviewActor");
	PreviewActor->SetActorLocation(FVector(0, 0, 0));

	// ParticleSystemComponent 생성 및 추가
	PreviewComponent = Cast<UParticleSystemComponent>(PreviewActor->AddNewComponent(UParticleSystemComponent::StaticClass()));
	PreviewComponent->SetTemplate(ParticleSystem);

	// Actor의 BeginPlay 호출 (InitializeComponent 호출)
	PreviewActor->BeginPlay();

	// 컴포넌트 초기화 및 활성화 (BeginPlay 이후에!)
	PreviewComponent->ResetAndActivate();

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
    FString PathStr = ResolveAssetRelativePath(WidePath.string(), ParticlePath.string());
    
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

	// 파티클 시스템에 이미터 추가
	CurrentParticleSystem->AddEmitter(UParticleEmitter::StaticClass());

	// 런타임 캐시 재구축
	CurrentParticleSystem->BuildRuntimeCache();

	// PreviewComponent가 있으면 다시 초기화
	if (PreviewComponent)
	{
		PreviewComponent->ResetAndActivate();
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
		PreviewComponent->ResetAndActivate();

	UE_LOG("Emitter deleted successfully");
}

// ===== 분리된 렌더링 함수들 =====

void SParticleViewerWindow::RenderMenuBar()
{
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Particle System"))
			{
				CreateParticleSystem();
			}
			if (ImGui::MenuItem("Open..."))
			{
				LoadParticleSystem();
			}
			if (ImGui::MenuItem("Save"))
			{
				SaveParticleSystem();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Close"))
			{
				bIsOpen = false;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}

void SParticleViewerWindow::RenderToolbar()
{
	// 툴바 버튼들
	if (ImGui::Button("Save"))
	{
		SaveParticleSystem();
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart"))
	{
		if (PreviewComponent)
		{
			PreviewComponent->ResetAndActivate();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Emitter"))
	{
		CreateNewEmitter();
	}
	ImGui::SameLine();
	if (ImGui::Button(bPaused ? "Resume" : "Pause"))
	{
		bPaused = !bPaused;
		if (PreviewComponent)
		{
			if (bPaused)
				PreviewComponent->PauseSimulation();
			else
				PreviewComponent->ResumeSimulation();
		}
	}
	ImGui::Separator();
}

void SParticleViewerWindow::RenderViewportPanel(float Width, float Height)
{
	ImGui::BeginChild("ViewportContainer", ImVec2(Width, Height), false);
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
}

void SParticleViewerWindow::RenderEmitterPanel(float Width, float Height)
{
	ImGui::BeginChild("EmitterPanel", ImVec2(Width, Height), true, ImGuiWindowFlags_HorizontalScrollbar);
	{
		ImGui::Text("이미터");
		ImGui::Separator();

		// 빈 영역 클릭 감지
		bool bShowContextMenu = false;
		bool bClickedOnEmitterBlock = false;
		bool bEmitterPanelClicked = false;
		if (ImGui::IsWindowHovered())
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				bShowContextMenu = true;
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				bEmitterPanelClicked = true;
			}
		}

		// Delete 키 입력 감지
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Delete))
			{
				if (SelectedModule)
				{
					if (Cast<UParticleModuleRequired>(SelectedModule))
					{
						UE_LOG("Required 모듈은 삭제할 수 없습니다.");
					}
					else
					{
						UParticleLODLevel* OwnerLOD = nullptr;
						if (CurrentParticleSystem)
						{
							for (UParticleEmitter* Emitter : CurrentParticleSystem->Emitters)
							{
								if (Emitter && Emitter->LODLevels.Num() > 0)
								{
									UParticleLODLevel* LOD = Emitter->LODLevels[0];
									if (LOD && LOD->AllModulesCache.Contains(SelectedModule))
									{
										OwnerLOD = LOD;
										break;
									}
								}
							}
						}
						if (OwnerLOD)
						{
							OwnerLOD->RemoveModule(SelectedModule);
							SelectedModule = nullptr;
							CurrentParticleSystem->BuildRuntimeCache();
							if (PreviewComponent)
							{
								PreviewComponent->ResetAndActivate();
							}
						}
					}
				}
				else if (SelectedEmitter)
				{
					DeleteSelectedEmitter();
				}
			}
		}

		if (CurrentParticleSystem && CurrentParticleSystem->Emitters.Num() > 0)
		{
			for (int i = 0; i < CurrentParticleSystem->Emitters.Num(); i++)
			{
				UParticleEmitter* Emitter = CurrentParticleSystem->Emitters[i];
				if (!Emitter) continue;

				ImGui::PushID(i);
				const float emitterBlockWidth = 200.0f;
				ImGui::BeginChild("EmitterBlock", ImVec2(emitterBlockWidth, 0), true);
				{
					if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						bClickedOnEmitterBlock = true;
					}

					bool bIsSelected = (SelectedEmitter == Emitter);
					if (bIsSelected)
					{
						SelectedEmitter;
					}
					const float headerHeight = 50.0f;

					if (bIsSelected)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0f, 0.5f, 0.0f, 0.8f));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0f, 0.6f, 0.1f, 0.9f));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
					}

					ImGui::PushID("emitter_header");
					if (ImGui::Selectable("##emitterheader", bIsSelected, 0, ImVec2(0, headerHeight)))
					{
						SelectedEmitter = Emitter;
						bClickedOnEmitterBlock = true;
					}
					if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						bClickedOnEmitterBlock = true;
					}
					ImGui::PopID();

					if (bIsSelected)
					{
						ImGui::PopStyleColor(3);
					}

					ImVec2 headerMin = ImGui::GetItemRectMin();
					ImVec2 headerMax = ImGui::GetItemRectMax();

					ImGui::SetCursorScreenPos(ImVec2(headerMin.x + 5, headerMin.y + 5));
					ImGui::Text("%s", Emitter->GetName().c_str());

					ImGui::SetCursorScreenPos(ImVec2(headerMax.x - 45, headerMin.y + 5));
					UTexture* EmitterMatTexture = nullptr;
					const char* matTooltip = "No Material";
					if (Emitter->LODLevels.Num() > 0 && Emitter->LODLevels[0]->RequiredModule && Emitter->LODLevels[0]->RequiredModule->Material)
					{
						UMaterialInterface* mat = Emitter->LODLevels[0]->RequiredModule->Material;
						EmitterMatTexture = mat->GetTexture(EMaterialTextureSlot::Diffuse);
						matTooltip = mat->GetName().c_str();
					}
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

					ImGui::SetCursorScreenPos(ImVec2(headerMin.x, headerMax.y));
					ImGui::Separator();

					// 모듈 리스트
					if (Emitter->LODLevels.Num() > 0)
					{
						UParticleLODLevel* LOD = Emitter->LODLevels[0];
						if (LOD)
						{
							float itemWidth = ImGui::GetContentRegionAvail().x;
							float buttonWidth = 20.0f;
							float rightMargin = 10.0f;
							float nameWidth = itemWidth - buttonWidth * 2 - rightMargin - 8;

							// TypeData 모듈 찾기 (Mesh 등)
							UParticleModuleTypeDataBase* TypeDataModule = nullptr;
							for (UParticleModule* Module : LOD->AllModulesCache)
							{
								if (UParticleModuleTypeDataBase* TD = Cast<UParticleModuleTypeDataBase>(Module))
								{
									TypeDataModule = TD;
									break;
								}
							}

							// 1. TypeData 슬롯 (Mesh 모듈 등) - Emitter 헤더 바로 아래
							ImGui::PushID("TypeDataSlot");
							if (TypeDataModule)
							{
								// TypeData 모듈이 있으면 표시 (파란색)
								bool isSelected = (SelectedModule == TypeDataModule);
								ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.4f, 0.8f, 0.8f));
								ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.5f, 0.9f, 0.9f));
								ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));

                                const char* typeName = "TypeData";
                                const char* typeTooltip = "파티클 렌더링 타입을 결정하는 모듈";
                                if (Cast<UParticleModuleMesh>(TypeDataModule))
                                {
                                    typeName = "Mesh";
                                    typeTooltip = "[Mesh TypeData]\n스프라이트 대신 3D 메시를 파티클로 렌더링합니다.\n각 파티클이 지정된 StaticMesh 형태로 표시됩니다.";
                                }
                                else if (Cast<UParticleModuleBeam>(TypeDataModule))
                                {
                                    typeName = "Beam";
                                    typeTooltip = "[Beam TypeData]\n두 점 사이를 연결하는 빔(레이저) 형태로 렌더링합니다.\n번개, 레이저 빔, 전기 효과 등에 사용됩니다.";
                                }
								else if (Cast<UParticleModuleRibbon>(TypeDataModule))
								{
									typeName = "Ribbon";
									typeTooltip = "[Ribbon TypeData]\n부착된 액터의 꼬리(Trail) 형태로 파티클을 렌더링합니다. \n검 휘두르는 효과, 별똥별 등에 사용됩니다.";
								}

                                if (ImGui::Selectable(typeName, isSelected, 0, ImVec2(nameWidth, 20)))
                                {
                                    SelectedModule = TypeDataModule;
                                }
                                if (ImGui::IsItemHovered())
                                {
                                    ImGui::SetTooltip("%s", typeTooltip);
                                }
                                ImGui::PopStyleColor(3);
                                ImGui::SameLine();
                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                                if (TypeDataModule->bEnabled)
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.7f, 0.0f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
                                    if (ImGui::Button("V##TD", ImVec2(buttonWidth, 20)))
                                    {
                                        TypeDataModule->bEnabled = false;
                                    }
                                    ImGui::PopStyleColor(3);
                                }
                                else
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.0f, 0.0f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                                    if (ImGui::Button("X##TD", ImVec2(buttonWidth, 20)))
                                    {
                                        TypeDataModule->bEnabled = true;
                                    }
                                    ImGui::PopStyleColor(3);
                                }
                                ImGui::PopStyleVar();
                                ImGui::SameLine();
                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                                if (ImGui::Button("C##TD", ImVec2(buttonWidth, 20)))
                                {
                                    SelectedModule = TypeDataModule;
                                }
                                ImGui::PopStyleVar();
                            }
                            else
                            {
                                // TypeData 모듈이 없으면 빈 슬롯 표시
                                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.6f));
                                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                                ImGui::Selectable("(None)", false, ImGuiSelectableFlags_Disabled, ImVec2(nameWidth, 20));
                                ImGui::PopStyleColor(3);
                                ImGui::SameLine();
                                ImGui::Dummy(ImVec2(buttonWidth * 2 + 4, 20));
                            }
                            ImGui::PopID();

                            // 2. Required 모듈 렌더링
                            if (LOD->RequiredModule)
                            {
                                ImGui::PushID("RequiredModule");
                                bool isSelected = (SelectedModule == LOD->RequiredModule);
                                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.8f, 0.7f, 0.0f, 0.8f));
                                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.9f, 0.8f, 0.1f, 0.9f));
                                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
                                if (ImGui::Selectable("Required", true, 0, ImVec2(nameWidth, 20)))
                                {
                                    SelectedModule = LOD->RequiredModule;
                                }
                                if (ImGui::IsItemHovered())
                                {
                                    ImGui::SetTooltip("[Required Module]\n에미터의 필수 기본 설정을 담당합니다.\n- Material: 파티클 텍스처/셰이더\n- Duration: 에미터 지속 시간\n- Max Particles: 최대 파티클 수\n- Local/World Space 설정\n- SubUV (스프라이트 시트) 설정");
                                }
                                ImGui::PopStyleColor(3);
                                ImGui::SameLine();
                                ImGui::Dummy(ImVec2(buttonWidth, 20));
                                ImGui::SameLine();
                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                                if (ImGui::Button("C##Req", ImVec2(buttonWidth, 20)))
                                {
                                    SelectedModule = LOD->RequiredModule;
                                }
                                ImGui::PopStyleVar();
                                ImGui::PopID();
                            }

							// 3. 나머지 모듈들 렌더링 (Required, TypeData 제외)
							for (int m = 0; m < LOD->AllModulesCache.Num(); m++)
							{
								if (UParticleModule* Module = LOD->AllModulesCache[m])
								{
									// Required와 TypeData는 이미 위에서 렌더링했으므로 skip
									if (Cast<UParticleModuleRequired>(Module)) continue;
									if (Cast<UParticleModuleTypeDataBase>(Module)) continue;

									ImGui::PushID(m + 1000);
									bool isSelected = (SelectedModule == Module);
									const char* fullName = Module->GetClass()->Name;
									const char* displayName = fullName;
									const char* prefix = "UParticleModule";
									size_t prefixLen = strlen(prefix);
									if (strncmp(fullName, prefix, prefixLen) == 0)
									{
										displayName = fullName + prefixLen;
									}

									bool isSpawn = (strcmp(displayName, "Spawn") == 0);

									if (isSpawn)
									{
										ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
										ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.9f));
										ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
									}

									bool showAsSelected = isSelected || isSpawn;
									if (ImGui::Selectable(displayName, showAsSelected, 0, ImVec2(nameWidth, 20)))
									{
										SelectedModule = Module;
									}

                                    // 모듈별 호버링 툴팁
                                    if (ImGui::IsItemHovered())
                                    {
                                        const char* moduleTooltip = "";
                                        if (strcmp(displayName, "Spawn") == 0)
                                            moduleTooltip = "[Spawn Module]\n파티클 생성 방식을 제어합니다.\n- Constant: 초당 일정 수 생성\n- OverTime: 시간에 따라 생성률 변화\n- Burst: 특정 시점에 한꺼번에 생성";
                                        else if (strcmp(displayName, "Lifetime") == 0)
                                            moduleTooltip = "[Lifetime Module]\n개별 파티클의 수명을 설정합니다.\n수명이 다한 파티클은 자동으로 소멸됩니다.\nRange 옵션으로 랜덤 수명 범위 지정 가능.";
                                        else if (strcmp(displayName, "Size") == 0)
                                            moduleTooltip = "[Size Module]\n파티클의 초기 크기를 설정합니다.\nX, Y, Z 축별로 개별 설정 가능.\nRange 옵션으로 랜덤 크기 범위 지정 가능.";
                                        else if (strcmp(displayName, "SizeMultiplyLife") == 0)
                                            moduleTooltip = "[Size Multiply Life Module]\n파티클 수명에 따라 크기를 변화시킵니다.\n커브로 시작->중간->끝 크기 비율 설정.\n폭발 효과(커졌다 작아짐) 등에 활용.";
                                        else if (strcmp(displayName, "Location") == 0)
                                            moduleTooltip = "[Location Module]\n파티클 생성 위치를 설정합니다.\n- Point: 특정 점/범위\n- Box: 박스 영역 내 랜덤\n- Sphere: 구 영역 내 랜덤\n- Cylinder: 원기둥 영역 내 랜덤";
                                        else if (strcmp(displayName, "Velocity") == 0)
                                            moduleTooltip = "[Velocity Module]\n파티클의 초기 속도와 중력을 설정합니다.\nX, Y, Z 방향별 속도 지정.\nGravity로 중력 효과 적용 가능.";
                                        else if (strcmp(displayName, "VelocityCone") == 0)
                                            moduleTooltip = "[Velocity Cone Module]\n원뿔 형태로 퍼지는 속도를 설정합니다.\n- Direction: 중심 방향\n- Angle: 퍼짐 각도 (0=직선, 180=구형)\n분사, 폭발 효과에 적합.";
                                        else if (strcmp(displayName, "Color") == 0)
                                            moduleTooltip = "[Color Module]\n파티클의 초기 색상과 투명도를 설정합니다.\nRGB 색상 + Alpha(투명도) 설정.\nRange 옵션으로 랜덤 색상 범위 지정 가능.";
                                        else if (strcmp(displayName, "ColorOverLife") == 0)
                                            moduleTooltip = "[Color Over Life Module]\n파티클 수명에 따라 색상/투명도를 변화시킵니다.\n페이드 인/아웃 효과에 적합.\n시작 색상 -> 끝 색상 자동 보간.";
                                        else if (strcmp(displayName, "Rotation") == 0)
                                            moduleTooltip = "[Rotation Module]\n파티클의 초기 회전 각도를 설정합니다.\n라디안 단위 (PI = 180도).\nRange 옵션으로 랜덤 회전 범위 지정 가능.";
                                        else if (strcmp(displayName, "RotationRate") == 0)
                                            moduleTooltip = "[Rotation Rate Module]\n파티클의 회전 속도를 설정합니다.\n- Initial Rotation: 초기 회전 각도\n- Rotation Rate: 초당 회전 속도 (rad/s)\n회전하는 파편, 눈송이 등에 활용.";
                                        else if (strcmp(displayName, "SubUV") == 0)
                                            moduleTooltip = "[SubUV Module]\n스프라이트 시트 애니메이션을 제어합니다.\nRequired에서 설정한 SubImages를 사용.\n- Index: 현재 프레임 (0~1 범위)\n- Interp: 보간 방식 (None/Linear/Random)";
                                        else if (strcmp(displayName, "Collision") == 0)
                                            moduleTooltip = "[Collision Module]\n파티클의 충돌 처리를 담당합니다.\n- Bounce: 튕김 (반사)\n- Stop: 정지\n- Kill: 소멸\nRestitution/Friction으로 물리 특성 조절.";
                                        else
                                            moduleTooltip = "파티클 모듈";

                                        ImGui::SetTooltip("%s", moduleTooltip);
                                    }

                                    if (isSpawn)
                                    {
                                        ImGui::PopStyleColor(3);
                                    }

									ImGui::SameLine();
									if (!isSpawn)
									{
										ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
										if (Module->bEnabled)
										{
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
										ImGui::Dummy(ImVec2(buttonWidth, 20));
									}
									ImGui::SameLine();
									ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
									if (ImGui::Button("C", ImVec2(buttonWidth, 20)))
									{
										SelectedModule = Module;
									}
									ImGui::PopStyleVar();
									ImGui::PopID();
								}
							}

							// 빈 공간 우클릭 시 모듈 추가 메뉴
							if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
							{
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
                                    CurrentParticleSystem->BuildRuntimeCache();
                                    PreviewComponent->ResetAndActivate();
                                }
                                if (ImGui::MenuItem("Velocity"))
                                {
                                    LOD->AddModule(UParticleModuleVelocity::StaticClass());
                                    CurrentParticleSystem->BuildRuntimeCache();
                                    PreviewComponent->ResetAndActivate();
                                }
                                if (ImGui::MenuItem("VelocityCone"))
                                {
                                    LOD->AddModule(UParticleModuleVelocityCone::StaticClass());
                                    CurrentParticleSystem->BuildRuntimeCache();
                                    PreviewComponent->ResetAndActivate();
                                }
                                if (ImGui::MenuItem("Size"))
                                {
                                    LOD->AddModule(UParticleModuleSize::StaticClass());
                                    CurrentParticleSystem->BuildRuntimeCache();
                                    PreviewComponent->ResetAndActivate();
                                }
                                if (ImGui::MenuItem("Color"))
                                {
                                    LOD->AddModule(UParticleModuleColor::StaticClass());
                                    CurrentParticleSystem->BuildRuntimeCache();
                                    PreviewComponent->ResetAndActivate();
                                }
                                if (ImGui::MenuItem("Location"))
                                {
                                    LOD->AddModule(UParticleModuleLocation::StaticClass());
                                    CurrentParticleSystem->BuildRuntimeCache();
                                    PreviewComponent->ResetAndActivate();
                                }

								ImGui::Separator();
								ImGui::TextDisabled("라이프타임 기반");
								ImGui::Separator();

								if (ImGui::MenuItem("Color Over Life"))
								{
									LOD->AddModule(UParticleModuleColorOverLife::StaticClass());
									CurrentParticleSystem->BuildRuntimeCache();
									PreviewComponent->ResetAndActivate();
								}
								if (ImGui::MenuItem("Size Multiply Life"))
								{
									LOD->AddModule(UParticleModuleSizeMultiplyLife::StaticClass());
									CurrentParticleSystem->BuildRuntimeCache();
									PreviewComponent->ResetAndActivate();
								}

								ImGui::Separator();
								ImGui::TextDisabled("회전");
								ImGui::Separator();

								if (ImGui::MenuItem("Rotation"))
								{
									LOD->AddModule(UParticleModuleRotation::StaticClass());
									CurrentParticleSystem->BuildRuntimeCache();
									PreviewComponent->ResetAndActivate();
								}
								if (ImGui::MenuItem("Rotation Rate"))
								{
									LOD->AddModule(UParticleModuleRotationRate::StaticClass());
									CurrentParticleSystem->BuildRuntimeCache();
									PreviewComponent->ResetAndActivate();
								}

								ImGui::Separator();
								ImGui::TextDisabled("렌더링 타입");
								ImGui::Separator();

								if (ImGui::MenuItem("Mesh"))
								{
									UParticleModuleMesh* MeshModule = Cast<UParticleModuleMesh>(LOD->AddModule(UParticleModuleMesh::StaticClass()));
									if (MeshModule && SelectedEmitter)
									{
										CurrentParticleSystem->BuildRuntimeCache();
										PreviewComponent->ResetAndActivate();
									}
								}
								if (ImGui::MenuItem("Ribbon"))
								{
									UParticleModuleRibbon* RibbonModule = Cast<UParticleModuleRibbon>(LOD->AddModule(UParticleModuleRibbon::StaticClass()));
									if (RibbonModule && SelectedEmitter)
									{										
										CurrentParticleSystem->BuildRuntimeCache();
										PreviewComponent->ResetAndActivate();
									}
								}

                                if (ImGui::MenuItem("Beam"))
                                {
                                    UParticleModuleBeam* BeamModule = Cast<UParticleModuleBeam>(LOD->AddModule(UParticleModuleBeam::StaticClass()));
                                    if (BeamModule && Emitter)
                                    {
                                        BeamModule->ApplyToEmitter(Emitter);
                                        CurrentParticleSystem->BuildRuntimeCache();
                                        PreviewComponent->ResetAndActivate();
                                    }
                                }

                                ImGui::Separator();
                                ImGui::TextDisabled("텍스처 애니메이션");
                                ImGui::Separator();

								if (ImGui::MenuItem("SubUV"))
								{
									LOD->AddModule(UParticleModuleSubUV::StaticClass());
									CurrentParticleSystem->BuildRuntimeCache();
									PreviewComponent->ResetAndActivate();
								}

                                ImGui::Separator();
                                ImGui::TextDisabled("충돌");
                                ImGui::Separator();

                                if (ImGui::MenuItem("Collision"))
                                {
                                    LOD->AddModule(UParticleModuleCollision::StaticClass());
                                    CurrentParticleSystem->BuildRuntimeCache();
                                    PreviewComponent->ResetAndActivate();
                                }

                                if (ImGui::MenuItem("EventReceiver Spawn"))
                                {
                                    LOD->AddModule(UParticleModuleEventReceiverSpawn::StaticClass());
                                    CurrentParticleSystem->BuildRuntimeCache();
                                    PreviewComponent->ResetAndActivate();
                                }

                                ImGui::EndPopup();
                            }
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::PopID();
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

		if (bEmitterPanelClicked && !bClickedOnEmitterBlock)
		{
			SelectedEmitter = nullptr;
		}
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
}

void SParticleViewerWindow::RenderCurveEditor(float Width, float Height)
{
	ImGui::BeginChild("CurveEditor", ImVec2(Width, Height), true);
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
						else if (auto* ColorModule = dynamic_cast<UParticleModuleColorOverLife*>(SelectedModule))
						{
							// ColorOverLife - Alpha 커브 포인트
							ImVec2 p1Screen = WorldToScreen(ColorModule->AlphaPoint1Time, ColorModule->AlphaPoint1Value);
							ImVec2 p2Screen = WorldToScreen(ColorModule->AlphaPoint2Time, ColorModule->AlphaPoint2Value);

							// 마우스 위치와의 거리 계산
							float dist1 = sqrtf((io.MousePos.x - p1Screen.x) * (io.MousePos.x - p1Screen.x) +
								(io.MousePos.y - p1Screen.y) * (io.MousePos.y - p1Screen.y));
							float dist2 = sqrtf((io.MousePos.x - p2Screen.x) * (io.MousePos.x - p2Screen.x) +
								(io.MousePos.y - p2Screen.y) * (io.MousePos.y - p2Screen.y));

							const float clickRadius = 10.0f;

							if (dist1 < clickRadius && dist1 <= dist2)
							{
								DraggingPointIndex = 0;
								bDraggingPoint = true;
							}
							else if (dist2 < clickRadius)
							{
								DraggingPointIndex = 1;
								bDraggingPoint = true;
							}
							else
							{
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
						else if (auto* ColorModule = dynamic_cast<UParticleModuleColorOverLife*>(SelectedModule))
						{
							FVector2D worldPos = ScreenToWorld(io.MousePos);

							// X: 시간 (0~1)
							float time = FMath::Clamp(worldPos.X, 0.0f, 1.0f);
							// Y: Alpha 값 (0~1)
							float alpha = FMath::Clamp(worldPos.Y, 0.0f, 1.0f);

							if (DraggingPointIndex == 0)
							{
								ColorModule->AlphaPoint1Time = FMath::Min(time, ColorModule->AlphaPoint2Time - 0.01f);
								ColorModule->AlphaPoint1Value = alpha;
							}
							else if (DraggingPointIndex == 1)
							{
								ColorModule->AlphaPoint2Time = FMath::Max(time, ColorModule->AlphaPoint1Time + 0.01f);
								ColorModule->AlphaPoint2Value = alpha;
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
				if (auto* SizeModule = Cast<UParticleModuleSizeMultiplyLife>(SelectedModule))
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
				else if (auto* ColorModule = Cast<UParticleModuleColorOverLife>(SelectedModule))
				{
					// ColorOverLife - Alpha 커브 (0~1 범위)
					float p1Time = ColorModule->AlphaPoint1Time;
					float p1Value = ColorModule->AlphaPoint1Value;
					float p2Time = ColorModule->AlphaPoint2Time;
					float p2Value = ColorModule->AlphaPoint2Value;

					// 1. 점1 이전: 왼쪽 끝(0)부터 점1까지 수평선
					ImVec2 leftStart = WorldToScreen(0.0f, p1Value);
					ImVec2 leftEnd = WorldToScreen(p1Time, p1Value);
					draw_list->AddLine(leftStart, leftEnd, IM_COL32(100, 200, 255, 255), 2.0f);

					// 2. 점1에서 점2까지 선형 보간
					ImVec2 p1Screen = WorldToScreen(p1Time, p1Value);
					ImVec2 p2Screen = WorldToScreen(p2Time, p2Value);
					draw_list->AddLine(p1Screen, p2Screen, IM_COL32(100, 200, 255, 255), 2.0f);

					// 3. 점2 이후: 점2부터 오른쪽 끝(1)까지 수평선
					ImVec2 rightStart = WorldToScreen(p2Time, p2Value);
					ImVec2 rightEnd = WorldToScreen(1.0f, p2Value);
					draw_list->AddLine(rightStart, rightEnd, IM_COL32(100, 200, 255, 255), 2.0f);

					// 키포인트 그리기
					float radius1 = (DraggingPointIndex == 0) ? 8.0f : 6.0f;
					float radius2 = (DraggingPointIndex == 1) ? 8.0f : 6.0f;

					// 점1 (파랑)
					draw_list->AddCircleFilled(p1Screen, radius1, IM_COL32(100, 150, 255, 255));
					draw_list->AddCircle(p1Screen, radius1 + 1.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);

					// 점2 (하늘색)
					draw_list->AddCircleFilled(p2Screen, radius2, IM_COL32(150, 255, 255, 255));
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
}

