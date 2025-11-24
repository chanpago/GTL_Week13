#pragma once
#include "ParticleModule.h"

// SubUV 보간 방식
enum class ESubUVInterpMethod : uint8
{
    None,           // 프레임 단위로 뚝뚝 끊김 (보간 없음)
    LinearBlend,    // 두 프레임을 샘플해서 색상 블렌딩 (부드러운 애니메이션)
    Random,         // 랜덤 프레임 선택 (커브 무시)
    RandomBlend     // 랜덤 + 보간
};

// SubUV 애니메이션 모듈
// 스프라이트 시트(Flipbook/Atlas)의 여러 타일 중 하나를 시간에 따라 선택
class UParticleModuleSubUV : public UParticleModule
{
    DECLARE_CLASS(UParticleModuleSubUV, UParticleModule)
public:
    UParticleModuleSubUV();

    // SubImageIndex 커브 (시간 t에 따라 float 프레임 번호 생성)
    // 예: t=0 → I=0, t=1 → I=63 (64프레임 전체 훑기)
    FRawDistributionFloat SubImageIndex;

    // 보간 방식
    ESubUVInterpMethod InterpMethod = ESubUVInterpMethod::None;

    // false: ParticleRelativeTime (Age/Lifetime, 0~1) 사용
    // true:  EmitterTime (Emitter 생성 후 절대 시간) 사용
    bool bUseRealTime = false;

    // UParticleModule 오버라이드
    virtual void Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase) override;
    virtual void Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime) override;
    virtual int32 GetRequiredBytesPerParticle() const override { return sizeof(float); }  // SubImageIndex 저장

private:
    float CalculateSubImageIndex(FParticleEmitterInstance* Owner, FBaseParticle* Particle, float RandomSeed) const;
};
