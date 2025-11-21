#pragma once
#include "ParticleModule.h"

enum class ELocationDistributionType : uint8
{
    Point,      // 한 점에서 생성
    Box,        // 박스 영역에서 랜덤 생성
    Sphere,     // 구 영역에서 랜덤 생성
    Cylinder    // 원통 영역에서 랜덤 생성
};

class UParticleModuleLocation : public UParticleModule
{
public:
    // 분포 타입
    ELocationDistributionType DistributionType = ELocationDistributionType::Point;

    // 시작 위치 (Point 타입일 때)
    FRawDistributionVector StartLocation = FRawDistributionVector(FVector(0.0f,0.0f,0.0f));

    // Box 분포 파라미터 (각 축의 범위)
    FVector BoxExtent = FVector(100.0f, 100.0f, 100.0f);

    // Sphere 분포 파라미터
    float SphereRadius = 100.0f;

    // Cylinder 분포 파라미터
    float CylinderRadius = 100.0f;
    float CylinderHeight = 200.0f;

    // ============================================================
    // 언리얼 스타일 구현
    // ============================================================

    virtual void Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase) override;
};
