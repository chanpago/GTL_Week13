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
#include "Source/Runtime/Engine/Particle/Modules/ParticleModuleRibbon.h"
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
    UParticleSystemComponent::DestroyParticles();
    ReleaseParticleBuffers();
    ReleaseInstanceBuffers();
    ReleaseRibbonBuffers();
    ReleaseBeamBuffers();
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
    ReleaseInstanceBuffers();
    ReleaseRibbonBuffers();
    ReleaseBeamBuffers();
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
    BuildBeamParticleBatch(CurrentData, OutMeshBatchElements, View);
    BuildRibbonParticleBatch(CurrentData, OutMeshBatchElements, View);
}

void UParticleSystemComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
    EmitterInstances.Empty();
    ParticleVertexBuffer = nullptr;
    ParticleIndexBuffer = nullptr;
    
    // TODO Release
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
    const TArray<FDynamicEmitterDataBase*>& EmitterRenderData,
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

    FParticleInstanceData* Instances = static_cast<FParticleInstanceData*>(Mapped.pData);

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

        for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
        {
            if (WrittenInstances >= ClampedCount) break;
            const int32 ParticleIdx = Base->AsyncSortedIndices[LocalIdx];
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
            Inst.Velocity = Particle->Velocity;
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
        
        Batch.ScreenAlignment = Cmd.SpriteData->Alignment;
        Batch.InstanceCount = Cmd.InstanceCount;
        Batch.InstanceStart = Cmd.StartInstance;
        Batch.bInstancedDraw = true;
        Batch.InstancingShaderResourceView = ParticleInstanceSRV;
    }
}


void UParticleSystemComponent::BuildSpriteParticleBatch_Immediate(
    const TArray<FDynamicEmitterDataBase*>& EmitterRenderData,
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
        for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
        {
            if (WrittenParticles >= ClampedCount) break;

            const int32 ParticleIdx = Base->AsyncSortedIndices[LocalIdx];
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
            const FVector Velocity = Particle->Velocity;

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
                Vertex.Velocity = Velocity;
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
        Batch.ScreenAlignment = Cmd.SpriteData->Alignment;

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

void UParticleSystemComponent::BuildRibbonParticleBatch(
    TArray<FDynamicEmitterDataBase*>& EmitterRenderData,
    TArray<FMeshBatchElement>& OutMeshBatchElements,
    const FSceneView* View)
{
    if (EmitterRenderData.IsEmpty())
        return;

    // 1) 전체 spine point 개수 집계 (capacity 확보용)
    uint32 TotalSpinePoints = 0;
    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EParticleType::Ribbon)
            continue;

        const auto* Src = static_cast<const FDynamicRibbonEmitterReplayData*>(Base->GetSource());
        if (Src && Src->ActiveParticleCount >= 2)
        {
            TotalSpinePoints += static_cast<uint32>(Src->ActiveParticleCount);
        }
    }

    if (TotalSpinePoints < 2)
        return;

    // 2) 리본 버퍼 확보 (spine point 기준)
    if (!EnsureRibbonBuffers(TotalSpinePoints))
        return;

    // 3) GPU 매핑
    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11DeviceContext* Context = RHIDevice ? RHIDevice->GetDeviceContext() : nullptr;
    if (!Context)
        return;

    D3D11_MAPPED_SUBRESOURCE VMap = {};
    if (FAILED(Context->Map(RibbonVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &VMap)))
        return;
    FParticleSpriteVertex* Vertices = reinterpret_cast<FParticleSpriteVertex*>(VMap.pData);

    D3D11_MAPPED_SUBRESOURCE IMap = {};
    if (FAILED(Context->Map(RibbonIndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &IMap)))
    {
        Context->Unmap(RibbonVertexBuffer, 0);
        return;
    }
    uint32* IndicesPtr = reinterpret_cast<uint32*>(IMap.pData);

    const FVector ViewOrigin = View ? View->ViewLocation : FVector::Zero();

    uint32 VertexCursor = 0;
    uint32 IndexCursor = 0;

    struct FRibbonBatchCommand
    {
        FDynamicRibbonEmitterData* RibbonData = nullptr;
        uint32 StartIndex = 0;
        uint32 IndexCount = 0;
        int32  SortPriority = 0;
    };
    TArray<FRibbonBatchCommand> RibbonCommands;

    // 4) 리본 지오메트리 생성 (trail 체인 기반)
    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EParticleType::Ribbon)
            continue;

        auto* RibbonData = static_cast<FDynamicRibbonEmitterData*>(Base);
        const auto* Src = &RibbonData->Source;

        const int32 TrailCount = Src->TrailCount;
        const TArray<int32>& TrailHeads = Src->TrailHeads;
        const float Width = Src->Width;
        const float TilingDistance = Src->TilingDistance;
        const bool  bUseCameraFacing = Src->bUseCameraFacing;

        if (!Src->DataContainer.ParticleData ||
            TrailCount <= 0)
        {
            continue;
        }

        // 이 emitter에서 처음으로 쓰는 인덱스 위치
        const uint32 EmitterBaseIndexStart = IndexCursor;

        // trail 별로 별도 체인 생성
        for (int32 TrailIdx = 0; TrailIdx < TrailCount; ++TrailIdx)
        {
            // 4-1) 이 trail의 head부터 NextIndex 따라가며 체인 구성
            TArray<int32> Chain;
            int32 Current = TrailHeads[TrailIdx];

            // Safety guard: 무한루프 방지
            int32 Safety = 0;
            while (Current != INDEX_NONE && Safety < Src->ActiveParticleCount)
            {
                ++Safety;
                Chain.Add(Current);

                const uint8* BasePtr =
                    Src->DataContainer.ParticleData +
                    Src->ParticleStride * Current;

                const FBaseParticle* ChainParticle =
                    reinterpret_cast<const FBaseParticle*>(BasePtr);

                if (!ChainParticle)
                    break;

                Current = ChainParticle->NextIndex;
            }

            if (Chain.Num() < 2)
            {
                continue; // 한 점짜리는 무시
            }

            const uint32 BaseVertexIndex = VertexCursor;
            const uint32 BaseIndexIndex = IndexCursor;

            // 4-2) 체인 순서대로 정점 생성
            float CurrentDistance = 0.0f;

            for (int32 ci = 0; ci < Chain.Num(); ++ci)
            {
                const int32 ParticleIndex = Chain[ci];
                const FBaseParticle* Particle = RibbonData->GetParticle(ParticleIndex);
                if (!Particle)
                    continue;

                const FVector ParticleWorldPos = RibbonData->bUseLocalSpace
                    ? GetWorldMatrix().TransformPosition(Particle->Location)
                    : Particle->Location;

                // Tangent: 체인 기준 이전/다음 점으로 계산
                FVector Tangent;
                if (ci == 0)
                {
                    const FVector NextPos = RibbonData->GetParticlePosition(Chain[ci + 1]);
                    Tangent = (NextPos - ParticleWorldPos).GetSafeNormal();
                }
                else if (ci == Chain.Num() - 1)
                {
                    const FVector PrevPos = RibbonData->GetParticlePosition(Chain[ci - 1]);
                    Tangent = (ParticleWorldPos - PrevPos).GetSafeNormal();
                }
                else
                {
                    const FVector PrevPos = RibbonData->GetParticlePosition(Chain[ci - 1]);
                    const FVector NextPos = RibbonData->GetParticlePosition(Chain[ci + 1]);
                    Tangent = (NextPos - PrevPos).GetSafeNormal();
                }

                // Right vector: 카메라 기반 또는 월드 업 기반
                FVector RightVector;
                if (bUseCameraFacing)
                {
                    FVector ToCam = (ViewOrigin - ParticleWorldPos).GetSafeNormal();
                    RightVector = FVector::Cross(ToCam, Tangent).GetSafeNormal();
                }
                else
                {
                    RightVector = FVector::Cross(FVector(0, 0, 1), Tangent).GetSafeNormal();
                }

                const float Age = FMath::Clamp(RibbonData->GetParticleAge(ParticleIndex), 0.0f, 1.0f);
                const float FadeFactor = 1.0f - Age;
                const float WidthScale = FMath::Max(0.01f, Width * FadeFactor);

                FLinearColor Color = Particle->Color;
                Color.A *= FadeFactor;

                // 거리 누적 (segment 길이)
                if (ci > 0)
                {
                    const FVector PrevPos = RibbonData->GetParticlePosition(Chain[ci - 1]);
                    CurrentDistance += FVector::Distance(ParticleWorldPos, PrevPos);
                }

                float V_Coord;
                if (TilingDistance > 0.0f)
                {
                    V_Coord = CurrentDistance / TilingDistance;
                }
                else
                {
                    V_Coord = static_cast<float>(ci) /
                        static_cast<float>(Chain.Num() - 1);
                }

                // SubImageIndex (있으면)
                float SubImageIndex = 0.0f;
                if (Src->SubUVModule && Src->SubUVPayloadOffset >= 0)
                {
                    const uint8* ParticleBase = reinterpret_cast<const uint8*>(Particle);
                    SubImageIndex = *reinterpret_cast<const float*>(ParticleBase + Src->SubUVPayloadOffset);
                }

                // Top vertex (U = 0)
                FParticleSpriteVertex& V0 = Vertices[VertexCursor++];
                V0.Position = ParticleWorldPos + (RightVector * WidthScale * 0.5f);
                V0.Corner = FVector2D(0.0f, V_Coord);
                V0.Color = Color;
                V0.Size = FVector2D(WidthScale, 0.0f);
                V0.Rotation = 0.0f;
                V0.SubImageIndex = SubImageIndex;

                // Bottom vertex (U = 1)
                FParticleSpriteVertex& V1 = Vertices[VertexCursor++];
                V1.Position = ParticleWorldPos - (RightVector * WidthScale * 0.5f);
                V1.Corner = FVector2D(1.0f, V_Coord);
                V1.Color = Color;
                V1.Size = FVector2D(WidthScale, 0.0f);
                V1.Rotation = 0.0f;
                V1.SubImageIndex = SubImageIndex;
            }

            // 4-3) 체인 기반 인덱스 생성
            for (int32 ci = 0; ci < Chain.Num() - 1; ++ci)
            {
                const uint32 V_TopLeft = BaseVertexIndex + (ci * 2);
                const uint32 V_BottomLeft = BaseVertexIndex + (ci * 2) + 1;
                const uint32 V_TopRight = BaseVertexIndex + ((ci + 1) * 2);
                const uint32 V_BottomRight = BaseVertexIndex + ((ci + 1) * 2) + 1;

                // 삼각형 1 (TL - TR - BL)
                IndicesPtr[IndexCursor++] = V_TopLeft;
                IndicesPtr[IndexCursor++] = V_TopRight;
                IndicesPtr[IndexCursor++] = V_BottomLeft;

                // 삼각형 2 (BL - TR - BR)
                IndicesPtr[IndexCursor++] = V_BottomLeft;
                IndicesPtr[IndexCursor++] = V_TopRight;
                IndicesPtr[IndexCursor++] = V_BottomRight;
            }

            // trail 하나가 만든 index 수는 (Chain.Num() - 1) * 6
            const uint32 TrailIndexCount = (Chain.Num() - 1) * 6;

            FRibbonBatchCommand Cmd;
            Cmd.RibbonData = RibbonData;
            Cmd.StartIndex = BaseIndexIndex;
            Cmd.IndexCount = TrailIndexCount;
            Cmd.SortPriority = 0;
            RibbonCommands.Add(Cmd);
        }

        // emitter 단위로 묶고 싶으면 위에서 trail별이 아니라
        // EmitterBaseIndexStart / (IndexCursor - EmitterBaseIndexStart) 기반으로
        // 한 번만 Cmd를 추가해도 된다.
        (void)EmitterBaseIndexStart;
    }

    Context->Unmap(RibbonVertexBuffer, 0);
    Context->Unmap(RibbonIndexBuffer, 0);

    if (RibbonCommands.IsEmpty())
        return;

    // 5) 리본 전용 쉐이더 준비
    static UMaterial* RibbonMaterial = nullptr;
    if (!RibbonMaterial)
    {
        RibbonMaterial = UResourceManager::GetInstance().Load<UMaterial>("Shaders/Effects/ParticleRibbon.hlsl");
    }
    if (!RibbonMaterial || !RibbonMaterial->GetShader())
        return;

    TArray<FShaderMacro> ShaderMacros = View ? View->ViewShaderMacros : TArray<FShaderMacro>();
    ShaderMacros.Append(RibbonMaterial->GetShaderMacros());

    FShaderVariant* ShaderVariant = RibbonMaterial->GetShader()->GetOrCompileShaderVariant(ShaderMacros);
    if (!ShaderVariant)
        return;

    // 6) Batch 생성 (trail 단위로 draw call)
    for (const FRibbonBatchCommand& Cmd : RibbonCommands)
    {
        if (!Cmd.RibbonData || Cmd.IndexCount == 0)
            continue;

        FMeshBatchElement& Batch = OutMeshBatchElements[OutMeshBatchElements.Add(FMeshBatchElement())];

        Batch.VertexShader = ShaderVariant->VertexShader;
        Batch.PixelShader = ShaderVariant->PixelShader;
        Batch.InputLayout = ShaderVariant->InputLayout;
        Batch.Material = ResolveEmitterMaterial(*Cmd.RibbonData);

        Batch.VertexBuffer = RibbonVertexBuffer;
        Batch.IndexBuffer = RibbonIndexBuffer;
        Batch.VertexStride = sizeof(FParticleSpriteVertex);
        Batch.IndexCount = Cmd.IndexCount;
        Batch.StartIndex = Cmd.StartIndex;
        Batch.BaseVertexIndex = 0;
        Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        Batch.WorldMatrix = FMatrix::Identity(); // 리본은 월드 위치로 작성
        Batch.ObjectID = InternalIndex;
        Batch.SortPriority = Cmd.SortPriority;
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

		for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
		{
		    const int32 ParticleIdx = Base->AsyncSortedIndices[LocalIdx];
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

		for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
		{
			if (WrittenInstances >= ClampedCount)
			{
				break;
			}
		    const int32 ParticleIdx = Base->AsyncSortedIndices[LocalIdx];
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

bool UParticleSystemComponent::EnsureRibbonBuffers(uint32 MaxSpinePoints)
{
    if (MaxSpinePoints < 2) return false;

    uint32 RequiredVertexCount = MaxSpinePoints * 2;
    uint32 RequiredIndexCount = (MaxSpinePoints - 1) * 6;

    if (RibbonVertexBuffer && RibbonIndexBuffer &&
        RequiredVertexCount <= RibbonVertexCapacity &&
        RequiredIndexCount <= RibbonIndexCapacity)
    {
        return true;
    }

    RibbonVertexCapacity = RequiredVertexCount;
    RibbonIndexCapacity = RequiredIndexCount;

    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11Device* Device = RHIDevice ? RHIDevice->GetDevice() : nullptr;
    if (!Device) return false;

    // --------------------------------------------------------
    // A. 리본 버텍스 버퍼 생성 (DYNAMIC)
    // --------------------------------------------------------
    D3D11_BUFFER_DESC VertexDesc = {};
    VertexDesc.ByteWidth = RibbonVertexCapacity * sizeof(FParticleSpriteVertex);
    VertexDesc.Usage = D3D11_USAGE_DYNAMIC;        // CPU Write
    VertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    VertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(Device->CreateBuffer(&VertexDesc, nullptr, &RibbonVertexBuffer)))
    {
        ReleaseRibbonBuffers();
        return false;
    }

    // --------------------------------------------------------
    // B. 리본 인덱스 버퍼 생성 (DYNAMIC) - 중요!
    // 스프라이트와 달리 리본은 인덱스 패턴이 가변적이므로 Dynamic이어야 함
    // --------------------------------------------------------
    D3D11_BUFFER_DESC IndexDesc = {};
    IndexDesc.ByteWidth = RibbonIndexCapacity * sizeof(uint32);
    IndexDesc.Usage = D3D11_USAGE_DYNAMIC;         // CPU Write (매 프레임 갱신)
    IndexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    IndexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(Device->CreateBuffer(&IndexDesc, nullptr, &RibbonIndexBuffer)))
    {
        ReleaseRibbonBuffers();
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

void UParticleSystemComponent::ReleaseRibbonBuffers()
{
    if (RibbonVertexBuffer)
    {
        RibbonVertexBuffer->Release();
        RibbonVertexBuffer = nullptr;
    }
    if (RibbonIndexBuffer)
    {
        RibbonIndexBuffer->Release();
        RibbonIndexBuffer = nullptr;
    }

    RibbonVertexCapacity = 0;
    RibbonIndexCapacity = 0;
}

void UParticleSystemComponent::ReleaseBeamBuffers()
{
    for (ID3D11Buffer* Buffer : PerFrameBeamBuffers)
    {
        if (Buffer)
        {
            Buffer->Release();
            Buffer = nullptr;
        }
    }
    PerFrameBeamBuffers.clear();
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

void UParticleSystemComponent::BuildBeamParticleBatch(TArray<FDynamicEmitterDataBase*>& EmitterRenderData, TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    if (EmitterRenderData.IsEmpty() || !View) return;

    D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
    ID3D11Device* Device = RHIDevice ? RHIDevice->GetDevice() : nullptr;
    if (!Device) return;

    ReleaseBeamBuffers();
    
    const FVector ViewOrigin = View->ViewLocation;

    // [중요] 이미터 단위로 루프를 돕니다. (이미터마다 재질이 다를 수 있으므로)
    for (FDynamicEmitterDataBase* Base : EmitterRenderData)
    {
        if (!Base || Base->EmitterType != EParticleType::Beam) continue;

        auto* BeamData = static_cast<FDynamicBeamEmitterData*>(Base);
        const auto* Src = static_cast<const FDynamicBeamEmitterReplayData*>(BeamData->GetSource());
        if (!Src || Src->ActiveParticleCount <= 0) continue;

        // -------------------------------------------------------
        // 1. 이 이미터(BeamData)에 속한 모든 파티클을 하나로 합칩니다.
        // -------------------------------------------------------
        TArray<FParticleBeamVertex> EmitterVertices;
        TArray<uint32> EmitterIndices;

        const int32 TessellationFactor = Src->TessellationFactor;
        const float BeamWidth = 1.0f; 

        // 파티클 루프 (Accumulate)
        for (int32 LocalIdx = 0; LocalIdx < Src->ActiveParticleCount; ++LocalIdx)
        {
            const FBaseParticle* Particle = BeamData->GetParticle(LocalIdx);
            if (!Particle) continue;

            // ... (Payload 해석 및 위치 계산 코드는 기존과 동일) ...
            const uint8* ParticleBase = reinterpret_cast<const uint8*>(Particle);
            const int32 PayloadOffset = sizeof(FBaseParticle);
            const FVector* BeamSource = reinterpret_cast<const FVector*>(ParticleBase + PayloadOffset);
            const FVector* BeamTarget = reinterpret_cast<const FVector*>(ParticleBase + PayloadOffset + sizeof(FVector));
            const float* BeamRandomSeed = reinterpret_cast<const float*>(ParticleBase + PayloadOffset + sizeof(FVector) * 2);

            FVector Start = *BeamSource;
            FVector End = *BeamTarget;
            float RandomSeed = *BeamRandomSeed;

            if (BeamData->bUseLocalSpace)
            {
                const FMatrix& WorldMatrix = GetWorldMatrix();
                Start = WorldMatrix.TransformPosition(Start);
                End = WorldMatrix.TransformPosition(End);
            }

            FVector BeamDir = (End - Start).GetSafeNormal();
            if (BeamDir.IsZero()) continue;

            FVector ToCam = (ViewOrigin - Start).GetSafeNormal();
            FVector Right = FVector::Cross(BeamDir, ToCam).GetSafeNormal();
            if (Right.IsZero())
            {
                Right = FVector::Cross(BeamDir, FVector(0, 0, 1)).GetSafeNormal();
                if (Right.IsZero()) Right = FVector::Cross(BeamDir, FVector(0, 1, 0)).GetSafeNormal();
            }
            FVector Up = FVector::Cross(Right, BeamDir).GetSafeNormal();

            // [인덱스 오프셋] 현재까지 쌓인 정점 개수를 더해줘야 합니다.
            uint32 BaseVertexIndex = EmitterVertices.Num();
            const uint32 SegmentCount = TessellationFactor;

            // 정점 생성
            for (uint32 i = 0; i <= SegmentCount; ++i)
            {
                float t = (float)i / SegmentCount;
                FVector Position = FVector::Lerp(Start, End, t);

                if (i > 0 && i < SegmentCount && Src->NoiseAmplitude > 0.0f)
                {
                    float NoisePhase = t * Src->NoiseFrequency * 6.28318f + RandomSeed;
                    float NoiseValue1 = sin(NoisePhase) * 0.5f + sin(NoisePhase * 2.3f) * 0.3f;
                    float NoiseValue2 = cos(NoisePhase * 1.3f) * 0.5f + cos(NoisePhase * 3.7f) * 0.3f;
                    float FallOff = sin(t * 3.14159f);
                    Position += Right * NoiseValue1 * Src->NoiseAmplitude * FallOff;
                    Position += Up * NoiseValue2 * Src->NoiseAmplitude * FallOff;
                }

                FVector LeftPos = Position - Right * BeamWidth * 0.5f;
                FVector RightPos = Position + Right * BeamWidth * 0.5f;

                FParticleBeamVertex V1, V2;
                V1.Position = LeftPos;  V1.UV = FVector2D(0.0f, t); V1.Color = Particle->Color;
                V2.Position = RightPos; V2.UV = FVector2D(1.0f, t); V2.Color = Particle->Color;

                EmitterVertices.Add(V1);
                EmitterVertices.Add(V2);
            }

            // 인덱스 생성
            for (uint32 i = 0; i < SegmentCount; ++i)
            {
                uint32 LocalBase = BaseVertexIndex + (i * 2);
                EmitterIndices.Add(LocalBase + 0); EmitterIndices.Add(LocalBase + 2); EmitterIndices.Add(LocalBase + 1);
                EmitterIndices.Add(LocalBase + 1); EmitterIndices.Add(LocalBase + 2); EmitterIndices.Add(LocalBase + 3);
            }
        } // End Particle Loop

        if (EmitterVertices.IsEmpty()) continue;

        // -------------------------------------------------------
        // 2. 버퍼 생성 (이미터마다 별도로 생성해야 안전함)
        // -------------------------------------------------------
        ID3D11Buffer* CurrentVB;
        ID3D11Buffer* CurrentIB;

        {
            D3D11_BUFFER_DESC VBDesc = {};
            VBDesc.ByteWidth = EmitterVertices.Num() * sizeof(FParticleBeamVertex);
            VBDesc.Usage = D3D11_USAGE_IMMUTABLE; // 이번 프레임에 쓰고 버릴 거면 IMMUTABLE도 괜찮음 (Map 불필요시)
            VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA VBData = { EmitterVertices.GetData(), 0, 0 };
            if (FAILED(Device->CreateBuffer(&VBDesc, &VBData, &CurrentVB))) continue;

            D3D11_BUFFER_DESC IBDesc = {};
            IBDesc.ByteWidth = EmitterIndices.Num() * sizeof(uint32);
            IBDesc.Usage = D3D11_USAGE_IMMUTABLE;
            IBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA IBData = { EmitterIndices.GetData(), 0, 0 };
            if (FAILED(Device->CreateBuffer(&IBDesc, &IBData, &CurrentIB))) continue;
        }
        PerFrameBeamBuffers.Add(CurrentVB);
        PerFrameBeamBuffers.Add(CurrentIB);
        
        // -------------------------------------------------------
        // 3. 셰이더 및 배치 생성 (이 이미터의 재질 사용)
        // -------------------------------------------------------
        // TODO: 실제로는 각 빔마다 다른 텍스처/재질을 쓸 수 있으므로 여기서 로드
        UMaterial* BeamShaderMaterial = RESOURCE.Load<UMaterial>("Shaders/Effects/ParticleBeam.hlsl");
        
        TArray<FShaderMacro> ShaderMacros;
        if (View) ShaderMacros = View->ViewShaderMacros;
        ShaderMacros.Append(BeamShaderMaterial->GetShaderMacros());

        FShaderVariant* ShaderVariant = BeamShaderMaterial->GetShader()->GetOrCompileShaderVariant(ShaderMacros);
        if (!ShaderVariant) continue;

        FMeshBatchElement& Batch = OutMeshBatchElements[OutMeshBatchElements.Add(FMeshBatchElement())];
        
        // [핵심] 여기서 현재 이미터(BeamData)의 재질을 적용합니다.
        Batch.Material = ResolveEmitterMaterial(*BeamData); 

        Batch.VertexShader = ShaderVariant->VertexShader;
        Batch.PixelShader = ShaderVariant->PixelShader;
        Batch.InputLayout = ShaderVariant->InputLayout;
        Batch.VertexBuffer = CurrentVB;
        Batch.IndexBuffer = CurrentIB;
        Batch.VertexStride = sizeof(FParticleBeamVertex);
        Batch.IndexCount = EmitterIndices.Num();
        Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        Batch.WorldMatrix = FMatrix::Identity();
        Batch.SortPriority = BeamData->SortPriority;

    } // End Emitter Loop
}