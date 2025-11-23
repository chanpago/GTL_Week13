#pragma once
#include "Vector.h" // uint8
#include "ParticleDataContainer.h"
#include "ParticleHelper.h"
#include "Modules/ParticleModuleRequired.h"

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
    UMaterialInterface* OverrideMaterial = nullptr; // 있으면 이걸로 덮어씀
    // TODO : GPU Instancing
    // int32        InstanceStride = 0;
    // int32        InstanceCount = 0;
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
// TODO : cpp로 옮겨!
struct FDynamicTranslucentEmitterDataBase : public FDynamicEmitterDataBase
{
    virtual const FDynamicEmitterReplayDataBase* GetSource() const = 0;

    void SortParticles(const FVector& ViewOrigin, const FVector& ViewDir, TArray<int32>& OutIndices) const
    {
        const auto* Src = GetSource();
        const int32 Num = Src->ActiveParticleCount;

        if (Num == 0 || SortMode == EParticleSortMode::None)
            return;

        // 인덱스 초기화
        OutIndices.SetNum(Num);
        for (int32 i = 0; i < Num; ++i)
            OutIndices[i] = i;

        // 정렬 키 계산
        TArray<float> Keys;
        Keys.SetNum(Num);

        for (int32 i = 0; i < Num; ++i)
        {
            FVector Pos = GetParticlePosition(i);

            switch (SortMode)
            {
            case EParticleSortMode::ByDistance:
                Keys[i] = (Pos - ViewOrigin).SizeSquared();
                break;

            case EParticleSortMode::ByViewDepth:
                Keys[i] = FVector::Dot(Pos - ViewOrigin, ViewDir);
                break;

            case EParticleSortMode::ByAge:
                Keys[i] = GetParticleAge(i);
                break;

            default:
                Keys[i] = 0.f;
                break;
            }
        }

        // Back-to-front (큰 값이 먼저)
        OutIndices.Sort([&Keys](int32 A, int32 B) {
            return Keys[A] > Keys[B];
            });
    }

    const FBaseParticle* GetParticle(int32 Idx) const
    {
        const FDynamicEmitterReplayDataBase* Src = GetSource();
        if (!Src || !Src->DataContainer.ParticleData)
            return nullptr;

        const uint8* BasePtr =
            Src->DataContainer.ParticleData +
            Src->ParticleStride * Idx;

        return reinterpret_cast<const FBaseParticle*>(BasePtr);
    }

    FVector GetParticlePosition(int32 Idx) const
    {
        if (const FBaseParticle* P = GetParticle(Idx))
            return P->Location;
        return FVector::Zero();
    }

    float GetParticleAge(int32 Idx) const
    {
        if (const FBaseParticle* P = GetParticle(Idx))
            return P->RelativeTime;
        return 0.0f;
    }
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
};

