#pragma once
#include "Name.h"
#include "ResourceBase.h"
#include "FKShapeElem.h"
struct FPhysicsConstraintSetup
{
    FName BodyNameA;
    FName BodyNameB;

    //Actor의 local 기준, 조인트의 transform
    FTransform LocalFrameA; 
    FTransform LocalFrameB;

    // UI에 노출될 제한 값들 : (-Limit ~ +Limit) 
    // 원하는 값이 좌 -15, 우 +45면 초기화를 +15로 하고, limit를 15로 줄 것
    float TwistLimitMin; // x축 회전
    float TwistLimitMax;
    float SwingLimitY; // y축 회전
    float SwingLimitZ;

    void Clear();
    void Serialize(const bool bInIsLoading, JSON& InOutHandle);
    bool  bEnableCollision = false;   // 기본은 서로 충돌 안 함
};

struct FBonePoints
{
    FVector Start;
    FVector End;
};

class UBodySetup;
struct FSkeleton;
struct FBonePoints;
class USkeletalMeshComponent;
class UPhysicsAsset : public UResourceBase
{
public:
    DECLARE_CLASS(UPhysicsAsset, UResourceBase)

    UPhysicsAsset() = default;
    virtual ~UPhysicsAsset();

    void SetName(const FName& NewName) { Name = NewName; }
    FName GetName() const { return Name; }

    TArray<UBodySetup*> BodySetups;
    TArray<FPhysicsConstraintSetup> Constraints; // Runtime에 Instance로 변환

    // 무조건 Map은 1 대 1 매핑
    TMap<FName, int32> BodySetupIndexMap; // Cache, BodyName -> BodySetups 인덱스

    // ====================================
    // 헬퍼 함수
    // ====================================
    // Runtime에서 같은 자산을  쓸 때, 모두 원본 JSON을 직접 해석하지 않도록 파생 데이터 묶음을 제작해줌
    void BuildRuntimeCache();
    void BuildBodySetupIndexMap();

    int32 FindBodyIndex(FName BodyName) const; 
    UBodySetup* FindBodySetup(FName BodyName) const;
    int32 FindConstraintIndex(FName BodyA, FName BodyB) const;



    // ====================================
    // 뷰어, 자동 PhysicsAsset 관련 함수
    // ====================================
    USkeletalMeshComponent* CurrentSkeletal = nullptr;
    float MinColliderLength = 0.1f;

    void CreateGenerateAllBodySetup(EAggCollisionShapeType ShapeType, FSkeleton* Skeleton, USkeletalMeshComponent* SkeletalComponent = nullptr);
    void SelectBonesForBodies(const FSkeleton* Skeleton, TArray<int32>& OutBones) const;
    FBonePoints GetBonePoints(const FSkeleton* Skeleton, int32 BoneIndex) const;
    FKSphereElem FitSphereToBone(const FSkeleton* Skeleton, int32 BoneIndex);
    FKBoxElem FitBoxToBone(const FSkeleton* Skeleton, int32 BoneIndex);
    FKCapsuleElem FitCapsuleToBone(const FSkeleton* Skeleton, int32 BoneIndex);

    void GenerateConstraintsFromSkeleton(const FSkeleton* Skeleton, const TArray<int32>& BoneIndicesToCreate, USkeletalMeshComponent* SkeletalComponent = nullptr);

    float GetMeshScale() const;
    void CalculateScaledMinColliderLength();

    // ====================================
    // Asset 직렬화
    // ====================================
    // UResourceBase Load
    bool Load(const FString& InFilePath, ID3D11Device* InDevice);

    // PhysicsAsset을 JSON 형식으로 파일에 저장
    bool SaveToFile(const FString& FilePath);

    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

private:
    FName Name;
};