#pragma once
struct FPhysicsConstraintSetup
{
    FName BodyNameA;
    FName BodyNameB;

    FTransform LocalFrameA;
    FTransform LocalFrameB;

    // 제한 값들
    float TwistLimitMin;
    float TwistLimitMax;
    float SwingLimitY;
    float SwingLimitZ;
};

class UBodySetup;
class UPhysicsAsset : public UObject
{
public:
    TArray<UBodySetup*> BodySetups;
    TArray<FPhysicsConstraintSetup> Constraints; // Runtime에 Instance로 변환

    // 무조건 Map은 1 대 1 매핑
    TMap<FName, int32> BodySetupIndexMap; // Cache, BodyName -> BodySetups 인덱스

    void BuildBodySetupIndexMap();

    int32 FindBodyIndex(FName BodyName) const; 

    UBodySetup* FindBodySetup(FName BodyName) const;

    int32 FindConstraintIndex(FName BodyA, FName BodyB) const;
};