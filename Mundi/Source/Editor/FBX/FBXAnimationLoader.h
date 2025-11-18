#pragma once
#include "fbxsdk.h"
#include "SkeletalMesh.h"

class UAnimSequence;

class FBXAnimationLoader
{
public:
	// Process all animations from an FBX scene
	static void ProcessAnimations(FbxScene* Scene, const FSkeletalMeshData& MeshData, const FString& FilePath, TArray<UAnimSequence*>& OutAnimations);

	// Extract bone animation data for a specific bone
	static void ExtractBoneAnimation(FbxNode* BoneNode, FbxAnimLayer* AnimLayer, FbxTime Start, FbxTime End, int32 NumFrames, TArray<FVector>& OutPositions, TArray<FQuat>& OutRotations, TArray<FVector>& OutScales);

	// Check if a node has animation curves
	static bool NodeHasAnimation(FbxNode* Node, FbxAnimLayer* AnimLayer);

	// Build a map from bone names to FbxNode pointers
	static void BuildBoneNodeMap(FbxNode* RootNode, TMap<FName, FbxNode*>& OutBoneNodeMap);
};
