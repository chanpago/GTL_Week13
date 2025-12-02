#pragma once
#include <PxPhysicsAPI.h>
#include "Delegates.h"

struct FHitResult;

using namespace physx;
using namespace DirectX;

class AActor;
struct FContactHit
{
    AActor* ActorTo = nullptr;
    AActor* ActorFrom = nullptr;
    FVector Position;
    FVector Normal;
    FVector ImpulseOnActorTo; // ImpulseOnActorFrom은 - 해주면 됨
};

struct FTriggerHit
{
    AActor* TriggerActor = nullptr; // Trigger 역할 하는 액터
    AActor* OtherActor = nullptr;   // Trigger에 들어온 / 나간 액터
    bool bIsEnter = false;
};

// 물리 데이터 접근 시 Thread-Safe Lock 매크로
#define SCOPED_PHYSX_READ_LOCK(scene) PxSceneReadLock scopedReadLock(scene)
#define SCOPED_PHYSX_WRITE_LOCK(scene) PxSceneWriteLock scopedWriteLock(scene)

// PhysX Assert를 로그로 출력하는 커스텀 핸들러
class FPhysXAssertHandler : public PxAssertHandler
{
public:
    virtual void operator()(const char* exp, const char* file, int line, bool& ignore) override
    {
        UE_LOG("[PhysX ASSERT FAILED] Expression: %s", exp);
        UE_LOG("[PhysX ASSERT FAILED] File: %s, Line: %d", file, line);
        // ignore = true로 설정하면 assert를 무시하고 계속 진행
        // ignore = false면 기본 동작 (크래시)
        ignore = true;  // 일단 무시하고 계속 진행하도록
    }
};

class FSimulationEventCallback;
class FPhysScene
{
public:
    struct GameObject
    {
        PxRigidDynamic* rigidBody = nullptr;
        XMMATRIX        worldMatrix = XMMatrixIdentity();

        //void UpdateFromPhysics();
    };

    DECLARE_DELEGATE(OnContactDelegate, FContactHit);
    DECLARE_DELEGATE(OnTriggerDelegate, FTriggerHit);

public:
    FPhysScene();
    ~FPhysScene();

    bool Initialize();         // PhysX 초기화
    void Shutdown();           // PhysX 리소스 해제

    /*
     * @brief physX 내부의 진짜 물리엔진 씬 = PxScene* Scene
     *          이걸 이용해서 매 프레임 Scene->simulate(dt) 한다
     *          PhysScene은 그저 통합 매니저이다.
     *
     *          simultate(dt) 호출 -> PhysX(PxScene)가 내부에 등록된
     *          모든 RigidActor/Shape를 가지고  충돌검사
     */
    void StepSimulation(float dt);                 // 매 프레임 시뮬레이션 (non-blocking)
    bool IsSimulationComplete() const;             // 시뮬레이션 완료 여부 확인
    void WaitForSimulation();                      // 시뮬레이션 완료 대기
    GameObject& CreateBox(const PxVec3& pos, const PxVec3& halfExtents); // 테스트용 박스 생성

    const std::vector<GameObject>& GetObjects() const;
    std::vector<GameObject>&       GetObjects();

    PxPhysics*              GetPhysics()         const;
    PxScene*                GetScene()           const;
    PxMaterial*             GetDefaultMaterial() const;
    PxDefaultCpuDispatcher* GetDispatcher()      const;

    // ===== Sweep Query =====
    /**
     * @brief 캡슐로 Sweep하여 Static 콜라이더와 충돌 검사
     * @param Start 시작 위치
     * @param End 끝 위치
     * @param Radius 캡슐 반지름
     * @param HalfHeight 캡슐 반높이
     * @param OutHit 충돌 결과
     * @param IgnoreActor 무시할 액터 (자기 자신 등)
     * @return 충돌 여부
     */
    bool SweepCapsule(
        const FVector& Start,
        const FVector& End,
        float Radius,
        float HalfHeight,
        FHitResult& OutHit,
        AActor* IgnoreActor = nullptr
    ) const;

    /**
     * @brief 박스로 Sweep하여 Static 콜라이더와 충돌 검사
     */
    bool SweepBox(
        const FVector& Start,
        const FVector& End,
        const FVector& HalfExtents,
        const FQuat& Rotation,
        FHitResult& OutHit,
        AActor* IgnoreActor = nullptr
    ) const;

    /**
     * @brief 스피어로 Sweep하여 Static 콜라이더와 충돌 검사
     */
    bool SweepSphere(
        const FVector& Start,
        const FVector& End,
        float Radius,
        FHitResult& OutHit,
        AActor* IgnoreActor = nullptr
    ) const;

private:
    PxDefaultAllocator      Allocator;
    PxDefaultErrorCallback  ErrorCallback;
    FPhysXAssertHandler     AssertHandler;  // 커스텀 Assert 핸들러
    PxFoundation*           Foundation      = nullptr;
    PxPvd*                  Pvd             = nullptr;  // PhysX Visual Debugger
    PxPvdTransport*         PvdTransport    = nullptr;
    PxPhysics*              Physics         = nullptr;
    PxScene*                Scene           = nullptr;
    PxMaterial*             DefaultMaterial = nullptr;
    PxDefaultCpuDispatcher* Dispatcher      = nullptr;

    std::vector<GameObject> Objects; // 간단 테스트용

    bool bSimulating = false;  // 시뮬레이션 진행 중 여부
    FSimulationEventCallback* SimulationEventCallback = nullptr;
};