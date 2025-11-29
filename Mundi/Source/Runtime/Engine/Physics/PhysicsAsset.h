#pragma once
#include "Name.h"
#include "ResourceBase.h"

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

    void Serialize(const bool bInIsLoading, JSON& InOutHandle)
    {
        if (bInIsLoading)
        {
            FString BodyAName;
            if (FJsonSerializer::ReadString(InOutHandle, "BodyA", BodyAName))
            {
                BodyNameA = FName(BodyAName);
            }
            FString BodyBName;
            if (FJsonSerializer::ReadString(InOutHandle, "BodyB", BodyAName))
            {
                BodyNameB = FName(BodyBName);
            }

            FJsonSerializer::ReadTransform(InOutHandle, "LocalFrameA", LocalFrameA);
            FJsonSerializer::ReadTransform(InOutHandle, "LocalFrameB", LocalFrameB);
            FJsonSerializer::ReadFloat(InOutHandle, "TwistMin", TwistLimitMin);
            FJsonSerializer::ReadFloat(InOutHandle, "TwistMax", TwistLimitMax);
            FJsonSerializer::ReadFloat(InOutHandle, "SwingY", SwingLimitY);
            FJsonSerializer::ReadFloat(InOutHandle, "SwingZ", SwingLimitZ);
        }
        else
        {
            JSON Item = JSON::Make(JSON::Class::Object);

            Item["BodyA"] = BodyNameA.ToString().c_str();
            Item["BodyB"] = BodyNameB.ToString().c_str();
            Item["LocalFrameA"] = FJsonSerializer::TransformToJson(LocalFrameA);
            Item["LocalFrameB"] = FJsonSerializer::TransformToJson(LocalFrameB);
            Item["TwistMin"] = TwistLimitMin;
            Item["TwistMax"] = TwistLimitMax;
            Item["SwingY"] = SwingLimitY;
            Item["SwingZ"] = SwingLimitZ;

            InOutHandle["Constraints"] = Item;
        }
    }
};

class UBodySetup;
class UPhysicsAsset : public UResourceBase
{
public:
    DECLARE_CLASS(UPhysicsAsset, UResourceBase)

    UPhysicsAsset() = default;
    virtual ~UPhysicsAsset() = default;

    void SetName(const FName& NewName) { Name = NewName; }
    FName GetName() const { return Name; }

    TArray<UBodySetup*> BodySetups;
    TArray<FPhysicsConstraintSetup> Constraints; // Runtime에 Instance로 변환

    // 무조건 Map은 1 대 1 매핑
    TMap<FName, int32> BodySetupIndexMap; // Cache, BodyName -> BodySetups 인덱스

    // ====================================
    // 헬퍼 함수
    // ====================================
    void BuildBodySetupIndexMap();

    int32 FindBodyIndex(FName BodyName) const; 

    UBodySetup* FindBodySetup(FName BodyName) const;

    int32 FindConstraintIndex(FName BodyA, FName BodyB) const;

    // void CreateBodyInstance(FBodyInstance& OutInstance, FPhysicsScene& PhysicsScnene/*physics의 life cycle을 돌리는 class*/, const FTransform& WorldTransform);

    // ====================================
    // Asset 직렬화
    // ====================================
    // UResourceBase Load
    bool Load(const FString& InFilePath, ID3D11Device* InDevice);

    // PhysicsAsset을 JSON 형식으로 파일에 저장
    bool SaveToFile(const FString& FilePath);

    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

    // Runtime에서 같은 자산을  쓸 때, 모두 원본 JSON을 직접 해석하지 않도록 파생 데이터 묶음을 제작해줌
    // TODO :
    void BuildRuntimeCache();

private:
    FName Name;
};