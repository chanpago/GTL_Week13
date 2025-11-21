#pragma once

// 파티클 한 알의 기본 스탯
struct FBaseParticle
{
    FVector    Location;
    FVector    OldLocation;
    FVector    Velocity;
    FVector    BaseVelocity;

    FVector    Size;
    FVector    BaseSize;

    FLinearColor Color;
    FLinearColor BaseColor;

    float      Rotation;
    float      BaseRotation;
    float      RotationRate;
    float      BaseRotationRate;

    float      RelativeTime;        // 0 ~ 1
    float      OneOverMaxLifetime;  // 1 / Lifetime

    uint32     Flags;

    // 이 뒤에 모듈별 Payload 데이터
};

// [참조형]
#define DECLARE_PARTICLE(Name, BaseAddress, Stride, Index) \
FBaseParticle& Name = *((FBaseParticle*)( (uint8*)(BaseAddress) + ((Index) * (Stride)) ));

// [포인터형]
#define DECLARE_PARTICLE_PTR(Name, BaseAddress, Stride, Index) \
FBaseParticle* Name = (FBaseParticle*)( (uint8*)(BaseAddress) + ((Index) * (Stride)) );

// [const 참조형]
#define DECLARE_PARTICLE_CONST(Name, BaseAddress, Stride, Index) \
const FBaseParticle& Name = *((const FBaseParticle*)( (uint8*)(BaseAddress) + ((Index) * (Stride)) ));

// 모든 파티클 순회 시작
#define BEGIN_UPDATE_LOOP \
{ \
    int32 ActiveParticleCount = Owner->ActiveParticles; \
    if (ActiveParticleCount > 0) \
    { \
        uint8* ParticleData = Owner->ParticleData; \
        uint16* ParticleIndices = Owner->ParticleIndices; \
        int32 ParticleStride = Owner->ParticleStride; \
        for (int32 i = 0; i < ActiveParticleCount; i++) \
        { \
            int32 CurrentIndex = ParticleIndices[i]; \
            uint8* ParticleBase = ParticleData + CurrentIndex * ParticleStride; \
            FBaseParticle& Particle = *((FBaseParticle*)ParticleBase);

// 모든 파티클 순회 종료
#define END_UPDATE_LOOP \
        } \
    } \
}

// 특정 파티클 인덱스로 접근
#define GET_PARTICLE_PTR(Index) \
(FBaseParticle*)(Owner->ParticleData + Owner->ParticleIndices[Index] * Owner->ParticleStride)

// 모듈별 Payload 데이터 접근
#define PARTICLE_ELEMENT(Type, Offset) \
(*((Type*)((uint8*)&Particle + (Offset))))

inline float FloatHash(uint32 Seed)
{
    // 비트 섞기 (Avalanche Effect)
    Seed ^= Seed << 13;
    Seed ^= Seed >> 17;
    Seed ^= Seed << 5;
    
    // 0 ~ 1 사이 실수로 변환 (맨티사 이용)
    return static_cast<float>(Seed * 16807 & 0x007FFFFF) / static_cast<float>(0x00800000);
}