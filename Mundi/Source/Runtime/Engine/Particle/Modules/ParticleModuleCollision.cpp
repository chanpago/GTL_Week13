#include "pch.h"
#include "ParticleModuleCollision.h"

#include "Source/Runtime/Engine/Particle/ParticleEmitterInstance.h"
#include "Source/Runtime/Engine/Particle/ParticleHelper.h"

IMPLEMENT_CLASS(UParticleModuleCollision)

UParticleModuleCollision::UParticleModuleCollision()
{
    bSpawnModule = false;
    bUpdateModule = true;
    bEnabled = true;
    ModuleType = EParticleModuleType::Update;
}

void UParticleModuleCollision::UpdateAsync(FParticleEmitterInstance* Owner, int32 Offset, const FParticleSimulationContext& Context)
{
    if (!bEnabled || !Owner || Context.WorldColliders.IsEmpty()) { return; }

    const TArray<FColliderProxy>& Colliders = Context.WorldColliders;

    BEGIN_UPDATE_LOOP
    {
        // 충돌 판정
        float ParticleRadius = (Particle.Size.X * 0.5f) * RadiusScale;

        FHitResult BestHit;
        BestHit.PenetrationDepth = -1.0f;
        for (const FColliderProxy& Proxy : Colliders)
        {
            FHitResult TempHit;
            if (Collision::ComputeSphereToShapePenetration(Particle.Location, ParticleRadius, Proxy, TempHit))
            {
                if (TempHit.PenetrationDepth > BestHit.PenetrationDepth)
                {
                    BestHit = TempHit;
                }
            }
        }

        // 충돌 반응
        if (BestHit.bHit)
        {
            // 위치 보정
            Particle.Location += BestHit.ImpactNormal * (BestHit.PenetrationDepth + 0.001f);

            switch (CollisionResponse)
            {
            case EParticleCollisionResponse::Kill:
                {
                    // 즉시 사망 처리
                    Particle.RelativeTime = 1.0f;
                }
                break;

            case EParticleCollisionResponse::Stop:
                {
                    // 제동
                    Particle.Velocity = FVector::Zero();
                    Particle.RotationRate = 0.0f;
                }
                break;

            case EParticleCollisionResponse::Bounce:
                {
                    // 성분 분해
                    float VelDotNormal = FVector::Dot(Particle.Velocity, BestHit.ImpactNormal);
                    if (VelDotNormal < 0.0f)
                    {
                        FVector NormalVel = BestHit.ImpactNormal * VelDotNormal;
                        FVector TangentVel = Particle.Velocity - NormalVel;

                        // 수직 성분
                        NormalVel *= -Restitution;
                        // 수평 성분
                        TangentVel *= (1.0f - Friction);
                        // 최종 속도
                        Particle.Velocity = NormalVel + TangentVel;
                    }
                }
                break;
            }

            // TODO - 이벤트 생성
        }
    }
    END_UPDATE_LOOP
}
