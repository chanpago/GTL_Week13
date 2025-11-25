#include "pch.h"
#include "ParticleModuleSize.h"
#include "../ParticleEmitter.h"
#include "../ParticleHelper.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitterInstance.h"

IMPLEMENT_CLASS(UParticleModuleSize)

UParticleModuleSize::UParticleModuleSize()
{
    bSpawnModule = true;
    bUpdateModule = true;
}

void UParticleModuleSize::Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase)
{
    if (!ParticleBase)
        return;

    // 초기 크기 설정
    FVector Size = StartSize.GetValue({Owner->GetRandomFloat(), Owner->GetRandomFloat(), Owner->GetRandomFloat()});

    if (bUniformSize)
    {
        // X 값으로 통일
        Size.Y = Size.X;
        Size.Z = Size.X;
    }

    ParticleBase->Size = Size;
    ParticleBase->BaseSize = Size;
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
