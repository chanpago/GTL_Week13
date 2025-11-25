#include "pch.h"
#include "Actor.h"
#include "SceneComponent.h"
#include "ObjectFactory.h"
#include "MeshComponent.h"
#include "TextRenderComponent.h"
#include "WorldPartitionManager.h"
#include "BillboardComponent.h"
#include "AABB.h"
#include "JsonSerializer.h"
#include "World.h"
#include "PrimitiveComponent.h"
#include "GameObject.h"

	/*BEGIN_PROPERTIES(AActor)
	ADD_PROPERTY(FName, ObjectName, "[액터]", true, "액터의 이름입니다")
	ADD_PROPERTY(bool, bActorIsActive, "[액터]", true, "액터를 활성 여부를 설정합니다")
	ADD_PROPERTY(bool, bActorHiddenInGame, "[액터]", true, "액터를 게임에서 숨깁니다")
	ADD_PROPERTY(FString, Tag, "[액터]", true, "액터의 태그를 지정합니다.")
END_PROPERTIES()*/

AActor::AActor()
{
	ObjectName = "DefaultActor";
	CustomTimeDillation = 1.0f;
}

AActor::~AActor()
{
	DestroyAllComponents();
	
	if (LuaGameObject)
	{
		delete LuaGameObject;
		LuaGameObject = nullptr;
	}
}

void AActor::BeginPlay()
{
	// Lua Game Object 초기화
	if (!LuaGameObject)
	{
		LuaGameObject = new FGameObject();
		LuaGameObject ->SetOwner(this); /*순서 보장 필수!*/
		LuaGameObject->UUID = this->UUID;
	}
	
	// NOTE: 아직 InitializeComponent/BeginPlay 순서가 완벽히 보장되지 않음 (PIE 시작 순간에는 지연 생성 처리 필요)
	// 컴포넌트들 Initialize/BeginPlay 순회
	for (UActorComponent* Comp : OwnedComponents)
	{
		if (Comp)
		{
			Comp->InitializeComponent();
		}
	}
	for (UActorComponent* Comp : OwnedComponents)
	{
		if (Comp)
		{
			Comp->BeginPlay();
		}
	}
}

void AActor::Tick(float DeltaSeconds)
{
	bool bIsEditor = World->bPie == false;
	for (UActorComponent* Comp : OwnedComponents)
	{
		if (!Comp || !Comp->IsComponentTickEnabled()) continue;

		if (bIsEditor)
		{
			// 컴포넌트 에디터 틱 껐으면 스킵
			if (!Comp->bTickInEditor)
			{
				continue; 
			}
		}

		// 통과했으면 틱 실행
		Comp->TickComponent(DeltaSeconds);
	}
}

void AActor::EndPlay()
{
	for (UActorComponent* Comp : OwnedComponents)
	{
		if (Comp)
		{
			Comp->EndPlay();
		}
	}

	if (LuaGameObject)
	{
		delete LuaGameObject;
		LuaGameObject = nullptr;
	}
}

// 지연 삭제 (이후 월드에서 Tick이 끝나면 실제로 삭제됨)
void AActor::Destroy()
{
	// 재진입/중복 방지
	if (IsPendingDestroy())
	{
		return;
	}

	MarkPendingDestroy();

	World->AddPendingKillActor(this);
}

void AActor::SetRootComponent(USceneComponent* InRoot)
{
	if (RootComponent == InRoot)
	{
		return;
	}

	// 루트 교체
	USceneComponent* TempRootComponent = RootComponent;
	RootComponent = InRoot;

	if (RootComponent)
	{
		RootComponent->SetOwner(this);
	}
}

UActorComponent* AActor::AddNewComponent(UClass* ComponentClass, USceneComponent* ParentToAttach)
{
	// 1. 유효성 검사
	if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return nullptr;
	}

	// 2. 생성
	UActorComponent* NewComp = Cast<UActorComponent>(ObjectFactory::NewObject(ComponentClass));
	if (!NewComp)
	{
		return nullptr;
	}

	// 3. 소유 목록에 추가
	AddOwnedComponent(NewComp);

	// 4. 부착
	if (USceneComponent* SceneComp = Cast<USceneComponent>(NewComp))
	{
		// 4-1. 부모 결정
		USceneComponent* TargetParent = ParentToAttach;

		// 4-2. 부모가 명시적으로 지정되지 않았다면, 액터의 루트 컴포넌트를 부모로 사용
		if (!TargetParent)
		{
			TargetParent = GetRootComponent();
		}

		// 4-3. 부모가 유효하고, 그 부모가 '자기 자신'이 아닌 경우에만 부착
		// (AddOwnedComponent에 의해 SceneComp가 방금 RootComponent가 되었을 수 있으므로)
		if (TargetParent && TargetParent != SceneComp)
		{
			SceneComp->SetupAttachment(TargetParent, EAttachmentRule::KeepRelative);
		}
	}

	// 5. 등록 및 초기화
	UWorld* World = GetWorld();
	if (World)
	{
		// 5-1. 등록 (OnRegister 호출)
		NewComp->RegisterComponent(World);

		// 5-2. 월드가 PIE/게임 상태라면, 즉시 초기화/시작
		if (World->bPie)
		{
			NewComp->InitializeComponent();
			NewComp->BeginPlay();
		}
	}

	return NewComp;
}

void AActor::AddOwnedComponent(UActorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (OwnedComponents.count(Component))
	{
		return;
	}

	OwnedComponents.insert(Component);
	Component->SetOwner(this);
	if (USceneComponent* SC = Cast<USceneComponent>(Component))
	{
		SceneComponents.AddUnique(SC);
		// 루트가 없으면 자동 루트 지정
		if (!RootComponent)
		{
			SetRootComponent(SC);
		}
	}
}

// 소유 중인 Component 개별 삭제
void AActor::RemoveOwnedComponent(UActorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (!OwnedComponents.count(Component) || RootComponent == Component)
	{
		return;
	}

	if (GetWorld()->bPie)
	{
		Component->EndPlay();
	}

	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		// 자식 컴포넌트들을 먼저 재귀적으로 삭제
		// (자식을 먼저 삭제하면 부모의 AttachChildren이 변경되므로 복사본으로 순회)
		TArray<USceneComponent*> ChildrenCopy = SceneComponent->GetAttachChildren();
		for (USceneComponent* Child : ChildrenCopy)
		{
			RemoveOwnedComponent(Child); // 재귀 호출로 자식들 먼저 삭제
		}

		if (SceneComponent == RootComponent)
		{
			RootComponent = nullptr;
		}

		SceneComponents.Remove(SceneComponent);
		SceneComponent->DetachFromParent(true);
	}

	// OwnedComponents에서 제거
	OwnedComponents.erase(Component);

	Component->DestroyComponent();
}

// Actor의 첫번째 Component 찾아 반환
UActorComponent* AActor::GetComponent(UClass* ComponentClass)
{
	if (!ComponentClass) return nullptr;
	for (UActorComponent* Comp : OwnedComponents)
	{
		if (!Comp) continue;
		if (Comp->IsA(ComponentClass)) return Comp;
	}
	return nullptr;
}

void AActor::RegisterAllComponents(UWorld* InWorld)
{
	// 액터 생성 후 아무 컴포넌트가 없으면 강제로 빈 루트 컴포넌트 할당
	if (!RootComponent)
	{
		RootComponent = CreateDefaultSubobject<USceneComponent>("DefaultSceneComponent");
	}

	for (UActorComponent* Component : OwnedComponents)
	{
		Component->RegisterComponent(InWorld);
	}
}

// 소유 중인 Component 전체 삭제
void AActor::DestroyAllComponents()
{
	TArray<UActorComponent*> Temp;
	Temp.reserve(OwnedComponents.size());

	for (UActorComponent* C : OwnedComponents) 
		Temp.push_back(C);

	for (UActorComponent* C : Temp)
	{
		if (!C) 
			continue;
		C->DestroyComponent(); // 내부에서 등록 해제도 처리
	}
	OwnedComponents.Empty();

	SceneComponents.Empty();
	RootComponent = nullptr;
}

// ───────────────
// Transform API
// ───────────────
void AActor::SetActorTransform(const FTransform& NewTransform)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	FTransform OldTransform = RootComponent->GetWorldTransform();
	if (!(OldTransform == NewTransform))
	{
		RootComponent->SetWorldTransform(NewTransform);
		MarkPartitionDirty();
	}
}

FTransform AActor::GetActorTransform() const
{
	return RootComponent ? RootComponent->GetWorldTransform() : FTransform();
}

void AActor::SetActorLocation(const FVector& NewLocation)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	FVector OldLocation = RootComponent->GetWorldLocation();
	if (!(OldLocation == NewLocation)) // 위치가 실제로 변경되었을 때만
	{
		RootComponent->SetWorldLocation(NewLocation);
		MarkPartitionDirty();
	}
} 

FVector AActor::GetActorLocation() const
{
	if (!RootComponent)
		return FVector();
	FVector Lo = RootComponent->GetWorldLocation();
	return Lo;
	// return RootComponent ? RootComponent->GetWorldLocation() : FVector();
}

void AActor::MarkPartitionDirty()
{
	if(GetWorld() && GetWorld()->GetPartitionManager())
		GetWorld()->GetPartitionManager()->MarkDirty(this);
}

float AActor::GetCustomTimeDillation()
{
	if (!World) return 0.0f;

	TWeakObjectPtr<AActor> Key(this);
	FActorTimeState* TimeState = World->ActorTimingMap.Find(Key);

	if (TimeState == nullptr) return 1.0f;

	return CustomTimeDillation; 
}

void AActor::SetCustomTimeDillation(float Duration , float Dilation)
{
    // Dilation 더 큰걸 반영
	CustomTimeDillation = FMath::Min( CustomTimeDillation, Dilation);

    if (!World) return;
	
	// World에서 Duration을 관리하고 있으므로 업데이트
    TWeakObjectPtr<AActor> Key(this);
    FActorTimeState* TimeState = World->ActorTimingMap.Find(Key);

    if (TimeState)
    {
        TimeState->Dilation = Dilation;
        TimeState->Durtaion = Duration;
    }
    else
    {
        FActorTimeState NewState{};
        NewState.Dilation = Dilation;
        NewState.Durtaion = Duration;
        World->ActorTimingMap.Add(Key, NewState);
    }
}

void AActor::SetActorRotation(const FVector& EulerDegree)
{
	if (RootComponent)
	{
		FQuat NewRotation = FQuat::MakeFromEulerZYX(EulerDegree);
		FQuat OldRotation = RootComponent->GetWorldRotation();
		if (!(OldRotation == NewRotation)) // 회전이 실제로 변경되었을 때만
		{
			RootComponent->SetWorldRotation(NewRotation);
			MarkPartitionDirty();
		}
	}
}

void AActor::SetActorRotation(const FQuat& InQuat)
{
	if (RootComponent == nullptr)
	{
		return;
	}
	FQuat OldRotation = RootComponent->GetWorldRotation();
	if (!(OldRotation == InQuat)) // 회전이 실제로 변경되었을 때만
	{
		RootComponent->SetWorldRotation(InQuat);
		MarkPartitionDirty();
	}
}

FQuat AActor::GetActorRotation() const
{
	return RootComponent ? RootComponent->GetWorldRotation() : FQuat();
}

void AActor::SetActorScale(const FVector& NewScale)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	FVector OldScale = RootComponent->GetWorldScale();
	if (!(OldScale == NewScale)) // 스케일이 실제로 변경되었을 때만
	{
		RootComponent->SetWorldScale(NewScale);
		MarkPartitionDirty();
	}
}

FVector AActor::GetActorScale() const
{
	return RootComponent ? RootComponent->GetWorldScale() : FVector(1, 1, 1);
}

void AActor::SetActorIsVisible(bool bIsActive)
{
	//TODO: Primitibe의 bGenerateOverlapEvents 끄기
	for (UActorComponent* Comp : OwnedComponents)
	{
		if (auto* Prim = Cast<UPrimitiveComponent>(Comp))
		{
			Prim->SetGenerateOverlapEvents(false);
		}
	}
	return RootComponent->SetVisibility(bIsActive);
}

bool AActor::GetActorIsVisible()
{
	return RootComponent->IsVisible();
}

FMatrix AActor::GetWorldMatrix() const
{
	if (RootComponent == nullptr)
	{
		UE_LOG("RootComponent is nullptr");
	}
	return RootComponent ? RootComponent->GetWorldMatrix() : FMatrix::Identity();
}

void AActor::AddActorWorldRotation(const FQuat& DeltaRotation)
{
	if (RootComponent && !DeltaRotation.IsIdentity()) // 단위 쿼터니온이 아닐 때만
	{
		RootComponent->AddWorldRotation(DeltaRotation);
		MarkPartitionDirty();
	}
}

void AActor::AddActorWorldRotation(const FVector& DeltaEuler)
{
	/* if (RootComponent)
	 {
		 FQuat DeltaQuat = FQuat::FromEuler(DeltaEuler.X, DeltaEuler.Y, DeltaEuler.Z);
		 RootComponent->AddWorldRotation(DeltaQuat);
	 }*/
}

void AActor::AddActorWorldLocation(const FVector& DeltaLocation)
{
	if (RootComponent && !DeltaLocation.IsZero()) // 영 벡터가 아닐 때만
	{
		RootComponent->AddWorldOffset(DeltaLocation);
		MarkPartitionDirty();
	}
}

void AActor::AddActorLocalRotation(const FVector& DeltaEuler)
{
	/*  if (RootComponent)
	  {
		  FQuat DeltaQuat = FQuat::FromEuler(DeltaEuler.X, DeltaEuler.Y, DeltaEuler.Z);
		  RootComponent->AddLocalRotation(DeltaQuat);
	  }*/
}

void AActor::AddActorLocalRotation(const FQuat& DeltaRotation)
{
	if (RootComponent && !DeltaRotation.IsIdentity()) // 단위 쿼터니온이 아닐 때만
	{
		RootComponent->AddLocalRotation(DeltaRotation);
		MarkPartitionDirty();
	}
}

void AActor::AddActorLocalLocation(const FVector& DeltaLocation)
{
	if (RootComponent && !DeltaLocation.IsZero()) // 영 벡터가 아닐 때만
	{
		RootComponent->AddLocalOffset(DeltaLocation);
		MarkPartitionDirty();
	}
}

void AActor::SetActorHiddenInEditor(bool bNewHidden)
{
	bHiddenInEditor = bNewHidden; 
	GWorld->GetLightManager()->SetDirtyFlag();
}

bool AActor::IsActorVisible() const
{
	return GWorld->bPie ? !bActorHiddenInGame : !bHiddenInEditor;
}

void AActor::PostDuplicate()
{
	Super::PostDuplicate();

}

bool AActor::IsOverlappingActor(const AActor* Other) const
{
    if (!Other)
    {
        return false;
    }

    for (UActorComponent* OwnedComp : OwnedComponents)
    {
        if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(OwnedComp))
        {
            if (PrimComp->GetOverlapInfos().Num() > 0 && PrimComp->IsOverlappingActor(Other))
            {
                return true;
            }
        }
    }
    return false;
}

void AActor::OnBeginOverlap(UPrimitiveComponent* MyComp, UPrimitiveComponent* OtherComp)
{
	if (OtherComp->GetOwner()->Tag == "player")
		UE_LOG("On Begin Overlap");
	
}

void AActor::OnEndOverlap(UPrimitiveComponent* MyComp, UPrimitiveComponent* OtherComp)
{
	if (OtherComp->GetOwner()->Tag == "player")
		UE_LOG("On End Overlap");
}

void AActor::OnHit(UPrimitiveComponent* MyComp, UPrimitiveComponent* OtherComp)
{
	UE_LOG("On Hit");
} 

void AActor::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();

	LuaGameObject = new FGameObject();
	LuaGameObject ->SetOwner(this); /*순서 보장 필수!*/
	LuaGameObject->UUID = this->UUID;
	
	// 기본 프로퍼티 초기화
	bIsPicked = false;
	bIsCulled = false;
	World = nullptr; // PIE World는 복제 프로세스의 상위 레벨에서 설정해 주어야 합니다.

	if (OwnedComponents.IsEmpty())
	{
		return; // 복제할 컴포넌트가 없으면 종료
	}

	// ========================================================================
	// 1단계: 모든 컴포넌트 복제 및 '원본 -> 사본' 매핑 테이블 생성
	// ========================================================================
	TMap<UActorComponent*, UActorComponent*> OldToNewComponentMap;
	TSet<UActorComponent*> NewOwnedComponents;

	for (UActorComponent* OriginalComp : OwnedComponents)
	{
		if (!OriginalComp) continue;

		// NOTE: 이 코드가 없으면 Direction을 나타내는 GizmoComponent가 PIE World에 복사되는 버그가 발생
		// PIE 모드로 복사할 때, 에디터 전용 컴포넌트(bIsEditable == false)는 복사하지 않음
		// BillboardComponent, DirectionGizmo(GizmoArrowComponent) 등이 CREATE_EDITOR_COMPONENT로 생성되며 bIsEditable = false
		if (!OriginalComp->IsEditable())
		{
			continue; // 에디터 전용 컴포넌트는 PIE World로 복사하지 않음
		}
		// 컴포넌트를 깊은 복사합니다.
		UActorComponent* NewComp = OriginalComp->Duplicate();
		NewComp->SetOwner(this);
		
		// 매핑 테이블에 (원본 포인터, 새 포인터) 쌍을 기록합니다.
		OldToNewComponentMap.insert({ OriginalComp, NewComp });

		// 새로운 소유 컴포넌트 목록에 추가합니다.
		NewOwnedComponents.insert(NewComp);
	}

	// 복제된 컴포넌트 목록으로 교체합니다.
	OwnedComponents = NewOwnedComponents;

	// ========================================================================
	// 2단계: 매핑 테이블을 이용해 씬 계층 구조 재구성
	// ========================================================================

	// 2-1. 새로운 루트 컴포넌트 설정
	UActorComponent** FoundNewRootPtr = OldToNewComponentMap.Find(RootComponent);
	if (FoundNewRootPtr)
	{
		RootComponent = Cast<USceneComponent>(*FoundNewRootPtr);
	}
	else
	{
		// 원본 루트를 찾지 못하는 심각한 오류. 
		// 이 경우엔 첫 번째 씬 컴포넌트를 임시 루트로 삼거나 에러 처리.
		RootComponent = nullptr;
	}

	// 2-2. 모든 씬 컴포넌트의 부모-자식 관계 재연결
	SceneComponents.Empty(); // 새 컴포넌트로 목록을 다시 채웁니다.
	if (RootComponent)
	{
		SceneComponents.push_back(RootComponent);
	}

	for (auto const& [OriginalComp, NewComp] : OldToNewComponentMap)
	{
		USceneComponent* OriginalSceneComp = Cast<USceneComponent>(OriginalComp);
		USceneComponent* NewSceneComp = Cast<USceneComponent>(NewComp);

		if (OriginalSceneComp && NewSceneComp)
		{
			// 루트가 아닌 경우에만 부모를 찾아 연결합니다.
			if (OriginalSceneComp != GetRootComponent()) // 여기서 비교는 원본 액터의 루트와 해야 합니다.
			{
				USceneComponent* OriginalParent = OriginalSceneComp->GetAttachParent();
				if (OriginalParent)
				{
					// 매핑 테이블에서 원본 부모에 해당하는 '새로운 부모'를 찾습니다.
					UActorComponent** FoundNewParentPtr = OldToNewComponentMap.Find(OriginalParent);
					if (FoundNewParentPtr)
					{
						if (USceneComponent* ParentSceneComponent = Cast<USceneComponent>(*FoundNewParentPtr))
						{
							NewSceneComp->SetupAttachment(ParentSceneComponent, EAttachmentRule::KeepRelative);
						}
					}
				}
			}

			// 루트가 아닌 씬 컴포넌트들을 캐시 목록에 추가합니다.
			if (NewSceneComp != RootComponent)
			{
				SceneComponents.push_back(NewSceneComp);
			}
		}
	}
}

void AActor::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

	if (bInIsLoading)
	{
		// 액터 생성자에서 만들어진 컴포넌트를 무시하고 저장된 컴포넌트만 다시 붙인다
		DestroyAllComponents();

		uint32 RootUUID;
		FJsonSerializer::ReadUint32(InOutHandle, "RootComponentId", RootUUID);
	
		JSON ComponentsJson;
		if (FJsonSerializer::ReadArray(InOutHandle, "OwnedComponents", ComponentsJson))
		{
			// 1) OwnedComponents와 SceneComponents에 Component들 추가
			for (uint32 i = 0; i < static_cast<uint32>(ComponentsJson.size()); ++i)
			{
				JSON ComponentJson = ComponentsJson.at(i);
				
				FString TypeString;
				FJsonSerializer::ReadString(ComponentJson, "Type", TypeString);
	
				UClass* NewClass = UClass::FindClass(TypeString);
	
				UActorComponent* NewComponent = Cast<UActorComponent>(ObjectFactory::NewObject(NewClass));
	
				NewComponent->Serialize(bInIsLoading, ComponentJson);
	
				// RootComponent 설정
				if (USceneComponent* NewSceneComponent = Cast<USceneComponent>(NewComponent))
				{
					if (RootUUID == NewSceneComponent->GetSceneId())
					{
						assert(NewSceneComponent);
						SetRootComponent(NewSceneComponent);
					}
				}
	
				// OwnedComponents와 SceneComponents에 Component 추가
				AddOwnedComponent(NewComponent);
			}
	
			// 2) 컴포넌트 간 부모 자식 관계 설정
			for (auto& Component : OwnedComponents)
			{
				USceneComponent* SceneComp = Cast<USceneComponent>(Component);
				if (!SceneComp)
				{
					continue;
				}
				uint32 ParentId = SceneComp->GetParentId();
				if (ParentId != 0) // RootComponent가 아니면 부모 설정
				{
					USceneComponent** ParentP = SceneComp->GetSceneIdMap().Find(ParentId);
					USceneComponent* Parent = *ParentP;
	
					SceneComp->SetupAttachment(Parent, EAttachmentRule::KeepRelative);
				}
			}
		}
	}
	else if (RootComponent)
	{
		InOutHandle["RootComponentId"] = RootComponent->UUID;
	
		JSON Components = JSON::Make(JSON::Class::Array);
		for (auto& Component : OwnedComponents)
		{
			// 에디터 전용 컴포넌트는 직렬화하지 않음
			// (CREATE_EDITOR_COMPONENT로 생성된 컴포넌트들은 OnRegister()에서 매번 새로 생성됨)
			if (!Component->IsEditable())
			{
				continue;
			}
	
			JSON ComponentJson;
	
			ComponentJson["Type"] = Component->GetClass()->Name;
	
			Component->Serialize(bInIsLoading, ComponentJson);
			Components.append(ComponentJson);
		}
		InOutHandle["OwnedComponents"] = Components;
	}
}

void AActor::RegisterComponentTree(USceneComponent* SceneComp, UWorld* InWorld)
{
	if (!SceneComp)
	{
		return;
	}
	// 씬 그래프 등록(월드에 등록/렌더 시스템 캐시 등)
	SceneComp->RegisterComponent(InWorld);
	// 자식들도 재귀적으로
	for (USceneComponent* Child : SceneComp->GetAttachChildren())
	{
		RegisterComponentTree(Child, InWorld);
	}
}

void AActor::UnregisterComponentTree(USceneComponent* SceneComp)
{
	if (!SceneComp)
	{
		return;
	}
	for (USceneComponent* Child : SceneComp->GetAttachChildren())
	{
		UnregisterComponentTree(Child);
	}
	SceneComp->UnregisterComponent();
}
