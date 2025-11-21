#include "pch.h"
#include "ParticleModuleSize.h"
#include "../ParticleEmitter.h"
#include "../ParticleHelper.h"

void UParticleModuleSize::Spawn(FParticleEmitterInstance* Owner, int32 Offset, int32 ParticleIndex, int32 InstancePayloadOffset)
{
    FBaseParticle* Particle = Owner->GetParticle(ParticleIndex);
    if (!Particle)
        return;

    // 랜덤 시드 생성
    float RandomSeed = (float)(Owner->ParticleCounter % 1000) / 1000.0f;

    // 초기 크기 설정
    FVector Size = StartSize.GetValue(RandomSeed);

    if (bUniformSize)
    {
        // X 값으로 통일
        Size.Y = Size.X;
        Size.Z = Size.X;
    }

    Particle->Size = Size;
    Particle->BaseSize = Size;
}

void UParticleModuleSize::Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime)
{
    if (!bUseSizeOverLife)
        return;

    BEGIN_UPDATE_LOOP
    {
        FVector SizeMultiplier = SizeOverLife.GetValue(Particle.RelativeTime);

        if (bUniformSize)
        {
            // X 값으로 통일
            float Scale = SizeMultiplier.X;
            Particle.Size = Particle.BaseSize * Scale;
        }
        else
        {
            Particle.Size = Particle.BaseSize * SizeMultiplier;
        }
    }
    END_UPDATE_LOOP;
}
