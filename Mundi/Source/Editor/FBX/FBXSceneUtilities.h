#pragma once
#include "fbxsdk.h"
#include "SkeletalMesh.h"

class FBXSceneUtilities
{
public:
	// Helper: Check if scene needs axis conversion and apply it conditionally
	// Returns true if conversion was applied, false if scene was already in Unreal axis system
	static bool ConvertSceneAxisIfNeeded(FbxScene* Scene);

	// Helper: Check if a node contains a skeleton attribute
	static bool NodeContainsSkeleton(FbxNode* InNode);

	// Helper: Check if a node has skeleton nodes in its descendants (recursive check)
	// Used to distinguish container nodes (like Armature, CactusPA) from unrelated nodes (like Mesh, Camera)
	static bool NodeContainsSkeletonInDescendants(FbxNode* InNode);

	// Helper: Compute accumulated correction matrix from non-skeleton parent nodes (e.g., Armature)
	// Walks up the parent chain and accumulates transforms of all non-skeleton ancestors
	static FbxAMatrix ComputeNonSkeletonParentCorrection(FbxNode* BoneNode, const TMap<const FbxNode*, FbxAMatrix>& NonSkeletonParentTransforms);

	// Helper: Get bind pose matrix for a node
	static FbxAMatrix GetBindPoseMatrix(FbxNode* Node);
};
