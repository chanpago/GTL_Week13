#include "pch.h"
#include "FBXAnimationLoader.h"
#include "FBXAnimationCache.h"
#include "FBXSceneUtilities.h"
#include "Source/Runtime/Engine/Animation/AnimSequence.h"
#include "Source/Runtime/Engine/Animation/AnimDateModel.h"
#include "ObjectFactory.h"
#include "PathUtils.h"
#include <filesystem>

void FBXAnimationLoader::ProcessAnimations(FbxScene* Scene, const FSkeletalMeshData& MeshData, const FString& FilePath, TArray<UAnimSequence*>& OutAnimations)
{
	// Find first non-skeleton container node and extract its transform for correction
	// This generalizes the old "Armature" hardcoded logic to work with any non-skeleton parent (e.g., CactusPA, Armature, etc.)
	FTransform NonSkeletonCorrection;
	bool bFoundNonSkeletonParent = false;
	FbxNode* RootNode = Scene->GetRootNode();

	if (RootNode)
	{
		// Search for the first child that is NOT a skeleton node
		// (skeleton nodes will be handled separately during bone extraction)
		int RootNodeChildCount = RootNode->GetChildCount();
		for (int i = 0; i < RootNodeChildCount; ++i)
		{
			FbxNode* ChildNode = RootNode->GetChild(i);

			// Check if this node is a non-skeleton container (e.g., Armature, CactusPA, etc.)
			if (!FBXSceneUtilities::NodeContainsSkeleton(ChildNode) && FBXSceneUtilities::NodeContainsSkeletonInDescendants(ChildNode)
				)
			{
				// Found a non-skeleton parent node - extract its transform
				FbxAMatrix LocalTransform = ChildNode->EvaluateLocalTransform(FBXSDK_TIME_ZERO);

				// Convert FbxAMatrix to FTransform
				FbxVector4 Translation = LocalTransform.GetT();
				FbxQuaternion Rotation = LocalTransform.GetQ();
				FbxVector4 Scale = LocalTransform.GetS();

				NonSkeletonCorrection.Translation = FVector(
					static_cast<float>(Translation[0]),
					static_cast<float>(Translation[1]),
					static_cast<float>(Translation[2])
				);
				NonSkeletonCorrection.Rotation = FQuat(
					static_cast<float>(Rotation[0]),
					static_cast<float>(Rotation[1]),
					static_cast<float>(Rotation[2]),
					static_cast<float>(Rotation[3])
				);
				NonSkeletonCorrection.Scale3D = FVector(
					static_cast<float>(Scale[0]),
					static_cast<float>(Scale[1]),
					static_cast<float>(Scale[2])
				);

				FString NodeName = ChildNode->GetName();
				UE_LOG("Found non-skeleton container node '%s', storing correction transform", NodeName.c_str());
				bFoundNonSkeletonParent = true;
				break;
			}
		}
	}

	// If no non-skeleton parent found, skeleton is directly under root (Mixamo case)
	if (!bFoundNonSkeletonParent)
	{
		NonSkeletonCorrection = FTransform();
		UE_LOG("No non-skeleton container found, skeleton directly under root (Mixamo-style FBX)");
	}

	// 중요
	// Get animation stack count
	// FBX에 애니메이션이 있는지 확인
	int32 AnimStackCount = Scene->GetSrcObjectCount<FbxAnimStack>();

	for (int32 StackIndex = 0; StackIndex < AnimStackCount; ++StackIndex)
	{
		// Get animation stack
		FbxAnimStack* AnimStack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		if (!AnimStack)
		{
			continue;
		}

		FbxAnimLayer* AnimLayer = AnimStack->GetMember<FbxAnimLayer>(0);
		if (!AnimLayer)
		{
			continue;
		}

		Scene->SetCurrentAnimationStack(AnimStack);
		FString AnimStackName = AnimStack->GetName();

		// Get time range
		FbxTimeSpan TimeSpan = AnimStack->GetLocalTimeSpan();
		FbxTime Start = TimeSpan.GetStart();
		FbxTime End = TimeSpan.GetStop();

		float FrameRate = static_cast<float>(FbxTime::GetFrameRate(Scene->GetGlobalSettings().GetTimeMode()));
		float Duration = static_cast<float>(End.GetSecondDouble() - Start.GetSecondDouble());

		// 마지막 프레임 누락 방지용 소수점 올림
		int32 NumFrames = FMath::CeilToInt(Duration * FrameRate);

		// 애니메이션을 저장할 AnimSequence 생성
		UAnimSequence* AnimSequence = NewObject<UAnimSequence>();
		AnimSequence->ObjectName = FName(AnimStackName);


		// Set Armature correction transform (Identity for Mixamo, actual transform for Blender)
		AnimSequence->SetNonSkeletonCorrection(NonSkeletonCorrection);

		// Get DataModel
		UAnimDataModel* DataModel = AnimSequence->GetDataModel();
		if (!DataModel)
		{
			continue;
		}

		DataModel->SetFrameRate(static_cast<int32>(FrameRate));
		DataModel->SetPlayLength(Duration);
		DataModel->SetNumberOfFrames(NumFrames);
		DataModel->SetNumberOfKeys(NumFrames);

		// Build bone node map
		FbxNode* RootNode = Scene->GetRootNode();
		TMap<FName, FbxNode*> BoneNodeMap;
		BuildBoneNodeMap(RootNode, BoneNodeMap);

		UE_LOG("ProcessAnimations: AnimStack='%s', BoneNodeMap size=%d, Skeleton bones=%d",
			AnimStackName.c_str(), BoneNodeMap.Num(), MeshData.Skeleton.Bones.Num());

		// Extract animation for each bone
		int32 ExtractedBones = 0;
		for (const FBone& Bone : MeshData.Skeleton.Bones)
		{
			FName BoneName = Bone.Name;
			if (!BoneNodeMap.Contains(BoneName))
			{
				static int LogCount = 0;
				if (LogCount++ < 5)
				{
					UE_LOG("Bone '%s' not found in BoneNodeMap", BoneName);
				}
				continue;
			}

			FbxNode* BoneNode = BoneNodeMap[BoneName];
			if (!BoneNode)
			{
				continue;
			}

			// Add bone track
			int32 TrackIndex = DataModel->AddBoneTrack(BoneName);
			if (TrackIndex == INDEX_NONE)
			{
				continue;
			}

			TArray<FVector> Positions;
			TArray<FQuat> Rotations;
			TArray<FVector> Scales;

			// Extract animation data - 무조건 추출 시도 (EvaluateLocalTransform 사용)
			ExtractBoneAnimation(BoneNode, AnimLayer, Start, End, NumFrames, Positions, Rotations, Scales);

			// Set keys in data model
			DataModel->SetBoneTrackKeys(BoneName, Positions, Rotations, Scales);
			ExtractedBones++;
		}

		UE_LOG("Extracted animation data for %d bones", ExtractedBones);

		// Register animation in ResourceManager
		FString AnimKey = FilePath + "_" + AnimStackName;
		RESOURCE.Add<UAnimSequence>(AnimKey, AnimSequence);

		OutAnimations.Add(AnimSequence);
		UE_LOG("Extracted animation: %s (Duration: %.2fs, Frames: %d) -> Key: %s",
			AnimStackName.c_str(), Duration, NumFrames, AnimKey.c_str());
	}

#ifdef USE_OBJ_CACHE
	// Save animations to cache
	if (OutAnimations.Num() > 0)
	{
		FString NormalizedPath = NormalizePath(FilePath);
		FString CacheBasePath = ConvertDataPathToCachePath(NormalizedPath);
		FString AnimCacheDir = CacheBasePath + "_Anims";

		// Create cache directory
		std::filesystem::create_directories(AnimCacheDir);

		for (int32 i = 0; i < OutAnimations.Num(); ++i)
		{
			FString AnimStackName = OutAnimations[i]->ObjectName.ToString();
			FString SanitizedName = AnimStackName;
			for (char& ch : SanitizedName)
			{
				if (ch == '|' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '/' || ch == '\\')
				{
					ch = '_';
				}
			}
			FString AnimCachePath = AnimCacheDir + "/" + SanitizedName + ".anim.bin";

			if (FBXAnimationCache::SaveAnimationToCache(OutAnimations[i], AnimCachePath))
			{
				UE_LOG("Saved animation to cache: %s", AnimCachePath.c_str());
			}
			else
			{
				UE_LOG("Failed to save animation to cache: %s", AnimCachePath.c_str());
			}
		}

		UE_LOG("Saved %d animations to cache directory: %s", OutAnimations.Num(), AnimCacheDir.c_str());
	}
#endif
}

void FBXAnimationLoader::ExtractBoneAnimation(FbxNode* BoneNode, FbxAnimLayer* AnimLayer, FbxTime Start, FbxTime End, int32 NumFrames, TArray<FVector>& OutPositions, TArray<FQuat>& OutRotations, TArray<FVector>& OutScales)
{
	if (!BoneNode || !AnimLayer || NumFrames <= 0)
	{
		UE_LOG("ExtractBoneAnimation failed: BoneNode=%p, AnimLayer=%p, NumFrames=%d", BoneNode, AnimLayer, NumFrames);
		return;
	}

	OutPositions.SetNum(NumFrames);
	OutRotations.SetNum(NumFrames);
	OutScales.SetNum(NumFrames);

	static bool bLoggedExtraction = false;
	if (!bLoggedExtraction)
	{
		UE_LOG("=== ExtractBoneAnimation START for %s ===", BoneNode->GetName());
		UE_LOG("NumFrames: %d, Start: %.2f, End: %.2f", NumFrames, Start.GetSecondDouble(), End.GetSecondDouble());
		bLoggedExtraction = true;
	}

	float Duration = static_cast<float>(End.GetSecondDouble() - Start.GetSecondDouble());
	FbxTime FrameTime;
	FrameTime.SetSecondDouble(Duration / static_cast<double>(NumFrames - 1));

	// 최상위 본(루트 본)에 대해서만 로그 출력
	static bool bLoggedRootBone = false;
	bool bIsRootBone = (BoneNode->GetParent() == nullptr || BoneNode->GetParent()->GetNodeAttribute() == nullptr);

	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		FbxTime CurrentTime = Start + FrameTime * FrameIndex;

		// 절대 로컬 트랜스폼을 직접 사용 (델타가 아닌 실제 로컬 변환)
		FbxAMatrix LocalTransform = BoneNode->EvaluateLocalTransform(CurrentTime);

		// Extract translation - ConvertScene()이 이미 cm → m 변환 적용했으므로 그대로 사용
		FbxVector4 Translation = LocalTransform.GetT();
		OutPositions[FrameIndex] = FVector(
			static_cast<float>(Translation[0]),
			static_cast<float>(Translation[1]),
			static_cast<float>(Translation[2])
		);

		// Extract rotation
		FbxQuaternion Rotation = LocalTransform.GetQ();
		OutRotations[FrameIndex] = FQuat(
			static_cast<float>(Rotation[0]),
			static_cast<float>(Rotation[1]),
			static_cast<float>(Rotation[2]),
			static_cast<float>(Rotation[3])
		);

		// Extract scale
		FbxVector4 Scale = LocalTransform.GetS();
		OutScales[FrameIndex] = FVector(
			static_cast<float>(Scale[0]),
			static_cast<float>(Scale[1]),
			static_cast<float>(Scale[2])
		);

		// 저장되는 실제 값 로그 (루트 본의 처음, 중간, 끝 프레임)
		if (!bLoggedRootBone && bIsRootBone)
		{
			int32 MidFrame = NumFrames / 2;
			int32 EndFrame = NumFrames - 1;

			if (FrameIndex < 3 || FrameIndex == MidFrame || FrameIndex == MidFrame + 1 || FrameIndex >= EndFrame - 1)
			{
				UE_LOG("[Frame %d/%d] LocalTransform: T(%.3f,%.3f,%.3f) R(%.3f,%.3f,%.3f,%.3f) S(%.3f,%.3f,%.3f)",
					FrameIndex, NumFrames - 1,
					OutPositions[FrameIndex].X, OutPositions[FrameIndex].Y, OutPositions[FrameIndex].Z,
					OutRotations[FrameIndex].X, OutRotations[FrameIndex].Y, OutRotations[FrameIndex].Z, OutRotations[FrameIndex].W,
					OutScales[FrameIndex].X, OutScales[FrameIndex].Y, OutScales[FrameIndex].Z);
			}
		}
	}

	// 루트 본 로그 출력 완료 플래그 설정
	if (!bLoggedRootBone && bIsRootBone)
	{
		bLoggedRootBone = true;
	}
}

bool FBXAnimationLoader::NodeHasAnimation(FbxNode* Node, FbxAnimLayer* AnimLayer)
{
	if (!Node || !AnimLayer)
	{
		return false;
	}

	// Check for position animation curves
	FbxAnimCurve* TranslationX = Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X);
	FbxAnimCurve* TranslationY = Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y);
	FbxAnimCurve* TranslationZ = Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z);

	// Check for rotation animation curves
	FbxAnimCurve* RotationX = Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X);
	FbxAnimCurve* RotationY = Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y);
	FbxAnimCurve* RotationZ = Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z);

	// Check for scale animation curves
	FbxAnimCurve* ScaleX = Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X);
	FbxAnimCurve* ScaleY = Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y);
	FbxAnimCurve* ScaleZ = Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z);

	return (TranslationX || TranslationY || TranslationZ ||
		RotationX || RotationY || RotationZ ||
		ScaleX || ScaleY || ScaleZ);
}

void FBXAnimationLoader::BuildBoneNodeMap(FbxNode* RootNode, TMap<FName, FbxNode*>& OutBoneNodeMap)
{
	if (!RootNode)
	{
		return;
	}

	// Check if this node is a bone
	for (int32 i = 0; i < RootNode->GetNodeAttributeCount(); ++i)
	{
		FbxNodeAttribute* Attribute = RootNode->GetNodeAttributeByIndex(i);
		if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			FName BoneName(RootNode->GetName());
			OutBoneNodeMap.Add(BoneName, RootNode);
			break;
		}
	}

	// Recursively process children
	for (int32 i = 0; i < RootNode->GetChildCount(); ++i)
	{
		BuildBoneNodeMap(RootNode->GetChild(i), OutBoneNodeMap);
	}
}
