#pragma once
#include "ParticleModule.h"

class UParticleModuleColor : public UParticleModule
{
public:
    // 초기 색상 (Spawn 시)
    FRawDistributionColor StartColor = FRawDistributionColor(FLinearColor(FVector(1.0f,1.0f,1.0f)));

    // 알파 값 (Spawn 시)
    FRawDistributionFloat StartAlpha = FRawDistributionFloat(1.0f);

    // 수명에 따른 색상 변화 (Update 시)
    FRawDistributionColor ColorOverLife = FRawDistributionColor(FLinearColor(FVector(1.0f,1.0f,1.0f)));

    // 수명에 따른 알파 변화 (Update 시)
    FRawDistributionFloat AlphaOverLife = FRawDistributionFloat(1.0f);

    // 색상 변화 사용 여부
    bool bUseColorOverLife = false;
    bool bUseAlphaOverLife = false;

    // ============================================================
    // 언리얼 스타일 구현
    // ============================================================

    virtual void Spawn(FParticleEmitterInstance* Owner, int32 Offset, int32 ParticleIndex, int32 InstancePayloadOffset) override;
    virtual void Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime) override;
};
