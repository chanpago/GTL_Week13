#include "pch.h"
#include "SelectionManager.h"
#include "Picking.h"
#include "CameraActor.h"
#include "StaticMeshActor.h"
#include "CameraComponent.h"
#include "ObjectFactory.h"
#include "TextRenderComponent.h"
#include "FViewport.h"
#include "Windows/SViewportWindow.h"
#include "USlateManager.h"
#include "StaticMesh.h"
#include "ObjManager.h"
#include "WorldPartitionManager.h"
#include "PrimitiveComponent.h"
#include "Octree.h"
#include "BVHierarchy.h"
#include "Frustum.h"
#include "Occlusion.h"
#include "Gizmo/GizmoActor.h"
#include "Grid/GridActor.h"
#include "StaticMeshComponent.h"
#include "DirectionalLightActor.h"
#include "DirectionalLightComponent.h"
#include "AmbientLightActor.h"
#include "AmbientLightComponent.h"
#include "Frustum.h"
#include "Level.h"
#include "LightManager.h"
#include "LuaManager.h"
#include "ShapeComponent.h"
#include "PlayerCameraManager.h"
#include "Hash.h"

IMPLEMENT_CLASS(UWorld)

UWorld::UWorld() : Partition(nullptr)  // Will be created in Initialize() based on world type
{
	SelectionMgr = std::make_unique<USelectionManager>();
	//PIE의 경우 Initalize 없이 빈 Level 생성만 해야함
	Level = std::make_unique<ULevel>();
	LightManager = std::make_unique<FLightManager>();
	LightManager->SetOwningWorld(this);  // Set owning world for optimization decisions
	LuaManager = std::make_unique<FLuaManager>();

	UnscaledDelta = 0;
	SlomoOnlyDelta = 0;
	GameDelta = 0;

	TimeStopDilation = 1.0;
	TimeDilation = 1.0;
	
	TimeStopDuration = 0;
	TimeDuration = 0;
}

UWorld::~UWorld()
{
	bIsTearingDown = true;	// 월드 삭제 중에는 새로운 액터 생성을 방지하기 위해

	if (Level)
	{
		// 모든 액터가 살아있을 때 EndPlay를 먼저 호출 후 삭제 진행
		for (AActor* Actor : Level->GetActors())
		{
			Actor->EndPlay();
		}
	}

	// 물리 씬 정리 - Actor 삭제 전에 수행해야 함
	// fetchResults() 호출 시 콜백에서 Actor/Component를 참조하므로
	// Actor가 살아있는 상태에서 물리 씬을 먼저 정리해야 dangling pointer 방지
	if (PhysScene)
	{
		PhysScene->Shutdown();
		PhysScene.reset();
	}

	if (Level)
	{
		TArray<AActor*> TempActors =  Level->GetActors();
		for (AActor* Actor : TempActors)
		{
			DestroyActor(Actor);
		}
		Level->Clear();
	}

	TArray<AActor*> TempEditorActors = EditorActors;
	for (AActor* Actor : TempEditorActors)
	{
		DestroyActor(Actor);
	}
	EditorActors.clear();

	GridActor = nullptr;
	GizmoActor = nullptr;
}

void UWorld::Initialize()
{
	// Create partition manager first, before CreateLevel() calls SetLevel()
	// Skip for preview worlds to save ~190 MB
	if (!IsPreviewWorld())
	{
		Partition = std::make_unique<UWorldPartitionManager>();
	}

	// 에디터 월드에서는 PhysScene을 생성하지 않음
	// 물리 시뮬레이션은 PIE에서만 동작 (DuplicateWorldForPIE에서 생성)

	// 기본 씬을 생성합니다.
	CreateLevel();

	// 에디터 전용 액터들을 초기화합니다.
	InitializeGrid();
	InitializeGizmo();
}

void UWorld::InitializeGrid()
{
	GridActor = NewObject<AGridActor>();
	GridActor->SetWorld(this);
	GridActor->RegisterAllComponents(this);
	GridActor->Initialize();

	EditorActors.push_back(GridActor);
}

void UWorld::InitializeGizmo()
{
	GizmoActor = NewObject<AGizmoActor>();
	GizmoActor->SetWorld(this);
	GizmoActor->RegisterAllComponents(this);
	GizmoActor->SetActorTransform(FTransform(
		FVector{ 0, 0, 0 }, 
		FQuat::MakeFromEulerZYX(FVector{ 0, -90, 0 }),
		FVector{ 1, 1, 1 }));

	EditorActors.push_back(GizmoActor);
}

bool UWorld::TryLoadLastUsedLevel()
{
	if (!EditorINI.Contains("LastUsedLevel"))
	{
		return false;
	}

	FWideString LastUsedLevelPath = UTF8ToWide(EditorINI["LastUsedLevel"]);
	
	// 로드 직전: Transform 위젯/선택 초기화
	UUIManager::GetInstance().ClearTransformWidgetSelection();
	GWorld->GetSelectionManager()->ClearSelection();

	std::unique_ptr<ULevel> NewLevel = ULevelService::CreateDefaultLevel();
	JSON LevelJsonData;
	if (FJsonSerializer::LoadJsonFromFile(LevelJsonData, LastUsedLevelPath))
	{
		NewLevel->Serialize(true, LevelJsonData);
	}
	else
	{
		UE_LOG("[error] MainToolbar: Failed To Load Level From: %s", LastUsedLevelPath.c_str());
		return false;
	}

	SetLevel(std::move(NewLevel));

	UE_LOG("MainToolbar: Scene loaded successfully: %s", LastUsedLevelPath.c_str());
	return true;
}

bool UWorld::LoadLevelFromFile(const FWideString& Path)
{
	std::unique_ptr<ULevel> NewLevel = ULevelService::CreateDefaultLevel();
	JSON LevelJsonData;

	if (FJsonSerializer::LoadJsonFromFile(LevelJsonData, Path))
	{
		NewLevel->Serialize(true, LevelJsonData);
	}
	else
	{
		UE_LOG("[error] MainToolbar: Failed To Load Level From: %s", Path.c_str());
		return false;
	}

	SetLevel(std::move(NewLevel));

	UE_LOG("UWorld: Scene loaded successfully: %s", Path.c_str());
	return true;
}

// 함수 내부 코드 순서 유지 필요
void UWorld::Tick(float DeltaSeconds)
{	
	// GameDelat: Unscaled * finalScale  
	float UnscaledDeltaSeconds = DeltaSeconds;

	// Time Stop 
	if (TimeStopDuration > 0 || TimeDuration > 0)
	{
		// Stop
		TimeStopDuration -= UnscaledDeltaSeconds; 
		if (TimeStopDuration <= 0)
		{
			TimeStopDuration = 0;
			TimeStopDilation = 1.0f;
		}

		// Slomo
		TimeDuration -= UnscaledDeltaSeconds; 

		if (TimeDuration <= 0)
		{
			TimeDuration = 0;
			TimeDilation = 1.0f;
		} 
	}  

    // Delta Time Update
    UnscaledDelta = UnscaledDeltaSeconds;
    SlomoOnlyDelta = UnscaledDeltaSeconds * TimeDilation;
    GameDelta = UnscaledDeltaSeconds * TimeDilation * TimeStopDilation;

	// Actor 별로 Dilation의 Duration을 처리하는 부분
	if (!ActorTimingMap.IsEmpty())
	{
		TArray<TWeakObjectPtr<AActor>> ToRemove;

		for (auto& Pair : ActorTimingMap)
		{
			TWeakObjectPtr<AActor> Key = Pair.first;
			FActorTimeState& State = Pair.second;

			State.Durtaion -= GetDeltaTime(EDeltaTime::Unscaled);
		
			if (State.Durtaion <= 0.0f || !Key.IsValid())
			{
				/*if (AActor * Actor =  Key.Get())
				{
					Actor->SetCustomTimeDillation(0.0, 1.0f);
				}*/
				ToRemove.Add(Key);
			}
		} 
		
        for (auto& Key : ToRemove)
        {
            ActorTimingMap.Remove(Key);
        }
	} 
	 
	// 중복충돌 방지 pair clear
    FrameOverlapPairs.clear();

    // Skip partition update for preview worlds (no spatial partitioning needed)
    if (Partition)
    {
        Partition->Update(DeltaSeconds, /*budget*/256);
    }

	// 물리 결과 수확 (PIE에서만) - Actor Tick 전에 수행
	// 이전 프레임에서 시작한 물리 계산 결과를 가져옴 (1프레임 지연)
	if (PhysScene && bPie)
	{
		PhysScene->WaitForSimulation();
	}

	if (Level)
	{
		// Tick 중에 새로운 actor가 추가될 수도 있어서 복사 후 호출
		TArray<AActor*> LevelActors = Level->GetActors();
		for (AActor* Actor : LevelActors)
		{
			if (Actor && Actor->IsActorActive())
			{
				if (Actor->CanEverTick())
				{
					Actor->Tick(GetDeltaTime(EDeltaTime::Game) * Actor->GetCustomTimeDillation());
				}
			}
		}
    }

    for (AActor* EditorActor : EditorActors)
    {
		if (EditorActor && !bPie)
		{
			EditorActor->Tick(GetDeltaTime(EDeltaTime::Unscaled));
		}
    }

	// Lua 코루틴 전용 Tick
	if (LuaManager && bPie)
	{
		LuaManager->Tick(GetDeltaTime(EDeltaTime::Game));
	}

	// 물리 시뮬레이션 시작 (PIE에서만) - non-blocking
	// 이번 프레임 물리 계산 시작, 결과는 다음 프레임에서 사용
	if (PhysScene && bPie)
	{
		PhysScene->StepSimulation(GetDeltaTime(EDeltaTime::Game));
	}

	// 지연 삭제 처리
	ProcessPendingKillActors();
}

UWorld* UWorld::DuplicateWorldForPIE(UWorld* InEditorWorld)
{
	// 레벨 새로 생성
	// 월드 카피 및 월드에 이 새로운 레벨 할당
	// 월드 컨텍스트 새로 생성(월드타입, 카피한 월드)
	// 월드의 레벨에 원래 Actor들 다 복사
	// 해당 월드의 Initialize 호출?

	//ULevel* NewLevel = ULevelService::CreateNewLevel();
	UWorld* PIEWorld = NewObject<UWorld>(); // 레벨도 새로 생성됨
	PIEWorld->Partition = std::make_unique<UWorldPartitionManager>();

	// 물리 씬 초기화
	PIEWorld->PhysScene = std::make_unique<FPhysScene>();
	PIEWorld->PhysScene->Initialize();

	PIEWorld->bPie = true;
	
	FWorldContext PIEWorldContext = FWorldContext(PIEWorld, EWorldType::Game);
	GEngine.AddWorldContext(PIEWorldContext);
	
	const TArray<AActor*>& SourceActors = InEditorWorld->GetLevel()->GetActors();
	for (AActor* SourceActor : SourceActors)
	{
		if (!SourceActor)
		{
			UE_LOG("Duplicate failed: SourceActor is nullptr");
			continue;
		}

		AActor* NewActor = SourceActor->Duplicate();

		if (!NewActor)
		{
			UE_LOG("Duplicate failed: NewActor is nullptr");
			continue;
		}

		NewActor->SetWorld(PIEWorld);
		NewActor->RegisterAllComponents(PIEWorld);
		// PlayerCameraManager 복사
		if (InEditorWorld->PlayerCameraManager == SourceActor)
		{
			if (APlayerCameraManager* NewPlayerCameraManager = Cast<APlayerCameraManager>(NewActor))
			{
				PIEWorld->PlayerCameraManager = NewPlayerCameraManager;
			}
		}

		PIEWorld->AddActorToLevel(NewActor);
	}

	PIEWorld->RenderSettings = InEditorWorld->RenderSettings;

	return PIEWorld;
}

float UWorld::GetDeltaTime(EDeltaTime type)
{
	switch (type)
	{
	case EDeltaTime::Unscaled:
		return UnscaledDelta;
		break;
	case EDeltaTime::SlomoOnly:
		return SlomoOnlyDelta;
		break;

	case EDeltaTime::Game:
		return GameDelta;
		break;

	default:
		return UnscaledDelta;
		break;
	}
}

void UWorld::RequestHitStop(float Duration, float Dilation)
{
	// Add를 할까, 덮어쓸까?
	TimeStopDilation = FMath::Min(Dilation, TimeStopDilation);
	TimeStopDuration = FMath::Max(Duration, TimeStopDuration);
}

void UWorld::RequestSlomo(float Duration, float Dilation)
{
	TimeDilation = FMath::Min(Dilation, TimeDilation);
	TimeDuration = FMath::Max(Duration, TimeDuration);
}

class UCameraComponent* UWorld::GetWorldCamera() 
{
	if (bPie)
	{
		return GetPlayerCameraManager()->GetViewCamera();
	}
	return GetEditorCameraActor()->GetCameraComponent();
}

void UWorld::SetEditorCameraActor(ACameraActor* InCamera)
{
	MainEditorCameraActor = InCamera;

	MainEditorCameraActor->SetWorld(this);

	//기즈모 카메라 설정
	if (GizmoActor)
		GizmoActor->SetEditorCameraActor(MainEditorCameraActor);
}

FString UWorld::GenerateUniqueActorName(const FString& ActorType)
{
	// GetInstance current count for this type
	int32& CurrentCount = ObjectTypeCounts[ActorType];
	FString UniqueName = ActorType + "_" + std::to_string(CurrentCount);
	CurrentCount++;
	return UniqueName;
}

// 액터 즉시 제거 (내부적으로만 호출)
bool UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor) return false;

	// 선택/UI 해제
	if (SelectionMgr) SelectionMgr->DeselectActor(Actor);

	// 컴포넌트 정리 (등록 해제 → 파괴)
	Actor->DestroyAllComponents();

	// 레벨에서 제거 시도
	if (Level && Level->RemoveActor(Actor))
	{
		// 메모리 해제
		ObjectFactory::DeleteObject(Actor);
		return true; // 성공적으로 삭제
	}

	return false; // 레벨에 없는 액터
}

inline FString RemoveObjExtension(const FString& FileName)
{
	const FString Extension = ".obj";

	// 마지막 경로 구분자 위치 탐색 (POSIX/Windows 모두 지원)
	const uint64 Sep = FileName.find_last_of("/\\");
	const uint64 Start = (Sep == FString::npos) ? 0 : Sep + 1;

	// 확장자 제거 위치 결정
	uint64 End = FileName.size();
	if (End >= Extension.size() &&
		FileName.compare(End - Extension.size(), Extension.size(), Extension) == 0)
	{
		End -= Extension.size();
	}

	// 베이스 이름(확장자 없는 파일명) 반환
	if (Start <= End)
		return FileName.substr(Start, End - Start);

	// 비정상 입력 시 원본 반환 (안전장치)
	return FileName;
}

void UWorld::CreateLevel()
{
	if (SelectionMgr) SelectionMgr->ClearSelection();

	// For preview worlds, create minimal level with no default actors
	// Spawn lights without shadows for proper lighting with zero shadow memory cost
	if (IsPreviewWorld())
	{
		SetLevel(ULevelService::CreateDefaultLevel());

		// Spawn directional light WITHOUT shadows for proper directional lighting (no memory cost)
		ADirectionalLightActor* DirectionalLight = SpawnActor<ADirectionalLightActor>();
		if (DirectionalLight)
		{
			DirectionalLight->GetLightComponent()->SetCastShadows(false);  // Disable shadows - saves ALL shadow memory
			DirectionalLight->SetActorRotation(FQuat::FromAxisAngle(FVector(1.f, -1.f, 1.f), 30.f));
		}

		// Add ambient light for base illumination
		AAmbientLightActor* AmbientLight = SpawnActor<AAmbientLightActor>();
		if (AmbientLight)
		{
			AmbientLight->GetLightComponent()->SetIntensity(0.2f);
		}
	}
	else
	{
		SetLevel(ULevelService::CreateNewLevel());
	}

	// 이름 카운터 초기화: 씬을 새로 시작할 때 각 BaseName 별 suffix를 0부터 다시 시작
	ObjectTypeCounts.clear();
}

//어느 레벨이든 기본적으로 존재하는 엑터(디렉셔널 라이트) 생성
void UWorld::SpawnDefaultActors()
{
	SpawnActor<ADirectionalLightActor>();
}

void UWorld::SetLevel(std::unique_ptr<ULevel> InLevel)
{
    // Make UI/selection safe before destroying previous actors
    if (SelectionMgr) SelectionMgr->ClearSelection();

	PlayerCameraManager = nullptr;

    // Cleanup current
    if (Level)
    {
        for (AActor* Actor : Level->GetActors())
        {
            ObjectFactory::DeleteObject(Actor);
        }
        Level->Clear();
    }
    // Clear spatial indices (skip if partition is null for preview worlds)
    if (Partition)
    {
        Partition->Clear();
    }

    Level = std::move(InLevel);

    // Adopt actors: set world and register
    if (Level)
    {
		// Bulk register only if partition exists
		if (Partition)
		{
			Partition->BulkRegister(Level->GetActors());
		}
        for (AActor* Actor : Level->GetActors())
        {
			if (Actor)
			{
				Actor->SetWorld(this);
				Actor->RegisterAllComponents(this);
				Actor->BeginPlay();
			}
        }
    }

	// 씬에서 PCM 검색
	this->PlayerCameraManager = FindActor<APlayerCameraManager>();

	// 씬에서 APCM을 찾지 못했다면, 비상용으로 새로 생성
	if (this->PlayerCameraManager == nullptr)
	{
		AActor* NewPlayerCameraManager = SpawnActor(APlayerCameraManager::StaticClass());
		this->PlayerCameraManager = Cast<APlayerCameraManager>(NewPlayerCameraManager);
		UE_LOG("[info] 씬에서 APlayerCameraManager를 찾지 못해, 비어있는 인스턴스를 새로 생성합니다.");
	}

    // Clean any dangling selection references just in case
    if (SelectionMgr)
		SelectionMgr->CleanupInvalidActors();
}

void UWorld::AddActorToLevel(AActor* Actor)
{
	if (Level)
	{
		Level->AddActor(Actor);

		Actor->SetWorld(this);

		Actor->RegisterAllComponents(this);
	}
}

void UWorld::AddPendingKillActor(AActor* Actor)
{
	PendingKillActors.Add(Actor);
}

void UWorld::ProcessPendingKillActors()
{
	// 1. 처리할 액터가 없으면 즉시 반환 (최적화)
	if (PendingKillActors.IsEmpty())
	{
		return;
	}

	// 2. (안전성) 파괴 목록의 '사본'을 만듭니다.
	TArray<AActor*> ActorsToKill = PendingKillActors;

	// 3. 원본 목록은 즉시 비워 다음 프레임을 준비합니다.
	PendingKillActors.Empty();

	// 4. '사본'을 순회하며 실제 파괴를 수행합니다.
	for (AActor* Actor : ActorsToKill)
	{
		// 게임 수명 종료
		if (bPie)
		{
			Actor->EndPlay();
		}

		DestroyActor(Actor);
	}
}

AActor* UWorld::SpawnActor(UClass* Class)
{
	// 기본 Transform(원점)으로 스폰하는 메인 함수를 호출합니다.
	return SpawnActor(Class, FTransform());
}

AActor* UWorld::FindActorByName(const FName& ActorName)
{
	if (!Level)
	{
		return nullptr;
	}

	for (AActor* Actor : Level->GetActors())
	{
		if (Actor && !Actor->IsPendingDestroy() && Actor->ObjectName == ActorName)
		{
			return Actor; // 첫 번째 일치하는 액터 반환
		}
	}

	return nullptr; // 찾지 못함
}

UActorComponent* UWorld::FindComponentByName(const FName& ComponentName)
{
	if (!Level)
	{
		return nullptr;
	}

	for (AActor* Actor : Level->GetActors())
	{
		if (Actor && !Actor->IsPendingDestroy())
		{
			for (UActorComponent* Component : Actor->GetOwnedComponents())
			{
				if (Component && !Component->IsPendingDestroy() && Component->ObjectName == ComponentName)
				{
					return Component; // 첫 번째 일치하는 컴포넌트 반환
				}
			}
		}
	}

	return nullptr; // 찾지 못함
}

AActor* UWorld::SpawnActor(UClass* Class, const FTransform& Transform)
{
	if (bIsTearingDown)
	{
		UE_LOG("[warning] 월드 삭제 시 새로운 액터를 추가할 수 없습니다.");
		return nullptr;
	}

	// 유효성 검사: Class가 유효하고 AActor를 상속했는지 확인
	if (!Class || !Class->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG("SpawnActor failed: Invalid class provided.");
		return nullptr;
	}

	// ObjectFactory를 통해 UClass*로부터 객체 인스턴스 생성
	AActor* NewActor = Cast<AActor>(ObjectFactory::NewObject(Class));
	if (!NewActor)
	{
		UE_LOG("SpawnActor failed: ObjectFactory could not create an instance of");
		return nullptr;
	}

	// 초기 트랜스폼 적용
	NewActor->SetActorTransform(Transform);

	// 현재 레벨에 액터 등록
	AddActorToLevel(NewActor);

	if (this->bPie)
	{
		NewActor->BeginPlay();
	}

	return NewActor;
}

AActor* UWorld::SpawnPrefabActor(const FWideString& PrefabPath)
{
	if (bIsTearingDown)
	{
		UE_LOG("[warning] 월드 삭제 시 새로운 액터를 추가할 수 없습니다.");
		return nullptr;
	}

	JSON ActorDataJson;

	if (FJsonSerializer::LoadJsonFromFile(ActorDataJson, PrefabPath))
	{
		// Pair.first는 ID 문자열, Pair.second는 단일 프리미티브의 JSON 데이터입니다.

		FString TypeString;
		if (FJsonSerializer::ReadString(ActorDataJson, "Type", TypeString))
		{
			//UClass* NewClass = FActorTypeMapper::TypeToActor(TypeString);
			UClass* NewClass = UClass::FindClass(TypeString);

			UWorld* World = GWorld;

			// 유효성 검사: Class가 유효하고 AActor를 상속했는지 확인
			if (!NewClass || !NewClass->IsChildOf(AActor::StaticClass()))
			{
				UE_LOG("[error] SpawnActor failed: Invalid class provided.");
				return nullptr;
			}

			// ObjectFactory를 통해 UClass*로부터 객체 인스턴스 생성
			AActor* NewActor = Cast<AActor>(ObjectFactory::NewObject(NewClass));
			if (!NewActor)
			{
				UE_LOG("[error] SpawnActor failed: ObjectFactory could not create an instance of");
				return nullptr;
			}
			
			// 데이터 불러오기
			NewActor->Serialize(true, ActorDataJson);

			// 현재 레벨에 액터 등록
			AddActorToLevel(NewActor);

			if (this->bPie)
			{
				NewActor->BeginPlay();
			}

			return NewActor;
		}
	}
	else
	{
		UE_LOG("[error] 존재하지 않는 Prefab 경로입니다. - %s", WideToUTF8(PrefabPath).c_str());
	}

	return nullptr;
}

bool UWorld::TryMarkOverlapPair(const AActor* Actor, const AActor* B)
{
	if (!Actor || !B) return false;
	// Canonicalize by pointer value to ensure consistent ordering
	uintptr_t Pa = reinterpret_cast<uintptr_t>(Actor);
	uintptr_t Pb = reinterpret_cast<uintptr_t>(B);

	if (Pa > Pb)
	{
		auto Tmp = Pa;
		Pa = Pb;
		Pb = Tmp;
	}
	uint64 Key1 = (uint64)Pa;
	uint64 Key2 = (uint64)Pb;


	uint64 Key = HashCombine(Key1, Key2);

	if (FrameOverlapPairs.Contains(Key))
		return false;

	FrameOverlapPairs.Add(Key);
	return true;
}
