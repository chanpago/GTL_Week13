#include "pch.h"
#include "ConstraintInstance.h"
#include "PhysScene.h"
#include "BodyInstance.h"
#include "PhysicsTypes.h"



using namespace physx;

void FConstraintInstance::InitD6(FPhysScene& World, const FTransform& ParentFrame, const FTransform& ChildFrame, const FConstraintLimitData& Limits)
{
    // 둘다 할당되어있지 않다면 리턴
    if (!ParentBody || !ChildBody)
    {
        return;
    }
    PxPhysics* Physics = World.GetPhysics();

    // Physics 못가져오면 리턴
    if (!Physics)
    {
        return;
    }
    
    PxScene*   Scene   = World.GetScene();

    // Scene 못가져오면 리턴
    if (!Scene)
    {
        return;
    }

    
    // PxD6JointCreate 같은 거 여기서 사용
    PxRigidActor* ParentActor = ParentBody->RigidActor;
    PxRigidActor* ChildActor  = ChildBody->RigidActor;
    if (!ParentActor || !ChildActor)
    {
        return;
    }
    
    PxTransform ParentLocal = ToPx(ParentFrame);
    PxTransform ChildLocal  = ToPx(ChildFrame);

    
    // PxD6JointCreate의 PxD6Joint* 를 리턴함. 기존의 Joint* 는 
    Joint = PxD6JointCreate(*Physics, ParentActor, ParentLocal, ChildActor, ChildLocal);


    // 선형축 제한 위치는 고정
    // 부모/자식 바디 중심이 서로 떨어져 나가거나 미끄러지지 않게 위치 고정
    Joint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
    Joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
    Joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
    
    // 회전 축 제한
    // PxD6Joint는 “6 자유도” 조인트
    // PxD6Axis::eTWIST = 부모-자식 바디를 잇느 "조인트 x축" 기준회전 쉽게말해 비틀기 회전 ex) 
    // 정확한 축 방향은 조인트를 만들 때 넘겨준 로컬 프레임(ParentFrame/ChildFrame)의 X/Y/Z축에 의해 결정
    // PxD6Joint::setMotion(axis, motion)에서 두 번째 파라미터 PxD6Motion은 “이 축은 어떻게 움직이게 할 건지” 지정하는 것
    Joint->setMotion(PxD6Axis::eTWIST,  PxD6Motion::eLIMITED);
    Joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLIMITED);
    Joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLIMITED);


    // Limits를 통해서 각도제한 
    PxJointAngularLimitPair Twist(Limits.TwistMin, Limits.TwistMax);
    PxJointLimitCone       Swing(Limits.Swing1, Limits.Swing2);

    if (Limits.bSoftLimit)
    {
        Twist.stiffness = Limits.Stiffness;
        Twist.damping   = Limits.Damping;
        Swing.stiffness = Limits.Stiffness;
        Swing.damping   = Limits.Damping;
    }

    Joint->setTwistLimit(Twist);
    Joint->setSwingLimit(Swing);

    // ★ 여기서 "연결된 두 Body는 서로 충돌할지" 결정
    // 기본은 ragdoll에서 자기 관절끼리는 충돌 OFF가 일반적
    // bEnableCollision = false면 같은 조인트에 연결된 두 콜라이더끼리는 충돌 안 함
    Joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, Limits.bEnableCollision);
}

void FConstraintInstance::Terminate(FPhysScene& World)
{
    if (Joint)
    {
        Joint->release();
        Joint = nullptr;
    }
}
