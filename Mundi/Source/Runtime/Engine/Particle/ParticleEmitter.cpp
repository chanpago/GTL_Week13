#include "pch.h"
#include "ParticleEmitter.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "Modules/ParticleModule.h"
#include "Modules/ParticleModuleRequired.h"

IMPLEMENT_CLASS(UParticleEmitter)

UParticleEmitter::UParticleEmitter()
{
    RenderType = EParticleType::Sprite;
    AddLODLevel(0);
}

UParticleEmitter::~UParticleEmitter()
{
    for (auto& LODLevel : LODLevels)
    {
        ObjectFactory::DeleteObject(LODLevel);
    }
}

UParticleLODLevel* UParticleEmitter::AddLODLevel(int32 LODIndex)
{
    UParticleLODLevel* LOD = NewObject<UParticleLODLevel>();
    LOD->LODIndex = LODIndex;
    LOD->bEnabled = true;
    LODLevels.Add(LOD);
    return LOD;
}

void UParticleEmitter::CacheEmitterModuleInfo()
{
    // 보통 LOD0 기준으로 캐시 (또는 LOD별 따로 캐시)
    UParticleLODLevel* LOD0 = (LODLevels.Num() > 0) ? LODLevels[0] : nullptr;
    if (!LOD0) return;

    LOD0->RebuildModuleCaches();

    int32 Offset = sizeof(FBaseParticle);

    for (UParticleModule* M : LOD0->AllModulesCache)
    {
        if (!M || !M->bEnabled) continue;

        int32 Bytes = M->GetRequiredBytesPerParticle();
        if (Bytes > 0)
        {
            M->PayloadOffset = Offset;
            // 16바이트 정렬 (SIMD 최적화)
            Offset += (Bytes + 15) & ~15;
        }

        if (M->ModuleType == EParticleModuleType::Required)
        {
            auto* R = static_cast<UParticleModuleRequired*>(M);
            MaxParticles = FMath::Max(MaxParticles, R->MaxParticles);
            MaxLifetime  = FMath::Max(MaxLifetime,  R->EmitterDuration);
        }
    }

    // TypeData가 파티클 구조 확장 요구하면 반영
    // ✅ TypeDataModule이 nullptr일 수 있으므로 체크만 하고 사용 안함
    if (LOD0->TypeDataModule)
    {
        // TypeDataModule 관련 처리는 나중에 구현
        // Offset = FMath::Max(Offset, LOD0->TypeDataModule->GetRequiredParticleBytes());
    }

    ParticleSizeBytes = Offset;
}

void UParticleEmitter::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);
    if (bInIsLoading)
    {
        // =========================================================
        // [LOAD] 역직렬화 로직
        // =========================================================
        // 기존 데이터 초기화 (생성자가 만든 기본값 등 제거)
        for (UParticleLODLevel* LOD : LODLevels)
        {
            ObjectFactory::DeleteObject(LOD);
        }
        LODLevels.Empty();

        // 기본 속성 로드
        FString NameStr;
        if (FJsonSerializer::ReadString(InOutHandle, "Name", NameStr))
        {
            ObjectName = FName(NameStr);
        }

        int32 RenderTypeVal = 0;
        if (FJsonSerializer::ReadInt32(InOutHandle, "RenderType", RenderTypeVal))
        {
            RenderType = static_cast<EParticleType>(RenderTypeVal);
        }

        // LODLevels 배열 로드
        if (InOutHandle.hasKey("LODLevels"))
        {
            JSON LODArray = InOutHandle["LODLevels"];
            for (int i = 0; i < LODArray.size(); ++i)
            {
                JSON LODJson = LODArray[i];
                UParticleLODLevel* NewLOD = Cast<UParticleLODLevel>(NewObject(UParticleLODLevel::StaticClass()));
                if (NewLOD)
                {
                    NewLOD->Serialize(true, LODJson);
                    LODLevels.Add(NewLOD);
                }
            }
        }
    }
    else
    {
        // =========================================================
        // [SAVE] 직렬화 로직
        // =========================================================
        // 기본 속성 저장
        InOutHandle["Name"] = ObjectName.ToString();
        InOutHandle["RenderType"] = static_cast<int32>(RenderType);

        // LODLevels 배열 저장 (하위 객체 위임)
        JSON LODArray = JSON::Make(JSON::Class::Array);
        for (UParticleLODLevel* LOD : LODLevels)
        {
            if (LOD)
            {
                JSON LODJson = JSON::Make(JSON::Class::Object);
                LOD->Serialize(false, LODJson);
                LODArray.append(LODJson);
            }
        }
        InOutHandle["LODLevels"] = LODArray;
    }
}

template<typename T>
T* UParticleEmitter::GetModule(int32 LODIndex) const
{
    // LOD 존재 검증
    if (LODLevels.Num() <= LODIndex || LODLevels[LODIndex] == nullptr)
        return nullptr;

    const UParticleLODLevel* LOD = LODLevels[LODIndex];

    // 캐시가 비었다면 Rebuild 필요
    if (LOD->AllModulesCache.Num() == 0)
        return nullptr;

    for (UParticleModule* M : LOD->AllModulesCache)
    {
        if (!M || !M->bEnabled)
            continue;

        // 템플릿 타입과 정확히 매칭되는 모듈 가져오기
        if (auto* Casted = dynamic_cast<T*>(M))
            return Casted;
    }

    return nullptr;
}