#pragma once

class FPhysScene;
class FBodyInstance;
struct FConstraintLimitData;

// PhysX의 PxJoint 전방 선언
namespace physx
{
    class PxJoint;
}


struct FConstraintInstance
{

public:
    void InitD6(FPhysScene& World, const FTransform& ParentFrame, const FTransform& ChildFrame, const FConstraintLimitData& Limits);

    void Terminate(FPhysScene& World);
    
public:
    FBodyInstance*          ParentBody   = nullptr;
    FBodyInstance*          ChildBody    = nullptr;
    physx::PxJoint*         Joint        = nullptr;  // 명시적으로 physx의 joint라고 선언, 이 후 cpp에서 <PxPhysicsAPI.h> include
};
