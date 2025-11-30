#pragma once
#include "ObjectFactory.h"
#include "Object.h"
#include "Shader.h"
#include "StaticMesh.h"
#include "Material.h"
#include "Texture.h"
#include "TextureConverter.h"
#include "DynamicMesh.h"
#include "../Engine/Audio/Sound.h"
#include "Quad.h"
#include "LineDynamicMesh.h"

#pragma once
#include "ObjectFactory.h"
#include "Object.h"
#include "SkeletalMesh.h"
#include "Source/Runtime/Engine/Animation/AnimSequence.h"
#include "Source/Runtime/Engine/Particle/ParticleSystem.h"
#include "Source/Runtime/Engine/Physics/PhysicsAsset.h"
// ... 기타 include ...

// --- 전방 선언 ---
class UStaticMesh;
class FMeshBVH;
class UResourceBase;
class UMaterial;
class USound;

//================================================================================================
// UResourceManager
//================================================================================================

class UResourceManager : public UObject
{
public:
	DECLARE_CLASS(UResourceManager, UObject)

	// --- 싱글톤 및 초기화 ---
	static UResourceManager& GetInstance();
	void Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);
	void Clear(); // 전체 해제

	// --- 생성자 ---
	UResourceManager() = default;

	// --- 리소스 로드 및 접근 ---
	template<typename T, typename... Args>
	T* Load(const FString& InFilePath, Args&&... InArgs);

	template<typename T>
	bool Add(const FString& InFilePath, UObject* InObject);

	template<typename T>
	T* Get(const FString& InFilePath);

	template<typename T>
	TArray<T*> GetAll();

	template<typename T>
	TArray<FString> GetAllFilePaths();

	template<typename T>
	EResourceType GetResourceType();

	// --- 헬퍼 및 유틸리티 ---
	ID3D11Device* GetDevice() { return Device; }
	ID3D11DeviceContext* GetDeviceContext() { return Context; }
	TArray<D3D11_INPUT_ELEMENT_DESC>& GetProperInputLayout(const FString& InShaderName);
	FString& GetProperShader(const FString& InTextureName);

	// --- Shader Hot Reload ---
	void CheckAndReloadShaders(float DeltaTime);

	// --- 리소스 생성 및 관리 ---
	FTextureData* CreateOrGetTextureData(const FWideString& FilePath);
	void UpdateDynamicVertexBuffer(const FString& name, TArray<FBillboardVertexInfo_GPU>& vertices);
	UMaterial* GetDefaultMaterial();

	void PreloadParticles();
	void PreloadPhysicsAssets();

	// --- 디버그 및 기본 메시 생성 ---
	void CreateDefaultShader();
	void CreateDefaultMaterial();
	void CreateAxisMesh(float Length, const FString& FilePath);
	void CreateGridMesh(int N, const FString& FilePath);
	void CreateBoxWireframeMesh(const FVector& Min, const FVector& Max, const FString& FilePath);
	void CreateBillboardMesh();
	void CreateTextBillboardMesh();
	void CreateTextBillboardTexture();

	// --- 캐시 관리 ---
	FMeshBVH* GetMeshBVH(const FString& ObjPath);
	FMeshBVH* GetOrBuildMeshBVH(const FString& ObjPath, const struct FStaticMesh* StaticMeshAsset);
	void SetStaticMeshs();
	void SetSkeletalMeshs();
	const TArray<UStaticMesh*>& GetStaticMeshs() { return StaticMeshs; }

	void SetAudioFiles();  

	// --- Deprecated (향후 제거될 함수들) ---
	TArray<UStaticMesh*> GetAllStaticMeshes() { return GetAll<UStaticMesh>(); }
	TArray<FString> GetAllStaticMeshFilePaths() { return GetAllFilePaths<UStaticMesh>(); }
	void InitShaderILMap();
	void InitTexToShaderMap();

protected:
	// --- 소멸자 및 복사/대입 방지 ---
	~UResourceManager() override;
	UResourceManager(const UResourceManager&) = delete;
	UResourceManager& operator=(const UResourceManager&) = delete;

	// --- 보호된 멤버 변수 ---
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* Context = nullptr;

	//Resource Type의 개수만큼 Array 생성 및 저장
	TArray<TMap<FString, UResourceBase*>> Resources;

	TMap<FString, TArray<D3D11_INPUT_ELEMENT_DESC>> ShaderToInputLayoutMap;
	TMap<FString, FString> TextureToShaderMap;

	TArray<UStaticMesh*> StaticMeshs;
	TArray<USkeletalMesh*> SkeletalMeshs;

	TArray<USound*> Sounds;

	// Deprecated
	TMap<FString, FResourceData*> ResourceMap;
	TMap<FWideString, FTextureData*> TextureMap;

private:
	// --- 비공개 멤버 변수 ---
	TMap<FString, UMaterial*> MaterialMap;

	// Cache for per-mesh BVHs to avoid rebuilding for identical OBJ assets
	TMap<FString, FMeshBVH*> MeshBVHCache;

	UMaterial* DefaultMaterialInstance;

	// Shader Hot Reload
	float ShaderCheckTimer = 0.0f;
	const float ShaderCheckInterval = 0.5f; // Check every 0.5 seconds
};

//-----definition
// 리소스 매니저에 새로운 리소스 등록하는 함수이다.
template<typename T>
bool UResourceManager::Add(const FString& InFilePath, UObject* InObject)
{
	// 경로 정규화: 모든 백슬래시를 슬래시로 변환하여 일관성 유지
	FString NormalizedPath = NormalizePath(InFilePath);

	uint8 typeIndex = static_cast<uint8>(GetResourceType<T>());
	auto iter = Resources[typeIndex].find(NormalizedPath);
	if (iter == Resources[typeIndex].end())
	{
		Resources[typeIndex][NormalizedPath] = static_cast<T*>(InObject);
		// 경로 저장
		Resources[typeIndex][NormalizedPath]->SetFilePath(NormalizedPath);
		return true;
	}
	return false;
}

template<typename T>
T* UResourceManager::Get(const FString& InFilePath)
{
	// 경로 정규화: 모든 백슬래시를 슬래시로 변환하여 일관성 유지
	FString NormalizedPath = NormalizePath(InFilePath);

	uint8 typeIndex = static_cast<uint8>(GetResourceType<T>());
	auto iter = Resources[typeIndex].find(NormalizedPath);
	if (iter != Resources[typeIndex].end())
	{
		return static_cast<T*>(iter->second);
	}

	return nullptr;
}

template<typename T, typename ...Args>
inline T* UResourceManager::Load(const FString& InFilePath, Args && ...InArgs)
{
	if (InFilePath.empty())
	{
		return nullptr;
	}

	// 경로 정규화: 모든 백슬래시를 슬래시로 변환하여 일관성 유지
	FString NormalizedPath = NormalizePath(InFilePath);

	uint8 typeIndex = static_cast<uint8>(GetResourceType<T>());
	auto iter = Resources[typeIndex].find(NormalizedPath);
	if (iter != Resources[typeIndex].end())
	{
		if constexpr (std::is_same_v<T, UShader>)
		{
			UShader* Shader = static_cast<UShader*>(iter->second);
			Shader->GetOrCompileShaderVariant(std::forward<Args>(InArgs)...);	// 매크로에 해당하는 셰이더를 별도로 컴파일 하기 위해
			return Shader;
		}

		return static_cast<T*>(iter->second);
	}
	else//없으면 해당 리소스의 Load실행
	{
		T* Resource = NewObject<T>();
		if (!Resource->Load(NormalizedPath, Device, std::forward<Args>(InArgs)...))
		{
			DeleteObject(Resource);
			return nullptr;
		}
		Resource->SetFilePath(NormalizedPath);
		Resources[typeIndex][NormalizedPath] = Resource;
		return Resource;
	}
}

template<>
inline UShader* UResourceManager::Load(const FString& InFilePath, TArray<FShaderMacro>& InMacros)
{
	// 경로 정규화: 모든 백슬래시를 슬래시로 변환하여 일관성 유지
	FString NormalizedPath = NormalizePath(InFilePath);

	// 2. 경로 리소스 맵 검색
	uint8 typeIndex = static_cast<uint8>(EResourceType::Shader);
	auto iter = Resources[typeIndex].find(NormalizedPath);
	if (iter != Resources[typeIndex].end())
	{
		UShader* Shader = static_cast<UShader*>((*iter).second);

		Shader->GetOrCompileShaderVariant(InMacros);

		return Shader;
	}
	else
	{
		// 3. 캐시에 없으면 새로 생성하여 로드
		UShader* Resource = NewObject<UShader>();
		// UShader::Load는 이제 매크로 인자를 받도록 수정되어야 함
		Resource->Load(NormalizedPath, Device, InMacros);
		Resource->SetFilePath(NormalizedPath); // 이름 저장

		Resources[typeIndex][NormalizedPath] = Resource;
		return Resource;
	}
}

template<typename T>
EResourceType UResourceManager::GetResourceType()
{
    if (T::StaticClass() == UStaticMesh::StaticClass())
        return EResourceType::StaticMesh;
    if (T::StaticClass() == USkeletalMesh::StaticClass())
        return EResourceType::SkeletalMesh;
	if (T::StaticClass() == UQuad::StaticClass())
		return EResourceType::Quad;
	if (T::StaticClass() == UDynamicMesh::StaticClass())
		return EResourceType::DynamicMesh;
	if (T::StaticClass() == ULineDynamicMesh::StaticClass())
		return EResourceType::DynamicMesh; // share bucket with DynamicMesh
	if (T::StaticClass() == UShader::StaticClass())
		return EResourceType::Shader;
	if (T::StaticClass() == UTexture::StaticClass())
		return EResourceType::Texture;
    if (T::StaticClass() == UMaterial::StaticClass())
        return EResourceType::Material;
    if (T::StaticClass() == USound::StaticClass())
        return EResourceType::Sound;
	if (T::StaticClass() == UAnimSequence::StaticClass())
		return EResourceType::Animation;
	if (T::StaticClass() == UParticleSystem::StaticClass())
		return EResourceType::Particle;
	if (T::StaticClass() == UPhysicsAsset::StaticClass())
		return EResourceType::PhysicsAsset;

    return EResourceType::None;
}

// Enumerate all resources of a type T
template<typename T>
TArray<T*> UResourceManager::GetAll()
{
	TArray<T*> Result;
	uint8 TypeIndex = static_cast<uint8>(GetResourceType<T>());
	if (TypeIndex >= Resources.size())
	{
		return Result;
	}

	for (auto& Pair : Resources[TypeIndex])
	{
		if (Pair.second)
		{
			Result.push_back(static_cast<T*>(Pair.second));
		}
	}
	return Result;
}

// Collect non-empty FilePath of all resources of type T
template<typename T>
TArray<FString> UResourceManager::GetAllFilePaths()
{
	TArray<FString> Paths;
	uint8 TypeIndex = static_cast<uint8>(GetResourceType<T>());
	if (TypeIndex >= Resources.size())
	{
		return Paths;
	}

	for (auto& Pair : Resources[TypeIndex])
	{
		if (Pair.second)
		{
			const FString& Path = Pair.second->GetFilePath();
			if (!Path.empty())
			{
				Paths.push_back(Path);
			}
		}
	}
	return Paths;
}
