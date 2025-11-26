#pragma once
#include "ParticleModuleTypeDataBase.h"

class UStaticMesh;
class UParticleEmitter;
class UMaterialInterface;

// Type data module for mesh-rendered particles
class UParticleModuleMesh : public UParticleModuleTypeDataBase
{
    DECLARE_CLASS(UParticleModuleMesh, UParticleModuleTypeDataBase)
public:
    UParticleModuleMesh();

    void SetMesh(UStaticMesh* InMesh, UParticleEmitter* OwnerEmitter);
    void SetOverrideMaterial(UMaterialInterface* InMaterial, UParticleEmitter* OwnerEmitter, bool bUpdatePath = true);
    void ApplyToEmitter(UParticleEmitter* OwnerEmitter);

    void SetMeshAssetPath(const FString& InPath) { MeshAssetPath = InPath; }
    void SetOverrideMaterialPath(const FString& InPath) { OverrideMaterialPath = InPath; }

    UStaticMesh* Mesh = nullptr;
    UMaterialInterface* OverrideMaterial = nullptr;
    bool bUseMeshMaterials = true;
    bool bLighting = true;
    // 이 타입에 필요한 파티클 데이터 크기 반환
    virtual int32 GetRequiredParticleBytes() const override
    {
        return sizeof(FVector)      // 위치
            + sizeof(FQuat)        // 회전
            + sizeof(FVector);     // 스케일
    }

    // 렌더링을 위한 정점 데이터 크기
    virtual int32 GetDynamicVertexStride() const override
    {
        // 기본 Mesh의 정점 크기
        return sizeof(FNormalVertex);
    }

    FString MeshAssetPath;
    FString OverrideMaterialPath;

};
