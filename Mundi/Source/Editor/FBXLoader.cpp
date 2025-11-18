#include "pch.h"
#include "ObjectFactory.h"
#include "FbxLoader.h"
#include "fbxsdk/fileio/fbxiosettings.h"
#include "fbxsdk/scene/geometry/fbxcluster.h"
#include "ObjectIterator.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include "PathUtils.h"
#include <filesystem>
#include "Source/Runtime/Engine/Animation/AnimSequence.h"
#include "Source/Runtime/Engine/Animation/AnimDateModel.h"

IMPLEMENT_CLASS(UFbxLoader)

UFbxLoader::UFbxLoader()
{
	// 메모리 관리, FbxManager 소멸시 Fbx 관련 오브젝트 모두 소멸
	SdkManager = FbxManager::Create();

}

void UFbxLoader::PreLoad()
{
	UFbxLoader& FbxLoader = GetInstance();

	const fs::path DataDir(GDataDir);

	if (!fs::exists(DataDir) || !fs::is_directory(DataDir))
	{
		UE_LOG("UFbxLoader::Preload: Data directory not found: %s", DataDir.string().c_str());
		return;
	}

	size_t LoadedCount = 0;
	std::unordered_set<FString> ProcessedFiles; // 중복 로딩 방지

	for (const auto& Entry : fs::recursive_directory_iterator(DataDir))
	{
		if (!Entry.is_regular_file())
			continue;

		const fs::path& Path = Entry.path();
		FString Extension = Path.extension().string();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (Extension == ".fbx")
		{
			FString PathStr = NormalizePath(Path.string());

			// 이미 처리된 파일인지 확인
			if (ProcessedFiles.find(PathStr) == ProcessedFiles.end())
			{
				ProcessedFiles.insert(PathStr);

				UE_LOG("=== Loading FBX: %s ===", PathStr.c_str());

				// LoadFbxMesh now also loads animations from the same Scene
				FbxLoader.LoadFbxMesh(PathStr);

				++LoadedCount;
			}
		}
		else if (Extension == ".dds" || Extension == ".jpg" || Extension == ".png")
		{
			UResourceManager::GetInstance().Load<UTexture>(Path.string()); // 데칼 텍스쳐를 ui에서 고를 수 있게 하기 위해 임시로 만듬.
		}
	}
	RESOURCE.SetSkeletalMeshs();

	UE_LOG("UFbxLoader::Preload: Loaded %zu .fbx files from %s", LoadedCount, DataDir.string().c_str());
}


UFbxLoader::~UFbxLoader()
{
	SdkManager->Destroy();
}
UFbxLoader& UFbxLoader::GetInstance()
{
	static UFbxLoader* FbxLoader = nullptr;
	if (!FbxLoader)
	{
		FbxLoader = ObjectFactory::NewObject<UFbxLoader>();
	}
	return *FbxLoader;
}

USkeletalMesh* UFbxLoader::LoadFbxMesh(const FString& FilePath)
{
	// 0) 경로
	FString NormalizedPathStr = NormalizePath(FilePath);

	// 1) 이미 로드된 UStaticMesh가 있는지 전체 검색 (정규화된 경로로 비교)
	for (TObjectIterator<USkeletalMesh> It; It; ++It)
	{
		USkeletalMesh* SkeletalMesh = *It;

		if (SkeletalMesh->GetFilePath() == NormalizedPathStr)
		{
			return SkeletalMesh;
		}
	}

	// 2) 없으면 새로 로드 (정규화된 경로 사용)
	// 이 부분에서 skeletalmesh의 load호출
	USkeletalMesh* SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(NormalizedPathStr);

	UE_LOG("USkeletalMesh(filename: \'%s\') is successfully crated!", NormalizedPathStr.c_str());
	return SkeletalMesh;
}

FSkeletalMeshData* UFbxLoader::LoadFbxMeshAsset(const FString& FilePath)
{
	MaterialInfos.clear();
	NonSkeletonParentTransforms.clear();
	FString NormalizedPath = NormalizePath(FilePath);
	FSkeletalMeshData* MeshData = nullptr;
#ifdef USE_OBJ_CACHE
	// 1. 캐시 파일 경로 설정
	FString CachePathStr = ConvertDataPathToCachePath(NormalizedPath);
	const FString BinPathFileName = CachePathStr + ".bin";

	// 캐시를 저장할 디렉토리가 없으면 생성
	std::filesystem::path CacheFileDirPath(BinPathFileName);
	if (CacheFileDirPath.has_parent_path())
	{
		std::filesystem::create_directories(CacheFileDirPath.parent_path());
	}

	bool bLoadedFromCache = false;


	// 2. 캐시 유효성 검사
	bool bShouldRegenerate = true;
	if (std::filesystem::exists(BinPathFileName))
	{
		try
		{
			auto binTime = std::filesystem::last_write_time(BinPathFileName);
			auto fbxTime = std::filesystem::last_write_time(NormalizedPath);

			// FBX 파일이 캐시보다 오래되었으면 캐시 사용
			if (fbxTime <= binTime)
			{
				bShouldRegenerate = false;
			}
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			UE_LOG("Filesystem error during cache validation: %s. Forcing regeneration.", e.what());
			bShouldRegenerate = true;
		}
	}

	// 3. 캐시에서 로드 시도
	if (!bShouldRegenerate)
	{
		UE_LOG("Attempting to load FBX '%s' from cache.", NormalizedPath.c_str());
		try
		{
			MeshData = new FSkeletalMeshData();
			MeshData->PathFileName = NormalizedPath;

			FWindowsBinReader Reader(BinPathFileName);
			if (!Reader.IsOpen())
			{
				throw std::runtime_error("Failed to open bin file for reading.");
			}
			Reader << *MeshData;
			Reader.Close();

			for (int Index = 0; Index < MeshData->GroupInfos.Num(); Index++)
			{
				if (MeshData->GroupInfos[Index].InitialMaterialName.empty())
					continue;
				const FString& MaterialName = MeshData->GroupInfos[Index].InitialMaterialName;
				const FString& MaterialFilePath = ConvertDataPathToCachePath(MaterialName + ".mat.bin");
				FWindowsBinReader MatReader(MaterialFilePath);
				if (!MatReader.IsOpen())
				{
					throw std::runtime_error("Failed to open material bin file for reading.");
				}
				// for bin Load
				FMaterialInfo MaterialInfo{};
				Serialization::ReadAsset<FMaterialInfo>(MatReader, &MaterialInfo);

				UMaterial* NewMaterial = NewObject<UMaterial>();

				UMaterial* Default = UResourceManager::GetInstance().GetDefaultMaterial();
				NewMaterial->SetMaterialInfo(MaterialInfo);
				NewMaterial->SetShader(Default->GetShader());
				NewMaterial->SetShaderMacros(Default->GetShaderMacros());
				UResourceManager::GetInstance().Add<UMaterial>(MaterialInfo.MaterialName, NewMaterial);
			}

			MeshData->CacheFilePath = BinPathFileName;
			bLoadedFromCache = true;

			UE_LOG("Successfully loaded FBX '%s' from cache.", NormalizedPath.c_str());

			TArray<UAnimSequence*> CachedAnimations;
			bool bLoadedAnimFromCache = TryLoadAnimationsFromCache(NormalizedPath, CachedAnimations);
			if (bLoadedAnimFromCache)
			{
				UE_LOG("Loaded %d animations from cache", CachedAnimations.Num());
			}

			// If animation cache doesn't exist or failed to load, extract from FBX
			if (!bLoadedAnimFromCache)
			{
				UE_LOG("Animation cache not found or invalid, extracting from FBX");

				FbxImporter* AnimImporter = FbxImporter::Create(SdkManager, "");
				if (AnimImporter->Initialize(NormalizedPath.c_str(), -1, SdkManager->GetIOSettings()))
				{
					FbxScene* AnimScene = FbxScene::Create(SdkManager, "AnimScene");
					AnimImporter->Import(AnimScene);
					AnimImporter->Destroy();

					ConvertSceneAxisIfNeeded(AnimScene);

					// Extract animations (ProcessAnimations will now save to cache)
					TArray<UAnimSequence*> Animations;
					ProcessAnimations(AnimScene, *MeshData, NormalizedPath, Animations);

					if (Animations.Num() > 0)
					{
						UE_LOG("Extracted %d animations from FBX: %s", Animations.Num(), NormalizedPath.c_str());
					}

					AnimScene->Destroy();
				}
			}

			return MeshData;
		}
		catch (const std::exception& e)
		{
			UE_LOG("Error loading FBX from cache: %s. Cache might be corrupt or incompatible.", e.what());
			UE_LOG("Deleting corrupt cache and forcing regeneration for '%s'.", NormalizedPath.c_str());

			std::filesystem::remove(BinPathFileName);
			if (MeshData)
			{
				delete MeshData;
				MeshData = nullptr;
			}
			bLoadedFromCache = false;
		}
	}

	// 4. 캐시 로드 실패 시 FBX 파싱
	UE_LOG("Regenerating cache for FBX '%s'...", NormalizedPath.c_str());
#endif // USE_OBJ_CACHE

	// 임포트 옵션 세팅 ( 에니메이션 임포트 여부, 머티리얼 임포트 여부 등등 IO에 대한 세팅 )
	// IOSROOT = IOSetting 객체의 기본 이름( Fbx매니저가 이름으로 관리하고 디버깅 시 매니저 내부의 객체를 이름으로 식별 가능)
	// 하지만 매니저 자체의 기본 설정이 있기 때문에 아직 안씀
	//FbxIOSettings* IoSetting = FbxIOSettings::Create(SdkManager, IOSROOT);
	//SdkManager->SetIOSettings(IoSetting);

	// 로드할때마다 임포터 생성
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");

	// 원하는 IO 세팅과 Fbx파일로 Importer initialize
	if (!Importer->Initialize(NormalizedPath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Call to FbxImporter::Initialize() Falied\n");
		UE_LOG("[FbxImporter::Initialize()] Error Reports: %s\n\n", Importer->GetStatus().GetErrorString());
		return nullptr;
	}

	// 임포터가 씬에 데이터를 채워줄 것임. 이름은 IoSetting과 마찬가지로 매니저가 이름으로 관리하고 Export될 때 표시할 씬 이름.
	FbxScene* Scene = FbxScene::Create(SdkManager, "My Scene");

	// 임포트해서 Fbx를 씬에 넣어줌
	// 여기서 FBX 파일의 모든 데이터가 Scene에 로드됨!
	Importer->Import(Scene);

	// 임포트 후 제거
	Importer->Destroy();

	FbxAxisSystem UnrealImportAxis(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
	FbxAxisSystem SourceSetup = Scene->GetGlobalSettings().GetAxisSystem();
	FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
	double SceneScaleFactor = SceneUnit.GetScaleFactor();

	FbxSystemUnit::m.ConvertScene(Scene);

	if (SourceSetup != UnrealImportAxis)
	{
		UE_LOG("Fbx 축 변환 완료!\n");
		UnrealImportAxis.DeepConvertScene(Scene);
	}

	// 매시의 폴리곤이 삼각형이 아닐 수가 있음, Converter가 모두 삼각화해줌.
	FbxGeometryConverter IGeometryConverter(SdkManager);
	if (IGeometryConverter.Triangulate(Scene, true))
	{
		UE_LOG("Fbx 씬 삼각화 완료\n");
	}
	else
	{
		UE_LOG("Fbx 씬 삼각화가 실패했습니다, 매시가 깨질 수 있습니다\n");
	}

	MeshData = new FSkeletalMeshData();
	MeshData->PathFileName = NormalizedPath;

	// Fbx파일에 씬은 하나만 존재하고 씬에 매쉬, 라이트, 카메라 등등의 element들이 트리 구조로 저장되어 있음.
	// 씬 Export 시에 루트 노드 말고 Child 노드만 저장됨. 노드 하나가 여러 Element를 저장할 수 있고 Element는 FbxNodeAttribute 클래스로 정의되어 있음.
	// 루트 노드 얻어옴
	FbxNode* RootNode = Scene->GetRootNode();

	// 뼈의 인덱스를 부여, 기본적으로 Fbx는 정점이 아니라 뼈 중심으로 데이터가 저장되어 있음(뼈가 몇번 ControlPoint에 영향을 주는지)
	// 정점이 몇번 뼈의 영향을 받는지 표현하려면 직접 뼈 인덱스를 만들어야함.
	TMap<FbxNode*, int32> BoneToIndex;

	// 머티리얼도 인덱스가 따로 없어서(있긴 한데 순서가 뒤죽박죽이고 모든 노드를 순회하기 전까지 머티리얼 개수를 몰라서) 직접 만들어줘야함
	// FbxSurfaceMaterial : 진짜 머티리얼 , FbxGeometryElementMaterial : 인덱싱용, 폴리곤이 어떤 머티리얼 슬롯을 쓰는지 알려줌
	TMap<FbxSurfaceMaterial*, int32> MaterialToIndex;

	// 머티리얼마다 정점 Index를 할당해 줄 것임. 머티리얼 정렬을 해놔야 렌더링 비용이 적게 드니까. 
	// 최종적으로 이 맵의 Array들을 합쳐서 MeshData.Indices에 저장하고 GroupInfos를 채워줄 것임.
	TMap<int32, TArray<uint32>> MaterialGroupIndexList;

	// 머티리얼이 어떤 노드는 있는데 어떤 노드는 없을 수 있음, 이때 머티리얼이 없는 노드의 Index를 저장해놓고 나중에 머티리얼이 있는 노드의 index를
	// 그룹별 정렬하게 되면 인덱스가 꼬임. 그래서 머티리얼이 없는 경우 그냥 0번 그룹을 쓰도록 하고 머티리얼 인덱스 0번을 nullptr에 할당함
	MaterialGroupIndexList.Add(0, TArray<uint32>());
	MaterialToIndex.Add(nullptr, 0);
	MeshData->GroupInfos.Add(FGroupInfo());
	if (RootNode)
	{
		// 2번의 패스로 나눠서 처음엔 뼈의 인덱스를 결정하고 2번째 패스에서 뼈가 영향을 미치는 정점들을 구하고 정점마다 뼈 인덱스를 할당해 줄 것임(동시에 TPose 역행렬도 구함)
		for (int Index = 0; Index < RootNode->GetChildCount(); Index++)
		{
			LoadSkeletonHierarchy(RootNode->GetChild(Index), *MeshData, -1, BoneToIndex);
		}
		for (int Index = 0; Index < RootNode->GetChildCount(); Index++)
		{
			LoadMeshFromNode(RootNode->GetChild(Index), *MeshData, MaterialGroupIndexList, BoneToIndex, MaterialToIndex);
		}

		// 여러 루트 본이 있으면 가상 루트 생성
		EnsureSingleRootBone(*MeshData);
	}

	// 머티리얼이 있는 경우 플래그 설정
	if (MeshData->GroupInfos.Num() > 1)
	{
		MeshData->bHasMaterial = true;
	}
	// 있든없든 항상 기본 머티리얼 포함 머티리얼 그룹별로 인덱스 저장하므로 Append 해줘야함
	uint32 Count = 0;
	for (auto& Element : MaterialGroupIndexList)
	{
		int32 MatrialIndex = Element.first;

		const TArray<uint32>& IndexList = Element.second;

		// 최종 인덱스 배열에 리스트 추가
		MeshData->Indices.Append(IndexList);

		// 그룹info에 Startindex와 Count 추가
		MeshData->GroupInfos[MatrialIndex].StartIndex = Count;
		MeshData->GroupInfos[MatrialIndex].IndexCount = IndexList.Num();
		Count += IndexList.Num();
	}

#ifdef USE_OBJ_CACHE
	// 5. 캐시 저장
	try
	{
		FWindowsBinWriter Writer(BinPathFileName);
		Writer << *MeshData;
		Writer.Close();

		for (FMaterialInfo& MaterialInfo : MaterialInfos)
		{

			const FString MaterialFilePath = ConvertDataPathToCachePath(MaterialInfo.MaterialName + ".mat.bin");
			FWindowsBinWriter MatWriter(MaterialFilePath);
			Serialization::WriteAsset<FMaterialInfo>(MatWriter, &MaterialInfo);
			MatWriter.Close();
		}

		MeshData->CacheFilePath = BinPathFileName;

		UE_LOG("Cache regeneration complete for FBX '%s'.", NormalizedPath.c_str());
	}
	catch (const std::exception& e)
	{
		UE_LOG("Failed to save FBX cache: %s", e.what());
	}
#endif // USE_OBJ_CACHE

	// Extract animations from the same Scene (same unit conversion & axis)
	TArray<UAnimSequence*> Animations;
	ProcessAnimations(Scene, *MeshData, NormalizedPath, Animations);

	if (Animations.Num() > 0)
	{
		UE_LOG("Loaded %d animations from the same Scene: %s", Animations.Num(), NormalizedPath.c_str());
	}

	// Cleanup Scene after everything is done
	Scene->Destroy();

	return MeshData;
}


void UFbxLoader::LoadMeshFromNode(FbxNode* InNode,
	FSkeletalMeshData& MeshData,
	TMap<int32, TArray<uint32>>& MaterialGroupIndexList,
	TMap<FbxNode*, int32>& BoneToIndex,
	TMap<FbxSurfaceMaterial*, int32>& MaterialToIndex)
{

	// 부모노드로부터 상대좌표 리턴
	/*FbxDouble3 Translation = InNode->LclTranslation.Get();
	FbxDouble3 Rotation = InNode->LclRotation.Get();
	FbxDouble3 Scaling  = InNode->LclScaling.Get();*/

	// 최적화, 메시 로드 전에 미리 머티리얼로부터 인덱스를 해시맵을 이용해서 얻고 그걸 TArray에 저장하면 됨. 
	// 노드의 머티리얼 리스트는 슬롯으로 참조함(내가 정한 MaterialIndex는 슬롯과 다름), 슬롯에 대응하는 머티리얼 인덱스를 캐싱하는 것
	// 그럼 폴리곤 순회하면서 해싱할 필요가 없음
	TArray<int32> MaterialSlotToIndex;
	// Attribute 참조 함수
	for (int Index = 0; Index < InNode->GetNodeAttributeCount(); Index++)
	{
		FbxNodeAttribute* Attribute = InNode->GetNodeAttributeByIndex(Index);
		if (!Attribute)
		{
			continue;
		}

		if (Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			FbxMesh* Mesh = (FbxMesh*)Attribute;
			// 위의 MaterialSlotToIndex는 MaterialToIndex 해싱을 안 하기 위함이고, MaterialGroupIndexList도 머티리얼이 없거나 1개만 쓰는 경우 해싱을 피할 수 있음.
			// 이를 위한 최적화 코드를 작성함.


			// 0번이 기본 머티리얼이고 1번 이상은 블렌딩 머티리얼이라고 함. 근데 엄청 고급 기능이라서 일반적인 로더는 0번만 쓴다고 함.
			if (Mesh->GetElementMaterialCount() > 0)
			{
				// 머티리얼 슬롯 인덱싱 해주는 클래스 (ex. materialElement->GetIndexArray() : 폴리곤마다 어떤 머티리얼 슬롯을 쓰는지 Array로 표현)
				FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial(0);
				// 머티리얼이 폴리곤 단위로 매핑함 -> 모든 폴리곤이 같은 머티리얼을 쓰지 않음(같은 머티리얼을 쓰는 경우 = eAllSame)
				// MaterialCount랑은 전혀 다른 동작임(슬롯이 2개 이상 있어도 매핑 모드가 eAllSame이라서 머티리얼을 하나만 쓰는 경우가 있음)
				if (MaterialElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
				{
					for (int MaterialSlot = 0; MaterialSlot < InNode->GetMaterialCount(); MaterialSlot++)
					{
						int MaterialIndex = 0;
						FbxSurfaceMaterial* Material = InNode->GetMaterial(MaterialSlot);
						if (MaterialToIndex.Contains(Material))
						{
							MaterialIndex = MaterialToIndex[Material];
						}
						else
						{
							FMaterialInfo MaterialInfo{};
							ParseMaterial(Material, MaterialInfo);
							// 새로운 머티리얼, 머티리얼 인덱스 설정
							MaterialIndex = MaterialToIndex.Num();
							MaterialToIndex.Add(Material, MaterialIndex);
							MeshData.GroupInfos.Add(FGroupInfo());
							MeshData.GroupInfos[MaterialIndex].InitialMaterialName = MaterialInfo.MaterialName;
						}
						// MaterialSlot에 대응하는 전역 MaterialIndex 저장
						MaterialSlotToIndex.Add(MaterialIndex);
					}
				}
				// 노드가 하나의 머티리얼만 쓰는 경우
				else if (MaterialElement->GetMappingMode() == FbxGeometryElement::eAllSame)
				{
					int MaterialIndex = 0;
					int MaterialSlot = MaterialElement->GetIndexArray().GetAt(0);
					FbxSurfaceMaterial* Material = InNode->GetMaterial(MaterialSlot);
					if (MaterialToIndex.Contains(Material))
					{
						MaterialIndex = MaterialToIndex[Material];
					}
					else
					{
						FMaterialInfo MaterialInfo{};
						ParseMaterial(Material, MaterialInfo);
						// 새로운 머티리얼, 머티리얼 인덱스 설정
						MaterialIndex = MaterialToIndex.Num();

						MaterialToIndex.Add(Material, MaterialIndex);
						MeshData.GroupInfos.Add(FGroupInfo());
						MeshData.GroupInfos[MaterialIndex].InitialMaterialName = MaterialInfo.MaterialName;
					}
					// MaterialSlotToIndex에 추가할 필요 없음(머티리얼 하나일때 해싱 패스하고 Material Index로 바로 그룹핑 할 거라서 안 씀)
					LoadMesh(Mesh, MeshData, MaterialGroupIndexList, BoneToIndex, MaterialSlotToIndex, MaterialIndex);
					continue;
				}
			}

			LoadMesh(Mesh, MeshData, MaterialGroupIndexList, BoneToIndex, MaterialSlotToIndex);
		}
	}

	for (int Index = 0; Index < InNode->GetChildCount(); Index++)
	{
		LoadMeshFromNode(InNode->GetChild(Index), MeshData, MaterialGroupIndexList, BoneToIndex, MaterialToIndex);
	}
}

// Helper: Check if scene needs axis conversion and apply it conditionally
// Returns true if conversion was applied, false if scene was already in Unreal axis system
bool UFbxLoader::ConvertSceneAxisIfNeeded(FbxScene* Scene)
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
bool UFbxLoader::NodeContainsSkeleton(FbxNode* InNode) const
{
	if (!InNode)
		return false;

	for (int i = 0; i < InNode->GetNodeAttributeCount(); ++i)
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
FbxAMatrix UFbxLoader::ComputeNonSkeletonParentCorrection(FbxNode* BoneNode) const
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

// Helper: Recursively traverse nodes until we find one with eSkeleton, skipping intermediate nodes (e.g., Armature)
void UFbxLoader::LoadSkeletonHierarchy(FbxNode* InNode, FSkeletalMeshData& MeshData, int32 ParentNodeIndex, TMap<FbxNode*, int32>& BoneToIndex)
{
	if (!InNode)
		return;

	// If this node has a skeleton attribute, start loading from here
	if (NodeContainsSkeleton(InNode))
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
void UFbxLoader::LoadSkeletonFromNode(FbxNode* InNode, FSkeletalMeshData& MeshData, int32 ParentNodeIndex, TMap<FbxNode*, int32>& BoneToIndex)
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

// 예시 코드
void UFbxLoader::LoadMeshFromAttribute(FbxNodeAttribute* InAttribute, FSkeletalMeshData& MeshData)
{

	/*if (!InAttribute)
	{
		return;
	}*/
	//FbxString TypeName = GetAttributeTypeName(InAttribute);
	// 타입과 별개로 Element 자체의 이름도 있음
	//FbxString AttributeName = InAttribute->GetName();

	// Buffer함수로 FbxString->char* 변환
	//UE_LOG("<Attribute Type = %s, Name = %s\n", TypeName.Buffer(), AttributeName.Buffer());
}

void UFbxLoader::LoadMesh(FbxMesh* InMesh, FSkeletalMeshData& MeshData, TMap<int32, TArray<uint32>>& MaterialGroupIndexList, TMap<FbxNode*, int32>& BoneToIndex, TArray<int32> MaterialSlotToIndex, int32 DefaultMaterialIndex)
{
	// 위에서 뼈 인덱스를 구했으므로 일단 ControlPoint에 대응되는 뼈 인덱스와 가중치부터 할당할 것임(이후 MeshData를 채우면서 ControlPoint를 순회할 것이므로)
	struct IndexWeight
	{
		uint32 BoneIndex;
		float BoneWeight;
	};
	// ControlPoint에 대응하는 뼈 인덱스, 가중치를 저장하는 맵
	// ControlPoint에 대응하는 뼈가 여러개일 수 있으므로 TArray로 저장
	TMap<int32, TArray<IndexWeight>> ControlPointToBoneWeight;
	// 메시 로컬 좌표계를 Fbx Scene World 좌표계로 바꿔주는 행렬
	FbxAMatrix FbxSceneWorld{};
	// 역전치(노말용)
	FbxAMatrix FbxSceneWorldInverseTranspose{};

	// Deformer: 매시의 모양을 변형시키는 모든 기능, ex) skin, blendShape(모프 타겟, 두 표정 미리 만들고 블랜딩해서 서서히 변화시킴)
	// 99.9퍼센트는 스킨이 하나만 있고 완전 복잡한 얼굴 표정을 표현하기 위해서 2개 이상을 쓰기도 하는데 0번만 쓰도록 해도 문제 없음(AAA급 게임에서 2개 이상을 처리함)
	// 2개 이상의 스킨이 들어가면 뼈 인덱스가 16개까지도 늘어남. 
	if (InMesh->GetDeformerCount(FbxDeformer::eSkin) > 0)
	{
		// 클러스터: 뼈라고 봐도 됨(뼈 정보와(Bind Pose 행렬) 그 뼈가 영향을 주는 정점, 가중치 저장)
		for (int Index = 0; Index < ((FbxSkin*)InMesh->GetDeformer(0, FbxDeformer::eSkin))->GetClusterCount(); Index++)
		{
			FbxCluster* Cluster = ((FbxSkin*)InMesh->GetDeformer(0, FbxDeformer::eSkin))->GetCluster(Index);

			if (Index == 0)
			{
				// 클러스터를 담고 있는 Node의(Mesh) Fbx Scene World Transform, 한 번만 구해도 되고 
				// 정점을 Fbx Scene World 좌표계로 저장하기 위해 사용(아티스트 의도를 그대로 반영 가능, 서브메시를 단일메시로 처리 가능)
				// 모든 SkeletalMesh는 Scene World 원점을 기준으로 제작되어야함
				Cluster->GetTransformMatrix(FbxSceneWorld);
				FbxSceneWorldInverseTranspose = FbxSceneWorld.Inverse().Transpose();
			}
			int IndexCount = Cluster->GetControlPointIndicesCount();
			// 클러스터가 영향을 주는 ControlPointIndex를 구함.
			int* Indices = Cluster->GetControlPointIndices();
			double* Weights = Cluster->GetControlPointWeights();
			// Bind Pose, Inverse Bind Pose 저장.
			// Fbx 스킨 규약:
			//  - TransformLinkMatrix: 본의 글로벌 바인드 행렬
			//  - TransformMatrix:     메시의 글로벌 바인드 행렬
			// 엔진 로컬(메시 기준) 바인드 행렬 = Inv(TransformMatrix) * TransformLinkMatrix
			FbxAMatrix BoneBindGlobal;
			Cluster->GetTransformLinkMatrix(BoneBindGlobal);
			FbxAMatrix BoneBindGlobalInv = BoneBindGlobal.Inverse();
			// FbxMatrix는 128바이트, FMatrix는 64바이트라서 memcpy쓰면 안 됨. 원소 단위로 복사합니다.
			for (int Row = 0; Row < 4; Row++)
			{
				for (int Col = 0; Col < 4; Col++)
				{
					MeshData.Skeleton.Bones[BoneToIndex[Cluster->GetLink()]].BindPose.M[Row][Col] = static_cast<float>(BoneBindGlobal[Row][Col]);
					MeshData.Skeleton.Bones[BoneToIndex[Cluster->GetLink()]].InverseBindPose.M[Row][Col] = static_cast<float>(BoneBindGlobalInv[Row][Col]);
				}
			}


			for (int ControlPointIndex = 0; ControlPointIndex < IndexCount; ControlPointIndex++)
			{
				// GetLink -> 아까 저장한 노드 To Index맵의 노드 (Cluster에 대응되는 뼈 인덱스를 ControlPointIndex에 대응시키는 과정)
				// ControlPointIndex = 클러스터가 저장하는 ControlPointIndex 배열에 대한 Index
				TArray<IndexWeight>& IndexWeightArray = ControlPointToBoneWeight[Indices[ControlPointIndex]];
				IndexWeightArray.Add(IndexWeight(BoneToIndex[Cluster->GetLink()], Weights[ControlPointIndex]));
			}
		}
	}

	bool bIsUniformScale = false;
	const FbxVector4& ScaleOfSceneWorld = FbxSceneWorld.GetS();
	// 비균등 스케일일 경우 그람슈미트 이용해서 탄젠트 재계산
	bIsUniformScale = ((FMath::Abs(ScaleOfSceneWorld[0] - ScaleOfSceneWorld[1]) < 0.001f) &&
		(FMath::Abs(ScaleOfSceneWorld[0] - ScaleOfSceneWorld[2]) < 0.001f));


	// 로드는 TriangleList를 가정하고 할 것임. 
	// TriangleStrip은 한번 만들면 편집이 사실상 불가능함, Fbx같은 호환성이 중요한 모델링 포멧이 유연성 부족한 모델을 저장할 이유도 없고
	// 엔진 최적화 측면에서도 GPU의 Vertex Cache가 Strip과 비슷한 성능을 내면서도 직관적이고 유연해서 잘 쓰지도 않기 때문에 그냥 안 씀.
	int PolygonCount = InMesh->GetPolygonCount();

	// ControlPoints는 정점의 위치 정보를 배열로 저장함, Vertex마다 ControlIndex로 참조함.
	FbxVector4* ControlPoints = InMesh->GetControlPoints();


	// Vertex 위치가 같아도 서로 다른 Normal, Tangent, UV좌표를 가질 수 있음, Fbx는 하나의 인덱스 배열에서 이들을 서로 다른 인덱스로 관리하길 강제하지 않고 
	// Vertex 위치는 ControlPoint로 관리하고 그 외의 정보들은 선택적으로 분리해서 관리하도록 함. 그래서 ControlPoint를 Index로 쓸 수도 없어서 따로 만들어야 하고, 
	// 위치정보 외의 정보를 참조할때는 매핑 방식별로 분기해서 저장해야함. 만약 매핑 방식이 eByPolygonVertex(꼭짓점 기준)인 경우 폴리곤의 꼭짓점을 순회하는 순서
	// 그대로 참조하면 됨, 그래서 VertexId를 꼭짓점 순회하는 순서대로 증가시키면서 매핑할 것임.
	int VertexId = 0;

	// 위의 이유로 ControlPoint를 인덱스 버퍼로 쓸 수가 없어서 Vertex마다 대응되는 Index Map을 따로 만들어서 계산할 것임.
	TMap<FSkinnedVertex, uint32> IndexMap;


	for (int PolygonIndex = 0; PolygonIndex < PolygonCount; PolygonIndex++)
	{
		// 최종적으로 사용할 머티리얼 인덱스를 구함, MaterialIndex 기본값이 0이므로 없는 경우 처리됨, 머티리얼이 하나일때 materialIndex가 1 이상이므로 처리됨.
		// 머티리얼이 여러개일때만 처리해주면 됌.

		// 머티리얼이 여러개인 경우(머티리얼이 하나 이상 있는데 materialIndex가 0이면 여러개, 하나일때는 MaterialIndex를 설정해주니까)
		// 이때는 해싱을 해줘야함
		int32 MaterialIndex = DefaultMaterialIndex;
		if (DefaultMaterialIndex == 0 && InMesh->GetElementMaterialCount() > 0)
		{
			FbxGeometryElementMaterial* Material = InMesh->GetElementMaterial(0);
			int MaterialSlot = Material->GetIndexArray().GetAt(PolygonIndex);
			MaterialIndex = MaterialSlotToIndex[MaterialSlot];
		}

		// 하나의 Polygon 내에서의 VertexIndex, PolygonSize가 다를 수 있지만 위에서 삼각화를 해줬기 때문에 무조건 3임
		for (int VertexIndex = 0; VertexIndex < InMesh->GetPolygonSize(PolygonIndex); VertexIndex++)
		{
			FSkinnedVertex SkinnedVertex{};
			// 폴리곤 인덱스와 폴리곤 내에서의 vertexIndex로 ControlPointIndex 얻어냄
			int ControlPointIndex = InMesh->GetPolygonVertex(PolygonIndex, VertexIndex);

			const FbxVector4& Position = FbxSceneWorld.MultT(ControlPoints[ControlPointIndex]);
			//const FbxVector4& Position = ControlPoints[ControlPointIndex];
			SkinnedVertex.Position = FVector(Position.mData[0], Position.mData[1], Position.mData[2]);


			if (ControlPointToBoneWeight.Contains(ControlPointIndex))
			{
				double TotalWeights = 0.0;


				const TArray<IndexWeight>& WeightArray = ControlPointToBoneWeight[ControlPointIndex];
				for (int BoneIndex = 0; BoneIndex < WeightArray.Num() && BoneIndex < 4; BoneIndex++)
				{
					// Total weight 구하기(정규화)
					TotalWeights += ControlPointToBoneWeight[ControlPointIndex][BoneIndex].BoneWeight;
				}
				// 5개 이상이 있어도 4개만 처리할 것임.
				for (int BoneIndex = 0; BoneIndex < WeightArray.Num() && BoneIndex < 4; BoneIndex++)
				{
					// ControlPoint에 대응하는 뼈 인덱스, 가중치를 모두 저장
					SkinnedVertex.BoneIndices[BoneIndex] = ControlPointToBoneWeight[ControlPointIndex][BoneIndex].BoneIndex;
					SkinnedVertex.BoneWeights[BoneIndex] = ControlPointToBoneWeight[ControlPointIndex][BoneIndex].BoneWeight / TotalWeights;
				}
			}


			// 함수명과 다르게 매시가 가진 버텍스 컬러 레이어 개수를 리턴함.( 0번 : Diffuse, 1번 : 블랜딩 마스크 , 기타..)
			// 엔진에서는 항상 0번만 사용하거나 Count가 0임. 그래서 하나라도 있으면 그냥 0번 쓰게 함.
			// 왜 이렇게 지어졌나? -> Fbx가 3D 모델링 관점에서 만들어졌기 때문, 모델링 툴에서는 여러 개의 컬러 레이어를 하나에 매시에 만들 수 있음.
			// 컬러 뿐만 아니라 UV Normal Tangent 모두 다 레이어로 저장하고 모두 다 0번만 쓰면 됨.
			if (InMesh->GetElementVertexColorCount() > 0)
			{
				// 왜 FbxLayerElement를 안 쓰지? -> 구버전 API
				FbxGeometryElementVertexColor* VertexColors = InMesh->GetElementVertexColor(0);
				int MappingIndex;
				// 확장성을 고려하여 switch를 씀, ControlPoint와 PolygonVertex말고 다른 모드들도 있음.
				switch (VertexColors->GetMappingMode())
				{
				case FbxGeometryElement::eByPolygon: //다른 모드 예시
				case FbxGeometryElement::eAllSame:
				case FbxGeometryElement::eNone:
				default:
					break;
					// 가장 단순한 경우, 그냥 ControlPoint(Vertex의 위치)마다 하나의 컬러값을 저장.
				case FbxGeometryElement::eByControlPoint:
					MappingIndex = ControlPointIndex;
					break;
					// 꼭짓점마다 컬러가 저장된 경우(같은 위치여도 다른 컬러 저장 가능), 위와 같지만 꼭짓점마다 할당되는 VertexId를 씀.
				case FbxGeometryElement::eByPolygonVertex:
					MappingIndex = VertexId;
					break;
				}

				// 매핑 방식에 더해서, 실제로 그 ControlPoint에서 어떻게 참조할 것인지가 다를 수 있음.(데이터 압축때문에 필요, IndexBuffer를 쓰는 것과 비슷함)
				switch (VertexColors->GetReferenceMode())
				{
					// 인덱스 자체가 데이터 배열의 인덱스인 경우(중복이 생길 수 있음)
				case FbxGeometryElement::eDirect:
				{
					// 바로 참조 가능.
					const FbxColor& Color = VertexColors->GetDirectArray().GetAt(MappingIndex);
					SkinnedVertex.Color = FVector4(Color.mRed, Color.mGreen, Color.mBlue, Color.mAlpha);
				}
				break;
				//인덱스 배열로 간접참조해야함
				case FbxGeometryElement::eIndexToDirect:
				{
					int Id = VertexColors->GetIndexArray().GetAt(MappingIndex);
					const FbxColor& Color = VertexColors->GetDirectArray().GetAt(Id);
					SkinnedVertex.Color = FVector4(Color.mRed, Color.mGreen, Color.mBlue, Color.mAlpha);
				}
				break;
				//외의 경우는 일단 배제
				default:
					break;
				}
			}

			if (InMesh->GetElementNormalCount() > 0)
			{
				FbxGeometryElementNormal* Normals = InMesh->GetElementNormal(0);

				// 각진 모서리 표현력 때문에 99퍼센트의 모델은 eByPolygonVertex를 쓴다고 함.
				// 근데 구 같이 각진 모서리가 아예 없는 경우, 부드러운 셰이딩 모델을 익스포트해서 eControlPoint로 저장될 수도 있음
				int MappingIndex;

				switch (Normals->GetMappingMode())
				{
				case FbxGeometryElement::eByControlPoint:
					MappingIndex = ControlPointIndex;
					break;
				case FbxGeometryElement::eByPolygonVertex:
					MappingIndex = VertexId;
					break;
				default:
					break;
				}

				switch (Normals->GetReferenceMode())
				{
				case FbxGeometryElement::eDirect:
				{
					const FbxVector4& Normal = Normals->GetDirectArray().GetAt(MappingIndex);
					FbxVector4 NormalWorld = FbxSceneWorldInverseTranspose.MultT(FbxVector4(Normal.mData[0], Normal.mData[1], Normal.mData[2], 0.0f));
					SkinnedVertex.Normal = FVector(NormalWorld.mData[0], NormalWorld.mData[1], NormalWorld.mData[2]);
				}
				break;
				case FbxGeometryElement::eIndexToDirect:
				{
					int Id = Normals->GetIndexArray().GetAt(MappingIndex);
					const FbxVector4& Normal = Normals->GetDirectArray().GetAt(Id);
					FbxVector4 NormalWorld = FbxSceneWorldInverseTranspose.MultT(FbxVector4(Normal.mData[0], Normal.mData[1], Normal.mData[2], 0.0f));
					SkinnedVertex.Normal = FVector(NormalWorld.mData[0], NormalWorld.mData[1], NormalWorld.mData[2]);
				}
				break;
				default:
					break;
				}
			}

			if (InMesh->GetElementTangentCount() > 0)
			{
				FbxGeometryElementTangent* Tangents = InMesh->GetElementTangent(0);

				// 왜 Color에서 계산한 Mapping Index를 안 쓰지? -> 컬러, 탄젠트, 노말, UV 모두 다 다른 매핑 방식을 사용 가능함.
				int MappingIndex;

				switch (Tangents->GetMappingMode())
				{
				case FbxGeometryElement::eByControlPoint:
					MappingIndex = ControlPointIndex;
					break;
				case FbxGeometryElement::eByPolygonVertex:
					MappingIndex = VertexId;
					break;
				default:
					break;
				}

				switch (Tangents->GetReferenceMode())
				{
				case FbxGeometryElement::eDirect:
				{
					const FbxVector4& Tangent = Tangents->GetDirectArray().GetAt(MappingIndex);
					FbxVector4 TangentWorld = FbxSceneWorld.MultT(FbxVector4(Tangent.mData[0], Tangent.mData[1], Tangent.mData[2], 0.0f));
					SkinnedVertex.Tangent = FVector4(TangentWorld.mData[0], TangentWorld.mData[1], TangentWorld.mData[2], Tangent.mData[3]);
				}
				break;
				case FbxGeometryElement::eIndexToDirect:
				{
					int Id = Tangents->GetIndexArray().GetAt(MappingIndex);
					const FbxVector4& Tangent = Tangents->GetDirectArray().GetAt(Id);
					FbxVector4 TangentWorld = FbxSceneWorld.MultT(FbxVector4(Tangent.mData[0], Tangent.mData[1], Tangent.mData[2], 0.0f));
					SkinnedVertex.Tangent = FVector4(TangentWorld.mData[0], TangentWorld.mData[1], TangentWorld.mData[2], Tangent.mData[3]);
				}
				break;
				default:
					break;
				}

				// 유니폼 스케일이 아니므로 그람슈미트, 노말이 필요하므로 노말 이후에 탄젠트 계산해야함
				if (!bIsUniformScale)
				{
					FVector Tangent = FVector(SkinnedVertex.Tangent.X, SkinnedVertex.Tangent.Y, SkinnedVertex.Tangent.Z);
					float Handedness = SkinnedVertex.Tangent.W;
					const FVector& Normal = SkinnedVertex.Normal;

					float TangentToNormalDir = FVector::Dot(Tangent, Normal);

					Tangent = Tangent - Normal * TangentToNormalDir;
					Tangent.Normalize();
					SkinnedVertex.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, Handedness);
				}

			}

			// UV는 매핑 방식이 위와 다름(eByPolygonVertex에서 VertexId를 안 쓰고 TextureUvIndex를 씀, 참조방식도 위와 다름.)
			// 이유 : 3D 모델의 부드러운 면에 2D 텍스처 매핑을 위해 제봉선(가상의)을 만드는 경우가 생김, 그때 하나의 VertexId가 그 제봉선을 경계로
			//		  서로 다른 uv 좌표를 가져야 할 때가 생김. 그냥 VertexId를 더 나누면 안되나? => 아티스트가 싫어하고 직관적이지도 않음, 실제로 
			//		  물리적으로 폴리곤이 찢어진 게 아닌데 텍스처를 입히겠다고 Vertex를 새로 만들고 폴리곤을 찢어야 함.
			//		  그래서 UV는 인덱싱을 나머지와 다르게함
			if (InMesh->GetElementUVCount() > 0)
			{
				FbxGeometryElementUV* UVs = InMesh->GetElementUV(0);

				switch (UVs->GetMappingMode())
				{
				case FbxGeometryElement::eByControlPoint:
					switch (UVs->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
					{
						const FbxVector2& UV = UVs->GetDirectArray().GetAt(ControlPointIndex);
						SkinnedVertex.UV = FVector2D(UV.mData[0], 1 - UV.mData[1]);
					}
					break;
					case FbxGeometryElement::eIndexToDirect:
					{
						int Id = UVs->GetIndexArray().GetAt(ControlPointIndex);
						const FbxVector2& UV = UVs->GetDirectArray().GetAt(Id);
						SkinnedVertex.UV = FVector2D(UV.mData[0], 1 - UV.mData[1]);
					}
					break;
					default:
						break;
					}
					break;
				case FbxGeometryElement::eByPolygonVertex:
				{
					int TextureUvIndex = InMesh->GetTextureUVIndex(PolygonIndex, VertexIndex);
					switch (UVs->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
					case FbxGeometryElement::eIndexToDirect:
					{
						const FbxVector2& UV = UVs->GetDirectArray().GetAt(TextureUvIndex);
						SkinnedVertex.UV = FVector2D(UV.mData[0], 1 - UV.mData[1]);
					}
					break;
					default:
						break;
					}
				}
				break;
				default:
					break;
				}
			}

			// 실제 인덱스 버퍼에서 사용할 인덱스
			uint32 IndexOfVertex;
			// 기존의 Vertex맵에 있으면 그 인덱스를 사용
			if (IndexMap.Contains(SkinnedVertex))
			{
				IndexOfVertex = IndexMap[SkinnedVertex];
			}
			else
			{
				// 없으면 Vertex 리스트에 추가하고 마지막 원소 인덱스를 사용
				MeshData.Vertices.Add(SkinnedVertex);
				IndexOfVertex = MeshData.Vertices.Num() - 1;

				// 인덱스 맵에 추가
				IndexMap.Add(SkinnedVertex, IndexOfVertex);
			}
			// 대응하는 머티리얼 인덱스 리스트에 추가
			TArray<uint32>& GroupIndexList = MaterialGroupIndexList[MaterialIndex];
			GroupIndexList.Add(IndexOfVertex);

			// 인덱스 리스트에 최종 인덱스 추가(Vertex 리스트와 대응)
			// 머티리얼 사용하면서 필요 없어짐.(머티리얼 소팅 후 한번에 복사할거임)
			//MeshData.Indices.Add(IndexOfVertex);

			// Vertex 하나 저장했고 Vertex마다 Id를 사용하므로 +1
			VertexId++;
		} // for PolygonSize
	} // for PolygonCount



	// FBX에 정점의 탄젠트 벡터가 존재하지 않을 시
	if (InMesh->GetElementTangentCount() == 0)
	{
		// 1. 계산된 탄젠트와 바이탄젠트(Bitangent)를 누적할 임시 저장소를 만듭니다.
		// MeshData.Vertices에 이미 중복 제거된 유일한 정점들이 들어있습니다.
		TArray<FVector> TempTangents(MeshData.Vertices.Num());
		TArray<FVector> TempBitangents(MeshData.Vertices.Num());

		// 2. 모든 머티리얼 그룹의 인덱스 리스트를 순회합니다.
		for (auto& Elem : MaterialGroupIndexList)
		{
			TArray<uint32>& GroupIndexList = Elem.second;

			// 인덱스 리스트를 3개씩(트라이앵글 단위로) 순회합니다.
			for (int32 i = 0; i < GroupIndexList.Num(); i += 3)
			{
				uint32 i0 = GroupIndexList[i];
				uint32 i1 = GroupIndexList[i + 1];
				uint32 i2 = GroupIndexList[i + 2];

				// 트라이앵글을 구성하는 3개의 정점 데이터를 가져옵니다.
				// 이 정점들은 MeshData.Vertices에 있는 *유일한* 정점입니다.
				const FSkinnedVertex& v0 = MeshData.Vertices[i0];
				const FSkinnedVertex& v1 = MeshData.Vertices[i1];
				const FSkinnedVertex& v2 = MeshData.Vertices[i2];

				// 위치(P)와 UV(W)를 가져옵니다.
				const FVector& P0 = v0.Position;
				const FVector& P1 = v1.Position;
				const FVector& P2 = v2.Position;

				const FVector2D& W0 = v0.UV;
				const FVector2D& W1 = v1.UV;
				const FVector2D& W2 = v2.UV;

				// 트라이앵글의 엣지(Edge)와 델타(Delta) UV를 계산합니다.
				FVector Edge1 = P1 - P0;
				FVector Edge2 = P2 - P0;
				FVector2D DeltaUV1 = W1 - W0;
				FVector2D DeltaUV2 = W2 - W0;

				// Lengyel's MikkTSpace/Schwarze Formula (분모)
				float r = 1.0f / (DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X);

				// r이 무한대(inf)나 NaN이 아닌지 확인 (UV가 겹치는 경우)
				if (isinf(r) || isnan(r))
				{
					r = 0.0f; // 이 트라이앵글은 계산에서 제외
				}

				// (정규화되지 않은) 탄젠트(T)와 바이탄젠트(B) 계산
				FVector T = (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * r;
				FVector B = (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * r;

				// 3개의 정점에 T와 B를 (정규화 없이) 누적합니다.
				// 이렇게 하면 동일한 정점을 공유하는 모든 트라이앵글의 T/B가 합산됩니다.
				TempTangents[i0] += T;
				TempTangents[i1] += T;
				TempTangents[i2] += T;

				TempBitangents[i0] += B;
				TempBitangents[i1] += B;
				TempBitangents[i2] += B;
			}
		}

		// 3. 모든 정점을 순회하며 누적된 T/B를 직교화(Gram-Schmidt)하고 저장합니다.
		for (int32 i = 0; i < MeshData.Vertices.Num(); ++i)
		{
			FSkinnedVertex& V = MeshData.Vertices[i]; // 실제 정점 데이터에 접근
			const FVector& N = V.Normal;
			const FVector& T_accum = TempTangents[i];
			const FVector& B_accum = TempBitangents[i];

			if (T_accum.IsZero() || N.IsZero())
			{
				// T 또는 N이 0이면 계산 불가. 유효한 기본값 설정
				FVector T_fallback = FVector(1.0f, 0.0f, 0.0f);
				if (FMath::Abs(FVector::Dot(N, T_fallback)) > 0.99f) // N이 X축과 거의 평행하면
				{
					T_fallback = FVector(0.0f, 1.0f, 0.0f); // Y축을 T로 사용
				}
				V.Tangent = FVector4(T_fallback.X, T_fallback.Y, T_fallback.Z, 1.0f);
				continue;
			}

			// Gram-Schmidt 직교화: T = T - (T dot N) * N
			// (T를 N에 투영한 성분을 T에서 빼서, N과 수직인 벡터를 만듭니다)
			FVector Tangent = (T_accum - N * (FVector::Dot(T_accum, N))).GetSafeNormal();

			// Handedness (W 컴포넌트) 계산:
			// 외적으로 구한 B(N x T)와 누적된 B(B_accum)의 방향을 비교합니다.
			float Handedness = (FVector::Dot((FVector::Cross(Tangent, N)), B_accum) > 0.0f) ? 1.0f : -1.0f;

			// 최종 탄젠트(T)와 Handedness(W)를 저장합니다.
			V.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, Handedness);
		}
	}
}


// 머티리얼 파싱해서 FMaterialInfo에 매핑
void UFbxLoader::ParseMaterial(FbxSurfaceMaterial* Material, FMaterialInfo& MaterialInfo)
{

	UMaterial* NewMaterial = NewObject<UMaterial>();

	// FbxPropertyT : 타입에 대해 애니메이션과 연결 지원(키프레임마다 타입 변경 등)
	FbxPropertyT<FbxDouble3> Double3Prop;
	FbxPropertyT<FbxDouble> DoubleProp;

	MaterialInfo.MaterialName = Material->GetName();
	// PBR 제외하고 Phong, Lambert 머티리얼만 처리함. 
	if (Material->GetClassId().Is(FbxSurfacePhong::ClassId))
	{
		FbxSurfacePhong* SurfacePhong = (FbxSurfacePhong*)Material;

		Double3Prop = SurfacePhong->Diffuse;
		MaterialInfo.DiffuseColor = FVector(Double3Prop.Get()[0], Double3Prop.Get()[1], Double3Prop.Get()[2]);

		Double3Prop = SurfacePhong->Ambient;
		MaterialInfo.AmbientColor = FVector(Double3Prop.Get()[0], Double3Prop.Get()[1], Double3Prop.Get()[2]);

		// SurfacePhong->Reflection : 환경 반사, 퐁 모델에선 필요없음
		Double3Prop = SurfacePhong->Specular;
		DoubleProp = SurfacePhong->SpecularFactor;
		MaterialInfo.SpecularColor = FVector(Double3Prop.Get()[0], Double3Prop.Get()[1], Double3Prop.Get()[2]) * DoubleProp.Get();

		// HDR 안 써서 의미 없음
	/*	Double3Prop = SurfacePhong->Emissive;
		MaterialInfo.EmissiveColor = FVector(Double3Prop.Get()[0], Double3Prop.Get()[1], Double3Prop.Get()[2]);*/

		DoubleProp = SurfacePhong->Shininess;
		MaterialInfo.SpecularExponent = DoubleProp.Get();

		DoubleProp = SurfacePhong->TransparencyFactor;
		MaterialInfo.Transparency = DoubleProp.Get();
	}
	else if (Material->GetClassId().Is(FbxSurfaceLambert::ClassId))
	{
		FbxSurfaceLambert* SurfacePhong = (FbxSurfaceLambert*)Material;

		Double3Prop = SurfacePhong->Diffuse;
		MaterialInfo.DiffuseColor = FVector(Double3Prop.Get()[0], Double3Prop.Get()[1], Double3Prop.Get()[2]);

		Double3Prop = SurfacePhong->Ambient;
		MaterialInfo.AmbientColor = FVector(Double3Prop.Get()[0], Double3Prop.Get()[1], Double3Prop.Get()[2]);

		// HDR 안 써서 의미 없음
	/*	Double3Prop = SurfacePhong->Emissive;
		MaterialInfo.EmissiveColor = FVector(Double3Prop.Get()[0], Double3Prop.Get()[1], Double3Prop.Get()[2]);*/

		DoubleProp = SurfacePhong->TransparencyFactor;
		MaterialInfo.Transparency = DoubleProp.Get();
	}


	FbxProperty Property;

	Property = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
	MaterialInfo.DiffuseTextureFileName = ParseTexturePath(Property);

	Property = Material->FindProperty(FbxSurfaceMaterial::sNormalMap);
	MaterialInfo.NormalTextureFileName = ParseTexturePath(Property);

	Property = Material->FindProperty(FbxSurfaceMaterial::sAmbient);
	MaterialInfo.AmbientTextureFileName = ParseTexturePath(Property);

	Property = Material->FindProperty(FbxSurfaceMaterial::sSpecular);
	MaterialInfo.SpecularTextureFileName = ParseTexturePath(Property);

	Property = Material->FindProperty(FbxSurfaceMaterial::sEmissive);
	MaterialInfo.EmissiveTextureFileName = ParseTexturePath(Property);

	Property = Material->FindProperty(FbxSurfaceMaterial::sTransparencyFactor);
	MaterialInfo.TransparencyTextureFileName = ParseTexturePath(Property);

	Property = Material->FindProperty(FbxSurfaceMaterial::sShininess);
	MaterialInfo.SpecularExponentTextureFileName = ParseTexturePath(Property);

	UMaterial* Default = UResourceManager::GetInstance().GetDefaultMaterial();
	NewMaterial->SetMaterialInfo(MaterialInfo);
	NewMaterial->SetShader(Default->GetShader());
	NewMaterial->SetShaderMacros(Default->GetShaderMacros());

	MaterialInfos.Add(MaterialInfo);
	UResourceManager::GetInstance().Add<UMaterial>(MaterialInfo.MaterialName, NewMaterial);
}

FString UFbxLoader::ParseTexturePath(FbxProperty& Property)
{
	if (Property.IsValid())
	{
		if (Property.GetSrcObjectCount<FbxFileTexture>() > 0)
		{
			FbxFileTexture* Texture = Property.GetSrcObject<FbxFileTexture>(0);
			if (Texture)
			{
				return FString(Texture->GetFileName());
			}
		}
	}
	return FString();
}

FbxString UFbxLoader::GetAttributeTypeName(FbxNodeAttribute* InAttribute)
{
	// 테스트코드
	// Attribute타입에 대한 자료형, 이것으로 Skeleton만 빼낼 수 있을 듯
	/*FbxNodeAttribute::EType Type = InAttribute->GetAttributeType();
	switch (Type) {
	case FbxNodeAttribute::eUnknown: return "unidentified";
	case FbxNodeAttribute::eNull: return "null";
	case FbxNodeAttribute::eMarker: return "marker";
	case FbxNodeAttribute::eSkeleton: return "skeleton";
	case FbxNodeAttribute::eMesh: return "mesh";
	case FbxNodeAttribute::eNurbs: return "nurbs";
	case FbxNodeAttribute::ePatch: return "patch";
	case FbxNodeAttribute::eCamera: return "camera";
	case FbxNodeAttribute::eCameraStereo: return "stereo";
	case FbxNodeAttribute::eCameraSwitcher: return "camera switcher";
	case FbxNodeAttribute::eLight: return "light";
	case FbxNodeAttribute::eOpticalReference: return "optical reference";
	case FbxNodeAttribute::eOpticalMarker: return "marker";
	case FbxNodeAttribute::eNurbsCurve: return "nurbs curve";
	case FbxNodeAttribute::eTrimNurbsSurface: return "trim nurbs surface";
	case FbxNodeAttribute::eBoundary: return "boundary";
	case FbxNodeAttribute::eNurbsSurface: return "nurbs surface";
	case FbxNodeAttribute::eShape: return "shape";
	case FbxNodeAttribute::eLODGroup: return "lodgroup";
	case FbxNodeAttribute::eSubDiv: return "subdiv";
	default: return "unknown";
	}*/
	return "test";
}

void UFbxLoader::EnsureSingleRootBone(FSkeletalMeshData& MeshData)
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

// ============================================================================
// Animation Loading Implementation
// ============================================================================

//void UFbxLoader::LoadAnimationsFromFbx(const FString& FilePath, TArray<UAnimSequence*>& OutAnimations)
//{
//	FString NormalizedPath = NormalizePath(FilePath);
//
//#ifdef USE_OBJ_CACHE
//	// 1. Check for cached animations
//	FString CacheBasePath = ConvertDataPathToCachePath(NormalizedPath);
//	FString AnimCacheDir = CacheBasePath + "_Anims";
//
//	bool bShouldRegenerate = true;
//
//	if (std::filesystem::exists(AnimCacheDir) && std::filesystem::is_directory(AnimCacheDir))
//	{
//		try
//		{
//			auto fbxTime = std::filesystem::last_write_time(NormalizedPath);
//
//			// Check if all cached animations are up to date
//			bool bAllCacheValid = true;
//			for (const auto& entry : std::filesystem::directory_iterator(AnimCacheDir))
//			{
//				if (entry.path().extension() == ".anim.bin")
//				{
//					auto cacheTime = std::filesystem::last_write_time(entry.path());
//					if (fbxTime > cacheTime)
//					{
//						bAllCacheValid = false;
//						break;
//					}
//				}
//			}
//
//			if (bAllCacheValid)
//			{
//				bShouldRegenerate = false;
//			}
//		}
//		catch (const std::filesystem::filesystem_error& e)
//		{
//			UE_LOG("Filesystem error during animation cache validation: %s", e.what());
//			bShouldRegenerate = true;
//		}
//	}
//
//	// 2. Try loading from cache
//	if (!bShouldRegenerate && std::filesystem::exists(AnimCacheDir))
//	{
//		UE_LOG("Loading animations from cache: %s", AnimCacheDir.c_str());
//	
//		for (const auto& entry : std::filesystem::directory_iterator(AnimCacheDir))
//		{
//			if (entry.path().extension() == ".bin" &&
//			    entry.path().stem().string().find(".anim") != std::string::npos)
//			{
//				UAnimSequence* CachedAnim = LoadAnimationFromCache(entry.path().string());
//				if (CachedAnim)
//				{
//					OutAnimations.Add(CachedAnim);
//				}
//			}
//		}
//	
//		if (OutAnimations.Num() > 0)
//		{
//			UE_LOG("Loaded %d animations from cache", OutAnimations.Num());
//			return;
//		}
//	}
//
//	// 3. Cache not available or invalid, load from FBX
//	UE_LOG("Regenerating animation cache for: %s", NormalizedPath.c_str());
//#endif
//
//	// Create importer
//	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
//	if (!Importer->Initialize(NormalizedPath.c_str(), -1, SdkManager->GetIOSettings()))
//	{
//		UE_LOG("Failed to initialize FbxImporter for animations: %s", NormalizedPath.c_str());
//		Importer->Destroy();
//		return;
//	}
//
//	// Create scene
//	FbxScene* Scene = FbxScene::Create(SdkManager, "AnimScene");
//	Importer->Import(Scene);
//	Importer->Destroy();
//
//	// Convert axis system
//	FbxAxisSystem UnrealImportAxis(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
//	FbxAxisSystem SourceSetup = Scene->GetGlobalSettings().GetAxisSystem();
//	FbxSystemUnit::m.ConvertScene(Scene);
//
//	if (SourceSetup != UnrealImportAxis)
//	{
//		UnrealImportAxis.DeepConvertScene(Scene);
//	}
//
//	// Load mesh data to get skeleton info
//	FSkeletalMeshData* MeshData = LoadFbxMeshAsset(FilePath);
//	if (!MeshData)
//	{
//		UE_LOG("Failed to load mesh data for animations from: %s", NormalizedPath.c_str());
//		Scene->Destroy();
//		return;
//	}
//
//	// Process animations
//	ProcessAnimations(Scene, *MeshData, NormalizedPath, OutAnimations);
//
//	// Cleanup
//	delete MeshData;
//	Scene->Destroy();
//
//#ifdef USE_OBJ_CACHE
//	// 4. Save animations to cache
//	if (OutAnimations.Num() > 0)
//	{
//		// Create cache directory
//		std::filesystem::create_directories(AnimCacheDir);
//
//		for (int32 i = 0; i < OutAnimations.Num(); ++i)
//		{
//			FString AnimCachePath = AnimCacheDir + "/" + std::to_string(i) + ".anim.bin";
//			SaveAnimationToCache(OutAnimations[i], AnimCachePath);
//		}
//
//		UE_LOG("Saved %d animations to cache", OutAnimations.Num());
//	}
//#endif
//
//	UE_LOG("Loaded %d animations from FBX file: %s", OutAnimations.Num(), NormalizedPath.c_str());
//}

void UFbxLoader::ProcessAnimations(FbxScene* Scene, const FSkeletalMeshData& MeshData, const FString& FilePath, TArray<UAnimSequence*>& OutAnimations)
{
	// Find Armature node by name and extract its transform for correction
	FTransform ArmatureCorrection;
	bool bFoundArmature = false;
	FbxNode* RootNode = Scene->GetRootNode();
	if (RootNode)
	{
		// Search for "Armature" node (case-insensitive)
		for (int i = 0; i < RootNode->GetChildCount(); ++i)
		{
			FbxNode* ChildNode = RootNode->GetChild(i);
			const char* NodeNameCStr = ChildNode->GetName();
			FString NodeName(NodeNameCStr);

			// Check if node name is "Armature" (case-insensitive)
			FString NodeNameLower = NodeName;
			std::transform(NodeNameLower.begin(), NodeNameLower.end(), NodeNameLower.begin(), ::tolower);

			if (NodeNameLower == "armature")
			{
				// Found Armature node - extract its transform
				FbxAMatrix LocalTransform = ChildNode->EvaluateLocalTransform(FBXSDK_TIME_ZERO);

				// Convert FbxAMatrix to FTransform
				FbxVector4 Translation = LocalTransform.GetT();
				FbxQuaternion Rotation = LocalTransform.GetQ();
				FbxVector4 Scale = LocalTransform.GetS();

				ArmatureCorrection.Translation = FVector(
					static_cast<float>(Translation[0]),
					static_cast<float>(Translation[1]),
					static_cast<float>(Translation[2])
				);
				ArmatureCorrection.Rotation = FQuat(
					static_cast<float>(Rotation[0]),
					static_cast<float>(Rotation[1]),
					static_cast<float>(Rotation[2]),
					static_cast<float>(Rotation[3])
				);
				ArmatureCorrection.Scale3D = FVector(
					static_cast<float>(Scale[0]),
					static_cast<float>(Scale[1]),
					static_cast<float>(Scale[2])
				);

				UE_LOG("Found Armature node '%s', storing correction transform", NodeName.c_str());
				bFoundArmature = true;
				break;
			}
		}
	}

	// If no Armature node found, set to Identity (Mixamo case)
	if (!bFoundArmature)
	{
		ArmatureCorrection = FTransform();
		UE_LOG("No Armature node found, using Identity transform (Mixamo FBX)");
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
		AnimSequence->SetArmatureCorrection(ArmatureCorrection);

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

			if (SaveAnimationToCache(OutAnimations[i], AnimCachePath))
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

void UFbxLoader::ExtractBoneAnimation(FbxNode* BoneNode, FbxAnimLayer* AnimLayer, FbxTime Start, FbxTime End, int32 NumFrames, TArray<FVector>& OutPositions, TArray<FQuat>& OutRotations, TArray<FVector>& OutScales)
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

bool UFbxLoader::NodeHasAnimation(FbxNode* Node, FbxAnimLayer* AnimLayer)
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

void UFbxLoader::BuildBoneNodeMap(FbxNode* RootNode, TMap<FName, FbxNode*>& OutBoneNodeMap)
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

FbxAMatrix UFbxLoader::GetBindPoseMatrix(FbxNode* Node)
{
	if (!Node)
	{
		return FbxAMatrix();
	}

	return Node->EvaluateGlobalTransform();
}

// ============================================================================
// Animation Caching Implementation
// ============================================================================

bool UFbxLoader::TryLoadAnimationsFromCache(const FString& NormalizedPath, TArray<UAnimSequence*>& OutAnimations)
{
#ifdef USE_OBJ_CACHE
	OutAnimations.Empty();

	FString CacheBasePath = ConvertDataPathToCachePath(NormalizedPath);
	FString AnimCacheDir = CacheBasePath + "_Anims";

	if (!std::filesystem::exists(AnimCacheDir) || !std::filesystem::is_directory(AnimCacheDir))
	{
		return false;
	}

	bool bLoadedAny = false;
	for (const auto& Entry : std::filesystem::directory_iterator(AnimCacheDir))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		if (Entry.path().extension() != ".bin")
		{
			continue;
		}

		FString CachePath = NormalizePath(Entry.path().string());
		if (UAnimSequence* CachedAnim = LoadAnimationFromCache(CachePath))
		{
			FString AnimStackName = CachedAnim->ObjectName.ToString();
			FString AnimKey = NormalizedPath + "_" + AnimStackName;

			if (!RESOURCE.Add<UAnimSequence>(AnimKey, CachedAnim))
			{
				UE_LOG("Animation cache already registered: %s", AnimKey.c_str());
			}

			OutAnimations.Add(CachedAnim);
			bLoadedAny = true;
		}
	}

	if (!bLoadedAny)
	{
		UE_LOG("Animation cache directory '%s' did not contain valid .anim.bin files", AnimCacheDir.c_str());
	}

	return bLoadedAny;
#else
	(void)NormalizedPath;
	(void)OutAnimations;
	return false;
#endif
}

bool UFbxLoader::SaveAnimationToCache(UAnimSequence* Animation, const FString& CachePath)
{
	if (!Animation)
	{
		return false;
	}

	try
	{
		FWindowsBinWriter Writer(CachePath);

		UAnimDataModel* DataModel = Animation->GetDataModel();
		if (!DataModel)
		{
			return false;
		}

		// Write animation name first
		FString AnimName = Animation->ObjectName.ToString();
		Serialization::WriteString(Writer, AnimName);

		// Write metadata
		float PlayLength = DataModel->GetPlayLength();
		int32 FrameRate = DataModel->GetFrameRate();
		int32 NumberOfFrames = DataModel->GetNumberOfFrames();
		int32 NumberOfKeys = DataModel->GetNumberOfKeys();

		Writer << PlayLength;
		Writer << FrameRate;
		Writer << NumberOfFrames;
		Writer << NumberOfKeys;

		// Write Armature correction transform
		FTransform CachedArmatureCorrection = Animation->GetArmatureCorrection();
		FVector ACTranslation = CachedArmatureCorrection.Translation;
		FQuat ACRotation = CachedArmatureCorrection.Rotation;
		FVector ACScale = CachedArmatureCorrection.Scale3D;
		Writer << ACTranslation.X;
		Writer << ACTranslation.Y;
		Writer << ACTranslation.Z;
		Writer << ACRotation.X;
		Writer << ACRotation.Y;
		Writer << ACRotation.Z;
		Writer << ACRotation.W;
		Writer << ACScale.X;
		Writer << ACScale.Y;
		Writer << ACScale.Z;

		// Write bone tracks
		TArray<FBoneAnimationTrack>& Tracks = DataModel->GetBoneAnimationTracks();
		uint32 NumTracks = (uint32)Tracks.Num();
		Writer << NumTracks;

		for (FBoneAnimationTrack& Track : Tracks)
		{
			// Write bone name
			FString BoneName = Track.Name.ToString();
			Serialization::WriteString(Writer, BoneName);

			// Write position keys
			Serialization::WriteArray(Writer, Track.InternalTrack.PosKeys);

			// Write rotation keys (FQuat는 직접 직렬화)
			TArray<FQuat>& RotKeys = Track.InternalTrack.RotKeys;
			uint32 NumRotKeys = (uint32)RotKeys.Num();
			Writer << NumRotKeys;
			for (FQuat& Rot : RotKeys)
			{
				Writer << Rot.X << Rot.Y << Rot.Z << Rot.W;
			}

			// Write scale keys
			Serialization::WriteArray(Writer, Track.InternalTrack.ScaleKeys);
		}

		Writer.Close();
		UE_LOG("Animation saved to cache: %s", CachePath.c_str());
		return true;
	}
	catch (const std::exception& e)
	{
		UE_LOG("Failed to save animation cache: %s", e.what());
		return false;
	}
}

UAnimSequence* UFbxLoader::LoadAnimationFromCache(const FString& CachePath)
{
	try
	{
		FWindowsBinReader Reader(CachePath);
		if (!Reader.IsOpen())
		{
			return nullptr;
		}

		// Create new animation sequence
		UAnimSequence* Animation = NewObject<UAnimSequence>();

		// Read animation name first
		FString AnimName;
		Serialization::ReadString(Reader, AnimName);
		Animation->ObjectName = FName(AnimName);

		UAnimDataModel* DataModel = Animation->GetDataModel();
		if (!DataModel)
		{
			return nullptr;
		}

		// Read metadata
		float PlayLength;
		int32 FrameRate;
		int32 NumberOfFrames;
		int32 NumberOfKeys;

		Reader << PlayLength;
		Reader << FrameRate;
		Reader << NumberOfFrames;
		Reader << NumberOfKeys;

		FTransform ArmatureCorrection;
		Reader << ArmatureCorrection.Translation.X;
		Reader << ArmatureCorrection.Translation.Y;
		Reader << ArmatureCorrection.Translation.Z;
		Reader << ArmatureCorrection.Rotation.X;
		Reader << ArmatureCorrection.Rotation.Y;
		Reader << ArmatureCorrection.Rotation.Z;
		Reader << ArmatureCorrection.Rotation.W;
		Reader << ArmatureCorrection.Scale3D.X;
		Reader << ArmatureCorrection.Scale3D.Y;
		Reader << ArmatureCorrection.Scale3D.Z;

		Animation->SetArmatureCorrection(ArmatureCorrection);

		DataModel->SetPlayLength(PlayLength);
		DataModel->SetFrameRate(FrameRate);
		DataModel->SetNumberOfFrames(NumberOfFrames);
		DataModel->SetNumberOfKeys(NumberOfKeys);

		// Read bone tracks
		uint32 NumTracks;
		Reader << NumTracks;

		for (uint32 i = 0; i < NumTracks; ++i)
		{
			// Read bone name
			FString BoneNameStr;
			Serialization::ReadString(Reader, BoneNameStr);
			FName BoneName(BoneNameStr);

			// Add bone track
			DataModel->AddBoneTrack(BoneName);

			// Read position keys
			TArray<FVector> PosKeys;
			Serialization::ReadArray(Reader, PosKeys);

			// Read rotation keys
			uint32 NumRotKeys;
			Reader << NumRotKeys;
			TArray<FQuat> RotKeys;
			RotKeys.resize(NumRotKeys);
			for (uint32 j = 0; j < NumRotKeys; ++j)
			{
				float X, Y, Z, W;
				Reader << X << Y << Z << W;
				RotKeys[j] = FQuat(X, Y, Z, W);
			}

			// Read scale keys
			TArray<FVector> ScaleKeys;
			Serialization::ReadArray(Reader, ScaleKeys);

			// Set keys in data model
			DataModel->SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys);
		}

		Reader.Close();
		UE_LOG("Animation loaded from cache: %s (Name: %s)", CachePath.c_str(), AnimName.c_str());
		return Animation;
	}
	catch (const std::exception& e)
	{
		UE_LOG("Failed to load animation from cache: %s", e.what());
		return nullptr;
	}
}

