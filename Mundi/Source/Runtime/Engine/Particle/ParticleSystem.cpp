#include "pch.h"
#include "ParticleSystem.h"
#include "ParticleEmitter.h"
#include "ParticleLODLevel.h"
#include "Modules/ParticleModuleRequired.h"
#include "Modules/ParticleModuleSpawn.h"
#include "Modules/ParticleModuleLifetime.h"
#include "Modules/ParticleModuleVelocity.h"
#include "Modules/ParticleModuleSize.h"
#include "Modules/ParticleModuleColor.h"
#include "Modules/ParticleModuleLocation.h"
#include "JsonSerializer.h"
#include <fstream>

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

// 모듈 직렬화 헬퍼 함수
JSON UParticleSystem::SerializeModule(UParticleModule* Module) const
{
    if (!Module) return JSON();

    JSON ModuleJson = JSON::Make(JSON::Class::Object);

    // 모듈 타입 판별
    if (auto* Lifetime = dynamic_cast<UParticleModuleLifetime*>(Module))
    {
        ModuleJson["Type"] = "Lifetime";
        ModuleJson["Lifetime_Min"] = Lifetime->Lifetime.MinValue;
        ModuleJson["Lifetime_Max"] = Lifetime->Lifetime.MaxValue;
        ModuleJson["Lifetime_bUseRange"] = Lifetime->Lifetime.bUseRange;
    }
    else if (auto* Velocity = dynamic_cast<UParticleModuleVelocity*>(Module))
    {
        ModuleJson["Type"] = "Velocity";
        JSON VelMinArr = JSON::Make(JSON::Class::Array);
        VelMinArr.append(Velocity->StartVelocity.MinValue.X);
        VelMinArr.append(Velocity->StartVelocity.MinValue.Y);
        VelMinArr.append(Velocity->StartVelocity.MinValue.Z);
        ModuleJson["StartVelocity_Min"] = VelMinArr;

        JSON VelMaxArr = JSON::Make(JSON::Class::Array);
        VelMaxArr.append(Velocity->StartVelocity.MaxValue.X);
        VelMaxArr.append(Velocity->StartVelocity.MaxValue.Y);
        VelMaxArr.append(Velocity->StartVelocity.MaxValue.Z);
        ModuleJson["StartVelocity_Max"] = VelMaxArr;
        ModuleJson["StartVelocity_bUseRange"] = Velocity->StartVelocity.bUseRange;
    }
    else if (auto* Size = dynamic_cast<UParticleModuleSize*>(Module))
    {
        ModuleJson["Type"] = "Size";
        JSON SizeMinArr = JSON::Make(JSON::Class::Array);
        SizeMinArr.append(Size->StartSize.MinValue.X);
        SizeMinArr.append(Size->StartSize.MinValue.Y);
        SizeMinArr.append(Size->StartSize.MinValue.Z);
        ModuleJson["StartSize_Min"] = SizeMinArr;

        JSON SizeMaxArr = JSON::Make(JSON::Class::Array);
        SizeMaxArr.append(Size->StartSize.MaxValue.X);
        SizeMaxArr.append(Size->StartSize.MaxValue.Y);
        SizeMaxArr.append(Size->StartSize.MaxValue.Z);
        ModuleJson["StartSize_Max"] = SizeMaxArr;
        ModuleJson["StartSize_bUseRange"] = Size->StartSize.bUseRange;
    }
    else if (auto* Color = dynamic_cast<UParticleModuleColor*>(Module))
    {
        ModuleJson["Type"] = "Color";
        JSON ColorMinArr = JSON::Make(JSON::Class::Array);
        ColorMinArr.append(Color->StartColor.MinValue.R);
        ColorMinArr.append(Color->StartColor.MinValue.G);
        ColorMinArr.append(Color->StartColor.MinValue.B);
        ColorMinArr.append(Color->StartColor.MinValue.A);
        ModuleJson["StartColor_Min"] = ColorMinArr;

        JSON ColorMaxArr = JSON::Make(JSON::Class::Array);
        ColorMaxArr.append(Color->StartColor.MaxValue.R);
        ColorMaxArr.append(Color->StartColor.MaxValue.G);
        ColorMaxArr.append(Color->StartColor.MaxValue.B);
        ColorMaxArr.append(Color->StartColor.MaxValue.A);
        ModuleJson["StartColor_Max"] = ColorMaxArr;
        ModuleJson["StartColor_bUseRange"] = Color->StartColor.bUseRange;
    }
    else if (auto* Location = dynamic_cast<UParticleModuleLocation*>(Module))
    {
        ModuleJson["Type"] = "Location";
        JSON LocMinArr = JSON::Make(JSON::Class::Array);
        LocMinArr.append(Location->StartLocation.MinValue.X);
        LocMinArr.append(Location->StartLocation.MinValue.Y);
        LocMinArr.append(Location->StartLocation.MinValue.Z);
        ModuleJson["StartLocation_Min"] = LocMinArr;

        JSON LocMaxArr = JSON::Make(JSON::Class::Array);
        LocMaxArr.append(Location->StartLocation.MaxValue.X);
        LocMaxArr.append(Location->StartLocation.MaxValue.Y);
        LocMaxArr.append(Location->StartLocation.MaxValue.Z);
        ModuleJson["StartLocation_Max"] = LocMaxArr;
        ModuleJson["StartLocation_bUseRange"] = Location->StartLocation.bUseRange;
    }
    else
    {
        return JSON(); // 알 수 없는 모듈 타입
    }

    return ModuleJson;
}

bool UParticleSystem::SaveToFile(const FString& FilePath)
{
    JSON JsonData = SerializeToJson();

    std::ofstream OutFile(FilePath, std::ios::out);
    if (!OutFile.is_open())
    {
        UE_LOG("Failed to open file for writing: %s", FilePath.c_str());
        return false;
    }

    OutFile << JsonData.dump(4); // Pretty print with 4 spaces
    OutFile.close();

    UE_LOG("ParticleSystem saved to: %s", FilePath.c_str());
    return true;
}

UParticleSystem* UParticleSystem::LoadFromFile(const FString& FilePath)
{
    std::ifstream InFile(FilePath, std::ios::in);
    if (!InFile.is_open())
    {
        UE_LOG("Failed to open file for reading: %s", FilePath.c_str());
        return nullptr;
    }

    std::string JsonString((std::istreambuf_iterator<char>(InFile)), std::istreambuf_iterator<char>());
    InFile.close();

    JSON JsonData = JSON::Load(JsonString);

    UParticleSystem* NewSystem = NewObject<UParticleSystem>();
    if (NewSystem->DeserializeFromJson(JsonData))
    {
        UE_LOG("ParticleSystem loaded from: %s", FilePath.c_str());
        return NewSystem;
    }

    delete NewSystem;
    return nullptr;
}

JSON UParticleSystem::SerializeToJson() const
{
    JSON JsonData = JSON::Make(JSON::Class::Object);

    // 기본 정보
    JsonData["Type"] = "ParticleSystem";
    JsonData["Version"] = 1;
    JsonData["Name"] = ObjectName.ToString();

    // Emitters 배열
    JSON EmittersArray = JSON::Make(JSON::Class::Array);
    for (int32 i = 0; i < Emitters.Num(); i++)
    {
        UParticleEmitter* Emitter = Emitters[i];
        if (!Emitter) continue;

        JSON EmitterJson = JSON::Make(JSON::Class::Object);
        EmitterJson["Name"] = Emitter->ObjectName.ToString();
        EmitterJson["RenderType"] = static_cast<int>(Emitter->RenderType);

        // LODLevels 직렬화
        JSON LODArray = JSON::Make(JSON::Class::Array);
        for (int32 lodIdx = 0; lodIdx < Emitter->LODLevels.Num(); lodIdx++)
        {
            UParticleLODLevel* LOD = Emitter->LODLevels[lodIdx];
            if (!LOD) continue;

            JSON LODJson = JSON::Make(JSON::Class::Object);
            LODJson["LODIndex"] = LOD->LODIndex;
            LODJson["bEnabled"] = LOD->bEnabled;

            // RequiredModule 직렬화
            if (LOD->RequiredModule)
            {
                JSON RequiredJson = JSON::Make(JSON::Class::Object);
                auto* Req = LOD->RequiredModule;
                RequiredJson["MaxParticles"] = Req->MaxParticles;
                RequiredJson["EmitterDuration"] = Req->EmitterDuration;
                RequiredJson["EmitterDelay"] = Req->EmitterDelay;
                RequiredJson["EmitterLoops"] = Req->EmitterLoops;
                RequiredJson["SpawnRateBase"] = Req->SpawnRateBase;
                RequiredJson["bUseLocalSpace"] = Req->bUseLocalSpace;
                RequiredJson["bKillOnDeactivate"] = Req->bKillOnDeactivate;
                RequiredJson["bKillOnCompleted"] = Req->bKillOnCompleted;
                RequiredJson["BlendMode"] = static_cast<int>(Req->BlendMode);
                RequiredJson["ScreenAlignment"] = static_cast<int>(Req->ScreenAlignment);
                RequiredJson["SortMode"] = static_cast<int>(Req->SortMode);

                // InitialSize
                JSON SizeMinArr = JSON::Make(JSON::Class::Array);
                SizeMinArr.append(Req->InitialSize.MinValue.X);
                SizeMinArr.append(Req->InitialSize.MinValue.Y);
                SizeMinArr.append(Req->InitialSize.MinValue.Z);
                RequiredJson["InitialSize_Min"] = SizeMinArr;

                JSON SizeMaxArr = JSON::Make(JSON::Class::Array);
                SizeMaxArr.append(Req->InitialSize.MaxValue.X);
                SizeMaxArr.append(Req->InitialSize.MaxValue.Y);
                SizeMaxArr.append(Req->InitialSize.MaxValue.Z);
                RequiredJson["InitialSize_Max"] = SizeMaxArr;
                RequiredJson["InitialSize_bUseRange"] = Req->InitialSize.bUseRange;

                // InitialColor
                JSON ColorMinArr = JSON::Make(JSON::Class::Array);
                ColorMinArr.append(Req->InitialColor.MinValue.R);
                ColorMinArr.append(Req->InitialColor.MinValue.G);
                ColorMinArr.append(Req->InitialColor.MinValue.B);
                ColorMinArr.append(Req->InitialColor.MinValue.A);
                RequiredJson["InitialColor_Min"] = ColorMinArr;

                JSON ColorMaxArr = JSON::Make(JSON::Class::Array);
                ColorMaxArr.append(Req->InitialColor.MaxValue.R);
                ColorMaxArr.append(Req->InitialColor.MaxValue.G);
                ColorMaxArr.append(Req->InitialColor.MaxValue.B);
                ColorMaxArr.append(Req->InitialColor.MaxValue.A);
                RequiredJson["InitialColor_Max"] = ColorMaxArr;
                RequiredJson["InitialColor_bUseRange"] = Req->InitialColor.bUseRange;

                // InitialRotation
                RequiredJson["InitialRotation_Min"] = Req->InitialRotation.MinValue;
                RequiredJson["InitialRotation_Max"] = Req->InitialRotation.MaxValue;
                RequiredJson["InitialRotation_bUseRange"] = Req->InitialRotation.bUseRange;

                LODJson["RequiredModule"] = RequiredJson;
            }

            // SpawnModule 직렬화
            if (LOD->SpawnModule)
            {
                JSON SpawnJson = JSON::Make(JSON::Class::Object);
                auto* Spawn = LOD->SpawnModule;
                SpawnJson["SpawnRateType"] = static_cast<int>(Spawn->SpawnRateType);
                SpawnJson["SpawnRate_Min"] = Spawn->SpawnRate.MinValue;
                SpawnJson["SpawnRate_Max"] = Spawn->SpawnRate.MaxValue;
                SpawnJson["SpawnRate_bUseRange"] = Spawn->SpawnRate.bUseRange;

                LODJson["SpawnModule"] = SpawnJson;
            }

            // SpawnModules 직렬화
            JSON SpawnModulesArray = JSON::Make(JSON::Class::Array);
            for (int32 modIdx = 0; modIdx < LOD->SpawnModules.Num(); modIdx++)
            {
                UParticleModule* Module = LOD->SpawnModules[modIdx];
                if (!Module) continue;

                JSON ModuleJson = SerializeModule(Module);
                if (!ModuleJson.IsNull())
                {
                    SpawnModulesArray.append(ModuleJson);
                }
            }
            LODJson["SpawnModules"] = SpawnModulesArray;

            // UpdateModules 직렬화
            JSON UpdateModulesArray = JSON::Make(JSON::Class::Array);
            for (int32 modIdx = 0; modIdx < LOD->UpdateModules.Num(); modIdx++)
            {
                UParticleModule* Module = LOD->UpdateModules[modIdx];
                if (!Module) continue;

                JSON ModuleJson = SerializeModule(Module);
                if (!ModuleJson.IsNull())
                {
                    UpdateModulesArray.append(ModuleJson);
                }
            }
            LODJson["UpdateModules"] = UpdateModulesArray;

            LODArray.append(LODJson);
        }
        EmitterJson["LODLevels"] = LODArray;

        EmittersArray.append(EmitterJson);
    }
    JsonData["Emitters"] = EmittersArray;

    return JsonData;
}

bool UParticleSystem::DeserializeFromJson(JSON& InJson)
{
    if (!InJson.hasKey("Type") || InJson["Type"].ToString() != "ParticleSystem")
    {
        UE_LOG("Invalid ParticleSystem JSON format");
        return false;
    }

    // 기본 정보
    if (InJson.hasKey("Name"))
    {
        ObjectName = FName(InJson["Name"].ToString());
    }

    // Emitters 배열
    if (InJson.hasKey("Emitters"))
    {
        JSON& EmittersArray = InJson["Emitters"];
        for (int i = 0; i < EmittersArray.length(); i++)
        {
            JSON& EmitterJson = EmittersArray[i];

            UParticleEmitter* NewEmitter = new UParticleEmitter();
            if (EmitterJson.hasKey("Name"))
            {
                NewEmitter->ObjectName = FName(EmitterJson["Name"].ToString());
            }
            if (EmitterJson.hasKey("RenderType"))
            {
                NewEmitter->RenderType = static_cast<EEmitterRenderType>(EmitterJson["RenderType"].ToInt());
            }

            // LODLevels 역직렬화
            if (EmitterJson.hasKey("LODLevels"))
            {
                JSON& LODArray = EmitterJson["LODLevels"];
                for (int lodIdx = 0; lodIdx < LODArray.length(); lodIdx++)
                {
                    JSON& LODJson = LODArray[lodIdx];

                    UParticleLODLevel* NewLOD = new UParticleLODLevel();
                    if (LODJson.hasKey("LODIndex")) NewLOD->LODIndex = LODJson["LODIndex"].ToInt();
                    if (LODJson.hasKey("bEnabled")) NewLOD->bEnabled = LODJson["bEnabled"].ToBool();

                    // RequiredModule 역직렬화
                    if (LODJson.hasKey("RequiredModule"))
                    {
                        JSON& ReqJson = LODJson["RequiredModule"];
                        UParticleModuleRequired* Req = new UParticleModuleRequired();

                        if (ReqJson.hasKey("MaxParticles")) Req->MaxParticles = ReqJson["MaxParticles"].ToInt();
                        if (ReqJson.hasKey("EmitterDuration")) Req->EmitterDuration = (float)ReqJson["EmitterDuration"].ToFloat();
                        if (ReqJson.hasKey("EmitterDelay")) Req->EmitterDelay = (float)ReqJson["EmitterDelay"].ToFloat();
                        if (ReqJson.hasKey("EmitterLoops")) Req->EmitterLoops = ReqJson["EmitterLoops"].ToInt();
                        if (ReqJson.hasKey("SpawnRateBase")) Req->SpawnRateBase = (float)ReqJson["SpawnRateBase"].ToFloat();
                        if (ReqJson.hasKey("bUseLocalSpace")) Req->bUseLocalSpace = ReqJson["bUseLocalSpace"].ToBool();
                        if (ReqJson.hasKey("bKillOnDeactivate")) Req->bKillOnDeactivate = ReqJson["bKillOnDeactivate"].ToBool();
                        if (ReqJson.hasKey("bKillOnCompleted")) Req->bKillOnCompleted = ReqJson["bKillOnCompleted"].ToBool();
                        if (ReqJson.hasKey("BlendMode")) Req->BlendMode = static_cast<EBlendMode>(ReqJson["BlendMode"].ToInt());
                        if (ReqJson.hasKey("ScreenAlignment")) Req->ScreenAlignment = static_cast<EScreenAlignment>(ReqJson["ScreenAlignment"].ToInt());
                        if (ReqJson.hasKey("SortMode")) Req->SortMode = static_cast<ESortMode>(ReqJson["SortMode"].ToInt());

                        // InitialSize
                        if (ReqJson.hasKey("InitialSize_Min"))
                        {
                            JSON& MinArr = ReqJson["InitialSize_Min"];
                            Req->InitialSize.MinValue = FVector((float)MinArr[0].ToFloat(), (float)MinArr[1].ToFloat(), (float)MinArr[2].ToFloat());
                        }
                        if (ReqJson.hasKey("InitialSize_Max"))
                        {
                            JSON& MaxArr = ReqJson["InitialSize_Max"];
                            Req->InitialSize.MaxValue = FVector((float)MaxArr[0].ToFloat(), (float)MaxArr[1].ToFloat(), (float)MaxArr[2].ToFloat());
                        }
                        if (ReqJson.hasKey("InitialSize_bUseRange")) Req->InitialSize.bUseRange = ReqJson["InitialSize_bUseRange"].ToBool();

                        // InitialColor
                        if (ReqJson.hasKey("InitialColor_Min"))
                        {
                            JSON& MinArr = ReqJson["InitialColor_Min"];
                            Req->InitialColor.MinValue = FLinearColor((float)MinArr[0].ToFloat(), (float)MinArr[1].ToFloat(), (float)MinArr[2].ToFloat(), (float)MinArr[3].ToFloat());
                        }
                        if (ReqJson.hasKey("InitialColor_Max"))
                        {
                            JSON& MaxArr = ReqJson["InitialColor_Max"];
                            Req->InitialColor.MaxValue = FLinearColor((float)MaxArr[0].ToFloat(), (float)MaxArr[1].ToFloat(), (float)MaxArr[2].ToFloat(), (float)MaxArr[3].ToFloat());
                        }
                        if (ReqJson.hasKey("InitialColor_bUseRange")) Req->InitialColor.bUseRange = ReqJson["InitialColor_bUseRange"].ToBool();

                        // InitialRotation
                        if (ReqJson.hasKey("InitialRotation_Min")) Req->InitialRotation.MinValue = (float)ReqJson["InitialRotation_Min"].ToFloat();
                        if (ReqJson.hasKey("InitialRotation_Max")) Req->InitialRotation.MaxValue = (float)ReqJson["InitialRotation_Max"].ToFloat();
                        if (ReqJson.hasKey("InitialRotation_bUseRange")) Req->InitialRotation.bUseRange = ReqJson["InitialRotation_bUseRange"].ToBool();

                        NewLOD->RequiredModule = Req;
                    }

                    // SpawnModule 역직렬화
                    if (LODJson.hasKey("SpawnModule"))
                    {
                        JSON& SpawnJson = LODJson["SpawnModule"];
                        UParticleModuleSpawn* Spawn = new UParticleModuleSpawn();

                        if (SpawnJson.hasKey("SpawnRateType")) Spawn->SpawnRateType = static_cast<ESpawnRateType>(SpawnJson["SpawnRateType"].ToInt());
                        if (SpawnJson.hasKey("SpawnRate_Min")) Spawn->SpawnRate.MinValue = (float)SpawnJson["SpawnRate_Min"].ToFloat();
                        if (SpawnJson.hasKey("SpawnRate_Max")) Spawn->SpawnRate.MaxValue = (float)SpawnJson["SpawnRate_Max"].ToFloat();
                        if (SpawnJson.hasKey("SpawnRate_bUseRange")) Spawn->SpawnRate.bUseRange = SpawnJson["SpawnRate_bUseRange"].ToBool();

                        NewLOD->SpawnModule = Spawn;
                    }

                    // SpawnModules 역직렬화
                    if (LODJson.hasKey("SpawnModules"))
                    {
                        JSON& ModulesArray = LODJson["SpawnModules"];
                        for (int modIdx = 0; modIdx < ModulesArray.length(); modIdx++)
                        {
                            JSON& ModJson = ModulesArray[modIdx];
                            UParticleModule* Mod = DeserializeModule(ModJson);
                            if (Mod) NewLOD->SpawnModules.Add(Mod);
                        }
                    }

                    // UpdateModules 역직렬화
                    if (LODJson.hasKey("UpdateModules"))
                    {
                        JSON& ModulesArray = LODJson["UpdateModules"];
                        for (int modIdx = 0; modIdx < ModulesArray.length(); modIdx++)
                        {
                            JSON& ModJson = ModulesArray[modIdx];
                            UParticleModule* Mod = DeserializeModule(ModJson);
                            if (Mod) NewLOD->UpdateModules.Add(Mod);
                        }
                    }

                    NewLOD->RebuildModuleCaches();
                    NewEmitter->LODLevels.Add(NewLOD);
                }
            }

            Emitters.Add(NewEmitter);
        }
    }

    BuildRuntimeCache();
    return true;
}

// 모듈 역직렬화 헬퍼 함수
UParticleModule* UParticleSystem::DeserializeModule(JSON& ModuleJson)
{
    if (!ModuleJson.hasKey("Type")) return nullptr;

    FString ModuleType = ModuleJson["Type"].ToString();

    if (ModuleType == "Lifetime")
    {
        UParticleModuleLifetime* Lifetime = new UParticleModuleLifetime();
        if (ModuleJson.hasKey("Lifetime_Min")) Lifetime->Lifetime.MinValue = (float)ModuleJson["Lifetime_Min"].ToFloat();
        if (ModuleJson.hasKey("Lifetime_Max")) Lifetime->Lifetime.MaxValue = (float)ModuleJson["Lifetime_Max"].ToFloat();
        if (ModuleJson.hasKey("Lifetime_bUseRange")) Lifetime->Lifetime.bUseRange = ModuleJson["Lifetime_bUseRange"].ToBool();
        return Lifetime;
    }
    else if (ModuleType == "Velocity")
    {
        UParticleModuleVelocity* Velocity = new UParticleModuleVelocity();
        if (ModuleJson.hasKey("StartVelocity_Min"))
        {
            JSON& MinArr = ModuleJson["StartVelocity_Min"];
            Velocity->StartVelocity.MinValue = FVector((float)MinArr[0].ToFloat(), (float)MinArr[1].ToFloat(), (float)MinArr[2].ToFloat());
        }
        if (ModuleJson.hasKey("StartVelocity_Max"))
        {
            JSON& MaxArr = ModuleJson["StartVelocity_Max"];
            Velocity->StartVelocity.MaxValue = FVector((float)MaxArr[0].ToFloat(), (float)MaxArr[1].ToFloat(), (float)MaxArr[2].ToFloat());
        }
        if (ModuleJson.hasKey("StartVelocity_bUseRange")) Velocity->StartVelocity.bUseRange = ModuleJson["StartVelocity_bUseRange"].ToBool();
        return Velocity;
    }
    else if (ModuleType == "Size")
    {
        UParticleModuleSize* Size = new UParticleModuleSize();
        if (ModuleJson.hasKey("StartSize_Min"))
        {
            JSON& MinArr = ModuleJson["StartSize_Min"];
            Size->StartSize.MinValue = FVector((float)MinArr[0].ToFloat(), (float)MinArr[1].ToFloat(), (float)MinArr[2].ToFloat());
        }
        if (ModuleJson.hasKey("StartSize_Max"))
        {
            JSON& MaxArr = ModuleJson["StartSize_Max"];
            Size->StartSize.MaxValue = FVector((float)MaxArr[0].ToFloat(), (float)MaxArr[1].ToFloat(), (float)MaxArr[2].ToFloat());
        }
        if (ModuleJson.hasKey("StartSize_bUseRange")) Size->StartSize.bUseRange = ModuleJson["StartSize_bUseRange"].ToBool();
        return Size;
    }
    else if (ModuleType == "Color")
    {
        UParticleModuleColor* Color = new UParticleModuleColor();
        if (ModuleJson.hasKey("StartColor_Min"))
        {
            JSON& MinArr = ModuleJson["StartColor_Min"];
            Color->StartColor.MinValue = FLinearColor((float)MinArr[0].ToFloat(), (float)MinArr[1].ToFloat(), (float)MinArr[2].ToFloat(), (float)MinArr[3].ToFloat());
        }
        if (ModuleJson.hasKey("StartColor_Max"))
        {
            JSON& MaxArr = ModuleJson["StartColor_Max"];
            Color->StartColor.MaxValue = FLinearColor((float)MaxArr[0].ToFloat(), (float)MaxArr[1].ToFloat(), (float)MaxArr[2].ToFloat(), (float)MaxArr[3].ToFloat());
        }
        if (ModuleJson.hasKey("StartColor_bUseRange")) Color->StartColor.bUseRange = ModuleJson["StartColor_bUseRange"].ToBool();
        return Color;
    }
    else if (ModuleType == "Location")
    {
        UParticleModuleLocation* Location = new UParticleModuleLocation();
        if (ModuleJson.hasKey("StartLocation_Min"))
        {
            JSON& MinArr = ModuleJson["StartLocation_Min"];
            Location->StartLocation.MinValue = FVector((float)MinArr[0].ToFloat(), (float)MinArr[1].ToFloat(), (float)MinArr[2].ToFloat());
        }
        if (ModuleJson.hasKey("StartLocation_Max"))
        {
            JSON& MaxArr = ModuleJson["StartLocation_Max"];
            Location->StartLocation.MaxValue = FVector((float)MaxArr[0].ToFloat(), (float)MaxArr[1].ToFloat(), (float)MaxArr[2].ToFloat());
        }
        if (ModuleJson.hasKey("StartLocation_bUseRange")) Location->StartLocation.bUseRange = ModuleJson["StartLocation_bUseRange"].ToBool();
        return Location;
    }

    return nullptr; // 알 수 없는 모듈 타입
}