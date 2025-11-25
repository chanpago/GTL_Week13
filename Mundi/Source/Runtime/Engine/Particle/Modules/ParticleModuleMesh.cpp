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

    // Mesh가 있을 때만 RenderType을 Mesh로 설정
    if (Mesh)
    {
        OwnerEmitter->RenderType = EParticleType::Mesh;
        OwnerEmitter->Mesh = Mesh;
        OwnerEmitter->bUseMeshMaterials = bUseMeshMaterials;
    }
    else
    {
        // Mesh가 없으면 Sprite로 복원
        OwnerEmitter->RenderType = EParticleType::Sprite;
        OwnerEmitter->Mesh = nullptr;
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
    OverrideMaterial = InMaterial;

    if (bUpdatePath)
    {
        OverrideMaterialPath = (InMaterial && !InMaterial->GetFilePath().empty()) ? InMaterial->GetFilePath() : FString();
    }
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
