#include "pch.h"

FString UObject::GetName()
{
    return ObjectName.ToString();
}

FString UObject::GetComparisonName()
{
    return FString();
}

// 리플렉션 기반 자동 직렬화 (현재 클래스의 프로퍼티만 처리)
void UObject::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	const TArray<FProperty>& Properties = this->GetClass()->GetAllProperties();

	for (const FProperty& Prop : Properties)
	{
		switch (Prop.Type)
		{
		case EPropertyType::Bool:
		{
			bool* Value = Prop.GetValuePtr<bool>(this);
			if (bInIsLoading)
			{
				bool ReadValue;
				if (FJsonSerializer::ReadBool(InOutHandle, Prop.Name, ReadValue))
				{
					*Value = ReadValue;
				}
			}
			else
			{
				InOutHandle[Prop.Name] = *Value;
			}
			break;
		}
		case EPropertyType::Int32:
		{
			int32* Value = Prop.GetValuePtr<int32>(this);
			if (bInIsLoading)
			{
				int32 ReadValue;
				if (FJsonSerializer::ReadInt32(InOutHandle, Prop.Name, ReadValue))
				{
					*Value = ReadValue;
				}
			}
			else
			{
				InOutHandle[Prop.Name] = *Value;
			}
			break;
		}
		case EPropertyType::Float:
		{
			float* Value = Prop.GetValuePtr<float>(this);
			if (bInIsLoading)
			{
				float ReadValue;
				if (FJsonSerializer::ReadFloat(InOutHandle, Prop.Name, ReadValue))
				{
					*Value = ReadValue;
				}
			}
			else
			{
				InOutHandle[Prop.Name] = *Value;
			}
			break;
		}
		case EPropertyType::FVector:
		{
			FVector* Value = Prop.GetValuePtr<FVector>(this);
			if (bInIsLoading)
			{
				FVector ReadValue;
				if (FJsonSerializer::ReadVector(InOutHandle, Prop.Name, ReadValue))
				{
					*Value = ReadValue;
				}
			}
			else
			{
				InOutHandle[Prop.Name] = FJsonSerializer::VectorToJson(*Value);
			}
			break;
		}
		case EPropertyType::FLinearColor:
		{
			FLinearColor* Value = Prop.GetValuePtr<FLinearColor>(this);
			if (bInIsLoading)
			{
				FVector4 ReadValue;
				if (FJsonSerializer::ReadVector4(InOutHandle, Prop.Name, ReadValue))
				{
					*Value = FLinearColor(ReadValue);
				}
			}
			else
			{
				InOutHandle[Prop.Name] = FJsonSerializer::Vector4ToJson(Value->ToFVector4());
			}
			break;
		}
		case EPropertyType::FString:
		case EPropertyType::ScriptFile:
		{
			FString* Value = Prop.GetValuePtr<FString>(this);
			if (bInIsLoading)
			{
				FString ReadValue;
				if (FJsonSerializer::ReadString(InOutHandle, Prop.Name, ReadValue))
				{
					*Value = ReadValue;
				}
			}
			else
			{
				InOutHandle[Prop.Name] = Value->c_str();
			}
			break;
		}
		case EPropertyType::FName:
		{
			FName* Value = Prop.GetValuePtr<FName>(this);
			if (bInIsLoading)
			{
				FString ReadValue;
				if (FJsonSerializer::ReadString(InOutHandle, Prop.Name, ReadValue))
				{
					*Value = FName(ReadValue);
				}
			}
			else
			{
				InOutHandle[Prop.Name] = Value->ToString().c_str();
			}
			break;
		}
		case EPropertyType::Texture:
		{
			UTexture** Value = Prop.GetValuePtr<UTexture*>(this);
			if (bInIsLoading)
			{
				FString TexturePath;
				FJsonSerializer::ReadString(InOutHandle, Prop.Name, TexturePath);
				if (!TexturePath.empty())
				{
					*Value = UResourceManager::GetInstance().Load<UTexture>(TexturePath);
				}
				else
				{
					*Value = nullptr;
				}
			}
			else
			{
				if (*Value)
				{
					InOutHandle[Prop.Name] = (*Value)->GetFilePath().c_str();
				}
				else
				{
					InOutHandle[Prop.Name] = "";
				}
			}
			break;
		}
		case EPropertyType::StaticMesh:
		{
			UStaticMesh** Value = Prop.GetValuePtr<UStaticMesh*>(this);
			if (bInIsLoading)
			{
				FString MeshPath;
				FJsonSerializer::ReadString(InOutHandle, Prop.Name, MeshPath);
				if (!MeshPath.empty())
				{
					*Value = UResourceManager::GetInstance().Load<UStaticMesh>(MeshPath);
				}
				else
				{
					*Value = nullptr;
				}
			}
			else
			{
				if (*Value)
				{
					InOutHandle[Prop.Name] = (*Value)->GetAssetPathFileName().c_str();
				}
				else
				{
					InOutHandle[Prop.Name] = "";
				}
			}
			break;
		}
		case EPropertyType::SkeletalMesh:
		{
			USkeletalMesh** Value = Prop.GetValuePtr<USkeletalMesh*>(this);
			if (bInIsLoading)
			{
				FString MeshPath;
				FJsonSerializer::ReadString(InOutHandle, Prop.Name, MeshPath);
				if (!MeshPath.empty())
				{
					*Value = UResourceManager::GetInstance().Load<USkeletalMesh>(MeshPath);
				}
				else
				{
					*Value = nullptr;
				}
			}
			else
			{
				if (*Value)
				{
					InOutHandle[Prop.Name] = (*Value)->GetPathFileName().c_str();
				}
				else
				{
					InOutHandle[Prop.Name] = "";
				}
			}
			break;
		}
		case EPropertyType::ParticleSystem:
		{
			UParticleSystem** Value = Prop.GetValuePtr<UParticleSystem*>(this);
			if (bInIsLoading)
			{
				FString ParticlePath;
				FJsonSerializer::ReadString(InOutHandle, Prop.Name, ParticlePath);
				if (!ParticlePath.empty())
				{
					*Value = RESOURCE.Load<UParticleSystem>(ParticlePath);
				}
				else
				{
					*Value = nullptr;
				}
			}
			else
			{
				if (*Value)
				{
					InOutHandle[Prop.Name] = (*Value)->GetFilePath().c_str();
				}
				else
				{
					InOutHandle[Prop.Name] = "";
				}
			}
			break;
		}
		case EPropertyType::Material:
		{
			UMaterial** Value = Prop.GetValuePtr<UMaterial*>(this);
			if (bInIsLoading)
			{
				FString MaterialPath;
				FJsonSerializer::ReadString(InOutHandle, Prop.Name, MaterialPath);
				if (!MaterialPath.empty())
				{
					*Value = UResourceManager::GetInstance().Load<UMaterial>(MaterialPath);
				}
				else
				{
					*Value = nullptr;
				}
			}
			else
			{
				if (*Value)
				{
					InOutHandle[Prop.Name] = (*Value)->GetFilePath().c_str();
				}
				else
				{
					InOutHandle[Prop.Name] = "";
				}
			}
			break;
		}
		case EPropertyType::PhysicsAsset:
		{
			// PhysicsAsset의 주소 저장(파일 이름)
			UPhysicsAsset** Value = Prop.GetValuePtr<UPhysicsAsset*>(this);
			if (bInIsLoading)
			{
				FString AssetPath;
				FJsonSerializer::ReadString(InOutHandle, Prop.Name, AssetPath);
				if (!AssetPath.empty())
				{
					*Value = UResourceManager::GetInstance().Load<UPhysicsAsset>(AssetPath);
				}
				else
				{
					*Value = nullptr;
				}
			}
			else
			{
				if (*Value)
				{
					InOutHandle[Prop.Name] = (*Value)->GetFilePath().c_str();
				}
				else
				{
					InOutHandle[Prop.Name] = "";
				}
			}
			break;
		}
		case EPropertyType::Curve:
		{
			// Curve 프로퍼티는 float[4] 배열입니다. 따라서 FVector4로 처리
			if (bInIsLoading)
			{
				FVector4 TempVec4;

				// 1. FJsonSerializer::ReadVector4를 사용해 JSON에서 4개 요소를 읽어옵니다.
				if (FJsonSerializer::ReadVector4(InOutHandle, Prop.Name, TempVec4))
				{
					// 2. 프로퍼티(float[4])의 실제 주소를 가져옵니다.
					float* PropData = Prop.GetValuePtr<float>(this);

					// 3. 읽어온 FVector4의 내용을 float[4] 메모리 위치에 복사합니다.
					memcpy(PropData, &TempVec4, sizeof(float) * 4);
				}
			}
			else // Saving
			{
				// 1. 프로퍼티(float[4])의 실제 주소를 가져옵니다.
				float* PropData = Prop.GetValuePtr<float>(this);

				// 2. float[4] 배열의 값으로 임시 FVector4 객체를 생성합니다.
				FVector4 TempVec4(PropData[0], PropData[1], PropData[2], PropData[3]);

				// 3. FJsonSerializer::Vector4ToJson을 사용해 JSON 배열로 변환하고 씁니다.
				InOutHandle[Prop.Name] = FJsonSerializer::Vector4ToJson(TempVec4);
			}
			break;
		}
		case EPropertyType::Array:
		{
			// Array 직렬화는 InnerType에 따라 처리
			if (Prop.InnerType == EPropertyType::Unknown)
			{
				UE_LOG("[AutoSerialize] Array property '%s' has Unknown InnerType, skipping.", Prop.Name);
				break;
			}

			JSON ArrayJson;
			if (bInIsLoading)
			{
				// JSON에서 배열 읽기
				if (!FJsonSerializer::ReadArray(InOutHandle, Prop.Name, ArrayJson))
				{
					break; // 배열이 없거나 유효하지 않음
				}
			}
			else
			{
				// 저장용 빈 배열 생성
				ArrayJson = JSON::Make(JSON::Class::Array);
			}

			// InnerType에 따라 처리
			switch (Prop.InnerType)
			{
			case EPropertyType::Int32:
			{
				TArray<int32>* ArrayPtr = Prop.GetValuePtr<TArray<int32>>(this);
				SerializePrimitiveArray<int32>(ArrayPtr, bInIsLoading, ArrayJson);
				break;
			}
			case EPropertyType::Float:
			{
				TArray<float>* ArrayPtr = Prop.GetValuePtr<TArray<float>>(this);
				SerializePrimitiveArray<float>(ArrayPtr, bInIsLoading, ArrayJson);
				break;
			}
			case EPropertyType::Bool:
			{
				TArray<bool>* ArrayPtr = Prop.GetValuePtr<TArray<bool>>(this);
				SerializePrimitiveArray<bool>(ArrayPtr, bInIsLoading, ArrayJson);
				break;
			}
			case EPropertyType::FString:
			{
				TArray<FString>* ArrayPtr = Prop.GetValuePtr<TArray<FString>>(this);
				SerializePrimitiveArray<FString>(ArrayPtr, bInIsLoading, ArrayJson);
				break;
			}
			
			case EPropertyType::Sound:
			{
				TArray<USound*>* ArrayPtr = Prop.GetValuePtr<TArray<USound*>>(this);
				if (bInIsLoading)
				{
					ArrayPtr->Empty();
					for (size_t i = 0; i < ArrayJson.size(); ++i)
					{
						const JSON& Elem = ArrayJson.at(i);
						if (Elem.JSONType() == JSON::Class::String)
						{
							FString Path = Elem.ToString();
							if (!Path.empty()) ArrayPtr->Add(UResourceManager::GetInstance().Load<USound>(Path));
							else ArrayPtr->Add(nullptr);
						}
					}
				}
				else
				{
					for (USound* Snd : *ArrayPtr)
					{
						ArrayJson.append((Snd) ? Snd->GetFilePath().c_str() : "");
					}
				}

				break;
			}
			default:
			{
				//UE_LOG("[AutoSerialize] Array property '%s' has unsupported InnerType: %d", Prop.Name, static_cast<int>(Prop.InnerType));
				break;
			}
			}

			// 저장 시 JSON에 배열 쓰기
			if (!bInIsLoading)
			{
				InOutHandle[Prop.Name] = ArrayJson;
			}
			break;
		}
		// ObjectPtr, Struct 등은 필요시 추가
		}
	}
}

void UObject::DuplicateSubObjects()
{
    UUID = GenerateUUID(); // UUID는 고유값이므로 새로 생성
}

UObject* UObject::Duplicate() const
{
    UObject* NewObject = ObjectFactory::DuplicateObject<UObject>(this); // 모든 멤버 얕은 복사

    NewObject->DuplicateSubObjects(); // 얕은 복사한 멤버들에 대해 메뉴얼하게 깊은 복사 수행

	NewObject->PostDuplicate();
    return NewObject;
}





