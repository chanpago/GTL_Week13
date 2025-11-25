#include "pch.h"
#include "ParticleEmitterInstance.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "ParticleSystemComponent.h"
#include "PlatformTime.h"
#include "Modules/ParticleModuleRequired.h"
#include "Modules/ParticleModuleSpawn.h"

void FParticleEmitterInstance::Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent)
{
    Template = InTemplate;
    Component = InComponent;

    UpdateModuleCache();

    if (CachedRequiredModule)
    {
        MaxActiveParticles = CachedRequiredModule->MaxParticles;
        EmitterDuration = CachedRequiredModule->EmitterDuration;
        LoopCount = CachedRequiredModule->EmitterLoops;

        // TEMPORARY FIX: MaxParticles가 0이면 기본값 사용
        if (MaxActiveParticles == 0)
        {
            MaxActiveParticles = 1000;
            UE_LOG("[ParticleEmitterInstance::Init] WARNING: MaxParticles was 0! Using default: 1000");
        }

        UE_LOG("[ParticleEmitterInstance::Init] Template: %s", Template ? Template->GetName().c_str() : "NULL");
        UE_LOG("[ParticleEmitterInstance::Init] MaxActiveParticles: %d (from template: %d)",
               MaxActiveParticles, CachedRequiredModule->MaxParticles);
        UE_LOG("[ParticleEmitterInstance::Init] EmitterDuration: %.2f", EmitterDuration);
        UE_LOG("[ParticleEmitterInstance::Init] EmitterLoops: %d", CachedRequiredModule->EmitterLoops);
        UE_LOG("[ParticleEmitterInstance::Init] SpawnRateBase: %.2f", CachedRequiredModule->SpawnRateBase);
        UE_LOG("[ParticleEmitterInstance::Init] Material: %s", CachedRequiredModule->Material ? CachedRequiredModule->Material->GetName().c_str() : "NULL");
        UE_LOG("[ParticleEmitterInstance::Init] LODLevel Enabled: %s", CurrentLODLevel && CurrentLODLevel->bEnabled ? "true" : "false");
    }
    else
    {
        MaxActiveParticles = 1000; // Fallback
        EmitterDuration = 1.0f;
        UE_LOG("[ParticleEmitterInstance::Init] WARNING: CachedRequiredModule is NULL! Using fallback values.");
    }

    InitializeParticleMemory();
    
    std::random_device Rd;
    InitRandom(Rd());
}

void FParticleEmitterInstance::InitializeParticleMemory()
{
    UE_LOG("[InitializeParticleMemory] Called! MaxActiveParticles before: %d", MaxActiveParticles);

    if (MaxActiveParticles == 0)
    {
        MaxActiveParticles = 1000; // 임시 기본값 (나중엔 Template에서 가져와야 함)
    }

    if (ParticleStride == 0)
    {
        ParticleSize = sizeof(FBaseParticle);
        ParticleStride = (ParticleSize + 15) & ~15;
    }

    // 기존 메모리 해제 - 이게 MaxActiveParticles를 0으로 리셋함!
    int32 SavedMaxActiveParticles = MaxActiveParticles;
    FreeParticleMemory();
    MaxActiveParticles = SavedMaxActiveParticles;  // 복원!

    UE_LOG("[InitializeParticleMemory] After FreeParticleMemory, restored MaxActiveParticles to: %d", MaxActiveParticles);

    constexpr SIZE_T Alignment = 16;
    const SIZE_T DataSize = static_cast<SIZE_T>(MaxActiveParticles * ParticleStride);
    const SIZE_T IndicesSize = MaxActiveParticles * sizeof(uint16);
    
    ParticleData = static_cast<uint8*>(FMemoryManager::Allocate(DataSize, Alignment));
    ParticleIndices = static_cast<uint16*>(FMemoryManager::Allocate(IndicesSize, Alignment));
    for (int32 i = 0; i < MaxActiveParticles; i++)
    {
        ParticleIndices[i] = static_cast<uint16>(i);
    }
    
    // InstanceData 할당 (필요하다면)
    if (InstancePayloadSize > 0)
    {
        InstanceData = static_cast<uint8*>(FMemoryManager::Allocate(InstancePayloadSize, Alignment));
    }

    ActiveParticles = 0;
    ParticleCounter = 0;
}

void FParticleEmitterInstance::FreeParticleMemory()
{
    UE_LOG("[FreeParticleMemory] Called! Resetting MaxActiveParticles from %d to 0", MaxActiveParticles);

    if (ParticleData)
    {
        FMemoryManager::Deallocate(ParticleData);
        ParticleData = nullptr;
    }

    if (ParticleIndices)
    {
        FMemoryManager::Deallocate(ParticleIndices);
        ParticleIndices = nullptr;
    }

    if (InstanceData)
    {
        FMemoryManager::Deallocate(InstanceData);
        InstanceData = nullptr;
    }

    ActiveParticles = 0;
    MaxActiveParticles = 0;  // ← 여기서 리셋됨!
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity, const FParticleSimulationContext& InContext)
{
    // UE_LOG("[SpawnParticles] Called with Count: %d, ActiveParticles: %d, MaxActiveParticles: %d",
    //        Count, ActiveParticles, MaxActiveParticles);

    if (ActiveParticles >= MaxActiveParticles)
    {
        UE_LOG("[SpawnParticles] BLOCKED! ActiveParticles >= MaxActiveParticles (%d >= %d)",
               ActiveParticles, MaxActiveParticles);
        return;
    }

    UParticleModuleRequired* Required = CurrentLODLevel->RequiredModule;
    for (int32 NowSpawnIdx = 0; NowSpawnIdx < Count; NowSpawnIdx++)
    {
        if (ActiveParticles >= MaxActiveParticles)
        {
            break;
        }
        
        int32 NewParticleIndex = ActiveParticles;
        DECLARE_PARTICLE_PTR(Particle, ParticleData, ParticleStride, NewParticleIndex)

        // 죽은 메모리 남아있을 수 있으므로 0으로 초기화
        memset(Particle, 0, ParticleSize);

        // 초기값 (추후 모듈이 덮어쓸 것임)
        if (Required)
        {
            Particle->Size = Required->InitialSize.GetValue(0.0f); 
            Particle->Color = Required->InitialColor.GetValue(0.0f);
            Particle->Rotation = Required->InitialRotation.GetValue(0.0f);
        }
        else
        {
            // Required도 없으면 진짜 쌩기본값
            Particle->Size = FVector(1.0f,1.0f, 1.0f);
            Particle->Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
        }
    
        FVector SpawnLoc = FVector::Zero();
        if (Required && Required->bUseLocalSpace)
        {
            SpawnLoc = FVector::Zero();
        }
        else
        {
            SpawnLoc = InitialLocation;
        }
        Particle->Location = SpawnLoc;
        Particle->BaseVelocity = InitialVelocity;
        Particle->Velocity = InitialVelocity;

        float CurrentSpawnTime = StartTime + (Increment * NowSpawnIdx);
        if (CurrentSpawnTime > 0.0f)
        {
            Particle->Location += Particle->Velocity * CurrentSpawnTime;
        }

        if (CurrentLODLevel)
        {
            for (UParticleModule* Module : CurrentLODLevel->SpawnModules)
            {
                if (!Module || !Module->bEnabled) { continue; }
                Module->SpawnAsync(this, PayloadOffset, CurrentSpawnTime, Particle, InContext);
            }
        }
        
        ParticleIndices[NewParticleIndex] = NewParticleIndex;
        ActiveParticles++;
        ParticleCounter++;
    }
}

void FParticleEmitterInstance::KillParticle(int32 Index)
{
    if (Index < 0 || Index >= ActiveParticles) return;

    // 1. 마지막 파티클의 인덱스 (ActiveParticles - 1)
    int32 LastIndex = ActiveParticles - 1;

    // 죽는 파티클이 맨 마지막 인덱스가 아니라면
    if (Index != LastIndex)
    {
        // 메모리를 직접 복사해서 앞쪽으로 당김
        DECLARE_PARTICLE_PTR(Dest, ParticleData, ParticleStride, Index)
        DECLARE_PARTICLE_PTR(Src, ParticleData, ParticleStride, LastIndex)
        memcpy(Dest, Src, ParticleStride);
    }

    ActiveParticles--;
}

// 비동기 고려된 Tick, 안에서 Component Raw Pointer 절대 사용금지!!!!!!!!
void FParticleEmitterInstance::Tick(const FParticleSimulationContext& Context)
{
    TIME_PROFILE(Particle_EmitterTick)
    if (!Context.bIsActive && ActiveParticles <= 0) { return; }
    UpdateModuleCache();
    
    if (!Template || !ParticleData)
    {
        if (!Template) UE_LOG("[ParticleEmitterInstance::Tick] Template is NULL!");
        if (!ParticleData) UE_LOG("[ParticleEmitterInstance::Tick] ParticleData is NULL!");
        return;
    }
    
    if (!CurrentLODLevel || !CurrentLODLevel->bEnabled)
    {
        if (!CurrentLODLevel) UE_LOG("[ParticleEmitterInstance::Tick] CurrentLODLevel is NULL!");
        else UE_LOG("[ParticleEmitterInstance::Tick] CurrentLODLevel is disabled!");
        return;
    }

    // ============================================================
    // Spawn
    // ============================================================
    // 이미터가 끝났는지 체크
    bool bEmitterFinished = CachedRequiredModule && CachedRequiredModule->EmitterLoops > 0
                                && LoopCount >= CachedRequiredModule->EmitterLoops;

    float RandomValue = GetRandomFloat();

    // 아직 안 끝났을 때만 스폰 시도
    if (!bEmitterFinished && !Context.bSuppressSpawning)
    {
        // [A] Continuous Spawn
        float SpawnRate = CachedSpawnModule ? CachedSpawnModule->GetSpawnRate(EmitterTime, RandomValue)
                            : (CachedRequiredModule ? CachedRequiredModule->SpawnRateBase : 0.0f);

        float OldSpawnFraction = SpawnFraction;
        SpawnFraction += SpawnRate * Context.DeltaTime;
        int32 NumToSpawn = static_cast<int32>(SpawnFraction);

        if (NumToSpawn > 0)
        {
            // UE_LOG("[ParticleEmitterInstance::Tick] SPAWNING! NumToSpawn: %d, MaxActiveParticles: %d, ActiveParticles: %d",
            //       NumToSpawn, MaxActiveParticles, ActiveParticles);

            SpawnFraction -= static_cast<float>(NumToSpawn);

            float RateDivisor = (SpawnRate > 0.0f) ? SpawnRate : 1.0f;
            float Increment = 1.0f / RateDivisor; // 파티클 1개당 걸리는 시간
            float StartTime = (1.0f - OldSpawnFraction) * Increment; // 첫 번째 파티클이 태어날 시간

            SpawnParticles(NumToSpawn, StartTime, Increment, Context.ComponentLocation, FVector::Zero(), Context);

            // UE_LOG("[ParticleEmitterInstance::Tick] AFTER SPAWN: ActiveParticles: %d", ActiveParticles);
        }

        // [B] 버스트 스폰 (Burst)
        if (CachedSpawnModule)
        {
            float OldTime = EmitterTime;
            float NewTime = EmitterTime + Context.DeltaTime;

            int32 BurstCount = CachedSpawnModule->GetBurstCount(OldTime, NewTime, RandomValue);

            if (BurstCount > 0)
            {
                // 버스트는 시간차 없이(0.0f) 한 번에 생성
                SpawnParticles(BurstCount, 0.0f, 0.0f, Context.ComponentLocation, FVector::Zero(), Context);
            }
        }
    }
    
    // ============================================================
    // Time Update & Kill
    // ============================================================
    for (int32 i = 0; i < ActiveParticles; i++)
    {
        DECLARE_PARTICLE_PTR(Particle, ParticleData, ParticleStride, i)

        Particle->OldLocation = Particle->Location;
        Particle->Location += Particle->Velocity * Context.DeltaTime;

        if (Particle->OneOverMaxLifetime > 0.0f)
        {
            Particle->RelativeTime += Particle->OneOverMaxLifetime * Context.DeltaTime;
        }

        if (Particle->RelativeTime >= 1.0f)
        {
            KillParticle(i);
            i--; // Swap & Pop 인덱스 보정
        }
    }
    
    // ============================================================
    // Module Update
    // ============================================================
    for (UParticleModule* Module : CurrentLODLevel->UpdateModules)
    {
        if (!Module || !Module->bEnabled) { continue; }
        Module->UpdateAsync(this, 0, Context);
    }

    // ============================================================
    // Emitter Time Update
    // ============================================================
    EmitterTime += Context.DeltaTime;
    
    if (CachedRequiredModule && CachedRequiredModule->EmitterDuration > 0.0f)
    {
        if (EmitterTime >= CachedRequiredModule->EmitterDuration)
        {
            // 무한 루프(0)거나 아직 횟수가 남았으면 리셋
            if (CachedRequiredModule->EmitterLoops == 0 || LoopCount < CachedRequiredModule->EmitterLoops)
            {
                EmitterTime = 0.0f;
                LoopCount++;
            }
        }
    }
}

void FParticleEmitterInstance::UpdateModuleCache()
{
    if (!Template) { return; }
    
    CurrentLODLevel = Template->LODLevels[CurrentLODLevelIndex];
    if (!CurrentLODLevel || !CurrentLODLevel->bEnabled) { return; }

    CachedRequiredModule = CurrentLODLevel->RequiredModule;
    CachedSpawnModule = CurrentLODLevel->SpawnModule;
}

FDynamicEmitterDataBase* FParticleEmitterInstance::CreateDynamicData()
{
    if (ActiveParticles <= 0) return nullptr;

    const EParticleType Type = GetDynamicType();
    FDynamicEmitterDataBase* NewData = nullptr;

    if (Type == EParticleType::Sprite)
    {
        auto* SpriteData = new FDynamicSpriteEmitterData();
        SpriteData->EmitterType = Type;
        SpriteData->SortMode = CachedRequiredModule->SortMode;
        SpriteData->SortPriority = 0;
        
        if (CachedRequiredModule)
        {
            SpriteData->SortMode = CachedRequiredModule->SortMode;
            SpriteData->bUseLocalSpace = CachedRequiredModule->bUseLocalSpace;
        }

        // 데이터 채우기 (Memcpy)
        BuildReplayData(SpriteData->Source);
        NewData = SpriteData;
    }
    else if (Type == EParticleType::Mesh)
    {
        auto* MeshData = new FDynamicMeshEmitterData();
        MeshData->EmitterType = Type;
        MeshData->SortMode = CachedRequiredModule->SortMode;
        MeshData->SortPriority = 0;
        
        if (CachedRequiredModule)
        {
            MeshData->SortMode = CachedRequiredModule->SortMode;
        }

        // 데이터 채우기
        BuildReplayData(MeshData->Source);

        if (MeshData->Source.Mesh)
        {
            NewData = MeshData;
        }
        else
        {
            delete MeshData;
            NewData = nullptr;
        }
    }

    return NewData;
}

void FParticleEmitterInstance::BuildReplayData(FDynamicEmitterReplayDataBase& OutData)
{ 
    if (ActiveParticles <= 0 || !ParticleData)
    {
        return;
    }

    // 1) 기본 공통 정보
    OutData.EmitterType = GetDynamicType();
    OutData.ActiveParticleCount = ActiveParticles;
    OutData.ParticleStride = ParticleStride;
    OutData.Scale = FVector::One();

    // 2) DataContainer 메모리 재할당
    OutData.DataContainer.Free();

    const int32 ParticleBytes = ActiveParticles * ParticleStride;
    const int32 IndexCount = ActiveParticles;  // 논리적으로 살아있는 파티클 수만

    OutData.DataContainer.Allocate(ParticleBytes, IndexCount);

    // Data Container 재할당
    std::memcpy(
        OutData.DataContainer.ParticleData,
        ParticleData,
        ParticleBytes
    );

    if (ParticleIndices && IndexCount > 0)
    {
        std::memcpy(
            OutData.DataContainer.ParticleIndices,
            ParticleIndices,
            sizeof(uint16) * IndexCount
        );
    }

    // 타입별 추가 필드 세팅
     // 3) 타입별 추가 필드 세팅
    switch (OutData.EmitterType)
    {
        case EParticleType::Sprite:
        {
            auto& SpriteOut = static_cast<FDynamicSpriteEmitterReplayData&>(OutData);
            SpriteOut.RequiredModule = CachedRequiredModule;
            break;
        }
        case EParticleType::Mesh: 
        {
            auto& MeshOut = static_cast<FDynamicMeshEmitterReplayData&>(OutData);
            MeshOut.Mesh = Template ? Template->Mesh : nullptr;
            MeshOut.InstanceStride = sizeof(FBaseParticle); // 추후 변경
            MeshOut.InstanceCount = ActiveParticles;
        }
    }
}

bool FParticleEmitterInstance::IsComplete() const
{
    if (!CurrentLODLevel || !CurrentLODLevel->RequiredModule) return true;
    
    UParticleModuleRequired* Required = CurrentLODLevel->RequiredModule;
    if (Required->EmitterLoops == 0 || LoopCount < Required->EmitterLoops)
    {
        return false;
    }

    if (ActiveParticles > 0)
    {
        return false;
    }

    // 루프도 끝났고 파티클도 다 사라짐
    return true;
}

void FParticleEmitterInstance::InitRandom(uint32 Seed)
{
    RandomStream.seed(Seed);
}

float FParticleEmitterInstance::GetRandomFloat()
{
    std::uniform_real_distribution<float> Dist(0.0f, 1.0f);
    return Dist(RandomStream);
}
