#pragma once
#include "Vector.h" // uint8
#include "ParticleDataContainer.h"

enum class EDynamicEmitterType : uint8
{
	Sprite,
	Mesh
};

// 나중에 멀티 스레딩 할 때 살리기
//struct FDynamicEmitterReplayDataBase
//{
//    EDynamicEmitterType EmitterType = EDynamicEmitterType::Sprite;
//
//    int32 ActiveParticleCount = 0;
//    int32 ParticleStride = 0;
//
//    FParticleDataContainer DataContainer;
//
//    FVector Scale = FVector::One();
//
//    int32 SortMode = 0; // 0 = none, 1 = back-to-front 등
//};

// Debug용!
struct FDynamicEmitterDataBase
{
	FVector Position;
	float   Size;
	FLinearColor  Color;
};
//struct FDynamicEmitterDataBase
//{
//    int32 EmitterIndex = 0;
//
//    virtual ~FDynamicEmitterDataBase() = default;
//
//    virtual const FDynamicEmitterReplayDataBase& GetSource() const = 0;
//};
//
//struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterReplayDataBase
//{
//
//};