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

	FWideString WDataDir = UTF8ToWide(GDataDir);
	const fs::path DataDir(WDataDir);

	if (!fs::exists(DataDir) || !fs::is_directory(DataDir))
	{
		UE_LOG("UFbxLoader::Preload: Data directory not found: %s", GDataDir.c_str());
		return;
	}

	size_t LoadedCount = 0;
	std::unordered_set<FWideString> ProcessedFiles;

	for (const auto& Entry : fs::recursive_directory_iterator(DataDir))
	{
		if (!Entry.is_regular_file())
			continue;

		const fs::path& Path = Entry.path();

		FWideString Extension = Path.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);

		if (Extension == L".fbx")
		{
			// 변경 (wstring -> WideToUTF8 -> FString)
			FWideString WPathStr = Path.wstring();
			FString PathStr = NormalizePath(WideToUTF8(WPathStr));

			if (ProcessedFiles.find(WPathStr) == ProcessedFiles.end())
			{
				ProcessedFiles.insert(WPathStr);
				FbxLoader.LoadFbxMesh(PathStr);
				++LoadedCount;
			}
		}
		else if (Extension == L".dds" || Extension == L".jpg" || Extension == L".png")
		{
			UResourceManager::GetInstance().Load<UTexture>(WideToUTF8(Path.wstring()));
		}
	}
	RESOURCE.SetSkeletalMeshs();

	UE_LOG("UFbxLoader::Preload: Loaded %zu .fbx files from %s", LoadedCount, GDataDir.c_str());
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
	
	// - FbxPath: std::filesystem 용 wide path
	// - FbxPathAcp: FBX SDK Initialize()에 넘길 ANSI(MBCS) 경로
	FWideString WNormalizedPath = UTF8ToWide(NormalizedPath);
	std::filesystem::path FbxPath(WNormalizedPath);
	FString FbxPathAcp = UTF8ToACP(NormalizedPath);
	CurrentFbxBaseDir = NormalizePath(WideToUTF8(FbxPath.parent_path().wstring()));

	FSkeletalMeshData* MeshData = nullptr;
#ifdef USE_OBJ_CACHE
	
	// 1. 캐시 파일 경로 설정
	FString CachePathStr = ConvertDataPathToCachePath(NormalizedPath);
	const FString BinPathFileName = CachePathStr + ".bin";

	// 파일 시스템용 wide path
	FWideString WBinPath = UTF8ToWide(BinPathFileName);
	std::filesystem::path BinPath(WBinPath);

	// 캐시를 저장할 디렉토리가 없으면 생성
	std::filesystem::path CacheFileDirPath = BinPath;

	if (CacheFileDirPath.has_parent_path())
	{
		std::filesystem::create_directories(CacheFileDirPath.parent_path());
	}

	bool bLoadedFromCache = false;
	
	// 2. 캐시 유효성 검사
	bool bShouldRegenerate = true;
	if (std::filesystem::exists(BinPath))
	{
		try
		{
			auto binTime = std::filesystem::last_write_time(BinPath);
			auto fbxTime = std::filesystem::last_write_time(FbxPath);
			
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
				// bin 로드용
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
			bool bLoadedAnimFromCache = FBXAnimationCache::TryLoadAnimationsFromCache(NormalizedPath, CachedAnimations);
			if (bLoadedAnimFromCache)
			{
				UE_LOG("Loaded %d animations from cache", CachedAnimations.Num());
			}

			// 애니메이션 캐시가 없거나 로드에 실패한 경우 FBX에서 추출
			if (!bLoadedAnimFromCache)
			{
				UE_LOG("Animation cache not found or invalid, extracting from FBX");

				FbxImporter* AnimImporter = FbxImporter::Create(SdkManager, "");
				if (AnimImporter->Initialize(NormalizedPath.c_str(), -1, SdkManager->GetIOSettings()))
				{
					FbxScene* AnimScene = FbxScene::Create(SdkManager, "AnimScene");
					AnimImporter->Import(AnimScene);
					AnimImporter->Destroy();

					FBXSceneUtilities::ConvertSceneAxisIfNeeded(AnimScene);

					// 애니메이션 추출 (ProcessAnimations가 이제 캐시에 저장함)
					TArray<UAnimSequence*> Animations;
					FBXAnimationLoader::ProcessAnimations(AnimScene, *MeshData, NormalizedPath, Animations);

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
	if (!Importer->Initialize(FbxPathAcp.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Call to FbxImporter::Initialize() Failed\n");
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
		int RootNodeChildCount = RootNode->GetChildCount();
		for (int Index = 0; Index < RootNodeChildCount; Index++)
		{
			FBXSkeletonLoader::LoadSkeletonHierarchy(RootNode->GetChild(Index), *MeshData, -1, BoneToIndex);
		}
		
		for (int Index = 0; Index < RootNodeChildCount; Index++)
		{
			FBXMeshLoader::LoadMeshFromNode(RootNode->GetChild(Index), *MeshData, MaterialGroupIndexList, BoneToIndex, MaterialToIndex, MaterialInfos);
		}

		// 여러 루트 본이 있으면 가상 루트 생성
		FBXSkeletonLoader::EnsureSingleRootBone(*MeshData);
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

	// 동일한 Scene에서 애니메이션 추출 (동일한 단위 변환 및 축)
	TArray<UAnimSequence*> Animations;
	FBXAnimationLoader::ProcessAnimations(Scene, *MeshData, NormalizedPath, Animations);

	if (Animations.Num() > 0)
	{
		UE_LOG("Loaded %d animations from the same Scene: %s", Animations.Num(), NormalizedPath.c_str());
	}

	// 모든 작업 완료 후 Scene 정리
	Scene->Destroy();

	return MeshData;
}
