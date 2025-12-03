#pragma once

#include "SceneComponent.h"
#include "Material.h"
#include "UPrimitiveComponent.generated.h"

struct FBodyInstance;
// 전방 선언
struct FSceneCompData;

class URenderer;
struct FMeshBatchElement;
class FSceneView;

struct FOverlapInfo
{
    AActor* OtherActor = nullptr;
    UPrimitiveComponent* Other = nullptr;
};

UCLASS(DisplayName="프리미티브 컴포넌트", Description="렌더링 가능한 기본 컴포넌트입니다")
class UPrimitiveComponent :public USceneComponent
{
public:

    GENERATED_REFLECTION_BODY();

    UPrimitiveComponent();
    virtual ~UPrimitiveComponent();

    void OnRegister(UWorld* InWorld) override;
    void OnUnregister() override;

    virtual FAABB GetWorldAABB() const { return FAABB(); }

    // 이 프리미티브를 렌더링하는 데 필요한 FMeshBatchElement를 수집합니다.
    virtual void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) {}

    virtual UMaterialInterface* GetMaterial(uint32 InElementIndex) const
    {
        // 기본 구현: UPrimitiveComponent 자체는 머티리얼을 소유하지 않으므로 nullptr 반환
        return nullptr;
    }
    virtual void SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial)
    {
        // 기본 구현: 아무것도 하지 않음 (머티리얼을 지원하지 않거나 설정 불가)
    }

    // 내부적으로 ResourceManager를 통해 UMaterial*를 찾아 SetMaterial을 호출합니다.
    void SetMaterialByName(uint32 InElementIndex, const FString& MaterialName);

    void SetCulled(bool InCulled)
    {
        bIsCulled = InCulled;
    }

    bool GetCulled() const
    {
        return bIsCulled;
    }

    // ───── 충돌 관련 ──────────────────────────── 
    bool IsOverlappingActor(const AActor* Other) const;
    virtual const TArray<FOverlapInfo>& GetOverlapInfos() const { static TArray<FOverlapInfo> Empty; return Empty; }

    // BodySetup 설정을 무시하고 싶을 때
    bool bOverrideCollisionSetting = false;
    ECollisionState CollisionEnabled = ECollisionState::QueryAndPhysics;
    
    // ───── 복사 관련 ────────────────────────────
    void DuplicateSubObjects() override;

    // Overlap event generation toggle API
    void SetGenerateOverlapEvents(bool bEnable) { bEnable ? bGenerateOverlapEvents = true : bGenerateOverlapEvents = false; }
    bool GetGenerateOverlapEvents() const { return bGenerateOverlapEvents; }

    // ───── 직렬화 ────────────────────────────
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

protected:
    bool bIsCulled = false;
     
    // ───── 충돌 관련 ────────────────────────────
    UPROPERTY(EditAnywhere, Category="Shape")
    bool bGenerateOverlapEvents;

    UPROPERTY(EditAnywhere, Category="Shape")
    bool bBlockComponent;

    FBodyInstance* BodyInstance;

private:
    UPROPERTY()
    int32 CollisionEnabled_Internal = 2; // ECollisionState::QueryAndPhysics;
};
