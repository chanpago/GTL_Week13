#pragma once
#include "Object.h"
#include "fbxsdk.h"

class UAnimSequence;

class UFbxLoader : public UObject
{
public:

	DECLARE_CLASS(UFbxLoader, UObject)
	static UFbxLoader& GetInstance();
	UFbxLoader();

	static void PreLoad();

	USkeletalMesh* LoadFbxMesh(const FString& FilePath);

	FSkeletalMeshData* LoadFbxMeshAsset(const FString& FilePath);

	// Animation loading
	//void LoadAnimationsFromFbx(const FString& FilePath, TArray<UAnimSequence*>& OutAnimations);

	// Animation caching
	bool SaveAnimationToCache(UAnimSequence* Animation, const FString& CachePath);
	UAnimSequence* LoadAnimationFromCache(const FString& CachePath);
	bool TryLoadAnimationsFromCache(const FString& NormalizedPath, TArray<UAnimSequence*>& OutAnimations);
	

protected:
	~UFbxLoader() override;
private:
	UFbxLoader(const UFbxLoader&) = delete;
	UFbxLoader& operator=(const UFbxLoader&) = delete;


	void LoadMeshFromNode(FbxNode* InNode, FSkeletalMeshData& MeshData, TMap<int32, TArray<uint32>>& MaterialGroupIndexList, TMap<FbxNode*, int32>& BoneToIndex, TMap<FbxSurfaceMaterial*, int32>& MaterialToIndex);

	void LoadSkeletonHierarchy(FbxNode* InNode, FSkeletalMeshData& MeshData, int32 ParentNodeIndex, TMap<FbxNode*, int32>& BoneToIndex);
	bool NodeContainsSkeleton(FbxNode* InNode) const;

	void LoadSkeletonFromNode(FbxNode* InNode, FSkeletalMeshData& MeshData, int32 ParentNodeIndex, TMap<FbxNode*, int32>& BoneToIndex);

	// Axis conversion helper
	bool ConvertSceneAxisIfNeeded(FbxScene* Scene);

	// Helper to compute accumulated parent correction for non-skeleton nodes (e.g., Armature)
	FbxAMatrix ComputeNonSkeletonParentCorrection(FbxNode* BoneNode) const;

	void LoadMeshFromAttribute(FbxNodeAttribute* InAttribute, FSkeletalMeshData& MeshData);

	void LoadMesh(FbxMesh* InMesh, FSkeletalMeshData& MeshData, TMap<int32, TArray<uint32>>& MaterialGroupIndexList, TMap<FbxNode*, int32>& BoneToIndex, TArray<int32> MaterialSlotToIndex, int32 DefaultMaterialIndex = 0);

	void ParseMaterial(FbxSurfaceMaterial* Material, FMaterialInfo& MaterialInfo);

	FString ParseTexturePath(FbxProperty& Property);

	FbxString GetAttributeTypeName(FbxNodeAttribute* InAttribute);

	void EnsureSingleRootBone(FSkeletalMeshData& MeshData);

	// Animation helper functions
	void ProcessAnimations(FbxScene* Scene, const FSkeletalMeshData& MeshData, const FString& FilePath, TArray<UAnimSequence*>& OutAnimations);
	void ExtractBoneAnimation(FbxNode* BoneNode, FbxAnimLayer* AnimLayer, FbxTime Start, FbxTime End, int32 NumFrames, TArray<FVector>& OutPositions, TArray<FQuat>& OutRotations, TArray<FVector>& OutScales);
	bool NodeHasAnimation(FbxNode* Node, FbxAnimLayer* AnimLayer);
	void BuildBoneNodeMap(FbxNode* RootNode, TMap<FName, FbxNode*>& OutBoneNodeMap);
	FbxAMatrix GetBindPoseMatrix(FbxNode* Node);

	// bin파일 저장용
	TArray<FMaterialInfo> MaterialInfos;
	FbxManager* SdkManager = nullptr;

	// Store local transforms of non-skeleton parent nodes (e.g., Armature) for animation correction
	TMap<const FbxNode*, FbxAMatrix> NonSkeletonParentTransforms;
};
