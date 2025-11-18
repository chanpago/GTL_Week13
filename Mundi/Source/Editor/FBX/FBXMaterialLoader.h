#pragma once
#include "fbxsdk.h"
#include "SkeletalMesh.h"
#include "Material.h"

class FBXMaterialLoader
{
public:
	// Parse material from FBX and create MaterialInfo
	static void ParseMaterial(FbxSurfaceMaterial* Material, FMaterialInfo& MaterialInfo, TArray<FMaterialInfo>& MaterialInfos);

	// Parse texture path from FBX property
	static FString ParseTexturePath(FbxProperty& Property);

	// Get attribute type name (for debugging/logging)
	static FbxString GetAttributeTypeName(FbxNodeAttribute* InAttribute);
};
