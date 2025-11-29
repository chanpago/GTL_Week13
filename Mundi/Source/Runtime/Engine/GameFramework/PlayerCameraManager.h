#pragma once

#include "Actor.h"
#include "SceneView.h"
#include "APlayerCameraManager.generated.h"

class UCameraComponent;
class UCameraModifierBase;
class FViewport;
class URenderSettings;
class UCamMod_Fade;

UCLASS(DisplayName="APlayerCameraManager", Description="APlayerCameraManager 액터")
class APlayerCameraManager : public AActor
{

	GENERATED_REFLECTION_BODY()

public:
	APlayerCameraManager() { ObjectName = "Player Camera Manager";  };

protected:
	~APlayerCameraManager() override;

public:
	void BeginPlay() override;
	void Destroy() override;
	void Tick(float DeltaTime) override;

	void RegisterView(UCameraComponent* RegisterViewTarget);
	void UnregisterView(UCameraComponent* UnregisterViewTarget);
	UCameraComponent* GetViewCamera() { return CurrentViewCamera; }
	void SetViewCamera(UCameraComponent* NewViewTarget);
	void SetViewCameraWithBlend(UCameraComponent* NewViewTarget, float InBlendTime);

	void StartCameraShake(float InDuration, float AmpLoc, float AmpRotDeg, float Frequency, int32 InPriority = 0);
	
	void StartFade(float InDuration, float FromAlpha, float ToAlpha, const FLinearColor& InColor= FLinearColor::Zero(), int32 InPriority = 0);
	
	void StartLetterBox(float InDuration, float Aspect, float BarHeight, const FLinearColor& InColor = FLinearColor::Zero(), int32 InPriority = 0);
	
	int StartVignette(float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor = FLinearColor::Zero(), int32 InPriority = 0);
	int UpdateVignette(int Idx, float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor = FLinearColor::Zero(), int32 InPriority = 0);
	void AdjustVignette(float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor = FLinearColor::Zero(), int32 InPriority = 0);
	void DeleteVignette();

	void StartGamma(float Gamma);

	void StartDOF(
		float FocalDistance = 5.0f,          // m (meters)
		float FocalRegion = 0.5f,            // m (0.5m = 50cm)
		float NearTransitionRegion = 2.0f,   // m
		float FarTransitionRegion = 5.0f,    // m
		float MaxNearBlurSize = 32.0f,       // pixels
		float MaxFarBlurSize = 32.0f,        // pixels
		int32 InPriority = 0
	); 

public:
	TArray<UCameraModifierBase*> ActiveModifiers;

	inline void FadeIn(float Duration, const FLinearColor& Color=FLinearColor::Zero(), int32 Priority=0)
	{   // 검은 화면(1) → 씬(0)
		StartFade(Duration, 1.f, 0.f, Color, Priority);
	}
	inline void FadeOut(float Duration, const FLinearColor& Color = FLinearColor::Zero(), int32 Priority = 0)
	{   // 씬(0) → 검은 화면(1)
		StartFade(Duration, 0.f, 1.f, Color, Priority);
	}

public:
	void CacheViewport(FViewport* InViewport) { CachedViewport = InViewport; }
	// Tick에서 미리 계산한 ViewInfo 반환
	FMinimalViewInfo* GetCurrentViewInfo() { return &CurrentViewInfo; }
	void UpdateViewInfo(float DeltaTime);
	TArray<FPostProcessModifier> GetModifiers() { return Modifiers; };

protected:
	void AddModifier(UCameraModifierBase* Modifier)
	{
		ActiveModifiers.Add(Modifier);
	}
	void BuildForFrame(float DeltaTime);

private:
	UCameraComponent* CurrentViewCamera{};

	FMinimalViewInfo CurrentViewInfo{};
	FMinimalViewInfo BlendStartViewInfo{};

	TArray<FPostProcessModifier> Modifiers;

	FViewport* CachedViewport = nullptr;

	float BlendTimeTotal = 0.0f;
	float BlendTimeRemaining = 0.0f;

	UPROPERTY(EditAnywhere, Category="트랜지션", Tooltip="카메라 전환에 사용할 이징 곡선입니다. X축:시간, Y축:강도")
	float TransitionCurve[4] = { 0.47f, 0.0f, 0.745f, 0.715f };

	// TODO : 감싸기 or 배열로 관리, 현재 vignette 1개만 Update 가능
	// Vignette 연속 효과를 위한 IDX
	int LastVignetteIdx = 0;
};
