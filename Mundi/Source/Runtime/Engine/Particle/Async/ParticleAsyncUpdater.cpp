#include "pch.h"
#include "ParticleAsyncUpdater.h"

#include "PlatformTime.h"

FParticleAsyncUpdater::~FParticleAsyncUpdater()
{
    if (TaskHandle.valid())
    {
        // 1. 작업이 끝날 때까지 기다림
        TaskHandle.wait();

        // 2. ★ 중요 ★ 결과물을 꺼내야 함!
        // 이걸 안 하면 future 안에 들어있던 포인터 뭉치가 그냥 증발(Leak)함
        FAsyncSimulationResult PendingResult = TaskHandle.get();

        // 3. 꺼낸 데이터(막 생성된 따끈따끈한 릭 유발자들)를 수동으로 삭제
        for (FDynamicEmitterDataBase* Data : PendingResult.RenderData)
        {
            if (Data) delete Data;
        }
        PendingResult.RenderData.Empty();
    }

    // 4. 기존에 멤버변수로 들고 있던 데이터 삭제
    InternalClearRenderData();
}

void FParticleAsyncUpdater::KickOff(const TArray<FParticleEmitterInstance*>& Instances, const FParticleSimulationContext& Context)
{
    if (IsBusy()) { return; }
    
    if (TaskHandle.valid())
    {
        FAsyncSimulationResult Result = TaskHandle.get();
        
        // 데이터 교체 (Swap)
        InternalClearRenderData();
        RenderData = std::move(Result.RenderData);
        LastFrameStats = Result.Stats;
    }
    
    TaskHandle = std::async(std::launch::async, [Instances, Context]() 
    {
        return DoSimulationWork(Instances, Context);
    });
}

void FParticleAsyncUpdater::KickOffSync(const TArray<FParticleEmitterInstance*>& Instances, const FParticleSimulationContext& Context)
{
    Sync();
    if (!RenderData.IsEmpty())
    {
        InternalClearRenderData();
    }
    FAsyncSimulationResult Result = DoSimulationWork(Instances, Context);
    // 새 데이터로 교체
    RenderData = std::move(Result.RenderData);

    // 통계 갱신
    LastFrameStats = Result.Stats;
}

void FParticleAsyncUpdater::EnsureCompletion()
{
    if (TaskHandle.valid())
    {
        TaskHandle.wait();
    }
}

void FParticleAsyncUpdater::ResetStats()
{
    LastFrameStats.bAllEmittersComplete = false; 
    LastFrameStats.bHasActiveParticles = false;
    LastFrameStats.TotalActiveParticles = 0;
}

void FParticleAsyncUpdater::Sync()
{
}

bool FParticleAsyncUpdater::TrySync()
{
    if (!TaskHandle.valid()) return false;

    // 즉시 상태 확인
    auto Status = TaskHandle.wait_for(std::chrono::seconds(0));
    if (Status == std::future_status::ready)
    {
        // 작업 완료
        FAsyncSimulationResult Result = TaskHandle.get();

        // 데이터 교체
        InternalClearRenderData();
        RenderData = std::move(Result.RenderData);
        LastFrameStats = Result.Stats;
            
        return true;
    }

    return false; // 아직 일하는 중 (기존 데이터 유지)
}

bool FParticleAsyncUpdater::IsBusy() const
{
    return TaskHandle.valid() && TaskHandle.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

FAsyncSimulationResult FParticleAsyncUpdater::DoSimulationWork(const TArray<FParticleEmitterInstance*>& Instances, const FParticleSimulationContext& Context)
{
    TIME_PROFILE(Particle_Simulation)
    FAsyncSimulationResult Result;
        
    // 통계 초기화
    Result.Stats.bAllEmittersComplete = true;
    Result.Stats.TotalActiveParticles = 0;
    Result.Stats.bHasActiveParticles = false;

    const FVector ViewOrigin = Context.CameraLocation; // 혹은 Context.CameraLocation (별도 추가 권장)
    const FVector ViewDir = Context.CameraRotation.ToEulerZYXDeg(); // 혹은 Context.CameraForward

    for (int32 Idx = 0; Idx < Instances.Num(); ++Idx)
    {
        FParticleEmitterInstance* Inst = Instances[Idx];
        if (!Inst) continue;

        // 시뮬레이션 수행
        Inst->Tick(Context);

        // 통계 집계
        int32 Count = Inst->ActiveParticles;
        Result.Stats.TotalActiveParticles += Count;
            
        if (Count > 0) 
        {
            Result.Stats.bHasActiveParticles = true;
        }
            
        if (!Inst->IsComplete())
        {
            Result.Stats.bAllEmittersComplete = false;
        }

        // 렌더 데이터 생성
        FDynamicEmitterDataBase* EmitterData = Inst->CreateDynamicData();
        if (EmitterData)
        {
            EmitterData->EmitterIndex = Idx;
            if (EmitterData->EmitterType == EParticleType::Sprite)
            {
                auto* SpriteData = static_cast<FDynamicSpriteEmitterData*>(EmitterData);
                SpriteData->SortParticles(ViewOrigin, ViewDir, Context.ComponentWorldMatrix, SpriteData->AsyncSortedIndices);
            }
            else if (EmitterData->EmitterType == EParticleType::Mesh)
            {
                auto* MeshData = static_cast<FDynamicMeshEmitterData*>(EmitterData);
                MeshData->SortParticles(ViewOrigin, ViewDir, Context.ComponentWorldMatrix, MeshData->AsyncSortedIndices);
            }
            Result.RenderData.Add(EmitterData);
        }
    }

    return Result;
}

void FParticleAsyncUpdater::InternalClearRenderData()
{
    for (FDynamicEmitterDataBase* Data : RenderData)
    {
        if (Data)
        {
            delete Data;
        }
    }
    RenderData.Empty();
}
