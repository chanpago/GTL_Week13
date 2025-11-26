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
						ImGui::NextColumn();
						ImGui::SetNextItemWidth(-1);
						ImGui::DragInt("##SortPriority", &RequiredModule->SortPriority, 1, 1, 10000);
						ImGui::NextColumn();
					}

					ImGui::Spacing();

					// Max Particles
					{
						ImGui::Text("Max Particles");
						ImGui::NextColumn();
						ImGui::SetNextItemWidth(-1);
						ImGui::DragInt("##MaxParticles", &RequiredModule->MaxParticles, 1, 1, 10000);
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
								? "Local Space: Particles follow the actor (e.g., rocket engine)"
								: "World Space: Particles stay in place after spawn (e.g., explosion)");
						}

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

					ImGui::Spacing();

					// SubUV Settings (스프라이트 시트 애니메이션)
					{
						ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "SubUV (Sprite Sheet)");
						ImGui::NextColumn();
						ImGui::NextColumn();
					}

					ImGui::Spacing();

					{
						ImGui::Text("SubImages Horizontal");
						ImGui::NextColumn();
						ImGui::DragInt("##SubImagesH", &RequiredModule->SubImages_Horizontal, 1.0f, 1, 16);
						ImGui::NextColumn();
					}

					{
						ImGui::Text("SubImages Vertical");
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
                    const char* SpawnTypes[] = {"Constant (Continuous)", "Curve (Over Time)", "Burst"};
                    int CurrentType = static_cast<int>(SpawnModule->SpawnRateType);
                    if (ImGui::Combo("Rate Type", &CurrentType, SpawnTypes, IM_ARRAYSIZE(SpawnTypes)))
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
                        DrawFloatDist("Spawn Rate (Per Second)", SpawnModule->SpawnRate);
                    }
                    else if (SpawnModule->SpawnRateType == ESpawnRateType::OverTime) // OverTime
                    {
                        DrawFloatDist("Spawn Rate (Scale Over Life)", SpawnModule->SpawnRateOverTime);
                        ImGui::TextDisabled("Note: Multiplies base rate by curve over emitter duration.");
                    }
                    else
                    {
                        // --- 3. Burst List (배열 관리) ---
                        ImGui::Text("Burst List");
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
                            ImGui::NextColumn();
                            ImGui::Text("Count");
                            ImGui::NextColumn();
                            ImGui::Text("Range");
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
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("0.0 = Start, 1.0 = End");
                                ImGui::NextColumn();

                                // 2. Count
                                ImGui::DragInt("##Count", &Entry.Count, 1, 0, 1000);
                                ImGui::NextColumn();

                                // 3. Range (Variance)
                                ImGui::DragInt("##Range", &Entry.CountRange, 1, 0, 1000);
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
                    if (LifetimeModule->Lifetime.bUseRange)
                    {
                        ImGui::DragFloat("Lifetime Min", &LifetimeModule->Lifetime.MinValue, 0.01f, 0.0f, 100.0f);
                        ImGui::DragFloat("Lifetime Max", &LifetimeModule->Lifetime.MaxValue, 0.01f, 0.0f, 100.0f);
                    }
                    else
                    {
                        ImGui::DragFloat("Lifetime", &LifetimeModule->Lifetime.MinValue, 0.01f, 0.0f, 100.0f);
                    }
                    ImGui::Checkbox("Use Range", &LifetimeModule->Lifetime.bUseRange);
                }
                else if (auto* SizeModule = Cast<UParticleModuleSize>(SelectedModule))
                {
                    ImGui::Text("Size Settings");
                    if (SizeModule->StartSize.bUseRange)
                    {
                        ImGui::DragFloat3("Start Size Min", &SizeModule->StartSize.MinValue.X, 1.0f, 0.0f, 1000.0f);
                        ImGui::DragFloat3("Start Size Max", &SizeModule->StartSize.MaxValue.X, 1.0f, 0.0f, 1000.0f);
                    }
                    else
                    {
                        ImGui::DragFloat3("Start Size", &SizeModule->StartSize.MinValue.X, 1.0f, 0.0f, 1000.0f);
                    }
                    ImGui::Checkbox("Use Range", &SizeModule->StartSize.bUseRange);
                }
                else if (auto* LocationModule = Cast<UParticleModuleLocation>(SelectedModule))
                {
                    ImGui::Text("Location Settings");

					// Distribution Type
					const char* DistTypes[] = { "Point", "Box", "Sphere", "Cylinder" };
					int CurrentDistType = (int)LocationModule->DistributionType;
					if (ImGui::Combo("Distribution Type", &CurrentDistType, DistTypes, IM_ARRAYSIZE(DistTypes)))
					{
						LocationModule->DistributionType = (ELocationDistributionType)CurrentDistType;
					}

					ImGui::Spacing();

					// 타입별 파라미터
					switch (LocationModule->DistributionType)
					{
					case ELocationDistributionType::Point:
						ImGui::DragFloat3("Start Location Min", &LocationModule->StartLocation.MinValue.X, 1.0f, -1000.0f, 1000.0f);
						ImGui::DragFloat3("Start Location Max", &LocationModule->StartLocation.MaxValue.X, 1.0f, -1000.0f, 1000.0f);
						ImGui::Checkbox("Use Range", &LocationModule->StartLocation.bUseRange);
						break;

					case ELocationDistributionType::Box:
						ImGui::DragFloat3("Box Extent", &LocationModule->BoxExtent.X, 1.0f, 0.0f, 1000.0f);
						break;

					case ELocationDistributionType::Sphere:
						ImGui::DragFloat("Sphere Radius", &LocationModule->SphereRadius, 1.0f, 0.0f, 1000.0f);
						break;

                    case ELocationDistributionType::Cylinder:
                        ImGui::DragFloat("Cylinder Radius", &LocationModule->CylinderRadius, 1.0f, 0.0f, 1000.0f);
                        ImGui::DragFloat("Cylinder Height", &LocationModule->CylinderHeight, 1.0f, 0.0f, 1000.0f);
                        break;
                    }
                }
                else if (auto* VelocityModule = Cast<UParticleModuleVelocity>(SelectedModule))
                {
                    ImGui::Text("Velocity Settings");
                    if (VelocityModule->StartVelocity.bUseRange)
                    {
                        ImGui::DragFloat3("Start Velocity Min", &VelocityModule->StartVelocity.MinValue.X, 1.0f,
                                          -1000.0f, 1000.0f);
                        ImGui::DragFloat3("Start Velocity Max", &VelocityModule->StartVelocity.MaxValue.X, 1.0f,
                                          -1000.0f, 1000.0f);
                    }
                    else
                    {
                        ImGui::DragFloat3("Start Velocity", &VelocityModule->StartVelocity.MinValue.X, 1.0f, -1000.0f,
                                          1000.0f);
                    }
                    ImGui::Checkbox("Use Range", &VelocityModule->StartVelocity.bUseRange);
                    ImGui::DragFloat3("Gravity", &VelocityModule->Gravity.X, 1.0f, -10000.0f, 10000.0f);
                }
                else if (auto* ConeModule = Cast<UParticleModuleVelocityCone>(SelectedModule))
                {
                    ImGui::Text("Velocity Cone Settings");
                    ImGui::Separator();

                    // 1. 속도 (세기)
                    ImGui::Text("Velocity (Speed)");
                    ImGui::DragFloat("Min##Vel", &ConeModule->Velocity.MinValue, 1.0f, 0.0f, 10000.0f);
                    ImGui::DragFloat("Max##Vel", &ConeModule->Velocity.MaxValue, 1.0f, 0.0f, 10000.0f);
                    ImGui::Checkbox("Range##Vel", &ConeModule->Velocity.bUseRange);
    
                    ImGui::Spacing();

                    // 2. 각도 (퍼짐 정도)
                    ImGui::Text("Cone Angle (Degrees)");
                    ImGui::DragFloat("Angle##Cone", &ConeModule->Angle.MinValue, 0.5f, 0.0f, 180.0f);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = Straight, 90 = Hemisphere, 180 = Sphere");
    
                    ImGui::Spacing();

                    // 3. 방향 (Direction)
                    ImGui::Text("Cone Direction");
                    float Dir[3] = { ConeModule->Direction.X, ConeModule->Direction.Y, ConeModule->Direction.Z };
                    if (ImGui::DragFloat3("Dir", Dir, 0.01f, -1.0f, 1.0f))
                    {
                        ConeModule->Direction = FVector(Dir[0], Dir[1], Dir[2]);
                    }
                    if (ImGui::Button("Normalize Direction"))
                    {
                        ConeModule->Direction.Normalize();
                    }
                }
                else if (auto* ColorModule = Cast<UParticleModuleColor>(SelectedModule))
                {
                    ImGui::Text("Color Settings");
                    if (ColorModule->StartColor.bUseRange)
                    {
                        ImGui::ColorEdit3("Start Color Min", &ColorModule->StartColor.MinValue.R);
                        ImGui::ColorEdit3("Start Color Max", &ColorModule->StartColor.MaxValue.R);
                    }
                    else
                    {
                        ImGui::ColorEdit3("Start Color", &ColorModule->StartColor.MinValue.R);
                    }
                    ImGui::Checkbox("Use Range", &ColorModule->StartColor.bUseRange);

					if (ColorModule->StartAlpha.bUseRange)
					{
						ImGui::DragFloat("Start Alpha Min", &ColorModule->StartAlpha.MinValue, 0.01f, 0.0f, 1.0f);
						ImGui::DragFloat("Start Alpha Max", &ColorModule->StartAlpha.MaxValue, 0.01f, 0.0f, 1.0f);
					}
					else
					{
						ImGui::DragFloat("Start Alpha", &ColorModule->StartAlpha.MinValue, 0.01f, 0.0f, 1.0f);
					}
				}
				else if (auto* ColorOverLifeModule = Cast<UParticleModuleColorOverLife>(SelectedModule))
				{
					ImGui::Text("Color Over Life Settings");
					ImGui::Separator();

					ImGui::Checkbox("Use Color Over Life", &ColorOverLifeModule->bUseColorOverLife);
					if (ColorOverLifeModule->bUseColorOverLife)
					{
						if (ColorOverLifeModule->ColorOverLife.bUseRange)
						{
							ImGui::ColorEdit3("Color Min", &ColorOverLifeModule->ColorOverLife.MinValue.R);
							ImGui::ColorEdit3("Color Max", &ColorOverLifeModule->ColorOverLife.MaxValue.R);
						}
						else
						{
							ImGui::ColorEdit3("Color", &ColorOverLifeModule->ColorOverLife.MinValue.R);
						}
						ImGui::Checkbox("Color Use Range", &ColorOverLifeModule->ColorOverLife.bUseRange);
					}

					ImGui::Spacing();
					ImGui::Checkbox("Use Alpha Over Life", &ColorOverLifeModule->bUseAlphaOverLife);
					if (ColorOverLifeModule->bUseAlphaOverLife)
					{
						if (ColorOverLifeModule->AlphaOverLife.bUseRange)
						{
							ImGui::DragFloat("Alpha Min", &ColorOverLifeModule->AlphaOverLife.MinValue, 0.01f, 0.0f,
								1.0f);
							ImGui::DragFloat("Alpha Max", &ColorOverLifeModule->AlphaOverLife.MaxValue, 0.01f, 0.0f,
								1.0f);
						}
						else
						{
							ImGui::DragFloat("Alpha", &ColorOverLifeModule->AlphaOverLife.MinValue, 0.01f, 0.0f, 1.0f);
						}
						ImGui::Checkbox("Alpha Use Range", &ColorOverLifeModule->AlphaOverLife.bUseRange);
					}
				}
				else if (auto* SizeMultiplyLifeModule = Cast<UParticleModuleSizeMultiplyLife>(SelectedModule))
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
				else if (auto* RotationModule = Cast<UParticleModuleRotation>(SelectedModule))
				{
					ImGui::Text("Rotation Settings");
					ImGui::Separator();

					if (RotationModule->StartRotation.bUseRange)
					{
						ImGui::DragFloat("Start Rotation Min (Radians)", &RotationModule->StartRotation.MinValue, 0.01f,
							-6.28f, 6.28f);
						ImGui::DragFloat("Start Rotation Max (Radians)", &RotationModule->StartRotation.MaxValue, 0.01f,
							-6.28f, 6.28f);
					}
					else
					{
						ImGui::DragFloat("Start Rotation (Radians)", &RotationModule->StartRotation.MinValue, 0.01f,
							-6.28f, 6.28f);
					}
					ImGui::Checkbox("Use Range", &RotationModule->StartRotation.bUseRange);

					ImGui::Spacing();
					ImGui::TextDisabled("Tip: PI = 3.14159, 2*PI = 6.28318");
				}
				else if (auto* RotationRateModule = Cast<UParticleModuleRotationRate>(SelectedModule))
				{
					ImGui::Text("Rotation Rate Settings");
					ImGui::Separator();

					ImGui::Text("Initial Rotation");
					if (RotationRateModule->InitialRotation.bUseRange)
					{
						ImGui::DragFloat("Initial Rotation Min (Rad)", &RotationRateModule->InitialRotation.MinValue,
							0.01f, 0.0f, 6.28318f);
						ImGui::DragFloat("Initial Rotation Max (Rad)", &RotationRateModule->InitialRotation.MaxValue,
							0.01f, 0.0f, 6.28318f);
					}
					else
					{
						ImGui::DragFloat("Initial Rotation (Rad)", &RotationRateModule->InitialRotation.MinValue, 0.01f,
							0.0f, 6.28318f);
					}
					ImGui::Checkbox("Use Initial Rotation Range", &RotationRateModule->InitialRotation.bUseRange);

					ImGui::Spacing();
					ImGui::Text("Rotation Speed");
					if (RotationRateModule->StartRotationRate.bUseRange)
					{
						ImGui::DragFloat("Start Rotation Rate Min (Rad/s)",
							&RotationRateModule->StartRotationRate.MinValue, 0.01f, -10.0f, 10.0f);
						ImGui::DragFloat("Start Rotation Rate Max (Rad/s)",
							&RotationRateModule->StartRotationRate.MaxValue, 0.01f, -10.0f, 10.0f);
					}
					else
					{
						ImGui::DragFloat("Start Rotation Rate (Rad/s)", &RotationRateModule->StartRotationRate.MinValue,
							0.01f, -10.0f, 10.0f);
					}
					ImGui::Checkbox("Use Rotation Rate Range", &RotationRateModule->StartRotationRate.bUseRange);

					ImGui::Spacing();
					ImGui::TextDisabled("Tip: PI = 3.14159, 2*PI = 6.28318");
					ImGui::TextDisabled("Tip: 1 rad/s = ~57 degrees/s");
				}
				else if (auto* SubUVModule = Cast<UParticleModuleSubUV>(SelectedModule))
				{
					ImGui::Text("SubUV Animation Settings");
					ImGui::Separator();

					// SubImageIndex 커브 (0~1 범위, 실제로는 곱하기 TotalFrames-1)
					ImGui::Text("SubImage Index (0~1)");
					ImGui::DragFloat("Index Min", &SubUVModule->SubImageIndex.MinValue, 0.01f, 0.0f, 1.0f);
					ImGui::DragFloat("Index Max", &SubUVModule->SubImageIndex.MaxValue, 0.01f, 0.0f, 1.0f);
					ImGui::Checkbox("Use Range##SubUV", &SubUVModule->SubImageIndex.bUseRange);

					ImGui::Spacing();

					// 보간 방식
					const char* InterpMethods[] = { "None", "Linear Blend", "Random", "Random Blend" };
					int CurrentMethod = (int)SubUVModule->InterpMethod;
					if (ImGui::Combo("Interpolation Method", &CurrentMethod, InterpMethods, IM_ARRAYSIZE(InterpMethods)))
					{
						SubUVModule->InterpMethod = (ESubUVInterpMethod)CurrentMethod;
					}

					ImGui::Spacing();
					ImGui::Checkbox("Use Real Time", &SubUVModule->bUseRealTime);

					ImGui::Spacing();
					ImGui::TextDisabled("Tip: Required 모듈에서 SubImages_Horizontal/Vertical 설정 필요");
				}
				else if (auto* MeshModule = Cast<UParticleModuleMesh>(SelectedModule))
				{
					ImGui::Spacing();
					ImGui::Columns(2, "MeshModuleColumns", false);
					ImGui::SetColumnWidth(0, 150.0f);

					// Mesh
					{
						ImGui::Text("Mesh");
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
                    ImGui::Spacing();
                    ImGui::Columns(2, "BeamModuleColumns", false);
                    ImGui::SetColumnWidth(0, 150.0f);

                    // Source Point
                    {
                        ImGui::Text("Source Point");
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
                        ImGui::NextColumn();
                    }

                    // Target Point
                    {
                        ImGui::Text("Target Point");
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
                        ImGui::NextColumn();
                    }

                    // Tessellation Factor
                    {
                        ImGui::Text("Tessellation Factor");
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
                        ImGui::NextColumn();
                    }

                    // Noise Frequency
                    {
                        ImGui::Text("Noise Frequency");
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
                        ImGui::NextColumn();
                    }

                    // Noise Amplitude
                    {
                        ImGui::Text("Noise Amplitude");
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
                        ImGui::NextColumn();
                    }

                    // Use Random Offset (번개 효과)
                    {
                        ImGui::Text("Use Random Offset");
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
                        ImGui::NextColumn();
                    }

                    // Source Offset (랜덤 범위)
                    if (BeamModule->bUseRandomOffset)
                    {
                        ImGui::Text("Source Offset");
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
                        ImGui::NextColumn();

                        // Target Offset (랜덤 범위)
                        ImGui::Text("Target Offset");
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
                    ImGui::Separator();

                    const char* ResponseItems[] = { "Bounce", "Stop", "Kill" }; 
                    int CurrentResponse = (int)CollisionModule->CollisionResponse;
    
                    if (ImGui::Combo("Response", &CurrentResponse, ResponseItems, IM_ARRAYSIZE(ResponseItems)))
                    {
                        CollisionModule->CollisionResponse = (EParticleCollisionResponse)CurrentResponse;
                    }
                    ImGui::Spacing();

                    ImGui::Text("Physics Properties");
                    // Restitution은 Bounce 모드일 때만 유효
                    if (CollisionModule->CollisionResponse != EParticleCollisionResponse::Bounce)
                    {
                        ImGui::BeginDisabled(); // UI 비활성화 시작
                    }
    
                    // 1.0을 넘으면 에너지가 증폭
                    ImGui::DragFloat("Restitution (Bounciness)", &CollisionModule->Restitution, 0.01f, 0.0f, 2.0f, "%.2f");
    
                    if (CollisionModule->CollisionResponse != EParticleCollisionResponse::Bounce)
                    {
                        ImGui::EndDisabled(); // UI 비활성화 끝
                    }

                    // 마찰 계수 (0.0 ~ 1.0)
                    ImGui::DragFloat("Friction", &CollisionModule->Friction, 0.01f, 0.0f, 1.0f, "%.2f");

                    // 파티클 반지름 스케일 (충돌체 크기 보정)
                    ImGui::DragFloat("Radius Scale", &CollisionModule->RadiusScale, 0.05f, 0.01f, 10.0f, "%.2f");

                    ImGui::Spacing();

                    // 3. Events
                    ImGui::Text("Events");
                    ImGui::Checkbox("Write Collision Events", &CollisionModule->bWriteEvent);
    
                    if (CollisionModule->bWriteEvent)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(Delegate Broadcast)");
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Note: Requires valid depth buffer or scene geometry.");
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
	// 빈 ParticleSystem 생성
	UParticleSystem* NewSystem = NewObject<UParticleSystem>();
	LoadParticleSystem(NewSystem);
	SavePath.clear();
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

	// 새 이미터 생성 (생성자에서 기본 모듈들이 자동으로 생성됨)
	UParticleEmitter* NewEmitter = NewObject<UParticleEmitter>();

	// 파티클 시스템에 이미터 추가
	CurrentParticleSystem->Emitters.Add(NewEmitter);

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
                                if (Cast<UParticleModuleMesh>(TypeDataModule)) typeName = "Mesh";
                                else if (Cast<UParticleModuleBeam>(TypeDataModule)) typeName = "Beam";
								if (Cast<UParticleModuleRibbon>(TypeDataModule)) typeName = "Ribbon";

								if (ImGui::Selectable(typeName, isSelected, 0, ImVec2(nameWidth, 20)))
								{
									SelectedModule = TypeDataModule;
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

