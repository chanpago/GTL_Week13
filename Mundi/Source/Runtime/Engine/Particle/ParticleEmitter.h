#pragma once

class UParticleLODLevel;
class UParticleSystemComponent;

class UParticleEmitter : public UObject
{
    DECLARE_CLASS(UParticleEmitter, UObject)

public:
    UParticleEmitter();
    ~UParticleEmitter() override;

    UParticleLODLevel* AddLODLevel(int32 LODIndex = 0);
    void CacheEmitterModuleInfo();
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

    template<typename T>
    T* GetModule(int32 LODIndex = 0) const;

public:
    TArray<UParticleLODLevel*> LODLevels;
    EParticleType RenderType = EParticleType::Sprite;

    // Mesh 타입인 경우에만 사용
    UStaticMesh* Mesh = nullptr;
    bool bUseMeshMaterials = true; // Mesh의 머티리얼 그대로 쓸지 여부

    int32 ParticleSizeBytes = 0;
    int32 MaxParticles = 0;
    float MaxLifetime = 0.f;
};
