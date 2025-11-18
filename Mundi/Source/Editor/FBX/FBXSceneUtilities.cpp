#include "pch.h"
#include "FBXSceneUtilities.h"

// Helper: Check if scene needs axis conversion and apply it conditionally
// Returns true if conversion was applied, false if scene was already in Unreal axis system
bool FBXSceneUtilities::ConvertSceneAxisIfNeeded(FbxScene* Scene)
{
	if (!Scene)
		return false;

	FbxAxisSystem UnrealImportAxis(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
	FbxAxisSystem SourceSetup = Scene->GetGlobalSettings().GetAxisSystem();

	// Check if scene is already in Unreal axis system
	if (SourceSetup == UnrealImportAxis)
	{
		UE_LOG("Scene is already in Unreal axis system, skipping DeepConvertScene");

		// Still need to check unit conversion
		FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
		double SceneScaleFactor = SceneUnit.GetScaleFactor();
		const double Tolerance = 1e-6;

		// Only convert if scale factor is significantly different from 1.0 (not already in meters)
		if ((SceneUnit != FbxSystemUnit::m) && (std::abs(SceneScaleFactor - 1.0) > Tolerance))
		{
			UE_LOG("Scene unit scale factor is %.6f, converting to meters", SceneScaleFactor);
			FbxSystemUnit::m.ConvertScene(Scene);
			UE_LOG("Converted scene unit to meters");
			return true;
		}
		else
		{
			UE_LOG("Scene is already in meters (scale factor: %.6f), skipping unit conversion", SceneScaleFactor);
			return false;
		}
	}

	// Scene needs axis conversion
	UE_LOG("Converting scene from source axis system to Unreal axis system");

	// Convert unit system
	FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
	double SceneScaleFactor = SceneUnit.GetScaleFactor();
	const double Tolerance = 1e-6;

	// Only convert if scale factor is significantly different from 1.0
	if ((SceneUnit != FbxSystemUnit::m) && (std::abs(SceneScaleFactor - 1.0) > Tolerance))
	{
		UE_LOG("Scene unit scale factor is %.6f, converting to meters", SceneScaleFactor);
		FbxSystemUnit::m.ConvertScene(Scene);
		UE_LOG("Converted scene unit to meters");
	}
	else
	{
		UE_LOG("Scene already has scale factor 1.0, skipping unit conversion");
	}

	// Convert axis system
	UnrealImportAxis.DeepConvertScene(Scene);
	UE_LOG("Applied DeepConvertScene to convert axis system");

	return true;
}

// Helper: Check if a node contains a skeleton attribute
bool FBXSceneUtilities::NodeContainsSkeleton(FbxNode* InNode)
{
	if (!InNode)
		return false;

	int InNodeCount = InNode->GetNodeAttributeCount();
	for (int i = 0; i < InNodeCount; ++i)
	{
		if (FbxNodeAttribute* Attribute = InNode->GetNodeAttributeByIndex(i))
		{
			if (Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
			{
				return true;
			}
		}
	}
	return false;
}

// Helper: Compute accumulated correction matrix from non-skeleton parent nodes (e.g., Armature)
// Walks up the parent chain and accumulates transforms of all non-skeleton ancestors
FbxAMatrix FBXSceneUtilities::ComputeNonSkeletonParentCorrection(FbxNode* BoneNode, const TMap<const FbxNode*, FbxAMatrix>& NonSkeletonParentTransforms)
{
	FbxAMatrix AccumulatedCorrection;
	AccumulatedCorrection.SetIdentity();

	if (!BoneNode)
		return AccumulatedCorrection;

	// Walk up the parent chain
	FbxNode* Parent = BoneNode->GetParent();
	while (Parent)
	{
		// If parent is a skeleton node, stop (we only care about non-skeleton ancestors)
		if (NodeContainsSkeleton(Parent))
		{
			break;
		}

		// Check if this non-skeleton parent has a stored transform
		const FbxAMatrix* ParentTransformPtr = NonSkeletonParentTransforms.Find(Parent);
		if (ParentTransformPtr)
		{
			// Accumulate the parent's transform (multiply on the right for local-to-global order)
			AccumulatedCorrection = (*ParentTransformPtr) * AccumulatedCorrection;
		}

		// Move to next parent
		Parent = Parent->GetParent();
	}

	return AccumulatedCorrection;
}

FbxAMatrix FBXSceneUtilities::GetBindPoseMatrix(FbxNode* Node)
{
	if (!Node)
	{
		return FbxAMatrix();
	}

	return Node->EvaluateGlobalTransform();
}

// Helper: Check if a node has skeleton nodes in its descendants (recursive check)
// Returns true if any child or descendant contains a skeleton attribute
bool FBXSceneUtilities::NodeContainsSkeletonInDescendants(FbxNode* InNode)
{
	if (!InNode)
		return false;

	// Check all children
	for (int i = 0; i < InNode->GetChildCount(); ++i)
	{
		FbxNode* ChildNode = InNode->GetChild(i);
		
		// If child itself is a skeleton, return true
		if (NodeContainsSkeleton(ChildNode))
		{
			return true;
		}
		
		// Recursively check child's descendants
		if (NodeContainsSkeletonInDescendants(ChildNode))
		{
			return true;
		}
	}

	return false;
}
