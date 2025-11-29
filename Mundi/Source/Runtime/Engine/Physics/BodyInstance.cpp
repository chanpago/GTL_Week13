#include "pch.h"
#include "BodyInstance.h"
#include "BodySetup.h"
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

     // ★★★★★ 여기부터가 핵심: BodySetup->AggGeom 기반으로 Shape 생성
    if (BodySetup)
    {
        const FKAggregateGeom& Agg = BodySetup->AggGeom;

        // 1) 스피어들
        for (const FKSphereElem& Sphere : Agg.SphereElements)
        {
            FTransform LocalXform(Sphere.Center, FQuat::Identity(), FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            PxSphereGeometry Geom(Sphere.Radius);
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                Shape->setLocalPose(LocalPose);
                DynamicActor->attachShape(*Shape);
                Shape->release();
            }
        }

        // 2) 박스들
        for (const FKBoxElem& Box : Agg.BoxElements)
        {
            FTransform LocalXform(Box.Center, Box.Rotation,FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            PxBoxGeometry Geom(Box.Extents.X, Box.Extents.Y, Box.Extents.Z);
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                Shape->setLocalPose(LocalPose);
                DynamicActor->attachShape(*Shape);
                Shape->release();
            }
        }

        // 3) 캡슐(Sphyl)들
        for (const FKSphylElem& Sphyl : Agg.SphylElements)
        {
            // 1) 엔진 기준: "본 로컬 Z축으로 뻗는 캡슐"이라고 생각하고 데이터 저장해 둔 상태
            FTransform LocalZCapsule(Sphyl.Center, Sphyl.Rotation, FVector(1,1,1));

            // 2) Z축 캡슐 → PhysX X축 캡슐로 보정
            //    +X -> +Z 로 보내는 회전 = Y축 기준 -90도
            FQuat ZToX = FQuat::FromAxisAngle(FVector(0, 1, 0), -XM_PIDIV2);

            // ★ 곱하는 순서는 엔진의 쿼터니언 convention에 따라 다를 수 있는데,
            //    보통 "기존 회전에 보정 회전을 추가" = Existing * Delta 로 많이 쓴다.
            FQuat PhysRot = Sphyl.Rotation * ZToX;

            
            // PhysX 캡슐 축은 X축 방향(±X) 기준
            // 우리 엔진에서 Z-up, X-forward 기준으로 Sphyl.Rotation을 이미 맞춰둔다고 가정
            FTransform LocalXform(Sphyl.Center, PhysRot, FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            float HalfHeight = Sphyl.HalfLength;

            // 이 부분에서 항상 로컬 x축 (+x)방향으로 길게 뻗는 캡슐임.
            // 우리엔진에서 캡슐의 길이는 z 방향이라고 생각(본이 z로 뻗는다고 생각)하기 때문에, 축변환을 해줘야함
            PxCapsuleGeometry Geom(Sphyl.Radius, HalfHeight);

            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                Shape->setLocalPose(LocalPose);
                DynamicActor->attachShape(*Shape);
                Shape->release();
            }
        }
    }
    else
    {
        // BodySetup이 없으면, 지금처럼 임시 박스 하나라도 붙여서 디버그용으로 사용
        PxBoxGeometry BoxGeom(0.5f, 0.5f, 0.5f);
        PxShape* Shape = Physics->createShape(BoxGeom, *Material);
        if (Shape)
        {
            DynamicActor->attachShape(*Shape);
            Shape->release();
        }
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
    
    // BodySetup->AggGeom 기반으로 Shape 생성
    if (BodySetup)
    {
        const FKAggregateGeom& Agg = BodySetup->AggGeom;

        // 1) 스피어들
        for (const FKSphereElem& Sphere : Agg.SphereElements)
        {
            FTransform LocalXform(Sphere.Center, FQuat::Identity(), FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            PxSphereGeometry Geom(Sphere.Radius);
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                Shape->setLocalPose(LocalPose);
                StaticActor->attachShape(*Shape);
                Shape->release();
            }
        }

        // 2) 박스들
        for (const FKBoxElem& Box : Agg.BoxElements)
        {
            FTransform LocalXform(Box.Center, Box.Rotation, FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            PxBoxGeometry Geom(Box.Extents.X, Box.Extents.Y, Box.Extents.Z);
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                Shape->setLocalPose(LocalPose);
                StaticActor->attachShape(*Shape);
                Shape->release();
            }
        }

        // 3) 캡슐(Sphyl)들
        for (const FKSphylElem& Sphyl : Agg.SphylElements)
        {
            // Z축 캡슐 → PhysX X축 캡슐로 보정
            FQuat ZToX = FQuat::FromAxisAngle(FVector(0, 1, 0), -XM_PIDIV2);
            FQuat PhysRot = Sphyl.Rotation * ZToX;

            FTransform LocalXform(Sphyl.Center, PhysRot, FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            PxCapsuleGeometry Geom(Sphyl.Radius, Sphyl.HalfLength);
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                Shape->setLocalPose(LocalPose);
                StaticActor->attachShape(*Shape);
                Shape->release();
            }
        }
    }
    else
    {
        // BodySetup이 없으면 임시 박스 하나 생성
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
