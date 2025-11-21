#pragma once

// Forward declarations
struct FParticleEmitterInstance;
struct FBaseParticle;

// Distribution 타입들 - 파티클 파라미터의 랜덤/커브 값을 표현
template<typename T>
struct FRawDistribution
{
    T MinValue;
    T MaxValue;
    bool bUseRange = false;  // true면 MinValue~MaxValue 범위, false면 MinValue 고정값

    FRawDistribution() : MinValue(T()), MaxValue(T()), bUseRange(false) {}
    FRawDistribution(const T& value) : MinValue(value), MaxValue(value), bUseRange(false) {}
    FRawDistribution(const T& min, const T& max) : MinValue(min), MaxValue(max), bUseRange(true) {}

    T GetValue(float randomSeed = 0.0f) const
    {
        if (bUseRange)
        {
            // MinValue와 MaxValue 사이를 보간
            return MinValue + (MaxValue - MinValue) * randomSeed;
        }
        return MinValue;
    }
};

using FRawDistributionFloat = FRawDistribution<float>;
using FRawDistributionVector = FRawDistribution<FVector>;
using FRawDistributionColor = FRawDistribution<FLinearColor>;

enum class EParticleModuleType : uint8
{
    Required, Spawn, Update, TypeData
};

class UParticleModule : public UObject
{
public:
    // ============================================================
    // 언리얼 스타일 인터페이스
    // ============================================================

    // 파티클 생성 시 호출 (단일 파티클)
    virtual void Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase) {}

    // 매 프레임 업데이트 시 호출 (모든 파티클)
    virtual void Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime) {}

public:
    EParticleModuleType ModuleType;
    bool  bEnabled = true;
    int32 SortPriority = 0;

    // payload가 필요한 모듈이면 바이트 수를 정의
    virtual int32 GetRequiredBytesPerParticle() const { return 0; }

    // Cache 단계에서 emitter가 채워줌(에셋 캐시)
    int32 PayloadOffset = -1;
};