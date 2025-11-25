#include "pch.h"
#include "ParticleSystemComponent.h"

#include "BoxComponent.h"
#include "BVHierarchy.h"
#include "CameraActor.h"
#include "CapsuleComponent.h"
#include "Collision.h"
#include "MeshBatchElement.h"
#include "PlatformTime.h"
#include "PlayerCameraManager.h"
#include "RenderManager.h"
#include "SceneView.h"
#include "SphereComponent.h"
#include "WorldPartitionManager.h"
#include "Source/Runtime/Engine/Particle/DynamicEmitterDataBase.h"
#include "Source/Runtime/Engine/Particle/ParticleEmitterInstance.h"
#include "Source/Runtime/Engine/Particle/ParticleLODLevel.h"
#include "Source/Runtime/Engine/Particle/ParticleStats.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleMesh.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleLocation.h"
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleSubUV.h"
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
    ReleaseInstanceBuffers();
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
    // 삭제하기 전에 비동기 작업이 끝날 때까지 기다림
    AsyncUpdater.EnsureCompletion();
    
    for (FParticleEmitterInstance* Inst : EmitterInstances)
    {
        if (Inst)
        {
            Inst->FreeParticleMemory();
            delete Inst;
        }
    }
    EmitterInstances.Empty();
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

    if (!Template) return;

    AccumulatedDeltaTime += DeltaTime;
    
    // [Main Thread] 비동기 관리자에게 작업 요청 & 결과 동기화
    FParticleSimulationContext Context;
    Context.DeltaTime = AccumulatedDeltaTime;
    Context.ComponentLocation = GetWorldLocation();
    Context.ComponentRotation = GetWorldRotation();
    Context.bIsActive = bIsActive;
    Context.bSuppressSpawning = bSuppressSpawning;
    Context.ComponentWorldMatrix = GetWorldMatrix();
    
    UCameraComponent* Camera = GWorld->GetWorldCamera();
    Context.CameraLocation = Camera ? Camera->GetWorldLocation() : FVector();
    Context.CameraRotation = Camera ? Camera->GetWorldRotation() : FQuat();

    if (GetWorld() && GetWorld()->GetPartitionManager())
    {
        float SearchRadius = 1000.0f; // 이거 나중에 ParticleSystem Asset에 정보 추가!!!!!!! 
        FVector Center = GetWorldLocation();
        
        FAABB QueryBox;
        QueryBox.Min = Center - FVector(SearchRadius, SearchRadius, SearchRadius);
        QueryBox.Max = Center + FVector(SearchRadius, SearchRadius, SearchRadius);

        TArray<UPrimitiveComponent*> Candidates = GetWorld()->GetPartitionManager()->GetBVH()->QueryIntersectedComponents(QueryBox);
        Context.WorldColliders.Reserve(Candidates.Num());

        for (UPrimitiveComponent* Prim : Candidates)
        {
            UShapeComponent* ShapeComponent = Cast<UShapeComponent>(Prim);
            if (!ShapeComponent) continue;

            const FTransform& TF = ShapeComponent->GetWorldTransform();
            FVector WorldLoc = TF.Translation;
            FQuat WorldRot = TF.Rotation;
            FVector Scale = TF.Scale3D;

            FColliderProxy Proxy;
            // [BOX]
            if (UBoxComponent* BoxComp = Cast<UBoxComponent>(ShapeComponent))
            {
                Proxy.Type = EShapeKind::Box;
                FShape Shape; BoxComp->GetShape(Shape);
                FOBB OBB; Collision::BuildOBB(Shape, TF, OBB);
                Proxy.Box = OBB;
            }
            // [SPHERE]
            else if (USphereComponent* SphereComp = Cast<USphereComponent>(ShapeComponent))
            {
                Proxy.Type = EShapeKind::Sphere;
                Proxy.Sphere.Center = WorldLoc;
                // 가장 큰 축의 스케일을 적용
                float MaxScale = Scale.GetMaxValue();
                Proxy.Sphere.Radius = SphereComp->SphereRadius * MaxScale;
            }
            // [CAPSULE]
            else if (UCapsuleComponent* CapsuleComp = Cast<UCapsuleComponent>(ShapeComponent))
            {
                Proxy.Type = EShapeKind::Capsule;
                float UnscaledRadius = CapsuleComp->CapsuleRadius;
                float ScaledRadius = UnscaledRadius * FMath::Max(FMath::Abs(Scale.X), FMath::Abs(Scale.Y));
                float UnscaledHalfHeight = CapsuleComp->CapsuleHalfHeight;
                float ScaledHalfHeight = UnscaledHalfHeight * FMath::Abs(Scale.Z);

                float CylHalfHeight = FMath::Max(0.0f, ScaledHalfHeight - ScaledRadius);

                FVector UpAxis = WorldRot.RotateVector(FVector{0, 0, 1});
                Proxy.Capsule.Radius = ScaledRadius;
                Proxy.Capsule.PosA = WorldLoc - (UpAxis * CylHalfHeight);
                Proxy.Capsule.PosB = WorldLoc + (UpAxis * CylHalfHeight);
            }

            Context.WorldColliders.Add(Proxy);
        }
    }
    
    if (bUseAsyncSimulation)
    {
        if (!AsyncUpdater.IsBusy())
        {
            // 워커가 놀고 있으면 누적된 시간만큼 일 시킴
            AsyncUpdater.KickOff(EmitterInstances, Context);
            AccumulatedDeltaTime = 0;
        }
    }
    else
    {
        AsyncUpdater.KickOffSync(EmitterInstances, Context);
        AccumulatedDeltaTime = 0;
    }

    // [Main Thread] 캐싱된 통계 데이터 사용
    const FParticleFrameStats& Stats = AsyncUpdater.LastFrameStats;
    FParticleStatManager::GetInstance().AddParticleCount(Stats.TotalActiveParticles);

    // 종료 처리
    if (bIsActive && Stats.bAllEmittersComplete)
    {
        OnParticleSystemFinished.Broadcast(this);
        DeactivateSystem(); 
    }

    if (!bIsActive && !Stats.bHasActiveParticles)
    {
        if (bAutoDestroy) 
        { 
            GetOwner()->Destroy(); 
        } 
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

    AsyncUpdater.TrySync();
    // EmitterInstance -> DynamicEmitterReplayDatabase
    TArray<FDynamicEmitterDataBase*>& CurrentData = AsyncUpdater.RenderData;
    if (CurrentData.IsEmpty()) return;

    // DynamicEmitterReplayDatabase -> MeshBatchElement
    BuildSpriteParticleBatch(CurrentData, OutMeshBatchElements, View);
    BuildMeshParticleBatch(CurrentData, OutMeshBatchElements, View);
}

void UParticleSystemComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
    EmitterInstances.Empty();
    ParticleVertexBuffer = nullptr;
    ParticleIndexBuffer = nullptr;
}


void UParticleSystemComponent::BuildSpriteParticleBatch(TArray<FDynamicEmitterDataBase*>& EmitterRenderData, TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
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

    const uint32 ClampedCount = MaxDebugParticles > 0
        ? std::min<uint32>(TotalParticles, (uint32)MaxDebugParticles)
        : TotalParticles;

    const FVector ViewOrigin = View ? View->ViewLocation : FVector::Zero();
    const FVector ViewDir = View
        ? View->ViewRotation.RotateVector(FVector(1, 0, 0)).GetSafeNormal()
        : FVector(1, 0, 0);

    if (bUseGpuInstancing)
    {
        BuildSpriteParticleBatch_Instanced(EmitterRenderData, OutMeshBatchElements, ClampedCount, ViewOrigin, ViewDir);
    }
    else
    {
        BuildSpriteParticleBatch_Immediate(EmitterRenderData, OutMeshBatchElements, ClampedCount, ViewOrigin, ViewDir);
    }
}

void UParticleSystemComponent::BuildSpriteParticleBatch_Instanced(
    TArray<FDynamicEmitterDataBase*>& EmitterRenderData,
    TArray<FMeshBatchElement>& OutMeshBatchElements,
    uint32 ClampedCount,
    const FVector& ViewOrigin,
    const FVector& ViewDir,
    const FSceneView* View)
{
    if (!EnsureInstanceBuffer(ClampedCount))
        return;

    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11DeviceContext* Context = RHIDevice ? RHIDevice->GetDeviceContext() : nullptr;
    if (!Context) return;

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(Context->Map(ParticleInstanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
        return;

    FParticleInstanceData* Instances = reinterpret_cast<FParticleInstanceData*>(Mapped.pData);

    struct FInstancedSpriteCommand
    {
        FDynamicSpriteEmitterData* SpriteData = nullptr;
        uint32 StartInstance = 0;
        uint32 InstanceCount = 0;
        int SortPriority = -1;
    };
    TArray<FInstancedSpriteCommand> InstancedCommands;

    uint32 WrittenInstances = 0;

    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EParticleType::Sprite) continue;

        auto* SpriteData = static_cast<FDynamicSpriteEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicSpriteEmitterReplayData*>(SpriteData->GetSource());
        if (!Src || Src->ActiveParticleCount <= 0) continue;

        const uint32 StartInstance = WrittenInstances;

        TArray<int32> SortIndices;
        SpriteData->SortParticles(ViewOrigin, ViewDir, GetWorldMatrix(), SortIndices);
        const bool bUseSortIndices = (SortIndices.Num() == Src->ActiveParticleCount);

        for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
        {
            if (WrittenInstances >= ClampedCount) break;

            const int32 ParticleIdx = bUseSortIndices ? SortIndices[LocalIdx] : LocalIdx;
            const FBaseParticle* Particle = SpriteData->GetParticle(ParticleIdx);
            if (!Particle) continue;

            FVector WorldPos = Particle->Location;
            if (SpriteData->bUseLocalSpace)
                WorldPos = GetWorldMatrix().TransformPosition(WorldPos);

            FParticleInstanceData& Inst = Instances[WrittenInstances++];
            Inst.Position = WorldPos;
            Inst.Size = FVector2D(Particle->Size.X, Particle->Size.Y);
            Inst.Color = Particle->Color;
            Inst.Rotation = Particle->Rotation;
        }

        const uint32 InstancesWritten = WrittenInstances - StartInstance;
        if (InstancesWritten > 0)
        {
            FInstancedSpriteCommand Cmd;
            Cmd.SpriteData = SpriteData;
            Cmd.StartInstance = StartInstance;
            Cmd.InstanceCount = InstancesWritten;
            Cmd.SortPriority = Src->RequiredModule ? Src->RequiredModule->SortPriority : -1;
            InstancedCommands.Add(Cmd);
        }

        if (WrittenInstances >= ClampedCount) break;
    }

    Context->Unmap(ParticleInstanceBuffer, 0);

    if (InstancedCommands.IsEmpty())
        return;

    // 쉐이더 준비
    if (!InstanceFallbackMaterial)
        InstanceFallbackMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleSprite_Instanced.hlsl");

    if (!InstanceFallbackMaterial || !InstanceFallbackMaterial->GetShader())
        return;

    TArray<FShaderMacro> ShaderMacros;
    if (View)
        ShaderMacros = View->ViewShaderMacros;
    ShaderMacros.Append(InstanceFallbackMaterial->GetShaderMacros());

    FShaderVariant* ShaderVariant = InstanceFallbackMaterial->GetShader()
        ->GetOrCompileShaderVariant(ShaderMacros);
    if (!ShaderVariant) return;

    // 배치 생성
    for (const FInstancedSpriteCommand& Cmd : InstancedCommands)
    {
        if (Cmd.InstanceCount == 0) continue;

        FMeshBatchElement& Batch = OutMeshBatchElements[OutMeshBatchElements.Add(FMeshBatchElement())];

        Batch.VertexShader = ShaderVariant->VertexShader;
        Batch.PixelShader = ShaderVariant->PixelShader;
        Batch.InputLayout = ShaderVariant->InputLayout;
        Batch.Material = ResolveEmitterMaterial(*Cmd.SpriteData);

        Batch.VertexBuffer = nullptr;
        Batch.IndexBuffer = nullptr;
        Batch.VertexStride = 0;
        Batch.IndexCount = 6;
        Batch.StartIndex = 0;
        Batch.BaseVertexIndex = 0;
        Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        Batch.WorldMatrix = GetWorldMatrix();
        Batch.ObjectID = InternalIndex;
        Batch.SortPriority = Cmd.SortPriority;

        Batch.InstanceCount = Cmd.InstanceCount;
        Batch.InstanceStart = Cmd.StartInstance;
        Batch.bInstancedDraw = true;
        Batch.InstancingShaderResourceView = ParticleInstanceSRV;
    }
}


void UParticleSystemComponent::BuildSpriteParticleBatch_Immediate(
    TArray<FDynamicEmitterDataBase*>& EmitterRenderData,
    TArray<FMeshBatchElement>& OutMeshBatchElements,
    uint32 ClampedCount,
    const FVector& ViewOrigin,
    const FVector& ViewDir,
    const FSceneView* View)
{
    if (!EnsureParticleBuffers(ClampedCount)) return;

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
        int SortPriority = -1;
    };

    TArray<FSpriteBatchCommand> SpriteBatchCommands;
    FParticleSpriteVertex* Vertices = reinterpret_cast<FParticleSpriteVertex*>(Mapped.pData);

    static const FVector2D CornerOffsets[4] = {
        FVector2D(-1.0f, -1.0f), FVector2D(1.0f, -1.0f),
        FVector2D(1.0f,  1.0f),  FVector2D(-1.0f,  1.0f)
    };

    uint32 VertexCursor = 0;
    uint32 WrittenParticles = 0;

    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EParticleType::Sprite) continue;

        auto* SpriteData = static_cast<FDynamicSpriteEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicSpriteEmitterReplayData*>(SpriteData->GetSource());
        if (!Src || Src->ActiveParticleCount <= 0) continue;

        const uint32 StartParticle = WrittenParticles;

        TArray<int32> SortIndices;
        SpriteData->SortParticles(ViewOrigin, ViewDir, GetWorldMatrix(), SortIndices);
        const bool bUseSortIndices = (SortIndices.Num() == Src->ActiveParticleCount);

        for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
        {
            if (WrittenParticles >= ClampedCount) break;

            const int32 ParticleIdx = bUseSortIndices ? SortIndices[LocalIdx] : LocalIdx;
            const FBaseParticle* Particle = SpriteData->GetParticle(ParticleIdx);
            if (!Particle) continue;

            FVector WorldPos = Particle->Location;
            if (SpriteData->bUseLocalSpace)
                WorldPos = GetWorldMatrix().TransformPosition(WorldPos);

            // SubImageIndex 추출 (SubUV 모듈이 있을 때)
            float SubImageIndex = 0.0f;
            if (Src->SubUVModule && Src->SubUVPayloadOffset >= 0)
            {
                const uint8* ParticleBase = reinterpret_cast<const uint8*>(Particle);
                SubImageIndex = *reinterpret_cast<const float*>(ParticleBase + Src->SubUVPayloadOffset);
            }

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
                Vertex.SubImageIndex = SubImageIndex;
            }

            ++WrittenParticles;
        }

        if (WrittenParticles > StartParticle)
        {
            FSpriteBatchCommand Cmd;
            Cmd.SpriteData = SpriteData;
            Cmd.StartParticle = StartParticle;
            Cmd.ParticleCount = WrittenParticles - StartParticle;
            Cmd.SortPriority = Src->RequiredModule ? Src->RequiredModule->SortPriority : -1;
            SpriteBatchCommands.Add(Cmd);
        }

        if (WrittenParticles >= ClampedCount) break;
    }

    Context->Unmap(ParticleVertexBuffer, 0);

    if (SpriteBatchCommands.IsEmpty()) return;

    // 쉐이더 준비
    if (!FallbackMaterial)
        FallbackMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleSprite.hlsl");

    if (!FallbackMaterial || !FallbackMaterial->GetShader())
        return;

    TArray<FShaderMacro> ShaderMacros;
    if (View)
        ShaderMacros = View->ViewShaderMacros;
    ShaderMacros.Append(FallbackMaterial->GetShaderMacros());

    FShaderVariant* ShaderVariant = FallbackMaterial->GetShader()
        ->GetOrCompileShaderVariant(ShaderMacros);
    if (!ShaderVariant) return;

    // 배치 생성
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
        Batch.SortPriority = Cmd.SortPriority;

        // SubUV 파라미터 설정
        const FDynamicSpriteEmitterReplayData* SrcData =
            static_cast<const FDynamicSpriteEmitterReplayData*>(Cmd.SpriteData->GetSource());
        if (SrcData && SrcData->RequiredModule)
        {
            Batch.SubImages_Horizontal = SrcData->RequiredModule->SubImages_Horizontal;
            Batch.SubImages_Vertical = SrcData->RequiredModule->SubImages_Vertical;
        }
        if (SrcData && SrcData->SubUVModule)
        {
            Batch.SubUV_InterpMethod = static_cast<int32>(SrcData->SubUVModule->InterpMethod);
        }
    }
}

void UParticleSystemComponent::BuildMeshParticleBatch(TArray<FDynamicEmitterDataBase*>& EmitterRenderData, TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
	if (EmitterRenderData.IsEmpty())
		return;

	if (bUseGpuInstancing)
	{
		BuildMeshParticleBatch_Instanced(EmitterRenderData, OutMeshBatchElements, View);
	}
	else
	{
		BuildMeshParticleBatch_Immediate(EmitterRenderData, OutMeshBatchElements, View);
	}
}

void UParticleSystemComponent::BuildMeshParticleBatch_Immediate(TArray<FDynamicEmitterDataBase*>& EmitterRenderData, TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (EmitterRenderData.IsEmpty())
        return;

    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EParticleType::Mesh)
            continue;

        auto* MeshData = static_cast<FDynamicMeshEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicMeshEmitterReplayData*>(MeshData->GetSource());
        if (!Src || !Src->Mesh || Src->ActiveParticleCount <= 0)
            continue;

        UStaticMesh* StaticMesh = Src->Mesh;
        
        TArray<FShaderMacro> ShaderMacros = View ? View->ViewShaderMacros : TArray<FShaderMacro>();

        bool bUseLighting = Src->bLighting;
        if (bUseLighting)
        {
            ShaderMacros.Add(FShaderMacro("PARTICLE_LIGHTING", "1"));
        }
        else
        {
            ShaderMacros.Add(FShaderMacro("PARTICLE_LIGHTING", "0"));
        }

        UMaterial* ParticleMeshMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleMesh.hlsl");

        if (!ParticleMeshMaterial || !ParticleMeshMaterial->GetShader())
        {
            UE_LOG("[BuildMeshParticleBatch] Failed to load ParticleMesh shader");
            continue;
        }

        ShaderMacros.Append(ParticleMeshMaterial->GetShaderMacros());

        FShaderVariant* ShaderVariant = ParticleMeshMaterial->GetShader()->GetOrCompileShaderVariant(ShaderMacros);
        if (!ShaderVariant)
        {
            UE_LOG("[BuildMeshParticleBatch] Failed to compile ParticleMesh shader variant");
            continue;
        }

        UMaterialInterface* Material = ResolveEmitterMaterial(*MeshData);

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
        const FMatrix ComponentWorld = GetWorldMatrix();

        const FVector ViewOrigin = View ? View->ViewLocation : FVector::Zero();
        FVector ViewDir = FVector(1, 0, 0);
        if (View)
        {
            ViewDir = View->ViewRotation.RotateVector(FVector(1, 0, 0)).GetSafeNormal();
        }

        TArray<int32> SortIndices;
        MeshData->SortParticles(ViewOrigin, ViewDir, ComponentWorld, SortIndices);
		const bool bUseSortIndices = (SortIndices.Num() == Src->ActiveParticleCount);

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

void UParticleSystemComponent::BuildMeshParticleBatch_Instanced(
	TArray<FDynamicEmitterDataBase*>& EmitterRenderData,
	TArray<FMeshBatchElement>& OutMeshBatchElements,
	const FSceneView* View)
{
	uint32 TotalParticles = 0;
	for (FDynamicEmitterDataBase* Base : EmitterRenderData)
	{
		if (!Base || Base->EmitterType != EParticleType::Mesh)
			continue;

		const auto* Src = static_cast<const FDynamicMeshEmitterReplayData*>(Base->GetSource());
		if (Src)
		{
			TotalParticles += static_cast<uint32>(Src->ActiveParticleCount);
		}
	}

	if (TotalParticles == 0)
	{
		return;
	}

	const uint32 ClampedCount = MaxDebugParticles > 0
		? std::min<uint32>(TotalParticles, static_cast<uint32>(MaxDebugParticles))
		: TotalParticles;

	if (!EnsureMeshInstanceBuffer(ClampedCount))
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
	if (FAILED(Context->Map(MeshInstanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return;
	}

	FMeshParticleInstanceData* Instances = reinterpret_cast<FMeshParticleInstanceData*>(Mapped.pData);

	struct FMeshInstancedCommand
	{
		FDynamicMeshEmitterData* MeshData = nullptr;
		UStaticMesh* StaticMesh = nullptr;
		FShaderVariant* ShaderVariant = nullptr;
		UMaterialInterface* Material = nullptr;
		uint32 StartInstance = 0;
		uint32 InstanceCount = 0;
		int SortPriority = -1;
	};

	TArray<FMeshInstancedCommand> Commands;
	uint32 WrittenInstances = 0;
	const FMatrix ComponentWorld = GetWorldMatrix();
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
		ID3D11Buffer* MeshVB = StaticMesh->GetVertexBuffer();
		ID3D11Buffer* MeshIB = StaticMesh->GetIndexBuffer();
		const uint32 MeshIndexCount = StaticMesh->GetIndexCount();
		const uint32 MeshVertexStride = StaticMesh->GetVertexStride();
		if (!MeshVB || !MeshIB || MeshIndexCount == 0)
		{
			UE_LOG("[BuildMeshParticleBatch] Mesh %s has invalid buffers",
				StaticMesh->GetName().c_str());
			continue;
		}

		TArray<FShaderMacro> ShaderMacros = View ? View->ViewShaderMacros : TArray<FShaderMacro>();
		ShaderMacros.Add(FShaderMacro("PARTICLE_LIGHTING", Src->bLighting ? "1" : "0"));
		ShaderMacros.Add(FShaderMacro("PARTICLE_MESH_INSTANCING", "1"));

		UMaterial* ParticleMeshMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleMesh.hlsl");
		if (!ParticleMeshMaterial || !ParticleMeshMaterial->GetShader())
		{
			UE_LOG("[BuildMeshParticleBatch] Failed to load ParticleMesh shader");
			continue;
		}
		ShaderMacros.Append(ParticleMeshMaterial->GetShaderMacros());
		FShaderVariant* ShaderVariant = ParticleMeshMaterial->GetShader()->GetOrCompileShaderVariant(ShaderMacros);
		if (!ShaderVariant)
		{
			UE_LOG("[BuildMeshParticleBatch] Failed to compile ParticleMesh shader variant");
			continue;
		}

		UMaterialInterface* Material = ResolveEmitterMaterial(*MeshData);
		const uint32 StartInstance = WrittenInstances;

		TArray<int32> SortIndices;
		MeshData->SortParticles(ViewOrigin, ViewDir, ComponentWorld, SortIndices);
		const bool bUseSortIndices = (SortIndices.Num() == Src->ActiveParticleCount);

		for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
		{
			if (WrittenInstances >= ClampedCount)
			{
				break;
			}

			const int32 ParticleIdx = bUseSortIndices ? SortIndices[LocalIdx] : LocalIdx;
			const FBaseParticle* Particle = MeshData->GetParticle(ParticleIdx);
			if (!Particle)
			{
				continue;
			}

			FMeshParticleInstanceData& Instance = Instances[WrittenInstances++];
			FQuat RotationQuat = FQuat::FromAxisAngle(FVector(0.0f, 0.0f, 1.0f), Particle->Rotation);
			FTransform ParticleTransform(Particle->Location, RotationQuat, Particle->Size);
			FMatrix ParticleWorld = ParticleTransform.ToMatrix();
			if (MeshData->bUseLocalSpace)
			{
				ParticleWorld = ParticleWorld * ComponentWorld;
			}

			Instance.WorldMatrix = ParticleWorld;
			Instance.WorldInverseTranspose = ParticleWorld.InverseAffine().Transpose();
			Instance.Color = Particle->Color;
		}

		const uint32 InstancesWritten = WrittenInstances - StartInstance;
		if (InstancesWritten > 0)
		{
			FMeshInstancedCommand Cmd;
			Cmd.MeshData = MeshData;
			Cmd.StaticMesh = StaticMesh;
			Cmd.ShaderVariant = ShaderVariant;
			Cmd.Material = Material;
			Cmd.StartInstance = StartInstance;
			Cmd.InstanceCount = InstancesWritten;
			Cmd.SortPriority = MeshData->SortPriority;
			Commands.Add(Cmd);
		}

		if (WrittenInstances >= ClampedCount)
		{
			break;
		}
	}

	Context->Unmap(MeshInstanceBuffer, 0);

	if (Commands.IsEmpty())
	{
		return;
	}

	for (const FMeshInstancedCommand& Cmd : Commands)
	{
		if (!Cmd.ShaderVariant || !Cmd.StaticMesh)
		{
			continue;
		}

		ID3D11Buffer* MeshVB = Cmd.StaticMesh->GetVertexBuffer();
		ID3D11Buffer* MeshIB = Cmd.StaticMesh->GetIndexBuffer();
		const uint32 MeshIndexCount = Cmd.StaticMesh->GetIndexCount();
		const uint32 MeshVertexStride = Cmd.StaticMesh->GetVertexStride();
		if (!MeshVB || !MeshIB || MeshIndexCount == 0)
		{
			continue;
		}

		FMeshBatchElement& Batch = OutMeshBatchElements[OutMeshBatchElements.Add(FMeshBatchElement())];
		Batch.VertexShader = Cmd.ShaderVariant->VertexShader;
		Batch.PixelShader = Cmd.ShaderVariant->PixelShader;
		Batch.InputLayout = Cmd.ShaderVariant->InputLayout;
		Batch.Material = Cmd.Material;
		Batch.VertexBuffer = MeshVB;
		Batch.IndexBuffer = MeshIB;
		Batch.VertexStride = MeshVertexStride;
		Batch.IndexCount = MeshIndexCount;
		Batch.StartIndex = 0;
		Batch.BaseVertexIndex = 0;
		Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		Batch.WorldMatrix = FMatrix::Identity();
		Batch.ObjectID = InternalIndex;
		Batch.SortPriority = Cmd.SortPriority;
		Batch.InstanceCount = Cmd.InstanceCount;
		Batch.InstanceStart = Cmd.StartInstance;
		Batch.bInstancedDraw = true;
		Batch.InstancingShaderResourceView = MeshInstanceSRV;
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

bool UParticleSystemComponent::EnsureInstanceBuffer(uint32 InstanceCount)
{
    if (InstanceCount == 0)
        return false;

    if (ParticleInstanceBuffer && InstanceCount <= InstanceCapacity)
        return true; // 이미 충분

    // 기존 것 폐기
    if (ParticleInstanceSRV) { ParticleInstanceSRV->Release(); ParticleInstanceSRV = nullptr; }
    if (ParticleInstanceBuffer) { ParticleInstanceBuffer->Release(); ParticleInstanceBuffer = nullptr; }
    InstanceCapacity = 0;

    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11Device* Device = RHIDevice ? RHIDevice->GetDevice() : nullptr;
    if (!Device) return false;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(FParticleInstanceData) * InstanceCount;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(FParticleInstanceData);

    if (FAILED(Device->CreateBuffer(&desc, nullptr, &ParticleInstanceBuffer)))
        return false;

    // SRV 생성
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = InstanceCount;

    if (FAILED(Device->CreateShaderResourceView(ParticleInstanceBuffer,
        &srvDesc, &ParticleInstanceSRV)))
    {
        ParticleInstanceBuffer->Release();
        ParticleInstanceBuffer = nullptr;
        return false;
    }

    InstanceCapacity = InstanceCount;
    return true;
}

bool UParticleSystemComponent::EnsureMeshInstanceBuffer(uint32 InstanceCount)
{
    if (InstanceCount == 0)
        return false;

    if (MeshInstanceBuffer && InstanceCount <= MeshInstanceCapacity)
        return true;

    if (MeshInstanceSRV) { MeshInstanceSRV->Release(); MeshInstanceSRV = nullptr; }
    if (MeshInstanceBuffer) { MeshInstanceBuffer->Release(); MeshInstanceBuffer = nullptr; }
    MeshInstanceCapacity = 0;

    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11Device* Device = RHIDevice ? RHIDevice->GetDevice() : nullptr;
    if (!Device) return false;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(FMeshParticleInstanceData) * InstanceCount;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(FMeshParticleInstanceData);

    if (FAILED(Device->CreateBuffer(&desc, nullptr, &MeshInstanceBuffer)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = InstanceCount;

    if (FAILED(Device->CreateShaderResourceView(MeshInstanceBuffer, &srvDesc, &MeshInstanceSRV)))
    {
        MeshInstanceBuffer->Release();
        MeshInstanceBuffer = nullptr;
        return false;
    }

    MeshInstanceCapacity = InstanceCount;
    return true;
}

void UParticleSystemComponent::ReleaseInstanceBuffers()
{
    if (ParticleInstanceSRV)
    {
        ParticleInstanceSRV->Release();
        ParticleInstanceSRV = nullptr;
    }

    if (ParticleInstanceBuffer)
    {
        ParticleInstanceBuffer->Release();
        ParticleInstanceBuffer = nullptr;
    }
    InstanceCapacity = 0;

    if (MeshInstanceSRV)
    {
        MeshInstanceSRV->Release();
        MeshInstanceSRV = nullptr;
    }

    if (MeshInstanceBuffer)
    {
        MeshInstanceBuffer->Release();
        MeshInstanceBuffer = nullptr;
    }
    MeshInstanceCapacity = 0;
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


void UParticleSystemComponent::RenderDebugVolume(URenderer* Renderer) const
{
    if (!Template) return;

    const FTransform WorldTransform = GetWorldTransform();
    TArray<FVector> StartPoints;
    TArray<FVector> EndPoints;
    TArray<FVector4> Colors;

    // 각 Emitter의 Location 모듈 찾아서 범위 그리기
    for (const auto* Emitter : Template->Emitters)
    {
        if (!Emitter || Emitter->LODLevels.IsEmpty()) continue;

        const UParticleLODLevel* LOD = Emitter->LODLevels[0];
        if (!LOD) continue;

        // Location 모듈 찾기
        UParticleModuleLocation* LocationModule = nullptr;
        for (auto* Module : LOD->AllModulesCache)
        {
            if (auto* LocMod = Cast<UParticleModuleLocation>(Module))
            {
                LocationModule = LocMod;
                break;
            }
        }

        if (!LocationModule) continue;

        const FVector4 DebugColor(0.0f, 1.0f, 0.0f, 1.0f); // 초록색

        switch (LocationModule->DistributionType)
        {
        case ELocationDistributionType::Box:
        {
            // Box 8개 꼭지점
            const FVector Extent = LocationModule->BoxExtent;
            FVector LocalCorners[8] = {
                {-Extent.X, -Extent.Y, -Extent.Z}, {+Extent.X, -Extent.Y, -Extent.Z},
                {-Extent.X, +Extent.Y, -Extent.Z}, {+Extent.X, +Extent.Y, -Extent.Z},
                {-Extent.X, -Extent.Y, +Extent.Z}, {+Extent.X, -Extent.Y, +Extent.Z},
                {-Extent.X, +Extent.Y, +Extent.Z}, {+Extent.X, +Extent.Y, +Extent.Z},
            };

            FVector WorldCorners[8];
            for (int i = 0; i < 8; i++)
            {
                WorldCorners[i] = WorldTransform.TransformPosition(LocalCorners[i]);
            }

            // 12개 엣지
            static const int Edges[12][2] = {
                {0,1},{1,3},{3,2},{2,0}, // bottom
                {4,5},{5,7},{7,6},{6,4}, // top
                {0,4},{1,5},{2,6},{3,7}  // verticals
            };

            for (int i = 0; i < 12; ++i)
            {
                StartPoints.Add(WorldCorners[Edges[i][0]]);
                EndPoints.Add(WorldCorners[Edges[i][1]]);
                Colors.Add(DebugColor);
            }
        }
        break;

        case ELocationDistributionType::Sphere:
        {
            // Sphere를 원으로 표현 (3개 원: XY, YZ, XZ 평면)
            const float Radius = LocationModule->SphereRadius;
            const int Segments = 32;

            // XY 평면 원
            for (int i = 0; i < Segments; ++i)
            {
                float angle1 = (float)i / Segments * 2.0f * PI;
                float angle2 = (float)(i + 1) / Segments * 2.0f * PI;

                FVector p1(Radius * cosf(angle1), Radius * sinf(angle1), 0.0f);
                FVector p2(Radius * cosf(angle2), Radius * sinf(angle2), 0.0f);

                StartPoints.Add(WorldTransform.TransformPosition(p1));
                EndPoints.Add(WorldTransform.TransformPosition(p2));
                Colors.Add(DebugColor);
            }

            // YZ 평면 원
            for (int i = 0; i < Segments; ++i)
            {
                float angle1 = (float)i / Segments * 2.0f * PI;
                float angle2 = (float)(i + 1) / Segments * 2.0f * PI;

                FVector p1(0.0f, Radius * cosf(angle1), Radius * sinf(angle1));
                FVector p2(0.0f, Radius * cosf(angle2), Radius * sinf(angle2));

                StartPoints.Add(WorldTransform.TransformPosition(p1));
                EndPoints.Add(WorldTransform.TransformPosition(p2));
                Colors.Add(DebugColor);
            }

            // XZ 평면 원
            for (int i = 0; i < Segments; ++i)
            {
                float angle1 = (float)i / Segments * 2.0f * PI;
                float angle2 = (float)(i + 1) / Segments * 2.0f * PI;

                FVector p1(Radius * cosf(angle1), 0.0f, Radius * sinf(angle1));
                FVector p2(Radius * cosf(angle2), 0.0f, Radius * sinf(angle2));

                StartPoints.Add(WorldTransform.TransformPosition(p1));
                EndPoints.Add(WorldTransform.TransformPosition(p2));
                Colors.Add(DebugColor);
            }
        }
        break;

        case ELocationDistributionType::Cylinder:
        {
            // Cylinder: 상단/하단 원 + 수직선
            const float Radius = LocationModule->CylinderRadius;
            const float HalfHeight = LocationModule->CylinderHeight * 0.5f;
            const int Segments = 32;

            // 상단 원 (Z = +HalfHeight)
            for (int i = 0; i < Segments; ++i)
            {
                float angle1 = (float)i / Segments * 2.0f * PI;
                float angle2 = (float)(i + 1) / Segments * 2.0f * PI;

                FVector p1(Radius * cosf(angle1), Radius * sinf(angle1), HalfHeight);
                FVector p2(Radius * cosf(angle2), Radius * sinf(angle2), HalfHeight);

                StartPoints.Add(WorldTransform.TransformPosition(p1));
                EndPoints.Add(WorldTransform.TransformPosition(p2));
                Colors.Add(DebugColor);
            }

            // 하단 원 (Z = -HalfHeight)
            for (int i = 0; i < Segments; ++i)
            {
                float angle1 = (float)i / Segments * 2.0f * PI;
                float angle2 = (float)(i + 1) / Segments * 2.0f * PI;

                FVector p1(Radius * cosf(angle1), Radius * sinf(angle1), -HalfHeight);
                FVector p2(Radius * cosf(angle2), Radius * sinf(angle2), -HalfHeight);

                StartPoints.Add(WorldTransform.TransformPosition(p1));
                EndPoints.Add(WorldTransform.TransformPosition(p2));
                Colors.Add(DebugColor);
            }

            // 수직선 4개 (상단-하단 연결)
            for (int i = 0; i < 4; ++i)
            {
                float angle = (float)i / 4.0f * 2.0f * PI;
                FVector pTop(Radius * cosf(angle), Radius * sinf(angle), HalfHeight);
                FVector pBottom(Radius * cosf(angle), Radius * sinf(angle), -HalfHeight);

                StartPoints.Add(WorldTransform.TransformPosition(pTop));
                EndPoints.Add(WorldTransform.TransformPosition(pBottom));
                Colors.Add(DebugColor);
            }
        }
        break;

        case ELocationDistributionType::Point:
            // Point는 범위가 없으므로 그리지 않음
            break;
        }
    }

    if (!StartPoints.IsEmpty())
    {
        Renderer->AddLines(StartPoints, EndPoints, Colors);
    }
}
