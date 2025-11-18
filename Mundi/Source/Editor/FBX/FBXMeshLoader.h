#pragma once
#include "fbxsdk.h"
#include "SkeletalMesh.h"

class FBXMeshLoader
{
public:
	// Load mesh data from a node and its children
	static void LoadMeshFromNode(FbxNode* InNode, FSkeletalMeshData& MeshData, TMap<int32, TArray<uint32>>& MaterialGroupIndexList, TMap<FbxNode*, int32>& BoneToIndex, TMap<FbxSurfaceMaterial*, int32>& MaterialToIndex, TArray<FMaterialInfo>& MaterialInfos);

	// Load mesh from a specific attribute
	static void LoadMeshFromAttribute(FbxNodeAttribute* InAttribute, FSkeletalMeshData& MeshData);

	// Load mesh geometry, UV, normals, tangents, etc.
	static void LoadMesh(FbxMesh* InMesh, FSkeletalMeshData& MeshData, TMap<int32, TArray<uint32>>& MaterialGroupIndexList, TMap<FbxNode*, int32>& BoneToIndex, TArray<int32> MaterialSlotToIndex, int32 DefaultMaterialIndex = 0);
};
