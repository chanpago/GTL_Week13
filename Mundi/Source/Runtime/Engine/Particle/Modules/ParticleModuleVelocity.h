#pragma once
#include "ParticleModule.h"

class UParticleModuleVelocity : public UParticleModule
{
    DECLARE_CLASS(UParticleModuleVelocity, UParticleModule)
public:
    // 초기 속도
    FRawDistributionVector StartVelocity = FRawDistributionVector(FVector(0.0f, 0.0f, 10.0f));

    // 속도 배율 (랜덤성 추가용)
    FRawDistributionFloat VelocityMultiplier = FRawDistributionFloat(1.0f);

    // 중력 가속도 (Update 단계에서 적용)
    FVector Gravity = FVector(0.0f, 0.0f, -980.0f);

    // 공기 저항 (0~1, 1에 가까울수록 저항이 큼)
    float Damping = 0.0f;

    // ============================================================
    // 언리얼 스타일 구현
    // ============================================================

    virtual void Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase) override;
    virtual void Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime) override;
};
