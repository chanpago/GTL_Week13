#include "pch.h"
#include "ParticleEmitter.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "ParticleSystemComponent.h"
#include "Modules/ParticleModule.h"
#include "Modules/ParticleModuleRequired.h"
#include "Modules/ParticleModuleSpawn.h"
#include "Modules/ParticleModuleTypeDataBase.h"

void FParticleEmitterInstance::Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent)
{
    Template = InTemplate;
    Component = InComponent;

    UParticleLODLevel* LOD0 = Template->LODLevels[0];
    UParticleModuleRequired* Required = LOD0->RequiredModule;

    if (Required)
    {
        MaxActiveParticles = Required->MaxParticles;
        EmitterDuration = Required->EmitterDuration;
        LoopCount = Required->EmitterLoops;
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
    if (!Template || !Component || !ParticleData) { return; }

    CurrentLODLevel = Template->LODLevels[CurrentLODLevelIndex];
    if (!CurrentLODLevel || !CurrentLODLevel->bEnabled) { return; }

    // Init에서 캐싱 하면 좋을듯
    UParticleModuleRequired* RequiredModule = CurrentLODLevel->RequiredModule;
    UParticleModuleSpawn* SpawnModule = nullptr;
    for (UParticleModule* Module : CurrentLODLevel->SpawnModules)
    {
        SpawnModule = Cast<UParticleModuleSpawn>(Module);
        if (RequiredModule) { break; } 
    }

    float SpawnRate = SpawnModule ?
        SpawnModule->GetSpawnRate(EmitterTime, 0.0f) : RequiredModule->SpawnRateBase;
    float OldSpawnFraction = SpawnFraction;
    SpawnFraction += SpawnRate * DeltaTime;
    int32 NumToSpawn = static_cast<int32>(SpawnFraction);
    
    if (NumToSpawn > 0)
    {
        SpawnFraction -= static_cast<float>(NumToSpawn);

        // 서브 프레임 보간 계산
        float RateDivisor = (SpawnRate > 0.0f) ? SpawnRate : 1.0f;
        float StartTime = -(OldSpawnFraction / RateDivisor);
        float Increment = 1.0f / RateDivisor;

        SpawnParticles(NumToSpawn, StartTime, Increment, Component->GetWorldLocation(), FVector::Zero());
    }

    // 버스트 스폰
    if (SpawnModule)
    {
        float NewTime = EmitterTime + DeltaTime;

        int32 BurstCount = SpawnModule->GetBurstCount(EmitterTime, 0.0f);
        if (BurstCount > 0)
        {
            // 버스트는 시간차 없이 한 번에 생성
            SpawnParticles(BurstCount, 0.0f, 0.0f, Component->GetWorldLocation(), FVector::Zero());
        }
    }
    
    // 살아있는 파티클 순회
    for (int32 i = 0; i < ActiveParticles; i++)
    {
        DECLARE_PARTICLE_PTR(Particle, ParticleData, ParticleStride, i);

        Particle->OldLocation = Particle->Location;
        Particle->Location += Particle->Velocity * DeltaTime;

        if (Particle->OneOverMaxLifetime > 0.0f)
        {
            Particle->RelativeTime += Particle->OneOverMaxLifetime * DeltaTime;
        }

        // 사망 판정
        if (Particle->RelativeTime >= 1.0f)
        {
            KillParticle(i);
            // Swap & Pop이므로 동일한 인덱스 다시 조사해야함
            i--; 
        }
    }
    
    // 현재 LOD 레벨에 있는 Update 모듈들 실행
    for (UParticleModule* Module : CurrentLODLevel->UpdateModules)
    {
        if (Module && Module->bEnabled)
        {
            // 모듈마다 오프셋이 다르다면 여기서 모듈별 오프셋을 넘겨줘야 함
            Module->Update(this, 0, DeltaTime);
        }
    }


    EmitterTime += DeltaTime;
    if (RequiredModule && RequiredModule->EmitterDuration > 0.0f)
    {
        // 수명이 다했으면?
        if (EmitterTime >= RequiredModule->EmitterDuration)
        {
            // 무한 루프(0)거나 아직 횟수가 남았으면 리셋
            if (RequiredModule->EmitterLoops == 0 || LoopCount < RequiredModule->EmitterLoops)
            {
                EmitterTime = 0.0f;
                LoopCount++;
            }
            else
            {
                // 루프 끝남 -> 더 이상 스폰 안 함 (비활성화)
                // Complete 처리는 컴포넌트 레벨에서 수행
            }
        }
    }
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
