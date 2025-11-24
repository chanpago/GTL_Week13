#include "pch.h"
#include "AnimationStateMachine.h"
#include "AnimInstance.h"
#include "AnimSequence.h"

IMPLEMENT_CLASS(UAnimationStateMachine)

void UAnimationStateMachine::Initialize(UAnimInstance* InOwner, EAnimLayer InLayer)
{
    Owner = InOwner;
    TargetLayer = InLayer;
    
    UE_LOG("AnimationStateMachine initialized");
}

void UAnimationStateMachine::AddState(const FAnimationState& State)
{
    States.Add(State);
    UE_LOG("AnimationStateMachine: State added - %s", State.Name.ToString().c_str());
}

void UAnimationStateMachine::AddTransition(const FStateTransition& Transition)
{
    Transitions.Add(Transition);
    UE_LOG("AnimationStateMachine: Transition added - %s -> %s",
        Transition.FromState.ToString().c_str(),
        Transition.ToState.ToString().c_str());
}

void UAnimationStateMachine::SetInitialState(const FName& StateName)
{
    const FAnimationState* State = FindState(StateName);
    if (!State)
    {
        UE_LOG("AnimationStateMachine::SetInitialState - State not found: %s",
            StateName.ToString().c_str());
        return;
    }

    CurrentStateName = StateName;

    // 초기 상태의 애니메이션 재생
    if (Owner)
    {
        if (State->Sequence)
        {
            // 기존 방식: AnimSequence 재생
            Owner->PlaySequence(State->Sequence, State->bLoop, State->PlayRate);
        }
        else if (State->PoseProvider)
        {
            // 새 방식: PoseProvider 재생 (BlendSpace 등)
            Owner->PlayPoseProvider(State->PoseProvider, State->bLoop, State->PlayRate);
        }
        UE_LOG("AnimationStateMachine: Initial state set to '%s'", StateName.ToString().c_str());
    }
}

void UAnimationStateMachine::ProcessState(float DeltaSeconds)
{
    if (!Owner)
    {
        return;
    }

    const FAnimationState* CurrentState = FindState(CurrentStateName);
    if (CurrentState && CurrentState->OnUpdate)
    {
        CurrentState->OnUpdate();
    }

    // 전이 조건 평가
    EvaluateTransitions();
}

const FAnimationState* UAnimationStateMachine::FindState(const FName& StateName) const
{
    for (const FAnimationState& State : States)
    {
        if (State.Name == StateName)
        {
            return &State;
        }
    }
    return nullptr;
}

void UAnimationStateMachine::Clear()
{
    States.Empty();
    Transitions.Empty();
    // @todo FName_None으로 대체
    CurrentStateName = "";
}

void UAnimationStateMachine::EvaluateTransitions()
{
    for (const FStateTransition& Transition : Transitions)
    {
        // 현재 상태에서 출발하는 전이만 평가
        if (Transition.FromState != CurrentStateName)
        {
            continue;
        }

        // 조건 함수가 없으면 건너뛰기
        if (!Transition.Condition)
        {
            continue;
        }

        // 조건 평가
        if (Transition.Condition())
        {
            // 조건이 만족되면 상태 전이
            ChangeState(Transition.ToState, Transition.BlendTime);
            break; // 하나의 전이만 처리
        }
    }
}

void UAnimationStateMachine::ChangeState(const FName& NewStateName, float BlendTime)
{
    if (CurrentStateName == NewStateName)
    {
        return; // 이미 해당 상태
    }

    const FAnimationState* NewState = FindState(NewStateName);
    if (!NewState)
    {
        UE_LOG("AnimationStateMachine::ChangeState - State not found: %s",
            NewStateName.ToString().c_str());
        return;
    }

    UE_LOG("AnimationStateMachine: State transition - %s -> %s (BlendTime: %.2f)",
        CurrentStateName.ToString().c_str(),
        NewStateName.ToString().c_str(),
        BlendTime);

    CurrentStateName = NewStateName;

    // 새 상태의 애니메이션 재생
    if (Owner)
    {
        if (NewState->Sequence)
        {
            // 기존 방식: AnimSequence
            if (BlendTime > 0.0f)
            {
                Owner->BlendTo(NewState->Sequence, NewState->bLoop, NewState->PlayRate, BlendTime);
            }
            else
            {
                Owner->PlaySequence(NewState->Sequence, NewState->bLoop, NewState->PlayRate);
            }
        }
        else if (NewState->PoseProvider)
        {
            // 새 방식: PoseProvider (BlendSpace 등)
            if (BlendTime > 0.0f)
            {
                Owner->BlendToPoseProvider(NewState->PoseProvider, NewState->bLoop, NewState->PlayRate, BlendTime);
            }
            else
            {
                Owner->PlayPoseProvider(NewState->PoseProvider, NewState->bLoop, NewState->PlayRate);
            }
        }
    }
}
