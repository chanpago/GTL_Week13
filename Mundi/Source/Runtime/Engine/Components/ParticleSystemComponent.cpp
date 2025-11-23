#include "pch.h"
#include "ParticleSystemComponent.h"
#include "MeshBatchElement.h"
#include "SceneView.h"
#include "Source/Runtime/Engine/Particle/DynamicEmitterDataBase.h"
#include "Source/Runtime/Engine/Particle/ParticleLODLevel.h"

UParticleSystemComponent::UParticleSystemComponent()
{
    // FOR TEST !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    bCanEverTick = true;  // 파티클 시스템은 매 프레임 Tick 필요
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
    if (!Template)
    {
        UE_LOG("[ParticleSystemComponent::InitParticles] Template is NULL!");
        return;
    }

    UE_LOG("[ParticleSystemComponent::InitParticles] Template: %s, Emitters: %d",
           Template->GetName().c_str(), Template->Emitters.Num());

    // 에셋에 정의된 이미터 개수만큼 인스턴스 생성
    for (int32 i = 0; i < Template->Emitters.Num(); i++)
    {
        UParticleEmitter* EmitterAsset = Template->Emitters[i];

        // 이미터가 유효하고 켜져있다면
        if (EmitterAsset)
        {
            UE_LOG("[ParticleSystemComponent::InitParticles] Creating instance for Emitter[%d]: %s",
                   i, EmitterAsset->GetName().c_str());
            FParticleEmitterInstance* NewInst = new FParticleEmitterInstance();
            NewInst->Init(EmitterAsset, this);
            EmitterInstances.Add(NewInst);
        }
        else
        {
            UE_LOG("[ParticleSystemComponent::InitParticles] Emitter[%d] is NULL!", i);
        }
    }

    if (bAutoActivate) { ActivateSystem(); }
    UE_LOG("[ParticleSystemComponent::InitParticles] Completed. EmitterInstances: %d", EmitterInstances.Num());
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    static bool bFirstTick = true;
    if (bFirstTick)
    {
        UE_LOG("[ParticleSystemComponent::TickComponent] First Tick!");
        UE_LOG("[ParticleSystemComponent::TickComponent] bIsActive: %s, Template: %s, EmitterInstances: %d",
               bIsActive ? "true" : "false",
               Template ? Template->GetName().c_str() : "NULL",
               EmitterInstances.Num());
        bFirstTick = false;
    }

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

    ClearEmitterRenderData();

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

void UParticleSystemComponent::ClearEmitterRenderData()
{
    for (FDynamicEmitterDataBase* Data : EmitterRenderData)
    {
        if (Data)
        {
            if (auto* Sprite = dynamic_cast<FDynamicSpriteEmitterData*>(Data))
            {
                Sprite->Source.DataContainer.Free();
            }
            else if (auto* Mesh = dynamic_cast<FDynamicMeshEmitterData*>(Data))
            {
                Mesh->Source.DataContainer.Free();
            }

            delete Data;
        }
    }
    EmitterRenderData.Empty();
}

void UParticleSystemComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (!IsVisible())
    {
        return;
    }
    
    // 시뮬레이션 데이터 → Replay → DynamicData
    BuildEmitterRenderData();

    if (EmitterRenderData.IsEmpty())
        return;

    BuildSpriteParticleBatch(OutMeshBatchElements, View);
    BuildMeshParticleBatch(OutMeshBatchElements, View);

    // 이번 프레임 끝에 DynamicData 파괴
    ClearEmitterRenderData();
}

void UParticleSystemComponent::BuildSpriteParticleBatch(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (EmitterRenderData.IsEmpty())
        return;

    // 1) Sprite 파티클 총 개수
    uint32 TotalParticles = 0;
    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EEmitterRenderType::Sprite)
            continue;

        const auto* Src = static_cast<const FDynamicSpriteEmitterReplayData*>(Base->GetSource());
        if (!Src)
        {
            continue;
        }

        TotalParticles += static_cast<uint32>(Src->ActiveParticleCount);
    }

    if (TotalParticles == 0)
    {
        return;
    }

    const uint32 ClampedCount = MaxDebugParticles > 0
        ? std::min<uint32>(TotalParticles, static_cast<uint32>(MaxDebugParticles))
        : TotalParticles;

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

    struct FSpriteBatchCommand
    {
        FDynamicSpriteEmitterData* SpriteData = nullptr;
        uint32 StartParticle = 0;
        uint32 ParticleCount = 0;
    };

    TArray<FSpriteBatchCommand> SpriteBatchCommands;

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
    FVector ViewDir = FVector(1, 0, 0);
    if (View)
    {
        ViewDir = View->ViewRotation.RotateVector(FVector(1, 0, 0)).GetSafeNormal();
    }

    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EEmitterRenderType::Sprite)
        {
            continue;
        }

        auto* SpriteData = static_cast<FDynamicSpriteEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicSpriteEmitterReplayData*>(SpriteData->GetSource());
        if (!Src || Src->ActiveParticleCount <= 0)
        {
            continue;
        }

        const uint32 StartParticle = WrittenParticles;
        const int32 Num = Src->ActiveParticleCount;

        TArray<int32> SortIndices;
        SpriteData->SortParticles(ViewOrigin, ViewDir, SortIndices);
        const bool bUseSortIndices = (SortIndices.Num() == Num);

        for (int32 LocalIdx = 0; LocalIdx < Num; ++LocalIdx)
        {
            if (WrittenParticles >= ClampedCount)
            {
                break;
            }

            const int32 ParticleIdx = bUseSortIndices ? SortIndices[LocalIdx] : LocalIdx;
            const FBaseParticle* Particle = SpriteData->GetParticle(ParticleIdx);
            if (!Particle)
            {
                continue;
            }

            const FVector2D Size = FVector2D(Particle->Size.X, Particle->Size.Y);
            FVector WorldPos = Particle->Location;
            if (SpriteData->bUseLocalSpace)
            {
                WorldPos = GetWorldMatrix().TransformPosition(WorldPos);
            }

            const FLinearColor Color = Particle->Color;

            // 첫 파티클만 로그 출력
            if (LocalIdx == 0 && WrittenParticles == 0)
            {
                UE_LOG("[BuildParticleBatch] First Particle Color: (%.2f, %.2f, %.2f, %.2f)",
                       Color.R, Color.G, Color.B, Color.A);
            }

            for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
            {
                FParticleSpriteVertex& Vertex = Vertices[VertexCursor++];
                Vertex.Position = WorldPos;
                Vertex.Corner = CornerOffsets[CornerIndex];
                Vertex.Size = Size;
                Vertex.Color = Color;
            }

            ++WrittenParticles;
        }

        if (WrittenParticles > StartParticle)
        {
            const int32 CommandIndex = SpriteBatchCommands.Add(FSpriteBatchCommand());
            FSpriteBatchCommand& Command = SpriteBatchCommands[CommandIndex];
            Command.SpriteData = SpriteData;
            Command.StartParticle = StartParticle;
            Command.ParticleCount = WrittenParticles - StartParticle;
        }

        if (WrittenParticles >= ClampedCount)
        {
            break;
        }
    }

    Context->Unmap(ParticleVertexBuffer, 0);

    if (SpriteBatchCommands.IsEmpty())
    {
        return;
    }

    // 파티클 전용 쉐이더 로드 (파티클은 특별한 vertex layout 필요)
    if (!ParticleMaterial)
    {
        ParticleMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleSprite.hlsl");
    }

    if (!ParticleMaterial)
    {
        UE_LOG("[BuildParticleBatch] ERROR: Failed to load particle shader!");
        return;
    }

    UShader* ParticleShader = ParticleMaterial->GetShader();
    if (!ParticleShader)
    {
        UE_LOG("[BuildParticleBatch] ERROR: Particle material has no shader!");
        return;
    }

    UE_LOG("[BuildParticleBatch] Using ParticleMaterial: %s, Shader: %s",
           ParticleMaterial->GetName().c_str(),
           ParticleShader ? ParticleShader->GetName().c_str() : "NULL");

    TArray<FShaderMacro> ShaderMacros = View ? View->ViewShaderMacros : TArray<FShaderMacro>();
    ShaderMacros.Append(ParticleMaterial->GetShaderMacros());

    FShaderVariant* ShaderVariant = ParticleShader->GetOrCompileShaderVariant(ShaderMacros);
    if (!ShaderVariant)
    {
        UE_LOG("[BuildParticleBatch] ERROR: Failed to compile particle shader variant!");
        return;
    }

    for (const FSpriteBatchCommand& Cmd : SpriteBatchCommands)
    {
        // 각 Emitter의 Material을 가져옴 (텍스처 적용용)
        UMaterialInterface* EmitterMaterial = nullptr;
        if (Cmd.SpriteData)
        {
            const auto* ReplayData = static_cast<const FDynamicSpriteEmitterReplayData*>(Cmd.SpriteData->GetSource());
            if (ReplayData)
            {
                EmitterMaterial = ReplayData->MaterialInterface;
                UE_LOG("[BuildParticleBatch] ReplayData->MaterialInterface: %s",
                       EmitterMaterial ? EmitterMaterial->GetName().c_str() : "NULL");
            }
            else
            {
                UE_LOG("[BuildParticleBatch] ReplayData is NULL!");
            }
        }
        else
        {
            UE_LOG("[BuildParticleBatch] SpriteData is NULL!");
        }

        if (Cmd.ParticleCount == 0)
        {
            continue;
        }

        // Mesh Batch Element 생성
        const int32 BatchIndex = OutMeshBatchElements.Add(FMeshBatchElement());
        FMeshBatchElement& Batch = OutMeshBatchElements[BatchIndex];

        // 항상 파티클 전용 쉐이더 사용 (FParticleSpriteVertex layout 필요)
        Batch.VertexShader = ShaderVariant->VertexShader;
        Batch.PixelShader = ShaderVariant->PixelShader;
        Batch.InputLayout = ShaderVariant->InputLayout;

        // Material은 EmitterMaterial 사용 (텍스처 바인딩용), 없으면 기본 파티클 Material
        Batch.Material = EmitterMaterial ? EmitterMaterial : ParticleMaterial;

        UE_LOG("[BuildParticleBatch] Batch VS: %p, PS: %p, Material: %s",
               Batch.VertexShader, Batch.PixelShader,
               Batch.Material ? Batch.Material->GetName().c_str() : "NULL");

        Batch.VertexBuffer = ParticleVertexBuffer;
        Batch.IndexBuffer = ParticleIndexBuffer;
        Batch.VertexStride = sizeof(FParticleSpriteVertex);
        Batch.IndexCount = Cmd.ParticleCount * 6;
        Batch.StartIndex = Cmd.StartParticle * 6;
        Batch.BaseVertexIndex = 0;
        Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        Batch.WorldMatrix = GetWorldMatrix();
        Batch.ObjectID = InternalIndex;

        UE_LOG("[BuildParticleBatch] Shader: ParticleSprite, Material: %s, Particles: %d",
               Batch.Material ? Batch.Material->GetName().c_str() : "NULL", Cmd.ParticleCount);
    }
}

void UParticleSystemComponent::BuildMeshParticleBatch(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (EmitterRenderData.IsEmpty())
        return;

    const FVector ViewOrigin = View ? View->ViewLocation : FVector::Zero();
    FVector ViewDir = FVector(1, 0, 0);
    if (View)
    {
        ViewDir = View->ViewRotation.RotateVector(FVector(1, 0, 0)).GetSafeNormal();
    }

    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EEmitterRenderType::Mesh)
            continue;

        auto* MeshData = static_cast<FDynamicMeshEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicMeshEmitterReplayData*>(MeshData->GetSource());
        if (!Src || !Src->Mesh || Src->ActiveParticleCount <= 0)
            continue;

        UStaticMesh* StaticMesh = Src->Mesh;

        UParticleEmitter* SourceEmitter = nullptr;
        UParticleModuleRequired* RequiredModule = nullptr;
        if (Template && MeshData->EmitterIndex >= 0 && MeshData->EmitterIndex < Template->Emitters.Num())
        {
            SourceEmitter = Template->Emitters[MeshData->EmitterIndex];
            if (SourceEmitter && SourceEmitter->LODLevels.Num() > 0)
            {
                UParticleLODLevel* LOD0 = SourceEmitter->LODLevels[0];
                if (LOD0)
                {
                    RequiredModule = LOD0->RequiredModule;
                }
            }
        }

        UMaterialInterface* Material = nullptr;
        if (Src->OverrideMaterial)
        {
            Material = Src->OverrideMaterial;
        }
        else if (RequiredModule && RequiredModule->Material)
        {
            Material = RequiredModule->Material;
        }

        if (!Material && SourceEmitter && SourceEmitter->bUseMeshMaterials && StaticMesh)
        {
            const TArray<FGroupInfo>& Groups = StaticMesh->GetMeshGroupInfo();
            if (!Groups.IsEmpty())
            {
                const FString& MatName = Groups[0].InitialMaterialName;
                if (!MatName.empty())
                {
                    UMaterial* MeshMaterial = UResourceManager::GetInstance().Load<UMaterial>(MatName);
                    Material = MeshMaterial;
                }
            }
        }


        if (!Material)
        {
            Material = ParticleMaterial;
        }

        if (!Material)
        {
            UE_LOG("[BuildMeshParticleBatch] No material for mesh emitter %d", MeshData->EmitterIndex);
            continue;
        }
        UShader* MeshShader = Material->GetShader();
        if (!MeshShader)
        {
            UE_LOG("[BuildMeshParticleBatch] Material %s has no shader",
                Material->GetName().c_str());
            continue;
        }

        TArray<FShaderMacro> ShaderMacros = View ? View->ViewShaderMacros : TArray<FShaderMacro>();
        ShaderMacros.Append(Material->GetShaderMacros());

        FShaderVariant* ShaderVariant = MeshShader->GetOrCompileShaderVariant(ShaderMacros);
        if (!ShaderVariant)
        {
            UE_LOG("[BuildMeshParticleBatch] Failed to get shader variant for %s",
                Material->GetName().c_str());
            continue;
        }

        ID3D11Buffer* MeshVB = StaticMesh->GetVertexBuffer();
        ID3D11Buffer* MeshIB = StaticMesh->GetIndexBuffer();
        const uint32  MeshIndexCount = StaticMesh->GetIndexCount();
        const uint32  MeshVertexStride = StaticMesh->GetVertexStride();
        const D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        if (!MeshVB || !MeshIB || MeshIndexCount == 0)
        {
            UE_LOG("[BuildMeshParticleBatch] Mesh %s has invalid buffers",
                StaticMesh->GetName().c_str());
            continue;
        }

        const int32 NumParticles = Src->ActiveParticleCount;

        TArray<int32> SortIndices;
        MeshData->SortParticles(ViewOrigin, ViewDir, SortIndices);
        const bool bUseSortIndices = (SortIndices.Num() == NumParticles);

        for (int32 LocalIdx = 0; LocalIdx < NumParticles; ++LocalIdx)
        {
            const int32 ParticleIdx =
                bUseSortIndices ? SortIndices[LocalIdx] : LocalIdx;

            const FBaseParticle* Particle = MeshData->GetParticle(ParticleIdx);
            if (!Particle)
                continue;

            FVector ParticleWorldPos = Particle->Location;
            FMatrix ComponentWorld = GetWorldMatrix();

            if (MeshData->bUseLocalSpace)
            {
                ParticleWorldPos = ComponentWorld.TransformPosition(ParticleWorldPos);
            }

            FMatrix ParticleWorld = ComponentWorld;
            const int32 BatchIndex = OutMeshBatchElements.Add(FMeshBatchElement());
            FMeshBatchElement& Batch = OutMeshBatchElements[BatchIndex];

            Batch.VertexShader = ShaderVariant->VertexShader;
            Batch.PixelShader = ShaderVariant->PixelShader;
            Batch.InputLayout = ShaderVariant->InputLayout;

            Batch.Material = Material;
            Batch.VertexBuffer = MeshVB;
            Batch.IndexBuffer = MeshIB;
            Batch.VertexStride = MeshVertexStride;
            Batch.IndexCount = MeshIndexCount;
            Batch.StartIndex = 0;
            Batch.BaseVertexIndex = 0;
            Batch.PrimitiveTopology = Topology;

            Batch.WorldMatrix = ParticleWorld;
            Batch.ObjectID = InternalIndex;

        }
    }
}

void UParticleSystemComponent::BuildEmitterRenderData()
{
    // 1) 이전 프레임 데이터 정리
    ClearEmitterRenderData();

    if (!EmitterInstances.IsEmpty())
    {
        // 2) 각 Instance -> DynamicData 생성
        for (int32 EmitterIdx = 0; EmitterIdx < EmitterInstances.Num(); ++EmitterIdx)
        {
            FParticleEmitterInstance* Inst = EmitterInstances[EmitterIdx];
            if (!Inst || Inst->ActiveParticles <= 0)
            {
                continue;
            }

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
                    }

                    Inst->BuildReplayData(SpriteData->Source);
                    // BuildReplayData()에서 이미 CachedRequiredModule->Material을 설정함!

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
                    NewData = MeshData;

                    if (!MeshData->Source.Mesh)
                    {
                        delete MeshData;
                        NewData = nullptr;
                    }

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

    if (EmitterRenderData.IsEmpty() && bEnableDebugEmitter)
    {
        //BuildDebugEmitterData();
    }
}

void UParticleSystemComponent::BuildDebugEmitterData()
{
    if (!bEnableDebugEmitter)
    {
        return;
    }

    constexpr int32 DebugCount = 3;
    const FVector DebugPositions[DebugCount] = {
        FVector(0.0f, 0.0f, 100.0f),
        FVector(80.0f, 20.0f, 120.0f),
        FVector(-60.0f, -40.0f, 90.0f)
    };
    const FLinearColor DebugColors[DebugCount] = {
        FLinearColor(1.0f, 0.2f, 0.2f, 0.85f),
        FLinearColor(0.2f, 1.0f, 0.3f, 0.85f),
        FLinearColor(0.2f, 0.4f, 1.0f, 0.85f)
    };
    const float DebugSizes[DebugCount] = { 50.0f, 70.0f, 90.0f };

    auto* SpriteData = new FDynamicSpriteEmitterData();
    SpriteData->EmitterIndex = -1;
    SpriteData->SortMode = EParticleSortMode::ByViewDepth;
    SpriteData->SortPriority = 0;

    UMaterial* TempMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleSprite.hlsl");
    if (TempMaterial)
    {
        FMaterialInfo Info = TempMaterial->GetMaterialInfo();
        Info.DiffuseTextureFileName = FString("Data/cube_texture.png");
        TempMaterial->SetMaterialInfo(Info);
        SpriteData->Source.MaterialInterface = TempMaterial;
    }
    else
    {
        SpriteData->Source.MaterialInterface = ParticleMaterial;
    }


    auto& Replay = SpriteData->Source;
    Replay.EmitterType = EEmitterRenderType::Sprite;
    Replay.ActiveParticleCount = DebugCount;
    Replay.ParticleStride = sizeof(FBaseParticle);
    Replay.DataContainer.Allocate(DebugCount * Replay.ParticleStride, DebugCount);

    for (int32 Index = 0; Index < DebugCount; ++Index)
    {
        uint8* ParticleBase = Replay.DataContainer.ParticleData + Replay.ParticleStride * Index;
        auto* Particle = reinterpret_cast<FBaseParticle*>(ParticleBase);
        std::memset(Particle, 0, sizeof(FBaseParticle));

        Particle->Location = DebugPositions[Index];
        const float Size = DebugSizes[Index];
        Particle->Size = FVector(Size, Size, 1.0f);
        Particle->Color = DebugColors[Index];
        Particle->RelativeTime = 0.0f;
        Particle->OneOverMaxLifetime = 1.0f;

        Replay.DataContainer.ParticleIndices[Index] = static_cast<uint16>(Index);
    }

    EmitterRenderData.Add(SpriteData);
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

