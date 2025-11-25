#include "pch.h"
#include "ParticleModuleLocation.h"

#include "ParticleModuleRequired.h"
#include "../ParticleEmitter.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitterInstance.h"
#include "Source/Runtime/Engine/Particle/ParticleHelper.h"

IMPLEMENT_CLASS(UParticleModuleLocation)

UParticleModuleLocation::UParticleModuleLocation()
{
    bSpawnModule = true;
}

void UParticleModuleLocation::SpawnAsync(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase, const FParticleSimulationContext& Context)
{
    ParticleBase->Location = FVector(0, 0, 0);
    FVector LocalOffset = FVector::Zero();

    switch (DistributionType)
    {
    case ELocationDistributionType::Point:
        LocalOffset = StartLocation.GetValue({Owner->GetRandomFloat(), Owner->GetRandomFloat(), Owner->GetRandomFloat()});
        break;

    case ELocationDistributionType::Box:
        {
            FVector RandPos;
            RandPos.X = (Owner->GetRandomFloat() * 2.0f - 1.0f) * BoxExtent.X;
            RandPos.Y = (Owner->GetRandomFloat() * 2.0f - 1.0f) * BoxExtent.Y;
            RandPos.Z = (Owner->GetRandomFloat() * 2.0f - 1.0f) * BoxExtent.Z;
            
            // StartLocation(중심점 오프셋) + 박스 랜덤 범위
            LocalOffset = StartLocation.GetValue(0.0f) + RandPos;
        }
        break;

    case ELocationDistributionType::Sphere:
        {
            float Theta = Owner->GetRandomFloat() * 2.0f * PI;
            float Pi = Owner->GetRandomFloat() * PI;
            float R = Owner->GetRandomFloat() * SphereRadius;

            LocalOffset.X = R * sinf(Pi) * cosf(Theta);
            LocalOffset.Y = R * sinf(Pi) * sinf(Theta);
            LocalOffset.Z = R * cosf(Pi);

            LocalOffset += StartLocation.GetValue(0.0f);
        }
        break;

    case ELocationDistributionType::Cylinder:
        {
            float Angle = Owner->GetRandomFloat() * 2.0f * PI;
            float Radius = Owner->GetRandomFloat() * CylinderRadius;
            float Height = (Owner->GetRandomFloat() * 2.0f - 1.0f) * CylinderHeight * 0.5f;

            LocalOffset.X = Radius * cosf(Angle);
            LocalOffset.Y = Radius * sinf(Angle);
            LocalOffset.Z = Height;

            LocalOffset += StartLocation.GetValue(0.0f);
        }
        break;
    }

    bool bUseLocalSpace = false;
    if (Owner->CachedRequiredModule)
    {
        bUseLocalSpace = Owner->CachedRequiredModule->bUseLocalSpace;
    }

    if (bUseLocalSpace)
    {
        // [Local Space]
        ParticleBase->Location += LocalOffset;
    }
    else
    {
        // [World Space] (기본)
        ParticleBase->Location += Context.ComponentWorldMatrix.TransformPosition(LocalOffset);
    }

    ParticleBase->OldLocation = ParticleBase->Location;
}