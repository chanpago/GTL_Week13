#include "pch.h"
#include "ParticleLODLevel.h"
#include "Modules/ParticleModuleLifetime.h"
#include "Modules/ParticleModuleRequired.h"
#include "Modules/ParticleModuleSize.h"
#include "Modules/ParticleModuleSpawn.h"
#include "Modules/ParticleModuleVelocity.h"
#include "Modules/ParticleModuleTypeDataBase.h"
#include "JsonSerializer.h"
#include "Modules/ParticleModuleColor.h"
#include "Modules/ParticleModuleLocation.h"
#include "Modules/ParticleModuleColorOverLife.h"
#include "Modules/ParticleModuleSizeMultiplyLife.h"
#include "Modules/ParticleModuleRotation.h"
#include "Modules/ParticleModuleRotationRate.h"
#include "Modules/ParticleModuleMesh.h"
#include "Modules/ParticleModuleSubUV.h"
#include "Material.h"
#include "Modules/ParticleModuleCollision.h"
#include "Modules/ParticleModuleVelocityCone.h"

IMPLEMENT_CLASS(UParticleLODLevel)

UParticleLODLevel::UParticleLODLevel()
{
    // Required 모듈
    UParticleModuleRequired* NewRequired = Cast<UParticleModuleRequired>(AddModule(UParticleModuleRequired::StaticClass()));
    
    // Spawn 모듈
    UParticleModuleSpawn* NewSpawn = Cast<UParticleModuleSpawn>(AddModule(UParticleModuleSpawn::StaticClass()));
    if (NewSpawn)
    {
        NewSpawn->SpawnRateType = ESpawnRateType::Constant;
        NewSpawn->SpawnRate = FRawDistributionFloat(20.0f); // 초당 20개
    }

    // Lifetime 모듈
    UParticleModuleLifetime* NewLifetime = Cast<UParticleModuleLifetime>(AddModule(UParticleModuleLifetime::StaticClass()));
    if (NewLifetime)
    {
        NewLifetime->Lifetime.MinValue = 1.0f;
        NewLifetime->Lifetime.MaxValue = 1.0f;
        NewLifetime->Lifetime.bUseRange = false;
    }

    // Size 모듈
    UParticleModuleSize* NewSize = Cast<UParticleModuleSize>(AddModule(UParticleModuleSize::StaticClass()));
    if (NewSize)
    {
        NewSize->StartSize.MinValue = FVector(10.0f, 10.0f, 10.0f);
        NewSize->StartSize.MaxValue = FVector(10.0f, 10.0f, 10.0f);
        NewSize->StartSize.bUseRange = false;
    }

    // Velocity 모듈
    UParticleModuleVelocity* NewVelocity = Cast<UParticleModuleVelocity>(AddModule(UParticleModuleVelocity::StaticClass()));
    if (NewVelocity)
    {
        NewVelocity->StartVelocity.MinValue = FVector(0.0f, 0.0f, 10.0f);
        NewVelocity->StartVelocity.MaxValue = FVector(0.0f, 0.0f, 20.0f);
        NewVelocity->StartVelocity.bUseRange = true;
    }
    RebuildModuleCaches();
}

UParticleLODLevel::~UParticleLODLevel()
{
    for (UParticleModule* Module: AllModulesCache)
    {
        ObjectFactory::DeleteObject(Module);
    }
}

void UParticleLODLevel::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // -----------------------------------------------------------
        // 생성자 기본값 청소
        // -----------------------------------------------------------
        for (UParticleModule* Module : AllModulesCache)
        {
            ObjectFactory::DeleteObject(Module);
        }
        AllModulesCache.Empty();
        SpawnModules.Empty();
        UpdateModules.Empty();
        RequiredModule = nullptr;
        SpawnModule = nullptr;

        // -----------------------------------------------------------
        // 기본 속성
        // -----------------------------------------------------------
        FJsonSerializer::ReadInt32(InOutHandle, "LODIndex", LODIndex);
        FJsonSerializer::ReadBool(InOutHandle, "bEnabled", bEnabled);

        // -----------------------------------------------------------
        // RequiredModule 로드
        // -----------------------------------------------------------
        if (InOutHandle.hasKey("RequiredModule"))
        {
            JSON& ReqJson = InOutHandle["RequiredModule"];
            auto* Req = Cast<UParticleModuleRequired>(AddModule(UParticleModuleRequired::StaticClass()));
            
            if (Req)
            {
                FJsonSerializer::ReadInt32(ReqJson, "MaxParticles", Req->MaxParticles);
                FJsonSerializer::ReadFloat(ReqJson, "EmitterDuration", Req->EmitterDuration);
                FJsonSerializer::ReadFloat(ReqJson, "EmitterDelay", Req->EmitterDelay);
                FJsonSerializer::ReadInt32(ReqJson, "EmitterLoops", Req->EmitterLoops);
                FJsonSerializer::ReadFloat(ReqJson, "SpawnRateBase", Req->SpawnRateBase);
                
                FJsonSerializer::ReadBool(ReqJson, "bUseLocalSpace", Req->bUseLocalSpace);
                FJsonSerializer::ReadBool(ReqJson, "bKillOnDeactivate", Req->bKillOnDeactivate);
                FJsonSerializer::ReadBool(ReqJson, "bKillOnCompleted", Req->bKillOnCompleted);

                int32 BlendModeVal = 0, AlignVal = 0, SortVal = 0;
                if (FJsonSerializer::ReadInt32(ReqJson, "BlendMode", BlendModeVal)) Req->BlendMode = (EBlendMode)BlendModeVal;
                if (FJsonSerializer::ReadInt32(ReqJson, "ScreenAlignment", AlignVal)) Req->ScreenAlignment = (EScreenAlignment)AlignVal;
                if (FJsonSerializer::ReadInt32(ReqJson, "SortMode", SortVal)) Req->SortMode = (EParticleSortMode)SortVal;

                // Material - 인스턴스 복제하여 파티클별 독립적인 텍스처 설정 지원
                FString MatPath;
                if (FJsonSerializer::ReadString(ReqJson, "Material", MatPath) && !MatPath.empty())
                {
                    UMaterial* BaseMaterial = UResourceManager::GetInstance().Load<UMaterial>(MatPath);
                    if (!BaseMaterial)
                    {
                        UE_LOG("Failed to load material: %s", MatPath.c_str());
                    }
                    else
                    {
                        // 텍스처 경로 읽기
                        FString DiffusePath, NormalPath;
                        FJsonSerializer::ReadString(ReqJson, "DiffuseTexture", DiffusePath);
                        FJsonSerializer::ReadString(ReqJson, "NormalTexture", NormalPath);

                        // 커스텀 텍스처가 있으면 MaterialInstanceDynamic 생성
                        if (!DiffusePath.empty() || !NormalPath.empty())
                        {
                            UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial);
                            if (MID)
                            {
                                auto& RM = UResourceManager::GetInstance();
                                if (!DiffusePath.empty())
                                {
                                    UTexture* DiffuseTex = RM.Load<UTexture>(DiffusePath, true);
                                    if (DiffuseTex) MID->SetTextureParameterValue(EMaterialTextureSlot::Diffuse, DiffuseTex);
                                }
                                if (!NormalPath.empty())
                                {
                                    UTexture* NormalTex = RM.Load<UTexture>(NormalPath, false);
                                    if (NormalTex) MID->SetTextureParameterValue(EMaterialTextureSlot::Normal, NormalTex);
                                }
                                Req->Material = MID;
                            }
                            else
                            {
                                Req->Material = BaseMaterial;
                            }
                        }
                        else
                        {
                            // 커스텀 텍스처 없으면 원본 Material 사용
                            Req->Material = BaseMaterial;
                        }
                    }
                }

                FJsonSerializer::ReadVector(ReqJson, "InitialSize_Min", Req->InitialSize.MinValue);
                FJsonSerializer::ReadVector(ReqJson, "InitialSize_Max", Req->InitialSize.MaxValue);
                FJsonSerializer::ReadBool(ReqJson, "InitialSize_bUseRange", Req->InitialSize.bUseRange);

                FJsonSerializer::ReadLinearColor(ReqJson, "InitialColor_Min", Req->InitialColor.MinValue);
                FJsonSerializer::ReadLinearColor(ReqJson, "InitialColor_Max", Req->InitialColor.MaxValue);
                FJsonSerializer::ReadBool(ReqJson, "InitialColor_bUseRange", Req->InitialColor.bUseRange);

                FJsonSerializer::ReadFloat(ReqJson, "InitialRotation_Min", Req->InitialRotation.MinValue);
                FJsonSerializer::ReadFloat(ReqJson, "InitialRotation_Max", Req->InitialRotation.MaxValue);
                FJsonSerializer::ReadBool(ReqJson, "InitialRotation_bUseRange", Req->InitialRotation.bUseRange);

                // SubUV (스프라이트 시트)
                FJsonSerializer::ReadInt32(ReqJson, "SubImages_Horizontal", Req->SubImages_Horizontal);
                FJsonSerializer::ReadInt32(ReqJson, "SubImages_Vertical", Req->SubImages_Vertical);
            }
        }
        // -----------------------------------------------------------
        // SpawnModule 로드
        // -----------------------------------------------------------
        if (InOutHandle.hasKey("SpawnModule"))
        {
            JSON& SpawnJson = InOutHandle["SpawnModule"];
            auto* Spawn = Cast<UParticleModuleSpawn>(AddModule(UParticleModuleSpawn::StaticClass()));
            
            if (Spawn)
            {
                int32 RateType = 0;
                if (FJsonSerializer::ReadInt32(SpawnJson, "SpawnRateType", RateType)) 
                    Spawn->SpawnRateType = static_cast<ESpawnRateType>(RateType);

                // 2. Constant Spawn Rate
                FJsonSerializer::ReadFloat(SpawnJson, "SpawnRate_Min", Spawn->SpawnRate.MinValue);
                FJsonSerializer::ReadFloat(SpawnJson, "SpawnRate_Max", Spawn->SpawnRate.MaxValue);
                FJsonSerializer::ReadBool(SpawnJson, "SpawnRate_bUseRange", Spawn->SpawnRate.bUseRange);

                // 3. OverTime Spawn Rate
                FJsonSerializer::ReadFloat(SpawnJson, "SpawnRateOverTime_Min", Spawn->SpawnRateOverTime.MinValue);
                FJsonSerializer::ReadFloat(SpawnJson, "SpawnRateOverTime_Max", Spawn->SpawnRateOverTime.MaxValue);
                FJsonSerializer::ReadBool(SpawnJson, "SpawnRateOverTime_bUseRange", Spawn->SpawnRateOverTime.bUseRange);

                // 4. Burst List
                if (SpawnJson.hasKey("BurstList"))
                {
                    JSON BurstArray = SpawnJson["BurstList"];
        
                    // JSON 배열 순회
                    int32 ArraySize = BurstArray.size();
                    for (int32 i = 0; i < ArraySize; ++i)
                    {
                        JSON EntryJson = BurstArray[i];
            
                        UParticleModuleSpawn::FBurstEntry Entry;
                        FJsonSerializer::ReadFloat(EntryJson, "Time", Entry.Time);
                        FJsonSerializer::ReadInt32(EntryJson, "Count", Entry.Count);
                        FJsonSerializer::ReadInt32(EntryJson, "CountRange", Entry.CountRange);
            
                        Spawn->BurstList.Add(Entry);
                    }
                }
            }
        }
        // -----------------------------------------------------------
        // 일반 Modules 배열 로드
        // -----------------------------------------------------------
        if (InOutHandle.hasKey("Modules"))
        {
            JSON& ModulesArray = InOutHandle["Modules"];
            for (int32 i = 0; i < ModulesArray.length(); i++)
            {
                ParseAndAddModule(ModulesArray[i]); // 위임
            }
        }

        // -----------------------------------------------------------
        // 최종 캐시 재구축
        // -----------------------------------------------------------
        RebuildModuleCaches();
    }
    else
    {
        // =========================================================
        // [SAVE] 직렬화
        // =========================================================
        InOutHandle["LODIndex"] = LODIndex;
        InOutHandle["bEnabled"] = bEnabled;

        // RequiredModule 저장
        if (RequiredModule)
        {
            JSON RequiredJson = JSON::Make(JSON::Class::Object);
            RequiredJson["MaxParticles"] = RequiredModule->MaxParticles;
            RequiredJson["EmitterDuration"] = RequiredModule->EmitterDuration;
            RequiredJson["EmitterDelay"] = RequiredModule->EmitterDelay;
            RequiredJson["EmitterLoops"] = RequiredModule->EmitterLoops;
            RequiredJson["SpawnRateBase"] = RequiredModule->SpawnRateBase;
            RequiredJson["bUseLocalSpace"] = RequiredModule->bUseLocalSpace;
            RequiredJson["bKillOnDeactivate"] = RequiredModule->bKillOnDeactivate;
            RequiredJson["bKillOnCompleted"] = RequiredModule->bKillOnCompleted;
            RequiredJson["BlendMode"] = static_cast<int>(RequiredModule->BlendMode);
            RequiredJson["ScreenAlignment"] = static_cast<int>(RequiredModule->ScreenAlignment);
            RequiredJson["SortMode"] = static_cast<int>(RequiredModule->SortMode);

            // Material
            if (RequiredModule->Material)
            {
                // MaterialInstanceDynamic인 경우 부모 Material 경로 저장
                UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(RequiredModule->Material);
                if (MID && MID->GetParentMaterial())
                {
                    RequiredJson["Material"] = MID->GetParentMaterial()->GetFilePath();

                    // 오버라이드된 텍스처 경로 저장
                    const auto& OverriddenTextures = MID->GetOverriddenTextures();
                    auto DiffuseIt = OverriddenTextures.find(EMaterialTextureSlot::Diffuse);
                    if (DiffuseIt != OverriddenTextures.end() && DiffuseIt->second)
                    {
                        RequiredJson["DiffuseTexture"] = DiffuseIt->second->GetFilePath();
                    }
                    auto NormalIt = OverriddenTextures.find(EMaterialTextureSlot::Normal);
                    if (NormalIt != OverriddenTextures.end() && NormalIt->second)
                    {
                        RequiredJson["NormalTexture"] = NormalIt->second->GetFilePath();
                    }
                }
                else
                {
                    // 일반 UMaterial인 경우
                    RequiredJson["Material"] = RequiredModule->Material->GetFilePath();

                    // Material 내 텍스처 경로 저장
                    FMaterialInfo MatInfo = RequiredModule->Material->GetMaterialInfo();
                    if (!MatInfo.DiffuseTextureFileName.empty())
                    {
                        RequiredJson["DiffuseTexture"] = MatInfo.DiffuseTextureFileName;
                    }
                    if (!MatInfo.NormalTextureFileName.empty())
                    {
                        RequiredJson["NormalTexture"] = MatInfo.NormalTextureFileName;
                    }
                }
            }

            // InitialSize
            RequiredJson["InitialSize_Min"] = FJsonSerializer::VectorToJson(RequiredModule->InitialSize.MinValue);
            RequiredJson["InitialSize_Max"] = FJsonSerializer::VectorToJson(RequiredModule->InitialSize.MaxValue);
            RequiredJson["InitialSize_bUseRange"] = RequiredModule->InitialSize.bUseRange;

            // InitialColor
            RequiredJson["InitialColor_Min"] = FJsonSerializer::LinearColorToJson(RequiredModule->InitialColor.MinValue);
            RequiredJson["InitialColor_Max"] = FJsonSerializer::LinearColorToJson(RequiredModule->InitialColor.MaxValue);
            RequiredJson["InitialColor_bUseRange"] = RequiredModule->InitialColor.bUseRange;

            // InitialRotation
            RequiredJson["InitialRotation_Min"] = RequiredModule->InitialRotation.MinValue;
            RequiredJson["InitialRotation_Max"] = RequiredModule->InitialRotation.MaxValue;
            RequiredJson["InitialRotation_bUseRange"] = RequiredModule->InitialRotation.bUseRange;

            // SubUV (스프라이트 시트)
            RequiredJson["SubImages_Horizontal"] = RequiredModule->SubImages_Horizontal;
            RequiredJson["SubImages_Vertical"] = RequiredModule->SubImages_Vertical;

            InOutHandle["RequiredModule"] = RequiredJson;
        }

        // SpawnModule 저장
        if (SpawnModule)
        {
            JSON SpawnJson = JSON::Make(JSON::Class::Object);

            // 1. 기본 Rate Type
            SpawnJson["SpawnRateType"] = static_cast<int32>(SpawnModule->SpawnRateType);

            // 2. Constant Spawn Rate
            SpawnJson["SpawnRate_Min"] = SpawnModule->SpawnRate.MinValue;
            SpawnJson["SpawnRate_Max"] = SpawnModule->SpawnRate.MaxValue;
            SpawnJson["SpawnRate_bUseRange"] = SpawnModule->SpawnRate.bUseRange;

            // 3. OverTime Spawn Rate
            SpawnJson["SpawnRateOverTime_Min"] = SpawnModule->SpawnRateOverTime.MinValue;
            SpawnJson["SpawnRateOverTime_Max"] = SpawnModule->SpawnRateOverTime.MaxValue;
            SpawnJson["SpawnRateOverTime_bUseRange"] = SpawnModule->SpawnRateOverTime.bUseRange;

            // 4. Burst List (배열 처리 추가됨)
            JSON BurstArray = JSON::Make(JSON::Class::Array);
            for (const auto& Entry : SpawnModule->BurstList)
            {
                JSON EntryJson = JSON::Make(JSON::Class::Object);
                EntryJson["Time"] = Entry.Time;
                EntryJson["Count"] = Entry.Count;
                EntryJson["CountRange"] = Entry.CountRange;
        
                BurstArray.append(EntryJson);
            }
            SpawnJson["BurstList"] = BurstArray;
            InOutHandle["SpawnModule"] = SpawnJson;
        }
        // 일반 Modules 배열 저장
        JSON ModulesArray = JSON::Make(JSON::Class::Array);
        
        for (UParticleModule* Module : AllModulesCache)
        {
            // 특수 모듈(Required, Spawn)은 중복 저장 방지
            if (Module == RequiredModule || Module == SpawnModule) continue;

            JSON ModJson = SerializeModule(Module);
            if (ModJson.hasKey("Type")) 
            {
                ModulesArray.append(ModJson);
            }
        }

        InOutHandle["Modules"] = ModulesArray;
    }
}

UParticleModule* UParticleLODLevel::AddModule(UClass* ParticleModuleClass)
{
    if (!ParticleModuleClass || !ParticleModuleClass->IsChildOf(UParticleModule::StaticClass()))
    {
        return nullptr;
    }

    UParticleModule* NewModule = Cast<UParticleModule>(NewObject(ParticleModuleClass));
    AllModulesCache.Add(NewModule);

    // 캐시 재구성하여 SpawnModules, UpdateModules에 추가
    RebuildModuleCaches();

    UE_LOG("AddModule: Added %s, now rebuilding caches", ParticleModuleClass->Name);

    return NewModule;
}

void UParticleLODLevel::RemoveModule(UParticleModule* Module)
{
    if (!Module)
    {
        UE_LOG("RemoveModule: Module is null");
        return;
    }

    UE_LOG("RemoveModule: Attempting to remove module: %s", Module->GetClass()->Name);

    // Required, Spawn 모듈은 삭제 불가
    if (Cast<UParticleModuleRequired>(Module))
    {
        UE_LOG("Required 모듈은 삭제할 수 없습니다.");
        return;
    }

    if (Cast<UParticleModuleSpawn>(Module))
    {
        UE_LOG("Spawn 모듈은 삭제할 수 없습니다.");
        return;
    }

    // AllModulesCache에서 제거
    int32 RemovedCount = AllModulesCache.Remove(Module);
    UE_LOG("RemoveModule: Removed %d instances from AllModulesCache", RemovedCount);

    // 모듈 메모리 해제
    ObjectFactory::DeleteObject(Module);
    UE_LOG("RemoveModule: Module deleted");

    // 캐시 재구성
    //RebuildModuleCaches();
    UE_LOG("RemoveModule: Caches rebuilt. AllModulesCache size: %d", AllModulesCache.Num());
}

void UParticleLODLevel::RebuildModuleCaches()
{
    SpawnModules.Empty();
    UpdateModules.Empty();

    RequiredModule = nullptr;
    SpawnModule = nullptr;
    TypeDataModule = nullptr;

    for (int32 i = 0; i < AllModulesCache.Num(); i++)
    {
        UParticleModule* Module = AllModulesCache[i];
        if (!Module) continue;

        if (UParticleModuleRequired* Req = Cast<UParticleModuleRequired>(Module))
        {
            RequiredModule = Req;
        }
        else if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
        {
            SpawnModule = Spawn;
        }
        else if (UParticleModuleTypeDataBase* TypeData = Cast<UParticleModuleTypeDataBase>(Module))
        {
            TypeDataModule = TypeData;
        }

        if (Module->bSpawnModule)
        {
            SpawnModules.Add(Module);
        }
        if (Module->bUpdateModule)
        {
            UpdateModules.Add(Module);
        }
    }
}

void UParticleLODLevel::ParseAndAddModule(JSON& ModuleJson)
{
    if (!ModuleJson.hasKey("Type")) return;

    FString ModuleType = ModuleJson["Type"].ToString();
    UParticleModule* NewModule = nullptr;

    if (ModuleType == "Lifetime")
    {
        auto* Lifetime = Cast<UParticleModuleLifetime>(AddModule(UParticleModuleLifetime::StaticClass()));
        if (Lifetime)
        {
            FJsonSerializer::ReadFloat(ModuleJson, "Lifetime_Min", Lifetime->Lifetime.MinValue);
            FJsonSerializer::ReadFloat(ModuleJson, "Lifetime_Max", Lifetime->Lifetime.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "Lifetime_bUseRange", Lifetime->Lifetime.bUseRange);
        }
        NewModule = Lifetime;
    }
    else if (ModuleType == "Velocity")
    {
        auto* Velocity = Cast<UParticleModuleVelocity>(AddModule(UParticleModuleVelocity::StaticClass()));
        if (Velocity)
        {
            FJsonSerializer::ReadVector(ModuleJson, "StartVelocity_Min", Velocity->StartVelocity.MinValue);
            FJsonSerializer::ReadVector(ModuleJson, "StartVelocity_Max", Velocity->StartVelocity.MaxValue);
            FJsonSerializer::ReadVector(ModuleJson, "Gravity", Velocity->Gravity);
            FJsonSerializer::ReadBool(ModuleJson, "StartVelocity_bUseRange", Velocity->StartVelocity.bUseRange);
        }
        NewModule = Velocity;
    }
    else if (ModuleType == "Size")
    {
        auto* Size = Cast<UParticleModuleSize>(AddModule(UParticleModuleSize::StaticClass()));
        if (Size)
        {
            FJsonSerializer::ReadVector(ModuleJson, "StartSize_Min", Size->StartSize.MinValue);
            FJsonSerializer::ReadVector(ModuleJson, "StartSize_Max", Size->StartSize.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "StartSize_bUseRange", Size->StartSize.bUseRange);
        }
        NewModule = Size;
    }
    else if (ModuleType == "Color")
    {
        auto* Color = Cast<UParticleModuleColor>(AddModule(UParticleModuleColor::StaticClass()));
        if (Color)
        {
            FJsonSerializer::ReadLinearColor(ModuleJson, "StartColor_Min", Color->StartColor.MinValue);
            FJsonSerializer::ReadLinearColor(ModuleJson, "StartColor_Max", Color->StartColor.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "StartColor_bUseRange", Color->StartColor.bUseRange);
        }
        NewModule = Color;
    }
    else if (ModuleType == "Location")
    {
        auto* Location = Cast<UParticleModuleLocation>(AddModule(UParticleModuleLocation::StaticClass()));
        if (Location)
        {
            int32 DistType = 0;
            FJsonSerializer::ReadInt32(ModuleJson, "DistributionType", DistType);
            Location->DistributionType = (ELocationDistributionType)DistType;

            FJsonSerializer::ReadVector(ModuleJson, "StartLocation_Min", Location->StartLocation.MinValue);
            FJsonSerializer::ReadVector(ModuleJson, "StartLocation_Max", Location->StartLocation.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "StartLocation_bUseRange", Location->StartLocation.bUseRange);
            FJsonSerializer::ReadVector(ModuleJson, "BoxExtent", Location->BoxExtent);
            FJsonSerializer::ReadFloat(ModuleJson, "SphereRadius", Location->SphereRadius);
            FJsonSerializer::ReadFloat(ModuleJson, "CylinderRadius", Location->CylinderRadius);
            FJsonSerializer::ReadFloat(ModuleJson, "CylinderHeight", Location->CylinderHeight);
        }
        NewModule = Location;
    }
    else if (ModuleType == "ColorOverLife")
    {
        auto* ColorOverLife = Cast<UParticleModuleColorOverLife>(AddModule(UParticleModuleColorOverLife::StaticClass()));
        if (ColorOverLife)
        {
            FJsonSerializer::ReadLinearColor(ModuleJson, "ColorOverLife_Min", ColorOverLife->ColorOverLife.MinValue);
            FJsonSerializer::ReadLinearColor(ModuleJson, "ColorOverLife_Max", ColorOverLife->ColorOverLife.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "ColorOverLife_bUseRange", ColorOverLife->ColorOverLife.bUseRange);
            FJsonSerializer::ReadFloat(ModuleJson, "AlphaOverLife_Min", ColorOverLife->AlphaOverLife.MinValue);
            FJsonSerializer::ReadFloat(ModuleJson, "AlphaOverLife_Max", ColorOverLife->AlphaOverLife.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "AlphaOverLife_bUseRange", ColorOverLife->AlphaOverLife.bUseRange);

            // Alpha 커브 2-point 시스템
            FJsonSerializer::ReadFloat(ModuleJson, "AlphaPoint1Time", ColorOverLife->AlphaPoint1Time);
            FJsonSerializer::ReadFloat(ModuleJson, "AlphaPoint1Value", ColorOverLife->AlphaPoint1Value);
            FJsonSerializer::ReadFloat(ModuleJson, "AlphaPoint2Time", ColorOverLife->AlphaPoint2Time);
            FJsonSerializer::ReadFloat(ModuleJson, "AlphaPoint2Value", ColorOverLife->AlphaPoint2Value);

            FJsonSerializer::ReadBool(ModuleJson, "bUseColorOverLife", ColorOverLife->bUseColorOverLife);
            FJsonSerializer::ReadBool(ModuleJson, "bUseAlphaOverLife", ColorOverLife->bUseAlphaOverLife);
        }
        NewModule = ColorOverLife;
    }
    else if (ModuleType == "SizeMultiplyLife")
    {
        auto* SizeMultiplyLife = Cast<UParticleModuleSizeMultiplyLife>(AddModule(UParticleModuleSizeMultiplyLife::StaticClass()));
        if (SizeMultiplyLife)
        {
            FJsonSerializer::ReadFloat(ModuleJson, "Point1Time", SizeMultiplyLife->Point1Time);
            FJsonSerializer::ReadVector(ModuleJson, "Point1Value", SizeMultiplyLife->Point1Value);
            FJsonSerializer::ReadFloat(ModuleJson, "Point2Time", SizeMultiplyLife->Point2Time);
            FJsonSerializer::ReadVector(ModuleJson, "Point2Value", SizeMultiplyLife->Point2Value);
            FJsonSerializer::ReadBool(ModuleJson, "bMultiplyX", SizeMultiplyLife->bMultiplyX);
            FJsonSerializer::ReadBool(ModuleJson, "bMultiplyY", SizeMultiplyLife->bMultiplyY);
            FJsonSerializer::ReadBool(ModuleJson, "bMultiplyZ", SizeMultiplyLife->bMultiplyZ);
        }
        NewModule = SizeMultiplyLife;
    }
    else if (ModuleType == "Rotation")
    {
        auto* Rotation = Cast<UParticleModuleRotation>(AddModule(UParticleModuleRotation::StaticClass()));
        if (Rotation)
        {
            FJsonSerializer::ReadFloat(ModuleJson, "StartRotation_Min", Rotation->StartRotation.MinValue);
            FJsonSerializer::ReadFloat(ModuleJson, "StartRotation_Max", Rotation->StartRotation.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "StartRotation_bUseRange", Rotation->StartRotation.bUseRange);
        }
        NewModule = Rotation;
    }
    else if (ModuleType == "RotationRate")
    {
        auto* RotationRate = Cast<UParticleModuleRotationRate>(AddModule(UParticleModuleRotationRate::StaticClass()));
        if (RotationRate)
        {
            FJsonSerializer::ReadFloat(ModuleJson, "InitialRotation_Min", RotationRate->InitialRotation.MinValue);
            FJsonSerializer::ReadFloat(ModuleJson, "InitialRotation_Max", RotationRate->InitialRotation.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "InitialRotation_bUseRange", RotationRate->InitialRotation.bUseRange);

            FJsonSerializer::ReadFloat(ModuleJson, "StartRotationRate_Min", RotationRate->StartRotationRate.MinValue);
            FJsonSerializer::ReadFloat(ModuleJson, "StartRotationRate_Max", RotationRate->StartRotationRate.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "StartRotationRate_bUseRange", RotationRate->StartRotationRate.bUseRange);
        }
        NewModule = RotationRate;
    }
    else if (ModuleType == "Mesh")
    {
        auto* Mesh = Cast<UParticleModuleMesh>(AddModule(UParticleModuleMesh::StaticClass()));
        if (Mesh)
        {
            FJsonSerializer::ReadString(ModuleJson, "MeshAssetPath", Mesh->MeshAssetPath);
            FJsonSerializer::ReadString(ModuleJson, "OverrideMaterialPath", Mesh->OverrideMaterialPath);
            FJsonSerializer::ReadBool(ModuleJson, "bUseMeshMaterials", Mesh->bUseMeshMaterials);
        }
        NewModule = Mesh;
    }
    else if (ModuleType == "SubUV")
    {
        auto* SubUV = Cast<UParticleModuleSubUV>(AddModule(UParticleModuleSubUV::StaticClass()));
        if (SubUV)
        {
            FJsonSerializer::ReadFloat(ModuleJson, "SubImageIndex_Min", SubUV->SubImageIndex.MinValue);
            FJsonSerializer::ReadFloat(ModuleJson, "SubImageIndex_Max", SubUV->SubImageIndex.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "SubImageIndex_bUseRange", SubUV->SubImageIndex.bUseRange);

            int32 InterpMethodVal = 0;
            if (FJsonSerializer::ReadInt32(ModuleJson, "InterpMethod", InterpMethodVal))
                SubUV->InterpMethod = static_cast<ESubUVInterpMethod>(InterpMethodVal);

            FJsonSerializer::ReadBool(ModuleJson, "bUseRealTime", SubUV->bUseRealTime);
        }
        NewModule = SubUV;
    }
    else if (ModuleType == "Collision")
    {
        auto* Collision = Cast<UParticleModuleCollision>(AddModule(UParticleModuleCollision::StaticClass()));
        if (Collision)
        {
            int32 ResponseVal = 0;
            if (FJsonSerializer::ReadInt32(ModuleJson, "CollisionResponse", ResponseVal))
            {
                Collision->CollisionResponse = static_cast<EParticleCollisionResponse>(ResponseVal);
            }
            FJsonSerializer::ReadFloat(ModuleJson, "Restitution", Collision->Restitution);
            FJsonSerializer::ReadFloat(ModuleJson, "Friction", Collision->Friction);
            FJsonSerializer::ReadFloat(ModuleJson, "RadiusScale", Collision->RadiusScale);
            FJsonSerializer::ReadBool(ModuleJson, "bWriteEvent", Collision->bWriteEvent);
        }
        NewModule = Collision;
    }
    else if (ModuleType == "VelocityCone")
    {
        auto* Cone = Cast<UParticleModuleVelocityCone>(AddModule(UParticleModuleVelocityCone::StaticClass()));
        if (Cone)
        {
            // 1. Direction
            // (기본값으로 0,0,1 등을 가지고 있으니, 읽기 실패해도 안전하도록 개별 처리)
            FJsonSerializer::ReadFloat(ModuleJson, "Direction_X", Cone->Direction.X);
            FJsonSerializer::ReadFloat(ModuleJson, "Direction_Y", Cone->Direction.Y);
            FJsonSerializer::ReadFloat(ModuleJson, "Direction_Z", Cone->Direction.Z);
        
            // 로드 후 정규화(Normalize)를 한 번 해주는 것이 안전함
            if (!Cone->Direction.IsZero())
            {
                Cone->Direction.Normalize();
            }

            // 2. Angle
            FJsonSerializer::ReadFloat(ModuleJson, "Angle_Min", Cone->Angle.MinValue);
            FJsonSerializer::ReadFloat(ModuleJson, "Angle_Max", Cone->Angle.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "Angle_bUseRange", Cone->Angle.bUseRange);

            // 3. Velocity
            FJsonSerializer::ReadFloat(ModuleJson, "Velocity_Min", Cone->Velocity.MinValue);
            FJsonSerializer::ReadFloat(ModuleJson, "Velocity_Max", Cone->Velocity.MaxValue);
            FJsonSerializer::ReadBool(ModuleJson, "Velocity_bUseRange", Cone->Velocity.bUseRange);
        }
        NewModule = Cone;
    }

    if (NewModule)
    {
        FJsonSerializer::ReadBool(ModuleJson, "bEnabled", NewModule->bEnabled, true);
    }
}

JSON UParticleLODLevel::SerializeModule(UParticleModule* Module)
{
    if (!Module) { return JSON(); }

    JSON ModuleJson = JSON::Make(JSON::Class::Object);
    ModuleJson["bEnabled"] = Module->bEnabled;

    // 모듈 타입 판별 및 저장
    if (auto* Lifetime = Cast<UParticleModuleLifetime>(Module))
    {
        ModuleJson["Type"] = "Lifetime";
        ModuleJson["Lifetime_Min"] = Lifetime->Lifetime.MinValue;
        ModuleJson["Lifetime_Max"] = Lifetime->Lifetime.MaxValue;
        ModuleJson["Lifetime_bUseRange"] = Lifetime->Lifetime.bUseRange;
    }
    else if (auto* Velocity = Cast<UParticleModuleVelocity>(Module))
    {
        ModuleJson["Type"] = "Velocity";
        ModuleJson["StartVelocity_Min"] = FJsonSerializer::VectorToJson(Velocity->StartVelocity.MinValue);
        ModuleJson["StartVelocity_Max"] = FJsonSerializer::VectorToJson(Velocity->StartVelocity.MaxValue);
        ModuleJson["Gravity"] = FJsonSerializer::VectorToJson(Velocity->Gravity);
        ModuleJson["StartVelocity_bUseRange"] = Velocity->StartVelocity.bUseRange;
    }
    else if (auto* Size = Cast<UParticleModuleSize>(Module))
    {
        ModuleJson["Type"] = "Size";
        ModuleJson["StartSize_Min"] = FJsonSerializer::VectorToJson(Size->StartSize.MinValue);
        ModuleJson["StartSize_Max"] = FJsonSerializer::VectorToJson(Size->StartSize.MaxValue);
        ModuleJson["StartSize_bUseRange"] = Size->StartSize.bUseRange;
    }
    else if (auto* Color = Cast<UParticleModuleColor>(Module))
    {
        ModuleJson["Type"] = "Color";
        ModuleJson["StartColor_Min"] = FJsonSerializer::LinearColorToJson(Color->StartColor.MinValue);
        ModuleJson["StartColor_Max"] = FJsonSerializer::LinearColorToJson(Color->StartColor.MaxValue);
        ModuleJson["StartColor_bUseRange"] = Color->StartColor.bUseRange;
    }
    else if (auto* Location = Cast<UParticleModuleLocation>(Module))
    {
        ModuleJson["Type"] = "Location";
        ModuleJson["DistributionType"] = (int)Location->DistributionType;
        ModuleJson["StartLocation_Min"] = FJsonSerializer::VectorToJson(Location->StartLocation.MinValue);
        ModuleJson["StartLocation_Max"] = FJsonSerializer::VectorToJson(Location->StartLocation.MaxValue);
        ModuleJson["StartLocation_bUseRange"] = Location->StartLocation.bUseRange;
        ModuleJson["BoxExtent"] = FJsonSerializer::VectorToJson(Location->BoxExtent);
        ModuleJson["SphereRadius"] = Location->SphereRadius;
        ModuleJson["CylinderRadius"] = Location->CylinderRadius;
        ModuleJson["CylinderHeight"] = Location->CylinderHeight;
    }
    else if (auto* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
    {
        ModuleJson["Type"] = "ColorOverLife";
        ModuleJson["ColorOverLife_Min"] = FJsonSerializer::LinearColorToJson(ColorOverLife->ColorOverLife.MinValue);
        ModuleJson["ColorOverLife_Max"] = FJsonSerializer::LinearColorToJson(ColorOverLife->ColorOverLife.MaxValue);
        ModuleJson["ColorOverLife_bUseRange"] = ColorOverLife->ColorOverLife.bUseRange;
        ModuleJson["AlphaOverLife_Min"] = ColorOverLife->AlphaOverLife.MinValue;
        ModuleJson["AlphaOverLife_Max"] = ColorOverLife->AlphaOverLife.MaxValue;
        ModuleJson["AlphaOverLife_bUseRange"] = ColorOverLife->AlphaOverLife.bUseRange;

        // Alpha 커브 2-point 시스템
        ModuleJson["AlphaPoint1Time"] = ColorOverLife->AlphaPoint1Time;
        ModuleJson["AlphaPoint1Value"] = ColorOverLife->AlphaPoint1Value;
        ModuleJson["AlphaPoint2Time"] = ColorOverLife->AlphaPoint2Time;
        ModuleJson["AlphaPoint2Value"] = ColorOverLife->AlphaPoint2Value;

        ModuleJson["bUseColorOverLife"] = ColorOverLife->bUseColorOverLife;
        ModuleJson["bUseAlphaOverLife"] = ColorOverLife->bUseAlphaOverLife;
    }
    else if (auto* SizeMultiplyLife = Cast<UParticleModuleSizeMultiplyLife>(Module))
    {
        ModuleJson["Type"] = "SizeMultiplyLife";
        ModuleJson["Point1Time"] = SizeMultiplyLife->Point1Time;
        ModuleJson["Point1Value"] = FJsonSerializer::VectorToJson(SizeMultiplyLife->Point1Value);
        ModuleJson["Point2Time"] = SizeMultiplyLife->Point2Time;
        ModuleJson["Point2Value"] = FJsonSerializer::VectorToJson(SizeMultiplyLife->Point2Value);
        ModuleJson["bMultiplyX"] = SizeMultiplyLife->bMultiplyX;
        ModuleJson["bMultiplyY"] = SizeMultiplyLife->bMultiplyY;
        ModuleJson["bMultiplyZ"] = SizeMultiplyLife->bMultiplyZ;
    }
    else if (auto* Rotation = Cast<UParticleModuleRotation>(Module))
    {
        ModuleJson["Type"] = "Rotation";
        ModuleJson["StartRotation_Min"] = Rotation->StartRotation.MinValue;
        ModuleJson["StartRotation_Max"] = Rotation->StartRotation.MaxValue;
        ModuleJson["StartRotation_bUseRange"] = Rotation->StartRotation.bUseRange;
    }
    else if (auto* RotationRate = Cast<UParticleModuleRotationRate>(Module))
    {
        ModuleJson["Type"] = "RotationRate";
        ModuleJson["InitialRotation_Min"] = RotationRate->InitialRotation.MinValue;
        ModuleJson["InitialRotation_Max"] = RotationRate->InitialRotation.MaxValue;
        ModuleJson["InitialRotation_bUseRange"] = RotationRate->InitialRotation.bUseRange;

        ModuleJson["StartRotationRate_Min"] = RotationRate->StartRotationRate.MinValue;
        ModuleJson["StartRotationRate_Max"] = RotationRate->StartRotationRate.MaxValue;
        ModuleJson["StartRotationRate_bUseRange"] = RotationRate->StartRotationRate.bUseRange;
    }
    else if (auto* Mesh = Cast<UParticleModuleMesh>(Module))
    {
        ModuleJson["Type"] = "Mesh";
        ModuleJson["MeshAssetPath"] = Mesh->MeshAssetPath;
        ModuleJson["OverrideMaterialPath"] = Mesh->OverrideMaterialPath;
        ModuleJson["bUseMeshMaterials"] = Mesh->bUseMeshMaterials;
    }
    else if (auto* SubUV = Cast<UParticleModuleSubUV>(Module))
    {
        ModuleJson["Type"] = "SubUV";
        ModuleJson["SubImageIndex_Min"] = SubUV->SubImageIndex.MinValue;
        ModuleJson["SubImageIndex_Max"] = SubUV->SubImageIndex.MaxValue;
        ModuleJson["SubImageIndex_bUseRange"] = SubUV->SubImageIndex.bUseRange;
        ModuleJson["InterpMethod"] = static_cast<int>(SubUV->InterpMethod);
        ModuleJson["bUseRealTime"] = SubUV->bUseRealTime;
    }else if (auto* Collision = Cast<UParticleModuleCollision>(Module))
    {
        ModuleJson["Type"] = "Collision";
        ModuleJson["CollisionResponse"] = static_cast<int>(Collision->CollisionResponse);
        ModuleJson["Restitution"] = Collision->Restitution;
        ModuleJson["Friction"] = Collision->Friction;
        ModuleJson["RadiusScale"] = Collision->RadiusScale;
        ModuleJson["bWriteEvent"] = Collision->bWriteEvent;
    }
    else if (auto* Cone = Cast<UParticleModuleVelocityCone>(Module))
    {
        ModuleJson["Type"] = "VelocityCone";

        // 1. Direction (Vector -> 3 Floats)
        ModuleJson["Direction_X"] = Cone->Direction.X;
        ModuleJson["Direction_Y"] = Cone->Direction.Y;
        ModuleJson["Direction_Z"] = Cone->Direction.Z;

        // 2. Angle (Distribution)
        ModuleJson["Angle_Min"] = Cone->Angle.MinValue;
        ModuleJson["Angle_Max"] = Cone->Angle.MaxValue;
        ModuleJson["Angle_bUseRange"] = Cone->Angle.bUseRange;

        // 3. Velocity (Distribution)
        ModuleJson["Velocity_Min"] = Cone->Velocity.MinValue;
        ModuleJson["Velocity_Max"] = Cone->Velocity.MaxValue;
        ModuleJson["Velocity_bUseRange"] = Cone->Velocity.bUseRange;
    }
    else
    {
        return {};
    }

    return ModuleJson;
}