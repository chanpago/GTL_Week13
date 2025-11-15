#pragma once
#include "AnimSequence.h"

// Forward declarations
class USkeletalMeshComponent;
class USkeleton;

/**
 * @brief 애니메이션 재생 상태를 관리하는 구조체
 */
struct FAnimationPlayState
{
    UAnimSequence* Sequence = nullptr;
    float CurrentTime = 0.0f;
    float PlayRate = 1.0f;
    float BlendWeight = 1.0f;
    bool bIsLooping = false;
    bool bIsPlaying = false;
};

/**
 * @brief 애니메이션 인스턴스
 * - 스켈레탈 메시 컴포넌트의 애니메이션 재생을 관리
 * - 시퀀스 재생, 블렌딩, 노티파이 처리 등을 담당
 */
class UAnimInstance : public UObject
{
    DECLARE_CLASS(UAnimInstance, UObject)

public:
    UAnimInstance() = default;
    virtual ~UAnimInstance() = default;

    // ============================================================
    // Initialization & Setup
    // ============================================================

    /**
     * @brief AnimInstance 초기화
     * @param InComponent 소유 컴포넌트
     */
    void Initialize(USkeletalMeshComponent* InComponent);

    // ============================================================
    // Update & Pose Evaluation
    // ============================================================

    /**
     * @brief 애니메이션 업데이트 (매 프레임 호출)
     * @param DeltaSeconds 프레임 시간
     */
    virtual void NativeUpdateAnimation(float DeltaSeconds);

    /**
     * @brief 현재 포즈를 평가하여 반환
     * @param OutPose 출력 포즈
     */
    void EvaluatePose(TArray<FTransform>& OutPose);

    // ============================================================
    // Playback API
    // ============================================================

    /**
     * @brief 애니메이션 시퀀스 재생
     * @param Sequence 재생할 시퀀스
     * @param bLoop 루프 여부
     * @param InPlayRate 재생 속도
     */
    void PlaySequence(UAnimSequence* Sequence, bool bLoop = true, float InPlayRate = 1.0f);

    /**
     * @brief 현재 재생 중인 시퀀스 정지
     */
    void StopSequence();

    /**
     * @brief 다른 시퀀스로 블렌드
     * @param Sequence 블렌드할 시퀀스
     * @param BlendTime 블렌드 시간
     */
    void BlendTo(UAnimSequence* Sequence, float BlendTime);

    /**
     * @brief 재생 중인지 확인
     */
    bool IsPlaying() const { return CurrentPlayState.bIsPlaying; }

    /**
     * @brief 현재 재생 중인 시퀀스 반환
     */
    UAnimSequence* GetCurrentSequence() const { return CurrentPlayState.Sequence; }

    // ============================================================
    // Notify & Curve Processing
    // ============================================================

    /**
     * @brief 애니메이션 노티파이 트리거
     * @param DeltaSeconds 프레임 시간
     */
    void TriggerAnimNotifies(float DeltaSeconds);

    /**
     * @brief 애니메이션 커브 값 업데이트
     */
    void UpdateAnimationCurves();

    // ============================================================
    // Getters
    // ============================================================

    USkeletalMeshComponent* GetOwningComponent() const { return OwningComponent; }

protected:
    // 소유 컴포넌트
    USkeletalMeshComponent* OwningComponent = nullptr;

    // 현재 재생 상태
    FAnimationPlayState CurrentPlayState;

    // 블렌딩 상태 (향후 확장용)
    FAnimationPlayState BlendTargetState;
    float BlendTimeRemaining = 0.0f;
    float BlendTotalTime = 0.0f;

    // 이전 프레임 재생 시간 (노티파이 검출용)
    float PreviousPlayTime = 0.0f;

    // 스켈레톤 참조
    USkeleton* CurrentSkeleton = nullptr;

    // 향후 확장: 상태머신
    // UAnimationStateMachine* AnimSM = nullptr;
};
