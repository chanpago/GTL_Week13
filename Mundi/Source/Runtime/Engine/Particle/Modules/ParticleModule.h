#pragma once
#include "Source/Runtime/Engine/Particle/Async/ParticleSimulationContext.h"

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

    T GetValue(float Ratio) const 
    {
        if (bUseRange)
        {
            // Ratio가 0~1 범위를 벗어날 경우를 대비해 Clamp를 걸거나,
            // 성능을 믿고 그냥 곱하거나 선택 (보통 그냥 곱함)
            return MinValue + (MaxValue - MinValue) * Ratio;
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
    DECLARE_CLASS(UParticleModule, UObject)
public:
    // 파티클 생성 시 호출 (단일 파티클)
    virtual void Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase) {}
    virtual void SpawnAsync(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase, const FParticleSimulationContext& Context)
    {
        // 기본적으로는 그냥 Spawn을 부르지만,
        // Component를 쓰는 자식 클래스는 이걸 오버라이딩해서 Context를 써야 함
        Spawn(Owner, Offset, SpawnTime, ParticleBase);
    }

    // 매 프레임 업데이트 시 호출 (모든 파티클)
    virtual void Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime) {}
    virtual void UpdateAsync(FParticleEmitterInstance* Owner, int32 Offset, const FParticleSimulationContext& Context)
    {
        // 기본적으로는 그냥 Update를 부르지만,
        // Component를 쓰는 자식 클래스는 이걸 오버라이딩해서 Context를 써야 함
        Update(Owner, Offset, Context.DeltaTime);
    }

public:
    EParticleModuleType ModuleType;
    bool  bEnabled = true;
    int32 SortPriority = 0;

    // payload가 필요한 모듈이면 바이트 수를 정의
    virtual int32 GetRequiredBytesPerParticle() const { return 0; }

    // Cache 단계에서 emitter가 채워줌(에셋 캐시)
    int32 PayloadOffset = -1;

    bool bSpawnModule = false;  // 이 모듈은 Spawn 단계에서 사용 가능한가?
    bool bUpdateModule = false; // 이 모듈은 Update 단계에서 사용 가능한가?
};