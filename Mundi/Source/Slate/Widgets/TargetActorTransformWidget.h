#pragma once
#include "Widget.h"
#include "Vector.h"
#include "Enums.h"

class UUIManager;
class UWorld;
class AActor;
class AGizmoActor;
class USceneComponent;
class UStaticMeshComponent;
class APlayerCameraManager;

static const char* kDisplayItems[] = {
				"Pawn_64x.png",
				"PointLight_64x.png",
				"SpotLight_64x.png"
};

static const char* kFullPaths[] = {
	"Editor/Pawn_64x.png",
	"Editor/PointLight_64x.png",
	"Editor/SpotLight_64x.png"
};

class UTargetActorTransformWidget
	: public UWidget
{
public:
	DECLARE_CLASS(UTargetActorTransformWidget, UWidget)

	void Initialize() override;
	void Update() override;
	void RenderWidget() override;

	void UpdateTransformFromComponent(USceneComponent* SelectedComponent);

	// Special Member Function
	UTargetActorTransformWidget();
	~UTargetActorTransformWidget() override;

	// 선택된 액터가 외부에서 삭제되었을 때 호출되어 내부 상태를 정리
	void OnSelectedActorCleared();

	void RenderHeader(AActor* SelectedActor, UActorComponent* SelectedComponent);
	void RenderComponentHierarchy(AActor* SelectedActor, UActorComponent* SelectedComponent);
	void RenderSelectedActorDetails(AActor* SelectedActor);
	void RenderSelectedComponentDetails(UActorComponent* SelectedComponent);
	void RenderDOFSettings(APlayerCameraManager* CamMgr);

private:
	UUIManager* UIManager = nullptr;
	FString CachedActorName; // 액터 이름 캐시 (안전한 출력을 위해)
	//ScneComponent가 아닌 ActorComponent 선택
	UActorComponent* SelectedActorComponent = nullptr; 

	// Transform UI 상태
	FVector EditLocation = { 0.0f, 0.0f, 0.0f };
	FVector EditRotation = { 0.0f, 0.0f, 0.0f };
	FVector EditScale = { 1.0f, 1.0f, 1.0f };

	// UI 변경 플래그
	bool bScaleChanged = false;
	bool bRotationChanged = false;
	bool bPositionChanged = false;
	bool bUniformScale = false;

	// 회전 UI 증분 누적용 상태
	FVector PrevEditRotationUI = FVector(0, 0, 0); // 직전 프레임 UI 표시값(도)
	bool bRotationEditing = false;               // 현재 회전 필드가 편집(active) 중인가

	// 헬퍼 메서드
	void ResetChangeFlags();
};
