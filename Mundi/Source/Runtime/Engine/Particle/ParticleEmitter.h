#pragma once

class UParticleLODLevel;
class UParticleSystemComponent;
class UParticleEmitter;
struct FBaseParticle;

// 런타임 Emitter Instance (언리얼 스타일)
struct FParticleEmitterInstance
{
    // Template (에셋)
    UParticleEmitter* SpriteTemplate = nullptr;

    // Owner Component
    UParticleSystemComponent* Component = nullptr;

    // 현재 LOD 레벨
    int32 CurrentLODLevelIndex = 0;
    UParticleLODLevel* CurrentLODLevel = nullptr;

    // ============================================================
    // 파티클 데이터 메모리 레이아웃 (언리얼 스타일)
    // ============================================================

    /** 파티클 데이터 배열 (연속된 메모리 블록) */
    uint8* ParticleData = nullptr;

    /** 파티클 인덱스 배열 (활성 파티클의 인덱스) */
    uint16* ParticleIndices = nullptr;

    /** 인스턴스별 데이터 배열 (모듈별 인스턴스 데이터) */
    uint8* InstanceData = nullptr;

    /** InstanceData 배열의 크기 */
    int32 InstancePayloadSize = 0;

    /** 파티클 데이터의 오프셋 */
    int32 PayloadOffset = 0;

    /** 파티클 하나의 전체 크기 (바이트) */
    int32 ParticleSize = 0;

    /** ParticleData 배열에서 파티클 간 간격 */
    int32 ParticleStride = 0;

    /** 현재 활성화된 파티클 수 */
    int32 ActiveParticles = 0;

    /** 단조 증가 카운터 */
    uint32 ParticleCounter = 0;

    /** 최대 활성 파티클 수 */
    int32 MaxActiveParticles = 0;

    /** 스폰 잔여 시간 (프레임 단위 스폰 처리용) */
    float SpawnFraction = 0.0f;

    /** Emitter 경과 시간 */
    float EmitterTime = 0.0f;

    /** Emitter 수명 */
    float EmitterDuration = 1.0f;

    /** Emitter Loop 카운트 */
    int32 LoopCount = 0;

    // ============================================================
    // 메서드
    // ============================================================

    /** 파티클 생성 */
    void SpawnParticles(int32 Count, float StartTime, float Increment,
                       const FVector& InitialLocation, const FVector& InitialVelocity);

    /** 파티클 제거 */
    void KillParticle(int32 Index);

    /** 파티클 업데이트 */
    void Tick(float DeltaTime);

    /** 메모리 초기화 */
    void InitializeParticleMemory();

    /** 메모리 해제 */
    void FreeParticleMemory();

    /** 특정 인덱스의 파티클 포인터 가져오기 */
    FBaseParticle* GetParticle(int32 Index)
    {
        if (Index >= 0 && Index < ActiveParticles)
        {
            uint16 ParticleIndex = ParticleIndices[Index];
            return (FBaseParticle*)(ParticleData + ParticleIndex * ParticleStride);
        }
        return nullptr;
    }
};

class UParticleEmitter : public UObject
{
public:
    void CacheEmitterModuleInfo();

public:
    TArray<UParticleLODLevel*> LODLevels;

    int32 ParticleSizeBytes = 0;
    int32 MaxParticles = 0;
    float MaxLifetime = 0.f;

};