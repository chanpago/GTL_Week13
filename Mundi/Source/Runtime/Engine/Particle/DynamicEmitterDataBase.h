#pragma once
#include "Vector.h" // uint8
#include "ParticleDataContainer.h"

enum class EParticleSortMode : uint8 
{ 
    None, 
    ByDistance, 
    ByViewDepth, 
    ByAge, 
};

struct FDynamicEmitterReplayDataBase
{
    EEmitterRenderType EmitterType = EEmitterRenderType::Sprite;

    int32 ActiveParticleCount = 0; // Vertex Buffer 크기 계산용
    int32 ParticleStride = 0; // 파티클 하나의 byte stride

    FParticleDataContainer DataContainer;  // ParticleData + ParticleIndices
    FVector Scale = FVector::One();

    virtual ~FDynamicEmitterReplayDataBase() = default;
};

struct FDynamicSpriteEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    UMaterialInterface* MaterialInterface = nullptr;
    UParticleModuleRequired* RequiredModule = nullptr;
};

struct FDynamicMeshEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    UStaticMesh* Mesh = nullptr;
    int32        InstanceStride = 0;
    int32        InstanceCount = 0;
};

struct FDynamicEmitterDataBase {
    EEmitterRenderType EmitterType = EEmitterRenderType::Sprite;
    int32 EmitterIndex = 0; bool bUseLocalSpace = false; 
    bool bUseSoftParticle = false; 
    float SoftFadeDistance = 50.0f; // 투명 파티클 정렬 기준 
    
    EParticleSortMode SortMode = EParticleSortMode::None; 
    int32 SortPriority = 0; // Emitter 우선순위 
    virtual ~FDynamicEmitterDataBase() = default; 
    
    virtual const FDynamicEmitterReplayDataBase* GetSource() const = 0; 
};

// FDynamicSpriteEmitterDataBase 에서 헷갈려서 아래 네이밍으로 바꿈
// 정렬 속성 상속을 위한 중간 struct
struct FDynamicTranslucentEmitterDataBase : public FDynamicEmitterDataBase
{
    virtual const FDynamicEmitterReplayDataBase* GetSource() const = 0;

    // ... (SortParticles 그대로)
};

struct FDynamicSpriteEmitterData : public FDynamicTranslucentEmitterDataBase
{
    FDynamicSpriteEmitterReplayData Source;

    FDynamicSpriteEmitterData()
    {
        EmitterType = EEmitterRenderType::Sprite;
    }

    virtual const FDynamicEmitterReplayDataBase* GetSource() const override
    {
        return &Source;
    }

    UMaterialInterface* Material = nullptr;
};

struct FDynamicMeshEmitterData : public FDynamicTranslucentEmitterDataBase
{
    FDynamicMeshEmitterReplayData Source;

    FDynamicMeshEmitterData()
    {
        EmitterType = EEmitterRenderType::Mesh;
    }

    virtual const FDynamicEmitterReplayDataBase* GetSource() const override
    {
        return &Source;
    }

    UMaterialInterface* Material = nullptr;
};