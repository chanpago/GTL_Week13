#pragma once

class UParticleLODLevel;
class UParticleSystemComponent;
class UParticleEmitter;
struct FBaseParticle;

// 런타임 Emitter Instance
struct FParticleEmitterInstance
{
    // Template (에셋)
    UParticleEmitter* Template = nullptr;
    // Owner Component
    UParticleSystemComponent* Component = nullptr;
    // 현재 LOD 레벨
    int32 CurrentLODLevelIndex = 0;
    UParticleLODLevel* CurrentLODLevel = nullptr;

    // ============================================================
    // 파티클 데이터 메모리 레이아웃
    // ============================================================
    /** 파티클 데이터 배열 */
    uint8* ParticleData = nullptr;
    /** 파티클 인덱스 배열 */
    uint16* ParticleIndices = nullptr;
    /** 모듈별 인스턴스별 데이터 배열 */
    uint8* InstanceData = nullptr;
    /** InstanceData 배열의 크기 */
    int32 InstancePayloadSize = 0;
    
    /** 추가 페이로드의 시작 오프셋 */
    int32 PayloadOffset = 0;
    /** 기본 파티클 하나의 실제 크기 (패딩 제외) */
    int32 ParticleSize = 0;
    /** ParticleData에서 다음 칸으로 넘어가는 크기 (패딩 포함) */
    int32 ParticleStride = 0;
    
    /** 현재 활성화된 파티클 수 */
    int32 ActiveParticles = 0;
    /** 단조 증가 카운터 (랜덤 시드 용) */
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
    /** 초기화 */
    void Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent);
    
    /** 메모리 초기화 */
    void InitializeParticleMemory();
    /** 메모리 해제 */
    void FreeParticleMemory();

    /**
     * 파티클 생성
     * @param Count 이번 프레임에 생성할 파티클 수
     * @param StartTime 시작 시간
     * @param Increment 생성할 파티클 시간 간격 (DT가 0.16초에 10개 소환이라면 0.016초)
     * @param InitialLocation 초기 Location 
     * @param InitialVelocity 초기 Velocity
     */
    void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity);

    /** 파티클 제거 */
    void KillParticle(int32 Index);

    /** 파티클 업데이트 */
    void Tick(float DeltaTime);
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