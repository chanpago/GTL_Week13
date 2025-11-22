#include "pch.h"
#include "ParticleSystemComponent.h"
#include "MeshBatchElement.h"
#include "SceneView.h"
#include "Source/Runtime/Engine/Particle/DynamicEmitterDataBase.h"

UParticleSystemComponent::UParticleSystemComponent()
{
    // FOR TEST !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    bAutoActivate = true;

    // 디버그용 파티클 3개
    //FDynamicEmitterDataBase* P0 = new FDynamicEmitterDataBase();
    //P0->Position = FVector(0, 0, 1);
    //P0->Size = 3.0f;
    //P0->Color = FLinearColor(1.0f, 0.0f, 0.0f, 0.5f);
    //EmitterRenderData.Add(std::move(P0));

    //FDynamicEmitterDataBase* P1 = new FDynamicEmitterDataBase();
    //P1->Position = FVector(1, 0, 1);
    //P1->Size = 4.0f;
    //P1->Color = FLinearColor(0.0f, 1.0f, 0.0f, 0.5f);
    //EmitterRenderData.Add(std::move(P1));

    //FDynamicEmitterDataBase* P2 = new FDynamicEmitterDataBase();
    //P2->Position = FVector(0, 1, 1.5);
    //P2->Size = 5.0f;
    //P2->Color = FLinearColor(0.0f, 0.0f, 1.0f, 0.5f);
    //EmitterRenderData.Add(std::move(P2));

    MaxDebugParticles = 128; 
}

UParticleSystemComponent::~UParticleSystemComponent()
{
    DestroyParticles();
    ReleaseParticleBuffers();
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

void UParticleSystemComponent::ReleaseParticleBuffers()
{
    if (ParticleVertexBuffer)
    {
        ParticleVertexBuffer->Release();
        ParticleVertexBuffer = nullptr;
    }

    if (ParticleIndexBuffer)
    {
        ParticleIndexBuffer->Release();
        ParticleIndexBuffer = nullptr;
    }

    ParticleVertexCapacity = 0;
    ParticleIndexCount = 0;
}

void UParticleSystemComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (!IsVisible() || EmitterRenderData.IsEmpty())
    {
        return;
    }

    // 시뮬레이션 데이터 → Replay → DynamicData
    BuildEmitterRenderData();

    if (EmitterRenderData.IsEmpty())
        return;

    BuildParticleBatch(OutMeshBatchElements, View);

    // 이번 프레임 끝에 DynamicData 파괴
    for (FDynamicEmitterDataBase* Data : EmitterRenderData)
    {
        delete Data;
    }
    EmitterRenderData.Empty();
}

void UParticleSystemComponent::BuildParticleBatch(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (EmitterRenderData.IsEmpty())
        return;

    // 1) Sprite 파티클 총 개수
    uint32 TotalParticles = 0;
    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EEmitterRenderType::Sprite)
            continue;

        auto* SpriteData = static_cast<FDynamicSpriteEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicSpriteEmitterReplayData*>(SpriteData->GetSource());
        if (!Src)
            continue;

        TotalParticles += Src->ActiveParticleCount;
    }

    if (TotalParticles == 0)
        return;

    const uint32 ParticleCount = static_cast<uint32>(EmitterRenderData.Num());
    if (ParticleCount == 0)
    {
        return;
    }

    const uint32 ClampedCount = MaxDebugParticles > 0
        ? std::min<uint32>(ParticleCount, static_cast<uint32>(MaxDebugParticles))
        : ParticleCount;

    if (!EnsureParticleBuffers(ClampedCount))
    {
        return;
    }

    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11DeviceContext* Context = RHIDevice ? RHIDevice->GetDeviceContext() : nullptr;
    if (!Context)
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(Context->Map(ParticleVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        return;
    }

    FParticleSpriteVertex* Vertices = reinterpret_cast<FParticleSpriteVertex*>(Mapped.pData);
    static const FVector2D CornerOffsets[4] = {
        FVector2D(-1.0f, -1.0f),
        FVector2D(1.0f, -1.0f),
        FVector2D(1.0f,  1.0f),
        FVector2D(-1.0f,  1.0f)
    };

    uint32 VertexCursor = 0;
    uint32 WrittenParticles = 0;

    const FVector ViewOrigin = View ? View->ViewLocation : FVector::Zero();
    const FVector ViewDir = View ? View->ViewRotation.RotateVector(FVector(1, 0, 0)) : FVector(1, 0, 0);

    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EEmitterRenderType::Sprite)
            continue;

        auto* SpriteData = static_cast<FDynamicSpriteEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicSpriteEmitterReplayData*>(SpriteData->GetSource());
        if (!Src || Src->ActiveParticleCount <= 0)
            continue;

        const int32 Num = Src->ActiveParticleCount;

        // 4-2-1. 정렬 인덱스
        TArray<int32> SortIndices;
        SpriteData->SortParticles(ViewOrigin, ViewDir, SortIndices);

        const bool bUseSortIndices = (SortIndices.Num() == Num);

        for (int32 LocalIdx = 0; LocalIdx < Num; ++LocalIdx)
        {
            if (WrittenParticles >= ClampedCount)
                break;

            const int32 ParticleIdx =
                bUseSortIndices ? SortIndices[LocalIdx] : LocalIdx;

            const FBaseParticle* Particle = SpriteData->GetParticle(ParticleIdx); // 위에서 만든 private 헬퍼를 public으로 바꾸거나, 유틸 함수로 노출
            if (!Particle)
                continue;

            const FVector2D Size = FVector2D(Particle->Size.X, Particle->Size.Y);
            FVector WorldPos = Particle->Location;

            if (SpriteData->bUseLocalSpace)
            {
                WorldPos = GetWorldMatrix().TransformPosition(WorldPos);
            }

            const FLinearColor Color = Particle->Color;

            for (int CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
            {
                FParticleSpriteVertex& V = Vertices[VertexCursor++];
                V.Position = WorldPos;
                V.Corner = CornerOffsets[CornerIndex];
                V.Size = Size;
                V.Color = Color;
            }

            ++WrittenParticles;
        }

        if (WrittenParticles >= ClampedCount)
            break;
    }

    Context->Unmap(ParticleVertexBuffer, 0);


    if (!ParticleMaterial)
    {
        ParticleMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleSprite.hlsl");
    }

    UShader* ParticleShader = nullptr;
    if (ParticleMaterial)
    {
        ParticleShader = ParticleMaterial->GetShader();
    }
    if (!ParticleShader)
    {
        ParticleShader = UResourceManager::GetInstance().Load<UShader>("Shaders/Effects/ParticleSprite.hlsl");
    }
    if (!ParticleShader)
    {
        return;
    }

    TArray<FShaderMacro> ShaderMacros = View ? View->ViewShaderMacros : TArray<FShaderMacro>();
    if (ParticleMaterial)
    {
        ShaderMacros.Append(ParticleMaterial->GetShaderMacros());
    }

    FShaderVariant* ShaderVariant = ParticleShader->GetOrCompileShaderVariant(ShaderMacros);
    if (!ShaderVariant)
    {
        return;
    }

    const int32 BatchIndex = OutMeshBatchElements.Add(FMeshBatchElement());
    FMeshBatchElement& Batch = OutMeshBatchElements[BatchIndex];

    Batch.VertexShader = ShaderVariant->VertexShader;
    Batch.PixelShader = ShaderVariant->PixelShader;
    Batch.InputLayout = ShaderVariant->InputLayout;
    Batch.Material = ParticleMaterial;
    Batch.VertexBuffer = ParticleVertexBuffer;
    Batch.IndexBuffer = ParticleIndexBuffer;
    Batch.VertexStride = sizeof(FParticleSpriteVertex);
    Batch.IndexCount = WrittenParticles * 6;
    Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    Batch.WorldMatrix = GetWorldMatrix();
    Batch.ObjectID = InternalIndex;
}

void UParticleSystemComponent::BuildEmitterRenderData()
{
    // 1) 이전 프레임 데이터 정리
    for (FDynamicEmitterDataBase* Data : EmitterRenderData)
    {
        delete Data;
    }
    EmitterRenderData.Empty();

    if (EmitterInstances.IsEmpty())
        return;

    // 2) 각 Instance -> DynamicData 생성
    for (int32 EmitterIdx = 0; EmitterIdx < EmitterInstances.Num(); ++EmitterIdx)
    {
        FParticleEmitterInstance* Inst = EmitterInstances[EmitterIdx];
        if (!Inst || Inst->ActiveParticles <= 0)
            continue;

        const EEmitterRenderType Type = Inst->GetDynamicType();

        FDynamicEmitterDataBase* NewData = nullptr;

        switch (Type)
        {
            case EEmitterRenderType::Sprite:
            {
                auto* SpriteData = new FDynamicSpriteEmitterData();
                SpriteData->EmitterIndex = EmitterIdx;
                SpriteData->SortMode = EParticleSortMode::ByViewDepth;
                SpriteData->SortPriority = 0;

                if (Inst->CachedRequiredModule)
                {
                    SpriteData->bUseLocalSpace = Inst->CachedRequiredModule->bUseLocalSpace;
                    SpriteData->Material = Inst->CachedRequiredModule->Material;
                }

                // 시뮬 결과 → Replay로 포장
                Inst->BuildReplayData(SpriteData->Source);

                NewData = SpriteData;
                break;
            }
            case EEmitterRenderType::Mesh:
            {
                auto* MeshData = new FDynamicMeshEmitterData();
                MeshData->EmitterIndex = EmitterIdx;
                MeshData->SortMode = EParticleSortMode::ByViewDepth;
                MeshData->SortPriority = 0;

                Inst->BuildReplayData(MeshData->Source);

                // 필요하면 Material은 Template 쪽에서
                if (Inst->CachedRequiredModule)
                {
                    MeshData->Material = Inst->CachedRequiredModule->Material;
                }

                NewData = MeshData;
                break;
            }
        }

        if (NewData)
        {
            NewData->EmitterType = Type;
            EmitterRenderData.Add(NewData);
        }
    }
}

UMaterialInterface* UParticleSystemComponent::GetMaterial(uint32 InSectionIndex) const
{
    return ParticleMaterial;
}

void UParticleSystemComponent::SetMaterial(uint32 InSectionIndex, UMaterialInterface* InNewMaterial)
{
    ParticleMaterial = InNewMaterial;
}

bool UParticleSystemComponent::EnsureParticleBuffers(uint32 ParticleCapacity)
{
    if (ParticleCapacity == 0)
    {
        return false;
    }

    if (ParticleVertexBuffer && ParticleIndexBuffer && ParticleCapacity <= ParticleVertexCapacity)
    {
        return true;
    }

    ReleaseParticleBuffers();
    ParticleVertexCapacity = ParticleCapacity;
    ParticleIndexCount = ParticleCapacity * 6;

    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11Device* Device = RHIDevice ? RHIDevice->GetDevice() : nullptr;
    if (!Device)
    {
        ParticleVertexCapacity = 0;
        ParticleIndexCount = 0;
        return false;
    }

    D3D11_BUFFER_DESC VertexDesc = {};
    VertexDesc.ByteWidth = ParticleVertexCapacity * 4 * sizeof(FParticleSpriteVertex);
    VertexDesc.Usage = D3D11_USAGE_DYNAMIC;
    VertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    VertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(Device->CreateBuffer(&VertexDesc, nullptr, &ParticleVertexBuffer)))
    {
        ReleaseParticleBuffers();
        return false;
    }

    TArray<uint32> Indices;
    Indices.SetNum(static_cast<int32>(ParticleCapacity * 6));
    for (uint32 ParticleIndex = 0; ParticleIndex < ParticleCapacity; ++ParticleIndex)
    {
        const uint32 VertexBase = ParticleIndex * 4;
        const uint32 IndexBase = ParticleIndex * 6;
        Indices[IndexBase + 0] = VertexBase + 2;
        Indices[IndexBase + 1] = VertexBase + 1;
        Indices[IndexBase + 2] = VertexBase + 0;
        Indices[IndexBase + 3] = VertexBase + 3;
        Indices[IndexBase + 4] = VertexBase + 2;
        Indices[IndexBase + 5] = VertexBase + 0;
    }

    D3D11_BUFFER_DESC IndexDesc = {};
    IndexDesc.ByteWidth = static_cast<UINT>(Indices.Num() * sizeof(uint32));
    IndexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    IndexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA IndexData = {};
    IndexData.pSysMem = Indices.GetData();

    if (FAILED(Device->CreateBuffer(&IndexDesc, &IndexData, &ParticleIndexBuffer)))
    {
        ReleaseParticleBuffers();
        return false;
    }

    return true;
}