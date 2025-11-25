#pragma once
#include "OBB.h"
#include "ShapeComponent.h"

/** 파티클 스레드에서 충돌 판정을 위해 미리 빌드하는 UShapeComponent의 충돌 데이터 */
struct FColliderProxy
{
    EShapeKind Type;

    union
    {
        // Box용 
        FOBB Box; 
        // Capsule용 
        struct
        {
            FVector PosA;
            FVector PosB;
            float Radius;
        } Capsule;
        // Sphere용
        struct
        {
            FVector Center;
            float Radius;
        } Sphere;
    };
    
    FColliderProxy() : Type(EShapeKind::Box), Box() 
    {
    }
};


struct FParticleSimulationContext
{
    // 시간 정보
    float DeltaTime;
    float RealTimeSeconds;

    // 공간 정보
    FVector ComponentLocation;
    FQuat   ComponentRotation;
    FVector ComponentScale;
    FMatrix ComponentWorldMatrix;

    // 카메라
    FVector CameraLocation;
    FQuat CameraRotation;

    // 상태 정보
    bool bIsActive;
    bool bSuppressSpawning;
    int32 CurrentLODIndex;

    // 충돌 정보
    TArray<FColliderProxy> WorldColliders; // 이번 프레임 월드에 있는 충돌체 정보
};
