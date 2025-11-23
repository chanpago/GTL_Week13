#pragma once
#include "PrimitiveComponent.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitter.h"
#include "Source/Runtime/Engine/Particle/ParticleSystem.h"
#include "Source/Runtime/Engine/Particle/ParticleHelper.h"
#include "Source/Runtime/Engine/Particle/DynamicEmitterDataBase.h"
#include "UParticleSystemComponent.generated.h"


UCLASS(DisplayName = "파티클 컴포넌트", Description = "파티클을 생성하는 컴포넌트")
class UParticleSystemComponent : public UPrimitiveComponent
{
public: 
	GENERATED_REFLECTION_BODY()
	DECLARE_DELEGATE(OnParticleSystemFinished, UParticleSystemComponent*);

public:
	UParticleSystemComponent();
	~UParticleSystemComponent() override;

public:
	/** 컴포넌트 초기화 */
	virtual void InitParticles();

	/** 컴포넌트 매 프레임 갱신 (Update) */
	void TickComponent(float DeltaTime) override;

	/** 컴포넌트 제거 시 정리 (OnDestroy) */
	virtual void DestroyParticles();
	void ReleaseParticleBuffers();
	void ClearEmitterRenderData();
	
	/** 활성화/비활성화 제어 */
	void ActivateSystem() { bAutoActivate = true; }
	void DeactivateSystem() { bAutoActivate = false; }

	// 렌더링을 위한 MeshBatch 수집 함수
	void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

	// sprite, mesh 나눠 BuildBatch
	void BuildSpriteParticleBatch(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View);
	void BuildMeshParticleBatch(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View);
	void BuildEmitterRenderData();
	void BuildDebugEmitterData();

	UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
	void SetMaterial(uint32 InSectionIndex, UMaterialInterface* InNewMaterial) override;

	bool EnsureParticleBuffers(uint32 ParticleCapacity);

	// Template accessor
	void SetTemplate(UParticleSystem* InTemplate) { Template = InTemplate; }
	UParticleSystem* GetTemplate() const { return Template; }

private:	
	/** 파티클 시스템 에셋 */
	UPROPERTY(EditAnywhere, Category = "Particle", DisplayName = "파티클 시스템")
	UParticleSystem* Template = nullptr;

private:

	/** 런타임 데이터 */
	TArray<FParticleEmitterInstance*> EmitterInstances;

	/** 렌더 스레드로 보낼 데이터 패킷들 */
	TArray<FDynamicEmitterDataBase*> EmitterRenderData;	
	int MaxDebugParticles = 1000;
	bool bEnableDebugEmitter = true;


	ID3D11Buffer* ParticleVertexBuffer = nullptr;
	ID3D11Buffer* ParticleIndexBuffer = nullptr;
	uint32 ParticleVertexCapacity = 0;
	uint32 ParticleIndexCount = 0;
	UMaterialInterface* ParticleMaterial = nullptr;

	/** 자동 시작 여부 */
	bool bAutoActivate = true;
	
	/** 자동 삭제 여부 */
	bool bAutoDestroy = false;
};
