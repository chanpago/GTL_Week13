#pragma once
#include "Vector.h" // uint8
#include "ParticleDataContainer.h"

enum class EDynamicEmitterType : uint8
{
	Sprite,
	Mesh
};

struct FDynamicEmitterReplayDataBase
{
    EDynamicEmitterType EmitterType = EDynamicEmitterType::Sprite;
    int32 ActiveParticleCount = 0;
    int32 ParticleStride = 0;
    FParticleDataContainer DataContainer;
    FVector Scale = FVector::One();
    int32 SortMode = 0; // 0 = none, 1 = back-to-front 등
};

struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterReplayDataBase
{
};