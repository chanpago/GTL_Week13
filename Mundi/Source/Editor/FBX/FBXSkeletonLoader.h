#pragma once
#include "fbxsdk.h"
#include "SkeletalMesh.h"

class FBXSkeletonLoader
{
public:
	// Helper: Recursively traverse nodes until we find one with eSkeleton, skipping intermediate nodes (e.g., Armature)
	static void LoadSkeletonHierarchy(FbxNode* InNode, FSkeletalMeshData& MeshData, int32 ParentNodeIndex, TMap<FbxNode*, int32>& BoneToIndex);

	// Skeleton은 계층구조까지 표현해야하므로 깊이 우선 탐색, ParentNodeIndex 명시.
	static void LoadSkeletonFromNode(FbxNode* InNode, FSkeletalMeshData& MeshData, int32 ParentNodeIndex, TMap<FbxNode*, int32>& BoneToIndex);

	// 여러 루트 본이 있으면 가상 루트 생성
	static void EnsureSingleRootBone(FSkeletalMeshData& MeshData);
};
