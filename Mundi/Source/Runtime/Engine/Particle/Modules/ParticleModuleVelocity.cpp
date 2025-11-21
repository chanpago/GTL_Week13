#include "pch.h"
#include "ParticleModuleVelocity.h"
#include "../ParticleEmitter.h"
#include "../ParticleHelper.h"

void UParticleModuleVelocity::Spawn(FParticleEmitterInstance* Owner, int32 Offset, int32 ParticleIndex, int32 InstancePayloadOffset)
{
    FBaseParticle* Particle = Owner->GetParticle(ParticleIndex);
    if (!Particle)
        return;

    // 랜덤 시드 생성
    float RandomSeed = (float)(Owner->ParticleCounter % 1000) / 1000.0f;

    // 초기 속도 설정
    FVector Velocity = StartVelocity.GetValue(RandomSeed);
    float Multiplier = VelocityMultiplier.GetValue(RandomSeed);

    Particle->Velocity = Velocity * Multiplier;
    Particle->BaseVelocity = Particle->Velocity;
}

void UParticleModuleVelocity::Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime)
{
    BEGIN_UPDATE_LOOP
    {
        // 이전 위치 저장
        Particle.OldLocation = Particle.Location;

        // 중력 적용
        Particle.Velocity += Gravity * DeltaTime;

        // 공기 저항 적용
        if (Damping > 0.0f)
        {
            float DampingFactor = FMath::Max(0.0f, 1.0f - Damping * DeltaTime);
            Particle.Velocity *= DampingFactor;
        }

        // 위치 업데이트
        Particle.Location += Particle.Velocity * DeltaTime;
    }
    END_UPDATE_LOOP;
}
