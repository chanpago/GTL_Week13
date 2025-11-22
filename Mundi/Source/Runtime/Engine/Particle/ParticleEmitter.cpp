#include "pch.h"
#include "ParticleEmitter.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "ParticleSystemComponent.h"
#include "Modules/ParticleModule.h"
#include "Modules/ParticleModuleRequired.h"
#include "Modules/ParticleModuleSpawn.h"
#include "Modules/ParticleModuleTypeDataBase.h"
#include "DynamicEmitterDataBase.h"

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
    }
    else
    {
        MaxActiveParticles = 1000; // Fallback
        EmitterDuration = 1.0f;
    }

    InitializeParticleMemory();
}

void FParticleEmitterInstance::InitializeParticleMemory()
{
    if (MaxActiveParticles == 0)
    {
        MaxActiveParticles = 1000; // 임시 기본값 (나중엔 Template에서 가져와야 함)
    }

    if (ParticleStride == 0)
    {
        ParticleSize = sizeof(FBaseParticle);
        ParticleStride = (ParticleSize + 15) & ~15;
    }

    // 기존 메모리 해제
    FreeParticleMemory();

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
    MaxActiveParticles = 0;
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity)
{
    if (ActiveParticles >= MaxActiveParticles)
    {
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
                Module->Spawn(this, PayloadOffset, CurrentSpawnTime, Particle);
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

void FParticleEmitterInstance::Tick(float DeltaTime)
{
    UpdateModuleCache();
    if (!Template || !Component || !ParticleData) { return; }
    if (!CurrentLODLevel || !CurrentLODLevel->bEnabled) { return; }

    // ============================================================
    // Spawn
    // ============================================================
    // 이미터가 끝났는지 체크
    bool bEmitterFinished = CachedRequiredModule && CachedRequiredModule->EmitterLoops > 0
                                && LoopCount >= CachedRequiredModule->EmitterLoops;

    float RandomValue = FloatHash(ParticleCounter);
    
    // 아직 안 끝났을 때만 스폰 시도
    if (!bEmitterFinished)
    {
        // [A] Continuous Spawn
        float SpawnRate = CachedSpawnModule ? CachedSpawnModule->GetSpawnRate(EmitterTime, RandomValue)
                            : (CachedSpawnModule ? CachedRequiredModule->SpawnRateBase : 0.0f);
        
        float OldSpawnFraction = SpawnFraction;
        SpawnFraction += SpawnRate * DeltaTime;
        int32 NumToSpawn = static_cast<int32>(SpawnFraction);
        
        if (NumToSpawn > 0)
        {
            SpawnFraction -= static_cast<float>(NumToSpawn);

            float RateDivisor = (SpawnRate > 0.0f) ? SpawnRate : 1.0f;
            float Increment = 1.0f / RateDivisor; // 파티클 1개당 걸리는 시간
            float StartTime = (1.0f - OldSpawnFraction) * Increment; // 첫 번째 파티클이 태어날 시간
            
            SpawnParticles(NumToSpawn, StartTime, Increment, Component->GetWorldLocation(), FVector::Zero());
        }

        // [B] 버스트 스폰 (Burst)
        if (CachedSpawnModule)
        {
            float OldTime = EmitterTime;
            float NewTime = EmitterTime + DeltaTime;

            int32 BurstCount = CachedSpawnModule->GetBurstCount(OldTime, NewTime, RandomValue);

            if (BurstCount > 0)
            {
                // 버스트는 시간차 없이(0.0f) 한 번에 생성
                SpawnParticles(BurstCount, 0.0f, 0.0f, Component->GetWorldLocation(), FVector::Zero());
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
        Particle->Location += Particle->Velocity * DeltaTime;

        if (Particle->OneOverMaxLifetime > 0.0f)
        {
            Particle->RelativeTime += Particle->OneOverMaxLifetime * DeltaTime;
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
        Module->Update(this, 0, DeltaTime);
    }

    // ============================================================
    // Emitter Time Update
    // ============================================================
    EmitterTime += DeltaTime;
    
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

void FParticleEmitterInstance::BuildReplayData(FDynamicEmitterReplayDataBase& OutData)
{ 
    // 1) 기본 공통 정보
    OutData.EmitterType = GetDynamicType();
    OutData.ActiveParticleCount = ActiveParticles;
    OutData.ParticleStride = ParticleStride;
    OutData.Scale = FVector::One();

    // 2) DataContainer 메모리 재할당
    OutData.DataContainer.Free();

    if (ActiveParticles <= 0 || !ParticleData)
    {
        return;
    }

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
        case EEmitterRenderType::Sprite:
        {
            auto& SpriteOut = static_cast<FDynamicSpriteEmitterReplayData&>(OutData);

            SpriteOut.MaterialInterface = CachedRequiredModule
                ? CachedRequiredModule->Material
                : nullptr;

            SpriteOut.RequiredModule = CachedRequiredModule;

            break;
        }
        case EEmitterRenderType::Mesh:
        {
            auto& MeshOut = static_cast<FDynamicMeshEmitterReplayData&>(OutData);
            

            const int32 ParticleBytes = ActiveParticles * ParticleStride;
            const int32 InstanceCount = ActiveParticles;

            MeshOut.DataContainer.Free();
            MeshOut.DataContainer.Allocate(ParticleBytes, InstanceCount);

            std::memcpy(MeshOut.DataContainer.ParticleData, ParticleData, ParticleBytes);

            // Mesh 세팅
            MeshOut.Mesh = Template ? Template->Mesh : nullptr;
            // MeshOut.InstanceStride = /* instance 데이터 stride */;
            MeshOut.InstanceCount = InstanceCount;
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

void UParticleEmitter::CacheEmitterModuleInfo()
{
    // 보통 LOD0 기준으로 캐시 (또는 LOD별 따로 캐시)
    UParticleLODLevel* LOD0 = (LODLevels.Num() > 0) ? LODLevels[0] : nullptr;
    if (!LOD0) return;

    LOD0->RebuildModuleCaches();

    int32 Offset = sizeof(FBaseParticle);

    for (UParticleModule* M : LOD0->AllModulesCache)
    {
        if (!M || !M->bEnabled) continue;

        int32 Bytes = M->GetRequiredBytesPerParticle();
        if (Bytes > 0)
        {
            M->PayloadOffset = Offset;
            // 16바이트 정렬 (SIMD 최적화)
            Offset += (Bytes + 15) & ~15;
        }

        if (M->ModuleType == EParticleModuleType::Required)
        {
            auto* R = static_cast<UParticleModuleRequired*>(M);
            MaxParticles = FMath::Max(MaxParticles, R->MaxParticles);
            MaxLifetime  = FMath::Max(MaxLifetime,  R->EmitterDuration);
        }
    }

    // TypeData가 파티클 구조 확장 요구하면 반영
    // ✅ TypeDataModule이 nullptr일 수 있으므로 체크만 하고 사용 안함
    if (LOD0->TypeDataModule)
    {
        // TypeDataModule 관련 처리는 나중에 구현
        Offset = FMath::Max(Offset, LOD0->TypeDataModule->GetRequiredParticleBytes());
    }

    ParticleSizeBytes = Offset;
}
