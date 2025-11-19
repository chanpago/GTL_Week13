#pragma once
#include "pch.h"
#include "AnimNotify.h"
#include "AnimNotifyState.h"


enum class EAnimationMode : uint8
{
	AnimationBlueprint,
	AnimationSingleNode,
	Custom
};

enum class EPendingNotifyType : uint8
{
	Trigger,
	StateBegin,
	StateTick,
	StateEnd
};

enum ETypeAdvanceAnim : int
{
    ETAA_Default,
    ETAA_Finished,
    ETAA_Looped,
};

struct FAnimNotifyEvent
{
    float GetEndTriggerTime() const { return TriggerTime + Duration; }
    float GetTriggerTime() const { return TriggerTime; }

    bool IsState() const { return  (Duration > 0.0f) && (NotifyState); }
    bool IsSingleShot() const { return (Duration <= 0.0f) && (Notify);  }
    
    UAnimNotify* Notify = nullptr;
    UAnimNotifyState* NotifyState = nullptr;

    float Duration = 0.0f;
    float TriggerTime = 0.0f;
    FName NotifyName;
};

struct FPendingAnimNotify
{
	const FAnimNotifyEvent* Event;
    EPendingNotifyType Type = EPendingNotifyType::Trigger; 
};

enum class EAnimLayer
{
    Base = 0,   // 하체
    Upper = 1,  // 상체 
    Count
};
