#include "pch.h"
#include "ParticleSystemComponent.h"
#include "MeshBatchElement.h"
#include "PlatformTime.h"
#include "SceneView.h"
#include "Source/Runtime/Engine/Particle/DynamicEmitterDataBase.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitterInstance.h"
#include "Source/Runtime/Engine/Particle/ParticleLODLevel.h"
#include "Source/Runtime/Engine/Particle/ParticleStats.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleMesh.h"

// ============================================================================
// Constructor & Destructor
// ============================================================================

UParticleSystemComponent::UParticleSystemComponent()
{
    SetActive(true);
    bTickInEditor = true;
    bCanEverTick = true;  // 파티클 시스템은 매 프레임 Tick 필요
    bAutoActivate = true;

    MaxDebugParticles = 10000; 
}

UParticleSystemComponent::~UParticleSystemComponent()
{
    DestroyParticles();
    ReleaseParticleBuffers();
}

// ============================================================================
// Initialization & Lifecycle
// ============================================================================

void UParticleSystemComponent::InitParticles()
{
    DestroyParticles();

    if (!Template)
    {
        UE_LOG("[ParticleSystemComponent::InitParticles] Template is NULL!");
        return;
    }

    // 에셋에 정의된 이미터 개수만큼 인스턴스 생성
    for (int32 i = 0; i < Template->Emitters.Num(); i++)
    {
        UParticleEmitter* EmitterAsset = Template->Emitters[i];

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
}

void UParticleSystemComponent::BeginPlay()
{
    Super::BeginPlay();
    InitParticles();
}

void UParticleSystemComponent::EndPlay()
{
    DestroyParticles();
    ReleaseParticleBuffers();
    Super::EndPlay();
}

// ============================================================================
// Update
// ============================================================================

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!bIsActive || !Template) return;

    uint32 FrameParticleCount = 0;
    bool bAllEmittersComplete = true;
    for (FParticleEmitterInstance* Inst : EmitterInstances)
    {
        TIME_PROFILE(Particle_EmitterTick)
        Inst->Tick(DeltaTime);
        FrameParticleCount += Inst->ActiveParticles;

        if (!Inst->IsComplete())
        {
            bAllEmittersComplete = false;
        }
    }

    FParticleStatManager::GetInstance().AddParticleCount(FrameParticleCount);

    if (bAllEmittersComplete)
    {
        OnParticleSystemFinished.Broadcast(this);
        DeactivateSystem();

        if (bAutoDestroy) { GetOwner()->Destroy(); }
    }
}

// ============================================================================
// Rendering
// ============================================================================

void UParticleSystemComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (!IsVisible())
    {
        return;
    }
    TIME_PROFILE(Particle_CollectBatches)

    // EmitterInstance -> DynamicEmitterReplayDatabase
    BuildEmitterRenderData();

    if (EmitterRenderData.IsEmpty())
        return;

    // DaynamicEmitterReplayDatabase -> MeshBatchElement
    BuildSpriteParticleBatch(OutMeshBatchElements, View);
    BuildMeshParticleBatch(OutMeshBatchElements, View);

    // 이번 프레임 끝에 DynamicData 파괴
    ClearEmitterRenderData();
}

void UParticleSystemComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
    EmitterInstances.Empty();
    EmitterRenderData.Empty();
    ParticleVertexBuffer = nullptr;
    ParticleIndexBuffer = nullptr;
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
            if (!Inst || Inst->ActiveParticles <= 0) continue;

            const EParticleType Type = Inst->GetDynamicType();
            FDynamicEmitterDataBase* NewData = nullptr;

            if (Type == EParticleType::Sprite)
            {
                auto* SpriteData = new FDynamicSpriteEmitterData();
                SpriteData->EmitterType = Type;
                SpriteData->EmitterIndex = EmitterIdx;
                SpriteData->SortMode = Inst->CachedRequiredModule->SortMode;
                SpriteData->SortPriority = 0;

                if (Inst->CachedRequiredModule)
                {
                    SpriteData->bUseLocalSpace = Inst->CachedRequiredModule->bUseLocalSpace;
                }

                Inst->BuildReplayData(SpriteData->Source);
                NewData = SpriteData;
            }
            else if (Type == EParticleType::Mesh)
            {
                auto* MeshData = new FDynamicMeshEmitterData();
                MeshData->EmitterType = Type;
                MeshData->EmitterIndex = EmitterIdx;
                MeshData->SortMode = Inst->CachedRequiredModule->SortMode;
                MeshData->SortPriority = 0;

                if (Inst->CachedRequiredModule)
                {
                    MeshData->bUseLocalSpace = Inst->CachedRequiredModule->bUseLocalSpace;
                }

                Inst->BuildReplayData(MeshData->Source);

                if (MeshData->Source.Mesh)
                {
                    NewData = MeshData;
                }
                else
                {
                    delete MeshData;
                }
            }

            if (NewData)
            {
                EmitterRenderData.Add(NewData);
            }
        }
    }
}

void UParticleSystemComponent::BuildSpriteParticleBatch(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (EmitterRenderData.IsEmpty())
        return;

    // 1) 총 파티클 개수 계산
    uint32 TotalParticles = 0;
    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EParticleType::Sprite) continue;

        const auto* Src = static_cast<const FDynamicSpriteEmitterReplayData*>(Base->GetSource());
        if (Src)
        {
            TotalParticles += static_cast<uint32>(Src->ActiveParticleCount);
        }
    }
    if (TotalParticles == 0) return;

    // 2) 버퍼 확보
    const uint32 ClampedCount = MaxDebugParticles > 0
        ? std::min<uint32>(TotalParticles, static_cast<uint32>(MaxDebugParticles))
        : TotalParticles;
    if (!EnsureParticleBuffers(ClampedCount)) return;

    // 3) 버퍼에 버텍스 데이터 쓰기
    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11DeviceContext* Context = RHIDevice ? RHIDevice->GetDeviceContext() : nullptr;
    if (!Context) return;

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(Context->Map(ParticleVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
        return;

    struct FSpriteBatchCommand
    {
        FDynamicSpriteEmitterData* SpriteData = nullptr;
        uint32 StartParticle = 0;
        uint32 ParticleCount = 0;
    };

    TArray<FSpriteBatchCommand> SpriteBatchCommands;

    FParticleSpriteVertex* Vertices = reinterpret_cast<FParticleSpriteVertex*>(Mapped.pData);
    static const FVector2D CornerOffsets[4] = {
        FVector2D(-1.0f, -1.0f), FVector2D(1.0f, -1.0f),
        FVector2D(1.0f,  1.0f),  FVector2D(-1.0f,  1.0f)
    };

    const FVector ViewOrigin = View ? View->ViewLocation : FVector::Zero();
    const FVector ViewDir = View
        ? View->ViewRotation.RotateVector(FVector(1, 0, 0)).GetSafeNormal()
        : FVector(1, 0, 0);

    uint32 VertexCursor = 0;
    uint32 WrittenParticles = 0;

    // 4) 각 Emitter의 Particle을 Vertex Buffer에 기록
    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EParticleType::Sprite) continue;

        auto* SpriteData = static_cast<FDynamicSpriteEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicSpriteEmitterReplayData*>(SpriteData->GetSource());
        if (!Src || Src->ActiveParticleCount <= 0) continue;

        const uint32 StartParticle = WrittenParticles;

        // 파티클 정렬
        TArray<int32> SortIndices;
        SpriteData->SortParticles(ViewOrigin, ViewDir, SortIndices);
        const bool bUseSortIndices = (SortIndices.Num() == Src->ActiveParticleCount);

        // 파티클 순회하며 버텍스 생성
        for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
        {
            if (WrittenParticles >= ClampedCount) break;

            const int32 ParticleIdx = bUseSortIndices ? SortIndices[LocalIdx] : LocalIdx;
            const FBaseParticle* Particle = SpriteData->GetParticle(ParticleIdx);
            if (!Particle) continue;

            FVector WorldPos = Particle->Location;
            if (SpriteData->bUseLocalSpace)  // Local Space: 로컬 좌표를 월드로 변환 (컴포넌트 종속)
            {
                WorldPos = GetWorldMatrix().TransformPosition(WorldPos);
            }
            // World Space: 이미 월드 좌표이므로 그대로 사용 (독립적)

            const FVector2D Size = FVector2D(Particle->Size.X, Particle->Size.Y);
            const FLinearColor Color = Particle->Color;
            const float Rotation = Particle->Rotation;

            // 4개 코너 버텍스 생성
            for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
            {
                FParticleSpriteVertex& Vertex = Vertices[VertexCursor++];
                Vertex.Position = WorldPos;
                Vertex.Corner = CornerOffsets[CornerIndex];
                Vertex.Size = Size;
                Vertex.Color = Color;
                Vertex.Rotation = Rotation;
            }

            ++WrittenParticles;
        }

        // 배치 커맨드 생성
        if (WrittenParticles > StartParticle)
        {
            FSpriteBatchCommand Cmd;
            Cmd.SpriteData = SpriteData;
            Cmd.StartParticle = StartParticle;
            Cmd.ParticleCount = WrittenParticles - StartParticle;
            SpriteBatchCommands.Add(Cmd);
        }

        if (WrittenParticles >= ClampedCount) break;
    }

    Context->Unmap(ParticleVertexBuffer, 0);

    if (SpriteBatchCommands.IsEmpty()) return;

    // 5) 파티클 전용 쉐이더 준비
    if (!FallbackMaterial)
    {
        FallbackMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleSprite.hlsl");
    }
    if (!FallbackMaterial || !FallbackMaterial->GetShader())
    {
        UE_LOG("[BuildSpriteParticleBatch] ERROR: Failed to load particle shader!");
        return;
    }

    TArray<FShaderMacro> ShaderMacros = View ? View->ViewShaderMacros : TArray<FShaderMacro>();
    ShaderMacros.Append(FallbackMaterial->GetShaderMacros());

    FShaderVariant* ShaderVariant = FallbackMaterial->GetShader()
        ->GetOrCompileShaderVariant(ShaderMacros);
    if (!ShaderVariant)
    {
        UE_LOG("[BuildSpriteParticleBatch] ERROR: Failed to compile shader variant!");
        return;
    }

    // 6) MeshBatch 생성
    for (const FSpriteBatchCommand& Cmd : SpriteBatchCommands)
    {
        if (Cmd.ParticleCount == 0) continue;

        FMeshBatchElement& Batch = OutMeshBatchElements[OutMeshBatchElements.Add(FMeshBatchElement())];

        Batch.VertexShader = ShaderVariant->VertexShader;
        Batch.PixelShader = ShaderVariant->PixelShader;
        Batch.InputLayout = ShaderVariant->InputLayout;
        Batch.Material = ResolveEmitterMaterial(*Cmd.SpriteData);

        Batch.VertexBuffer = ParticleVertexBuffer;
        Batch.IndexBuffer = ParticleIndexBuffer;
        Batch.VertexStride = sizeof(FParticleSpriteVertex);
        Batch.IndexCount = Cmd.ParticleCount * 6;
        Batch.StartIndex = Cmd.StartParticle * 6;
        Batch.BaseVertexIndex = 0;
        Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        Batch.WorldMatrix = GetWorldMatrix();
        Batch.ObjectID = InternalIndex;
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
        if (!Base || Base->EmitterType != EParticleType::Mesh)
            continue;

        auto* MeshData = static_cast<FDynamicMeshEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicMeshEmitterReplayData*>(MeshData->GetSource());
        if (!Src || !Src->Mesh || Src->ActiveParticleCount <= 0)
            continue;

        UStaticMesh* StaticMesh = Src->Mesh;
        UMaterialInterface* Material = ResolveEmitterMaterial(*MeshData);;

        if (!Material || !Material->GetShader()) continue;
        
        TArray<FShaderMacro> ShaderMacros = View ? View->ViewShaderMacros : TArray<FShaderMacro>();
        ShaderMacros.Append(Material->GetShaderMacros());

        FShaderVariant* ShaderVariant = Material->GetShader()->GetOrCompileShaderVariant(ShaderMacros);
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

        if (!MeshVB || !MeshIB || MeshIndexCount == 0)
        {
            UE_LOG("[BuildMeshParticleBatch] Mesh %s has invalid buffers",
                StaticMesh->GetName().c_str());
            continue;
        }

        // 파티클 정렬
        TArray<int32> SortIndices;
        MeshData->SortParticles(ViewOrigin, ViewDir, SortIndices);
        const bool bUseSortIndices = (SortIndices.Num() == Src->ActiveParticleCount);

        const FMatrix ComponentWorld = GetWorldMatrix();

        for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
        {
            const int32 ParticleIdx = bUseSortIndices ? SortIndices[LocalIdx] : LocalIdx;
            const FBaseParticle* Particle = MeshData->GetParticle(ParticleIdx);
            if (!Particle)
                continue;

            FVector ParticleWorldPos = Particle->Location;
            FMatrix ParticleWorld;

            if (MeshData->bUseLocalSpace)  // Local Space: 로컬 좌표 + 컴포넌트 변환 (컴포넌트 종속)
            {
                ParticleWorld = FMatrix::MakeScale(Particle->Size) * FMatrix::MakeTranslation(ParticleWorldPos) * ComponentWorld;
            }
            else  // World Space: 이미 월드 좌표이므로 그대로 사용 (독립적)
            {
                ParticleWorld = FMatrix::MakeScale(Particle->Size) * FMatrix::MakeTranslation(ParticleWorldPos);
            }

            FMeshBatchElement& Batch = OutMeshBatchElements[OutMeshBatchElements.Add(FMeshBatchElement())];

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
            Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            Batch.WorldMatrix = ParticleWorld;
            Batch.ObjectID = InternalIndex;
        }
    }
}

// ============================================================================
// Material Resolution
// ============================================================================
UMaterialInterface* UParticleSystemComponent::ResolveEmitterMaterial(const FDynamicEmitterDataBase& DynData) const
{
    UParticleEmitter* SourceEmitter = nullptr;
    UParticleModuleRequired* RequiredModule = nullptr;
    UParticleModuleMesh* MeshModule = nullptr;
    
    if (Template &&
        DynData.EmitterIndex >= 0 &&
        DynData.EmitterIndex < Template->Emitters.Num())
    {
        SourceEmitter = Template->Emitters[DynData.EmitterIndex];
        if (SourceEmitter && !SourceEmitter->LODLevels.IsEmpty())
        {
            if (auto* LOD0 = SourceEmitter->LODLevels[0])
            {
                RequiredModule = LOD0->RequiredModule;
                MeshModule = Cast<UParticleModuleMesh>(LOD0->TypeDataModule);
            }
        }
    }
    

    // 만약 MeshEmitter + bUseMeshMaterials -> StaticMesh 재질
    if (DynData.EmitterType == EParticleType::Mesh && MeshModule)
    {
        // Override Material 사용 (bUseMeshMaterials = false)
        if (!MeshModule->bUseMeshMaterials && MeshModule->OverrideMaterial)
        {
            return MeshModule->OverrideMaterial;
        }

        // Mesh 기본 Material 사용 (bUseMeshMaterials = true)
        if (MeshModule->bUseMeshMaterials && MeshModule->Mesh)
        {
            const auto& Groups = MeshModule->Mesh->GetMeshGroupInfo();
            if (!Groups.IsEmpty())
            {
                const FString& MatName = Groups[0].InitialMaterialName;
                if (!MatName.empty())
                    if (auto* M = UResourceManager::GetInstance().Load<UMaterial>(MatName))
                        return M;
            }
        }
    }

    if (RequiredModule && RequiredModule->Material)
    {
        return RequiredModule->Material;
    }
    
    return FallbackMaterial;
}

// ============================================================================
// Resource Management
// ============================================================================
bool UParticleSystemComponent::EnsureParticleBuffers(uint32 ParticleCapacity)
{
    if (ParticleCapacity == 0) return false;

    if (ParticleVertexBuffer && ParticleIndexBuffer &&
        ParticleCapacity <= ParticleVertexCapacity)
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

    // 버텍스 버퍼 생성
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

    // 인덱스 버퍼 생성 (Quad 인덱스)
    TArray<uint32> Indices;
    Indices.SetNum(static_cast<int32>(ParticleCapacity * 6));
    for (uint32 i = 0; i < ParticleCapacity; ++i)
    {
        const uint32 VertexBase = i * 4;
        const uint32 IndexBase = i * 6;
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
