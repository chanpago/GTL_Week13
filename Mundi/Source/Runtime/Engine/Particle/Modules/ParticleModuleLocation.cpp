#include "pch.h"
#include "ParticleModuleLocation.h"
#include "../ParticleEmitter.h"

void UParticleModuleLocation::Spawn(FParticleEmitterInstance* Owner, int32 Offset, int32 ParticleIndex, int32 InstancePayloadOffset)
{
    FBaseParticle* Particle = Owner->GetParticle(ParticleIndex);
    if (!Particle)
        return;

    // 랜덤 시드 생성
    float RandomSeed = (float)(Owner->ParticleCounter % 1000) / 1000.0f;

    FVector SpawnLocation = FVector(0.0f,0.0f,0.0f);

    switch (DistributionType)
    {
    case ELocationDistributionType::Point:
        SpawnLocation = StartLocation.GetValue(RandomSeed);
        break;

    case ELocationDistributionType::Box:
        {
            // 박스 영역 내 랜덤 위치
            float rx = (RandomSeed * 2.0f - 1.0f) * BoxExtent.X;
            float ry = (fmodf(RandomSeed * 7.919f, 1.0f) * 2.0f - 1.0f) * BoxExtent.Y;
            float rz = (fmodf(RandomSeed * 13.531f, 1.0f) * 2.0f - 1.0f) * BoxExtent.Z;
            SpawnLocation = FVector(rx, ry, rz);
        }
        break;

    case ELocationDistributionType::Sphere:
        {
            // 구 영역 내 랜덤 위치
            float theta = RandomSeed * 2.0f * PI;
            float phi = fmodf(RandomSeed * 7.919f, 1.0f) * PI;
            float r = fmodf(RandomSeed * 13.531f, 1.0f) * SphereRadius;

            SpawnLocation.X = r * sinf(phi) * cosf(theta);
            SpawnLocation.Y = r * sinf(phi) * sinf(theta);
            SpawnLocation.Z = r * cosf(phi);
        }
        break;

    case ELocationDistributionType::Cylinder:
        {
            // 원통 영역 내 랜덤 위치
            float angle = RandomSeed * 2.0f * PI;
            float radius = fmodf(RandomSeed * 7.919f, 1.0f) * CylinderRadius;
            float height = (fmodf(RandomSeed * 13.531f, 1.0f) * 2.0f - 1.0f) * CylinderHeight * 0.5f;

            SpawnLocation.X = radius * cosf(angle);
            SpawnLocation.Y = radius * sinf(angle);
            SpawnLocation.Z = height;
        }
        break;
    }

    Particle->Location = SpawnLocation;
    Particle->OldLocation = SpawnLocation;
}
