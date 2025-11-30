#include "pch.h"
#include "PhysicsAsset.h"
#include "BodySetup.h"
#include "../Animation/AnimationAsset.h"
#include "SkeletalMeshComponent.h"
IMPLEMENT_CLASS(UPhysicsAsset)

namespace
{
    inline FVector InverseTransformPositionNoScale(const FTransform& Transform, const FVector& Point)
    {
        // Position -> subtract translation, then rotate by inverse
        const FVector Local = Point - Transform.Translation;
        return Transform.Rotation.Inverse().RotateVector(Local);
    }

    inline FQuat InverseTransformRotationNoScale(const FTransform& Transform, const FQuat& Rotation)
    {
        return Transform.Rotation.Inverse() * Rotation;
    }
}

UPhysicsAsset::~UPhysicsAsset()
{
    CurrentSkeletal = nullptr;
}

void UPhysicsAsset::BuildRuntimeCache()
{
    // 1) Body 이름 -> 인덱스 맵
    BuildBodySetupIndexMap();

    // 2) 각 BodySetup 캐시 (Bounds, Mass 등)
    for (UBodySetup* Body : BodySetups)
    {
        if (!Body) continue;
        if (Body->bCachedDataDirty)
        {
            Body->BuildCachedData();
            Body->bCachedDataDirty = false;
        }
    }
}

void UPhysicsAsset::BuildBodySetupIndexMap()
{
	BodySetupIndexMap.Empty();

	for (int32 Index = 0; Index < BodySetups.Num(); ++Index)
	{
		if (BodySetups[Index])
		{
			BodySetupIndexMap.Add(BodySetups[Index]->BoneName, Index);
		}
	}
}

int32 UPhysicsAsset::FindBodyIndex(FName BodyName) const
{
	if (const int32* Found = BodySetupIndexMap.Find(BodyName))
	{
		return *Found;
	}
	return INDEX_NONE;
}

UBodySetup* UPhysicsAsset::FindBodySetup(FName BodyName) const
{
	const int32 Index = FindBodyIndex(BodyName);
	return (Index != INDEX_NONE) ? BodySetups[Index] : nullptr;
}

int32 UPhysicsAsset::FindConstraintIndex(FName BodyA, FName BodyB) const
{
	for (int32 i = 0; i < Constraints.Num(); ++i)
	{
		const auto& C = Constraints[i];
		if ((C.BodyNameA == BodyA && C.BodyNameB == BodyB) ||
			(C.BodyNameA == BodyB && C.BodyNameB == BodyA))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UPhysicsAsset::CreateGenerateAllBodySetup(EAggCollisionShapeType ShapeType, FSkeleton* Skeleton, USkeletalMeshComponent* SkeletalComponent)
{
    if (!Skeleton)
    {
        UE_LOG("UPhysicsAsset::CreateGenerateAllBodySetup : Skeleton is null");
        return;
    }

    BodySetups.Empty();
    Constraints.Empty();
    CurrentSkeletal = SkeletalComponent;

    CalculateScaledMinColliderLength();

    TArray<int32> BoneIndicesToCreate;
    SelectBonesForBodies(Skeleton, BoneIndicesToCreate);
    if (BoneIndicesToCreate.IsEmpty())
    {
        UE_LOG("UPhysicsAsset::CreateGenerateAllBodySetup : no bones selected");
        return;
    }

    for (int32 BoneIndex : BoneIndicesToCreate)
    {
        UBodySetup* Body = NewObject<UBodySetup>();
        if (!Body)
        {
            continue;
        }

        Body->BoneName = Skeleton->GetBoneName(BoneIndex);

        switch (ShapeType)
        {
        case EAggCollisionShapeType::Capsule:
            Body->AddCapsule(FitCapsuleToBone(Skeleton, BoneIndex));
            break;
        case EAggCollisionShapeType::Box:
            Body->AddBox(FitBoxToBone(Skeleton, BoneIndex));
            break;
        case EAggCollisionShapeType::Sphere:
            Body->AddSphere(FitSphereToBone(Skeleton, BoneIndex));
            break;
        default:
            Body->AddCapsule(FitCapsuleToBone(Skeleton, BoneIndex));
            break;
        }

        BodySetups.Add(Body);
    }

    GenerateConstraintsFromSkeleton(Skeleton, BoneIndicesToCreate, SkeletalComponent);
    BuildRuntimeCache();
}

void UPhysicsAsset::SelectBonesForBodies(const FSkeleton* Skeleton, TArray<int32>& OutBones) const
{
    OutBones.Empty();
    if (!Skeleton)
    {
        return;
    }

    const int32 NumBones = Skeleton->GetNumBones();
    const float MinBoneLength = 0.1f;

    for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
    {
        const int32 ParentIndex = Skeleton->GetParentIndex(BoneIndex);
        if (ParentIndex == INDEX_NONE)
        {
            OutBones.Add(BoneIndex);
            continue;
        }

        const FTransform ParentTransform(CurrentSkeletal->GetBoneWorldTransform(ParentIndex));
        const FTransform ChildTransform(CurrentSkeletal->GetBoneWorldTransform(BoneIndex));

        const float Length = (ChildTransform.Translation - ParentTransform.Translation).Size();
        
        if (Length >= MinBoneLength)
        {
            OutBones.Add(BoneIndex);
        }
    }
}

FBonePoints UPhysicsAsset::GetBonePoints(const FSkeleton* Skeleton, int32 BoneIndex) const
{
    FBonePoints Points{};

    if (!Skeleton)
    {
        return Points;
    }

    const int32 NumBones = Skeleton->GetNumBones();
    if (BoneIndex < 0 || BoneIndex >= NumBones)
    {
        return Points;
    }

    const int32 ParentIndex = Skeleton->GetParentIndex(BoneIndex);
    const FTransform ChildT = CurrentSkeletal->GetBoneWorldTransform(BoneIndex);
    FTransform ParentT = ChildT;
    if (ParentIndex != INDEX_NONE && ParentIndex < NumBones)
    {
        ParentT = CurrentSkeletal->GetBoneWorldTransform(ParentIndex);
    }

    Points.Start = ParentT.Translation;
    Points.End = ChildT.Translation;

    return Points;
}

FKSphereElem UPhysicsAsset::FitSphereToBone(const FSkeleton* Skeleton, int32 BoneIndex)
{
    FBonePoints Point = GetBonePoints(Skeleton, BoneIndex);
    const FVector BoneDir = (Point.End - Point.Start);
    const float BoneLen = BoneDir.Size();

    FKSphereElem Elem;

    if (BoneLen < KINDA_SMALL_NUMBER)
    {
        Elem.Radius = 0.1f;
        Elem.Center = FVector::Zero();
    }
    else
    {
        FVector WorldCenter = (Point.Start + Point.End) * 0.5f;
        Elem.Radius = BoneLen * 0.5f;

        // 월드 스페이스 Center to Bone의 로컬 스페이스
        FTransform BoneWorldTransform = CurrentSkeletal->GetBoneWorldTransform(BoneIndex);
        BoneWorldTransform.Scale3D = FVector(1.f, 1.f, 1.f);
        Elem.Center = InverseTransformPositionNoScale(BoneWorldTransform, WorldCenter);
    }

    return Elem;
}

FKBoxElem UPhysicsAsset::FitBoxToBone(const FSkeleton* Skeleton, int32 BoneIndex)
{
    FBonePoints Point = GetBonePoints(Skeleton, BoneIndex);

    const FVector BoneDir = (Point.End - Point.Start);
    const float BoneLen = BoneDir.Size();

    FKBoxElem Elem;

    if (BoneLen < KINDA_SMALL_NUMBER)
    {
        Elem.Extents = FVector(0.1f, 0.1f, 0.1f);
        Elem.Center = FVector::Zero();
        Elem.Rotation = FQuat::Identity();
    }
    else
    {
        FVector DirNorm = BoneDir.GetSafeNormal();
        const FVector WorldCenter = (Point.Start + Point.End) * 0.5f;

        const float HalfDepth = BoneLen * 0.5f; // Main axis along bone length
        const float HalfWidth = BoneLen * 0.15f; // Other axes smaller

        const FVector BoxAxis(0, 0, 1);
        const FQuat WorldRot = FQuat::FindBetweenNormals(BoxAxis, DirNorm);

        Elem.Extents = FVector(HalfWidth, HalfWidth, HalfDepth);

        FTransform BoneWorldTransform = CurrentSkeletal->GetBoneWorldTransform(BoneIndex);
        BoneWorldTransform.Scale3D = FVector(1.f, 1.f, 1.f);

        Elem.Center = InverseTransformPositionNoScale(BoneWorldTransform, WorldCenter);
        Elem.Rotation = InverseTransformRotationNoScale(BoneWorldTransform, WorldRot);
    }

    return Elem;
}

FKCapsuleElem UPhysicsAsset::FitCapsuleToBone(const FSkeleton* Skeleton, int32 BoneIndex)
{
    FBonePoints Point = GetBonePoints(Skeleton, BoneIndex);

    const FVector BoneDir = (Point.End - Point.Start);
    const float BoneLen = BoneDir.Size();
    
    FKCapsuleElem Elem;

    if (BoneLen < KINDA_SMALL_NUMBER)
    {
        Elem.Radius = 0.1f; 
        Elem.HalfLength = 0.0f;
        Elem.Center = FVector::Zero(); 
        Elem.Rotation = FQuat::Identity();
    }
    else
    {
        Elem.Radius = BoneLen / 4.0f;
        Elem.HalfLength = BoneLen / 4.0f;

        const FVector DirNorm = BoneDir.GetSafeNormal();
        
        const FVector CapsuleAxis(0, 0, 1);
        const FQuat WorldRot = FQuat::FindBetweenNormals(CapsuleAxis, DirNorm);

        const FVector WorldCenter = (Point.Start + Point.End) * 0.5f;

        FTransform BoneWorldTransform = CurrentSkeletal->GetBoneWorldTransform(BoneIndex);
        BoneWorldTransform.Scale3D = FVector(1.f, 1.f, 1.f);

        Elem.Center = InverseTransformPositionNoScale(BoneWorldTransform, WorldCenter);
        Elem.Rotation = InverseTransformRotationNoScale(BoneWorldTransform, WorldRot);
    }

    return Elem;
}

void UPhysicsAsset::GenerateConstraintsFromSkeleton(const FSkeleton* Skeleton, const TArray<int32>& BoneIndicesToCreate, USkeletalMeshComponent* SkeletalComponent)
{
    if (!Skeleton)
    {
        return;
    }

    TSet<int32> BodyBones;
    for (int32 BoneIndex : BoneIndicesToCreate)
    {
        BodyBones.Add(BoneIndex);
    }

    for (int32 BoneIndex : BoneIndicesToCreate)
    {
        const int32 ParentIndex = Skeleton->GetParentIndex(BoneIndex);
        if (ParentIndex == INDEX_NONE)
        {
            continue;
        }

        if (!BodyBones.Contains(ParentIndex))
        {
            continue;
        }

        const FName ChildBoneName = Skeleton->GetBoneName(BoneIndex);
        const FName ParentBoneName = Skeleton->GetBoneName(ParentIndex);

        FPhysicsConstraintSetup Setup{};
        Setup.BodyNameA = ParentBoneName;
        Setup.BodyNameB = ChildBoneName;

        FTransform ParentWorldTransform;
        FTransform ChildWorldTransform;
        if (SkeletalComponent)
        {
            ParentWorldTransform = SkeletalComponent->GetBoneWorldTransform(ParentIndex);
            ChildWorldTransform = SkeletalComponent->GetBoneWorldTransform(BoneIndex);

            ParentWorldTransform.Scale3D = FVector(1.0f, 1.0f, 1.0f);
            ChildWorldTransform.Scale3D = FVector(1.0f, 1.0f, 1.0f);
        }
        else
        {
            ParentWorldTransform = SkeletalComponent->GetBoneWorldTransform(ParentIndex);
            ChildWorldTransform = SkeletalComponent->GetBoneWorldTransform(BoneIndex);

            ParentWorldTransform.Scale3D = FVector(1.0f, 1.0f, 1.0f);
            ChildWorldTransform.Scale3D = FVector(1.0f, 1.0f, 1.0f);
        }

        Setup.LocalFrameA = ParentWorldTransform.GetRelativeTransform(ChildWorldTransform);
        Setup.LocalFrameB = FTransform();

        Setup.TwistLimitMin = DegreesToRadians(-45.0f);
        Setup.TwistLimitMax = DegreesToRadians(45.0f);
        Setup.SwingLimitY = DegreesToRadians(30.0f);
        Setup.SwingLimitZ = DegreesToRadians(30.0f);

        Constraints.Add(Setup);
    }
}

float UPhysicsAsset::GetMeshScale() const
{
    if (!CurrentSkeletal)
    {
        return 1.0f;
    }

    // Root bone의 월드 스케일 가져오기
    const FTransform RootTransform = CurrentSkeletal->GetBoneWorldTransform(0);
    const FVector Scale = RootTransform.Scale3D;

    return FMath::Max(FMath::Abs(Scale.X), FMath::Abs(Scale.Y), FMath::Abs(Scale.Z));
}

void UPhysicsAsset::CalculateScaledMinColliderLength()
{
    float MeshScale = GetMeshScale();
    MinColliderLength *= MeshScale;
}

bool UPhysicsAsset::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
    JSON Root;
    if (!FJsonSerializer::LoadJsonFromFile(Root, UTF8ToWide(InFilePath)))
    {
        UE_LOG("[UPhysicsAsset] Failed to load file: %s", InFilePath.c_str());
        return false;
    }

    Serialize(true, Root);
    SetFilePath(NormalizePath(InFilePath)); 
    
    BuildRuntimeCache();

    UE_LOG("[UPhysicsAsset] Loaded successfully: %s", InFilePath.c_str());
    return true;
}

bool UPhysicsAsset::SaveToFile(const FString& FilePath)
{
    JSON JsonData = JSON::Make(JSON::Class::Object);

    Serialize(false, JsonData);

    std::ofstream OutFile(FilePath, std::ios::out);
    if (!OutFile.is_open())
    {
        UE_LOG("Failed to open file for writing: %s", FilePath.c_str());
        return false;
    }

    OutFile << JsonData.dump(4); // Pretty print
    OutFile.close();

    UE_LOG("UPhysicsAsset saved to: %s", FilePath.c_str());
    return true;
}

void UPhysicsAsset::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // =========================================================
        // [LOAD] 역직렬화
        // =========================================================
        if (!InOutHandle.hasKey("Type") || InOutHandle["Type"].ToString() != "PhysicsAsset") { return; }

        // 기존 데이터 초기화 (BodySetups)
        for (UBodySetup* BodySetup : BodySetups)
        {
            if (BodySetup) ObjectFactory::DeleteObject(BodySetup);
        }
        BodySetups.Empty();

        // 기본 정보 로드
        FString NameStr;
        if (FJsonSerializer::ReadString(InOutHandle, "Name", NameStr))
        {
            Name = FName(NameStr);
        }

        // BodySetups 배열 로드
        if (InOutHandle.hasKey("BodySetups"))
        {
            JSON BodySetupsArray = InOutHandle["BodySetups"];
            for (int32 i = 0; i < BodySetupsArray.size(); ++i)
            {
                JSON BodySetupJson = BodySetupsArray[i];

                // BodySetup 생성
                UBodySetup* NewBodySetup = Cast<UBodySetup>(NewObject(UBodySetup::StaticClass()));
                if (NewBodySetup)
                {
                    NewBodySetup->Serialize(true, BodySetupJson);
                    BodySetups.Add(NewBodySetup);
                }
            }
        }

        // Constraints 배열 로드
        Constraints.Empty();
        if (InOutHandle.hasKey("Constraints"))
        {
            JSON& ConstraintArray = InOutHandle["Constraints"];
            for (int32 i = 0; i < ConstraintArray.size(); ++i)
            {
                JSON& Item = ConstraintArray.at(i);

                FPhysicsConstraintSetup Setup{};
                Setup.Serialize(true, Item);
                Constraints.Add(Setup);
            }
        }

        // 런타임 캐시 구축 (Bounds 계산 등)
        BuildRuntimeCache();
    }
    else
    {
        // =========================================================
        // [SAVE] 직렬화
        // =========================================================
        InOutHandle["Type"] = "PhysicsAsset";
        InOutHandle["Name"] = Name.ToString();

        // Physics 배열 저장
        JSON PhysicsArray = JSON::Make(JSON::Class::Array);
        for (UBodySetup* BodySetup : BodySetups)
        {
            if (!BodySetup) continue;

            JSON BodySetupJson = JSON::Make(JSON::Class::Object);
            BodySetup->Serialize(false, BodySetupJson);

            PhysicsArray.append(BodySetupJson);
        }
        InOutHandle["BodySetups"] = PhysicsArray;

        JSON ConstraintArray = JSON::Make(JSON::Class::Array); 
        for (FPhysicsConstraintSetup& Setup : Constraints)
        { 
            if (!Setup.BodyNameA.IsValid() || !Setup.BodyNameB.IsValid())
            {
                continue;
            }

            JSON ConstraintJson = JSON::Make(JSON::Class::Object);
            Setup.Serialize(false, ConstraintJson);
            ConstraintArray.append(ConstraintJson);
        } 
        InOutHandle["Constraints"] = ConstraintArray;
    }
}

void FKAggregateGeom::Clear()
{
    SphereElements.Empty();
    BoxElements.Empty();
    CapsuleElements.Empty();
    ConvexElements.Empty();
}

void FPhysicsConstraintSetup::Serialize(const bool bInIsLoading,  JSON& InOutHandle)
{
    if (bInIsLoading)
    {
        FString BodyAName;
        if (FJsonSerializer::ReadString(InOutHandle, "BodyA", BodyAName))
        {
            BodyNameA = FName(BodyAName);
        }
        FString BodyBName;
        if (FJsonSerializer::ReadString(InOutHandle, "BodyB", BodyBName))
        {
            BodyNameB = FName(BodyBName);
        }

        FJsonSerializer::ReadTransform(InOutHandle, "LocalFrameA", LocalFrameA);
        FJsonSerializer::ReadTransform(InOutHandle, "LocalFrameB", LocalFrameB);
        FJsonSerializer::ReadFloat(InOutHandle, "TwistMin", TwistLimitMin);
        FJsonSerializer::ReadFloat(InOutHandle, "TwistMax", TwistLimitMax);
        FJsonSerializer::ReadFloat(InOutHandle, "SwingY", SwingLimitY);
        FJsonSerializer::ReadFloat(InOutHandle, "SwingZ", SwingLimitZ);
    }
    else
    {
        InOutHandle["BodyA"] = BodyNameA.ToString().c_str();
        InOutHandle["BodyB"] = BodyNameB.ToString().c_str();
        InOutHandle["LocalFrameA"] = FJsonSerializer::TransformToJson(LocalFrameA);
        InOutHandle["LocalFrameB"] = FJsonSerializer::TransformToJson(LocalFrameB);
        InOutHandle["TwistMin"] = TwistLimitMin;
        InOutHandle["TwistMax"] = TwistLimitMax;
        InOutHandle["SwingY"] = SwingLimitY;
        InOutHandle["SwingZ"] = SwingLimitZ;
    }
}