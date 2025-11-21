#include "pch.h"
#include "ParticleSystemComponent.h"

UParticleSystemComponent::UParticleSystemComponent()
{
    // 여기에 임시 Template 만들어서 테스트 가능
}

UParticleSystemComponent::~UParticleSystemComponent()
{
    DestroyParticles();
}

void UParticleSystemComponent::InitParticles()
{
    DestroyParticles();
    if (!Template) { return; }

    // 에셋에 정의된 이미터 개수만큼 인스턴스 생성
    for (int32 i = 0; i < Template->Emitters.Num(); i++)
    {
        UParticleEmitter* EmitterAsset = Template->Emitters[i];
        
        // 이미터가 유효하고 켜져있다면
        if (EmitterAsset)
        {
            FParticleEmitterInstance* NewInst = new FParticleEmitterInstance();
            NewInst->Init(EmitterAsset, this);
            EmitterInstances.Add(NewInst);
        }
    }

    if (bAutoActivate) { ActivateSystem(); }
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!bIsActive || !Template) return;

    bool bAllEmittersComplete = true;
    for (FParticleEmitterInstance* Inst : EmitterInstances)
    {
        Inst->Tick(DeltaTime);

        if (!Inst->IsComplete())
        {
            bAllEmittersComplete = false;
        }
    }

    if (bAllEmittersComplete)
    {
        OnParticleSystemFinished.Broadcast(this);
        DeactivateSystem();
        
        if (bAutoDestroy)
        {
            GetOwner()->Destroy();
        }
    }
}

void UParticleSystemComponent::DestroyParticles()
{
    for (FParticleEmitterInstance* Inst : EmitterInstances)
    {
        if (Inst)
        {
            Inst->FreeParticleMemory();
            delete Inst;
        }
    }
    EmitterInstances.Empty();
    bIsActive = false;
}

void UParticleSystemComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{

}
