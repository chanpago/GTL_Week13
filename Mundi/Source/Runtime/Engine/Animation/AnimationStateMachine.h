#pragma once
#include "Object.h"
#include <functional>

// Forward declarations
class UAnimSequence;
class UAnimInstance;

/**
 * @brief 애니메이션 상태 정의
 */
struct FAnimationState
{
    FName Name;                 // 상태 이름 (예: "Idle", "Walk", "Run")
    UAnimSequence* Sequence;    // 재생할 시퀀스
    bool bLoop;                 // 루프 여부
    float PlayRate;             // 재생 속도

    FAnimationState()
        : Name("None")
        , Sequence(nullptr)
        , bLoop(true)
        , PlayRate(1.0f)
    {}

    FAnimationState(const FName& InName, UAnimSequence* InSequence, bool InLoop = true, float InPlayRate = 1.0f)
        : Name(InName)
        , Sequence(InSequence)
        , bLoop(InLoop)
        , PlayRate(InPlayRate)
    {}
};

/**
 * @brief 상태 전이 조건
 */
struct FStateTransition
{
    FName FromState;            // 출발 상태
    FName ToState;              // 도착 상태
    std::function<bool()> Condition;  // 전이 조건 함수
    float BlendTime;            // 블렌드 시간

    FStateTransition()
        : FromState("None")
        , ToState("None")
        , Condition(nullptr)
        , BlendTime(0.2f)
    {}

    FStateTransition(const FName& From, const FName& To, std::function<bool()> InCondition, float InBlendTime = 0.2f)
        : FromState(From)
        , ToState(To)
        , Condition(InCondition)
        , BlendTime(InBlendTime)
    {}
};

/**
 * @brief 애니메이션 상태 머신
 * - 상태와 전이를 관리
 * - AnimInstance와 연동하여 애니메이션 재생 제어
 *
 * @example 사용 예시:
 *
 * // 1. 애니메이션 시퀀스 로드
 * UAnimSequence* IdleAnim = RESOURCE.Get<UAnimSequence>("Data/Character_Idle.fbx_Idle");
 * UAnimSequence* WalkAnim = RESOURCE.Get<UAnimSequence>("Data/Character_Walk.fbx_Walk");
 * UAnimSequence* RunAnim = RESOURCE.Get<UAnimSequence>("Data/Character_Run.fbx_Run");
 *
 * // 2. 상태머신 생성 및 초기화
 * UAnimationStateMachine* StateMachine = NewObject<UAnimationStateMachine>();
 * StateMachine->Initialize(AnimInstance);
 *
 * // 3. 상태 추가
 * StateMachine->AddState(FAnimationState("Idle", IdleAnim, true, 1.0f));
 * StateMachine->AddState(FAnimationState("Walk", WalkAnim, true, 1.0f));
 * StateMachine->AddState(FAnimationState("Run", RunAnim, true, 1.0f));
 *
 * // 4. 전이 조건 추가 (람다 함수 사용)
 * // Idle -> Walk: 이동 속도가 0보다 크고 5 미만일 때
 * StateMachine->AddTransition(FStateTransition(
 *     "Idle", "Walk",
 *     [AnimInstance]() {
 *         float Speed = AnimInstance->GetMovementSpeed();
 *         return Speed > 0.0f && Speed < 5.0f;
 *     },
 *     0.2f // BlendTime
 * ));
 *
 * // Walk -> Run: 이동 속도가 5 이상일 때
 * StateMachine->AddTransition(FStateTransition(
 *     "Walk", "Run",
 *     [AnimInstance]() {
 *         return AnimInstance->GetMovementSpeed() >= 5.0f;
 *     },
 *     0.3f
 * ));
 *
 * // Run -> Walk: 이동 속도가 5 미만일 때
 * StateMachine->AddTransition(FStateTransition(
 *     "Run", "Walk",
 *     [AnimInstance]() {
 *         return AnimInstance->GetMovementSpeed() < 5.0f;
 *     },
 *     0.2f
 * ));
 *
 * // Walk -> Idle: 이동 속도가 0일 때
 * StateMachine->AddTransition(FStateTransition(
 *     "Walk", "Idle",
 *     [AnimInstance]() {
 *         return AnimInstance->GetMovementSpeed() <= 0.0f;
 *     },
 *     0.2f
 * ));
 *
 * // 5. 초기 상태 설정 (자동으로 애니메이션 재생 시작)
 * StateMachine->SetInitialState("Idle");
 *
 * // 6. AnimInstance에 상태머신 연결
 * AnimInstance->SetStateMachine(StateMachine);
 *
 * // 7. 게임플레이 중 파라미터 업데이트 (전이 조건 평가에 사용됨)
 * AnimInstance->SetMovementSpeed(3.0f);  // Walk 애니메이션으로 자동 전이
 * AnimInstance->SetMovementSpeed(7.0f);  // Run 애니메이션으로 자동 전이
 * AnimInstance->SetMovementSpeed(0.0f);  // Idle 애니메이션으로 자동 전이
 */
class UAnimationStateMachine : public UObject
{
    DECLARE_CLASS(UAnimationStateMachine, UObject)

public:
    UAnimationStateMachine() = default;
    virtual ~UAnimationStateMachine() = default;

    /**
     * @brief 상태 머신 초기화
     * @param InOwner 소유 AnimInstance
     */
    void Initialize(UAnimInstance* InOwner, EAnimLayer InLayer = EAnimLayer::Base);

    /**
     * @brief 상태 추가
     * @param State 추가할 상태
     */
    void AddState(const FAnimationState& State);

    /**
     * @brief 전이 추가
     * @param Transition 추가할 전이
     */
    void AddTransition(const FStateTransition& Transition);

    /**
     * @brief 초기 상태 설정
     * @param StateName 초기 상태 이름
     */
    void SetInitialState(const FName& StateName);

    /**
     * @brief 상태 머신 업데이트 (매 프레임 호출)
     * @param DeltaSeconds 프레임 시간
     */
    void ProcessState(float DeltaSeconds);

    /**
     * @brief 현재 상태 반환
     */
    FName GetCurrentState() const { return CurrentStateName; }

    /**
     * @brief 특정 상태 찾기
     */
    const FAnimationState* FindState(const FName& StateName) const;

    /**
     * @brief 내부 상태 초기화 (Owner 정보는 유지, 블루프린트 컴파일용)
     */
    void Clear();

protected:
    /**
     * @brief 상태 전이 평가
     */
    void EvaluateTransitions();

    /**
     * @brief 상태 변경
     * @param NewStateName 새 상태 이름
     * @param BlendTime 블렌드 시간
     */
    void ChangeState(const FName& NewStateName, float BlendTime);

protected:
    /** 소유 AnimInstance */
    UAnimInstance* Owner = nullptr;

    /** 상태 목록 */
    TArray<FAnimationState> States;

    /** 전이 목록 */
    TArray<FStateTransition> Transitions;

    /** 현재 상태 */
    FName CurrentStateName;

    /** 애니메이션을 적용시킬 layer */
    EAnimLayer TargetLayer = EAnimLayer::Base;
};
