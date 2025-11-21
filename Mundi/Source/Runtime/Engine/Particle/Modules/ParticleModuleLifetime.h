#pragma once
#include "ParticleModule.h"

class UParticleModuleLifetime : public UParticleModule
{
public:
    // 파티클 수명 (초 단위)
    FRawDistributionFloat Lifetime = FRawDistributionFloat(1.0f, 5.0f);

    // ============================================================
    // 언리얼 스타일 구현
    // ============================================================

    virtual void Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase) override;
    virtual void Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime) override;
};
