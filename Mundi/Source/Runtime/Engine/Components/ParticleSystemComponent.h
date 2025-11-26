#pragma once
#include "PrimitiveComponent.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitter.h"
#include "Source/Runtime/Engine/Particle/ParticleSystem.h"
#include "Source/Runtime/Engine/Particle/ParticleHelper.h"
#include "Source/Runtime/Engine/Particle/DynamicEmitterDataBase.h"
#include "UParticleSystemComponent.generated.h"
#include "Source/Runtime/Engine/Particle/Async/ParticleAsyncUpdater.h"

UCLASS(DisplayName = "파티클 컴포넌트", Description = "파티클을 생성하는 컴포넌트")
class UParticleSystemComponent : public UPrimitiveComponent
{
public: 
	GENERATED_REFLECTION_BODY()
	DECLARE_DELEGATE(OnParticleSystemFinished, UParticleSystemComponent*);

public:
	UParticleSystemComponent();
	~UParticleSystemComponent() override;

	UPROPERTY(EditAnywhere, Category = "렌더링")
	bool bUseGpuInstancing = false;
public:
	// 컴포넌트 초기화 & LifeCycle
	virtual void InitParticles();
	virtual void DestroyParticles();

	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime) override;

	// ParticleSystem 활성화/비활성화 제어
	void ActivateSystem() { bTickEnabled = true; bSuppressSpawning = false; }
	void DeactivateSystem() { bTickEnabled = false; bSuppressSpawning = true; }
	void ResetAndActivate() { InitParticles(); ActivateSystem(); SetActive(true); }

	// Template accessor
	void SetTemplate(UParticleSystem* InTemplate) { Template = InTemplate; InitParticles(); }
	UParticleSystem* GetTemplate() const { return Template; }

	// 렌더링을 위한 MeshBatch 수집 함수
	void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

	void DuplicateSubObjects() override;

	// Location 모듈 범위 디버그 드로잉
	void RenderDebugVolume(class URenderer* Renderer) const override;

private:
	// sprite, mesh 나눠 BuildBatch
	void BuildSpriteParticleBatch(TArray<FDynamicEmitterDataBase*>& EmitterRenderData, TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View);
	void BuildMeshParticleBatch(TArray<FDynamicEmitterDataBase*>& EmitterRenderData, TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View);
	void BuildBeamParticleBatch(TArray<FDynamicEmitterDataBase*>& EmitterRenderData, TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View);
	void BuildRibbonParticleBatch(TArray<FDynamicEmitterDataBase*>& EmitterRenderData, TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View);
	
	UMaterialInterface* ResolveEmitterMaterial(const FDynamicEmitterDataBase& DynData) const;

	// Resource 관리
	bool EnsureParticleBuffers(uint32 ParticleCapacity);
	bool EnsureRibbonBuffers(uint32 MaxSpinePoints);
	bool EnsureMeshBuffer(uint32 InstanceCount);	

	void ReleaseParticleBuffers();
	void ReleaseInstanceBuffers();
	void ReleaseRibbonBuffers();
	void ReleaseBeamBuffers();
	
private:	
	UPROPERTY(EditAnywhere, Category = "Particle", DisplayName = "파티클 시스템")
	UParticleSystem* Template = nullptr;

	// Runtime Data
	TArray<FParticleEmitterInstance*> EmitterInstances;	
	
	// Render Resources
	// Sprite Resources
	ID3D11Buffer* ParticleVertexBuffer = nullptr;
	ID3D11Buffer* ParticleIndexBuffer = nullptr;
	uint32 ParticleVertexCapacity = 0;
	uint32 ParticleIndexCount = 0;
	UMaterialInterface* FallbackMaterial = nullptr;

	// Mesh Instancing
	ID3D11Buffer* MeshInstanceBuffer = nullptr;
	uint32 MeshInstanceCapacity = 0;

	// Ribbon Resources
	ID3D11Buffer* RibbonVertexBuffer = nullptr;
	ID3D11Buffer* RibbonIndexBuffer = nullptr;
	uint32 RibbonVertexCapacity = 0;
	uint32 RibbonIndexCapacity = 0;

	// Beam Resources
	TArray<ID3D11Buffer*> PerFrameBeamBuffers;
	uint32 BeamVertexBufferSize = 0;
	uint32 BeamIndexBufferSize = 0;

	//Async
	FParticleAsyncUpdater AsyncUpdater;
	float AccumulatedDeltaTime = 0.0f;
	
	// Settings
	UPROPERTY(EditAnywhere, Category = "Particle", Tooltip="시작 시 자동으로 활성화")
	bool bAutoActivate = true;
	UPROPERTY(EditAnywhere, Category = "Particle", Tooltip="끝날 시 소유 액터 파괴")
	bool bAutoDestroy = false;
	bool bSuppressSpawning = true; // True면 새 파티클 생성 멈추기
	UPROPERTY(EditAnywhere, Category = "Particle", Tooltip="비동기 활성화")
	bool bUseAsyncSimulation = true; // 비동기 시뮬레이션 활성화

	int MaxDebugParticles = 10000;
};
