#pragma once
#include "Object.h"
#include "fbxsdk.h"
#include "FBXSceneUtilities.h"
#include "FBXMaterialLoader.h"
#include "FBXSkeletonLoader.h"
#include "FBXMeshLoader.h"
#include "FBXAnimationLoader.h"
#include "FBXAnimationCache.h"

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


protected:
	~UFbxLoader() override;
private:
	UFbxLoader(const UFbxLoader&) = delete;
	UFbxLoader& operator=(const UFbxLoader&) = delete;


	// bin파일 저장용
	TArray<FMaterialInfo> MaterialInfos;
	FbxManager* SdkManager = nullptr;

	// Store local transforms of non-skeleton parent nodes (e.g., Armature) for animation correction
	TMap<const FbxNode*, FbxAMatrix> NonSkeletonParentTransforms;
};
