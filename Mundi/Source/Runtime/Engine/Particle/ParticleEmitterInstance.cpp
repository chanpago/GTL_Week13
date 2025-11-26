#include "pch.h"
#include "ParticleEmitterInstance.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "ParticleSystemComponent.h"
#include "PlatformTime.h"
#include "Modules/ParticleModuleRequired.h"
#include "Modules/ParticleModuleSpawn.h"
#include "Modules/ParticleModuleSubUV.h"
#include "Modules/ParticleModuleMesh.h"
#include "Modules/ParticleModuleBeam.h"
#include "Modules/ParticleModuleRibbon.h"

void FParticleEmitterInstance::Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent)
{
    Template = InTemplate;
    Component = InComponent;
    LoopCount = 0;
    EmitterTime = 0.0f;

    UpdateModuleCache();

    if (CachedRequiredModule)
    {
        MaxActiveParticles = CachedRequiredModule->MaxParticles;
        EmitterDuration = CachedRequiredModule->EmitterDuration;

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
    InitializeRibbonState();

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
        // Template에서 계산된 파티클 크기 사용 (Beam Payload 포함)
        if (Template && Template->ParticleSizeBytes > 0)
        {
            ParticleSize = Template->ParticleSizeBytes;
        }
        else
        {
            ParticleSize = sizeof(FBaseParticle);
        }
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

        // 빔 타입인 경우 Payload에 시작점/끝점 저장
        if (Template && Template->RenderType == EParticleType::Beam)
        {
            // TypeDataModule에서 직접 가져오기
            UParticleModuleBeam* BeamModule = CurrentLODLevel ? Cast<UParticleModuleBeam>(CurrentLODLevel->TypeDataModule) : nullptr;

            UE_LOG("[SpawnParticles::Beam] CurrentLODLevel=%p, TypeDataModule=%p, BeamModule=%p, PayloadOffset=%d",
                CurrentLODLevel,
                CurrentLODLevel ? CurrentLODLevel->TypeDataModule : nullptr,
                BeamModule,
                PayloadOffset);

            if (BeamModule)
            {
                uint8* ParticleBase = reinterpret_cast<uint8*>(Particle);
                FVector* BeamSource = reinterpret_cast<FVector*>(ParticleBase + PayloadOffset);
                FVector* BeamTarget = reinterpret_cast<FVector*>(ParticleBase + PayloadOffset + sizeof(FVector));

                // 기본 시작점/끝점 설정
                FVector FinalSource = BeamModule->SourcePoint;
                FVector FinalTarget = BeamModule->TargetPoint;

                UE_LOG("[SpawnParticles::Beam] Source=(%.1f, %.1f, %.1f), Target=(%.1f, %.1f, %.1f)",
                    FinalSource.X, FinalSource.Y, FinalSource.Z,
                    FinalTarget.X, FinalTarget.Y, FinalTarget.Z);

                // 랜덤 오프셋 적용 (번개 효과)
                if (BeamModule->bUseRandomOffset)
                {
                    // 각 축마다 -Offset ~ +Offset 범위에서 랜덤 값 생성
                    // GetRandomFloat()는 0~1 범위이므로 -1~1로 변환 후 오프셋 곱하기
                    FVector SourceRandom(
                        (GetRandomFloat() * 2.0f - 1.0f) * BeamModule->SourceOffset.X,
                        (GetRandomFloat() * 2.0f - 1.0f) * BeamModule->SourceOffset.Y,
                        (GetRandomFloat() * 2.0f - 1.0f) * BeamModule->SourceOffset.Z
                    );

                    FVector TargetRandom(
                        (GetRandomFloat() * 2.0f - 1.0f) * BeamModule->TargetOffset.X,
                        (GetRandomFloat() * 2.0f - 1.0f) * BeamModule->TargetOffset.Y,
                        (GetRandomFloat() * 2.0f - 1.0f) * BeamModule->TargetOffset.Z
                    );

                    FinalSource += SourceRandom;
                    FinalTarget += TargetRandom;
                }

                *BeamSource = FinalSource;
                *BeamTarget = FinalTarget;

                // 노이즈용 랜덤 시드 저장
                float* BeamRandomSeed = reinterpret_cast<float*>(ParticleBase + PayloadOffset + sizeof(FVector) * 2);
                *BeamRandomSeed = GetRandomFloat() * 1000.0f;
            }
        }

        if (bHasRibbonTrails && RibbonPayloadOffset >= 0)
        {
            uint8* ParticleBase = reinterpret_cast<uint8*>(Particle);
            auto* TrailPayload = reinterpret_cast<FRibbonTrailRuntimePayload*>(ParticleBase +
                RibbonPayloadOffset);
            AttachRibbonParticle(NewParticleIndex, TrailPayload);
        }

        ParticleIndices[NewParticleIndex] = NewParticleIndex;
        ActiveParticles++;
        ParticleCounter++;
    }
}

void FParticleEmitterInstance::KillParticle(int32 Index)
{
    if (Index < 0 || Index >= ActiveParticles) return;

    if (bHasRibbonTrails)
    {
        DetachRibbonParticle(Index);
    }

    // 1. 마지막 파티클의 인덱스 (ActiveParticles - 1)
    int32 LastIndex = ActiveParticles - 1;

    // 죽는 파티클이 맨 마지막 인덱스가 아니라면
    if (Index != LastIndex)
    {
        // 메모리를 직접 복사해서 앞쪽으로 당김
        DECLARE_PARTICLE_PTR(Dest, ParticleData, ParticleStride, Index)
        DECLARE_PARTICLE_PTR(Src, ParticleData, ParticleStride, LastIndex)
        memcpy(Dest, Src, ParticleStride);

        if (bHasRibbonTrails)
        {
            RemapRibbonParticleIndex(LastIndex, Index);
        }
    }

    ActiveParticles--;
}

// 비동기 고려된 Tick, 안에서 Component Raw Pointer 절대 사용금지!!!!!!!!
void FParticleEmitterInstance::Tick(const FParticleSimulationContext& Context)
{
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
    if (bHasRibbonTrails)
    {
        UpdateRibbonTrailDistances();
    }

    for (UParticleModule* Module : CurrentLODLevel->UpdateModules)
    {
        if (!Module || !Module->bEnabled) { continue; }
        Module->UpdateAsync(this, Module->PayloadOffset, Context);
    }

    // ============================================================
    // Emitter Time Update
    // ============================================================
    EmitterTime += Context.DeltaTime;

    if (CachedRequiredModule && CachedRequiredModule->EmitterDuration > 0.0f)
    {
        // 이미터 시간이 수명을 초과했는지 확인
        if (EmitterTime >= CachedRequiredModule->EmitterDuration)
        {
            // 루프 조건 확인
            bool bShouldLoop = (CachedRequiredModule->EmitterLoops == 0) || 
                               (LoopCount < CachedRequiredModule->EmitterLoops - 1);

            if (bShouldLoop)
            {
                // Duration이 1.0초인데 EmitterTime이 1.05초가 됐다면,
                // 다음 루프의 0.05초 시점에서 시작해야 파티클 간격이 일정하게 유지
                EmitterTime -= CachedRequiredModule->EmitterDuration;
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

    // BeamModule이 있으면 PayloadOffset 설정
    if (Template->RenderType == EParticleType::Beam)
    {
        // TypeDataModule에서 직접 가져오기
        if (UParticleModuleBeam* BeamModule = Cast<UParticleModuleBeam>(CurrentLODLevel->TypeDataModule))
        {
            // BeamModule의 PayloadOffset이 설정되어 있으면 사용, 아니면 기본값
            if (BeamModule->PayloadOffset > 0)
            {
                PayloadOffset = BeamModule->PayloadOffset;
            }
            else
            {
                // PayloadOffset이 설정 안 됐으면 FBaseParticle 바로 뒤로 설정
                PayloadOffset = sizeof(FBaseParticle);
            }
        }
        else
        {
            PayloadOffset = sizeof(FBaseParticle);
        }
    }
    else if (Template->RenderType == EParticleType::Ribbon)
    {
        if (auto* RibbonModule = Cast<UParticleModuleRibbon>(CurrentLODLevel->TypeDataModule))
        {
            CachedRibbonModule = RibbonModule;
            RibbonTrailCount = FMath::Max(1, RibbonModule->MaxTrailCount);
            RibbonPayloadOffset = (RibbonModule->PayloadOffset > 0) ? RibbonModule->PayloadOffset :
                sizeof(FBaseParticle);
            PayloadOffset = RibbonPayloadOffset;
        }
        else
        {
            CachedRibbonModule = nullptr;
            RibbonTrailCount = 0;
            RibbonPayloadOffset = sizeof(FBaseParticle);
            PayloadOffset = RibbonPayloadOffset;
        }
    }
    else
    {
        PayloadOffset = sizeof(FBaseParticle);
        RibbonPayloadOffset = -1;
        CachedRibbonModule = nullptr;
    }
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
        SpriteData->Alignment = CachedRequiredModule->ScreenAlignment;
        SpriteData->SortPriority = 0;
        SpriteData->bUseLocalSpace = CachedRequiredModule->bUseLocalSpace;
        
        // 데이터 채우기 (Memcpy)
        BuildReplayData(SpriteData->Source);
        NewData = SpriteData;
    }
    else if (Type == EParticleType::Mesh)
    {
        auto* MeshData = new FDynamicMeshEmitterData();
        MeshData->EmitterType = Type;
        MeshData->SortMode = CachedRequiredModule->SortMode;
        MeshData->Alignment = CachedRequiredModule->ScreenAlignment;
        MeshData->SortPriority = 0;

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
    else if (Type == EParticleType::Beam)
    {
        auto* BeamData = new FDynamicBeamEmitterData();
        BeamData->EmitterType = Type;
        BeamData->SortMode = CachedRequiredModule->SortMode;
        BeamData->SortPriority = 0;
        BeamData->bUseLocalSpace = CachedRequiredModule->bUseLocalSpace;
        BuildReplayData(BeamData->Source);
        NewData = BeamData;
    }
    else if (Type == EParticleType::Ribbon)
    {
        // RIBBON
        auto* RibbonData = new FDynamicRibbonEmitterData();
        RibbonData->EmitterType = Type;

        // 데이터 채우기
        BuildReplayData(RibbonData->Source);
        
        NewData = RibbonData;
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

            // SubUV 모듈 찾기
            if (CurrentLODLevel)
            {
                for (UParticleModule* Module : CurrentLODLevel->UpdateModules)
                {
                    if (auto* SubUV = Cast<UParticleModuleSubUV>(Module))
                    {
                        if (SubUV->bEnabled)
                        {
                            SpriteOut.SubUVModule = SubUV;
                            SpriteOut.SubUVPayloadOffset = SubUV->PayloadOffset;
                            break;
                        }
                    }
                }
            }
            break;
        }
        case EParticleType::Mesh:
        {
            auto& MeshOut = static_cast<FDynamicMeshEmitterReplayData&>(OutData);
            MeshOut.Mesh = Template ? Template->Mesh : nullptr;
            MeshOut.InstanceStride = sizeof(FBaseParticle); // 추후 변경
            MeshOut.InstanceCount = ActiveParticles;

            for (UParticleModule* Module : CurrentLODLevel->AllModulesCache)
            {
                if (UParticleModuleMesh* ModuleMesh = Cast<UParticleModuleMesh>(Module))
                {
                    MeshOut.bLighting = ModuleMesh->bLighting;
                }
            }
            break;
        }
        case EParticleType::Ribbon:
        {
            auto& RibbonOut = static_cast<FDynamicRibbonEmitterReplayData&>(OutData);

            RibbonOut.Width = CachedRibbonModule ? CachedRibbonModule->Width : 10.0f;
            RibbonOut.TilingDistance = CachedRibbonModule ? CachedRibbonModule->TilingDistance : 0.0f;
            RibbonOut.TrailLifetime = CachedRibbonModule ? CachedRibbonModule->TrailLifetime : 1.0f;
            RibbonOut.bUseCameraFacing = CachedRibbonModule ? CachedRibbonModule->bUseCameraFacing : true;
            RibbonOut.TrailPayloadOffset = RibbonPayloadOffset;
            RibbonOut.TrailCount = RibbonTrailCount;
            RibbonOut.TrailHeads = RibbonTrailHeads;
            break;
        }
        case EParticleType::Beam:
        {
            auto& BeamOut = static_cast<FDynamicBeamEmitterReplayData&>(OutData);
            BeamOut.RequiredModule = CachedRequiredModule;

            // 빔 모듈에서 설정 가져오기 (TypeDataModule에서 직접 가져오기)
            if (CurrentLODLevel && CurrentLODLevel->TypeDataModule)
            {
                UParticleModuleBeam* BeamModule = Cast<UParticleModuleBeam>(CurrentLODLevel->TypeDataModule);
                if (BeamModule)
                {
                    BeamOut.TessellationFactor = BeamModule->TessellationFactor;
                    BeamOut.NoiseFrequency = BeamModule->NoiseFrequency;
                    BeamOut.NoiseAmplitude = BeamModule->NoiseAmplitude;

                    UE_LOG("[BuildReplayData::Beam] TessellationFactor=%d, NoiseFreq=%.2f, NoiseAmp=%.2f",
                        BeamOut.TessellationFactor, BeamOut.NoiseFrequency, BeamOut.NoiseAmplitude);
                }
                else
                {
                    UE_LOG("[BuildReplayData::Beam] BeamModule Cast failed!");
                }
            }
            else
            {
                UE_LOG("[BuildReplayData::Beam] CurrentLODLevel=%p, TypeDataModule=%p",
                    CurrentLODLevel, CurrentLODLevel ? CurrentLODLevel->TypeDataModule : nullptr);
            }
            break;
        }
        default:
            break;
    }
}

void FParticleEmitterInstance::InitializeRibbonState()
{
    RibbonTrailHeads.Empty();
    RibbonSpawnTrailCursor = 0;

    if (Template && Template->RenderType == EParticleType::Ribbon)
    {
        bHasRibbonTrails = true;
        if (CachedRibbonModule)
        {
            RibbonTrailCount = FMath::Max(1, CachedRibbonModule->MaxTrailCount);
            RibbonPayloadOffset = (CachedRibbonModule->PayloadOffset > 0) ?
                CachedRibbonModule->PayloadOffset : sizeof(FBaseParticle);
        }
        else
        {
            RibbonTrailCount = FMath::Max(1, RibbonTrailCount);
            RibbonPayloadOffset = sizeof(FBaseParticle);
        }
        RibbonTrailHeads.resize(RibbonTrailCount, INDEX_NONE);
    }
    else
    {
        bHasRibbonTrails = false;
        RibbonTrailCount = 0;
        RibbonPayloadOffset = -1;
    }
}

void FParticleEmitterInstance::UpdateRibbonTrailDistances()
{
    if (!bHasRibbonTrails || RibbonPayloadOffset < 0) return;

    for (int32 TrailIdx = 0; TrailIdx < RibbonTrailHeads.Num(); ++TrailIdx)
    {
        int32 Current = RibbonTrailHeads[TrailIdx];
        float AccDistance = 0.0f;
        int32 Safety = 0;

        while (Current != INDEX_NONE && Safety++ < ActiveParticles)
        {
            if (Current < 0 || Current >= ActiveParticles)
            {
                RibbonTrailHeads[TrailIdx] = INDEX_NONE;
                break;
            }

            const FBaseParticle* Particle = reinterpret_cast<const FBaseParticle*>(ParticleData + Current
                * ParticleStride);
            FRibbonTrailRuntimePayload* Payload = GetRibbonPayload(Current);
            if (!Particle || !Payload)
            {
                break;
            }

            Payload->TrailIndex = TrailIdx;
            Payload->DistanceFromHead = AccDistance;

            const int32 Next = Particle->NextIndex;
            if (Next == INDEX_NONE || Next < 0 || Next >= ActiveParticles)
            {
                break;
            }

            const FBaseParticle* NextParticle = reinterpret_cast<const FBaseParticle*>(ParticleData + Next
                * ParticleStride);
            if (!NextParticle)
            {
                break;
            }

            AccDistance += FVector::Distance(Particle->Location, NextParticle->Location);
            Current = Next;
        }
    }
}

void FParticleEmitterInstance::AttachRibbonParticle(int32 NewIndex, FRibbonTrailRuntimePayload* Payload)
{
    if (!bHasRibbonTrails || !Payload) return;

    const int32 TrailIndex = (RibbonTrailCount > 0) ? (RibbonSpawnTrailCursor++ % RibbonTrailCount) : 0;
    Payload->TrailIndex = TrailIndex;
    Payload->DistanceFromHead = 0.0f;

    const int32 PrevHead = (RibbonTrailHeads[TrailIndex] >= 0) ? RibbonTrailHeads[TrailIndex] :
        INDEX_NONE;
    RibbonTrailHeads[TrailIndex] = NewIndex;

    if (PrevHead != INDEX_NONE)
    {
        DECLARE_PARTICLE_PTR(HeadParticle, ParticleData, ParticleStride, NewIndex);
        HeadParticle->NextIndex = PrevHead;
    }
    else
    {
        DECLARE_PARTICLE_PTR(HeadParticle, ParticleData, ParticleStride, NewIndex);
        HeadParticle->NextIndex = INDEX_NONE;
    }
}

void FParticleEmitterInstance::DetachRibbonParticle(int32 Index)
{
    if (!bHasRibbonTrails) return;

    DECLARE_PARTICLE_PTR(Particle, ParticleData, ParticleStride, Index);
    auto It = std::find(RibbonTrailHeads.begin(), RibbonTrailHeads.end(), Index);
    int32 TrailIndex = (It != RibbonTrailHeads.end())
        ? std::distance(RibbonTrailHeads.begin(), It)
        : INDEX_NONE;

    if (TrailIndex != INDEX_NONE && RibbonTrailHeads[TrailIndex] == Index)
    {
        RibbonTrailHeads[TrailIndex] = Particle->NextIndex;
    }

    for (int32 i = 0; i < ActiveParticles; ++i)
    {
        if (i == Index) continue;
        DECLARE_PARTICLE_PTR(Other, ParticleData, ParticleStride, i);
        if (Other->NextIndex == Index)
        {
            Other->NextIndex = Particle->NextIndex;
        }
    }
    Particle->NextIndex = INDEX_NONE;
}

void FParticleEmitterInstance::RemapRibbonParticleIndex(int32 FromIndex, int32 ToIndex)
{
    if (!bHasRibbonTrails || FromIndex == ToIndex) return;

    for (int32& Head : RibbonTrailHeads)
    {
        if (Head == FromIndex)
        {
            Head = ToIndex;
        }
    }

    for (int32 i = 0; i < ActiveParticles; ++i)
    {
        DECLARE_PARTICLE_PTR(P, ParticleData, ParticleStride, i);
        if (P->NextIndex == FromIndex)
        {
            P->NextIndex = ToIndex;
        }
    }
}

FRibbonTrailRuntimePayload* FParticleEmitterInstance::GetRibbonPayload(int32 Index) const
{
    if (!bHasRibbonTrails || RibbonPayloadOffset < 0 || !ParticleData) return nullptr;
    if (Index < 0 || Index >= ActiveParticles) return nullptr;

    uint8* BasePtr = ParticleData + Index * ParticleStride;
    return reinterpret_cast<FRibbonTrailRuntimePayload*>(BasePtr + RibbonPayloadOffset);
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
