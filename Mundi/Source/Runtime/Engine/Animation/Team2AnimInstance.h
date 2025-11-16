#pragma once
#include "AnimInstance.h"

// Forward declarations
class UAnimSequence;
class UAnimationStateMachine;
class USkeletalMeshComponent;

/**
 * @brief Team2 애니메이션 인스턴스
 * - Walk/Run 상태머신을 구현한 AnimInstance
 * - 이동 속도에 따라 Walk <-> Run 자동 전환
 *
 * @example 사용법:
 *
 * // BeginPlay에서 스켈레탈 메시 컴포넌트 설정
 * SkeletalMeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
 * UTeam2AnimInstance* AnimInst = NewObject<UTeam2AnimInstance>();
 * SkeletalMeshComp->SetAnimInstance(AnimInst);
 *
 * // 게임플레이 중 이동 속도 업데이트
 * AnimInst->SetMovementSpeed(PlayerVelocity.Length());
 */
class UTeam2AnimInstance : public UAnimInstance
{
    DECLARE_CLASS(UTeam2AnimInstance, UAnimInstance)

public:
    UTeam2AnimInstance() = default;
    virtual ~UTeam2AnimInstance() = default;

    /**
     * @brief AnimInstance 초기화
     * - 애니메이션 시퀀스 로드
     * - 상태머신 구축
     * @param InComponent 소유 스켈레탈 메시 컴포넌트
     */
    virtual void Initialize(USkeletalMeshComponent* InComponent) override;

    /**
     * @brief 매 프레임 애니메이션 업데이트
     * - UpdateParameters로 파라미터 업데이트
     * - Super::NativeUpdateAnimation으로 상태머신 처리 및 포즈 계산
     * @param DeltaSeconds 프레임 시간
     */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
    /**
     * @brief 상태머신 구축
     * - Walk, Run 상태 추가
     * - Walk <-> Run 전이 조건 설정
     */
    void BuildStateMachine();

    /**
     * @brief 애니메이션 파라미터 업데이트
     * - 이동 속도, 이동 여부 등 파라미터 업데이트
     * @param DeltaSeconds 프레임 시간
     */
    void UpdateParameters(float DeltaSeconds);

private:
    /** Idle 애니메이션 시퀀스 */
    UAnimSequence* IdleSequence = nullptr;

    /** Walk 애니메이션 시퀀스 */
    UAnimSequence* WalkSequence = nullptr;

    /** Run 애니메이션 시퀀스 */
    UAnimSequence* RunSequence = nullptr;

    /** 상태머신 */
    UAnimationStateMachine* StateMachine = nullptr;

    /** Walk로 전환되는 속도 임계값 */
    float WalkThreshold = 0.1f;

    /** Run으로 전환되는 속도 임계값 */
    float RunThreshold = 5.0f;
};
