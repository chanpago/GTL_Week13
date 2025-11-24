#pragma once
#include <future>

#include "Source/Runtime/Engine/Particle/DynamicEmitterDataBase.h"

struct FParticleFrameStats
{
    uint32 TotalActiveParticles = 0;
    bool bAllEmittersComplete = false;
    bool bHasActiveParticles = false;
};

struct FAsyncSimulationResult
{
    // 렌더링 데이터
    TArray<FDynamicEmitterDataBase*> RenderData;
    // 통계 데이터
    FParticleFrameStats Stats;
};

class FParticleAsyncUpdater
{
public:
    FParticleAsyncUpdater() = default;

    // TaskHandle(std::future)은 복사할 수 없으므로, 그냥 빈 상태로 초기화
    FParticleAsyncUpdater(const FParticleAsyncUpdater& Other)
    {
        LastFrameStats = FParticleFrameStats();
    }

    FParticleAsyncUpdater& operator=(const FParticleAsyncUpdater& Other)
    {
        if (this != &Other)
        {
            if (TaskHandle.valid()) TaskHandle.wait();
            InternalClearRenderData();
            LastFrameStats = FParticleFrameStats();
        }
        return *this;
    }

    FParticleAsyncUpdater(FParticleAsyncUpdater&&) = default;
    FParticleAsyncUpdater& operator=(FParticleAsyncUpdater&&) = default;
    ~FParticleAsyncUpdater();
    
    // [Main Thread 읽기 전용] 이전 프레임의 통계 캐시
    FParticleFrameStats LastFrameStats;
    // [Main Thread 읽기 전용] 렌더링 데이터
    TArray<FDynamicEmitterDataBase*> RenderData;

    // 작업 시작
    void KickOff(const TArray<FParticleEmitterInstance*>& Instances, const FParticleSimulationContext& Context);
    void KickOffSync(const TArray<FParticleEmitterInstance*>& Instances, const FParticleSimulationContext& Context);
    void EnsureCompletion();

    // 결과 동기화
    void Sync();
    // 비차단 동기화
    bool TrySync();
    // 현재 작업 중인지 확인
    bool IsBusy() const;

private:
    static FAsyncSimulationResult DoSimulationWork(const TArray<FParticleEmitterInstance*>& Instances, const FParticleSimulationContext& Context);
    void InternalClearRenderData();
    // 비동기 작업 핸들
    std::future<FAsyncSimulationResult> TaskHandle;
};