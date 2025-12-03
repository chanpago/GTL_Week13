#include "pch.h"
#include <string>
#include "TargetActorTransformWidget.h"
#include "UIManager.h"
#include "ImGui/imgui.h"
#include "Vector.h"
#include "World.h"
#include "ResourceManager.h"
#include "SelectionManager.h"
#include "WorldPartitionManager.h"
#include "PropertyRenderer.h"

#include "Actor.h"
#include "Grid/GridActor.h"
#include "Gizmo/GizmoActor.h"
#include "StaticMeshActor.h"
#include "FakeSpotLightActor.h"

#include "StaticMeshComponent.h"
#include "TextRenderComponent.h"
#include "CameraComponent.h"
#include "BillboardComponent.h"
#include "DecalComponent.h"
#include "PerspectiveDecalComponent.h"
#include "HeightFogComponent.h"
#include "DirectionalLightComponent.h"
#include "AmbientLightComponent.h"
#include "PointLightComponent.h"
#include "SpotLightComponent.h"
#include "SceneComponent.h"
#include "PrimitiveComponent.h"
#include "Color.h"
#include "PlatformProcess.h"
#include "JsonSerializer.h"
#include "SkeletalMeshComponent.h"
#include "Source/Editor/BlueprintGraph/AnimationGraph.h"
#include "Source/Runtime/Core/Misc/PathUtils.h"
#include "PlayerCameraManager.h"
#include "Camera/CamMod_DOF.h"

using namespace std;

IMPLEMENT_CLASS(UTargetActorTransformWidget)

namespace
{
	struct FAddableComponentDescriptor
	{
		const char* Label;
		UClass* Class;
		const char* Description;
	};

	const TArray<FAddableComponentDescriptor>& GetAddableComponentDescriptors()
	{
		static TArray<FAddableComponentDescriptor> Options = []()
			{
				TArray<FAddableComponentDescriptor> Result;

				// 리플렉션 시스템을 통해 자동으로 컴포넌트 목록 가져오기
				TArray<UClass*> ComponentClasses = UClass::GetAllComponents();

				for (UClass* Class : ComponentClasses)
				{
					if (Class && Class->bIsComponent && Class->DisplayName)
					{
						Result.push_back({
							Class->DisplayName,
							Class,
							Class->Description ? Class->Description : ""
						});
					}
				}

				return Result;
			}();
		return Options;
	}

	bool TryAttachComponentToActor(AActor& Actor, UClass* ComponentClass, UActorComponent*& SelectedComponent)
	{
		// 부모 컴포넌트 결정 (만약 ParentToAttach가 null이면, AddNewComponent가 알아서 Root에 붙여줄 것임)
		USceneComponent* ParentToAttach = Cast<USceneComponent>(SelectedComponent);

		// AActor에 새로운 컴포넌트 추가
		UActorComponent* NewComp = Actor.AddNewComponent(ComponentClass, ParentToAttach);
		if (!NewComp)
		{
			return false;
		}

		// 새로 생긴 컴포넌트 선택
		SelectedComponent = NewComp;
		GWorld->GetSelectionManager()->SelectComponent(SelectedComponent);

		return true;
	}

	void MarkComponentSubtreeVisited(USceneComponent* Component, TSet<USceneComponent*>& Visited)
	{
		if (!Component || Visited.count(Component))
		{
			return;
		}

		Visited.insert(Component);
		for (USceneComponent* Child : Component->GetAttachChildren())
		{
			MarkComponentSubtreeVisited(Child, Visited);
		}
	}

	void RenderActorComponent(
		AActor* SelectedActor,
		UActorComponent* SelectedComponent,
		UActorComponent* ComponentPendingRemoval
	)
	{
		for (UActorComponent* Component : SelectedActor->GetOwnedComponents())
		{
			if (Cast<USceneComponent>(Component))
			{
				continue;
			}

			ImGuiTreeNodeFlags NodeFlags =
				ImGuiTreeNodeFlags_SpanAvailWidth |
				ImGuiTreeNodeFlags_Leaf | 
				ImGuiTreeNodeFlags_NoTreePushOnOpen;

			// 선택 하이라이트: 현재 선택된 컴포넌트와 같으면 Selected 플래그
			if (Component == SelectedComponent && !GWorld->GetSelectionManager()->IsActorMode())
			{
				NodeFlags |= ImGuiTreeNodeFlags_Selected;
			}

			FString Label = Component->GetClass() ? Component->GetName() : "Unknown Component";

			ImGui::PushID(Component);
			const bool bNodeOpen = ImGui::TreeNodeEx(Component, NodeFlags, "%s", Label.c_str());
			// 좌클릭 시 컴포넌트 선택으로 전환(액터 Row 선택 해제)
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				GWorld->GetSelectionManager()->SelectComponent(Component);
			}

			if (ImGui::BeginPopupContextItem("ComponentContext"))
			{
				const bool bCanRemove = !Component->IsNative();
				if (ImGui::MenuItem("삭제", "Delete", false, bCanRemove))
				{
					ComponentPendingRemoval = Component;
				}
				ImGui::EndPopup();
			}
			
			ImGui::PopID();
		}
	}
	void RenderSceneComponentTree(
		USceneComponent* Component,
		AActor& Actor,
		UActorComponent*& SelectedComponent,
		UActorComponent*& ComponentPendingRemoval,
		TSet<USceneComponent*>& Visited)
	{
		if (!Component)
			return;

		// 에디터 전용 컴포넌트는 계층구조에 표시하지 않음
		// (CREATE_EDITOR_COMPONENT로 생성된 DirectionGizmo, SpriteComponent 등)
		if (!Component->IsEditable())
		{
			return;
		}

		Visited.insert(Component);
		const TArray<USceneComponent*>& Children = Component->GetAttachChildren();
		const bool bHasChildren = !Children.IsEmpty();

		ImGuiTreeNodeFlags NodeFlags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DefaultOpen;

		if (!bHasChildren)
		{
			NodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		// 선택 하이라이트: 현재 선택된 컴포넌트와 같으면 Selected 플래그
		if (Component == SelectedComponent && !GWorld->GetSelectionManager()->IsActorMode())
		{
			NodeFlags |= ImGuiTreeNodeFlags_Selected;
		}

		FString Label = Component->GetClass() ? Component->GetName() : "Unknown Component";
		if (Component == Actor.GetRootComponent())
		{
			Label += " (Root)";
		}

		// 트리 노드 그리기 직전에 ID 푸시
		ImGui::PushID(Component);
		const bool bNodeOpen = ImGui::TreeNodeEx(Component, NodeFlags, "%s", Label.c_str());
		// 좌클릭 시 컴포넌트 선택으로 전환(액터 Row 선택 해제)
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			GWorld->GetSelectionManager()->SelectComponent(Component);
		}

		if (ImGui::BeginPopupContextItem("ComponentContext"))
		{
			const bool bCanRemove = !Component->IsNative();
			if (ImGui::MenuItem("삭제", "Delete", false, bCanRemove))
			{
				ComponentPendingRemoval = Component;
			}
			ImGui::EndPopup();
		}

		if (bNodeOpen && bHasChildren)
		{
			for (USceneComponent* Child : Children)
			{
				RenderSceneComponentTree(Child, Actor, SelectedComponent, ComponentPendingRemoval, Visited);
			}
			ImGui::TreePop();
		}
		else if (!bNodeOpen && bHasChildren)
		{
			for (USceneComponent* Child : Children)
			{
				MarkComponentSubtreeVisited(Child, Visited);
			}
		}
		// 항목 처리가 끝나면 반드시 PopID
		ImGui::PopID();
	}
}

// 파일명 스템(Cube 등) 추출 + .obj 확장자 제거
static inline FString GetBaseNameNoExt(const FString& Path)
{
	const size_t sep = Path.find_last_of("/\\");
	const size_t start = (sep == FString::npos) ? 0 : sep + 1;

	const FString ext = ".obj";
	size_t end = Path.size();

	if (end >= ext.size())
	{
		FString PathExt = Path.substr(end - ext.size());

		std::transform(PathExt.begin(), PathExt.end(), PathExt.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (PathExt == ext)
		{
			end -= ext.size();
		}
	}

	if (start <= end)
	{
		return Path.substr(start, end - start);
	}

	return Path.substr(start);
}

UTargetActorTransformWidget::UTargetActorTransformWidget()
	: UWidget("Target Actor Transform Widget")
	, UIManager(&UUIManager::GetInstance())
{

}

UTargetActorTransformWidget::~UTargetActorTransformWidget() = default;

void UTargetActorTransformWidget::OnSelectedActorCleared()
{
	ResetChangeFlags();
}

void UTargetActorTransformWidget::Initialize()
{
	// UIManager 참조 확보
	UIManager = &UUIManager::GetInstance();

	// Transform 위젯을 UIManager에 등록하여 선택 해제 브로드캐스트를 받을 수 있게 함
	if (UIManager)
	{
		UIManager->RegisterTargetTransformWidget(this);
	}
}


void UTargetActorTransformWidget::Update()
{
	USceneComponent* SelectedComponent = GWorld->GetSelectionManager()->GetSelectedComponent();
	if (SelectedComponent)
	{
		if (!bRotationEditing)
		{
			UpdateTransformFromComponent(SelectedComponent);
		}
	}
}

void UTargetActorTransformWidget::RenderWidget()
{
	AActor* SelectedActor = GWorld->GetSelectionManager()->GetSelectedActor();
	UActorComponent* SelectedComponent = GWorld->GetSelectionManager()->GetSelectedActorComponent();
	if (!SelectedActor)
	{
		return;
	}

	// 1. 헤더 (액터 이름, "+추가" 버튼) 렌더링
	RenderHeader(SelectedActor, SelectedComponent);

	// 2. 컴포넌트 계층 구조 렌더링
	RenderComponentHierarchy(SelectedActor, SelectedComponent);
	// 위 함수에서 SelectedComponent를 Delete하는데 아래 함수에서 SelectedComponent를 그대로 인자로 사용하고 있었음.
	// 댕글링 포인터 참조를 막기 위해 다시 한번 SelectionManager에서 Component를 얻어옴
	// 기존에는 DestroyComponent에서 DeleteObject를 호출하지도 않았음. Delete를 실제로 진행하면서 발견된 버그.
	
	// 3. 선택된 컴포넌트, 엑터의 상세 정보 렌더링 (Transform 포함)
	if (GWorld->GetSelectionManager()->IsActorMode())
	{
		RenderSelectedActorDetails(GWorld->GetSelectionManager()->GetSelectedActor());
	}
	else
	{
		RenderSelectedComponentDetails(GWorld->GetSelectionManager()->GetSelectedActorComponent());
	}
}

void UTargetActorTransformWidget::RenderHeader(AActor* SelectedActor, UActorComponent* SelectedComponent)
{
	ImGui::Text(SelectedActor->GetName().c_str());
	ImGui::SameLine();

    if (ImGui::Button("to Prefab", ImVec2(80, 25)))
    {
        std::filesystem::path PrefabPath = FPlatformProcess::OpenSaveFileDialog(UTF8ToWide(GDataDir) + L"/Prefabs", L"prefab", L"Prefab Files", UTF8ToWide(SelectedActor->ObjectName.ToString()));
        if (!PrefabPath.empty())
        {
            // If the actor has a skeletal mesh component with an animation graph,
            // save the graph alongside the prefab using the same stem name.
            if (UActorComponent* Comp = SelectedActor->GetComponent(USkeletalMeshComponent::StaticClass()))
            {
                if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Comp))
                {
                    if (SkelComp->GetAnimGraph())
                    {
                        // Build graph path: Data/Graphs/<PrefabStem>.graph
                        std::filesystem::path GraphDir = UTF8ToWide(GDataDir) + L"/Graphs";
                        std::error_code ec;
                        std::filesystem::create_directories(GraphDir, ec);

                        std::filesystem::path GraphPath = GraphDir / PrefabPath.stem();
                        GraphPath.replace_extension(L".graph");

                        JSON GraphJson = JSON::Make(JSON::Class::Object);
                        SkelComp->GetAnimGraph()->Serialize(false, GraphJson);
                        FJsonSerializer::SaveJsonToFile(GraphJson, GraphPath.wstring());

                        // Set AnimGraphPath relative under Data
                        FString Stem = WideToUTF8(PrefabPath.stem().wstring());
                        SkelComp->SetAnimGraphPath(FString("Data/Graphs/") + Stem + ".graph");
                    }
                }
            }

            // Save the actor prefab (includes AnimGraphPath via component serialization)
            JSON ActorJson;
            ActorJson["Type"] = SelectedActor->GetClass()->Name;
            SelectedActor->Serialize(false, ActorJson);
            bool bSuccess = FJsonSerializer::SaveJsonToFile(ActorJson, PrefabPath);
        }
    }

	ImGui::SameLine();
	const float ButtonWidth = 60.0f;
	const float ButtonHeight = 25.0f;
	float Avail = ImGui::GetContentRegionAvail().x;
	float NewX = ImGui::GetCursorPosX() + std::max(0.0f, Avail - ButtonWidth);
	ImGui::SetCursorPosX(NewX);

	if (ImGui::Button("+ 추가", ImVec2(ButtonWidth, ButtonHeight)))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		ImGui::TextUnformatted("Add Component");
		ImGui::Separator();

		for (const FAddableComponentDescriptor& Descriptor : GetAddableComponentDescriptors())
		{
			ImGui::PushID(Descriptor.Label);
			if (ImGui::Selectable(Descriptor.Label))
			{
				if (TryAttachComponentToActor(*SelectedActor, Descriptor.Class, SelectedComponent))
				{
					ImGui::CloseCurrentPopup();
				}
			}
			if (Descriptor.Description && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Descriptor.Description);
			}
			ImGui::PopID();
		}
		ImGui::EndPopup();
	}
	ImGui::Spacing();
}

void UTargetActorTransformWidget::RenderComponentHierarchy(AActor* SelectedActor, UActorComponent* SelectedComponent)
{
	AActor* ActorPendingRemoval = nullptr;
	UActorComponent* ComponentPendingRemoval = nullptr;

	// 컴포넌트 트리 박스 크기 관련
	static float PaneHeight = 120.0f;
	const float SplitterThickness = 4.0f;
	const float MinTop = 1.0f;
	const float MinBottom = 0.0f;
	const float WindowAvailY = ImGui::GetContentRegionAvail().y;
	PaneHeight = std::clamp(PaneHeight, MinTop, std::max(MinTop, WindowAvailY - (MinBottom + SplitterThickness)));

	ImGui::BeginChild("ComponentBox", ImVec2(0, PaneHeight), true);

	// 액터 행 렌더링
	ImGui::PushID("ActorDisplay");
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));

	const bool bActorSelected = GWorld->GetSelectionManager()->IsActorMode();
	if (ImGui::Selectable(SelectedActor->GetName().c_str(), bActorSelected, ImGuiSelectableFlags_SelectOnClick | ImGuiSelectableFlags_SpanAvailWidth))
	{
		GWorld->GetSelectionManager()->SelectActor(SelectedActor);
	}
	ImGui::PopStyleColor();
	if (ImGui::BeginPopupContextItem("ActorContextMenu"))
	{
		if (ImGui::MenuItem("삭제", "Delete", false, true))
		{
			ActorPendingRemoval = SelectedActor;
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();

	// 컴포넌트 트리 렌더링
	USceneComponent* RootComponent = SelectedActor->GetRootComponent();
	if (!RootComponent)
	{
		ImGui::BulletText("Root component not found.");
	}
	else
	{
		TSet<USceneComponent*> VisitedComponents;
		RenderSceneComponentTree(RootComponent, *SelectedActor, SelectedComponent, ComponentPendingRemoval, VisitedComponents);
		// ... (루트에 붙지 않은 씬 컴포넌트 렌더링 로직은 생략 가능성 있음, 엔진 설계에 따라)
		ImGui::Separator();
		RenderActorComponent(SelectedActor, SelectedComponent, ComponentPendingRemoval);
	}

	// 삭제 입력 처리 - 이 위젯에 포커스가 있을 때만 처리
	const bool bDeletePressed = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete);
	if (bDeletePressed)
	{
		if (bActorSelected) ActorPendingRemoval = SelectedActor;
		else if (SelectedComponent && !SelectedComponent->IsNative()) ComponentPendingRemoval = SelectedComponent;
	}

	// 컴포넌트 삭제 실행
	if (ComponentPendingRemoval)
	{

		USceneComponent* NewSelection = SelectedActor->GetRootComponent();
		// SelectionManager를 통해 선택 해제
		GWorld->GetSelectionManager()->ClearSelection();

		// 컴포넌트 삭제
		SelectedActor->RemoveOwnedComponent(ComponentPendingRemoval);
		ComponentPendingRemoval = nullptr;

		// 삭제 후 새로운 컴포넌트 선택
		if (NewSelection)
		{
			GWorld->GetSelectionManager()->SelectComponent(NewSelection);
		}
	}

	// 액터 삭제 실행
	if (ActorPendingRemoval)
	{
		// 삭제 전에 선택 해제 (dangling pointer 방지)
		GWorld->GetSelectionManager()->ClearSelection();

		// 액터 삭제
		ActorPendingRemoval->Destroy();

		OnSelectedActorCleared();
	}

	ImGui::EndChild();

	// 스플리터 렌더링
	ImGui::InvisibleButton("VerticalSplitter", ImVec2(-FLT_MIN, SplitterThickness));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	if (ImGui::IsItemActive()) PaneHeight += ImGui::GetIO().MouseDelta.y;
	ImVec2 Min = ImGui::GetItemRectMin(), Max = ImGui::GetItemRectMax();
	ImGui::GetWindowDrawList()->AddLine(ImVec2(Min.x, (Min.y + Max.y) * 0.5f), ImVec2(Max.x, (Min.y + Max.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
	ImGui::Spacing();
}

// 액터 와 루트 컴포넌트 프로퍼티 출력
void UTargetActorTransformWidget::RenderSelectedActorDetails(AActor* SelectedActor)
{
	if (!SelectedActor)
	{
		return;
	}

	// 액터 프로퍼티 표시
	UPropertyRenderer::RenderAllPropertiesWithInheritance(SelectedActor);

	ImGui::Spacing();
	ImGui::Separator();

	// 루트 컴포넌트의 프로퍼티를 이어서 표시
	USceneComponent* RootComponent = SelectedActor->GetRootComponent();
	if (RootComponent)
	{
		UPropertyRenderer::RenderAllPropertiesWithInheritance(RootComponent);
	}

	ImGui::Spacing();
	ImGui::Separator();

	// PlayerCameraManager인 경우 DOF 설정 UI 표시
	if (APlayerCameraManager* CamMgr = Cast<APlayerCameraManager>(SelectedActor))
	{
		RenderDOFSettings(CamMgr);
	}
}

void UTargetActorTransformWidget::RenderDOFSettings(APlayerCameraManager* CamMgr)
{
	if (!CamMgr)
	{
		return;
	}

	// 기존 DOF Modifier 찾기
	UCamMod_DOF* DOF = nullptr;
	for (UCameraModifierBase* Mod : CamMgr->ActiveModifiers)
	{
		if (UCamMod_DOF* Found = Cast<UCamMod_DOF>(Mod))
		{
			DOF = Found;
			break;
		}
	}

	bool bDOFEnabled = (DOF != nullptr && DOF->bEnabled);

	if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(10.0f);

		// DOF Enable 체크박스
		bool bPrevEnabled = bDOFEnabled;
		if (ImGui::Checkbox("Enable DOF", &bDOFEnabled))
		{
			if (bDOFEnabled && !bPrevEnabled)
			{
				// DOF 활성화 - Modifier 생성
				if (!DOF)
				{
					DOF = new UCamMod_DOF();
					CamMgr->ActiveModifiers.Add(DOF);
				}
				DOF->bEnabled = true;
			}
			else if (!bDOFEnabled && bPrevEnabled)
			{
				// DOF 비활성화
				if (DOF)
				{
					DOF->bEnabled = false;
				}
			}
		}

		if (bDOFEnabled && DOF)
		{
			ImGui::Spacing();
			ImGui::Text("Focus Settings");

			// Focal Distance
			ImGui::Text("Focal Distance (m)");
			ImGui::SetNextItemWidth(-1);
			ImGui::DragFloat("##FocalDistance", &DOF->FocalDistance, 0.1f, 0.1f, 1000.0f, "%.1f");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The distance to the focal plane in meters");
			}

			// Focal Region
			ImGui::Text("Focal Region (m)");
			ImGui::SetNextItemWidth(-1);
			ImGui::DragFloat("##FocalRegion", &DOF->FocalRegion, 0.1f, 0.0f, 100.0f, "%.1f");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The size of the perfectly sharp region around the focal plane");
			}

			ImGui::Spacing();
			ImGui::Text("Transition Regions");

			// Near Transition
			ImGui::Text("Near Transition (m)");
			ImGui::SetNextItemWidth(-1);
			ImGui::DragFloat("##NearTransition", &DOF->NearTransitionRegion, 0.1f, 0.1f, 100.0f, "%.1f");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Distance over which blur transitions from sharp to max near blur");
			}

			// Far Transition
			ImGui::Text("Far Transition (m)");
			ImGui::SetNextItemWidth(-1);
			ImGui::DragFloat("##FarTransition", &DOF->FarTransitionRegion, 0.1f, 0.1f, 500.0f, "%.1f");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Distance over which blur transitions from sharp to max far blur");
			}

			ImGui::Spacing();
			ImGui::Text("Blur Size (pixels)");

			// Max Near Blur
			ImGui::Text("Max Near Blur");
			ImGui::SetNextItemWidth(-1);
			ImGui::DragFloat("##MaxNearBlur", &DOF->MaxNearBlurSize, 0.5f, 0.0f, 128.0f, "%.1f");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Maximum blur radius for near (foreground) objects in pixels");
			}

			// Max Far Blur
			ImGui::Text("Max Far Blur");
			ImGui::SetNextItemWidth(-1);
			ImGui::DragFloat("##MaxFarBlur", &DOF->MaxFarBlurSize, 0.5f, 0.0f, 128.0f, "%.1f");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Maximum blur radius for far (background) objects in pixels");
			}
		}

		ImGui::Unindent(10.0f);
	}
}

// 컴포넌트의 프로퍼티 출력
void UTargetActorTransformWidget::RenderSelectedComponentDetails(UActorComponent* SelectedComponent)
{
	if (!SelectedComponent) return;

	// 리플렉션이 적용된 컴포넌트는 자동으로 UI 생성
	if (SelectedComponent)
	{
		UPropertyRenderer::RenderAllPropertiesWithInheritance(SelectedComponent);
	}
}

void UTargetActorTransformWidget::UpdateTransformFromComponent(USceneComponent* SelectedComponent)
{
	if (SelectedComponent)
	{
		EditLocation = SelectedComponent->GetRelativeLocation();
		EditRotation = SelectedComponent->GetRelativeRotation().ToEulerZYXDeg();
		EditScale = SelectedComponent->GetRelativeScale();
	}

	ResetChangeFlags();
}

void UTargetActorTransformWidget::ResetChangeFlags()
{
	bPositionChanged = false;
	bRotationChanged = false;
	bScaleChanged = false;
}
