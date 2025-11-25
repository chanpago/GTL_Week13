#pragma once
#include "ParticleModule.h"

UCLASS()
class UParticleModuleVelocityCone : public UParticleModule
{
    DECLARE_CLASS(UParticleModuleVelocityCone, UParticleModule)

public:
    UParticleModuleVelocityCone();
    void Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase) override;

public:
    // 원뿔이 향할 기준 방향
    FVector Direction = FVector(0.0f, 0.0f, 1.0f);

    // 원뿔의 벌어진 각도
    FRawDistributionFloat Angle = FRawDistributionFloat(30.0f);

    // 쏘아보내는 세기
    FRawDistributionFloat Velocity = FRawDistributionFloat(100.0f, 200.0f);
};