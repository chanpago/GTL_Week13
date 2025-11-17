#include "pch.h"
#include "AnimSequenceBase.h"
#include "ObjectFactory.h"
#include "AnimSequence.h"
#include "AnimationRuntime.h"
#include "AnimNotify_PlaySound.h"

#include "AnimTypes.h"


IMPLEMENT_CLASS(UAnimSequenceBase)

UAnimSequenceBase::UAnimSequenceBase()
    : RateScale(1.0f)
    , bLoop(true)
    , DataModel(nullptr)
{
    // Create DataModel
    DataModel = NewObject<UAnimDataModel>();
}

UAnimDataModel* UAnimSequenceBase::GetDataModel() const
{
    return DataModel;
}

bool UAnimSequenceBase::IsNotifyAvailable() const
{
    return (Notifies.Num() != 0) && (GetPlayLength() > 0.f);
}

TArray<FAnimNotifyEvent>& UAnimSequenceBase::GetAnimNotifyEvents()
{
    return Notifies;
}

const TArray<FAnimNotifyEvent>& UAnimSequenceBase::GetAnimNotifyEvents() const
{
    return Notifies;
}

void UAnimSequenceBase::GetAnimNotify(const float& StartTime, const float& DeltaTime, TArray<FPendingAnimNotify>& OutNotifies) const
{
    OutNotifies.Empty();

    if (!IsNotifyAvailable())
    {
        return;
    }

    bool const bPlayinfBackywards = (DeltaTime < 0.f);
    float PreviousPosition = StartTime;
    float CurrentPosition = StartTime;
    float DesiredDeltaMove = DeltaTime;
    const float PlayLength = GetPlayLength();

    // 한 틱 동안 애니메이션 여러번 돌게 될 때,
    // notify가 호출되는 상한선을 정해주는 함수
    uint32_t MaxLoopCount = 2;
    if (PlayLength > 0.0f && FMath::Abs(DeltaTime) > PlayLength)
    {
        MaxLoopCount = FMath::Clamp((DesiredDeltaMove / PlayLength), 2.0f, 1000.0f); 
    }

    for (int i = 0; i < 1; ++i)
    {
        const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, PlayLength);

        GetAnimNotifiesFromDeltaPosition(PreviousPosition, CurrentPosition, OutNotifies);

        // 끝까지 갔고, 루프가 true일 때 다음 바퀴를 준비 
        if ((AdvanceType == ETAA_Finished) && bLoop) 
        {
            const float ActualDelatMove = (CurrentPosition - PreviousPosition);
            DesiredDeltaMove -= ActualDelatMove;

            PreviousPosition = (bPlayinfBackywards) ? GetPlayLength() : 0.f;
            CurrentPosition = PreviousPosition;
        }
        else
        {
            break;
        }
    }
     
}

void UAnimSequenceBase::GetAnimNotifiesFromDeltaPosition(const float& PreviousPosition, const float& CurrentPosition, TArray<FPendingAnimNotify>& OutNotifies) const
{
    if (Notifies.Num() == 0)
    {
        return;
    }

    bool const bPlayingBackwards = (CurrentPosition < PreviousPosition);

    const float MinTime = bPlayingBackwards ? CurrentPosition : PreviousPosition;
    const float MaxTime = bPlayingBackwards ? PreviousPosition : CurrentPosition;
     
    for (int32 NotifyIndex = 0; NotifyIndex < Notifies.Num(); ++NotifyIndex)
    {
        const FAnimNotifyEvent& AnimNotifyEvent = Notifies[NotifyIndex];

        const float NotifyStartTime = AnimNotifyEvent.GetTriggerTime();
        const float NotifyEndTime = AnimNotifyEvent.GetEndTriggerTime();

        const bool bIsState = AnimNotifyEvent.IsState();
        const bool bIsSingleShot = AnimNotifyEvent.IsSingleShot();

        if (!bPlayingBackwards)
        {
            // 정방향 
            if (bIsSingleShot)
            {
                if (MinTime < NotifyStartTime && MaxTime >= NotifyStartTime)
                {
                    FPendingAnimNotify Pending;
                    Pending.Event = &AnimNotifyEvent;
                    Pending.Type = EPendingNotifyType::Trigger;

                    OutNotifies.Add(Pending);
                    
                }
            }
            else if (bIsState)
            {
                const bool bNotIntersects = (MinTime >= NotifyEndTime || MaxTime < NotifyStartTime);

                if (!bNotIntersects)
                {
                    continue;
                }

                // State중 Being 판별 
                if (NotifyStartTime >= MinTime && NotifyStartTime < MaxTime)
                {
                    FPendingAnimNotify PendingBegin;
                    PendingBegin.Event = &AnimNotifyEvent;
                    PendingBegin.Type = EPendingNotifyType::StateBegin;
                    OutNotifies.Add(PendingBegin);
                }

                // State중 Tick 판별 
                {
                    FPendingAnimNotify PendingTick;
                    PendingTick.Event = &AnimNotifyEvent;
                    PendingTick.Type = EPendingNotifyType::StateTick;
                    OutNotifies.Add(PendingTick);
                }

                // State중 End 판별 
                if (NotifyEndTime > MinTime && NotifyEndTime <= MaxTime)
                {
                    FPendingAnimNotify PendingEnd;
                    PendingEnd.Event = &AnimNotifyEvent;
                    PendingEnd.Type = EPendingNotifyType::StateEnd;
                    OutNotifies.Add(PendingEnd);
                }
            } 
        } 
        // 역방향 
        else
        {
            if (bIsSingleShot)
            {
                //if (MinTime < NotifyStartTime && MaxTime >= NotifyStartTime) 
                if (MinTime >=  NotifyEndTime && MaxTime < NotifyStartTime)
                {
                    FPendingAnimNotify Pending;
                    Pending.Event = &AnimNotifyEvent;
                    Pending.Type = EPendingNotifyType::Trigger;

                    OutNotifies.Add(Pending);
                }
            }
            // TODO: 역방향 Notify는 정상작동하는 지 아직 체크 못해봄
            else if (bIsState)
            {
                const bool bNotIntersects = (MaxTime < NotifyStartTime || MinTime >= NotifyEndTime);
                if (bNotIntersects)
                {
                    continue;
                }

                if (MaxTime >= NotifyEndTime && MinTime < NotifyEndTime)
                {
                    FPendingAnimNotify PendingEnd;
                    PendingEnd.Event = &AnimNotifyEvent;
                    PendingEnd.Type = EPendingNotifyType::StateEnd;
                    OutNotifies.Add(PendingEnd);
                }

                {
                    FPendingAnimNotify PendingTick;
                    PendingTick.Event = &AnimNotifyEvent;
                    PendingTick.Type = EPendingNotifyType::StateTick;
                    OutNotifies.Add(PendingTick);
                }

                if (NotifyStartTime < MaxTime && NotifyStartTime >= MinTime)
                {
                    FPendingAnimNotify PendingBegin;
                    PendingBegin.Event = &AnimNotifyEvent;
                    PendingBegin.Type = EPendingNotifyType::StateBegin;
                    OutNotifies.Add(PendingBegin);
                }

            }
           
        }
    }
}

void UAnimSequenceBase::AddPlaySoundNotify(float Time, UAnimNotify* Notify, float Duration)
{
    if (!Notify)
    {
        return;
    } 

    FAnimNotifyEvent NewEvent;
    NewEvent.TriggerTime = Time;
    NewEvent.Duration = Duration;
    NewEvent.Notify = Notify;
    NewEvent.NotifyState = nullptr;

    Notifies.Add(NewEvent); 
}
