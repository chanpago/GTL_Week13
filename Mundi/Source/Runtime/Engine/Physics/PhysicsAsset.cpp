#include "pch.h"
#include "PhysicsAsset.h"
#include "BodySetup.h"

IMPLEMENT_CLASS(UPhysicsAsset)

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
    SphylElements.Empty();
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