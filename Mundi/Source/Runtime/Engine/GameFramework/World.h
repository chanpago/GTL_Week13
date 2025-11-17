#pragma once
#include "Object.h"
#include "Enums.h"
#include "RenderSettings.h"
#include "Level.h"
#include "Gizmo/GizmoActor.h"
#include "LightManager.h"

// Forward Declarations
class UResourceManager;
class UUIManager;
class UInputManager;
class USelectionManager;
class FLuaManager;
class AActor;
class URenderer;
class ACameraActor;
class AGizmoActor;
class AGridActor;
class FViewport;
class USlateManager;
class URenderManager;
class SViewportWindow;
class UWorldPartitionManager;
class AStaticMeshActor;
class BVHierachy;
class UStaticMesh;
class FOcclusionCullingManagerCPU;
class APlayerCameraManager;
class AGameModeBase;

struct FTransform;
struct FSceneCompData;
struct Frustum;
struct FCandidateDrawable;

enum EDeltaTime { Unscaled, SlomoOnly, Game };
struct FActorTimeState
{
    float Durtaion;
    float Dilation;
};

enum class EWorldType : uint8;

class UWorld final : public UObject
{
public:
    DECLARE_CLASS(UWorld, UObject)
    UWorld();
    ~UWorld() override;

    bool bPie = false;

    // World type management
    void SetWorldType(EWorldType InWorldType) { WorldType = InWorldType; }
    EWorldType GetWorldType() const { return WorldType; }
    bool IsPreviewWorld() const { return WorldType == EWorldType::PreviewMinimal; }
public:
    /** 초기화 */
    void Initialize();
    void InitializeGrid();
    void InitializeGizmo();

    bool TryLoadLastUsedLevel();
    bool LoadLevelFromFile(const FWideString& Path);

    template<class T>
    T* SpawnActor();
    template<class T>
    T* SpawnActor(const FTransform& Transform);
    template<typename T>
    T* FindActor();
    template<typename T>
    T* FindComponent();
    template<typename T>
    TArray<T*> FindActors();
    // ObjectName으로 '첫 번째' 액터를 찾아 반환합니다.
    AActor* FindActorByName(const FName& ActorName);
    // 모든 액터의 모든 컴포넌트를 순회하여 ObjectName으로 '첫 번째' 컴포넌트를 찾아 반환합니다 (비용이 매우 크므로 매 프레임 호출을 권장하지 않습니다.)
    UActorComponent* FindComponentByName(const FName& ComponentName);

    AActor* SpawnActor(UClass* Class, const FTransform& Transform);
    AActor* SpawnActor(UClass* Class);
    AActor* SpawnPrefabActor(const FWideString& PrefabPath);
    void AddActorToLevel(AActor* Actor);

    void AddPendingKillActor(AActor* Actor);
    void ProcessPendingKillActors();

    void CreateLevel();

    void SpawnDefaultActors();

    // Level ownership API
    void SetLevel(std::unique_ptr<ULevel> InLevel);
    ULevel* GetLevel() const { return Level.get(); }
    FLightManager* GetLightManager() const { return LightManager.get(); }
    FLuaManager* GetLuaManager() const { return LuaManager.get(); }

    ACameraActor* GetEditorCameraActor() { return MainEditorCameraActor; }
    void SetEditorCameraActor(ACameraActor* InCamera);

    void SetPlayerCameraManager(APlayerCameraManager* InPlayerCameraManager) {  PlayerCameraManager = InPlayerCameraManager; };
    APlayerCameraManager* GetPlayerCameraManager() { return PlayerCameraManager; };

    // Per-world render settings
    URenderSettings& GetRenderSettings() { return RenderSettings; }
    const URenderSettings& GetRenderSettings() const { return RenderSettings; }

    // Per-world SelectionManager accessor
    USelectionManager* GetSelectionManager() { return SelectionMgr.get(); }

    /** Generate unique name for actor based on type */
    FString GenerateUniqueActorName(const FString& ActorType);

    /** === 타임 / 틱 === */
    virtual void Tick(float DeltaSeconds);
    // Overlap pair de-duplication (per-frame)
    bool TryMarkOverlapPair(const AActor* A, const AActor* B);

    TMap<TWeakObjectPtr<AActor>, FActorTimeState> ActorTimingMap;

    /** === 필요한 엑터 게터 === */
    const TArray<AActor*>& GetActors() { static TArray<AActor*> Empty; return Level ? Level->GetActors() : Empty; }
    const TArray<AActor*>& GetEditorActors() { return EditorActors; }
    AGizmoActor* GetGizmoActor() { return GizmoActor; }
    AGridActor* GetGridActor() { return GridActor; }
    UWorldPartitionManager* GetPartitionManager() { return Partition.get(); }

    // PIE용 World 생성
    static UWorld* DuplicateWorldForPIE(UWorld* InEditorWorld);

    /** Timing Function */
    float GetDeltaTime(EDeltaTime type);

    // 모든게 정지
    void RequestHitStop(float Duration ,float Dilation = 0.0f); 
    void RequestSlomo(float Duration, float Dilation = 0.0f);

    // GameMode 
    void SetGameMode(AGameModeBase* InGameMode) { GameMode = InGameMode; }
    AGameModeBase* GetGameMode() { return GameMode; }
private:
    bool DestroyActor(AActor* Actor);   // 즉시 삭제

private:
    /** === 에디터 특수 액터 관리 === */
    TArray<AActor*> EditorActors;
    ACameraActor* MainEditorCameraActor = nullptr;  // 첫번째 뷰포트 용 에디터 카메라
    AGridActor* GridActor = nullptr;
    AGizmoActor* GizmoActor = nullptr;
    APlayerCameraManager* PlayerCameraManager;

    /** === 레벨 컨테이너 === */
    std::unique_ptr<ULevel> Level;
    TArray<AActor*> PendingKillActors;  // 지연 삭제 예정 액터 목록

    /** === 라이트 매니저 ===*/
    std::unique_ptr<FLightManager> LightManager;

    /** === 루아 매니저 ===*/
    std::unique_ptr<FLuaManager> LuaManager;
    
    /** === GameMode === */
    AGameModeBase* GameMode = nullptr;
    UClass* GameModeClass = nullptr;


    // Object naming system
    TMap<FString, int32> ObjectTypeCounts;


    // Per-world render settings
    URenderSettings RenderSettings;

    //partition
    std::unique_ptr<UWorldPartitionManager> Partition = nullptr;

    // Per-world selection manager
    std::unique_ptr<USelectionManager> SelectionMgr;

    // Per-frame processed overlap pairs (A,B) keyed canonically
    TSet<uint64> FrameOverlapPairs;

    //Timinig
    float UnscaledDelta;
    float SlomoOnlyDelta;
    float GameDelta;

    float TimeStopDilation; /* 얼마나 느리게 */
    float TimeDilation;

    float TimeStopDuration;  /* 얼마 동안 */
    float TimeDuration;

    bool bIsTearingDown = false;    // 월드가 파괴 중임을 알리는 플래그

    EWorldType WorldType = EWorldType::Editor;  // Default to editor world
};
template<class T>
inline T* UWorld::SpawnActor()
{
    return SpawnActor<T>(FTransform());
}

template<class T>
inline T* UWorld::SpawnActor(const FTransform& Transform)
{
    static_assert(std::is_base_of<AActor, T>::value, "T must be derived from AActor");

    // 새 액터 생성
    T* NewActor = NewObject<T>();

    // 초기 트랜스폼 적용
    NewActor->SetActorTransform(Transform);

    //  월드 등록
    NewActor->SetWorld(this);

    // 월드에 등록
    AddActorToLevel(NewActor);

    return NewActor;
}

// 월드에서 특정 클래스(T)의 '첫 번째' 액터를 찾아 반환합니다. 없으면 nullptr.
template<typename T>
inline T* UWorld::FindActor()
{
    static_assert(std::is_base_of_v<AActor, T>, "T must be derived from AActor.");

    UClass* TypeClass = T::StaticClass();
    if (!TypeClass)
    {
        return nullptr;
    }

    for (AActor* Actor : Level->GetActors())
    {
        if (Actor && !Actor->IsPendingDestroy() &&Actor->IsA(TypeClass))
        {
            // 찾은 액터를 T* 타입으로 캐스팅하여 반환합니다.
            // (IsA를 통과했으므로 Cast는 안전함)
            return Cast<T>(Actor);
        }
    }
    return nullptr;
}

// 월드에서 특정 클래스(T)의 '모든' 액터를 찾아 반환합니다. 없으면 nullptr.
template<typename T>
inline TArray<T*> UWorld::FindActors()
{
    static_assert(std::is_base_of_v<AActor, T>, "T must be derived from AActor.");

    TArray<T*> FoundActors;

    UClass* TypeClass = T::StaticClass();
    if (!TypeClass)
    {
        return FoundActors; // 빈 배열 반환
    }

    for (AActor* Actor : Level->GetActors())
    {
        if (Actor && !Actor->IsPendingDestroy() && Actor->IsA(TypeClass))
        {
            // 일치하는 모든 액터를 캐스팅하여 배열에 추가
            FoundActors.Add(Cast<T>(Actor));
        }
    }
    return FoundActors;
}

// 모든 액터의 컴포넌트를 순회해서 클래스(T)의 '첫 번째' 컴포넌트를 찾아 반환합니다. 없으면 nullptr. (비용이 크기 때문에 매 프레임 호출 비권장)
template<typename T>
inline T* UWorld::FindComponent()
{
    static_assert(std::is_base_of_v<UActorComponent, T>, "T must be derived from UActorComponent.");

    UClass* TypeClass = T::StaticClass();
    if (!TypeClass)
    {
        return nullptr;
    }

    for (AActor* Actor : Level->GetActors())
    {
        if (Actor && !Actor->IsPendingDestroy())
        {
            for (UActorComponent* Component : Actor->GetOwnedComponents())
            {
                if (Component && !Component->IsPendingDestroy() && Component->IsA(TypeClass))
                {
                    return Cast<T>(Component);
                }
            }
        }
    }

    return nullptr;
}
