#include "pch.h"
#include "Material.h"
#include "Shader.h"
#include "Texture.h"
#include "ResourceManager.h"

IMPLEMENT_CLASS(UMaterial)

UMaterial::UMaterial()
{
	// 배열 크기를 미리 할당 (Enum 값 사용)
	ResolvedTextures.resize(static_cast<size_t>(EMaterialTextureSlot::Max));
}

// 해당 경로의 셰이더 또는 텍스쳐를 로드해서 머티리얼로 생성 후 반환한다
bool UMaterial::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
	MaterialInfo.MaterialName = InFilePath;

	// 기본 쉐이더 로드 (LayoutType에 따라)
	// dds 의 경우 
	if (InFilePath.find(".dds") != std::string::npos)
	{
		FString shaderName = UResourceManager::GetInstance().GetProperShader(InFilePath);

		Shader = UResourceManager::GetInstance().Load<UShader>(shaderName);
		UResourceManager::GetInstance().Load<UTexture>(InFilePath);
		MaterialInfo.DiffuseTextureFileName = InFilePath;
	} // hlsl 의 경우 
	else if (InFilePath.find(".hlsl") != std::string::npos)
	{
		Shader = UResourceManager::GetInstance().Load<UShader>(InFilePath);
	}
	else
	{
		throw std::runtime_error(".dds나 .hlsl만 입력해주세요. 현재 입력 파일명 : " + InFilePath);
	}
	return true;
}

void UMaterial::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle); // 부모 Serialize 호출

	if (bInIsLoading)
	{
		// UStaticMeshComponent가 이미 UMaterial 객체를 생성한 후
		// 이 함수를 호출했다고 가정합니다.
	}
	else // 저장
	{
		InOutHandle["AssetPath"] = GetFilePath();
	}
}

void UMaterial::SetShader(UShader* InShaderResource)
{
	Shader = InShaderResource;
}

void UMaterial::SetShaderByName(const FString& InShaderName)
{
	SetShader(UResourceManager::GetInstance().Load<UShader>(InShaderName));
}

UShader* UMaterial::GetShader()
{
	return Shader;
}

const TArray<FShaderMacro> UMaterial::GetShaderMacros() const
{
	return ShaderMacros;
}

void UMaterial::SetShaderMacros(const TArray<FShaderMacro>& InShaderMacro)
{
	if (Shader)
	{
		UResourceManager::GetInstance().Load<UShader>(Shader->GetFilePath(), InShaderMacro);
	}

	ShaderMacros = InShaderMacro;
}

UTexture* UMaterial::GetTexture(EMaterialTextureSlot Slot) const
{
	size_t Index = static_cast<size_t>(Slot);
	// std::vector의 유효 인덱스 검사
	if (Index < ResolvedTextures.size())
	{
		return ResolvedTextures[Index];
	}
	return nullptr;
}

const FMaterialInfo& UMaterial::GetMaterialInfo() const
{
	return MaterialInfo;
}

bool UMaterial::HasTexture(EMaterialTextureSlot Slot) const
{
	switch (Slot)
	{
	case EMaterialTextureSlot::Diffuse:
		return !MaterialInfo.DiffuseTextureFileName.empty();
	case EMaterialTextureSlot::Normal:
		return !MaterialInfo.NormalTextureFileName.empty();
	default:
		return false;
	}
}

void UMaterial::ResolveTextures()
{
	auto& RM = UResourceManager::GetInstance();
	size_t MaxSlots = static_cast<size_t>(EMaterialTextureSlot::Max);

	// 배열 크기가 Enum 크기와 맞는지 확인 (안전장치)
	if (ResolvedTextures.size() != MaxSlots)
	{
		ResolvedTextures.resize(MaxSlots);
	}

	// 각 슬롯에 해당하는 텍스처 경로로 UTexture* 찾아서 배열에 저장
	if (!MaterialInfo.DiffuseTextureFileName.empty())
		ResolvedTextures[static_cast<int32>(EMaterialTextureSlot::Diffuse)] = RM.Load<UTexture>(MaterialInfo.DiffuseTextureFileName, true);
	else
		ResolvedTextures[static_cast<int32>(EMaterialTextureSlot::Diffuse)] = nullptr; // 또는 기본 텍스처

	if (!MaterialInfo.NormalTextureFileName.empty())
		ResolvedTextures[static_cast<int32>(EMaterialTextureSlot::Normal)] = RM.Load<UTexture>(MaterialInfo.NormalTextureFileName, false);
	else
		ResolvedTextures[static_cast<int32>(EMaterialTextureSlot::Normal)] = nullptr; // 또는 기본 노멀 텍스처
}

void UMaterial::SetMaterialInfo(const FMaterialInfo& InMaterialInfo)
{
	MaterialInfo = InMaterialInfo;
	ResolveTextures();
}


IMPLEMENT_CLASS(UMaterialInstanceDynamic)

UMaterialInstanceDynamic::UMaterialInstanceDynamic()
{
}

UMaterialInstanceDynamic* UMaterialInstanceDynamic::Create(UMaterialInterface* InParentMaterial)
{
	if (!InParentMaterial)
		return nullptr;

	// MID를 또다른 MID 위에 중첩해서 만드는 것은 현재 지원하지 않음 (필요 시 재귀적으로 구현 가능)
	if (Cast<UMaterialInstanceDynamic>(InParentMaterial))
	{
		UE_LOG("Creating a MID from another MID is not supported.");
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = NewObject<UMaterialInstanceDynamic>();
	MID->ParentMaterial = InParentMaterial;
	return MID;
}

void UMaterialInstanceDynamic::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle); // 부모 Serialize 호출

	if (bInIsLoading)
	{
		// UStaticMeshComponent가 'new UMaterialInstanceDynamic()'로
		// 빈 객체를 생성한 후, 이 함수를 호출합니다.
		// 여기서 부모를 로드하고 오버라이드 데이터를 채웁니다.

		// 1. 부모 로드
		FString ParentPath;
		FJsonSerializer::ReadString(InOutHandle, "ParentPath", ParentPath, "", false);
			this->ParentMaterial = UResourceManager::GetInstance().Load<UMaterial>(ParentPath);
		if (!this->ParentMaterial)
		{
			this->ParentMaterial = UResourceManager::GetInstance().GetDefaultMaterial();
			UE_LOG("UMID::Serialize: Failed to load parent %s. Using default.", ParentPath.c_str());
		}

		// 2. 오버라이드 데이터 로드
		JSON OverridesJson;
		if (FJsonSerializer::ReadObject(InOutHandle, "Overrides", OverridesJson, JSON::Make(JSON::Class::Object), false))
		{
			// 2-1. 텍스처 로드
			JSON TexturesJson;
			if (FJsonSerializer::ReadObject(OverridesJson, "Textures", TexturesJson, JSON::Make(JSON::Class::Object), false))
			{
				TMap<EMaterialTextureSlot, UTexture*> LoadedTextures;
				for (auto& [SlotKey, PathJson] : TexturesJson.ObjectRange())
				{
					uint8 SlotIndex = static_cast<uint8>(std::stoi(SlotKey));
					FString TexPath = PathJson.ToString();
					UTexture* Tex = (TexPath == "None" || TexPath.empty()) ? nullptr : UResourceManager::GetInstance().Load<UTexture>(TexPath);
					LoadedTextures.Add(static_cast<EMaterialTextureSlot>(SlotIndex), Tex);
				}
				SetOverriddenTextureParameters(LoadedTextures);
			}

			// 2-2. 스칼라 파라미터 로드
			JSON ScalarsJson;
			if (FJsonSerializer::ReadObject(OverridesJson, "Scalars", ScalarsJson, JSON::Make(JSON::Class::Object), false))
			{
				TMap<FString, float> LoadedScalars;
				for (auto& [ParamName, ValueJson] : ScalarsJson.ObjectRange())
				{
					LoadedScalars.Add(ParamName, static_cast<float>(ValueJson.ToFloat()));
				}
				SetOverriddenScalarParameters(LoadedScalars);
			}

			// 2-3. 벡터 파라미터 로드
			JSON VectorsJson;
			if (FJsonSerializer::ReadObject(OverridesJson, "Vectors", VectorsJson, JSON::Make(JSON::Class::Object), false))
			{
				TMap<FString, FLinearColor> LoadedVectors;
				for (auto& [ParamName, ColorArrayJson] : VectorsJson.ObjectRange())
				{
					if (ColorArrayJson.JSONType() == JSON::Class::Array && ColorArrayJson.size() == 4)
					{
						LoadedVectors.Add(ParamName, FLinearColor(
							static_cast<float>(ColorArrayJson.at(0).ToFloat()),
							static_cast<float>(ColorArrayJson.at(1).ToFloat()),
							static_cast<float>(ColorArrayJson.at(2).ToFloat()),
							static_cast<float>(ColorArrayJson.at(3).ToFloat())
						));
					}
				}
				SetOverriddenVectorParameters(LoadedVectors);
			}
		}

		// 인스턴스 파일 경로는 부모 기반으로 설정 (디버깅/에디터용)
		if (this->ParentMaterial)
		{
			SetFilePath("(Instance) " + this->ParentMaterial->GetFilePath());
		}
	}
	else // 저장
	{
		// 1. 부모 경로 저장
		UMaterialInterface* Parent = GetParentMaterial();
		InOutHandle["ParentPath"] = Parent ? Parent->GetFilePath() : "None";

		// 2. 오버라이드 데이터 저장
		JSON OverridesJson = JSON::Make(JSON::Class::Object);

		// 2-1. 텍스처 저장
		JSON TexturesJson = JSON::Make(JSON::Class::Object);
		const TMap<EMaterialTextureSlot, UTexture*>& OverriddenTextures = GetOverriddenTextures();
		for (const auto& Pair : OverriddenTextures)
		{
			TexturesJson[std::to_string(static_cast<uint8>(Pair.first))] = Pair.second ? Pair.second->GetFilePath() : "None";
		}
		OverridesJson["Textures"] = TexturesJson;

		// 2-2. 스칼라 파라미터 저장
		JSON ScalarsJson = JSON::Make(JSON::Class::Object);
		const TMap<FString, float>& OverriddenScalars = GetOverriddenScalarParameters();
		for (const auto& Pair : OverriddenScalars)
		{
			ScalarsJson[Pair.first] = Pair.second;
		}
		OverridesJson["Scalars"] = ScalarsJson;

		// 2-3. 벡터 파라미터 저장
		JSON VectorsJson = JSON::Make(JSON::Class::Object);
		const TMap<FString, FLinearColor>& OverriddenVectors = GetOverriddenVectorParameters();
		for (const auto& Pair : OverriddenVectors)
		{
			JSON ColorArray = JSON::Make(JSON::Class::Array);
			ColorArray.append(Pair.second.R);
			ColorArray.append(Pair.second.G);
			ColorArray.append(Pair.second.B);
			ColorArray.append(Pair.second.A);
			VectorsJson[Pair.first] = ColorArray;
		}
		OverridesJson["Vectors"] = VectorsJson;

		InOutHandle["Overrides"] = OverridesJson;
	}
}

void UMaterialInstanceDynamic::CopyParametersFrom(const UMaterialInstanceDynamic* Other)
{
	if (!Other)
	{
		return;
	}

	// 부모는 생성 시(Create) 설정되므로, 여기서는 덮어쓴 값만 복사합니다.
	this->OverriddenTextures = Other->OverriddenTextures;
	this->OverriddenScalarParameters = Other->OverriddenScalarParameters;
	this->OverriddenColorParameters = Other->OverriddenColorParameters;

	this->bIsCachedMaterialInfoDirty = true;
}

UMaterialInstanceDynamic::UMaterialInstanceDynamic(UMaterialInterface* InParentMaterial)
	: ParentMaterial(InParentMaterial)
{
	// 생성자에서는 부모 포인터를 저장하는 것 외에 아무것도 하지 않습니다.
}

UShader* UMaterialInstanceDynamic::GetShader()
{
	// 셰이더는 항상 부모의 것을 그대로 사용합니다.
	if (ParentMaterial)
	{
		return ParentMaterial->GetShader();
	}
	return nullptr;
}

UTexture* UMaterialInstanceDynamic::GetTexture(EMaterialTextureSlot Slot) const
{
	// 1. 이 인스턴스에서 덮어쓴 텍스처가 있는지 먼저 확인합니다.
	UTexture* const* OverriddenTexture = OverriddenTextures.Find(Slot);
	if (OverriddenTexture)
	{
		// 찾았다면(nullptr이 아니라면) 그 값을 반환합니다.
		return *OverriddenTexture;
	}

	// 2. 덮어쓴 값이 없다면, 부모 머티리얼에게 물어봅니다.
	if (ParentMaterial)
	{
		return ParentMaterial->GetTexture(Slot);
	}

	return nullptr;
}

bool UMaterialInstanceDynamic::HasTexture(EMaterialTextureSlot Slot) const
{
	// 1. 이 인스턴스에서 덮어쓴 텍스처가 있는지 먼저 확인합니다.
	// Value가 nullptr일 수도 있으므로, 키의 존재 자체를 확인합니다.
	if (OverriddenTextures.Contains(Slot))
	{
		// 키가 존재하고, 그 값이 nullptr이 아니면 true
		return OverriddenTextures.Find(Slot) != nullptr;
	}

	// 2. 덮어쓴 값이 없다면, 부모 머티리얼에게 물어봅니다.
	if (ParentMaterial)
	{
		return ParentMaterial->HasTexture(Slot);
	}

	return false;
}

const FMaterialInfo& UMaterialInstanceDynamic::GetMaterialInfo() const
{
	if (bIsCachedMaterialInfoDirty)
	{
		if (ParentMaterial)
		{
			// 1. 부모의 MaterialInfo를 복사하여 캐시를 초기화합니다.
			CachedMaterialInfo = ParentMaterial->GetMaterialInfo();
		}

		// 2. 이 인스턴스에 덮어쓴 스칼라 파라미터로 캐시를 수정합니다.
		//    (리플렉션이 없으므로 하드코딩으로 처리)
		for (const auto& Pair : OverriddenScalarParameters)
		{
			if (Pair.first == "SpecularExponent")
			{
				CachedMaterialInfo.SpecularExponent = Pair.second;
			}
			else if (Pair.first == "OpticalDensity")
			{
				CachedMaterialInfo.OpticalDensity = Pair.second;
			}
			else if (Pair.first == "Transparency")
			{
				CachedMaterialInfo.Transparency = Pair.second;
			}
			else if (Pair.first == "BumpMultiplier")
			{
				CachedMaterialInfo.BumpMultiplier = Pair.second;
			}
			else if (Pair.first == "IlluminationModel")
			{
				// IlluminationModel은 int32 이므로 float에서 캐스팅
				CachedMaterialInfo.IlluminationModel = static_cast<int32>(Pair.second);
			}
		}

		// 3. 이 인스턴스에 덮어쓴 벡터 파라미터로 캐시를 수정합니다.
		for (const auto& Pair : OverriddenColorParameters)
		{
			// OverriddenVectorParameters는 FLinearColor (RGBA)로 저장되므로
			// FMaterialInfo의 FVector (RGB)로 변환
			const FVector ColorVec = FVector(Pair.second.R, Pair.second.G, Pair.second.B);

			if (Pair.first == "DiffuseColor")
			{
				CachedMaterialInfo.DiffuseColor = ColorVec;
			}
			else if (Pair.first == "AmbientColor")
			{
				CachedMaterialInfo.AmbientColor = ColorVec;
			}
			else if (Pair.first == "SpecularColor")
			{
				CachedMaterialInfo.SpecularColor = ColorVec;
			}
			else if (Pair.first == "EmissiveColor")
			{
				CachedMaterialInfo.EmissiveColor = ColorVec;
			}
			else if (Pair.first == "TransmissionFilter") // [추가]
			{
				CachedMaterialInfo.TransmissionFilter = ColorVec;
			}
		}

		// 4. 이 인스턴스에 덮어쓴 텍스처 파라미터로 캐시를 수정합니다.
		for (const auto& Pair : OverriddenTextures)
		{
			// Pair.first는 EMaterialTextureSlot, Pair.second는 UTexture*
			UTexture* OverriddenTexture = Pair.second;

			// UTexture*가 유효하면 그 경로를, 아니면 빈 문자열을 사용합니다.
			// UTexture가 UResourceBase를 상속하며, GetFilePath() 함수가 있다고 가정합니다.
			// 만약 GetFilePath()가 없다면 UResourceBase에 정의된 적절한 경로 반환 함수로 대체해야 합니다.
			const FString TexturePath = OverriddenTexture ? OverriddenTexture->GetFilePath() : FString();

			switch (Pair.first)
			{
			case EMaterialTextureSlot::Diffuse:
				CachedMaterialInfo.DiffuseTextureFileName = TexturePath;
				break;
			case EMaterialTextureSlot::Normal:
				CachedMaterialInfo.NormalTextureFileName = TexturePath;
				break;
				// 참고: FMaterialInfo에 정의된 다른 텍스처(Ambient, Specular 등)를
				// 런타임에 오버라이드하려면, EMaterialTextureSlot enum에도 
				// 해당 항목들을 추가하고 여기에 case문을 추가해야 합니다.
			}
		}

		bIsCachedMaterialInfoDirty = false;
	}
	return CachedMaterialInfo;
}

const TArray<FShaderMacro> UMaterialInstanceDynamic::GetShaderMacros() const
{
	if (ParentMaterial)
	{
		return ParentMaterial->GetShaderMacros();
	}

	// ParentMaterial가 비어있으므로 (정적 TArray를 반환하는 것보다) 안전하게 반환
	static const TArray<FShaderMacro> EmptyMacros;
	return EmptyMacros;
}

void UMaterialInstanceDynamic::SetTextureParameterValue(EMaterialTextureSlot Slot, UTexture* Value)
{
	OverriddenTextures.Add(Slot, Value);
	bIsCachedMaterialInfoDirty = true;
}

void UMaterialInstanceDynamic::SetColorParameterValue(const FString& ParameterName, const FLinearColor& Value)
{
	OverriddenColorParameters.Add(ParameterName, Value);
	bIsCachedMaterialInfoDirty = true;
}

void UMaterialInstanceDynamic::SetScalarParameterValue(const FString& ParameterName, float Value)
{
	OverriddenScalarParameters.Add(ParameterName, Value);
	bIsCachedMaterialInfoDirty = true;
}

void UMaterialInstanceDynamic::SetOverriddenTextureParameters(const TMap<EMaterialTextureSlot, UTexture*>& InTextures)
{
	OverriddenTextures = InTextures;
	bIsCachedMaterialInfoDirty = true;
}

void UMaterialInstanceDynamic::SetOverriddenScalarParameters(const TMap<FString, float>& InScalars)
{
	OverriddenScalarParameters = InScalars;
	bIsCachedMaterialInfoDirty = true; // 스칼라 값이 변경되었으므로 캐시를 갱신해야 함
}

void UMaterialInstanceDynamic::SetOverriddenVectorParameters(const TMap<FString, FLinearColor>& InVectors)
{
	OverriddenColorParameters = InVectors;
	bIsCachedMaterialInfoDirty = true; // 벡터 값이 변경되었으므로 캐시를 갱신해야 함
}
