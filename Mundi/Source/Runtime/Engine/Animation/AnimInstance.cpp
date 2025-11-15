#include "pch.h"
#include "AnimInstance.h"
#include "SkeletalMeshComponent.h"
#include "AnimTypes.h"

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
    if (!CurrentPlayState.bIsPlaying || !CurrentPlayState.Sequence)
    {
        return;
    }

    // 이전 시간 저장 (노티파이 검출용)
    PreviousPlayTime = CurrentPlayState.CurrentTime;

    // 시간 갱신
    CurrentPlayState.CurrentTime += DeltaSeconds * CurrentPlayState.PlayRate;

    float PlayLength = CurrentPlayState.Sequence->GetPlayLength();

    // 루핑 처리
    if (CurrentPlayState.bIsLooping)
    {
        if (CurrentPlayState.CurrentTime >= PlayLength)
        {
            CurrentPlayState.CurrentTime = FMath::Fmod(CurrentPlayState.CurrentTime, PlayLength);
        }
    }
    else
    {
        // 비루핑: 끝에 도달하면 정지
        if (CurrentPlayState.CurrentTime >= PlayLength)
        {
            CurrentPlayState.CurrentTime = PlayLength;
            CurrentPlayState.bIsPlaying = false;
        }
    }

    // 포즈 추출 및 컴포넌트에 적용
    if (OwningComponent)
    {
        TArray<FTransform> Pose;
        EvaluatePose(Pose);

        // 컴포넌트의 로컬 포즈에 적용
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
    if (!CurrentPlayState.Sequence)
    {
        return;
    }

    UAnimDataModel* DataModel = CurrentPlayState.Sequence->GetDataModel();
    if (!DataModel)
    {
        return;
    }

    int32 NumBones = DataModel->GetNumBoneTracks();
    OutPose.SetNum(NumBones);

    // FAnimExtractContext 생성
    FAnimExtractContext ExtractContext(CurrentPlayState.CurrentTime, CurrentPlayState.bIsLooping);
    FPoseContext PoseContext(NumBones);

    // 애니메이션 포즈 추출
    CurrentPlayState.Sequence->GetAnimationPose(PoseContext, ExtractContext);

    // 결과 복사
    OutPose = PoseContext.Pose;
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

void UAnimInstance::BlendTo(UAnimSequence* Sequence, float BlendTime)
{
    if (!Sequence)
    {
        UE_LOG("UAnimInstance::BlendTo - Invalid sequence");
        return;
    }

    // 간단한 블렌드 구현 (향후 확장 가능)
    // 현재는 즉시 전환
    BlendTargetState.Sequence = Sequence;
    BlendTargetState.CurrentTime = 0.0f;
    BlendTargetState.PlayRate = CurrentPlayState.PlayRate;
    BlendTargetState.bIsLooping = CurrentPlayState.bIsLooping;
    BlendTargetState.bIsPlaying = true;
    BlendTargetState.BlendWeight = 0.0f;

    BlendTimeRemaining = BlendTime;
    BlendTotalTime = BlendTime;

    if (BlendTime <= 0.0f)
    {
        // 즉시 전환
        CurrentPlayState = BlendTargetState;
        CurrentPlayState.BlendWeight = 1.0f;
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
