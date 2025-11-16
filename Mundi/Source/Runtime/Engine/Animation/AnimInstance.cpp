#include "pch.h"
#include "AnimInstance.h"
#include "SkeletalMeshComponent.h"
#include "AnimTypes.h"
#include "AnimationStateMachine.h"
#include "AnimSequence.h"


// ============================================================
// Initialization & Setup
// ============================================================

void UAnimInstance::Initialize(USkeletalMeshComponent* InComponent)
{
    OwningComponent = InComponent;

    if (OwningComponent && OwningComponent->GetSkeletalMesh())
    {
        CurrentSkeleton = OwningComponent->GetSkeletalMesh()->GetSkeleton();
    }
}

// ============================================================
// Update & Pose Evaluation
// ============================================================

void UAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    // 상태머신이 있으면 먼저 ProcessState 호출
    if (AnimStateMachine)
    {
        AnimStateMachine->ProcessState(DeltaSeconds);
    }

    if (!CurrentPlayState.Sequence)
    {
        return;
    }

    // 이전 시간 저장 (노티파이 검출용)
    PreviousPlayTime = CurrentPlayState.CurrentTime;

    // 현재 상태 시간 갱신
    AdvancePlayState(CurrentPlayState, DeltaSeconds);

    const bool bIsBlending = (BlendTimeRemaining > 0.0f && BlendTargetState.Sequence != nullptr);

    if (bIsBlending)
    {
        AdvancePlayState(BlendTargetState, DeltaSeconds);

        const float SafeTotalTime = FMath::Max(BlendTotalTime, 1e-6f);
        float BlendAlpha = 1.0f - (BlendTimeRemaining / SafeTotalTime);
        BlendAlpha = FMath::Clamp(BlendAlpha, 0.0f, 1.0f);

        TArray<FTransform> FromPose;
        TArray<FTransform> TargetPose;
        EvaluatePoseForState(CurrentPlayState, FromPose);
        EvaluatePoseForState(BlendTargetState, TargetPose);

        TArray<FTransform> BlendedPose;
        BlendPoseArrays(FromPose, TargetPose, BlendAlpha, BlendedPose);

        if (OwningComponent && BlendedPose.Num() > 0)
        {
            OwningComponent->SetAnimationPose(BlendedPose);
        }

        BlendTimeRemaining = FMath::Max(BlendTimeRemaining - DeltaSeconds, 0.0f);
        if (BlendTimeRemaining <= 1e-4f)
        {
            CurrentPlayState = BlendTargetState;
            CurrentPlayState.BlendWeight = 1.0f;

            BlendTargetState = FAnimationPlayState();
            BlendTimeRemaining = 0.0f;
            BlendTotalTime = 0.0f;
        }
    }
    else if (OwningComponent)
    {
        TArray<FTransform> Pose;
        EvaluatePoseForState(CurrentPlayState, Pose);

        if (Pose.Num() > 0)
        {
            OwningComponent->SetAnimationPose(Pose);
        }
    }

    // 노티파이 트리거
    TriggerAnimNotifies(DeltaSeconds);

    // 커브 업데이트
    UpdateAnimationCurves();
}


void UAnimInstance::EvaluatePose(TArray<FTransform>& OutPose)
{
    EvaluatePoseForState(CurrentPlayState, OutPose);
}
// ============================================================
// Playback API
// ============================================================

void UAnimInstance::PlaySequence(UAnimSequence* Sequence, bool bLoop, float InPlayRate)
{
    if (!Sequence)
    {
        UE_LOG("UAnimInstance::PlaySequence - Invalid sequence");
        return;
    }

    CurrentPlayState.Sequence = Sequence;
    CurrentPlayState.CurrentTime = 0.0f;
    CurrentPlayState.PlayRate = InPlayRate;
    CurrentPlayState.bIsLooping = bLoop;
    CurrentPlayState.bIsPlaying = true;
    CurrentPlayState.BlendWeight = 1.0f;

    PreviousPlayTime = 0.0f;

    UE_LOG("UAnimInstance::PlaySequence - Playing: %s (Loop: %d, PlayRate: %.2f)",
        Sequence->ObjectName.ToString().c_str(), bLoop, InPlayRate);
}

void UAnimInstance::StopSequence()
{
    CurrentPlayState.bIsPlaying = false;
    CurrentPlayState.CurrentTime = 0.0f;

    UE_LOG("UAnimInstance::StopSequence - Stopped");
}

void UAnimInstance::BlendTo(UAnimSequence* Sequence, bool bLoop, float InPlayRate, float BlendTime)
{
    if (!Sequence)
    {
        UE_LOG("UAnimInstance::BlendTo - Invalid sequence");
        return;
    }

    BlendTargetState.Sequence = Sequence;
    BlendTargetState.CurrentTime = 0.0f;
    BlendTargetState.PlayRate = InPlayRate;
    BlendTargetState.bIsLooping = bLoop;
    BlendTargetState.bIsPlaying = true;
    BlendTargetState.BlendWeight = 0.0f;

    BlendTimeRemaining = FMath::Max(BlendTime, 0.0f);
    BlendTotalTime = BlendTimeRemaining;

    if (BlendTimeRemaining <= 0.0f)
    {
        // 즉시 전환
        CurrentPlayState = BlendTargetState;
        CurrentPlayState.BlendWeight = 1.0f;
        BlendTargetState = FAnimationPlayState();
    }

    UE_LOG("UAnimInstance::BlendTo - Blending to: %s (BlendTime: %.2f)",
        Sequence->ObjectName.ToString().c_str(), BlendTime);
}


// ============================================================
// Notify & Curve Processing
// ============================================================

void UAnimInstance::TriggerAnimNotifies(float DeltaSeconds)
{
    if (!CurrentPlayState.Sequence)
    {
        return;
    }

    // UAnimSequenceBase의 GetAnimNotify를 사용하여 노티파이 수집
    TArray<FPendingAnimNotify> PendingNotifies;

    float DeltaMove = DeltaSeconds * CurrentPlayState.PlayRate;
    CurrentPlayState.Sequence->GetAnimNotify(PreviousPlayTime, DeltaMove, PendingNotifies);

    // 수집된 노티파이 처리
    for (const FPendingAnimNotify& Pending : PendingNotifies)
    {
        const FAnimNotifyEvent& Event = *Pending.Event;

        UE_LOG("AnimNotify Triggered: %s at %.2f (Type: %d)",
            Event.NotifyName.ToString().c_str(), Event.TriggerTime, (int)Pending.Type);

        // TODO: 노티파이 타입별 처리
        // switch (Pending.Type)
        // {
        //     case EAnimNotifyEventType::Begin:
        //         // 노티파이 시작
        //         break;
        //     case EAnimNotifyEventType::End:
        //         // 노티파이 종료
        //         break;
        // }
    }

}

void UAnimInstance::UpdateAnimationCurves()
{
    if (!CurrentPlayState.Sequence)
    {
        return;
    }

    // TODO: 애니메이션 커브 구현
    // UAnimDataModel에 CurveData가 추가되면 아래와 같이 구현:
    /*
    UAnimDataModel* DataModel = CurrentPlayState.Sequence->GetDataModel();
    const FAnimationCurveData& CurveData = DataModel->GetCurveData();

    for (const auto& Curve : CurveData.Curves)
    {
        float CurveValue = Curve.Evaluate(CurrentPlayState.CurrentTime);
        // 커브 값을 컴포넌트나 다른 시스템에 전달
    }
    */
}

// ============================================================
// State Machine & Parameters
// ============================================================

void UAnimInstance::SetStateMachine(UAnimationStateMachine* InStateMachine)
{
    AnimStateMachine = InStateMachine;

    if (AnimStateMachine)
    {
        AnimStateMachine->Initialize(this);
        UE_LOG("AnimInstance: StateMachine set and initialized");
    }
}
void UAnimInstance::EvaluatePoseForState(const FAnimationPlayState& PlayState, TArray<FTransform>& OutPose) const
{
    OutPose.Empty();

    if (!PlayState.Sequence)
    {
        return;
    }

    UAnimDataModel* DataModel = PlayState.Sequence->GetDataModel();
    if (!DataModel)
    {
        return;
    }

    const int32 NumBones = DataModel->GetNumBoneTracks();
    OutPose.SetNum(NumBones);

    FAnimExtractContext ExtractContext(PlayState.CurrentTime, PlayState.bIsLooping);
    FPoseContext PoseContext(NumBones);
    PlayState.Sequence->GetAnimationPose(PoseContext, ExtractContext);

    OutPose = PoseContext.Pose;
}

void UAnimInstance::AdvancePlayState(FAnimationPlayState& PlayState, float DeltaSeconds)
{
    if (!PlayState.Sequence || !PlayState.bIsPlaying)
    {
        return;
    }

    PlayState.CurrentTime += DeltaSeconds * PlayState.PlayRate;

    const float PlayLength = PlayState.Sequence->GetPlayLength();
    if (PlayLength <= 0.0f)
    {
        return;
    }

    if (PlayState.bIsLooping)
    {
        if (PlayState.CurrentTime >= PlayLength)
        {
            PlayState.CurrentTime = FMath::Fmod(PlayState.CurrentTime, PlayLength);
        }
    }
    else if (PlayState.CurrentTime >= PlayLength)
    {
        PlayState.CurrentTime = PlayLength;
        PlayState.bIsPlaying = false;
    }
}

void UAnimInstance::BlendPoseArrays(const TArray<FTransform>& FromPose, const TArray<FTransform>& ToPose, float Alpha, TArray<FTransform>& OutPose) const
{
    const int32 NumBones = FMath::Min(FromPose.Num(), ToPose.Num());
    if (NumBones == 0)
    {
        OutPose = FromPose;
        return;
    }

    const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
    OutPose.SetNum(NumBones);

    for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
    {
        const FTransform& From = FromPose[BoneIndex];
        const FTransform& To = ToPose[BoneIndex];

        FTransform Result;
        //Result.Lerp(From,To,ClampedAlpha);
        Result.Translation = FMath::Lerp(From.Translation, To.Translation, ClampedAlpha);
        Result.Scale3D = FMath::Lerp(From.Scale3D, To.Scale3D, ClampedAlpha);
        Result.Rotation = FQuat::Slerp(From.Rotation, To.Rotation, ClampedAlpha);
        Result.Rotation.Normalize();

        OutPose[BoneIndex] = Result;
    }
}
