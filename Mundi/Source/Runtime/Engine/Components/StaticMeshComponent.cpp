#include "pch.h"
#include "StaticMeshComponent.h"
#include "StaticMesh.h"
#include "Shader.h"
#include "ResourceManager.h"
#include "ObjManager.h"
#include "JsonSerializer.h"
#include "CameraComponent.h"
#include "MeshBatchElement.h"
#include "Material.h"
#include "SceneView.h"
#include "LuaBindHelpers.h"
#include "Source/Runtime/Engine/Physics/BodyInstance.h"
#include "Source/Runtime/Engine/Physics/PhysScene.h"
#include "World.h"
#include "Source/Runtime/Engine/Physics/BodySetup.h"

// IMPLEMENT_CLASS is now auto-generated in .generated.cpp
UStaticMeshComponent::UStaticMeshComponent()
{
	SetStaticMesh(GDataDir + "/cube-tex.obj");     // 임시 기본 static mesh 설정
}

UStaticMeshComponent::~UStaticMeshComponent()
{
	if (BodySetupOverride)
	{
		ObjectFactory::DeleteObject(BodySetupOverride);
		BodySetupOverride = nullptr;
	}
}

void UStaticMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	InitCollisionShape();
	RecreateBodySetup();

	// 콜라이더가 비활성화되어 있으면 물리 등록 안 함
	if (!bEnableCollision)
	{
		return;
	}

	// Dynamic(bSimulatePhysics)인 경우 틱 활성화 (Transform 동기화 필요)
	if (bSimulatePhysics)
	{
		bCanEverTick = true;
		bTickEnabled = true;
	}

	// 물리 씬에 등록 (기본: Static, bSimulatePhysics=true: Dynamic)
	UWorld* World = GetWorld();
	if (World && World->GetPhysScene())
	{
		InitPhysics(*World->GetPhysScene());
	}
}

void UStaticMeshComponent::EndPlay()
{
	// 물리 바디 정리 (PhysScene이 아직 유효할 때)
	if (BodyInstance && BodyInstance->RigidActor)
	{
		if (UWorld* World = GetWorld())
		{
			if (FPhysScene* PhysScene = World->GetPhysScene())
			{
				BodyInstance->Terminate(*PhysScene);
			}
		}
	}

	Super::EndPlay();
}

void UStaticMeshComponent::OnCollisionShapeChanged()
{
	RecreateBodySetup();

	// 물리 상태가 이미 초기화되었다면 업데이트합니다.
	if (BodyInstance && BodyInstance->RigidActor)
	{
		if (UWorld* World = GetWorld())
		{
			if (FPhysScene* PhysScene = World->GetPhysScene())
			{
				BodyInstance->Terminate(*PhysScene);
				InitPhysics(*PhysScene);
			}
		}
	}
}

void UStaticMeshComponent::InitCollisionShape()
{
	EAggCollisionShapeType CurrentShapeType = static_cast<EAggCollisionShapeType>(CollisionType);
	if (CurrentShapeType != EAggCollisionShapeType::Convex)
	{
		FAABB LocalBound = StaticMesh->GetLocalBound();
		FVector BoundExtent = LocalBound.GetHalfExtent();

		bool bBoxExtentIsDefault = (BoxExtent == FVector(50.f));
		bool bSphereRadiusIsDefault = (SphereRadius == 50.f);
		bool bCapsuleRadiusIsDefault = (CapsuleRadius == 22.f);
		bool bCapsuleHalfHeightIsDefault = (CapsuleHalfHeight == 44.f);

		if (bBoxExtentIsDefault)
		{
			BoxExtent = BoundExtent; // Set to mesh's half-extent
		}
		if (bSphereRadiusIsDefault)
		{
			SphereRadius = BoundExtent.GetMaxValue(); // Max of half-extents
		}
		if (bCapsuleRadiusIsDefault)
		{
			CapsuleRadius = BoundExtent.GetMaxValue() / 2.f; // Heuristic for capsule radius
		}
		if (bCapsuleHalfHeightIsDefault)
		{
			CapsuleHalfHeight = BoundExtent.Z; // Heuristic for capsule half-height
		}
	}

	OnCollisionShapeChanged();
}

void UStaticMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	// Dynamic 물리 오브젝트의 Transform을 PhysX 결과와 동기화
	if (bSimulatePhysics && BodyInstance && BodyInstance->RigidActor)
	{
		FTransform PhysTransform = BodyInstance->GetWorldTransform();
		// PhysX는 스케일을 지원하지 않으므로 기존 스케일 유지
		PhysTransform.Scale3D = GetWorldTransform().Scale3D;
		SetWorldTransform(PhysTransform);
	}
}

void UStaticMeshComponent::OnStaticMeshReleased(UStaticMesh* ReleasedMesh)
{
	// TODO : 왜 this가 없는지 추적 필요!
	if (!this || !StaticMesh || StaticMesh != ReleasedMesh)
	{
		return;
	}

	StaticMesh = nullptr;
}

void UStaticMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
	if (!StaticMesh || !StaticMesh->GetStaticMeshAsset())
	{
		return;
	}

	const TArray<FGroupInfo>& MeshGroupInfos = StaticMesh->GetMeshGroupInfo();

	auto DetermineMaterialAndShader = [&](uint32 SectionIndex) -> TPair<UMaterialInterface*, UShader*>
		{
			UMaterialInterface* Material = GetMaterial(SectionIndex);
			UShader* Shader = nullptr;

			if (Material && Material->GetShader())
			{
				Shader = Material->GetShader();
			}
			else
			{
				UE_LOG("UStaticMeshComponent: 머티리얼이 없거나 셰이더가 없어서 기본 머티리얼 사용 section %u.", SectionIndex);
				Material = UResourceManager::GetInstance().GetDefaultMaterial();
				if (Material)
				{
					Shader = Material->GetShader();
				}
				if (!Material || !Shader)
				{
					UE_LOG("UStaticMeshComponent: 기본 머티리얼이 없습니다.");
					return { nullptr, nullptr };
				}
			}
			return { Material, Shader };
		};

	const bool bHasSections = !MeshGroupInfos.IsEmpty();
	const uint32 NumSectionsToProcess = bHasSections ? static_cast<uint32>(MeshGroupInfos.size()) : 1;

	for (uint32 SectionIndex = 0; SectionIndex < NumSectionsToProcess; ++SectionIndex)
	{
		uint32 IndexCount = 0;
		uint32 StartIndex = 0;

		if (bHasSections)
		{
			const FGroupInfo& Group = MeshGroupInfos[SectionIndex];
			IndexCount = Group.IndexCount;
			StartIndex = Group.StartIndex;
		}
		else
		{
			IndexCount = StaticMesh->GetIndexCount();
			StartIndex = 0;
		}

		if (IndexCount == 0)
		{
			continue;
		}

		auto [MaterialToUse, ShaderToUse] = DetermineMaterialAndShader(SectionIndex);
		if (!MaterialToUse || !ShaderToUse)
		{
			continue;
		}

		FMeshBatchElement BatchElement;
		// View 모드 전용 매크로와 머티리얼 개인 매크로를 결합한다
		TArray<FShaderMacro> ShaderMacros = View->ViewShaderMacros;
		if (0 < MaterialToUse->GetShaderMacros().Num())
		{
			ShaderMacros.Append(MaterialToUse->GetShaderMacros());
		}
		FShaderVariant* ShaderVariant = ShaderToUse->GetOrCompileShaderVariant(ShaderMacros);

		if (ShaderVariant)
		{
			BatchElement.VertexShader = ShaderVariant->VertexShader;
			BatchElement.PixelShader = ShaderVariant->PixelShader;
			BatchElement.InputLayout = ShaderVariant->InputLayout;
		}

		// UMaterialInterface를 UMaterial로 캐스팅해야 할 수 있음. 렌더러가 UMaterial을 기대한다면.
		// 지금은 Material.h 구조상 UMaterialInterface에 필요한 정보가 다 있음.
		BatchElement.Material = MaterialToUse;
		BatchElement.VertexBuffer = StaticMesh->GetVertexBuffer();
		BatchElement.IndexBuffer = StaticMesh->GetIndexBuffer();
		BatchElement.VertexStride = StaticMesh->GetVertexStride();
		BatchElement.IndexCount = IndexCount;
		BatchElement.StartIndex = StartIndex;
		BatchElement.BaseVertexIndex = 0;
		BatchElement.WorldMatrix = GetWorldMatrix();
		BatchElement.ObjectID = InternalIndex;
		BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		OutMeshBatchElements.Add(BatchElement);
	}
}

void UStaticMeshComponent::SetStaticMesh(const FString& PathFileName)
{
	// 새 메시를 설정하기 전에, 기존에 생성된 모든 MID와 슬롯 정보를 정리합니다.
	ClearDynamicMaterials();

	// 새 메시를 로드합니다.
	StaticMesh = UResourceManager::GetInstance().Load<UStaticMesh>(PathFileName);
	if (StaticMesh && StaticMesh->GetStaticMeshAsset())
	{
		const TArray<FGroupInfo>& GroupInfos = StaticMesh->GetMeshGroupInfo();

		// 4. 새 메시 정보에 맞게 슬롯을 재설정합니다.
		MaterialSlots.resize(GroupInfos.size()); // ClearDynamicMaterials()에서 비워졌으므로, 새 크기로 재할당

		for (int i = 0; i < GroupInfos.size(); ++i)
		{
			SetMaterialByName(i, GroupInfos[i].InitialMaterialName);
		}
		MarkWorldPartitionDirty();
	}
	else
	{
		// 메시 로드에 실패한 경우, StaticMesh 포인터를 nullptr로 보장합니다.
		// (슬롯은 이미 위에서 비워졌습니다.)
		StaticMesh = nullptr;
	}
}

FAABB UStaticMeshComponent::GetWorldAABB() const
{
	const FTransform WorldTransform = GetWorldTransform();
	const FMatrix WorldMatrix = GetWorldMatrix();

	if (!StaticMesh)
	{
		const FVector Origin = WorldTransform.TransformPosition(FVector());
		return FAABB(Origin, Origin);
	}

	const FAABB LocalBound = StaticMesh->GetLocalBound();
	const FVector LocalMin = LocalBound.Min;
	const FVector LocalMax = LocalBound.Max;

	const FVector LocalCorners[8] = {
		FVector(LocalMin.X, LocalMin.Y, LocalMin.Z),
		FVector(LocalMax.X, LocalMin.Y, LocalMin.Z),
		FVector(LocalMin.X, LocalMax.Y, LocalMin.Z),
		FVector(LocalMax.X, LocalMax.Y, LocalMin.Z),
		FVector(LocalMin.X, LocalMin.Y, LocalMax.Z),
		FVector(LocalMax.X, LocalMin.Y, LocalMax.Z),
		FVector(LocalMin.X, LocalMax.Y, LocalMax.Z),
		FVector(LocalMax.X, LocalMax.Y, LocalMax.Z)
	};

	FVector4 WorldMin4 = FVector4(LocalCorners[0].X, LocalCorners[0].Y, LocalCorners[0].Z, 1.0f) * WorldMatrix;
	FVector4 WorldMax4 = WorldMin4;

	for (int32 CornerIndex = 1; CornerIndex < 8; ++CornerIndex)
	{
		const FVector4 WorldPos = FVector4(LocalCorners[CornerIndex].X
			, LocalCorners[CornerIndex].Y
			, LocalCorners[CornerIndex].Z
			, 1.0f)
			* WorldMatrix;
		WorldMin4 = WorldMin4.ComponentMin(WorldPos);
		WorldMax4 = WorldMax4.ComponentMax(WorldPos);
	}

	FVector WorldMin = FVector(WorldMin4.X, WorldMin4.Y, WorldMin4.Z);
	FVector WorldMax = FVector(WorldMax4.X, WorldMax4.Y, WorldMax4.Z);
	return FAABB(WorldMin, WorldMax);
}

void UStaticMeshComponent::OnTransformUpdated()
{
	Super::OnTransformUpdated();
	MarkWorldPartitionDirty();
}

void UStaticMeshComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();

	// BodyInstance는 PIE에서 새로 생성해야 하므로 nullptr로 초기화
	BodyInstance = nullptr;
}

void UStaticMeshComponent::InitPhysics(FPhysScene& PhysScene)
{
	if (!BodyInstance)
		BodyInstance = new FBodyInstance();

	BodyInstance->OwnerComponent = this;
	BodyInstance->BodySetup = GetBodySetup(); // 이제 override를 고려함

	// Override 값 설정
	BodyInstance->bUseOverrideValues = true;
	BodyInstance->MassOverride = MassOverride;
	BodyInstance->LinearDampingOverride = LinearDampingOverride;
	BodyInstance->AngularDampingOverride = AngularDampingOverride;

	// PhysMaterialPreset에서 물리 재질 값 가져오기
	GetPhysMaterialValues(
		BodyInstance->StaticFrictionOverride,
		BodyInstance->DynamicFrictionOverride,
		BodyInstance->RestitutionOverride
	);
	BodyInstance->FrictionCombineModeOverride = FrictionCombineModeOverride;
	BodyInstance->RestitutionCombineModeOverride = RestitutionCombineModeOverride;

	FTransform WorldTM = GetWorldTransform();
	FVector Scale3D = WorldTM.Scale3D;  // 월드 스케일 추출

	if (bSimulatePhysics)
	{
		// Dynamic: 물리 시뮬레이션 적용 (중력, 힘 등)
		BodyInstance->InitDynamic(PhysScene, WorldTM, MassOverride, Scale3D);
	}
	else
	{
		// Static: 움직이지 않는 콜라이더 (충돌 감지용)
		BodyInstance->InitStatic(PhysScene, WorldTM, Scale3D);
	}
}

UBodySetup* UStaticMeshComponent::GetBodySetup()
{
	if (BodySetupOverride)
	{
		return BodySetupOverride;
	}

	if (StaticMesh)
	{
		return StaticMesh->BodySetup;
	}
	return nullptr;
}

void UStaticMeshComponent::RecreateBodySetup()
{
	if (BodySetupOverride)
	{
		ObjectFactory::DeleteObject(BodySetupOverride);
		BodySetupOverride = nullptr;
	}
	
	EAggCollisionShapeType CurrentShapeType = static_cast<EAggCollisionShapeType>(CollisionType);

	// 컨벡스 타입은 StaticMesh의 기본 BodySetup을 사용하므로 override가 필요 없음
	if (CurrentShapeType == EAggCollisionShapeType::Convex)
	{
		return;
	}

	if (!StaticMesh)
	{
		return;
	}

	BodySetupOverride = ObjectFactory::NewObject<UBodySetup>();
	BodySetupOverride->BoneName = FName("None");
	
	FAABB LocalBound = StaticMesh->GetLocalBound();
	FVector Center = LocalBound.GetCenter();
	FVector Extent = LocalBound.GetHalfExtent();


	switch (CurrentShapeType)
	{
		case EAggCollisionShapeType::Box:
		{
			FKBoxElem BoxElem;
			BoxElem.Center = CollisionOffset;
			BoxElem.Rotation = CollisionRotation;
			BoxElem.Extents = BoxExtent; // Extent는 반쪽 크기
			BodySetupOverride->AggGeom.BoxElements.Add(BoxElem);
			break;
		}
		case EAggCollisionShapeType::Sphere:
		{
			FKSphereElem SphereElem;
			SphereElem.Center = CollisionOffset;
			SphereElem.Radius = SphereRadius;
			BodySetupOverride->AggGeom.SphereElements.Add(SphereElem);
			break;
		}
		case EAggCollisionShapeType::Capsule:
		{
			FKCapsuleElem CapsuleElem;
			CapsuleElem.Center = CollisionOffset;
			CapsuleElem.Rotation = CollisionRotation;
			CapsuleElem.Radius = CapsuleRadius;
			CapsuleElem.HalfLength = CapsuleHalfHeight;
			BodySetupOverride->AggGeom.CapsuleElements.Add(CapsuleElem);
			break;
		}
		case EAggCollisionShapeType::Convex:
			// Already handled
			break;
	}

	BodySetupOverride->BuildCachedData();
}

void UStaticMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

	if (bInIsLoading)
	{
		// 역직렬화 (로드)
		FJsonSerializer::ReadBool(InOutHandle, "bEnableCollision", bEnableCollision, true, false);
		FJsonSerializer::ReadBool(InOutHandle, "bSimulatePhysics", bSimulatePhysics, false, false);
		
		int32 ShapeType = static_cast<int32>(EAggCollisionShapeType::Box);
		FJsonSerializer::ReadInt32(InOutHandle, "CollisionType", ShapeType, static_cast<int32>(EAggCollisionShapeType::Box), false);
		CollisionType = static_cast<EAggCollisionShapeType>(ShapeType);

		FJsonSerializer::ReadVector(InOutHandle, "BoxExtent", BoxExtent, FVector(50.f), false);
		FJsonSerializer::ReadFloat(InOutHandle, "SphereRadius", SphereRadius, 50.f, false);
		FJsonSerializer::ReadFloat(InOutHandle, "CapsuleRadius", CapsuleRadius, 22.f, false);
		FJsonSerializer::ReadFloat(InOutHandle, "CapsuleHalfHeight", CapsuleHalfHeight, 44.f, false);
		FJsonSerializer::ReadVector(InOutHandle, "CollisionOffset", CollisionOffset, FVector::Zero(), false);

		// Physics 속성 역직렬화
		FJsonSerializer::ReadFloat(InOutHandle, "MassOverride", MassOverride, 10.0f, false);
		FJsonSerializer::ReadFloat(InOutHandle, "LinearDampingOverride", LinearDampingOverride, 0.01f, false);
		FJsonSerializer::ReadFloat(InOutHandle, "AngularDampingOverride", AngularDampingOverride, 0.05f, false);
		FJsonSerializer::ReadInt32(InOutHandle, "PhysMaterialPreset", PhysMaterialPreset, 0, false);

		int32 FrictionMode = static_cast<int32>(FrictionCombineModeOverride);
		int32 RestitutionMode = static_cast<int32>(RestitutionCombineModeOverride);
		FJsonSerializer::ReadInt32(InOutHandle, "FrictionCombineModeOverride", FrictionMode, 2, false);
		FJsonSerializer::ReadInt32(InOutHandle, "RestitutionCombineModeOverride", RestitutionMode, 2, false);
		FrictionCombineModeOverride = static_cast<ECombineMode>(FrictionMode);
		RestitutionCombineModeOverride = static_cast<ECombineMode>(RestitutionMode);

		RecreateBodySetup();
	}
	else
	{
		// 직렬화 (저장)
		InOutHandle["bEnableCollision"] = bEnableCollision;
		InOutHandle["bSimulatePhysics"] = bSimulatePhysics;

		InOutHandle["CollisionType"] = static_cast<int32>(CollisionType);
		InOutHandle["BoxExtent"] = FJsonSerializer::VectorToJson(FVector(BoxExtent.X, BoxExtent.Y, BoxExtent.Z));
		InOutHandle["SphereRadius"] = SphereRadius;
		InOutHandle["CapsuleRadius"] = CapsuleRadius;
		InOutHandle["CapsuleHalfHeight"] = CapsuleHalfHeight;
		InOutHandle["CollisionOffset"] = FJsonSerializer::VectorToJson(FVector(CollisionOffset.X, CollisionOffset.Y, CollisionOffset.Z));
		InOutHandle["CollisionRotation"] = FJsonSerializer::VectorToJson(FVector(CollisionRotation.X, CollisionRotation.Y, CollisionRotation.Z));

		// Physics 속성 직렬화
		InOutHandle["MassOverride"] = MassOverride;
		InOutHandle["LinearDampingOverride"] = LinearDampingOverride;
		InOutHandle["AngularDampingOverride"] = AngularDampingOverride;
		InOutHandle["PhysMaterialPreset"] = PhysMaterialPreset;
		InOutHandle["FrictionCombineModeOverride"] = static_cast<int32>(FrictionCombineModeOverride);
		InOutHandle["RestitutionCombineModeOverride"] = static_cast<int32>(RestitutionCombineModeOverride);
	}
}

void UStaticMeshComponent::GetPhysMaterialValues(float& OutStaticFriction, float& OutDynamicFriction, float& OutRestitution) const
{
	// PhysMaterialPreset에 따른 물리 재질 값 반환
	// 0: Default, 1: Mud(진흙), 2: Wood(나무), 3: Rubber(고무), 4: Billiard(당구공), 5: Ice(얼음), 6: Metal(금속)
	switch (PhysMaterialPreset)
	{
	case 1: // Mud (진흙) - 높은 마찰, 탄성 없음
		OutStaticFriction = 1.5f;
		OutDynamicFriction = 1.2f;
		OutRestitution = 0.0f;
		break;
	case 2: // Wood (나무) - 중간 마찰, 약간의 탄성
		OutStaticFriction = 0.4f;
		OutDynamicFriction = 0.3f;
		OutRestitution = 0.4f;
		break;
	case 3: // Rubber (고무공) - 높은 마찰, 높은 탄성
		OutStaticFriction = 1.0f;
		OutDynamicFriction = 0.8f;
		OutRestitution = 0.8f;
		break;
	case 4: // Billiard (당구공) - 낮은 마찰, 매우 높은 탄성
		OutStaticFriction = 0.2f;
		OutDynamicFriction = 0.15f;
		OutRestitution = 0.95f;
		break;
	case 5: // Ice (얼음) - 매우 낮은 마찰, 약간의 탄성
		OutStaticFriction = 0.05f;
		OutDynamicFriction = 0.03f;
		OutRestitution = 0.1f;
		break;
	case 6: // Metal (금속) - 중간 마찰, 중간 탄성
		OutStaticFriction = 0.6f;
		OutDynamicFriction = 0.4f;
		OutRestitution = 0.3f;
		break;
	case 0: // Default
	default:
		OutStaticFriction = 0.5f;
		OutDynamicFriction = 0.4f;
		OutRestitution = 0.0f;
		break;
	}
}
