#include "pch.h"
#include "PropertyRenderer.h"
#include "ImGui/imgui.h"
#include "Vector.h"
#include "Color.h"
#include "SceneComponent.h"
#include "ResourceManager.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "Material.h"
#include "BillboardComponent.h"
#include "DecalComponent.h"
#include "StaticMeshComponent.h"
#include "LightComponentBase.h"
#include "LightComponent.h"
#include "ParticleSystemComponent.h"
#include "PointLightComponent.h"
#include "SpotLightComponent.h"
#include "PlatformProcess.h"
#include "SkeletalMeshComponent.h"
#include "USlateManager.h"
#include "BlueprintGraph/AnimationGraph.h"
#include "BlueprintGraph/AnimBlueprintCompiler.h"
#include "ImGui/imgui_curve.hpp"
#include "Source/Runtime/Engine/Animation/AnimationStateMachine.h"
#include"Source/Runtime/Engine/Particle/ParticleSystem.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"

// 정적 멤버 변수 초기화
TArray<FString> UPropertyRenderer::CachedSkeletalMeshPaths;
TArray<FString> UPropertyRenderer::CachedSkeletalMeshItems;
TArray<FString> UPropertyRenderer::CachedStaticMeshPaths;
TArray<FString> UPropertyRenderer::CachedStaticMeshItems;
TArray<FString> UPropertyRenderer::CachedMaterialPaths;
TArray<const char*> UPropertyRenderer::CachedMaterialItems;
TArray<FString> UPropertyRenderer::CachedShaderPaths;
TArray<const char*> UPropertyRenderer::CachedShaderItems;
TArray<FString> UPropertyRenderer::CachedTexturePaths;
TArray<const char*> UPropertyRenderer::CachedTextureItems;
TArray<FString> UPropertyRenderer::CachedSoundPaths;
TArray<const char*> UPropertyRenderer::CachedSoundItems;
TArray<FString> UPropertyRenderer::CachedScriptPaths;
TArray<const char*> UPropertyRenderer::CachedScriptItems;

static bool ItemsGetter(void* Data, int Index, const char** CItem)
{
	TArray<FString>* Items = (TArray<FString>*)Data;

	if (Index < 0 || Index >= Items->Num())
	{
		return false;
	}

	*CItem = (*Items)[Index].c_str();
	return true;
}

bool UPropertyRenderer::RenderProperty(const FProperty& Property, void* ObjectInstance)
{
	// 렌더링에 필요한 리소스가 캐시되었는지 확인하고, 없으면 로드합니다.
	CacheResources();

	bool bChanged = false;

	switch (Property.Type)
	{
	case EPropertyType::Bool:
		bChanged = RenderBoolProperty(Property, ObjectInstance);
		break;

	case EPropertyType::Int32:
		bChanged = RenderInt32Property(Property, ObjectInstance);
		break;

	case EPropertyType::Float:
		bChanged = RenderFloatProperty(Property, ObjectInstance);
		break;

	case EPropertyType::FVector:
		bChanged = RenderVectorProperty(Property, ObjectInstance);
		break;

	case EPropertyType::FLinearColor:
		bChanged = RenderColorProperty(Property, ObjectInstance);
		break;

	case EPropertyType::FString:
		bChanged = RenderStringProperty(Property, ObjectInstance);
		break;

	case EPropertyType::FName:
		bChanged = RenderNameProperty(Property, ObjectInstance);
		break;

	case EPropertyType::ObjectPtr:
		bChanged = RenderObjectPtrProperty(Property, ObjectInstance);
		break;

	case EPropertyType::Struct:
		bChanged = RenderStructProperty(Property, ObjectInstance);
		break;

	case EPropertyType::Texture:
		bChanged = RenderTextureProperty(Property, ObjectInstance);
		break;

	case EPropertyType::SkeletalMesh:
		bChanged = RenderSkeletalMeshProperty(Property, ObjectInstance);
		break;

	case EPropertyType::StaticMesh:
		bChanged = RenderStaticMeshProperty(Property, ObjectInstance);
		break;

	case EPropertyType::Material:
		bChanged = RenderMaterialProperty(Property, ObjectInstance);
		break;

	case EPropertyType::ParticleSystem:
		bChanged = RenderParticleSystemProperty(Property, ObjectInstance);
		break;

	case EPropertyType::SRV:
		bChanged = RenderSRVProperty(Property, ObjectInstance);
		break;

	case EPropertyType::ScriptFile:
		bChanged = RenderScriptFileProperty(Property, ObjectInstance);
		break;

	case EPropertyType::Curve:
		bChanged = RenderCurveProperty(Property, ObjectInstance);
		break;

	case EPropertyType::Array:
		switch (Property.InnerType)
		{
		case EPropertyType::Material:
			bChanged = RenderMaterialArrayProperty(Property, ObjectInstance);
			break;
		case EPropertyType::Sound:
			// Render array of USound* via simple combo per element
			{
				TArray<USound*>* Arr = Property.GetValuePtr<TArray<USound*>>(ObjectInstance);
				if (Arr)
				{
					if (ImGui::Button("Add Sound")) { Arr->Add(nullptr); bChanged = true; }
					for (int i = 0; i < Arr->Num(); ++i)
					{
						ImGui::PushID(i);
						USound* cur = (*Arr)[i];
						USound* neu = nullptr;
						FString label = FString(Property.Name) + " [" + std::to_string(i) + "]";
						if (RenderSoundSelectionComboSimple(label.c_str(), cur, neu))
						{
							(*Arr)[i] = neu; bChanged = true;
						}
						ImGui::SameLine();
						if (ImGui::Button("Remove")) { Arr->RemoveAt(i); --i; bChanged = true; ImGui::PopID(); continue; }
						ImGui::PopID();
					}
				}
			}
			break;
		}
		break;

	case EPropertyType::Sound:
		bChanged = RenderSoundProperty(Property, ObjectInstance);
		break;

	case EPropertyType::PhysMaterialPreset:
		bChanged = RenderPhysMaterialPresetProperty(Property, ObjectInstance);
		break;

	case EPropertyType::CombineMode:
		bChanged = RenderCombineModeProperty(Property, ObjectInstance);
		break;

	default:
		ImGui::Text("%s: [Unknown Type]", Property.Name);
		break;
	}

	if (Property.Tooltip && Property.Tooltip[0] != '\0' && ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s", Property.Tooltip);
	}

	// SceneComponent는 Transform 프로퍼티가 변경되면 Setter를 통해 동기화
	if (bChanged && ObjectInstance)
	{
		UObject* Obj = static_cast<UObject*>(ObjectInstance);
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Obj))
		{
			// 프로퍼티 이름으로 Transform 프로퍼티 판별 후 Setter 호출
			if (strcmp(Property.Name, "RelativeRotationEuler") == 0)
			{
				FVector* EulerValue = Property.GetValuePtr<FVector>(ObjectInstance);
				SceneComponent->SetRelativeRotationEuler(*EulerValue);
			}
			else if (strcmp(Property.Name, "RelativeLocation") == 0)
			{
				FVector* LocationValue = Property.GetValuePtr<FVector>(ObjectInstance);
				SceneComponent->SetRelativeLocation(*LocationValue);
			}
			else if (strcmp(Property.Name, "RelativeScale") == 0)
			{
				FVector* ScaleValue = Property.GetValuePtr<FVector>(ObjectInstance);
				SceneComponent->SetRelativeScale(*ScaleValue);
			}
		}

		// LightComponent는 Light 프로퍼티가 변경되면 UpdateLightData를 호출하여 동기화
		if (ULightComponentBase* LightComponent = Cast<ULightComponentBase>(Obj))
		{
			// Light 관련 프로퍼티가 변경되면 UpdateLightData 호출
			if (strcmp(Property.Name, "LightColor") == 0 ||
				strcmp(Property.Name, "Intensity") == 0 ||
				strcmp(Property.Name, "Temperature") == 0 ||
				strcmp(Property.Name, "bIsVisible") == 0 ||
				strcmp(Property.Name, "bIsActive") == 0 ||
				strcmp(Property.Name, "AttenuationRadius") == 0 ||
				strcmp(Property.Name, "bUseInverseSquareFalloff") == 0 ||
				strcmp(Property.Name, "FalloffExponent") == 0 ||
				strcmp(Property.Name, "InnerConeAngle") == 0 ||
				strcmp(Property.Name, "OuterConeAngle") == 0)

			{
				LightComponent->UpdateLightData();
			}
		}
	}

	return bChanged;
}

void UPropertyRenderer::RenderProperties(const TArray<FProperty>& Properties, UObject* Object)
{
	if (Properties.IsEmpty())
		return;

	// 1. 카테고리 (삽입 순서 보장용 TArray)
	TArray<TPair<FString, TArray<const FProperty*>>> CategorizedProps;

	// 2. 카테고리 인덱스 (빠른 검색용 TMap)
	TMap<FString, int32> CategoryIndexMap;

	for (const FProperty& Prop : Properties)
	{
		if (Prop.bIsEditAnywhere)
		{
			FString CategoryName = Prop.Category ? Prop.Category : "Default";

			// 3. 맵에서 이미 카테고리가 있는지 빠르게 검색
			int32* IndexPtr = CategoryIndexMap.Find(CategoryName);

			if (IndexPtr)
			{
				// 4-A. 이미 존재하는 카테고리면, TArray의 해당 인덱스에 프로퍼티만 추가
				CategorizedProps[*IndexPtr].second.Add(&Prop);
			}
			else
			{
				// 4-B. 새 카테고리면, TArray에 새 항목을 추가
				TArray<const FProperty*> NewPropArray;
				NewPropArray.Add(&Prop);

				// TArray에 실제 데이터 추가 (MakePair는 std::make_pair와 동일)
				int32 NewIndex = CategorizedProps.Add(TPair<FString, TArray<const FProperty*>>(CategoryName, NewPropArray));

				// TMap에 "이 카테고리는 TArray의 NewIndex에 있다"고 기록
				CategoryIndexMap.Add(CategoryName, NewIndex);
			}
		}
	}

	// 5. TArray를 순회하면 '삽입 순서'가 완벽하게 보장됩니다.
	// (순서를 뒤집고 싶다면 여기서 CategorizedProps.rbegin() / rend() 사용)
	for (auto& Pair : CategorizedProps)
	{
		const FString& Category = Pair.first;
		const TArray<const FProperty*>& Props = Pair.second;

		ImGui::Separator();
		ImGui::Text("%s", Category.c_str());

		for (const FProperty* Prop : Props)
		{
			ImGui::PushID(Prop); // 프로퍼티 포인터로 고유 ID 푸시
			RenderProperty(*Prop, Object);
			ImGui::PopID();
		}
	}
}

void UPropertyRenderer::RenderAllProperties(UObject* Object)
{
	if (!Object)
		return;

	UClass* Class = Object->GetClass();
	const TArray<FProperty>& Properties = Class->GetProperties();

	RenderProperties(Properties, Object);
}

void UPropertyRenderer::RenderAllPropertiesWithInheritance(UObject* Object)
{
	if (!Object)
		return;

	UClass* Class = Object->GetClass();
	const TArray<FProperty>& AllProperties = Class->GetAllProperties();

	RenderProperties(AllProperties, Object);

	if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Object))
	{
		RenderPrimitiveComponentDetails(Prim);
	}
	if (USkeletalMeshComponent* Skel = Cast<USkeletalMeshComponent>(Object))
	{
		RenderSkeletalMeshComponentDetails(Skel);
	}
}

// ===== 리소스 캐싱 =====

void UPropertyRenderer::CacheResources()
{
	// NOTE: 추후에 변경된 것이 있으면 다시 캐싱하도록 변경 필요
	//ClearResourcesCache();

	UResourceManager& ResMgr = UResourceManager::GetInstance();


	// 1. 스태틱 메시
	if (CachedStaticMeshPaths.IsEmpty() && CachedStaticMeshItems.IsEmpty())
	{
		CachedStaticMeshPaths = ResMgr.GetAllFilePaths<UStaticMesh>();
		for (const FString& path : CachedStaticMeshPaths)
		{
			// 파일명만 추출해서 표시
			std::filesystem::path fsPath(path);
			CachedStaticMeshItems.push_back(fsPath.filename().string());
		}
		CachedStaticMeshPaths.Insert("", 0);
		CachedStaticMeshItems.Insert("None", 0);
	}

	if (CachedSkeletalMeshPaths.IsEmpty() && CachedSkeletalMeshItems.IsEmpty())
	{
		CachedSkeletalMeshPaths = ResMgr.GetAllFilePaths<USkeletalMesh>();
		for (const FString& path : CachedSkeletalMeshPaths)
		{
			// 파일명만 추출해서 표시
			std::filesystem::path fsPath(path);
			CachedSkeletalMeshItems.push_back(fsPath.filename().string());
		}
		CachedSkeletalMeshPaths.Insert("", 0);
		CachedSkeletalMeshItems.Insert("None", 0);
	}

	// 2. 머티리얼
	if (CachedMaterialPaths.IsEmpty() && CachedTexturePaths.IsEmpty())
	{
		CachedMaterialPaths = ResMgr.GetAllFilePaths<UMaterial>();
		CachedMaterialItems.Add("None");
		for (const FString& path : CachedMaterialPaths)
		{
			CachedMaterialItems.push_back(path.c_str());
		}
	}

	// 3. 셰이더
	if (CachedShaderPaths.IsEmpty() && CachedShaderItems.IsEmpty())
	{
		CachedShaderPaths = ResMgr.GetAllFilePaths<UShader>();
		CachedShaderItems.Add("None");
		for (const FString& path : CachedShaderPaths)
		{
			CachedShaderItems.push_back(path.c_str());
		}
	}

	// 4. 텍스처
	if (CachedTexturePaths.IsEmpty() && CachedTextureItems.IsEmpty())
	{
		CachedTexturePaths = ResMgr.GetAllFilePaths<UTexture>();
		CachedTextureItems.Add("None");
		for (const FString& path : CachedTexturePaths)
		{
			CachedTextureItems.push_back(path.c_str());
		}
	}

	// --- 5. 스크립트 파일 ---
	if (CachedScriptPaths.IsEmpty() && CachedScriptItems.IsEmpty())
	{
		// 1. "None" 항목 추가 (경로는 "", UI 표시는 "<스크립트 생성>")
		CachedScriptPaths.Add("");
		CachedScriptItems.Add("<스크립트 생성>");

		// 2. 파일 시스템 스캔 (Content/Scripts/ 디렉토리)
		const FString ScriptDir = GDataDir + "/Scripts/";
		if (fs::exists(ScriptDir) && fs::is_directory(ScriptDir))
		{
			for (const auto& Entry : fs::recursive_directory_iterator(ScriptDir))
			{
				if (Entry.is_regular_file() && Entry.path().extension() == ".lua")
				{
					FString Path = WideToUTF8(Entry.path().generic_wstring());
					CachedScriptPaths.Add(NormalizePath(Path));
				}
			}
		}

		// 3. 콤보박스 아이템 채우기 (경로가 모두 추가된 후에!)
		// "None" 항목(인덱스 0)은 이미 추가했으므로 1부터 시작
		for (size_t i = 1; i < CachedScriptPaths.size(); ++i)
		{
			// c_str() 포인터는 CachedScriptPaths가 재할당되지 않는 한 유효합니다.
			// (주의: ClearResourcesCache() 호출 전까지 유효)
			CachedScriptItems.Add(CachedScriptPaths[i].c_str());
		}
	}
    // 5. Sound (.wav)
    if (CachedSoundPaths.IsEmpty() && CachedSoundItems.IsEmpty())
    {
        CachedSoundPaths = ResMgr.GetAllFilePaths<USound>();
        CachedSoundItems.Add("None");
        for (const FString& path : CachedSoundPaths)
        {
            CachedSoundItems.push_back(path.c_str());
        }
    }
}

void UPropertyRenderer::ClearResourcesCache()
{
	CachedSkeletalMeshPaths.Empty();
	CachedSkeletalMeshItems.Empty();
	CachedStaticMeshPaths.Empty();
	CachedStaticMeshItems.Empty();
	CachedMaterialPaths.Empty();
	CachedMaterialItems.Empty();
	CachedShaderPaths.Empty();
	CachedShaderItems.Empty();
	CachedTexturePaths.Empty();
	CachedTextureItems.Empty();
	CachedSoundPaths.Empty();
	CachedSoundItems.Empty();
	CachedScriptPaths.Empty();
	CachedScriptItems.Empty();
}

// ===== 타입별 렌더링 구현 =====

bool UPropertyRenderer::RenderBoolProperty(const FProperty& Prop, void* Instance)
{
	bool* Value = Prop.GetValuePtr<bool>(Instance);
	return ImGui::Checkbox(Prop.Name, Value);
}

bool UPropertyRenderer::RenderInt32Property(const FProperty& Prop, void* Instance)
{
	int32* Value = Prop.GetValuePtr<int32>(Instance);
	
	// 드래그 속도를 계산합니다.
	const float TotalRange = (float)Prop.MaxValue - (float)Prop.MinValue;
	const float ProportionalSpeed = TotalRange * 0.0005f;
	const float DragSpeed = FMath::Max(ProportionalSpeed, 0.01f);	// 최소 속도

	return ImGui::DragInt(Prop.Name, Value, DragSpeed, (int)Prop.MinValue, (int)Prop.MaxValue);
}

bool UPropertyRenderer::RenderFloatProperty(const FProperty& Prop, void* Instance)
{
	float* Value = Prop.GetValuePtr<float>(Instance);
	char DisplayFormat[16]; // 포맷 문자열을 저장할 버퍼 (예: "%.5f")

	// Min과 Max가 둘 다 0이면 범위 제한 없음
	if (Prop.MinValue == 0.0f && Prop.MaxValue == 0.0f)
	{
		// 범위가 없으므로 기본값 (소수점 3자리) 사용
		snprintf(DisplayFormat, 16, "%%.3f"); // "%.3f"
		return ImGui::DragFloat(Prop.Name, Value, 1.0f, 0.0f, 0.0f, DisplayFormat);
	}
	else
	{
		// 1. 드래그 속도 계산
		// (안전하게 절대값을 사용)
		const float TotalRange = std::fabsf((float)Prop.MaxValue - (float)Prop.MinValue);
		const float ProportionalSpeed = TotalRange * 0.0005f;
		const float DragSpeed = FMath::Max(ProportionalSpeed, 0.00001f);

		// 2. TotalRange에 비례하는 소수점 자리수 계산
		int DecimalPlaces = 3; // 기본 3자리

		if (TotalRange > 0.0f) // log10(0) 방지
		{
			// 범위 1.0을 기준으로 3자리를 설정합니다.
			// 범위가 10배 커지면 (10.0) -> 2자리가 됩니다. (3 - 1)
			// 범위가 10배 작아지면 (0.1) -> 4자리가 됩니다. (3 - (-1))
			DecimalPlaces = static_cast<int>(3.0 - std::floor(std::log10(TotalRange)));
		}

		// 3. 자리수를 0~5 사이로 제한 (사용자 요청)
		// (FMath::Clamp가 C++17의 std::clamp와 동일하다고 가정)
		DecimalPlaces = FMath::Clamp(DecimalPlaces, 0, 5);

		// 4. 포맷 문자열 동적 생성
		// "%%"는 리터럴 '%' 문자를 의미합니다.
		// (예: DecimalPlaces가 5이면 "%.5f" 문자열이 생성됨)
		snprintf(DisplayFormat, 16, "%%.%df", DecimalPlaces);

		// 5. ImGui 호출
		return ImGui::DragFloat(Prop.Name, Value, DragSpeed, Prop.MinValue, Prop.MaxValue, DisplayFormat);
	}
}

bool UPropertyRenderer::RenderVectorProperty(const FProperty& Prop, void* Instance)
{
	// Transform 프로퍼티 (Location, Rotation, Scale) 감지
	bool bIsTransformProperty =
		strcmp(Prop.Name, "RelativeLocation") == 0 ||
		strcmp(Prop.Name, "RelativeRotationEuler") == 0 ||
		strcmp(Prop.Name, "RelativeScale") == 0;

	if (bIsTransformProperty)
	{
		return RenderTransformProperty(Prop, Instance);
	}

	// 일반 Vector 프로퍼티는 기본 렌더링
	FVector* Value = Prop.GetValuePtr<FVector>(Instance);
	return ImGui::DragFloat3(Prop.Name, &Value->X, 0.1f);
}

bool UPropertyRenderer::RenderColorProperty(const FProperty& Prop, void* Instance)
{
	FLinearColor* Value = Prop.GetValuePtr<FLinearColor>(Instance);
	return ImGui::ColorEdit4(Prop.Name, &Value->R);
}

bool UPropertyRenderer::RenderStringProperty(const FProperty& Prop, void* Instance)
{
	FString* Value = Prop.GetValuePtr<FString>(Instance);

	// ImGui::InputText는 char 버퍼를 사용하므로 변환 필요
	char Buffer[256];
	strncpy_s(Buffer, Value->c_str(), sizeof(Buffer) - 1);
	Buffer[sizeof(Buffer) - 1] = '\0';

	if (ImGui::InputText(Prop.Name, Buffer, sizeof(Buffer)))
	{
		*Value = Buffer;
		return true;
	}

	return false;
}

bool UPropertyRenderer::RenderNameProperty(const FProperty& Prop, void* Instance)
{
	FName* ValuePtr = Prop.GetValuePtr<FName>(Instance);
	if (!ValuePtr)
	{
		return false;
	}

	ImGuiID WidgetID = ImGui::GetID(ValuePtr);

	// 2. 위젯 ID별로 편집용 'char' 버퍼를 저장할 static 맵
	//    TArray<char>를 사용하여 버퍼 메모리를 관리합니다.
	static TMap<ImGuiID, TArray<char>> EditBuffers;

	// 3. 이 위젯 ID에 해당하는 버퍼를 가져옵니다.
	TArray<char>& EditBuffer = EditBuffers[WidgetID];

	const SIZE_T BufferSize = 256; // FName에 충분한 버퍼 크기

	// 4. 버퍼가 비어있거나 활성 상태가 아니면 FName 값으로 채웁니다.
	if (EditBuffer.empty() || !ImGui::IsItemActive())
	{
		FString CurrentNameStr = ValuePtr->ToString();
		EditBuffer.assign(BufferSize, '\0'); // 버퍼를 0으로 초기화
		// FString의 내용을 버퍼로 복사 (strncpy_s 또는 유사 함수 사용 권장)
		strncpy_s(EditBuffer.data(), EditBuffer.size(), CurrentNameStr.c_str(), CurrentNameStr.length());
	}

	// 5. InputText 렌더링
	//    이제 EditBuffer.data() (char* 타입)를 전달합니다.
	bool bValueWasChanged = false;
	if (ImGui::InputText(Prop.Name, EditBuffer.data(), BufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
	{
		// 6. 엔터/포커스 해제 시, 버퍼의 텍스트(char*)로 FName을 생성
		FString BufferText(EditBuffer.data());

		if (BufferText != ValuePtr->ToString())
		{
			*ValuePtr = FName(BufferText);
			bValueWasChanged = true;
		}
	}

	return bValueWasChanged;
}

bool UPropertyRenderer::RenderObjectPtrProperty(const FProperty& Prop, void* Instance)
{
	UObject** ObjPtr = Prop.GetValuePtr<UObject*>(Instance);

	if (*ObjPtr)
	{
		// 객체가 있으면 클래스 이름과 객체 이름 표시
		UClass* ObjClass = (*ObjPtr)->GetClass();
		FString ObjName = (*ObjPtr)->GetName();
		ImGui::Text("%s: %s (%s)", Prop.Name, ObjName.c_str(), ObjClass->Name);
	}
	else
	{
		// nullptr이면 None 표시
		ImGui::Text("%s: None", Prop.Name);
	}

	return false; // 읽기 전용
}

bool UPropertyRenderer::RenderStructProperty(const FProperty& Prop, void* Instance)
{
	// Struct는 읽기 전용으로 표시
	// FVector, FLinearColor 등 주요 타입은 이미 별도로 처리됨
	ImGui::Text("%s: [Struct]", Prop.Name);
	return false;
}

bool UPropertyRenderer::RenderTextureProperty(const FProperty& Prop, void* Instance)
{
	UTexture** TexturePtr = Prop.GetValuePtr<UTexture*>(Instance);
	UTexture* CurrentTexture = *TexturePtr;
	UTexture* NewTexture = nullptr;

	// 헬퍼 함수를 호출하여 텍스처 콤보박스를 렌더링합니다.
	bool bChanged = RenderTextureSelectionCombo(Prop.Name, CurrentTexture, NewTexture);

	if (bChanged)
	{
		// 프로퍼티 포인터 업데이트
		*TexturePtr = NewTexture;

		// 새 텍스처 경로 (None일 경우 빈 문자열)
		FString NewPath = (NewTexture) ? NewTexture->GetFilePath() : "";

		// 컴포넌트별 Setter 호출
		UObject* Obj = static_cast<UObject*>(Instance);
		if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Obj))
		{
			Billboard->SetTexture(NewPath);
		}
		else if (UDecalComponent* Decal = Cast<UDecalComponent>(Obj))
		{
			Decal->SetDecalTexture(NewPath);
		}

		return true;
	}

	return false;
}


bool UPropertyRenderer::RenderSoundProperty(const FProperty& Prop, void* Instance)
{
	USound** SoundPtr = Prop.GetValuePtr<USound*>(Instance);
	USound* CurrentSound = *SoundPtr;
	USound* NewSound = nullptr;

	// 헬퍼 함수를 호출하여 텍스처 콤보박스를 렌더링합니다.
	bool bChanged = RenderSoundSelectionComboSimple(Prop.Name, CurrentSound, NewSound);

	if (bChanged)
	{
		// 프로퍼티 포인터 업데이트
		*SoundPtr = NewSound;

		// 새 텍스처 경로 (None일 경우 빈 문자열)
		FString NewPath = (NewSound) ? NewSound->GetFilePath() : "";

		// 컴포넌트별 Setter 호출
		UObject* Obj = static_cast<UObject*>(Instance);
		if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Obj))
		{
			Billboard->SetTexture(NewPath);
		}
		else if (UDecalComponent* Decal = Cast<UDecalComponent>(Obj))
		{
			Decal->SetDecalTexture(NewPath);
		}

		return true;
	}

	return false;
}


bool UPropertyRenderer::RenderSRVProperty(const FProperty& Prop, void* Instance)
{
	ULightComponent* LightComp = static_cast<ULightComponent*>(Instance);
	if (!LightComp) return false;

	FLightManager* LightManager = GWorld->GetLightManager();
	if (!LightManager) return false;

	//@TODO PropertyRenderer 와 Component 간 결합 줄이기
	// Point Light와 Spot Light 구분
	USpotLightComponent* SpotLight = Cast<USpotLightComponent>(LightComp);
	UPointLightComponent* PointLight = Cast<UPointLightComponent>(LightComp);

	// Point Light (Spot Light 제외)
	if (PointLight && !SpotLight)
	{
		int32 CubeSliceIndex = -1;
		if (!LightManager->GetCachedShadowCubeSliceIndex(LightComp, CubeSliceIndex) || CubeSliceIndex < 0)
		{
			ImGui::Text("Cube Shadow Map not available");
			return false;
		}
		return RenderPointLightCubeShadowMap(LightManager, LightComp, CubeSliceIndex);
	}

	// Spot Light 및 기타 2D Shadow Map
	ID3D11ShaderResourceView* AtlasSRV = LightManager->GetShadowAtlasSRV2D();
	if (!AtlasSRV)
	{
		ImGui::Text("Shadow Atlas SRV is null");
		return false;
	}
	return RenderSpotLightShadowMap(LightManager, LightComp, AtlasSRV);
}

bool UPropertyRenderer::RenderScriptFileProperty(const FProperty& Property, void* ObjectInstance)
{
	// 콤보박스 전용 텍스트 필터를 static으로 선언합니다.
	static ImGuiTextFilter ScriptComboFilter;
	static char NewScriptNameBuffer[128] = "NewScript";

	bool bChanged = false;
	FString* FilePath = Property.GetValuePtr<FString>(ObjectInstance);
	if (!FilePath) return false;

	// 1. 현재 선택된 아이템 찾기
	int CurrentItem = 0; // 0번 인덱스("<스크립트 생성>")가 기본값
	for (int i = 0; i < CachedScriptPaths.Num(); ++i)
	{
		// 경로가 비어있지 않고, 캐시된 경로와 일치하는 경우
		if (!FilePath->empty() && CachedScriptPaths[i] == *FilePath)
		{
			CurrentItem = i;
			break;
		}
	}

	// 2. 콤보 박스 렌더링
	ImGui::Text("%s", Property.Name);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	ImGui::PushID(Property.Name); // 고유 ID

	// 콤보박스에 표시될 텍스트
	const char* PreviewText = CachedScriptItems[CurrentItem];
	FString FormattedPreviewText; // (missing) 텍스트를 담을 임시 FString
	bool bFileIsMissing = false;

	// 실제 파일이 존재하는지 확인
	if (!FilePath->empty())
	{
		if (!fs::exists(UTF8ToWide(*FilePath)))
		{
			bFileIsMissing = true;
			// (missing) 태그가 붙은 임시 텍스트 생성
			FormattedPreviewText = "(missing) " + *FilePath;
			// 콤보박스 프리뷰가 임시 텍스트를 가리키도록 함
			PreviewText = FormattedPreviewText.c_str();
		}
	}

	if (ImGui::BeginCombo("##ScriptCombo", PreviewText))
	{
		// 콤보박스 팝업이 '처음 열린 프레임'에만
		if (ImGui::IsWindowAppearing())
		{
			// 스크립트 목록 다시 로드하기 위해서
			CachedScriptPaths.Empty();
			CachedScriptItems.Empty();

			// "다음에 그려질 위젯" (Search)에 포커스를 한 번만 줍니다.
			ImGui::SetKeyboardFocusHere(0);
		}

		// 검색창을 렌더링하고, 포커스 여부를 '먼저' 변수에 저장
		ScriptComboFilter.Draw("Search");
		bool bFilterIsFocused = ImGui::IsItemFocused();

		for (int i = 0; i < CachedScriptItems.Num(); ++i)
		{
			if (i == 0 || ScriptComboFilter.PassFilter(CachedScriptItems[i]))
			{
				const bool bIsSelected = (CurrentItem == i);
				if (ImGui::Selectable(CachedScriptItems[i], bIsSelected))
				{
					CurrentItem = i;
					*FilePath = CachedScriptPaths[i];
					bChanged = true;

					// 0번("<스크립트 생성>")이 선택되었는지 확인
					if (i == 0)
					{
						// 검색창(Filter)의 텍스트가 비어있지 않다면
						if (ScriptComboFilter.InputBuf[0] != '\0')
						{
							// 검색창의 텍스트를 'NewScriptNameBuffer'로 복사
							strncpy_s(NewScriptNameBuffer,
								IM_ARRAYSIZE(NewScriptNameBuffer),
								ScriptComboFilter.InputBuf,
								_TRUNCATE);
						}
					}

					// 항목이 선택되었으므로 검색창은 비웁니다.
					ScriptComboFilter.Clear();
				}
				if (bIsSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopID();

	// 사라진 파일 표시
	if (bFileIsMissing)
	{
		// 파일이 존재하지 않음 (빨간색 텍스트 + 선택 해제 버튼)
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		ImGui::TextWrapped("파일이 존재하지 않습니다. 경로를 다시 설정하십시오.");
		ImGui::PopStyleColor();

		ImGui::SameLine();
		if (ImGui::SmallButton("선택 해제"))
		{
			*FilePath = ""; // 경로를 비움
			bChanged = true;
			// CurrentItem은 다음 프레임에 0으로 다시 계산될 것임
		}
	}
	// 3. 스크립트 생성 UI (선택된 파일이 없을 때만 표시)
	else if (FilePath->empty() || CurrentItem == 0)
	{
		ImGui::Separator();

		// "스크립트 생성 메뉴"
		ImGui::InputText("스크립트 명", NewScriptNameBuffer, IM_ARRAYSIZE(NewScriptNameBuffer));
		ImGui::SameLine();

		if (ImGui::Button("스크립트 생성"))
		{
			// 1. 경로 및 확장자 설정
			const FString* ExtPtr = Property.Metadata.Find(FName("FileExtension"));
			FString Extension = (ExtPtr) ? *ExtPtr : ".lua";
			if (Extension[0] != '.') Extension = "." + Extension;

			// (참고: 스크립트 기본 경로는 "Content/Scripts/"로 가정)
			FString BaseDir = GDataDir + "/Scripts/";
			FString NewFileName(NewScriptNameBuffer);
			FString RelativePath = BaseDir + NewFileName + Extension;

			// 템플릿 파일 경로 정의
			const FString TemplatePath = GDataDir + "/Templates/template.lua";

			// 2. 디렉토리 생성 (없을 경우)
			fs::create_directories(UTF8ToWide(BaseDir));

			// 3. 템플릿 파일 존재 여부 확인
			if (!fs::exists(UTF8ToWide(TemplatePath)))
			{
				UE_LOG("[error] 템플릿 파일(template.lua)을 찾을 수 없습니다.");
			}
			// 4. 생성할 파일이 이미 존재하는지 중복 체크
			else if (fs::exists(UTF8ToWide(RelativePath)))
			{
				UE_LOG(("[error] '" + NewFileName + Extension + "' 파일이 이미 존재합니다.").c_str());
			}
			else
			{
				try
				{
					// 템플릿 파일을 새 경로로 복사
					fs::copy(UTF8ToWide(TemplatePath), UTF8ToWide(RelativePath), fs::copy_options::overwrite_existing);

					// 4. 새 파일을 캐시에 즉시 추가
					CachedScriptPaths.Add(RelativePath);

					CachedScriptItems.Add(CachedScriptPaths.back().c_str());

					// 5. 현재 프로퍼티에 새 경로 설정
					*FilePath = RelativePath;
					bChanged = true;
				}
				catch (const fs::filesystem_error& E)
				{
					UE_LOG(("[error] 파일 복사에 실패했습니다. " + FString(E.what())).c_str());
				}
			}

			if (bChanged)
			{
				strcpy_s(NewScriptNameBuffer, IM_ARRAYSIZE(NewScriptNameBuffer), "NewScript");
			}
		}

		ImGui::Separator();
	}
	// 스크립트 파일이 있을 때
	else
	{
		if (ImGui::Button("편집하기"))
		{
			FPlatformProcess::OpenFileInDefaultEditor(UTF8ToWide(*FilePath));
		}
	}

	return bChanged;
}

bool UPropertyRenderer::RenderCurveProperty(const FProperty& Prop, void* Instance)
{
	// 1. 프로퍼티에서 실제 데이터(float[4])에 대한 포인터를 가져옵니다.
    //    GetValuePtr<float>는 배열의 첫 번째 요소(float*)를 반환합니다.
	float* PropertyData = Prop.GetValuePtr<float>(Instance);
	if (!PropertyData)
	{
		ImGui::Text("%s: [Invalid Data]", Prop.Name);
		return false;
	}

	// 2. ImGui::Bezier 위젯은 float[5] 배열을 필요로 합니다 (P[4] = 프리셋 인덱스).
	//    이 위젯을 위한 임시 배열을 스택에 생성합니다.
	float EditorData[5];

	//    실제 데이터(float[4])를 임시 배열의 앞부분(EditorData[0]~[3])으로 복사합니다.
	memcpy(EditorData, PropertyData, sizeof(float) * 4);

	// 3. 프리셋 인덱스(EditorData[4])는 UI 상태이므로, 객체 데이터에 저장되지 않습니다.
	//    ImGui의 내부 상태 저장소를 사용하여 이 UI 상태를 프레임 간에 유지합니다.

	//    이 프로퍼티 위젯 전체에 대한 고유 ID를 설정합니다.
	ImGui::PushID(Prop.Name);
	ImGui::PushID(Instance); // 동일한 객체라도 다른 인스턴스일 수 있으므로 포인터로 ID 추가

	ImGuiStorage* storage = ImGui::GetStateStorage();
	// 이 프로퍼티의 "PresetIndex" 상태를 위한 고유 ID 생성
	ImGuiID presetId = ImGui::GetID("PresetIndex");

	// 4. 저장된 프리셋 인덱스 로드, 없으면 0.0f (일반적으로 "Linear" 또는 "Custom")
	EditorData[4] = storage->GetFloat(presetId, 0.0f);

	// 5. Bezier 위젯을 호출합니다. 
	//    Prop.Name을 레이블로 사용합니다. (PushID로 고유성 보장됨)
	bool bCurveChanged = ImGui::Bezier(Prop.Name, EditorData);

	// 6. 값이 변경되었는지 확인합니다.
	if (bCurveChanged)
	{
		// 7. 변경된 곡선 값(EditorData[0]~[3])을 
		//    실제 프로퍼티(PropertyData)에 다시 복사합니다.
		memcpy(PropertyData, EditorData, sizeof(float) * 4);

		// 8. 변경된 프리셋 인덱스(EditorData[4])를 ImGui 저장소에 다시 저장합니다.
		storage->SetFloat(presetId, EditorData[4]);
	}

	ImGui::PopID(); // Instance ID
	ImGui::PopID(); // Prop.Name ID

	// 툴팁 렌더링은 부모 함수인 RenderProperty에서 이미 처리하고 있습니다.

	return bCurveChanged;
}

bool UPropertyRenderer::RenderPointLightCubeShadowMap(FLightManager* LightManager, ULightComponent* LightComp, int32 CubeSliceIndex)
{
	ImGui::Text("Point Light Cube Shadow Map (Slice %d)", CubeSliceIndex);
	ImGui::Spacing();

	const char* FaceNames[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
	const int32 FaceSize = 128;

	// 2x3 그리드로 Cube Map의 6개 면 표시
	for (int row = 0; row < 2; ++row)
	{
		for (int col = 0; col < 3; ++col)
		{
			int32 faceIdx = row * 3 + col;
			ID3D11ShaderResourceView* FaceSRV = LightManager->GetShadowCubeFaceSRV(CubeSliceIndex, faceIdx);

			ImGui::BeginGroup();
			ImGui::Text("%s", FaceNames[faceIdx]);
			if (FaceSRV)
			{
				ImGui::Image((ImTextureID)FaceSRV, ImVec2(FaceSize, FaceSize));
			}
			else
			{
				ImGui::Text("N/A");
			}
			ImGui::EndGroup();

			if (col < 2) ImGui::SameLine();
		}
	}

	return false;
}

bool UPropertyRenderer::RenderSpotLightShadowMap(FLightManager* LightManager, ULightComponent* LightComp, ID3D11ShaderResourceView* AtlasSRV)
{
	FShadowMapData ShadowData;
	if (!LightManager->GetCachedShadowData(LightComp, 0, ShadowData))
	{
		ImGui::Text("Shadow data not available");
		return false;
	}

	// Atlas UV 계산
	ImVec2 uv0(ShadowData.AtlasScaleOffset.Z, ShadowData.AtlasScaleOffset.W);
	ImVec2 uv1(ShadowData.AtlasScaleOffset.Z + ShadowData.AtlasScaleOffset.X,
		ShadowData.AtlasScaleOffset.W + ShadowData.AtlasScaleOffset.Y);

	// Shadow Map 표시
	ImGui::Text("Shadow Map:");
	ImGui::Image((ImTextureID)AtlasSRV, ImVec2(256, 256), uv0, uv1);

	// 전체 Atlas 표시 (디버깅용)
	ImGui::Text("Full Atlas:");
	ImGui::Image((ImTextureID)AtlasSRV, ImVec2(256, 256), ImVec2(0, 0), ImVec2(1, 1));

	return true;
}

bool UPropertyRenderer::RenderPrimitiveComponentDetails(UPrimitiveComponent* PrimitiveComponent)
{
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Collision Settings");

	ImGui::Checkbox("Use Collision Override", &PrimitiveComponent->bOverrideCollisionSetting);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("뷰어에서 세팅된 메시의 기본 콜리전 값을 오버라이드 합니다.");
	}
	int CurrentIndex = static_cast<int>(PrimitiveComponent->CollisionEnabled);
	static const char* CollisionItems[] = {
		"NoCollision",
		"QueryOnly (Trigger)",
		"Query + Physics",
	};
	if (ImGui::Combo("Collision Settings", &CurrentIndex, CollisionItems, IM_ARRAYSIZE(CollisionItems)))
	{
		PrimitiveComponent->CollisionEnabled = static_cast<ECollisionState>(CurrentIndex);
	}

	return true;
}

bool UPropertyRenderer::RenderSkeletalMeshComponentDetails(USkeletalMeshComponent* Component)
{
	bool bChanged = false;
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Physics Asset Override");

	USkeletalMesh* Mesh = Component->GetSkeletalMesh();
	UPhysicsAsset* DefaultAsset = Mesh ? Mesh->PhysicsAsset : nullptr;
	if (DefaultAsset)
	{
		ImGui::Text("Mesh Default: %s", DefaultAsset->GetName().ToString().c_str());
	}
	else
	{
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Mesh Default: None");
	}

	UPhysicsAsset* ActiveAsset = Component->GetPhysicsAsset();
	if (ActiveAsset)
	{
		ImGui::Text("Active Asset: %s", ActiveAsset->GetName().ToString().c_str());
	}
	else
	{
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Active Asset: None");
	}

	const bool bHasOverride = Component->HasPhysicsAssetOverride();
	const FString& OverridePath = Component->GetPhysicsAssetOverridePath();
	if (bHasOverride)
	{
		ImGui::Text("Override Path: %s", OverridePath.empty() ? "<unsaved>" : OverridePath.c_str());
	}

	TArray<FString> AvailablePaths = UResourceManager::GetInstance().GetAllFilePaths<UPhysicsAsset>();
	TArray<FString> ComboPaths;
	ComboPaths.push_back("");
	TArray<FString> ComboLabels;
	ComboLabels.push_back("Use Mesh Default");

	for (const FString& Path : AvailablePaths)
	{
		ComboPaths.push_back(Path);
		std::filesystem::path PathView(Path);
		FString Label = PathView.filename().string();
		if (Label.empty())
		{
			Label = Path;
		}
		ComboLabels.push_back(Label);
	}

	int SelectedIndex = 0;
	bool bOverrideMissing = false;
	if (bHasOverride)
	{
		for (int32 Index = 1; Index < ComboPaths.Num(); ++Index)
		{
			if (ComboPaths[Index] == OverridePath)
			{
				SelectedIndex = Index;
				break;
			}
		}
		if (SelectedIndex == 0 && !OverridePath.empty())
		{
			bOverrideMissing = true;
		}
	}

	const char* CurrentLabel = ComboLabels[SelectedIndex].c_str();
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo("##PhysicsAssetOverrideCombo", CurrentLabel))
	{
		for (int32 Index = 0; Index < ComboLabels.Num(); ++Index)
		{
			bool bSelected = (SelectedIndex == Index);
			if (ImGui::Selectable(ComboLabels[Index].c_str(), bSelected))
			{
				if (Index == 0)
				{
					Component->ClearPhysicsAssetOverride();
				}
				else if (Index < ComboPaths.Num())
				{
					Component->SetPhysicsAssetOverrideByPath(ComboPaths[Index]);
				}
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	// Animation Graph part
	ImGui::Spacing();
	ImGui::Separator();

	FString GraphName = Component->GetAnimGraph() ? Component->GetAnimGraph()->GetName() : "None";
	ImGui::Text("애니메이션 그래프: %s", GraphName.c_str());

	if (Component->GetAnimGraph() == nullptr)
	{
		if (ImGui::Button("새로운 애니메이션 그래프 생성"))
		{
			UAnimationGraph* NewGraph = NewObject<UAnimationGraph>();
			Component->SetAnimGraph(NewGraph);
			USlateManager::GetInstance().OpenAnimationGraphEditor(NewGraph);
		}
	}
	else
	{
		if (ImGui::Button("그래프 에디터 열기"))
		{
			if (USlateManager::GetInstance().IsAnimationGraphEditorOpen())
			{
				USlateManager::GetInstance().OpenAnimationGraphEditor(Component->GetAnimGraph());
			}
			else
			{
				USlateManager::GetInstance().OpenAnimationGraphEditor(Component->GetAnimGraph());
			}
		}
	}

	return bChanged;
}

bool UPropertyRenderer::RenderSkeletalMeshProperty(const FProperty& Prop, void* Instance)
{
	USkeletalMesh** MeshPtr = Prop.GetValuePtr<USkeletalMesh*>(Instance);

	FString CurrentPath;
	if (*MeshPtr)
	{
		CurrentPath = (*MeshPtr)->GetFilePath();
	}

	if (CachedSkeletalMeshPaths.empty())
	{
		ImGui::Text("%s: <No Meshes>", Prop.Name);
		return false;
	}

	int SelectedIdx = -1;
	for (int i = 0; i < static_cast<int>(CachedSkeletalMeshPaths.size()); ++i)
	{
		if (CachedSkeletalMeshPaths[i] == CurrentPath)
		{
			SelectedIdx = i;
			break;
		}
	}

	if (ImGui::Button("Skeletal Viewer"))
	{
		if (!USlateManager::GetInstance().IsSkeletalMeshViewerOpen())
		{
			// Open viewer with the currently selected skeletal mesh if available
			if (!CurrentPath.empty())
			{
				USlateManager::GetInstance().OpenSkeletalMeshViewerWithFile(CurrentPath.c_str());
			}
			else
			{
				USlateManager::GetInstance().OpenSkeletalMeshViewer();
			}
		}
		else
		{
			USlateManager::GetInstance().CloseSkeletalMeshViewer();
		}
	}

	ImGui::SetNextItemWidth(240);
	if (ImGui::Combo(Prop.Name, &SelectedIdx, &ItemsGetter, (void*)&CachedSkeletalMeshItems, static_cast<int>(CachedSkeletalMeshItems.size())))
	{
		if (SelectedIdx >= 0 && SelectedIdx < static_cast<int>(CachedSkeletalMeshPaths.size()))
		{
			// 컴포넌트별 Setter 호출
			UObject* Object = static_cast<UObject*>(Instance);
			if (USkeletalMeshComponent* StaticMeshComponent = Cast<USkeletalMeshComponent>(Object))
			{
				StaticMeshComponent->SetSkeletalMesh(CachedSkeletalMeshPaths[SelectedIdx]);
			}
			else
			{
				*MeshPtr = UResourceManager::GetInstance().Load<USkeletalMesh>(CachedSkeletalMeshPaths[SelectedIdx]);
			}
			return true;
		}
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();

		// 1. 원본 경로 표시 (CurrentTexturePath는 null일 때 "None"을 가짐)
		ImGui::TextUnformatted(CurrentPath.c_str());

		//// 2. CurrentTexture가 유효하고 캐시 파일 경로가 있다면 추가로 표시
		//if ((*MeshPtr))
		//{
		//	const FString& CachedPath = (*MeshPtr)->GetCacheFilePath();
		//	if (!CachedPath.empty())
		//	{
		//		ImGui::Separator(); // 원본 경로와 구분하기 위해 선 추가
		//		ImGui::Text("Cache: %s", CachedPath.c_str());
		//	}
		//}

		ImGui::EndTooltip();
	}
	
	ImGui::Spacing();
	ImGui::Separator(); 
	ImGui::Spacing();

	UPhysicsAsset* PhysicsAsset = nullptr;
	if(*MeshPtr)
	{
		PhysicsAsset = (*MeshPtr)->PhysicsAsset;
	}
	
	if (PhysicsAsset)
	{
		// PhysicsAsset 이름 표시
		FString PhysicsAssetName = PhysicsAsset->GetName().ToString();
		ImGui::Text("PhysicsAsset: %s", PhysicsAssetName.c_str());

		// PhysicsAsset 파일 경로 표시
		FString PhysicsAssetPath = PhysicsAsset->GetFilePath();
		ImGui::Text("경로: %s", PhysicsAssetPath.c_str());

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}
	else
	{
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "PhysicsAsset: None");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}

	UObject* Object = static_cast<UObject*>(Instance);
	USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Object);

	if (SkelComp)
	{
		// #1. 현재 할당된 애니메이션 그래프 이름 표시
		FString GraphName = SkelComp->GetAnimGraph() ? SkelComp->GetAnimGraph()->GetName() : "None";
		ImGui::Text("애니메이션 그래프: %s", GraphName.c_str());

		// #2. 그래프가 없으면 [Create], 있으면 [Edit] 버튼 표시
		if (SkelComp->GetAnimGraph() == nullptr)
		{
			if (ImGui::Button("새로운 애니메이션 그래프 생성"))
			{
				UAnimationGraph* NewGraph = NewObject<UAnimationGraph>();
				// NewGraph->SetName("NewAnimGraph_" + std::to_string(NewGraph->GetID())); // 임시 이름
                
				SkelComp->SetAnimGraph(NewGraph);
                
				USlateManager::GetInstance().OpenAnimationGraphEditor(NewGraph);
			}
		}
		else
		{
			if (ImGui::Button("그래프 에디터 열기"))
			{
				if (USlateManager::GetInstance().IsAnimationGraphEditorOpen())
				{
					USlateManager::GetInstance().OpenAnimationGraphEditor(SkelComp->GetAnimGraph());
				}
				else
				{
					USlateManager::GetInstance().OpenAnimationGraphEditor(SkelComp->GetAnimGraph());
				}
			}
		}
	}

	return false;
}

bool UPropertyRenderer::RenderStaticMeshProperty(const FProperty& Prop, void* Instance)
{
	UStaticMesh** MeshPtr = Prop.GetValuePtr<UStaticMesh*>(Instance);

	FString CurrentPath;
	if (*MeshPtr)
	{
		CurrentPath = (*MeshPtr)->GetFilePath();
	}

	if (CachedStaticMeshPaths.empty())
	{
		ImGui::Text("%s: <No Meshes>", Prop.Name);
		return false;
	}

	int SelectedIdx = -1;
	for (int i = 0; i < static_cast<int>(CachedStaticMeshPaths.size()); ++i)
	{
		if (CachedStaticMeshPaths[i] == CurrentPath)
		{
			SelectedIdx = i;
			break;
		}
	}

	// TArray<FString>을 const char* 배열로 변환
	TArray<const char*> ItemsPtr;
	ItemsPtr.reserve(CachedStaticMeshItems.size());
	for (const FString& item : CachedStaticMeshItems)
	{
		ItemsPtr.push_back(item.c_str());
	}

	ImGui::SetNextItemWidth(240);
	if (ImGui::Combo(Prop.Name, &SelectedIdx, ItemsPtr.data(), static_cast<int>(ItemsPtr.size())))
	{
		if (SelectedIdx >= 0 && SelectedIdx < static_cast<int>(CachedStaticMeshPaths.size()))
		{
			// 컴포넌트별 Setter 호출
			UObject* Object = static_cast<UObject*>(Instance);
			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Object))
			{
				StaticMeshComponent->SetStaticMesh(CachedStaticMeshPaths[SelectedIdx]);
			}
			else
			{
				*MeshPtr = UResourceManager::GetInstance().Load<UStaticMesh>(CachedStaticMeshPaths[SelectedIdx]);
			}
			return true;
		}
	}

	// 닫힌 콤보박스 '텍스트' 부분에 마우스를 올렸을 때 전체 경로 툴팁
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();

		// 1. 원본 경로 표시 (CurrentTexturePath는 null일 때 "None"을 가짐)
		ImGui::TextUnformatted(CurrentPath.c_str());

		// 2. CurrentTexture가 유효하고 캐시 파일 경로가 있다면 추가로 표시
		if ((*MeshPtr))
		{
			const FString& CachedPath = (*MeshPtr)->GetCacheFilePath();
			if (!CachedPath.empty())
			{
				ImGui::Separator(); // 원본 경로와 구분하기 위해 선 추가
				ImGui::Text("Cache: %s", CachedPath.c_str());
			}
		}

		ImGui::EndTooltip();
	}

	return false;
}

bool UPropertyRenderer::RenderMaterialProperty(const FProperty& Prop, void* Instance)
{
	UMaterialInterface** MaterialPtr = Prop.GetValuePtr<UMaterialInterface*>(Instance);
	if (!MaterialPtr)
	{
		ImGui::Text("%s: [Invalid Material Ptr]", Prop.Name);
		return false;
	}

	UObject* OwningObject = static_cast<UObject*>(Instance);

	// Material 렌더 함수 호출
	return RenderSingleMaterialSlot(Prop.Name, MaterialPtr, OwningObject, 0);	// 단일 슬롯은 0번 인덱스
}

bool UPropertyRenderer::RenderMaterialArrayProperty(const FProperty& Prop, void* Instance)
{
	TArray<UMaterialInterface*>* MaterialSlots = Prop.GetValuePtr<TArray<UMaterialInterface*>>(Instance);
	if (!MaterialSlots)
	{
		ImGui::Text("%s: [Invalid Array Ptr]", Prop.Name);
		return false;
	}

	UObject* OwningObject = static_cast<UObject*>(Instance);
	bool bArrayChanged = false;

	// 배열의 각 요소를 순회하며 헬퍼 함수 호출 (매개변수 없음)
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlots->Num(); ++MaterialIndex)
	{
		FString Label = FString(Prop.Name) + " [" + std::to_string(MaterialIndex) + "]";
		UMaterialInterface** MaterialPtr = &(*MaterialSlots)[MaterialIndex];

		if (RenderSingleMaterialSlot(Label.c_str(), MaterialPtr, OwningObject, MaterialIndex))
		{
			bArrayChanged = true;
		}

		// 마지막 머티리얼은 제외하고 영역 분리
		if (MaterialIndex < MaterialSlots->Num() - 1)
		{
			ImGui::NewLine();
			ImGui::Separator();
			ImGui::NewLine();
		}
	}

	return bArrayChanged;
}

bool UPropertyRenderer::RenderSingleMaterialSlot(const char* Label, UMaterialInterface** MaterialPtr, UObject* OwningObject, uint32 MaterialIndex)
{
	bool bElementChanged = false;
	UMaterialInterface* CurrentMaterial = *MaterialPtr;

	// 캐시가 비어있으면 아무것도 렌더링하지 않음 (필수)
	if (CachedMaterialItems.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No materials available in cache.");
		return false;
	}

	// --- 1. 머티리얼 에셋 선택 콤보박스 ---
	FString CurrentMaterialPath = (CurrentMaterial) ? CurrentMaterial->GetFilePath() : "None";

	ImGui::SetNextItemWidth(240);
	if (ImGui::BeginCombo(Label, CurrentMaterialPath.c_str()))
	{
		// "None" 옵션
		bool bIsNoneSelected = (CurrentMaterial == nullptr);
		if (ImGui::Selectable(CachedMaterialItems[0], bIsNoneSelected))
		{
			bElementChanged = true;
			if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(OwningObject))
			{
				PrimitiveComponent->SetMaterial(MaterialIndex, nullptr);
			}
			else
			{
				*MaterialPtr = nullptr;
			}
		}
		if (bIsNoneSelected) ImGui::SetItemDefaultFocus();

		// 캐시된 머티리얼 목록
		for (int j = 0; j < (int)CachedMaterialPaths.size(); ++j)
		{
			const FString& Path = CachedMaterialPaths[j];
			const char* DisplayName = CachedMaterialItems[j + 1];
			bool bIsSelected = (CurrentMaterialPath == Path);

			if (ImGui::Selectable(DisplayName, bIsSelected))
			{
				bElementChanged = true;
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(OwningObject))
				{
					PrimitiveComponent->SetMaterialByName(MaterialIndex, Path);
				}
				else
				{
					*MaterialPtr = UResourceManager::GetInstance().Load<UMaterial>(Path);
				}
			}
			if (bIsSelected) ImGui::SetItemDefaultFocus();
		}

		ImGui::EndCombo();
		CurrentMaterial = *MaterialPtr; // 콤보박스에서 변경되었을 수 있으므로 업데이트
	}

	// --- 2. UMaterialInterface 내부 프로퍼티 렌더링 ---
	if (CurrentMaterial)
	{
		ImGui::Indent();

		// --- 2-1. 셰이더 (읽기 전용) ---
		UShader* CurrentShader = CurrentMaterial->GetShader();
		FString CurrentShaderPath = (CurrentShader) ? CurrentShader->GetFilePath() : "None";
		char ShaderPathBuffer[512];
		strncpy_s(ShaderPathBuffer, sizeof(ShaderPathBuffer), CurrentShaderPath.c_str(), _TRUNCATE);
		FString ShaderLabel = "Shader##" + FString(Label);
		ImGui::SetNextItemWidth(300);
		ImGui::BeginDisabled(true);
		ImGui::InputText(ShaderLabel.c_str(), ShaderPathBuffer, sizeof(ShaderPathBuffer), ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();

		// --- 셰이더 매크로 (읽기 전용) ---
		FString MacroKey = UShader::GenerateMacrosToString(CurrentMaterial->GetShaderMacros());
		char ShaderMacroBuffer[512];
		strncpy_s(ShaderMacroBuffer, sizeof(ShaderMacroBuffer), MacroKey.c_str(), _TRUNCATE);
		FString ShaderMacroKeyLabel = "MacroKey##" + FString(Label);
		ImGui::SetNextItemWidth(300);
		ImGui::BeginDisabled(true);
		ImGui::InputText(ShaderMacroKeyLabel.c_str(), ShaderMacroBuffer, sizeof(ShaderMacroBuffer), ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();

		// --- 2-2. 텍스처 슬롯 ---
		// ImGui 위젯 값이 변경될 때만 호출됩니다.
		UMeshComponent* MeshComponent = Cast<UMeshComponent>(OwningObject);

		if (!MeshComponent)
		{
			ImGui::Text("UMeshComponent만 텍스처를 변경할 수 있습니다");
			ImGui::Unindent();
			return false;
		}

		for (uint8 TexSlotIndex = 0; TexSlotIndex < (uint8)EMaterialTextureSlot::Max; ++TexSlotIndex)
		{
			EMaterialTextureSlot Slot = static_cast<EMaterialTextureSlot>(TexSlotIndex);
			UTexture* CurrentTexture = CurrentMaterial->GetTexture(Slot);

			const char* SlotName = "Unknown Slot";
			switch (Slot)
			{
			case EMaterialTextureSlot::Diffuse: SlotName = "Diffuse Texture"; break;
			case EMaterialTextureSlot::Normal: SlotName = "Normal Texture"; break;
			}
			FString TextureLabel = FString(SlotName) + "##" + Label;

			UTexture* NewTexture = nullptr;
			if (RenderTextureSelectionCombo(TextureLabel.c_str(), CurrentTexture, NewTexture))
			{
				MeshComponent->SetMaterialTextureByUser(MaterialIndex, Slot, NewTexture);
				bElementChanged = true;
			}
		}
		

		// --- 2-3. FMaterialInfo 파라미터 (스칼라 및 벡터) ---

		// ImGui에 표시하기 위해 현재 머티리얼의 최종 Info 값을 가져옵니다.
		// (MID인 경우 덮어쓰기가 적용된 복사본, UMaterial인 경우 원본)
		const FMaterialInfo& Info = CurrentMaterial->GetMaterialInfo();

		ImGui::Separator();

		// --- Colors (FVector -> ImGui::ColorEdit3) ---
		FLinearColor TempColor; // ImGui 위젯에 바인딩할 임시 변수

		// DiffuseColor
		TempColor = FLinearColor(Info.DiffuseColor); // FVector에서 FLinearColor로 변환 (생성자 가정)
		FString DiffuseLabel = "Diffuse Color##" + FString(Label);
		if (ImGui::ColorEdit3(DiffuseLabel.c_str(), &TempColor.R))
		{
			MeshComponent->SetMaterialColorByUser(MaterialIndex, "DiffuseColor", TempColor);
			bElementChanged = true;
		}

		// AmbientColor
		TempColor = FLinearColor(Info.AmbientColor);
		FString AmbientLabel = "Ambient Color##" + FString(Label);
		if (ImGui::ColorEdit3(AmbientLabel.c_str(), &TempColor.R))
		{
			MeshComponent->SetMaterialColorByUser(MaterialIndex, "AmbientColor", TempColor);
			bElementChanged = true;
		}

		// SpecularColor
		TempColor = FLinearColor(Info.SpecularColor);
		FString SpecularLabel = "Specular Color##" + FString(Label);
		if (ImGui::ColorEdit3(SpecularLabel.c_str(), &TempColor.R))
		{
			MeshComponent->SetMaterialColorByUser(MaterialIndex, "SpecularColor", TempColor);
			bElementChanged = true;
		}

		// EmissiveColor
		TempColor = FLinearColor(Info.EmissiveColor);
		FString EmissiveLabel = "Emissive Color##" + FString(Label);
		if (ImGui::ColorEdit3(EmissiveLabel.c_str(), &TempColor.R))
		{
			MeshComponent->SetMaterialColorByUser(MaterialIndex, "EmissiveColor", TempColor);
			bElementChanged = true;
		}

		// TransmissionFilter
		TempColor = FLinearColor(Info.TransmissionFilter);
		FString TransmissionLabel = "Transmission Filter##" + FString(Label);
		if (ImGui::ColorEdit3(TransmissionLabel.c_str(), &TempColor.R))
		{
			MeshComponent->SetMaterialColorByUser(MaterialIndex, "TransmissionFilter", TempColor);
			bElementChanged = true;
		}

		// --- Floats (float -> ImGui::DragFloat) ---
		float TempFloat; // ImGui 위젯에 바인딩할 임시 변수

		// SpecularExponent (Shininess)
		TempFloat = Info.SpecularExponent;
		FString SpecExpLabel = "Specular Exponent##" + FString(Label);
		if (ImGui::DragFloat(SpecExpLabel.c_str(), &TempFloat, 1.0f, 0.0f, 1024.0f))
		{
			MeshComponent->SetMaterialScalarByUser(MaterialIndex, "SpecularExponent", TempFloat);
			bElementChanged = true;
		}

		// Transparency (d or Tr)
		TempFloat = Info.Transparency;
		FString TransparencyLabel = "Transparency##" + FString(Label);
		if (ImGui::DragFloat(TransparencyLabel.c_str(), &TempFloat, 0.01f, 0.0f, 1.0f))
		{
			MeshComponent->SetMaterialScalarByUser(MaterialIndex, "Transparency", TempFloat);
			bElementChanged = true;
		}

		// OpticalDensity (Ni)
		TempFloat = Info.OpticalDensity;
		FString OpticalDensityLabel = "Optical Density##" + FString(Label);
		if (ImGui::DragFloat(OpticalDensityLabel.c_str(), &TempFloat, 0.01f, 0.0f, 10.0f)) // 범위는 임의로 지정
		{
			MeshComponent->SetMaterialScalarByUser(MaterialIndex, "OpticalDensity", TempFloat);
			bElementChanged = true;
		}

		// BumpMultiplier (bm)
		TempFloat = Info.BumpMultiplier;
		FString BumpMultiplierLabel = "Bump Multiplier##" + FString(Label);
		if (ImGui::DragFloat(BumpMultiplierLabel.c_str(), &TempFloat, 0.01f, 0.0f, 5.0f)) // 범위는 임의로 지정
		{
			MeshComponent->SetMaterialScalarByUser(MaterialIndex, "BumpMultiplier", TempFloat);
			bElementChanged = true;
		}

		// --- Ints (IlluminationModel) ---
		int TempInt = Info.IlluminationModel;
		FString IllumModelLabel = "Illum Model##" + FString(Label);
		if (ImGui::DragInt(IllumModelLabel.c_str(), &TempInt, 1, 0, 10)) // 0-10은 OBJ 표준 범위
		{
			MeshComponent->SetMaterialScalarByUser(MaterialIndex, "IlluminationModel", (float)TempInt);
			bElementChanged = true;
		}

		ImGui::Unindent();
	}

	return bElementChanged;
}

// ===== 텍스처 선택 헬퍼 함수 =====
bool UPropertyRenderer::RenderTextureSelectionCombo(const char* Label, UTexture* CurrentTexture, UTexture*& OutNewTexture)
{
	// 텍스처 캐시가 비어있으면 렌더링하지 않음
	if (CachedTextureItems.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No textures available in cache.");
		return false;
	}

	bool bChanged = false;
	FString CurrentTexturePath = (CurrentTexture) ? CurrentTexture->GetFilePath() : "None";

	// 1. 현재 선택된 텍스처의 인덱스 찾기
	int SelectedTextureIdx = 0; // "None"
	for (int j = 0; j < (int)CachedTexturePaths.size(); ++j)
	{
		if (CachedTexturePaths[j] == CurrentTexturePath)
		{
			SelectedTextureIdx = j + 1; // 0번은 "None"이므로 +1
			break;
		}
	}

	// 2. 콤보박스 왼쪽에 썸네일 표시
	const float ThumbnailSize = 24.0f;
	const ImVec2 ThumbnailImVec(ThumbnailSize, ThumbnailSize);

	if (CurrentTexture && CurrentTexture->GetShaderResourceView())
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(CurrentTexture->GetShaderResourceView()), ThumbnailImVec);

		// 썸네일 이미지에 마우스를 올렸을 때 이미지/크기 툴팁
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(CurrentTexture->GetFilePath().c_str());
			ImGui::Text("Size: %u x %u", CurrentTexture->GetWidth(), CurrentTexture->GetHeight());

			// 썸네일 크기 계산 (가로/세로 비율 유지, 최대 128x128)
			const float MAX_THUMBNAIL_SIZE = 128.0f;
			float width = (float)CurrentTexture->GetWidth();
			float height = (float)CurrentTexture->GetHeight();
			float ratio = width / height;

			ImVec2 thumbSize;
			if (ratio > 1.0f) // 가로가 더 긴 이미지
			{
				thumbSize = { MAX_THUMBNAIL_SIZE, MAX_THUMBNAIL_SIZE / ratio };
			}
			else // 세로가 더 길거나 같은 이미지
			{
				thumbSize = { MAX_THUMBNAIL_SIZE * ratio, MAX_THUMBNAIL_SIZE };
			}

			ImGui::Image(reinterpret_cast<ImTextureID>(CurrentTexture->GetShaderResourceView()), thumbSize);
			ImGui::EndTooltip();
		}
	}
	else
	{
		// 리소스가 없을 때도 레이아웃 유지를 위해 Dummy 사용
		ImGui::Dummy(ThumbnailImVec);
	}

	ImGui::SameLine();

	// 3. 커스텀 콤보박스 시작
	// 콤보박스에 표시될 텍스트 (현재 선택된 항목의 텍스트)
	const char* PreviewText = CachedTextureItems[SelectedTextureIdx];

	// SetNextItemWidth를 썸네일 크기만큼 보정
	ImGui::SetNextItemWidth(220.0f - ThumbnailSize - ImGui::GetStyle().ItemSpacing.x);

	if (ImGui::BeginCombo(Label, PreviewText))
	{
		// --- 콤보박스 드롭다운 리스트 렌더링 ---

		// 드롭다운 리스트 내부의 아이템 간 수직 간격 설정
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 1.0f));

		// 텍스처 리스트 (미리보기 포함) - "None" 옵션(i=0)을 루프에 포함
		for (int i = 0; i < (int)CachedTextureItems.size(); ++i)
		{
			bool is_selected = (SelectedTextureIdx == i);
			const char* ItemText = CachedTextureItems[i];

			UTexture* previewTexture = nullptr;
			FString TexturePath = "None";

			if (i > 0)
			{
				TexturePath = CachedTexturePaths[i - 1];
				previewTexture = UResourceManager::GetInstance().Load<UTexture>(TexturePath);
			}

			FString SelectableID = FString("##Selectable_") + TexturePath + std::to_string(i) + Label; // Label을 추가하여 ID 고유성 보장

			// 1. Selectable을 먼저 호출
			if (ImGui::Selectable(SelectableID.c_str(), is_selected, 0, ImVec2(0, ThumbnailSize - 1.0f)))
			{
				// 선택된 텍스처 리소스를 경로로부터 로드합니다.
				UTexture* SelectedTexture = (i == 0)
					? nullptr
					: UResourceManager::GetInstance().Load<UTexture>(TexturePath);

				// OutNewTexture에 선택된 텍스처를 할당합니다.
				OutNewTexture = SelectedTexture;
				bChanged = true;
			}
			ImVec2 NextItemCursorPos = ImGui::GetCursorPos(); // 다음 아이템 위치 저장

			// 2. 툴팁 표시
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				if (previewTexture && previewTexture->GetTexture2D() && previewTexture->GetShaderResourceView())
				{
					ImGui::TextUnformatted(previewTexture->GetFilePath().c_str());
					ImGui::Text("Size: %u x %u", previewTexture->GetWidth(), previewTexture->GetHeight());
					float width = (float)previewTexture->GetWidth();
					float height = (float)previewTexture->GetHeight();
					float ratio = width / height;
					ImVec2 thumbSize = (ratio > 1.0f) ? ImVec2(128.0f, 128.0f / ratio) : ImVec2(128.0f * ratio, 128.0f);
					ImGui::Image(reinterpret_cast<ImTextureID>(previewTexture->GetShaderResourceView()), thumbSize);
				}
				else
				{
					ImGui::TextUnformatted("텍스처 없음");
				}
				ImGui::EndTooltip();
			}

			// 3. Selectable 위에 겹쳐 그리기 위해 커서를 되돌림
			ImVec2 ItemPos = ImGui::GetItemRectMin();

			// 4. 내용물(썸네일) 그리기
			ImVec2 ImagePos = ImVec2(ItemPos.x, ItemPos.y);
			ImGui::SetCursorScreenPos(ImagePos);


			if (previewTexture && previewTexture->GetShaderResourceView())
			{
				ImGui::Image(reinterpret_cast<ImTextureID>(previewTexture->GetShaderResourceView()), ThumbnailImVec);
			}
			else
			{
				ImGui::Dummy(ThumbnailImVec);
			}

			// 5. 내용물(텍스트) 그리기
			ImVec2 TextPos = ImVec2(
				ItemPos.x + ThumbnailSize + ImGui::GetStyle().ItemSpacing.x,
				ItemPos.y
			);
			ImGui::SetCursorScreenPos(TextPos);
			ImGui::TextUnformatted(ItemText);

			if (is_selected)
				ImGui::SetItemDefaultFocus();

			// 6. 커서 위치 복원
			ImGui::SetCursorPos(NextItemCursorPos);

			// --- 어설션 해결을 위해 Dummy 위젯 추가 ---
			ImGui::Dummy(ImVec2(0.0f, 0.0f));
		}

		// PushStyleVar로 변경했던 스타일을 원래대로 복원합니다.
		ImGui::PopStyleVar();

		ImGui::EndCombo();
	}

	// 닫힌 콤보박스 '텍스트' 부분에 마우스를 올렸을 때 전체 경로 툴팁
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();

		// 1. 원본 경로 표시 (CurrentTexturePath는 null일 때 "None"을 가짐)
		ImGui::TextUnformatted(CurrentTexturePath.c_str());

		// 2. CurrentTexture가 유효하고 캐시 파일 경로가 있다면 추가로 표시
		if (CurrentTexture)
		{
			const FString& CachedPath = CurrentTexture->GetCacheFilePath();
			if (!CachedPath.empty())
			{
				ImGui::Separator(); // 원본 경로와 구분하기 위해 선 추가
				ImGui::Text("Cache: %s", CachedPath.c_str());
			}
		}

		ImGui::EndTooltip();
	}

	return bChanged;
}
 



bool UPropertyRenderer::RenderSoundSelectionComboSimple(const char* Label, USound* CurrentSound, USound*& OutNewSound)
{
    if (CachedSoundItems.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No Sound available in cache.");
        return false;
    }

    bool bChanged = false;
    FString CurrentSoundPath = (CurrentSound) ? CurrentSound->GetFilePath() : "None";

    int SelectedSoundIdx = 0; // 0 = None
    for (int j = 0; j < (int)CachedSoundPaths.size(); ++j)
    {
        if (CachedSoundPaths[j] == CurrentSoundPath)
        {
            SelectedSoundIdx = j + 1; // +1 for "None"
            break;
        }
    }

    const char* PreviewText = CachedSoundItems[SelectedSoundIdx];
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo(Label, PreviewText))
    {
        for (int i = 0; i < (int)CachedSoundItems.size(); ++i)
        {
            bool is_selected = (SelectedSoundIdx == i);
            const char* ItemText = CachedSoundItems[i];

            if (ImGui::Selectable(ItemText, is_selected))
            {
                USound* SelectedSound = (i == 0)
                    ? nullptr
                    : UResourceManager::GetInstance().Load<USound>(CachedSoundPaths[i - 1]);
                OutNewSound = SelectedSound;
                bChanged = true;
            }

            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    return bChanged;
}


bool UPropertyRenderer::RenderTransformProperty(const FProperty& Prop, void* Instance)
{
	FVector* Value = Prop.GetValuePtr<FVector>(Instance);
	bool bAnyChanged = false;

	// 축 색상 정의
	ImVec4 ColorX(0.85f, 0.2f, 0.2f, 1.0f);   // 부드러운 빨간색
	ImVec4 ColorY(0.3f, 0.8f, 0.3f, 1.0f);    // 부드러운 초록색
	ImVec4 ColorZ(0.2f, 0.5f, 1.0f, 1.0f);    // 밝은 파란색

	float Speed = 0.1f;

	// 라벨 너비 설정 (Location, Rotation, Scale 모두 동일한 너비로 정렬)
	const float LabelWidth = 150.0f;
	const float InputWidth = 70.0f;
	const float ResetButtonWidth = 50.0f;
	const float FrameHeight = ImGui::GetFrameHeight(); // 입력 필드 높이
	const float ColorBoxWidth = FrameHeight * 0.25f;
	const float ColorBoxHeight = FrameHeight;

	// 프로퍼티별 고유 ID 스코프 시작
	ImGui::PushID(Prop.Name);

	// 라벨 출력
	ImGui::Text("%s", Prop.Name);
	ImGui::SameLine(LabelWidth);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	// X축 (Red) - 색상 박스 + 입력 필드
	ImGui::PushID("X");

	// 색상 박스
	ImVec2 CursorPos = ImGui::GetCursorScreenPos();
	DrawList->AddRectFilled(
		CursorPos,
		ImVec2(CursorPos.x + ColorBoxWidth, CursorPos.y + ColorBoxHeight),
		ImGui::ColorConvertFloat4ToU32(ColorX)
	);
	ImGui::Dummy(ImVec2(ColorBoxWidth, ColorBoxHeight));
	ImGui::SameLine(0.0f, 0.0f); // 간격 없이 붙임

	// 입력 필드
	ImGui::PushItemWidth(InputWidth);
	if (ImGui::DragFloat("##X", &Value->X, Speed))
	{
		bAnyChanged = true;
	}
	ImGui::PopItemWidth();
	ImGui::PopID();
	ImGui::SameLine();

	// Y축 (Green) - 색상 박스 + 입력 필드
	ImGui::PushID("Y");

	// 색상 박스
	CursorPos = ImGui::GetCursorScreenPos();
	DrawList->AddRectFilled(
		CursorPos,
		ImVec2(CursorPos.x + ColorBoxWidth, CursorPos.y + ColorBoxHeight),
		ImGui::ColorConvertFloat4ToU32(ColorY)
	);
	ImGui::Dummy(ImVec2(ColorBoxWidth, ColorBoxHeight));
	ImGui::SameLine(0.0f, 0.0f); // 간격 없이 붙임

	// 입력 필드
	ImGui::PushItemWidth(InputWidth);
	if (ImGui::DragFloat("##Y", &Value->Y, Speed))
	{
		bAnyChanged = true;
	}
	ImGui::PopItemWidth();
	ImGui::PopID();
	ImGui::SameLine();

	// Z축 (Blue) - 색상 박스 + 입력 필드
	ImGui::PushID("Z");

	// 색상 박스
	CursorPos = ImGui::GetCursorScreenPos();
	DrawList->AddRectFilled(
		CursorPos,
		ImVec2(CursorPos.x + ColorBoxWidth, CursorPos.y + ColorBoxHeight),
		ImGui::ColorConvertFloat4ToU32(ColorZ)
	);
	ImGui::Dummy(ImVec2(ColorBoxWidth, ColorBoxHeight));
	ImGui::SameLine(0.0f, 0.0f); // 간격 없이 붙임

	// 입력 필드
	ImGui::PushItemWidth(InputWidth);
	if (ImGui::DragFloat("##Z", &Value->Z, Speed))
	{
		bAnyChanged = true;
	}
	ImGui::PopItemWidth();
	ImGui::PopID();
	ImGui::SameLine();

	// Reset 버튼
	ImGui::PushID("Reset");
	if (ImGui::Button("Reset", ImVec2(ResetButtonWidth, 0)))
	{
		// 프로퍼티에 따라 기본값 설정
		if (strcmp(Prop.Name, "RelativeLocation") == 0)
		{
			*Value = FVector(0.0f, 0.0f, 0.0f);
		}
		else if (strcmp(Prop.Name, "RelativeRotationEuler") == 0)
		{
			*Value = FVector(0.0f, 0.0f, 0.0f);
		}
		else if (strcmp(Prop.Name, "RelativeScale") == 0)
		{
			*Value = FVector(1.0f, 1.0f, 1.0f);
		}
		bAnyChanged = true;
	}
	ImGui::PopID();

	// 프로퍼티별 고유 ID 스코프 종료
	ImGui::PopID();

	return bAnyChanged;
}

bool UPropertyRenderer::RenderParticleSystemProperty(const FProperty& Prop, void* Instance)
{
    // Get the particle system pointer from the property
    UParticleSystem** PSPtr = Prop.GetValuePtr<UParticleSystem*>(Instance);
    if (!PSPtr)
       return false;

    UParticleSystem* CurrentPS = *PSPtr;
    FString CurrentName = CurrentPS ? CurrentPS->GetFilePath() : "None";
    const char* PreviewValue = CurrentName.c_str();

    ImGui::Text("%s:", Prop.Name);
    ImGui::SameLine();
	
    // Particle Viewer 버튼
    if (ImGui::Button("Particle Viewer"))
    {
       if (!USlateManager::GetInstance().IsParticleViewerOpen())
       {
          if (CurrentPS)
             USlateManager::GetInstance().OpenParticleViewerWithSystem(CurrentPS, CurrentPS->GetFilePath());
          else
             USlateManager::GetInstance().OpenParticleViewer();
       }
       else
       {
          USlateManager::GetInstance().CloseParticleViewer();
       }
    }

    // 리소스 가져오기
    TArray<UParticleSystem*> ParticleSystems = RESOURCE.GetAll<UParticleSystem>();
    TArray<FString> ParticleSystemNames = RESOURCE.GetAllFilePaths<UParticleSystem>();
    
    bool bValueChanged = false;

    ImGui::PushID(Prop.Name);
    if (ImGui::BeginCombo("##PSSelect", PreviewValue))
    {
        // 1. 'None' 옵션 추가 (선택 해제 기능)
        bool bIsNoneSelected = (CurrentPS == nullptr);
        if (ImGui::Selectable("None", bIsNoneSelected))
        {
            *PSPtr = nullptr;
            bValueChanged = true;
        }
        if (bIsNoneSelected)
            ImGui::SetItemDefaultFocus();

        for (int i = 0; i < ParticleSystems.Num(); ++i)
        {
            UParticleSystem* Item = ParticleSystems[i];
            
            bool bIsSelected = (CurrentPS == Item);

            if (ImGui::Selectable(Item->GetFilePath().c_str(), bIsSelected))
            {
                *PSPtr = Item;
                bValueChanged = true;
            }

            if (bIsSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    ImGui::PopID();

	if (bValueChanged)
	{
		UParticleSystemComponent* PSC = static_cast<UParticleSystemComponent*>(Instance);
		PSC->SetTemplate(*PSPtr);
	}

    return bValueChanged;
}

// ===== Physics Material Preset 선택 콤보박스 =====
bool UPropertyRenderer::RenderPhysMaterialPresetProperty(const FProperty& Prop, void* Instance)
{
	int32* Value = Prop.GetValuePtr<int32>(Instance);
	if (!Value)
		return false;

	bool bChanged = false;

	// 프리셋 이름 목록
	static const char* PresetNames[] = {
		"Default",      // 0
		"Mud",          // 1 - 진흙
		"Wood",         // 2 - 나무
		"Rubber",       // 3 - 고무공
		"Billiard",     // 4 - 당구공
		"Ice",          // 5 - 얼음
		"Metal"         // 6 - 금속
	};
	static const int NumPresets = sizeof(PresetNames) / sizeof(PresetNames[0]);

	// 현재 선택된 프리셋이 범위 내인지 확인
	int CurrentIndex = *Value;
	if (CurrentIndex < 0 || CurrentIndex >= NumPresets)
		CurrentIndex = 0;

	const char* PreviewText = PresetNames[CurrentIndex];

	FString Label = FString(Prop.Name) + "##PhysMaterialPreset";
	ImGui::SetNextItemWidth(150.0f);
	if (ImGui::BeginCombo(Label.c_str(), PreviewText))
	{
		for (int i = 0; i < NumPresets; ++i)
		{
			bool bIsSelected = (CurrentIndex == i);
			if (ImGui::Selectable(PresetNames[i], bIsSelected))
			{
				*Value = i;
				bChanged = true;
			}
			if (bIsSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// 드롭다운 아래에 선택된 프리셋의 물리 속성 표시
	float StaticFriction = 0.5f, DynamicFriction = 0.4f, Restitution = 0.0f;
	switch (CurrentIndex)
	{
	case 1: StaticFriction = 1.5f; DynamicFriction = 1.2f; Restitution = 0.0f; break;  // Mud
	case 2: StaticFriction = 0.4f; DynamicFriction = 0.3f; Restitution = 0.4f; break;  // Wood
	case 3: StaticFriction = 1.0f; DynamicFriction = 0.8f; Restitution = 0.8f; break;  // Rubber
	case 4: StaticFriction = 0.2f; DynamicFriction = 0.15f; Restitution = 0.95f; break; // Billiard
	case 5: StaticFriction = 0.05f; DynamicFriction = 0.03f; Restitution = 0.1f; break; // Ice
	case 6: StaticFriction = 0.6f; DynamicFriction = 0.4f; Restitution = 0.3f; break;  // Metal
	default: break;
	}

	ImGui::Indent(10.0f);
	ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Static: %.2f  Dynamic: %.2f  Restitution: %.2f",
		StaticFriction, DynamicFriction, Restitution);
	ImGui::Unindent(10.0f);

	return bChanged;
}

// ===== Combine Mode 선택 콤보박스 =====
bool UPropertyRenderer::RenderCombineModeProperty(const FProperty& Prop, void* Instance)
{
	// ECombineMode는 uint8 기반이므로 직접 캐스팅
	uint8* ValuePtr = Prop.GetValuePtr<uint8>(Instance);
	if (!ValuePtr)
		return false;

	bool bChanged = false;

	// 합성 모드 이름 목록
	static const char* ModeNames[] = {
		"Average",   // 0
		"Min",       // 1
		"Multiply",  // 2
		"Max"        // 3
	};
	static const int NumModes = sizeof(ModeNames) / sizeof(ModeNames[0]);

	int CurrentIndex = static_cast<int>(*ValuePtr);
	if (CurrentIndex < 0 || CurrentIndex >= NumModes)
		CurrentIndex = 2; // Default to Multiply

	const char* PreviewText = ModeNames[CurrentIndex];

	FString Label = FString(Prop.Name) + "##CombineMode";
	ImGui::SetNextItemWidth(150.0f);
	if (ImGui::BeginCombo(Label.c_str(), PreviewText))
	{
		for (int i = 0; i < NumModes; ++i)
		{
			bool bIsSelected = (CurrentIndex == i);
			if (ImGui::Selectable(ModeNames[i], bIsSelected))
			{
				*ValuePtr = static_cast<uint8>(i);
				bChanged = true;
			}
			if (bIsSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// 툴팁으로 합성 모드 설명
	if (ImGui::IsItemHovered())
	{
		const char* Description = "";
		switch (CurrentIndex)
		{
		case 0: Description = "Average: (A + B) / 2"; break;
		case 1: Description = "Min: min(A, B)"; break;
		case 2: Description = "Multiply: A * B (Default)"; break;
		case 3: Description = "Max: max(A, B)"; break;
		}
		ImGui::SetTooltip("%s", Description);
	}

	return bChanged;
}