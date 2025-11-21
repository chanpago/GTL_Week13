#include "pch.h"
#include "ParticleModuleColor.h"
#include "../ParticleEmitter.h"
#include "../ParticleHelper.h"

void UParticleModuleColor::Spawn(FParticleEmitterInstance* Owner, int32 Offset, int32 ParticleIndex, int32 InstancePayloadOffset)
{
    FBaseParticle* Particle = Owner->GetParticle(ParticleIndex);
    if (!Particle)
        return;

    // 랜덤 시드 생성 (파티클 카운터 기반)
    float RandomSeed = (float)(Owner->ParticleCounter % 1000) / 1000.0f;

    // 초기 색상 설정
    Particle->Color = StartColor.GetValue(RandomSeed);
    Particle->Color.A = StartAlpha.GetValue(RandomSeed);

    // Base 색상도 저장 (나중에 참조용)
    Particle->BaseColor = Particle->Color;
}

void UParticleModuleColor::Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime)
{
    // BEGIN_UPDATE_LOOP 매크로 사용
    BEGIN_UPDATE_LOOP
    {
        if (bUseColorOverLife)
        {
            FLinearColor NewColor = ColorOverLife.GetValue(Particle.RelativeTime);
            Particle.Color.R = NewColor.R;
            Particle.Color.G = NewColor.G;
            Particle.Color.B = NewColor.B;
        }

        if (bUseAlphaOverLife)
        {
            Particle.Color.A = AlphaOverLife.GetValue(Particle.RelativeTime);
        }
    }
    END_UPDATE_LOOP;
}
