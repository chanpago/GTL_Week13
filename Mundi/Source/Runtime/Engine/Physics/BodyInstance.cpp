#include "pch.h"
#include "BodyInstance.h"
#include "BodySetup.h"
#include "PhysScene.h"          // FPhysScene 선언
#include "PhysicsTypes.h"       // ToPx / FromPx (FVector/FTransform <-> Px 타입 변환)
#include "PhysicalMaterial.h"   // UPhysicalMaterial
#include "../Components/PrimitiveComponent.h"

using namespace physx;

// -------------------------
// Dynamic Body 초기화
// -------------------------
// ECombineMode를 PxCombineMode로 변환하는 헬퍼 함수
static PxCombineMode::Enum ToPxCombineMode(ECombineMode Mode)
{
    switch (Mode)
    {
    case ECombineMode::Average:  return PxCombineMode::eAVERAGE;
    case ECombineMode::Min:      return PxCombineMode::eMIN;
    case ECombineMode::Multiply: return PxCombineMode::eMULTIPLY;
    case ECombineMode::Max:      return PxCombineMode::eMAX;
    default:                     return PxCombineMode::eMULTIPLY;
    }
}

static void SetShapeCollisionFlags(PxShape* Shape, UPrimitiveComponent* OwnerComponent, UBodySetup* BodySetup)
{
    if (!Shape || !OwnerComponent || !BodySetup)
    {
        return;
    }

    ECollisionState CollisionStateToUse = BodySetup->CollisionState;
    if (OwnerComponent->bOverrideCollisionSetting)
    {
        CollisionStateToUse = OwnerComponent->CollisionEnabled;
    }

    switch (CollisionStateToUse)
    {
        case ECollisionState::NoCollision:
            Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
            Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
            break;
        case ECollisionState::QueryOnly:
            Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
            Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
            break;
        case ECollisionState::QueryAndPhysics:
            Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
            Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
            break;
    }
}

void FBodyInstance::InitDynamic(FPhysScene& World, const FTransform& WorldTransform, float Mass, const FVector& Scale3D, uint32 OwnerID)
{
    // 이미 RigidActor가 존재하면 정리
    if (RigidActor)
    {
        Terminate(World);
    }

    PxPhysics*      Physics     = World.GetPhysics();
    PxScene*        Scene       = World.GetScene();

    if (!Physics || !Scene)
    {
        return;
    }

    // BodySetup의 PhysMaterial이 있으면 사용, 없으면 DefaultMaterial 사용
    PxMaterial* Material = nullptr;
    if (BodySetup && BodySetup->PhysMaterial)
    {
        UPhysicalMaterial* PhysMat = BodySetup->PhysMaterial;
        Material = Physics->createMaterial(
            PhysMat->StaticFriction,
            PhysMat->DynamicFriction,
            PhysMat->Restitution
        );

        // Combine Mode 설정 (언리얼 스타일 우선순위 기반, 기본값 Multiply)
        Material->setFrictionCombineMode(ToPxCombineMode(PhysMat->FrictionCombineMode));
        Material->setRestitutionCombineMode(ToPxCombineMode(PhysMat->RestitutionCombineMode));
    }
    else
    {
        Material = World.GetDefaultMaterial();
    }

    if (!Material)
    {
        return;
    }

    // BodySetup에서 물리 속성 가져오기
    float ActualMass = Mass;
    float LinearDamping = 0.01f;
    float AngularDamping = 0.05f;
    bool bEnableGravity = true;

    if (BodySetup)
    {
        ActualMass = BodySetup->Mass;
        LinearDamping = BodySetup->LinearDamping;
        AngularDamping = BodySetup->AngularDamping;
        bEnableGravity = BodySetup->bEnableGravity;
    }

    // 엔진 Transform(FTransform) -> PhysX Transform(PxTransform)
    PxTransform Pose = ToPx(WorldTransform);

    // Dynamic Body 생성
    PxRigidDynamic* DynamicActor = Physics->createRigidDynamic(Pose);
    if (!DynamicActor)
    {
        return;
    }

    // Damping 설정
    DynamicActor->setLinearDamping(LinearDamping);
    DynamicActor->setAngularDamping(AngularDamping);

    // 중력 설정
    DynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bEnableGravity);

    // Self-Collision 방지를 위한 FilterData 설정
    // word0: OwnerID (같은 스켈레탈 메쉬 = 같은 ID → 서로 충돌 안 함)
    PxFilterData FilterData;
    FilterData.word0 = OwnerID;
    FilterData.word1 = 0xFFFFFFFF; // TODO : 모든 그룹과 충돌 (Self 제외)
    FilterData.word2 = 0;
    FilterData.word3 = 0;

     // ★★★★★ 여기부터가 핵심: BodySetup->AggGeom 기반으로 Shape 생성
    if (BodySetup)
    {
        const FKAggregateGeom& Agg = BodySetup->AggGeom;

        // 스케일의 절대값 사용 (음수 스케일 대응)
        const FVector AbsScale(std::fabs(Scale3D.X), std::fabs(Scale3D.Y), std::fabs(Scale3D.Z));

        // 1) 스피어들
        for (const FKSphereElem& Sphere : Agg.SphereElements)
        {
            // 스피어 중심도 스케일 적용
            FVector ScaledCenter = Sphere.Center * Scale3D;
            FTransform LocalXform(ScaledCenter, FQuat::Identity(), FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            // 균등 스케일이 아닌 경우 가장 큰 축 사용
            float MaxScale = std::max({AbsScale.X, AbsScale.Y, AbsScale.Z});
            PxSphereGeometry Geom(Sphere.Radius * MaxScale);
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                SetShapeCollisionFlags(Shape, OwnerComponent, BodySetup);
                Shape->setLocalPose(LocalPose);
                Shape->setSimulationFilterData(FilterData);  // Self-Collision 방지
                DynamicActor->attachShape(*Shape);
                Shape->release();
            }
        }

        // 2) 박스들
        for (const FKBoxElem& Box : Agg.BoxElements)
        {
            // 박스 중심도 스케일 적용
            FVector ScaledCenter = Box.Center * Scale3D;
            FTransform LocalXform(ScaledCenter, Box.Rotation, FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            // 박스 Extents에 스케일 적용
            PxBoxGeometry Geom(
                Box.Extents.X * AbsScale.X,
                Box.Extents.Y * AbsScale.Y,
                Box.Extents.Z * AbsScale.Z
            );
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                SetShapeCollisionFlags(Shape, OwnerComponent, BodySetup);
                Shape->setLocalPose(LocalPose);
                Shape->setSimulationFilterData(FilterData);  // Self-Collision 방지
                DynamicActor->attachShape(*Shape);
                Shape->release();
            }
        }

        // 3) 캡슐(Capsule)들
        for (const FKCapsuleElem& Capsule : Agg.CapsuleElements)
        {
            // 캡슐 중심도 스케일 적용
            FVector ScaledCenter = Capsule.Center * Scale3D;

            // Z축 캡슐 → PhysX X축 캡슐로 보정
            FQuat ZToX = FQuat::FromAxisAngle(FVector(0, 1, 0), -XM_PIDIV2);
            FQuat PhysRot = Capsule.Rotation * ZToX;

            FTransform LocalXform(ScaledCenter, PhysRot, FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            // 캡슐: 반지름은 XY 평균, 높이는 Z 스케일 적용
            float RadiusScale = (AbsScale.X + AbsScale.Y) * 0.5f;
            float HeightScale = AbsScale.Z;

            PxCapsuleGeometry Geom(Capsule.Radius * RadiusScale, Capsule.HalfLength * HeightScale);

            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                SetShapeCollisionFlags(Shape, OwnerComponent, BodySetup);
                Shape->setLocalPose(LocalPose);
                Shape->setSimulationFilterData(FilterData);  // Self-Collision 방지
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
            Shape->setSimulationFilterData(FilterData);  // Self-Collision 방지
            SetShapeCollisionFlags(Shape, OwnerComponent, BodySetup);
            DynamicActor->attachShape(*Shape);
            Shape->release();
        }
    }

    // 질량 / 관성 텐서 설정
    PxRigidBodyExt::updateMassAndInertia(*DynamicActor, ActualMass);

    // 씬에 등록
    Scene->addActor(*DynamicActor);

    // FBodyInstance와 연결
    RigidActor           = DynamicActor;
    RigidActor->userData = this; // 나중에 콜백에서 FBodyInstance로 되돌리기 용

    // BodySetup에서 생성한 Material은 release (DefaultMaterial은 PhysScene이 관리)
    if (BodySetup && BodySetup->PhysMaterial)
    {
        Material->release();
    }
}

// -------------------------
// Static Body 초기화
// -------------------------
void FBodyInstance::InitStatic(FPhysScene& World, const FTransform& WorldTransform, const FVector& Scale3D, uint32 OwnerID)
{
    // 이미 바디가 존재하면 정리
    if (RigidActor)
    {
        Terminate(World);
    }

    PxPhysics*   Physics   = World.GetPhysics();
    PxScene*     Scene     = World.GetScene();

    if (!Physics || !Scene)
        return;

    // BodySetup의 PhysMaterial이 있으면 사용, 없으면 DefaultMaterial 사용
    PxMaterial* Material = nullptr;
    if (BodySetup && BodySetup->PhysMaterial)
    {
        UPhysicalMaterial* PhysMat = BodySetup->PhysMaterial;
        Material = Physics->createMaterial(
            PhysMat->StaticFriction,
            PhysMat->DynamicFriction,
            PhysMat->Restitution
        );

        // Combine Mode 설정 (언리얼 스타일 우선순위 기반, 기본값 Multiply)
        Material->setFrictionCombineMode(ToPxCombineMode(PhysMat->FrictionCombineMode));
        Material->setRestitutionCombineMode(ToPxCombineMode(PhysMat->RestitutionCombineMode));
    }
    else
    {
        Material = World.GetDefaultMaterial();
    }

    if (!Material)
    {
        return;
    }

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

        // 스케일의 절대값 사용 (음수 스케일 대응)
        const FVector AbsScale(std::fabs(Scale3D.X), std::fabs(Scale3D.Y), std::fabs(Scale3D.Z));

        // 1) 스피어들
        for (const FKSphereElem& Sphere : Agg.SphereElements)
        {
            // 스피어 중심도 스케일 적용
            FVector ScaledCenter = Sphere.Center * Scale3D;
            FTransform LocalXform(ScaledCenter, FQuat::Identity(), FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            // 균등 스케일이 아닌 경우 가장 큰 축 사용
            float MaxScale = std::max({AbsScale.X, AbsScale.Y, AbsScale.Z});
            PxSphereGeometry Geom(Sphere.Radius * MaxScale);
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                SetShapeCollisionFlags(Shape, OwnerComponent, BodySetup);
                Shape->setLocalPose(LocalPose);
                StaticActor->attachShape(*Shape);
                Shape->release();
            }
        }

        // 2) 박스들
        for (const FKBoxElem& Box : Agg.BoxElements)
        {
            // 박스 중심도 스케일 적용
            FVector ScaledCenter = Box.Center * Scale3D;
            FTransform LocalXform(ScaledCenter, Box.Rotation, FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            // 박스 Extents에 스케일 적용
            PxBoxGeometry Geom(
                Box.Extents.X * AbsScale.X,
                Box.Extents.Y * AbsScale.Y,
                Box.Extents.Z * AbsScale.Z
            );
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                SetShapeCollisionFlags(Shape, OwnerComponent, BodySetup);
                Shape->setLocalPose(LocalPose);
                StaticActor->attachShape(*Shape);
                Shape->release();
            }
        }

        // 3) 캡슐(Capsule)들
        for (const FKCapsuleElem& Capsule : Agg.CapsuleElements)
        {
            // 캡슐 중심도 스케일 적용
            FVector ScaledCenter = Capsule.Center * Scale3D;

            // Z축 캡슐 → PhysX X축 캡슐로 보정
            FQuat ZToX = FQuat::FromAxisAngle(FVector(0, 1, 0), -XM_PIDIV2);
            FQuat PhysRot = Capsule.Rotation * ZToX;

            FTransform LocalXform(ScaledCenter, PhysRot, FVector(1,1,1));
            PxTransform LocalPose = ToPx(LocalXform);

            // 캡슐: 반지름은 XY 평균, 높이는 Z 스케일 적용
            float RadiusScale = (AbsScale.X + AbsScale.Y) * 0.5f;
            float HeightScale = AbsScale.Z;

            PxCapsuleGeometry Geom(Capsule.Radius * RadiusScale, Capsule.HalfLength * HeightScale);
            PxShape* Shape = Physics->createShape(Geom, *Material);
            if (Shape)
            {
                SetShapeCollisionFlags(Shape, OwnerComponent, BodySetup);
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
            SetShapeCollisionFlags(Shape, OwnerComponent, BodySetup);
            StaticActor->attachShape(*Shape);
            Shape->release();
        }
    }

    Scene->addActor(*StaticActor);

    RigidActor           = StaticActor;
    RigidActor->userData = this;

    // BodySetup에서 생성한 Material은 release (DefaultMaterial은 PhysScene이 관리)
    if (BodySetup && BodySetup->PhysMaterial)
    {
        Material->release();
    }
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