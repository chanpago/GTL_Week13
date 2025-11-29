#include "pch.h"
#include "ParticleSystem.h"
#include "ParticleEmitter.h"
#include "JsonSerializer.h"
#include <fstream>

IMPLEMENT_CLASS(UParticleSystem)

UParticleSystem::UParticleSystem()
{
    // 디폴트 이미터 생성
    AddEmitter(UParticleEmitter::StaticClass());
    BuildRuntimeCache();
}

UParticleSystem::~UParticleSystem()
{
    for (auto& Emitter: Emitters)
    {
        ObjectFactory::DeleteObject(Emitter);
    }
    Emitters.Empty();
}

void UParticleSystem::BuildRuntimeCache()
{
    MaxActiveParticles = 0;
    MaxLifetime = 0.f;

    for (auto* Emitter : Emitters)
    {
        if (!Emitter) continue;
        Emitter->CacheEmitterModuleInfo();
        MaxActiveParticles += Emitter->MaxParticles;
        MaxLifetime = FMath::Max(MaxLifetime, Emitter->MaxLifetime);
    }
}

UParticleEmitter* UParticleSystem::AddEmitter(UClass* EmitterClass)
{
    if (EmitterClass->IsChildOf(UParticleEmitter::StaticClass()))
    {
        UParticleEmitter* NewEmitter = Cast<UParticleEmitter>(NewObject(EmitterClass));
        Emitters.Add(NewEmitter);
        return NewEmitter;
    }
    
    return nullptr;
}

bool UParticleSystem::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
    JSON Root;
    if (!FJsonSerializer::LoadJsonFromFile(Root, UTF8ToWide(InFilePath)))
    {
        UE_LOG("[UParticleSystem] Failed to load file: %s", InFilePath.c_str());
        return false;
    }

    Serialize(true, Root);
    SetFilePath(NormalizePath(InFilePath)); 
    BuildBodySetupIndexMap();

    UE_LOG("[UParticleSystem] Loaded successfully: %s", InFilePath.c_str());
    return true;
}

bool UParticleSystem::SaveToFile(const FString& FilePath)
{
    JSON JsonData = JSON::Make(JSON::Class::Object);

    Serialize(false, JsonData);

    std::ofstream OutFile(FilePath, std::ios::out);
    if (!OutFile.is_open())
    {
        UE_LOG("Failed to open file for writing: %s", FilePath.c_str());
        return false;
    }

    OutFile << JsonData.dump(4); // Pretty print
    OutFile.close();

    UE_LOG("ParticleSystem saved to: %s", FilePath.c_str());
    return true;
}

void UParticleSystem::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // =========================================================
        // [LOAD] 역직렬화
        // =========================================================
        if (!InOutHandle.hasKey("Type") || InOutHandle["Type"].ToString() != "ParticleSystem") { return; }

        // 기존 데이터 초기화 (Emitters)
        for (UParticleEmitter* Emitter : Emitters)
        {
            if (Emitter) ObjectFactory::DeleteObject(Emitter);
        }
        Emitters.Empty();

        // 기본 정보 로드
        FString NameStr;
        if (FJsonSerializer::ReadString(InOutHandle, "Name", NameStr))
        {
            ObjectName = FName(NameStr);
        }

        // Emitters 배열 로드
        if (InOutHandle.hasKey("Emitters"))
        {
            JSON EmittersArray = InOutHandle["Emitters"];
            for (int32 i = 0; i < EmittersArray.size(); ++i)
            {
                JSON EmitterJson = EmittersArray[i];

                // Emitter 생성
                UParticleEmitter* NewEmitter = Cast<UParticleEmitter>(NewObject(UParticleEmitter::StaticClass()));
                if (NewEmitter)
                {
                    NewEmitter->Serialize(true, EmitterJson);
                    Emitters.Add(NewEmitter);
                }
            }
        }

        // 런타임 캐시 구축 (Bounds 계산 등)
        BuildRuntimeCache();
    }
    else
    {
        // =========================================================
        // [SAVE] 직렬화
        // =========================================================
        InOutHandle["Type"] = "ParticleSystem";
        InOutHandle["Name"] = ObjectName.ToString();

        // Emitters 배열 저장
        JSON EmittersArray = JSON::Make(JSON::Class::Array);
        for (UParticleEmitter* Emitter : Emitters)
        {
            if (!Emitter) continue;

            JSON EmitterJson = JSON::Make(JSON::Class::Object);
            Emitter->Serialize(false, EmitterJson);
            
            EmittersArray.append(EmitterJson);
        }
        InOutHandle["Emitters"] = EmittersArray;
    }
}
