#pragma once
#include "ParticleModule.h"

class UParticleModuleSize : public UParticleModule
{
public:
    // 초기 크기 (Spawn 시)
    FRawDistributionVector StartSize = FRawDistributionVector(FVector(1.0f,1.0f,1.0f) * 50.0f);

    // 수명에 따른 크기 변화 (Update 시)
    FRawDistributionVector SizeOverLife = FRawDistributionVector(FVector(1.0f,1.0f,1.0f));

    // 크기 변화 사용 여부
    bool bUseSizeOverLife = false;

    // 균일한 크기 사용 (X 값만 사용해서 모든 축에 적용)
    bool bUniformSize = true;

    // ============================================================
    // 언리얼 스타일 구현
    // ============================================================

    virtual void Spawn(FParticleEmitterInstance* Owner, int32 Offset, int32 ParticleIndex, int32 InstancePayloadOffset) override;
    virtual void Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime) override;
};
