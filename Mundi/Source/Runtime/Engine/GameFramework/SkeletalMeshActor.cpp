#include "pch.h"
#include "SkeletalMeshActor.h"
#include "World.h"
#include "Source/Runtime/Engine/Physics/PhysicsAsset.h"
#include "Source/Runtime/Engine/Physics/BodySetup.h"
#include "Source/Runtime/Engine/Physics/FKShapeElem.h"

ASkeletalMeshActor::ASkeletalMeshActor()
{
    ObjectName = "Skeletal Mesh Actor";

    // 스킨드 메시 렌더용 컴포넌트 생성 및 루트로 설정
    // - 프리뷰 장면에서 메시를 표시하는 실제 렌더링 컴포넌트
    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
    RootComponent = SkeletalMeshComponent;
}

ASkeletalMeshActor::~ASkeletalMeshActor() = default;

void ASkeletalMeshActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

FAABB ASkeletalMeshActor::GetBounds() const
{
    // Be robust to component replacement: query current root
    if (auto* Current = Cast<USkeletalMeshComponent>(RootComponent))
    {
        return Current->GetWorldAABB();
    }
    return FAABB();
}

void ASkeletalMeshActor::SetSkeletalMeshComponent(USkeletalMeshComponent* InComp)
{
    SkeletalMeshComponent = InComp;
}

void ASkeletalMeshActor::SetSkeletalMesh(const FString& PathFileName)
{
    if (SkeletalMeshComponent)
    {
        SkeletalMeshComponent->SetSkeletalMesh(PathFileName);
    }
}

void ASkeletalMeshActor::EnsureViewerComponents()
{
    // Only create viewer components if they don't exist and we're in a preview world
    if (BoneLineComponent && BoneAnchor && BodyLineComponent && ConstraintLineComponent)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || !World->IsPreviewWorld())
    {
        return;
    }

    // Create bone line component for skeleton visualization
    if (!BoneLineComponent)
    {
        BoneLineComponent = NewObject<ULineComponent>();
        if (BoneLineComponent && RootComponent)
        {
            BoneLineComponent->ObjectName = "BoneLines";
            BoneLineComponent->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
            BoneLineComponent->SetAlwaysOnTop(true);
            AddOwnedComponent(BoneLineComponent);
            BoneLineComponent->RegisterComponent(World);
        }
    }

    // Create body line component for physics body visualization
    if (!BodyLineComponent)
    {
        BodyLineComponent = NewObject<ULineComponent>();
        if (BodyLineComponent && RootComponent)
        {
            BodyLineComponent->ObjectName = "BodyLines";
            BodyLineComponent->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
            BodyLineComponent->SetAlwaysOnTop(true);
            AddOwnedComponent(BodyLineComponent);
            BodyLineComponent->RegisterComponent(World);
        }
    }

    // Create constraint line component for physics constraint visualization
    if (!ConstraintLineComponent)
    {
        ConstraintLineComponent = NewObject<ULineComponent>();
        if (ConstraintLineComponent && RootComponent)
        {
            ConstraintLineComponent->ObjectName = "ConstraintLines";
            ConstraintLineComponent->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
            ConstraintLineComponent->SetAlwaysOnTop(true);
            AddOwnedComponent(ConstraintLineComponent);
            ConstraintLineComponent->RegisterComponent(World);
        }
    }

    // Create bone anchor for gizmo placement
    if (!BoneAnchor)
    {
        BoneAnchor = NewObject<UBoneAnchorComponent>();
        if (BoneAnchor && RootComponent)
        {
            BoneAnchor->ObjectName = "BoneAnchor";
            BoneAnchor->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
            BoneAnchor->SetVisibility(false);
            AddOwnedComponent(BoneAnchor);
            BoneAnchor->RegisterComponent(World);
        }
    }
}

void ASkeletalMeshActor::RebuildBoneLines(int32 SelectedBoneIndex)
{
    // Ensure viewer components exist before using them
    EnsureViewerComponents();

    if (!BoneLineComponent || !SkeletalMeshComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        return;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }

    const auto& Bones = Data->Skeleton.Bones;
    const int32 BoneCount = static_cast<int32>(Bones.size());
    if (BoneCount <= 0)
    {
        return;
    }

    // Initialize cache once per mesh
    if (!bBoneLinesInitialized || BoneLinesCache.Num() != BoneCount)
    {
        BoneLineComponent->ClearLines();
        BuildBoneLinesCache();
        bBoneLinesInitialized = true;
        CachedSelected = -1;
    }

    // Update selection highlight only when changed
    if (CachedSelected != SelectedBoneIndex)
    {
        UpdateBoneSelectionHighlight(SelectedBoneIndex);
        CachedSelected = SelectedBoneIndex;
    }

    // Update transforms only for the selected bone subtree
    // if (SelectedBoneIndex >= 0 && SelectedBoneIndex < BoneCount)
    // {
    //     UpdateBoneSubtreeTransforms(SelectedBoneIndex);
    // }

    /* @note 애니메이션이 적용되었을 경우 본 전체를 갱신해주어야 함 */
    for (int32 i = 0; i < BoneCount; ++i)
    {
        if (Bones[i].ParentIndex < 0)
        {
            UpdateBoneSubtreeTransforms(i);
        }
    }
}

void ASkeletalMeshActor::RepositionAnchorToBone(int32 BoneIndex)
{
    // Ensure viewer components exist before using them
    EnsureViewerComponents();

    if (!SkeletalMeshComponent || !BoneAnchor)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        return;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }

    const auto& Bones = Data->Skeleton.Bones;
    if (BoneIndex < 0 || BoneIndex >= (int32)Bones.size())
    {
        return;
    }

    // Wire target/index first, then place anchor without writeback
    BoneAnchor->SetTarget(SkeletalMeshComponent, BoneIndex);

    BoneAnchor->SetEditability(true);
    BoneAnchor->SetVisibility(true);
}

void ASkeletalMeshActor::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    // Find skeletal mesh component (always exists)
    for (UActorComponent* Component : OwnedComponents)
    {
        if (auto* Comp = Cast<USkeletalMeshComponent>(Component))
        {
            SkeletalMeshComponent = Comp;
            break;
        }
    }

    // Find viewer components (may not exist if not in preview world)
    for (UActorComponent* Component : OwnedComponents)
    {
        if (auto* Comp = Cast<ULineComponent>(Component))
        {
            if (Comp->ObjectName == FName("BoneLines"))
            {
                BoneLineComponent = Comp;
            }
            else if (Comp->ObjectName == FName("BodyLines"))
            {
                BodyLineComponent = Comp;
            }
            else if (Comp->ObjectName == FName("ConstraintLines"))
            {
                ConstraintLineComponent = Comp;
        }
        }
        else if (auto* Comp = Cast<UBoneAnchorComponent>(Component))
        {
            BoneAnchor = Comp;
        }
    }
}

void ASkeletalMeshActor::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        SkeletalMeshComponent = Cast<USkeletalMeshComponent>(RootComponent);
    }
}

void ASkeletalMeshActor::BuildBoneLinesCache()
{
    if (!SkeletalMeshComponent || !BoneLineComponent)
    {
        return;
    }
    
    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        return;
    }
    
    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }

    const auto& Bones = Data->Skeleton.Bones;
    const int32 BoneCount = static_cast<int32>(Bones.size());

    BoneLinesCache.Empty();
    BoneLinesCache.resize(BoneCount);
    BoneChildren.Empty();
    BoneChildren.resize(BoneCount);

    for (int32 i = 0; i < BoneCount; ++i)
    {
        const int32 p = Bones[i].ParentIndex;
        if (p >= 0 && p < BoneCount)
        {
            BoneChildren[p].Add(i);
        }
    }

    // Initial centers from current bone transforms (object space)
    // Use GetBoneWorldTransform to properly accumulate parent transforms
    const FMatrix WorldInv = GetWorldMatrix().InverseAffine();
    TArray<FVector> JointPos;
    JointPos.resize(BoneCount);

    for (int32 i = 0; i < BoneCount; ++i)
    {
        const FMatrix W = SkeletalMeshComponent->GetBoneWorldTransform(i).ToMatrix();
        const FMatrix O = W * WorldInv;
        JointPos[i] = FVector(O.M[3][0], O.M[3][1], O.M[3][2]);
    }

    const int NumSegments = CachedSegments;
    
    for (int32 i = 0; i < BoneCount; ++i)
    {
        FBoneDebugLines& BL = BoneLinesCache[i];
        const FVector Center = JointPos[i];
        const int32 parent = Bones[i].ParentIndex;
        
        if (parent >= 0)
        {
            const FVector ParentPos = JointPos[parent];
            const FVector ChildPos = Center;
            const FVector BoneDir = (ChildPos - ParentPos).GetNormalized();

            // Calculate perpendicular vectors for the cone base
            // Find a vector not parallel to BoneDir
            FVector Up = FVector(0, 0, 1);
            if (std::abs(FVector::Dot(Up, BoneDir)) > 0.99f)
            {
                Up = FVector(0, 1, 0); // Use Y if bone is too aligned with Z
            }

            FVector Right = FVector::Cross(BoneDir, Up).GetNormalized();
            FVector Forward = FVector::Cross(Right, BoneDir).GetNormalized();

            // Scale cone radius based on bone length
            const float BoneLength = (ChildPos - ParentPos).Size();
            const float Radius = std::min(BoneBaseRadius, BoneLength * 0.15f);

            // Create cone geometry
            BL.ConeEdges.reserve(NumSegments);
            BL.ConeBase.reserve(NumSegments);

            for (int k = 0; k < NumSegments; ++k)
            {
                const float angle0 = (static_cast<float>(k) / NumSegments) * TWO_PI;
                const float angle1 = (static_cast<float>((k + 1) % NumSegments) / NumSegments) * TWO_PI;

                // Base circle vertices at parent
                const FVector BaseVertex0 = ParentPos + Right * (Radius * std::cos(angle0)) + Forward * (Radius * std::sin(angle0));
                const FVector BaseVertex1 = ParentPos + Right * (Radius * std::cos(angle1)) + Forward * (Radius * std::sin(angle1));

                // Cone edge from base vertex to tip (child)
                BL.ConeEdges.Add(BoneLineComponent->AddLine(BaseVertex0, ChildPos, FVector4(0, 1, 0, 1)));

                // Base circle edge
                BL.ConeBase.Add(BoneLineComponent->AddLine(BaseVertex0, BaseVertex1, FVector4(0, 1, 0, 1)));
            }
        }

        // Joint sphere visualization (3 orthogonal rings)
        BL.Rings.reserve(NumSegments*3);

        for (int k = 0; k < NumSegments; ++k)
        {
            const float a0 = (static_cast<float>(k) / NumSegments) * TWO_PI;
            const float a1 = (static_cast<float>((k + 1) % NumSegments) / NumSegments) * TWO_PI;
            BL.Rings.Add(BoneLineComponent->AddLine(
                Center + FVector(BoneJointRadius * std::cos(a0), BoneJointRadius * std::sin(a0), 0.0f),
                Center + FVector(BoneJointRadius * std::cos(a1), BoneJointRadius * std::sin(a1), 0.0f),
                FVector4(0.8f,0.8f,0.8f,1.0f)));
            BL.Rings.Add(BoneLineComponent->AddLine(
                Center + FVector(BoneJointRadius * std::cos(a0), 0.0f, BoneJointRadius * std::sin(a0)),
                Center + FVector(BoneJointRadius * std::cos(a1), 0.0f, BoneJointRadius * std::sin(a1)),
                FVector4(0.8f,0.8f,0.8f,1.0f)));
            BL.Rings.Add(BoneLineComponent->AddLine(
                Center + FVector(0.0f, BoneJointRadius * std::cos(a0), BoneJointRadius * std::sin(a0)),
                Center + FVector(0.0f, BoneJointRadius * std::cos(a1), BoneJointRadius * std::sin(a1)),
                FVector4(0.8f,0.8f,0.8f,1.0f)));
        }
    }
}

void ASkeletalMeshActor::UpdateBoneSelectionHighlight(int32 SelectedBoneIndex)
{
    if (!SkeletalMeshComponent)
    {
        return;
    }
    
    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        return;
    }
    
    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }
    
    const auto& Bones = Data->Skeleton.Bones;
    const int32 BoneCount = static_cast<int32>(Bones.size());

    const FVector4 SelRing(1.0f, 0.85f, 0.2f, 1.0f);
    const FVector4 NormalRing(0.8f, 0.8f, 0.8f, 1.0f);
    const FVector4 SelCone(1.0f, 0.0f, 0.0f, 1.0f);      // Red for selected bone cone
    const FVector4 NormalCone(0.0f, 1.0f, 0.0f, 1.0f);   // Green for normal bone cone

    for (int32 i = 0; i < BoneCount; ++i)
    {
        const bool bSelected = (i == SelectedBoneIndex);
        const FVector4 RingColor = bSelected ? SelRing : NormalRing;
        FBoneDebugLines& BL = BoneLinesCache[i];

        // Update joint ring colors
        for (ULine* L : BL.Rings)
        {
            if (L) L->SetColor(RingColor);
        }

        // Update cone colors
        const int32 parent = Bones[i].ParentIndex;
        const bool bConeSelected = (i == SelectedBoneIndex || parent == SelectedBoneIndex);
        const FVector4 ConeColor = bConeSelected ? SelCone : NormalCone;

        for (ULine* L : BL.ConeEdges)
        {
            if (L) L->SetColor(ConeColor);
        }

        for (ULine* L : BL.ConeBase)
        {
            if (L) L->SetColor(ConeColor);
        }
    }
}

void ASkeletalMeshActor::UpdateBodySelectionHighlight(int32 SelectedBodyIndex)
{
    if (!SkeletalMeshComponent || !BodyLineComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
    if (!SkeletalMesh || !PhysicsAsset)
    {
        return;
    }

    const int32 BodyCount = PhysicsAsset->BodySetups.Num();

    const FVector4 SelectedColor(1.0f, 0.0f, 0.0f, 1.0f);   // Red for selected body
    const FVector4 NormalColor(0.2f, 0.5f, 1.0f, 0.7f);     // Blue for normal body

    for (int32 i = 0; i < BodyCount && i < BodyLinesCache.Num(); ++i)
    {
        const bool bSelected = (i == SelectedBodyIndex);
        const FVector4 Color = bSelected ? SelectedColor : NormalColor;
        FBodyDebugLines& BDL = BodyLinesCache[i];

        // Update sphere line colors
        for (ULine* L : BDL.SphereLines)
        {
            if (L) L->SetColor(Color);
        }

        // Update box line colors
        for (ULine* L : BDL.BoxLines)
        {
            if (L) L->SetColor(Color);
        }

        // Update capsule line colors
        for (ULine* L : BDL.CapsuleLines)
        {
            if (L) L->SetColor(Color);
        }

        // Update convex line colors
        for (ULine* L : BDL.ConvexLines)
        {
            if (L) L->SetColor(Color);
        }
    }
}

void ASkeletalMeshActor::UpdateBoneSubtreeTransforms(int32 BoneIndex)
{
    if (!SkeletalMeshComponent)
    {
        return;
    }
    
    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        return;
    }
    
    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }
    
    const auto& Bones = Data->Skeleton.Bones;
    const int32 BoneCount = static_cast<int32>(Bones.size());
    if (BoneIndex < 0 || BoneIndex >= BoneCount)
    {
        return;
    }

    TArray<int32> Stack; Stack.Add(BoneIndex);
    TArray<int32> ToUpdate;
    while (!Stack.IsEmpty())
    {
        int32 b = Stack.Last(); Stack.Pop();
        ToUpdate.Add(b);
        for (int32 c : BoneChildren[b]) Stack.Add(c);
    }

    const FMatrix WorldInv = GetWorldMatrix().InverseAffine();
    TArray<FVector> Centers; Centers.resize(BoneCount);
    
    for (int32 b : ToUpdate)
    {
        const FMatrix W = SkeletalMeshComponent->GetBoneWorldTransform(b).ToMatrix();
        const FMatrix O = W * WorldInv;
        Centers[b] = FVector(O.M[3][0], O.M[3][1], O.M[3][2]);
    }

    const int NumSegments = CachedSegments;

    for (int32 b : ToUpdate)
    {
        FBoneDebugLines& BL = BoneLinesCache[b];
        const int32 parent = Bones[b].ParentIndex;

        // Update cone geometry
        if (parent >= 0 && !BL.ConeEdges.IsEmpty())
        {
            const FMatrix ParentW = SkeletalMeshComponent->GetBoneWorldTransform(parent).ToMatrix();
            const FMatrix ParentO = ParentW * WorldInv;
            const FVector ParentPos(ParentO.M[3][0], ParentO.M[3][1], ParentO.M[3][2]);
            const FVector ChildPos = Centers[b];

            const FVector BoneDir = (ChildPos - ParentPos).GetNormalized();

            // Calculate perpendicular vectors for the cone base
            FVector Up = FVector(0, 0, 1);
            if (std::abs(FVector::Dot(Up, BoneDir)) > 0.99f)
            {
                Up = FVector(0, 1, 0);
            }

            FVector Right = FVector::Cross(BoneDir, Up).GetNormalized();
            FVector Forward = FVector::Cross(Right, BoneDir).GetNormalized();

            // Scale cone radius based on bone length
            const float BoneLength = (ChildPos - ParentPos).Size();
            const float Radius = std::min(BoneBaseRadius, BoneLength * 0.15f);

            // Update cone lines
            for (int k = 0; k < NumSegments && k < BL.ConeEdges.Num(); ++k)
            {
                const float angle0 = (static_cast<float>(k) / NumSegments) * TWO_PI;
                const float angle1 = (static_cast<float>((k + 1) % NumSegments) / NumSegments) * TWO_PI;

                const FVector BaseVertex0 = ParentPos + Right * (Radius * std::cos(angle0)) + Forward * (Radius * std::sin(angle0));
                const FVector BaseVertex1 = ParentPos + Right * (Radius * std::cos(angle1)) + Forward * (Radius * std::sin(angle1));

                // Update cone edge
                if (BL.ConeEdges[k])
                {
                    BL.ConeEdges[k]->SetLine(BaseVertex0, ChildPos);
                }

                // Update base circle edge
                if (k < BL.ConeBase.Num() && BL.ConeBase[k])
                {
                    BL.ConeBase[k]->SetLine(BaseVertex0, BaseVertex1);
                }
            }
        }

        // Update joint rings
        const FVector Center = Centers[b];

        for (int k = 0; k < NumSegments; ++k)
        {
            const float a0 = (static_cast<float>(k) / NumSegments) * TWO_PI;
            const float a1 = (static_cast<float>((k + 1) % NumSegments) / NumSegments) * TWO_PI;
            const int base = k * 3;
            if (BL.Rings.IsEmpty() || base + 2 >= BL.Rings.Num()) break;
            BL.Rings[base+0]->SetLine(
                Center + FVector(BoneJointRadius * std::cos(a0), BoneJointRadius * std::sin(a0), 0.0f),
                Center + FVector(BoneJointRadius * std::cos(a1), BoneJointRadius * std::sin(a1), 0.0f));
            BL.Rings[base+1]->SetLine(
                Center + FVector(BoneJointRadius * std::cos(a0), 0.0f, BoneJointRadius * std::sin(a0)),
                Center + FVector(BoneJointRadius * std::cos(a1), 0.0f, BoneJointRadius * std::sin(a1)));
            BL.Rings[base+2]->SetLine(
                Center + FVector(0.0f, BoneJointRadius * std::cos(a0), BoneJointRadius * std::sin(a0)),
                Center + FVector(0.0f, BoneJointRadius * std::cos(a1), BoneJointRadius * std::sin(a1)));
        }
    }
}

int32 ASkeletalMeshActor::PickBone(const FRay& Ray, float& OutDistance) const
{
    if (!SkeletalMeshComponent)
    {
        return -1;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        return -1;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return -1;
    }

    const auto& Bones = Data->Skeleton.Bones;
    if (Bones.empty())
    {
        return -1;
    }

    int32 ClosestBoneIndex = -1;
    float ClosestDistance = FLT_MAX;

    // Test each bone with a bounding sphere
    for (int32 i = 0; i < (int32)Bones.size(); ++i)
    {
        // Get bone world transform
        FTransform BoneWorldTransform = SkeletalMeshComponent->GetBoneWorldTransform(i);
        FVector BoneWorldPos = BoneWorldTransform.Translation;

        // Use BoneJointRadius as picking radius (can be adjusted)
        float PickRadius = BoneJointRadius * 2.0f; // Slightly larger for easier picking

        // Test ray-sphere intersection
        float HitDistance;
        if (IntersectRaySphere(Ray, BoneWorldPos, PickRadius, HitDistance))
        {
            if (HitDistance < ClosestDistance)
            {
                ClosestDistance = HitDistance;
                ClosestBoneIndex = i;
            }
        }
    }

    if (ClosestBoneIndex >= 0)
    {
        OutDistance = ClosestDistance;
    }

    return ClosestBoneIndex;
}

void ASkeletalMeshActor::RebuildBodyLines(bool& bChangedGeomNum, int32 SelectedBodyIndex)
{
    // Ensure viewer components exist before using them
    EnsureViewerComponents();

    if (!BodyLineComponent || !SkeletalMeshComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
    if (!SkeletalMesh || !PhysicsAsset)
    {
        if (bBodyLinesInitialized)
        {
            BodyLineComponent->ClearLines();
            BodyLinesCache.Empty();
            bBodyLinesInitialized = false;
            CachedPhysicsAsset = nullptr;
        }
        return;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        if (bBodyLinesInitialized)
        {
            BodyLineComponent->ClearLines();
            BodyLinesCache.Empty();
            bBodyLinesInitialized = false;
            CachedPhysicsAsset = nullptr;
        }
        return;
    }

    // Initialize cache once per physics asset or rebuild if physics asset changed
    if (!bBodyLinesInitialized || CachedPhysicsAsset != PhysicsAsset || 
        BodyLinesCache.Num() != PhysicsAsset->BodySetups.Num() || bChangedGeomNum)
    {
        BodyLineComponent->ClearLines();
        BuildBodyLinesCache();
        bBodyLinesInitialized = true;
        CachedPhysicsAsset = PhysicsAsset;
        CachedSelectedBody = -1;
        
        bChangedGeomNum = false;
    }

    // Update selection highlight only when changed
    if (CachedSelectedBody != SelectedBodyIndex)
    {
        UpdateBodySelectionHighlight(SelectedBodyIndex);
        CachedSelectedBody = SelectedBodyIndex;
    }

	// Update transforms only(body들은 그대로고 트랜스폼만 바뀌는 경우)
    UpdateBodyTransforms();
}

void ASkeletalMeshActor::RebuildConstraintLines(int32 SelectedConstraintIndex)
{
    // Ensure viewer components exist before using them
    EnsureViewerComponents();

    if (!ConstraintLineComponent || !SkeletalMeshComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
	UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
    if (!SkeletalMesh || !PhysicsAsset)
    {
        if (bConstraintLinesInitialized)
        {
            ConstraintLineComponent->ClearLines();
            ConstraintLinesCache.Empty();
            bConstraintLinesInitialized = false;
        }
        return;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        if (bConstraintLinesInitialized)
        {
            ConstraintLineComponent->ClearLines();
            ConstraintLinesCache.Empty();
            bConstraintLinesInitialized = false;
        }
        return;
    }

    // Initialize cache once per physics asset or rebuild if physics asset changed
    if (!bConstraintLinesInitialized || CachedPhysicsAsset != PhysicsAsset || 
        ConstraintLinesCache.Num() != PhysicsAsset->Constraints.Num())
    {
        ConstraintLineComponent->ClearLines();
        BuildConstraintLinesCache();
        bConstraintLinesInitialized = true;
        CachedPhysicsAsset = PhysicsAsset;
        CachedSelectedConstraint = -1;
    }

    // Update selection highlight only when changed
    if (CachedSelectedConstraint != SelectedConstraintIndex)
    {
        UpdateConstraintSelectionHighlight(SelectedConstraintIndex);
        CachedSelectedConstraint = SelectedConstraintIndex;
    }

    // Update transforms for all constraints (they follow bone transforms)
    UpdateConstraintTransforms();
}

void ASkeletalMeshActor::BuildBodyLinesCache()
{
    if (!SkeletalMeshComponent || !BodyLineComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
    if (!SkeletalMesh || !PhysicsAsset)
    {
        return;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }

    const FSkeleton* Skeleton = &Data->Skeleton;

    // Blue color for physics bodies (semi-transparent)
    const FVector4 BodyColor(0.2f, 0.5f, 1.0f, 0.7f);
    constexpr int NumSegments = 16;

    BodyLinesCache.Empty();
    BodyLinesCache.resize(PhysicsAsset->BodySetups.Num());

    // Build lines for each body setup
    for (int32 BodyIdx = 0; BodyIdx < PhysicsAsset->BodySetups.Num(); ++BodyIdx)
    {
        UBodySetup* Body = PhysicsAsset->BodySetups[BodyIdx];
        if (!Body)
            continue;

        FBodyDebugLines& BDL = BodyLinesCache[BodyIdx];

        // Find the bone index for this body
        int32 BoneIndex = Skeleton->FindBoneIndex(Body->BoneName);
        if (BoneIndex == INDEX_NONE)
            continue;

        // Get bone transform in local (actor) space
        FTransform BoneWorldTransform = SkeletalMeshComponent->GetBoneWorldTransform(BoneIndex);
        FMatrix WorldInv = GetWorldMatrix().InverseAffine();
        FMatrix BoneLocalMatrix = BoneWorldTransform.ToMatrix() * WorldInv;
        FTransform BoneLocalTransform(BoneLocalMatrix);

        // Build Sphere Elements
        for (const FKSphereElem& Sphere : Body->AggGeom.SphereElements)
        {
            FVector LocalCenter = BoneLocalTransform.TransformPosition(Sphere.Center);
            float LocalRadius = Sphere.Radius * BoneLocalTransform.Scale3D.GetMaxValue();

            // Draw 3 orthogonal circles (XY, XZ, YZ planes)
            // XY plane
            for (int i = 0; i < NumSegments; ++i)
            {
                float angle1 = (float)i / NumSegments * 2.0f * PI;
                float angle2 = (float)(i + 1) / NumSegments * 2.0f * PI;

                FVector p1 = LocalCenter + FVector(LocalRadius * cosf(angle1), LocalRadius * sinf(angle1), 0.0f);
                FVector p2 = LocalCenter + FVector(LocalRadius * cosf(angle2), LocalRadius * sinf(angle2), 0.0f);

                BDL.SphereLines.Add(BodyLineComponent->AddLine(p1, p2, BodyColor));
            }

            // XZ plane
            for (int i = 0; i < NumSegments; ++i)
            {
                float angle1 = (float)i / NumSegments * 2.0f * PI;
                float angle2 = (float)(i + 1) / NumSegments * 2.0f * PI;

                FVector p1 = LocalCenter + FVector(LocalRadius * cosf(angle1), 0.0f, LocalRadius * sinf(angle1));
                FVector p2 = LocalCenter + FVector(LocalRadius * cosf(angle2), 0.0f, LocalRadius * sinf(angle2));

                BDL.SphereLines.Add(BodyLineComponent->AddLine(p1, p2, BodyColor));
            }

            // YZ plane
            for (int i = 0; i < NumSegments; ++i)
            {
                float angle1 = (float)i / NumSegments * 2.0f * PI;
                float angle2 = (float)(i + 1) / NumSegments * 2.0f * PI;

                FVector p1 = LocalCenter + FVector(0.0f, LocalRadius * cosf(angle1), LocalRadius * sinf(angle1));
                FVector p2 = LocalCenter + FVector(0.0f, LocalRadius * cosf(angle2), LocalRadius * sinf(angle2));

                BDL.SphereLines.Add(BodyLineComponent->AddLine(p1, p2, BodyColor));
            }
        }

        // Build Box Elements
        for (const FKBoxElem& Box : Body->AggGeom.BoxElements)
        {
            FVector LocalExtent = Box.Extents;
            FTransform ShapeLocalTransform(Box.Center, Box.Rotation, FVector::One());
            FTransform ShapeTransform = BoneLocalTransform.GetWorldTransform(ShapeLocalTransform);
            FTransform::RemoveScaling(ShapeTransform);

            // Box의 8개 꼭지점 (local space)
            FVector LocalCorners[8] = {
                {-LocalExtent.X, -LocalExtent.Y, -LocalExtent.Z}, {+LocalExtent.X, -LocalExtent.Y, -LocalExtent.Z},
                {-LocalExtent.X, +LocalExtent.Y, -LocalExtent.Z}, {+LocalExtent.X, +LocalExtent.Y, -LocalExtent.Z},
                {-LocalExtent.X, -LocalExtent.Y, +LocalExtent.Z}, {+LocalExtent.X, -LocalExtent.Y, +LocalExtent.Z},
                {-LocalExtent.X, +LocalExtent.Y, +LocalExtent.Z}, {+LocalExtent.X, +LocalExtent.Y, +LocalExtent.Z},
            };

            FVector Corners[8];
            for (int i = 0; i < 8; ++i)
            {
                Corners[i] = ShapeTransform.TransformPosition(LocalCorners[i]);
            }

            // 12개 모서리
            static const int Edges[12][2] = {
                {0,1},{1,3},{3,2},{2,0}, // bottom
                {4,5},{5,7},{7,6},{6,4}, // top
                {0,4},{1,5},{2,6},{3,7}  // verticals
            };

            for (int i = 0; i < 12; ++i)
            {
                BDL.BoxLines.Add(BodyLineComponent->AddLine(Corners[Edges[i][0]], Corners[Edges[i][1]], BodyColor));
            }
        }

        // Build Capsule Elements
        for (const FKCapsuleElem& Capsule : Body->AggGeom.CapsuleElements)
        {
            FTransform ShapeLocalTransform(Capsule.Center, Capsule.Rotation, FVector::One());
            FTransform ShapeTransform = BoneLocalTransform.GetWorldTransform(ShapeLocalTransform);

            const float Radius = Capsule.Radius;
            const float HalfHeightCylinder = Capsule.HalfLength;

            const FMatrix ShapeNoScale = FMatrix::FromTRS(
                ShapeTransform.Translation,
                ShapeTransform.Rotation,
                FVector(1.0f, 1.0f, 1.0f)
            );

            constexpr int NumOfSphereSlice = 4;
            constexpr int NumHemisphereSegments = 8;

            // Build top and bottom rings
            TArray<FVector> TopRingLocal;
            TArray<FVector> BottomRingLocal;
            TopRingLocal.Reserve(NumOfSphereSlice);
            BottomRingLocal.Reserve(NumOfSphereSlice);

            for (int i = 0; i < NumOfSphereSlice; ++i)
            {
                const float a0 = (static_cast<float>(i) / NumOfSphereSlice) * TWO_PI;
                const float x = Radius * std::sin(a0);
                const float y = Radius * std::cos(a0);
                TopRingLocal.Add(FVector(x, y, +HalfHeightCylinder));
                BottomRingLocal.Add(FVector(x, y, -HalfHeightCylinder));
            }

            // Top and bottom ring lines
            for (int i = 0; i < NumOfSphereSlice; ++i)
            {
                const int j = (i + 1) % NumOfSphereSlice;

                // Top ring
                FVector p1 = TopRingLocal[i] * ShapeNoScale;
                FVector p2 = TopRingLocal[j] * ShapeNoScale;
                BDL.CapsuleLines.Add(BodyLineComponent->AddLine(p1, p2, BodyColor));

                // Bottom ring
                p1 = BottomRingLocal[i] * ShapeNoScale;
                p2 = BottomRingLocal[j] * ShapeNoScale;
                BDL.CapsuleLines.Add(BodyLineComponent->AddLine(p1, p2, BodyColor));
            }

            // Vertical connecting lines
            for (int i = 0; i < NumOfSphereSlice; ++i)
            {
                FVector p1 = TopRingLocal[i] * ShapeNoScale;
                FVector p2 = BottomRingLocal[i] * ShapeNoScale;
                BDL.CapsuleLines.Add(BodyLineComponent->AddLine(p1, p2, BodyColor));
            }

            // Hemisphere arcs (top and bottom)
            auto AddHemisphereArcs = [&](float CenterZSign)
            {
                const float CenterZ = CenterZSign * HalfHeightCylinder;

                for (int i = 0; i < NumHemisphereSegments; ++i)
                {
                    const float t0 = (static_cast<float>(i) / NumHemisphereSegments) * PI;
                    const float t1 = (static_cast<float>(i + 1) / NumHemisphereSegments) * PI;

                    // XZ plane arc
                    FVector PlaneXZ0(Radius * std::cos(t0), 0.0f, CenterZ + CenterZSign * Radius * std::sin(t0));
                    FVector PlaneXZ1(Radius * std::cos(t1), 0.0f, CenterZ + CenterZSign * Radius * std::sin(t1));

                    BDL.CapsuleLines.Add(BodyLineComponent->AddLine(PlaneXZ0 * ShapeNoScale, PlaneXZ1 * ShapeNoScale, BodyColor));

                    // YZ plane arc
                    FVector PlaneYZ0(0.0f, Radius * std::cos(t0), CenterZ + CenterZSign * Radius * std::sin(t0));
                    FVector PlaneYZ1(0.0f, Radius * std::cos(t1), CenterZ + CenterZSign * Radius * std::sin(t1));

                    BDL.CapsuleLines.Add(BodyLineComponent->AddLine(PlaneYZ0 * ShapeNoScale, PlaneYZ1 * ShapeNoScale, BodyColor));
                }
            };

            AddHemisphereArcs(+1.0f); // Top hemisphere
            AddHemisphereArcs(-1.0f); // Bottom hemisphere
        }

        // Build Convex Elements (simplified as vertex connections)
        for (const FKConvexElem& Convex : Body->AggGeom.ConvexElements)
        {
            if (Convex.Vertices.IsEmpty())
                continue;

            // Simple line strip through all vertices
            for (int32 i = 0; i < Convex.Vertices.Num() - 1; ++i)
            {
                FVector p1 = BoneLocalTransform.TransformPosition(Convex.Vertices[i]);
                FVector p2 = BoneLocalTransform.TransformPosition(Convex.Vertices[i + 1]);
                BDL.ConvexLines.Add(BodyLineComponent->AddLine(p1, p2, BodyColor));
            }

            // Close the loop
            if (Convex.Vertices.Num() > 2)
            {
                FVector p1 = BoneLocalTransform.TransformPosition(Convex.Vertices[Convex.Vertices.Num() - 1]);
                FVector p2 = BoneLocalTransform.TransformPosition(Convex.Vertices[0]);
                BDL.ConvexLines.Add(BodyLineComponent->AddLine(p1, p2, BodyColor));
            }
        }
    }
}

void ASkeletalMeshActor::UpdateBodyTransforms()
{
    if (!SkeletalMeshComponent || !BodyLineComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
	UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
    if (!SkeletalMesh || !PhysicsAsset)
    {
        return;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }

    const FSkeleton* Skeleton = &Data->Skeleton;

    constexpr int NumSegments = 16;
    const FMatrix WorldInv = GetWorldMatrix().InverseAffine();

    // Update each body setup's lines
    for (int32 BodyIdx = 0; BodyIdx < PhysicsAsset->BodySetups.Num() && BodyIdx < BodyLinesCache.Num(); ++BodyIdx)
    {
        UBodySetup* Body = PhysicsAsset->BodySetups[BodyIdx];
        if (!Body)
            continue;

        FBodyDebugLines& BDL = BodyLinesCache[BodyIdx];

        // Find the bone index for this body
        int32 BoneIndex = Skeleton->FindBoneIndex(Body->BoneName);
        if (BoneIndex == INDEX_NONE)
            continue;

        // Get bone transform in local (actor) space
        FTransform BoneWorldTransform = SkeletalMeshComponent->GetBoneWorldTransform(BoneIndex);
        FMatrix BoneLocalMatrix = BoneWorldTransform.ToMatrix() * WorldInv;
        FTransform BoneLocalTransform(BoneLocalMatrix);

		// physicsX에서 body가 속한 공간의 "스케일"은 아마? 무시하기 때문에, 스케일을 1로 고정
		BoneLocalTransform.Scale3D = FVector::One(); // Ignore scale for transform updates

        int32 LineIndex = 0;

        // Update Sphere Elements
        for (const FKSphereElem& Sphere : Body->AggGeom.SphereElements)
        {
            FVector LocalCenter = BoneLocalTransform.TransformPosition(Sphere.Center);
            float LocalRadius = Sphere.Radius * BoneLocalTransform.Scale3D.GetMaxValue();

            // XY plane
            for (int i = 0; i < NumSegments && LineIndex < BDL.SphereLines.Num(); ++i, ++LineIndex)
            {
                float angle1 = (float)i / NumSegments * 2.0f * PI;
                float angle2 = (float)(i + 1) / NumSegments * 2.0f * PI;

                FVector p1 = LocalCenter + FVector(LocalRadius * cosf(angle1), LocalRadius * sinf(angle1), 0.0f);
                FVector p2 = LocalCenter + FVector(LocalRadius * cosf(angle2), LocalRadius * sinf(angle2), 0.0f);

                if (BDL.SphereLines[LineIndex])
                    BDL.SphereLines[LineIndex]->SetLine(p1, p2);
            }

            // XZ plane
            for (int i = 0; i < NumSegments && LineIndex < BDL.SphereLines.Num(); ++i, ++LineIndex)
            {
                float angle1 = (float)i / NumSegments * 2.0f * PI;
                float angle2 = (float)(i + 1) / NumSegments * 2.0f * PI;

                FVector p1 = LocalCenter + FVector(LocalRadius * cosf(angle1), 0.0f, LocalRadius * sinf(angle1));
                FVector p2 = LocalCenter + FVector(LocalRadius * cosf(angle2), 0.0f, LocalRadius * sinf(angle2));

                if (BDL.SphereLines[LineIndex])
                    BDL.SphereLines[LineIndex]->SetLine(p1, p2);
            }

            // YZ plane
            for (int i = 0; i < NumSegments && LineIndex < BDL.SphereLines.Num(); ++i, ++LineIndex)
            {
                float angle1 = (float)i / NumSegments * 2.0f * PI;
                float angle2 = (float)(i + 1) / NumSegments * 2.0f * PI;

                FVector p1 = LocalCenter + FVector(0.0f, LocalRadius * cosf(angle1), LocalRadius * sinf(angle1));
                FVector p2 = LocalCenter + FVector(0.0f, LocalRadius * cosf(angle2), LocalRadius * sinf(angle2));

                if (BDL.SphereLines[LineIndex])
                    BDL.SphereLines[LineIndex]->SetLine(p1, p2);
            }
        }

        LineIndex = 0;

        // Update Box Elements
        for (const FKBoxElem& Box : Body->AggGeom.BoxElements)
        {
            FVector LocalExtent = Box.Extents;
            FTransform ShapeLocalTransform(Box.Center, Box.Rotation, FVector::One());
            FTransform ShapeTransform = BoneLocalTransform.GetWorldTransform(ShapeLocalTransform);

            // Box의 8개 꼭지점 (local space)
            FVector LocalCorners[8] = {
                {-LocalExtent.X, -LocalExtent.Y, -LocalExtent.Z}, {+LocalExtent.X, -LocalExtent.Y, -LocalExtent.Z},
                {-LocalExtent.X, +LocalExtent.Y, -LocalExtent.Z}, {+LocalExtent.X, +LocalExtent.Y, -LocalExtent.Z},
                {-LocalExtent.X, -LocalExtent.Y, +LocalExtent.Z}, {+LocalExtent.X, -LocalExtent.Y, +LocalExtent.Z},
                {-LocalExtent.X, +LocalExtent.Y, +LocalExtent.Z}, {+LocalExtent.X, +LocalExtent.Y, +LocalExtent.Z},
            };

            FVector Corners[8];
            for (int i = 0; i < 8; ++i)
            {
                Corners[i] = ShapeTransform.TransformPosition(LocalCorners[i]);
            }

            // 12개 모서리
            static const int Edges[12][2] = {
                {0,1},{1,3},{3,2},{2,0}, // bottom
                {4,5},{5,7},{7,6},{6,4}, // top
                {0,4},{1,5},{2,6},{3,7}  // verticals
            };

            for (int i = 0; i < 12 && LineIndex < BDL.BoxLines.Num(); ++i, ++LineIndex)
            {
                if (BDL.BoxLines[LineIndex])
                    BDL.BoxLines[LineIndex]->SetLine(Corners[Edges[i][0]], Corners[Edges[i][1]]);
            }
        }

        LineIndex = 0;

        // Update Capsule Elements
        for (const FKCapsuleElem& Capsule : Body->AggGeom.CapsuleElements)
        {
            FTransform ShapeLocalTransform(Capsule.Center, Capsule.Rotation, FVector::One());
            FTransform ShapeTransform = BoneLocalTransform.GetWorldTransform(ShapeLocalTransform);

            const float Radius = Capsule.Radius;
            const float HalfHeightCylinder = Capsule.HalfLength;

            const FMatrix ShapeNoScale = FMatrix::FromTRS(
                ShapeTransform.Translation,
                ShapeTransform.Rotation,
                FVector(1.0f, 1.0f, 1.0f)
            );

            constexpr int NumOfSphereSlice = 4;
            constexpr int NumHemisphereSegments = 8;

            // Build top and bottom rings
            TArray<FVector> TopRingLocal;
            TArray<FVector> BottomRingLocal;
            TopRingLocal.Reserve(NumOfSphereSlice);
            BottomRingLocal.Reserve(NumOfSphereSlice);

            for (int i = 0; i < NumOfSphereSlice; ++i)
            {
                const float a0 = (static_cast<float>(i) / NumOfSphereSlice) * TWO_PI;
                const float x = Radius * std::sin(a0);
                const float y = Radius * std::cos(a0);
                TopRingLocal.Add(FVector(x, y, +HalfHeightCylinder));
                BottomRingLocal.Add(FVector(x, y, -HalfHeightCylinder));
            }

            // Top and bottom ring lines
            for (int i = 0; i < NumOfSphereSlice && LineIndex < BDL.CapsuleLines.Num(); ++i)
            {
                const int j = (i + 1) % NumOfSphereSlice;

                // Top ring
                FVector p1 = TopRingLocal[i] * ShapeNoScale;
                FVector p2 = TopRingLocal[j] * ShapeNoScale;
                if (BDL.CapsuleLines[LineIndex])
                    BDL.CapsuleLines[LineIndex]->SetLine(p1, p2);
                ++LineIndex;

                // Bottom ring
                p1 = BottomRingLocal[i] * ShapeNoScale;
                p2 = BottomRingLocal[j] * ShapeNoScale;
                if (LineIndex < BDL.CapsuleLines.Num() && BDL.CapsuleLines[LineIndex])
                    BDL.CapsuleLines[LineIndex]->SetLine(p1, p2);
                ++LineIndex;
            }

            // Vertical connecting lines
            for (int i = 0; i < NumOfSphereSlice && LineIndex < BDL.CapsuleLines.Num(); ++i, ++LineIndex)
            {
                FVector p1 = TopRingLocal[i] * ShapeNoScale;
                FVector p2 = BottomRingLocal[i] * ShapeNoScale;
                if (BDL.CapsuleLines[LineIndex])
                    BDL.CapsuleLines[LineIndex]->SetLine(p1, p2);
            }

            // Hemisphere arcs (top and bottom)
            auto UpdateHemisphereArcs = [&](float CenterZSign)
            {
                const float CenterZ = CenterZSign * HalfHeightCylinder;

                for (int i = 0; i < NumHemisphereSegments && LineIndex < BDL.CapsuleLines.Num(); ++i)
                {
                    const float t0 = (static_cast<float>(i) / NumHemisphereSegments) * PI;
                    const float t1 = (static_cast<float>(i + 1) / NumHemisphereSegments) * PI;

                    // XZ plane arc
                    FVector PlaneXZ0(Radius * std::cos(t0), 0.0f, CenterZ + CenterZSign * Radius * std::sin(t0));
                    FVector PlaneXZ1(Radius * std::cos(t1), 0.0f, CenterZ + CenterZSign * Radius * std::sin(t1));

                    if (BDL.CapsuleLines[LineIndex])
                        BDL.CapsuleLines[LineIndex]->SetLine(PlaneXZ0 * ShapeNoScale, PlaneXZ1 * ShapeNoScale);
                    ++LineIndex;

                    // YZ plane arc
                    if (LineIndex < BDL.CapsuleLines.Num())
                    {
                        FVector PlaneYZ0(0.0f, Radius * std::cos(t0), CenterZ + CenterZSign * Radius * std::sin(t0));
                        FVector PlaneYZ1(0.0f, Radius * std::cos(t1), CenterZ + CenterZSign * Radius * std::sin(t1));

                        if (BDL.CapsuleLines[LineIndex])
                            BDL.CapsuleLines[LineIndex]->SetLine(PlaneYZ0 * ShapeNoScale, PlaneYZ1 * ShapeNoScale);
                        ++LineIndex;
                    }
                }
            };

            UpdateHemisphereArcs(+1.0f); // Top hemisphere
            UpdateHemisphereArcs(-1.0f); // Bottom hemisphere
        }

        LineIndex = 0;

        // Update Convex Elements
        for (const FKConvexElem& Convex : Body->AggGeom.ConvexElements)
        {
            if (Convex.Vertices.IsEmpty())
                continue;

            // Simple line strip through all vertices
            for (int32 i = 0; i < Convex.Vertices.Num() - 1 && LineIndex < BDL.ConvexLines.Num(); ++i, ++LineIndex)
            {
                FVector p1 = BoneLocalTransform.TransformPosition(Convex.Vertices[i]);
                FVector p2 = BoneLocalTransform.TransformPosition(Convex.Vertices[i + 1]);
                
                if (BDL.ConvexLines[LineIndex])
                    BDL.ConvexLines[LineIndex]->SetLine(p1, p2);
            }

            // Close the loop
            if (Convex.Vertices.Num() > 2 && LineIndex < BDL.ConvexLines.Num())
            {
                FVector p1 = BoneLocalTransform.TransformPosition(Convex.Vertices[Convex.Vertices.Num() - 1]);
                FVector p2 = BoneLocalTransform.TransformPosition(Convex.Vertices[0]);
                
                if (BDL.ConvexLines[LineIndex])
                    BDL.ConvexLines[LineIndex]->SetLine(p1, p2);
                
                ++LineIndex;
            }
        }
    }
}

void ASkeletalMeshActor::BuildConstraintLinesCache()
{
    if (!SkeletalMeshComponent || !ConstraintLineComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
    if (!SkeletalMesh || !PhysicsAsset)
    {
        return;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }

    const FSkeleton* Skeleton = &Data->Skeleton;

    // Yellow color for normal constraints
    const FVector4 ConstraintColor(1.0f, 1.0f, 0.0f, 1.0f);

    ConstraintLinesCache.Empty();
    ConstraintLinesCache.resize(PhysicsAsset->Constraints.Num());

    const FMatrix WorldInv = GetWorldMatrix().InverseAffine();

    // Build lines for each constraint
    for (int32 ConstraintIdx = 0; ConstraintIdx < PhysicsAsset->Constraints.Num(); ++ConstraintIdx)
    {
        const FPhysicsConstraintSetup& Constraint = PhysicsAsset->Constraints[ConstraintIdx];
        FConstraintDebugLines& CDL = ConstraintLinesCache[ConstraintIdx];

        // Find bone indices for both bodies
        int32 BoneIndexA = Skeleton->FindBoneIndex(Constraint.BodyNameA);
        int32 BoneIndexB = Skeleton->FindBoneIndex(Constraint.BodyNameB);

        if (BoneIndexA == INDEX_NONE || BoneIndexB == INDEX_NONE)
        {
            continue;
        }

        // Get bone transforms in local (actor) space
        FTransform BoneWorldTransformA = SkeletalMeshComponent->GetBoneWorldTransform(BoneIndexA);
        FTransform BoneWorldTransformB = SkeletalMeshComponent->GetBoneWorldTransform(BoneIndexB);

        FMatrix BoneLocalMatrixA = BoneWorldTransformA.ToMatrix() * WorldInv;
        FMatrix BoneLocalMatrixB = BoneWorldTransformB.ToMatrix() * WorldInv;

        FVector LocalPosA(BoneLocalMatrixA.M[3][0], BoneLocalMatrixA.M[3][1], BoneLocalMatrixA.M[3][2]);
        FVector LocalPosB(BoneLocalMatrixB.M[3][0], BoneLocalMatrixB.M[3][1], BoneLocalMatrixB.M[3][2]);

        // Create a line connecting the two bones
        CDL.ConnectionLine = ConstraintLineComponent->AddLine(LocalPosA, LocalPosB, ConstraintColor);
    }
}

void ASkeletalMeshActor::UpdateConstraintSelectionHighlight(int32 SelectedConstraintIndex)
{
    if (!SkeletalMeshComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
    if (!SkeletalMesh || !PhysicsAsset)
    {
        return;
    }

    const int32 ConstraintCount = PhysicsAsset->Constraints.Num();

    const FVector4 SelectedColor(1.0f, 0.0f, 0.0f, 1.0f);   // Red for selected constraint
    const FVector4 NormalColor(1.0f, 1.0f, 0.0f, 1.0f);     // Yellow for normal constraint

    for (int32 i = 0; i < ConstraintCount && i < ConstraintLinesCache.Num(); ++i)
    {
        const bool bSelected = (i == SelectedConstraintIndex);
        const FVector4 Color = bSelected ? SelectedColor : NormalColor;
        FConstraintDebugLines& CDL = ConstraintLinesCache[i];

        if (CDL.ConnectionLine)
        {
            CDL.ConnectionLine->SetColor(Color);
        }
    }
}

void ASkeletalMeshActor::UpdateConstraintTransforms()
{
    if (!SkeletalMeshComponent || !ConstraintLineComponent)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
    if (!SkeletalMesh || !PhysicsAsset)
    {
        return;
    }

    const FSkeletalMeshData* Data = SkeletalMesh->GetSkeletalMeshData();
    if (!Data)
    {
        return;
    }

    const FSkeleton* Skeleton = &Data->Skeleton;

    const FMatrix WorldInv = GetWorldMatrix().InverseAffine();

    // Update each constraint's line endpoints
    for (int32 ConstraintIdx = 0; ConstraintIdx < PhysicsAsset->Constraints.Num() && ConstraintIdx < ConstraintLinesCache.Num(); ++ConstraintIdx)
    {
        const FPhysicsConstraintSetup& Constraint = PhysicsAsset->Constraints[ConstraintIdx];
        FConstraintDebugLines& CDL = ConstraintLinesCache[ConstraintIdx];

        if (!CDL.ConnectionLine)
        {
            continue;
        }

        // Find bone indices for both bodies
        int32 BoneIndexA = Skeleton->FindBoneIndex(Constraint.BodyNameA);
        int32 BoneIndexB = Skeleton->FindBoneIndex(Constraint.BodyNameB);

        if (BoneIndexA == INDEX_NONE || BoneIndexB == INDEX_NONE)
        {
            continue;
        }

        // Get bone transforms in local (actor) space
        FTransform BoneWorldTransformA = SkeletalMeshComponent->GetBoneWorldTransform(BoneIndexA);
        FTransform BoneWorldTransformB = SkeletalMeshComponent->GetBoneWorldTransform(BoneIndexB);

        FMatrix BoneLocalMatrixA = BoneWorldTransformA.ToMatrix() * WorldInv;
        FMatrix BoneLocalMatrixB = BoneWorldTransformB.ToMatrix() * WorldInv;

        FVector LocalPosA(BoneLocalMatrixA.M[3][0], BoneLocalMatrixA.M[3][1], BoneLocalMatrixA.M[3][2]);
        FVector LocalPosB(BoneLocalMatrixB.M[3][0], BoneLocalMatrixB.M[3][1], BoneLocalMatrixB.M[3][2]);

        // Update the line endpoints
        CDL.ConnectionLine->SetLine(LocalPosA, LocalPosB);
    }
}
