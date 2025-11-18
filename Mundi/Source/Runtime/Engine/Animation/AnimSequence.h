#pragma once
#include "AnimSequenceBase.h"




/**
 * @brief  UAnimSequence는 FBX에서 뽑아 온 키 데이터를 게임 실행 시 뼈 포즈로 변환해 주는 래퍼
 * 핵심역할
 * 1. 데이터 모델 보관
 * 생성 시 UAnimSequenceBase에서 UAnimDataModel을 하나 갖고 있고, 
 * FBX 로더가 SetBoneTrackKeys로 채운 위치/회전/스케일 키들을 그대로 담음
 * 
 * 2. 포즈평가(GetAnimationPose / GetBonePose)
 * USkeletalMeshComponent::TickAnimInstances가 CurrentAnimation->GetAnimationPose(...)를 호출하면, 
 * UAnimSequence는 내부 UAnimDataModel::EvaluateBoneTrackTransform을 사용해 현재 시간의 각 본 로컬 트랜스폼을 계산해 FPoseContext.Pose에 채워줌
 * 
 */


// Forward declarations
struct FPoseContext
{
    TArray<FTransform> Pose;

    FPoseContext() {}
    explicit FPoseContext(int32 NumBones)
    {
        Pose.SetNum(NumBones);
    }
};

class UAnimSequence : public UAnimSequenceBase
{
    DECLARE_CLASS(UAnimSequence, UAnimSequenceBase)

public:
    UAnimSequence();
    virtual ~UAnimSequence() = default;

    // Get animation pose at specific time
    void GetAnimationPose(FPoseContext& OutPoseContext, const FAnimExtractContext& ExtractionContext);

    // Get bone pose for specific bones
    void GetBonePose(FPoseContext& OutPoseContext, const FAnimExtractContext& ExtractionContext);

    // Override GetPlayLength from base class
    virtual float GetPlayLength() const override;

    // Armature correction transform (for Blender FBX files with non-skeleton parent nodes)
    // Identity if no correction needed (e.g., Mixamo), or Armature's local transform if present
    FTransform ArmatureCorrection;

    void SetArmatureCorrection(const FTransform& InTransform) { ArmatureCorrection = InTransform; }
    FTransform GetArmatureCorrection() const { return ArmatureCorrection; }
};
