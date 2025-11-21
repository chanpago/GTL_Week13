#include "pch.h"
#include "ParticleModuleLifetime.h"
#include "../ParticleEmitter.h"

void UParticleModuleLifetime::Spawn(FParticleEmitterInstance* Owner, int32 Offset, int32 ParticleIndex, int32 InstancePayloadOffset)
{
    FBaseParticle* Particle = Owner->GetParticle(ParticleIndex);
    if (!Particle)
        return;

    // 랜덤 시드 생성
    float RandomSeed = (float)(Owner->ParticleCounter % 1000) / 1000.0f;

    // 수명 설정
    float MaxLifetime = Lifetime.GetValue(RandomSeed);
    if (MaxLifetime > 0.0f)
    {
        Particle->OneOverMaxLifetime = 1.0f / MaxLifetime;
    }
    else
    {
        Particle->OneOverMaxLifetime = 0.0f;
    }

    Particle->RelativeTime = 0.0f;
}

void UParticleModuleLifetime::Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime)
{
    // Lifetime은 보통 별도의 업데이트가 필요없음
    // RelativeTime은 다른 시스템에서 업데이트됨
    // 필요시 여기에 추가 로직 구현 가능
}
