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
public:
    // 스폰 타입
    ESpawnRateType SpawnRateType = ESpawnRateType::Constant;

    // 초당 생성할 파티클 수 (Constant 타입)
    FRawDistributionFloat SpawnRate = FRawDistributionFloat(10.0f);

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
    float GetSpawnRate(float emitterTime, float randomSeed) const
    {
        switch (SpawnRateType)
        {
        case ESpawnRateType::Constant:
            return SpawnRate.GetValue(randomSeed);

        case ESpawnRateType::OverTime:
            // emitterTime을 0~1 범위로 정규화해서 사용 (emitter의 수명 기준)
            return SpawnRateOverTime.GetValue(emitterTime);

        case ESpawnRateType::Burst:
            // Burst는 GetSpawnRate가 아닌 별도 처리
            return 0.0f;

        default:
            return 10.0f;
        }
    }

    // Burst 처리 - 특정 시간에 생성할 파티클 수 반환
    int32 GetBurstCount(float emitterTime, float randomSeed) const
    {
        if (SpawnRateType != ESpawnRateType::Burst)
            return 0;

        int32 totalCount = 0;
        for (const FBurstEntry& burst : BurstList)
        {
            // emitterTime이 버스트 시간과 거의 일치하면 생성
            // (실제로는 이전 프레임과 현재 프레임 사이에 버스트 시간이 있는지 체크)
            if (FMath::Abs(emitterTime - burst.Time) < 0.016f) // ~60fps 기준
            {
                int32 count = burst.Count;
                if (burst.CountRange > 0)
                {
                    count += static_cast<int32>(randomSeed * burst.CountRange);
                }
                totalCount += count;
            }
        }
        return totalCount;
    }

    // 이 모듈은 개별 파티클에 대해서는 특별한 동작이 없음
    // Spawn/Update는 오버라이드하지 않음 (기본 구현 사용)
};
