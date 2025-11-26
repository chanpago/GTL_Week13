#include "pch.h"
#include "ParticleModuleSubUV.h"
#include "../ParticleEmitter.h"
#include "../ParticleHelper.h"
#include "ParticleModuleRequired.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitterInstance.h"
#include <algorithm>

IMPLEMENT_CLASS(UParticleModuleSubUV)

UParticleModuleSubUV::UParticleModuleSubUV()
{
    bSpawnModule = true;
    bUpdateModule = true;

    // 기본값: 0~1 사이에서 전체 프레임 훑기
    SubImageIndex = FRawDistributionFloat(0.0f, 1.0f);
    SubImageIndex.bUseRange = false;  // 기본은 고정값 0 (첫 프레임)
}

void UParticleModuleSubUV::Spawn(FParticleEmitterInstance* Owner, int32 Offset, float SpawnTime, FBaseParticle* ParticleBase)
{
    if (!ParticleBase)
        return;

    // Spawn 시점에 초기 SubImageIndex 계산 (랜덤 방식에서 사용)
    float RandomSeed = Owner->GetRandomFloat();
    float InitialIndex = CalculateSubImageIndex(Owner, ParticleBase, RandomSeed);

    // Payload에 저장 (ParticleBase 포인터 기준으로 직접 접근)
    uint8* ParticleBytes = reinterpret_cast<uint8*>(ParticleBase);
    float* SubImageIndexPtr = reinterpret_cast<float*>(ParticleBytes + Offset);
    *SubImageIndexPtr = InitialIndex;

    // UE_LOG("SubUV Spawn: Index=%f", InitialIndex);
}

void UParticleModuleSubUV::Update(FParticleEmitterInstance* Owner, int32 Offset, float DeltaTime)
{
    // Random 계열은 Spawn에서 한 번만 결정하고 Update에서 변경하지 않음
    if (InterpMethod == ESubUVInterpMethod::Random ||
        InterpMethod == ESubUVInterpMethod::RandomBlend)
    {
        return;
    }

    BEGIN_UPDATE_LOOP
    {
        // 매 프레임마다 SubImageIndex 재계산
        float RandomSeed = FloatHash(static_cast<uint32>(reinterpret_cast<uintptr_t>(&Particle)) ^ Owner->ParticleCounter);
        float NewIndex = CalculateSubImageIndex(Owner, &Particle, RandomSeed);

        // Payload에 저장
        float& SubImageIndexPayload = PARTICLE_ELEMENT(float, Offset);
        SubImageIndexPayload = NewIndex;

        // 첫 번째 파티클만 디버그 출력
        FBaseParticle* FirstParticle = reinterpret_cast<FBaseParticle*>(Owner->ParticleData);
        if (&Particle == FirstParticle)
        {
            // UE_LOG("SubUV Update: RelativeTime=%f, NewIndex=%f", Particle.RelativeTime, NewIndex);
        }
    }
    END_UPDATE_LOOP;
}

float UParticleModuleSubUV::CalculateSubImageIndex(FParticleEmitterInstance* Owner, FBaseParticle* Particle, float RandomSeed) const
{
    // Required 모듈에서 타일 수 가져오기
    UParticleModuleRequired* Required = Owner->CachedRequiredModule;
    if (!Required)
    {
        UE_LOG("SubUV: Required module is null!");
        return 0.0f;
    }

    int32 NX = Required->SubImages_Horizontal;
    int32 NY = Required->SubImages_Vertical;
    int32 TotalFrames = NX * NY;

    static int32 CalcLogCounter = 0;
    if (CalcLogCounter++ % 60 == 0)
    {
        // UE_LOG("SubUV Calculate: NX=%d, NY=%d, TotalFrames=%d", NX, NY, TotalFrames);
    }

    if (TotalFrames <= 1)
    {
        return 0.0f;  // 애니메이션 없음
    }

    float Index = 0.0f;

    // 보간 방식에 따라 Index 계산
    switch (InterpMethod)
    {
    case ESubUVInterpMethod::Random:
    case ESubUVInterpMethod::RandomBlend:
    {
        // 랜덤 프레임 선택 (0 ~ TotalFrames-1)
        Index = RandomSeed * (TotalFrames - 1);
        break;
    }

    case ESubUVInterpMethod::None:
    case ESubUVInterpMethod::LinearBlend:
    default:
    {
        // 시간 t 선택
        float t = bUseRealTime ? Owner->EmitterTime : Particle->RelativeTime;

        // 커브에서 인덱스 가져오기
        float NormalizedIndex;
        if (SubImageIndex.bUseRange)
        {
            // Range 모드: MinValue~MaxValue 사이를 t로 보간
            NormalizedIndex = SubImageIndex.GetValue(t);
        }
        else
        {
            // 고정값 모드: t를 직접 사용 (0~1 → 0~1)
            // 사용자가 MinValue를 설정했다면 그 값을 곱함
            NormalizedIndex = t * SubImageIndex.MinValue;
            // MinValue가 0이면 그냥 t를 사용
            if (SubImageIndex.MinValue == 0.0f)
            {
                NormalizedIndex = t;
            }
        }

        // 0~1 범위를 0 ~ (TotalFrames-1) 범위로 스케일링
        Index = NormalizedIndex * (TotalFrames - 1);

        static int32 LinearLogCounter = 0;
        if (LinearLogCounter++ % 60 == 0)
        {
            // UE_LOG("SubUV Linear: t=%f, bUseRange=%d, MinVal=%f, NormalizedIndex=%f, Index=%f",
            //     t, SubImageIndex.bUseRange, SubImageIndex.MinValue, NormalizedIndex, Index);
        }
        break;
    }
    }

    // 범위 클램프 (중요: 보간 시 N-1을 넘으면 다음 프레임이 없어서 터짐)
    Index = std::clamp(Index, 0.0f, static_cast<float>(TotalFrames - 1));

    return Index;
}
