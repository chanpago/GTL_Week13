#include "pch.h"
#include "FBXSkeletonLoader.h"
#include "FBXSceneUtilities.h"

// Helper: Recursively traverse nodes until we find one with eSkeleton, skipping intermediate nodes (e.g., Armature)
void FBXSkeletonLoader::LoadSkeletonHierarchy(FbxNode* InNode, FSkeletalMeshData& MeshData, int32 ParentNodeIndex, TMap<FbxNode*, int32>& BoneToIndex)
{
	if (!InNode)
		return;

	// If this node has a skeleton attribute, start loading from here
	if (FBXSceneUtilities::NodeContainsSkeleton(InNode))
	{
		LoadSkeletonFromNode(InNode, MeshData, ParentNodeIndex, BoneToIndex);
		return;
	}

	// Otherwise, this is an intermediate node (e.g., Armature) - just skip to children
	// Note: We don't need to store Armature transform anymore since DeepConvertScene
	// converts all nodes (including Armature) to Unreal coordinate system
	FbxString NodeName = InNode->GetName();

	static bool bLoggedArmatureSkip = false;
	if (!bLoggedArmatureSkip && NodeName.CompareNoCase("Armature") == 0)
	{
		UE_LOG("UFbxLoader: Detected Armature node '%s'. Skipping to skeleton children.", NodeName.Buffer());
		bLoggedArmatureSkip = true;
	}

	// Recurse to children to find skeleton nodes
	for (int i = 0; i < InNode->GetChildCount(); ++i)
	{
		LoadSkeletonHierarchy(InNode->GetChild(i), MeshData, ParentNodeIndex, BoneToIndex);
	}
}

// Skeleton은 계층구조까지 표현해야하므로 깊이 우선 탐색, ParentNodeIndex 명시.
void FBXSkeletonLoader::LoadSkeletonFromNode(FbxNode* InNode, FSkeletalMeshData& MeshData, int32 ParentNodeIndex, TMap<FbxNode*, int32>& BoneToIndex)
{
	int32 BoneIndex = ParentNodeIndex;
	for (int Index = 0; Index < InNode->GetNodeAttributeCount(); Index++)
	{

		FbxNodeAttribute* Attribute = InNode->GetNodeAttributeByIndex(Index);
		if (!Attribute)
		{
			continue;
		}

		if (Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			FBone BoneInfo{};

			BoneInfo.Name = FString(InNode->GetName());

			BoneInfo.ParentIndex = ParentNodeIndex;

			// 뼈 리스트에 추가
			MeshData.Skeleton.Bones.Add(BoneInfo);

			// 뼈 인덱스 우리가 정해줌(방금 추가한 마지막 원소)
			BoneIndex = MeshData.Skeleton.Bones.Num() - 1;

			// 뼈 이름으로 인덱스 서치 가능하게 함.
			MeshData.Skeleton.BoneNameToIndex.Add(BoneInfo.Name, BoneIndex);

			// 매시 로드할때 써야되서 맵에 인덱스 저장
			BoneToIndex.Add(InNode, BoneIndex);
			// 뼈가 노드 하나에 여러개 있는 경우는 없음. 말이 안되는 상황임.
			break;
		}
	}
	for (int Index = 0; Index < InNode->GetChildCount(); Index++)
	{
		// 깊이 우선 탐색 부모 인덱스 설정(InNOde가 eSkeleton이 아니면 기존 부모 인덱스가 넘어감(BoneIndex = ParentNodeIndex)
		LoadSkeletonFromNode(InNode->GetChild(Index), MeshData, BoneIndex, BoneToIndex);
	}
}

void FBXSkeletonLoader::EnsureSingleRootBone(FSkeletalMeshData& MeshData)
{
	if (MeshData.Skeleton.Bones.IsEmpty())
		return;

	// 루트 본 개수 세기
	TArray<int32> RootBoneIndices;
	for (int32 i = 0; i < MeshData.Skeleton.Bones.size(); ++i)
	{
		if (MeshData.Skeleton.Bones[i].ParentIndex == -1)
		{
			RootBoneIndices.Add(i);
		}
	}

	// 루트 본이 2개 이상이면 가상 루트 생성
	if (RootBoneIndices.Num() > 1)
	{
		// 가상 루트 본 생성
		FBone VirtualRoot;
		VirtualRoot.Name = "VirtualRoot";
		VirtualRoot.ParentIndex = -1;

		// 항등 행렬로 초기화 (원점에 위치, 회전/스케일 없음)
		VirtualRoot.BindPose = FMatrix::Identity();
		VirtualRoot.InverseBindPose = FMatrix::Identity();

		// 가상 루트를 배열 맨 앞에 삽입
		MeshData.Skeleton.Bones.Insert(VirtualRoot, 0);

		// 기존 본들의 인덱스가 모두 +1 씩 밀림
		// 모든 본의 ParentIndex 업데이트
		for (int32 i = 1; i < MeshData.Skeleton.Bones.size(); ++i)
		{
			if (MeshData.Skeleton.Bones[i].ParentIndex >= 0)
			{
				MeshData.Skeleton.Bones[i].ParentIndex += 1;
			}
			else // 원래 루트 본들
			{
				MeshData.Skeleton.Bones[i].ParentIndex = 0; // 가상 루트를 부모로 설정
			}
		}

		// Vertex의 BoneIndex도 모두 +1 해줘야 함
		for (auto& Vertex : MeshData.Vertices)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				Vertex.BoneIndices[i] += 1;
			}
		}

		UE_LOG("UFbxLoader: Created virtual root bone. Found %d root bones.", RootBoneIndices.Num());
	}
}
