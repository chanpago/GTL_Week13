#pragma once

#include "MeshComponent.h"
#include "AABB.h"
#include "Source/Runtime/Engine/Physics/PhysicalMaterial.h"
#include "UStaticMeshComponent.generated.h"

class FPhysScene;
class UStaticMesh;
class UShader;
class UTexture;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPhysicalMaterial;
struct FSceneCompData;

UCLASS(DisplayName="스태틱 메시 컴포넌트", Description="정적 메시를 렌더링하는 컴포넌트입니다")
class UStaticMeshComponent : public UMeshComponent
{
public:

	GENERATED_REFLECTION_BODY()

	UStaticMeshComponent();

protected:
	~UStaticMeshComponent() override;

public:
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	void OnStaticMeshReleased(UStaticMesh* ReleasedMesh);

	void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

	void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

	void SetStaticMesh(const FString& PathFileName);

	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	
	FAABB GetWorldAABB() const override;

	void DuplicateSubObjects() override;

	void InitPhysics(FPhysScene& PhysScene);

	class UBodySetup* GetBodySetup() const;

protected:
	void OnTransformUpdated() override;

protected:
	UPROPERTY(EditAnywhere, Category="Static Mesh", Tooltip="Static mesh asset to render")
	UStaticMesh* StaticMesh = nullptr;

	// Physics 설정
	UPROPERTY(EditAnywhere, Category="Physics", Tooltip="Enable physics simulation for this mesh")
	bool bSimulatePhysics = false;

	UPROPERTY(EditAnywhere, Category="Physics", Tooltip="If true, this is a static collider (immovable). If false, it's dynamic (affected by gravity/forces)")
	bool bIsStaticPhysics = true;

	// Physics Material Override 설정
	UPROPERTY(EditAnywhere, Category="Physics", Tooltip="Mass in kg")
	float MassOverride = 10.0f;

	UPROPERTY(EditAnywhere, Category="Physics", Tooltip="Linear damping coefficient")
	float LinearDampingOverride = 0.01f;

	UPROPERTY(EditAnywhere, Category="Physics", Tooltip="Angular damping coefficient")
	float AngularDampingOverride = 0.05f;

	UPROPERTY(EditAnywhere, Category="Physics", Tooltip="Physical material preset index (0=Default, 1=Mud, 2=Wood, 3=Rubber, 4=Billiard)")
	int32 PhysMaterialPreset = 0;

	UPROPERTY(EditAnywhere, Category="Physics", Tooltip="Friction combine mode")
	ECombineMode FrictionCombineModeOverride = ECombineMode::Multiply;

	UPROPERTY(EditAnywhere, Category="Physics", Tooltip="Restitution combine mode")
	ECombineMode RestitutionCombineModeOverride = ECombineMode::Multiply;

public:
	// Getters/Setters for physics properties
	float GetMassOverride() const { return MassOverride; }
	void SetMassOverride(float InMass) { MassOverride = InMass; }

	float GetLinearDampingOverride() const { return LinearDampingOverride; }
	void SetLinearDampingOverride(float InDamping) { LinearDampingOverride = InDamping; }

	float GetAngularDampingOverride() const { return AngularDampingOverride; }
	void SetAngularDampingOverride(float InDamping) { AngularDampingOverride = InDamping; }

	int32 GetPhysMaterialPreset() const { return PhysMaterialPreset; }
	void SetPhysMaterialPreset(int32 InPreset) { PhysMaterialPreset = InPreset; }

	ECombineMode GetFrictionCombineModeOverride() const { return FrictionCombineModeOverride; }
	void SetFrictionCombineModeOverride(ECombineMode InMode) { FrictionCombineModeOverride = InMode; }

	ECombineMode GetRestitutionCombineModeOverride() const { return RestitutionCombineModeOverride; }
	void SetRestitutionCombineModeOverride(ECombineMode InMode) { RestitutionCombineModeOverride = InMode; }

	// 현재 선택된 프리셋에 맞는 PhysicalMaterial 값을 가져오기
	void GetPhysMaterialValues(float& OutStaticFriction, float& OutDynamicFriction, float& OutRestitution) const;
};
