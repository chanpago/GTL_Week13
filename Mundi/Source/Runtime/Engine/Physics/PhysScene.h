#pragma once
#include <PxPhysicsAPI.h>


using namespace physx;
using namespace DirectX;

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

class FPhysScene
{
public:
    struct GameObject
    {
        PxRigidDynamic* rigidBody = nullptr;
        XMMATRIX        worldMatrix = XMMatrixIdentity();

        //void UpdateFromPhysics();
    };

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
};
