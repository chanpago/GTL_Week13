#include "pch.h"
#include "ParticleLODLevel.h"
#include "Modules/ParticleModuleRequired.h"
#include "Modules/ParticleModuleTypeDataBase.h"

void UParticleLODLevel::RebuildModuleCaches()
{
    AllModulesCache.Empty();
    if (RequiredModule) AllModulesCache.Add(RequiredModule);
    if (TypeDataModule) AllModulesCache.Add(TypeDataModule);
    AllModulesCache.Append(SpawnModules);
    AllModulesCache.Append(UpdateModules);
    
}