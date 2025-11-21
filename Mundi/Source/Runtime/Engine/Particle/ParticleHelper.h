// 파티클 접근 매크로들
#define DECLARE_PARTICLE_PTR(Particle) \
FBaseParticle* Particle = nullptr

#define DECLARE_PARTICLE(Particle) \
FBaseParticle& Particle = *(FBaseParticle*)nullptr

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
(*((Type*)((uint8*)&Particle + Offset)))
