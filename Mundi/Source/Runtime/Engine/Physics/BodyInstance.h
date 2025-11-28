#pragma once


class UPrimitiveComponent;
class UBodySetup;
struct FPhysScene;

// PhysX의 PxRigidActor 전방 선언
namespace physx
{
    class PxRigidActor;
}


/**
 * @brief 엔진에서 "물리 바디 한 개"를 대표하는 런타임 객체, 내부에 PxRigidDynamic / PxRigidStatic을 들고있고 OwnerComponent 포인터도 들고있다.
 */

struct FBodyInstance
{
    void InitDynamic(FPhysScene& World, const FTransform& WorldTransform, float Mass);
    void InitStatic(FPhysScene& World, const FTransform& WorldTransform);

    void Terminate(FPhysScene& World);

    void AddForce(const FVector& Force);
    FTransform GetWorldTransform() const;

public:
    UPrimitiveComponent*        OwnerComponent = nullptr;
    UBodySetup*                 BodySetup      = nullptr;
    physx::PxRigidActor*        RigidActor     = nullptr;
};
