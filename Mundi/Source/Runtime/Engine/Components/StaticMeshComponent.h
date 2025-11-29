#pragma once

#include "MeshComponent.h"
#include "AABB.h"
#include "UStaticMeshComponent.generated.h"

class FPhysScene;
class UStaticMesh;
class UShader;
class UTexture;
class UMaterialInterface;
class UMaterialInstanceDynamic;
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
};
