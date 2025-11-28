#include "pch.h"
#include "ConstraintInstance.h"
#include "PhysScene.h"
#include "BodyInstance.h"
#include "PhysicsTypes.h"



using namespace physx;

void FConstraintInstance::InitD6(FPhysScene& World, const FTransform& ParentFrame, const FTransform& ChildFrame, const FConstraintLimitData& Limits)
{
    PxPhysics* physics = World.GetPhysics();
    PxScene*   scene   = World.GetScene();

    // PxD6JointCreate 같은 거 여기서 사용
    PxRigidActor* ParentActor = ParentBody->RigidActor;
    PxRigidActor* ChildActor  = ChildBody->RigidActor;

    PxTransform ParentLocal = ToPx(ParentFrame);
    PxTransform ChildLocal  = ToPx(ChildFrame);

    Joint = PxD6JointCreate(*physics, ParentActor, ParentLocal, ChildActor, ChildLocal);

    // limit 세팅 등등...
}