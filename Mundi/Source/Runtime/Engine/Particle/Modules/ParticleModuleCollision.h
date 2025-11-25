#pragma once
#include "ParticleModule.h"
#include "Collision.h"

// 충돌 반응 타입
enum class EParticleCollisionResponse : uint8
{
    Bounce, // 튕김
    Stop,   // 멈춤
    Kill    // 삭제
};

class UParticleModuleCollision : public UParticleModule
{
    DECLARE_CLASS(UParticleModuleCollision, UParticleModule)

public:
    UParticleModuleCollision();
    virtual void UpdateAsync(FParticleEmitterInstance* Owner, int32 Offset, const FParticleSimulationContext& Context) override;

// Module Attribute
public:
    // 충돌 시 반응
    EParticleCollisionResponse CollisionResponse = EParticleCollisionResponse::Bounce;

    // 탄성 계수 (클수록 잘 튀어오름), Bounce 일 때만 사용
    float Restitution = 0.5f;

    // 마찰 계수 (클수록 마찰 높음)
    float Friction = 0.0f;

    // 파티클 반지름 스케일 (충돌 판정 크기 조절)
    float RadiusScale = 1.0f;

    // 이벤트를 발생시킬 것인가
    bool bWriteEvent = false;

};