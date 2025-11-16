#include "pch.h"
#include "Team2AnimInstance.h"
#include "AnimSequence.h"
#include "AnimationStateMachine.h"
#include "SkeletalMeshComponent.h"
#include "ResourceManager.h"

// ============================================================
// Initialization
// ============================================================

IMPLEMENT_CLASS(UTeam2AnimInstance)

void UTeam2AnimInstance::Initialize(USkeletalMeshComponent* InComponent)
{
    // 부모 클래스 초기화 호출
    Super::Initialize(InComponent);

    UE_LOG("Team2AnimInstance::Initialize - Loading animation sequences...");

    // 애니메이션 시퀀스 로드
    IdleSequence = RESOURCE.Get<UAnimSequence>("Data/JamesIdle.fbx_mixamo.com");
    WalkSequence = RESOURCE.Get<UAnimSequence>("Data/JamesWalking.fbx_mixamo.com");
    RunSequence = RESOURCE.Get<UAnimSequence>("Data/JamesRunning.fbx_mixamo.com");

    // 로드 성공 여부 확인
    if (!IdleSequence)
    {
        UE_LOG("Team2AnimInstance::Initialize - Failed to load IdleSequence");
    }
    else
    {
        UE_LOG("Team2AnimInstance::Initialize - IdleSequence loaded successfully");
    }

    if (!WalkSequence)
    {
        UE_LOG("Team2AnimInstance::Initialize - Failed to load WalkSequence");
    }
    else
    {
        UE_LOG("Team2AnimInstance::Initialize - WalkSequence loaded successfully");
    }

    if (!RunSequence)
    {
        UE_LOG("Team2AnimInstance::Initialize - Failed to load RunSequence");
    }
    else
    {
        UE_LOG("Team2AnimInstance::Initialize - RunSequence loaded successfully");
    }

    // 애니메이션이 모두 로드되었으면 상태머신 구축
    if (IdleSequence && WalkSequence && RunSequence)
    {
        BuildStateMachine();
        UE_LOG("Team2AnimInstance::Initialize - StateMachine built successfully");
    }
    else
    {
        UE_LOG("Team2AnimInstance::Initialize - Failed to build StateMachine (missing sequences)");
    }
}

// ============================================================
// State Machine Setup
// ============================================================

void UTeam2AnimInstance::BuildStateMachine()
{
    // 상태머신이 이미 존재하거나 애니메이션이 없으면 중단
    if (StateMachine || !IdleSequence || !WalkSequence || !RunSequence)
    {
        return;
    }

    // 상태머신 생성 및 초기화
    StateMachine = NewObject<UAnimationStateMachine>();
    StateMachine->Initialize(this);

    constexpr float BlendTimeSeconds = 0.3f;

    // Idle 상태 추가 (루프, 1.0배속)
    StateMachine->AddState(FAnimationState("Idle", IdleSequence, true, 1.0f));

    // Walk 상태 추가 (루프, 1.0배속)
    StateMachine->AddState(FAnimationState("Walk", WalkSequence, true, 1.0f));

    // Run 상태 추가 (루프, 1.0배속)
    StateMachine->AddState(FAnimationState("Run", RunSequence, true, 1.0f));

    // Idle -> Walk 전이
    // 조건: 속도가 WalkThreshold(0.1) 이상이고 RunThreshold(5.0) 미만일 때
    // 블렌드 시간: 0.3초 (블렌드 전환)
    StateMachine->AddTransition(FStateTransition(
        "Idle", "Walk",
        [this]() { return GetMovementSpeed() >= WalkThreshold && GetMovementSpeed() < RunThreshold; },
        BlendTimeSeconds
    ));

    // Idle -> Run 전이
    // 조건: 속도가 RunThreshold(5.0) 이상일 때
    // 블렌드 시간: 0.3초 (블렌드 전환)
    StateMachine->AddTransition(FStateTransition(
        "Idle", "Run",
        [this]() { return GetMovementSpeed() >= RunThreshold; },
        BlendTimeSeconds
    ));

    // Walk -> Idle 전이
    // 조건: 속도가 WalkThreshold(0.1) 미만일 때
    // 블렌드 시간: 0.3초 (블렌드 전환)
    StateMachine->AddTransition(FStateTransition(
        "Walk", "Idle",
        [this]() { return GetMovementSpeed() < WalkThreshold; },
        BlendTimeSeconds
    ));

    // Walk -> Run 전이
    // 조건: 속도가 RunThreshold(5.0) 이상일 때
    // 블렌드 시간: 0.3초 (블렌드 전환)
    StateMachine->AddTransition(FStateTransition(
        "Walk", "Run",
        [this]() { return GetMovementSpeed() >= RunThreshold; },
        BlendTimeSeconds
    ));

    // Run -> Walk 전이
    // 조건: 속도가 WalkThreshold(0.1) 이상이고 RunThreshold(5.0) 미만일 때
    // 블렌드 시간: 0.3초 (블렌드 전환)
    StateMachine->AddTransition(FStateTransition(
        "Run", "Walk",
        [this]() { return GetMovementSpeed() >= WalkThreshold && GetMovementSpeed() < RunThreshold; },
        BlendTimeSeconds
    ));

    // Run -> Idle 전이
    // 조건: 속도가 WalkThreshold(0.1) 미만일 때
    // 블렌드 시간: 0.3초 (블렌드 전환)
    StateMachine->AddTransition(FStateTransition(
        "Run", "Idle",
        [this]() { return GetMovementSpeed() < WalkThreshold; },
        BlendTimeSeconds
    ));

    // 초기 상태를 Idle로 설정
    StateMachine->SetInitialState("Idle");

    // 상태머신을 AnimInstance에 연결
    SetStateMachine(StateMachine);

    UE_LOG("Team2AnimInstance::BuildStateMachine - Idle/Walk/Run state machine created");
}

// ============================================================
// Update
// ============================================================

void UTeam2AnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    // 파라미터 업데이트 먼저 수행
    UpdateParameters(DeltaSeconds);

    // 부모 클래스의 NativeUpdateAnimation 호출
    // 이 함수에서 상태머신의 ProcessState와 포즈 계산이 수행됨
    Super::NativeUpdateAnimation(DeltaSeconds);
}

void UTeam2AnimInstance::UpdateParameters(float DeltaSeconds)
{
    // 소유 컴포넌트가 없으면 중단
    if (!OwningComponent)
    {
        return;
    }

    // TODO: 실제 게임에서는 여기서 플레이어의 이동 속도를 가져와야 함
    // 예시:
    // AActor* Owner = OwningComponent->GetOwner();
    // if (Owner)
    // {
    //     FVector Velocity = Owner->GetVelocity();
    //     float Speed = Velocity.Length();
    //     SetMovementSpeed(Speed);
    //     SetIsMoving(Speed > 0.1f);
    // }

    // 현재는 외부에서 SetMovementSpeed를 호출하여 업데이트한다고 가정
    // 이동 여부는 속도가 0.1보다 크면 true
    SetIsMoving(GetMovementSpeed() > 0.1f);
}
