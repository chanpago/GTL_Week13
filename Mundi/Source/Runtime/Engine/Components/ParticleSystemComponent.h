#pragma once
#include "PrimitiveComponent.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitter.h"
#include "Source/Runtime/Engine/Particle/ParticleSystem.h"
#include "Source/Runtime/Engine/Particle/ParticleHelper.h"
#include "UParticleSystemComponent.generated.h"

struct FDynamicEmitterDataBase;

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

	/** 활성화/비활성화 제어 */
	void ActivateSystem() { bAutoActivate = true; }
	void DeactivateSystem() { bAutoActivate = false; }

	// 렌더링을 위한 MeshBatch 수집 함수 
	void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

	// sprite, mesh 나눠 BuildBatch
	// 추후 FDynamicEmitterDataBase를 바꿀 것!
	void BuildParticleBatch(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View);
	void BuildEmitterRenderData();

	UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
	void SetMaterial(uint32 InSectionIndex, UMaterialInterface* InNewMaterial) override;	
	
	bool EnsureParticleBuffers(uint32 ParticleCapacity);

private:	
	/** 파티클 시스템 에셋 */
	UParticleSystem* Template = nullptr;

	/** 런타임 데이터 */
	TArray<FParticleEmitterInstance*> EmitterInstances;

	//  FOR TEST!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	/** 렌더 스레드로 보낼 데이터 패킷들 */
	TArray<FDynamicEmitterDataBase*> EmitterRenderData;	
	int MaxDebugParticles = 0;


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