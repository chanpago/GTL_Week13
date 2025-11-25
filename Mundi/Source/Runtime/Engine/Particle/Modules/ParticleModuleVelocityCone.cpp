#include "pch.h"
#include "ParticleModuleVelocityCone.h"
#include "../ParticleEmitterInstance.h"
#include "Source/Runtime/Engine/Particle/ParticleHelper.h"

IMPLEMENT_CLASS(UParticleModuleVelocityCone)

UParticleModuleVelocityCone::UParticleModuleVelocityCone()
{
    bSpawnModule = true; // 스폰할 때 한 번만 속도를 줌
    bUpdateModule = false;
}

void UParticleModuleVelocityCone::Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase)
{
    float Speed = Velocity.GetValue(Owner->GetRandomFloat());
    float ConeAngleDeg = Angle.GetValue(Owner->GetRandomFloat());
    
    // 각도 클램핑 (0 ~ 180도)
    ConeAngleDeg = FMath::Clamp(ConeAngleDeg, 0.0f, 180.0f);

    // 기준 방향 정규화
    FVector Dir = Direction.GetSafeNormal();
    if (Dir.IsZero()) Dir = FVector(0, 0, 1);
    
    float ConeHalfAngleRad = DegreesToRadians(ConeAngleDeg);
    
    // Z값(높이)을 랜덤으로 뽑아야 표면적이 균등해짐.
    float CosAngle = cos(ConeHalfAngleRad);
    
    // 1.0(꼭대기) ~ CosAngle(바닥) 사이의 랜덤 높이
    float Z = FMath::Lerp(CosAngle, 1.0f, Owner->GetRandomFloat());
    // 0 ~ 360도 회전
    float Pi = Owner->GetRandomFloat() * 2.0f * PI;
    // 피타고라스 정리에 의한 반지름 (r = sqrt(1 - z^2))
    float R = FMath::Sqrt(1.0f - Z * Z);

    // 로컬 공간(Z-Up)에서의 랜덤 방향 벡터
    FVector LocalDir;
    LocalDir.X = R * cos(Pi);
    LocalDir.Y = R * sin(Pi);
    LocalDir.Z = Z;
    
    FQuat Rot = FQuat::FindBetweenNormals({0,0,1}, Dir);
    
    FVector FinalVelocityDir = Rot.RotateVector(LocalDir);
    ParticleBase->Velocity += FinalVelocityDir * Speed;
}