#pragma once
#include <PxPhysicsAPI.h>


using namespace physx;
using namespace DirectX;

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
    void StepSimulation(float dt);                 // 매 프레임 시뮬레이션
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
    PxFoundation*           Foundation      = nullptr;
    PxPvd*                  Pvd             = nullptr;  // PhysX Visual Debugger
    PxPvdTransport*         PvdTransport    = nullptr;
    PxPhysics*              Physics         = nullptr;
    PxScene*                Scene           = nullptr;
    PxMaterial*             DefaultMaterial = nullptr;
    PxDefaultCpuDispatcher* Dispatcher      = nullptr;

    std::vector<GameObject> Objects; // 간단 테스트용

};
