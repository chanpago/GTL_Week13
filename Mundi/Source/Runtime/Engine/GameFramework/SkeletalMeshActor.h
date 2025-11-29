#pragma once
#include "Actor.h"
#include "LineComponent.h"
#include "SkeletalMeshComponent.h"
#include "BoneAnchorComponent.h"
#include "ASkeletalMeshActor.generated.h"

UCLASS(DisplayName="스켈레탈 메시", Description="스켈레탈 메시를 배치하는 액터입니다")

class ASkeletalMeshActor : public AActor
{
public:
    GENERATED_REFLECTION_BODY()

    ASkeletalMeshActor();
    ~ASkeletalMeshActor() override;

    // AActor
    void Tick(float DeltaTime) override;
    FAABB GetBounds() const override;

    // 컴포넌트 접근자
    USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }
    void SetSkeletalMeshComponent(USkeletalMeshComponent* InComp);
    
    // - 본 오버레이(뼈대 선) 시각화를 위한 라인 컴포넌트
    ULineComponent* GetBoneLineComponent() const { return BoneLineComponent; }
    ULineComponent* GetBodyLineComponent() const { return BodyLineComponent; }
    UBoneAnchorComponent* GetBoneGizmoAnchor() const { return BoneAnchor; }

    // Convenience: forward to component
    void SetSkeletalMesh(const FString& PathFileName);

    // Rebuild bone line overlay from the current skeletal mesh bind pose
    // SelectedBoneIndex: highlight this bone and its parent connection
    void RebuildBoneLines(int32 SelectedBoneIndex);

    // Rebuild physics body line overlay from the current physics asset
    void RebuildBodyLines(bool& bChangedGeomNum, int32 SelectedBodyIndex);

    // Position the anchor
    void RepositionAnchorToBone(int32 BoneIndex);

    // Bone picking with ray
    // Returns bone index if hit, -1 otherwise
    int32 PickBone(const FRay& Ray, float& OutDistance) const;

    // Copy/Serialize
    void DuplicateSubObjects() override;
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

protected:
    // 스켈레탈 메시를 실제로 렌더링하는 컴포넌트 (미리뷰/프리뷰 액터의 루트로 사용)
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
    
    // 본(부모-자식) 연결을 라인으로 그리기 위한 디버그용 컴포넌트
    // - 액터의 로컬 공간에서 선분을 추가하고, 액터 트랜스폼에 따라 함께 이동/회전/스케일됨
    ULineComponent* BoneLineComponent = nullptr;

    // Physics body 시각화를 위한 라인 컴포넌트
    ULineComponent* BodyLineComponent = nullptr;

    // Anchor component used for gizmo selection/transform at a bone
    UBoneAnchorComponent* BoneAnchor = nullptr;

    // Incremental bone line overlay cache (avoid ClearLines every frame)   
    struct FBoneDebugLines
    {
        TArray<ULine*> ConeEdges;         // NumSegments lines from base circle to tip (child joint)
        TArray<ULine*> ConeBase;          // NumSegments lines forming the base circle at parent
        TArray<ULine*> Rings;             // 3 * NumSegments lines per bone (joint spheres)
    };

    // Physics body line cache (avoid ClearLines every frame)
    struct FBodyDebugLines
    {
        TArray<ULine*> SphereLines;       // Lines for sphere collision shapes
        TArray<ULine*> BoxLines;          // Lines for box collision shapes
        TArray<ULine*> CapsuleLines;      // Lines for capsule collision shapes
        TArray<ULine*> ConvexLines;       // Lines for convex collision shapes
    };

    bool bBoneLinesInitialized = false;
    int32 CachedSegments = 4;
    int32 CachedSelected = -1;
    TArray<FBoneDebugLines> BoneLinesCache; // size == BoneCount
    TArray<TArray<int32>> BoneChildren;     // adjacency for subtree updates

    bool bBodyLinesInitialized = false;
    TArray<FBodyDebugLines> BodyLinesCache; // size == BodySetup count
    UPhysicsAsset* CachedPhysicsAsset = nullptr;

    float BoneJointRadius = 0.02f;
    float BoneBaseRadius = 0.03f;

    void BuildBoneLinesCache();
    void BuildBodyLinesCache();
    void UpdateBoneSubtreeTransforms(int32 BoneIndex);
    void UpdateBoneSelectionHighlight(int32 SelectedBoneIndex);
    void UpdateBodyTransforms();

    // Lazily create viewer-only components (BoneLineComponent, BodyLineComponent, BoneAnchor) if in preview world
    void EnsureViewerComponents();
};
