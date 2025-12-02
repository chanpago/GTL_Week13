#pragma once

#include "Source/Runtime/Engine/Physics/PhysicalMaterial.h"

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
    // Dynamic과 Static에 스케일을 적용한다. 여기 들어간 파라미터를 이용해서 Shape를 생성한다.
    // OwnerID: 같은 스켈레탈 메쉬의 바디들은 같은 ID를 가짐 (Self-Collision 방지용)
    void InitDynamic(FPhysScene& World, const FTransform& WorldTransform, float Mass, const FVector& Scale3D = FVector(1,1,1), uint32 OwnerID = 0);
    void InitStatic(FPhysScene& World, const FTransform& WorldTransform, const FVector& Scale3D = FVector(1,1,1), uint32 OwnerID = 0);

    void Terminate(FPhysScene& World);

    void AddForce(const FVector& Force);
    FTransform GetWorldTransform() const;

public:
    UPrimitiveComponent*        OwnerComponent = nullptr;
    UBodySetup*                 BodySetup      = nullptr;
    physx::PxRigidActor*        RigidActor     = nullptr;

    // Override 값들 (컴포넌트에서 설정)
    bool bUseOverrideValues = false;
    float MassOverride = 10.0f;
    float LinearDampingOverride = 0.01f;
    float AngularDampingOverride = 0.05f;

    // Physics Material Override
    float StaticFrictionOverride = 0.5f;
    float DynamicFrictionOverride = 0.4f;
    float RestitutionOverride = 0.0f;
    ECombineMode FrictionCombineModeOverride = ECombineMode::Multiply;
    ECombineMode RestitutionCombineModeOverride = ECombineMode::Multiply;
};
