#include "pch.h"
#include "ParticleModuleMesh.h"
#include "../ParticleEmitter.h"
#include "../ParticleLODLevel.h"
#include "ParticleModuleRequired.h"
#include "Source/Runtime/AssetManagement/ResourceManager.h"
#include "StaticMesh.h"
#include "Source/Runtime/Renderer/Material.h"

IMPLEMENT_CLASS(UParticleModuleMesh)

UParticleModuleMesh::UParticleModuleMesh()
{
    ModuleType = EParticleModuleType::TypeData;
    TypeDataType = EParticleType::Mesh;
}

void UParticleModuleMesh::SetMesh(UStaticMesh* InMesh, UParticleEmitter* OwnerEmitter)
{
    Mesh = InMesh;
    MeshAssetPath = Mesh ? Mesh->GetAssetPathFileName() : FString();

    if (!OwnerEmitter)
    {
        return;
    }

    OwnerEmitter->RenderType = EParticleType::Mesh;
    OwnerEmitter->Mesh = Mesh;
    OwnerEmitter->bUseMeshMaterials = bUseMeshMaterials;

    if (!Mesh)
    {
        return;
    }

    if (bUseMeshMaterials)
    {
        const TArray<FGroupInfo>& Groups = Mesh->GetMeshGroupInfo();
        if (!Groups.IsEmpty())
        {
            const FString& MatPath = Groups[0].InitialMaterialName;
            if (!MatPath.empty())
            {
                if (UMaterialInterface* Mat = UResourceManager::GetInstance().Load<UMaterial>(MatPath))
                {
                    SetOverrideMaterial(Mat, OwnerEmitter, false);
                }
            }
        }
    }
}

void UParticleModuleMesh::SetOverrideMaterial(UMaterialInterface* InMaterial, UParticleEmitter* OwnerEmitter, bool bUpdatePath)
{
    if (bUpdatePath)
    {
        OverrideMaterialPath = (InMaterial && !InMaterial->GetFilePath().empty()) ? InMaterial->GetFilePath() : FString();
    }

    if (!OwnerEmitter)
    {
        return;
    }

    //if (UParticleModuleRequired* Required = OwnerEmitter->GetModule<UParticleModuleRequired>())
    //{
    //    // Required->Material = InMaterial;
    //}
}

void UParticleModuleMesh::ApplyToEmitter(UParticleEmitter* OwnerEmitter)
{
    if (!OwnerEmitter)
    {
        return;
    }

    UStaticMesh* MeshAsset = Mesh;
    if (!MeshAsset && !MeshAssetPath.empty())
    {
        MeshAsset = UResourceManager::GetInstance().Load<UStaticMesh>(MeshAssetPath);
    }

    SetMesh(MeshAsset, OwnerEmitter);

    if (!bUseMeshMaterials)
    {
        UMaterialInterface* OverrideMat = nullptr;
        if (!OverrideMaterialPath.empty())
        {
            OverrideMat = UResourceManager::GetInstance().Load<UMaterial>(OverrideMaterialPath);
        }
        SetOverrideMaterial(OverrideMat, OwnerEmitter);
    }
}
