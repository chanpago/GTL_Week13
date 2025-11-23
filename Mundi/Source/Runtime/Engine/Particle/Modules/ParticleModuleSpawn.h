#pragma once
#include "ParticleModule.h"

enum class ESpawnRateType : uint8
{
    Constant,    // 고정된 스폰 속도
    OverTime,    // 시간에 따라 변화하는 스폰 속도
    Burst        // 특정 시점에 한 번에 대량 생성
};

class UParticleModuleSpawn : public UParticleModule
{
    DECLARE_CLASS(UParticleModuleSpawn, UParticleModule)
public:
    // 스폰 타입
    ESpawnRateType SpawnRateType = ESpawnRateType::Constant;

    // 초당 생성할 파티클 수 (Constant 타입)
    FRawDistributionFloat SpawnRate = FRawDistributionFloat(3.0f);

    // 시간에 따른 스폰 속도 (OverTime 타입)
    FRawDistributionFloat SpawnRateOverTime = FRawDistributionFloat(10.0f, 50.0f);

    // Burst 설정
    struct FBurstEntry
    {
        float Time;       // 버스트 발생 시간 (emitter 수명 기준)
        int32 Count;      // 생성할 파티클 수
        int32 CountRange; // 랜덤 범위 (Count ~ Count+CountRange)

        FBurstEntry() : Time(0.0f), Count(100), CountRange(0) {}
        FBurstEntry(float time, int32 count, int32 range = 0)
            : Time(time), Count(count), CountRange(range) {}
    };

    TArray<FBurstEntry> BurstList;

    // Process Spawn Rate - Emitter에서 이 값을 읽어서 스폰 처리
    float GetSpawnRate(float EmitterTime, float RandomSeed) const
    {
        switch (SpawnRateType)
        {
        case ESpawnRateType::Constant:
            return SpawnRate.GetValue(RandomSeed);

        case ESpawnRateType::OverTime:
            // emitterTime을 0~1 범위로 정규화해서 사용 (emitter의 수명 기준)
            return SpawnRateOverTime.GetValue(EmitterTime);

        case ESpawnRateType::Burst:
            // Burst는 GetSpawnRate가 아닌 별도 처리
            return 0.0f;

        default:
            return 10.0f;
        }
    }

    // Burst 처리 - 특정 시간에 생성할 파티클 수 반환
    int32 GetBurstCount(float OldTime, float NewTime, float RandomSeed) const
    {
        // SpawnRateType이 Burst가 아니어도, BurstList에 값이 있으면 터뜨리는 게 일반적입니다.
        // 하지만 님 규칙(Enum이 Burst일 때만 동작)을 따른다면 아래 코드를 유지하세요.
        if (SpawnRateType != ESpawnRateType::Burst)
            return 0;

        int32 TotalCount = 0;

        for (const FBurstEntry& Burst : BurstList)
        {
            // [핵심 로직]
            // "버스트 시간이 지난 프레임(Old)보다는 크고, 현재 시간(New)보다는 작거나 같은가?"
            // 즉, 이번 프레임의 시간 간격(DeltaTime) 사이에 버스트 타이밍이 끼어있었는지 확인
            if (OldTime < Burst.Time && Burst.Time <= NewTime)
            {
                int32 Count = Burst.Count;
            
                // 랜덤 범위 적용
                if (Burst.CountRange > 0)
                {
                    // RandomSeed는 0.0~1.0 사이의 값이어야 함
                    Count += static_cast<int32>(RandomSeed * Burst.CountRange);
                }
            
                TotalCount += Count;
            }
        }

        return TotalCount;
    }

    // 이 모듈은 개별 파티클에 대해서는 특별한 동작이 없음
    // Spawn/Update는 오버라이드하지 않음 (기본 구현 사용)
};
