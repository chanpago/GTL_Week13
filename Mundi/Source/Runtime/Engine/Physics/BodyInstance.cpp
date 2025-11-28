#include "pch.h"
#include "BodyInstance.h"
#include "PhysScene.h"          // FPhysScene 선언
#include "PhysicsTypes.h"       // ToPx / FromPx (FVector/FTransform <-> Px 타입 변환)


using namespace physx;

// -------------------------
// Dynamic Body 초기화
// -------------------------
void FBodyInstance::InitDynamic(FPhysScene& World, const FTransform& WorldTransform, float Mass)
{
    // 이미 RigidActor가 존재하면 정리
    if (RigidActor)
    {
        Terminate(World);
    }

    PxPhysics*      Physics     = World.GetPhysics();
    PxScene*        Scene       = World.GetScene();
    PxMaterial*     Material    = World.GetDefaultMaterial();

    if (!Physics || !Scene || !Material)
    {
        return;
    }

    // 엔진 Transform(FTransform) -> PhysX Transform(PxTransform)
    PxTransform Pose = ToPx(WorldTransform);

    // Dynamic Body 생성
    PxRigidDynamic* DynamicActor = Physics->createRigidDynamic(Pose);
    if (!DynamicActor)
    {
        return;
    }

    // 일단 지금은 박스 콜라이더로 세팅
    // TODO: 추후 BodySetup->AggGeom기반으로 여러 shape 생성할 것
    PxBoxGeometry BoxGeom(0.5f, 0.5f, 0.5f); // half extents (임시값)
    PxShape* Shape = Physics->createShape(BoxGeom, *Material);
    if (Shape)
    {
        DynamicActor->attachShape(*Shape);
        // Actor가 내부적으로 ref 카운트를 들고 있으므로, 외부 참조는 해제해도 됨
        Shape->release();
    }

    // 질량 / 관성 텐서 설정
    PxRigidBodyExt::updateMassAndInertia(*DynamicActor, Mass);

    // 씬에 등록
    Scene->addActor(*DynamicActor);

    // FBodyInstance와 연결
    RigidActor           = DynamicActor;
    RigidActor->userData = this; // 나중에 콜백에서 FBodyInstance로 되돌리기 용
    
}

// -------------------------
// Static Body 초기화
// -------------------------
void FBodyInstance::InitStatic(FPhysScene& World, const FTransform& WorldTransform)
{
    // 이미 바디가 존재하면 정리
    if (RigidActor)
    {
        Terminate(World);
    }

    PxPhysics*   Physics   = World.GetPhysics();
    PxScene*     Scene     = World.GetScene();
    PxMaterial*  Material  = World.GetDefaultMaterial();

    if (!Physics || !Scene || !Material)
        return;

    PxTransform Pose = ToPx(WorldTransform);

    // Static 바디 생성
    PxRigidStatic* StaticActor = Physics->createRigidStatic(Pose);
    if (!StaticActor)
    {
        return;
    }
    
    // TODO: 마찬가지로 나중에 BodySetup->AggGeom 기반으로 생성
    {
        PxBoxGeometry BoxGeom(0.5f, 0.5f, 0.5f);
        PxShape* Shape = Physics->createShape(BoxGeom, *Material);
        if (Shape)
        {
            StaticActor->attachShape(*Shape);
            Shape->release();
        }
    }

    Scene->addActor(*StaticActor);

    RigidActor           = StaticActor;
    RigidActor->userData = this;
}

void FBodyInstance::Terminate(FPhysScene& World)
{
    if (!RigidActor)
    {
        return;
    }

    
    PxScene* Scene = World.GetScene();
    if (Scene)
    {
        // 씬에서 제거
        Scene->removeActor(*RigidActor);
    }

    // Actor 자체 Release
    RigidActor->release();
    RigidActor = nullptr;
}

void FBodyInstance::AddForce(const FVector& Force)
{
    if (!RigidActor)
    {
        return;
    }
    
    // Dynamic인지 체크
    PxRigidDynamic* DynamicActor = RigidActor->is<PxRigidDynamic>();
    if (!DynamicActor)
    {
        return;
    }
    
    PxVec3 PxForce = ToPx(Force);
    DynamicActor->addForce(PxForce, PxForceMode::eFORCE);
}

FTransform FBodyInstance::GetWorldTransform() const
{
    if (!RigidActor)
    {
        return FTransform(); // 기본 생성자 = Identity라고 가정
    }

    PxTransform Pose = RigidActor->getGlobalPose();
    return FromPx(Pose);
}
