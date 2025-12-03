#include "pch.h"
#include "PlayerCameraManager.h"
#include "Camera/CameraModifierBase.h"
#include "Camera/CamMod_Fade.h"
#include "Camera/CamMod_Shake.h"
#include "Camera/CamMod_LetterBox.h"
#include "Camera/CamMod_Vignette.h"
#include "Camera/CamMod_Gamma.h"
#include "Camera/CamMod_DOF.h"
#include "SceneView.h"
#include "CameraActor.h"
#include "World.h"
#include "FViewport.h"
#include "RenderSettings.h"
// IMPLEMENT_CLASS is now auto-generated in .generated.cpp
//
//BEGIN_PROPERTIES(APlayerCameraManager)
//	MARK_AS_SPAWNABLE("플레이어 카메라 매니저", "최종적으로 카메라 화면을 관리하는 액터입니다. (씬에 1개만 존재 필요)")
//	ADD_PROPERTY_CURVE(TransitionCurve, "트랜지션", true, "카메라 전환에 사용할 이징 곡선입니다. X축:시간, Y축:강도")
//END_PROPERTIES()

namespace
{
	float GetBezierValueY(float Curve[4], float T)
	{
		const float P1_y = Curve[1];
		const float P2_y = Curve[3];

		const float OneMinusT = 1.0f - T;
		const float TSquared = T * T;
		const float OneMinusTSquared = OneMinusT * OneMinusT;

		// y(t) = 3(1-t)^2 * t * P1_y  +  3(1-t) * t^2 * P2_y  +  t^3
		return (3.0f * OneMinusTSquared * T * P1_y) +
			(3.0f * OneMinusT * TSquared * P2_y) +
			(TSquared * T);
	}
}

APlayerCameraManager::~APlayerCameraManager()
{
	// 남아있는 모든 모디파이어 객체를 삭제합니다.
	for (UCameraModifierBase* Modifier : ActiveModifiers)
	{
		if (Modifier)
		{
			DeleteObject(Modifier);
		}
	}
	ActiveModifiers.Empty();

	CurrentViewCamera = nullptr;

	CachedViewport = nullptr;
}

void APlayerCameraManager::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();

	// ActiveModifiers는 raw pointer 배열이므로 얕은 복사 시 댕글링 포인터 발생
	// PIE용 복제본은 모디파이어 없이 시작해야 함 (게임플레이 중에 새로 생성됨)
	ActiveModifiers.Empty();
	Modifiers.clear();

	// 카메라 참조도 초기화 (PIE World에서 새로 찾아야 함)
	CurrentViewCamera = nullptr;
	CachedViewport = nullptr;

	// 블렌딩 상태 초기화
	BlendTimeTotal = 0.0f;
	BlendTimeRemaining = 0.0f;

	// Vignette 인덱스 초기화
	LastVignetteIdx = 0;
}

void APlayerCameraManager::BeginPlay()
{
	Super::BeginPlay();

	CurrentViewCamera = GetWorld()->FindComponent<UCameraComponent>();
	if (!CurrentViewCamera)
	{
		UE_LOG("[warning] 현재 월드에 카메라가 없습니다. (Editor에서만 Editor 전용 카메라로 Fallback 처리됨)");
	}
}

// 월드에 또 다른 APlayerCameraManager 가 있을 때만 삭제 가능
void APlayerCameraManager::Destroy()
{
	// 교체할 수 있으면 해당 매니저로 교체 후 삭제 허용
	TArray<APlayerCameraManager*> PlayerCameraManagers = GetWorld()->FindActors<APlayerCameraManager>();
	if (1 < PlayerCameraManagers.Num())
	{
		for (APlayerCameraManager* PlayerCameraManager : PlayerCameraManagers)
		{
			if (this != PlayerCameraManager)
			{
				GetWorld()->SetPlayerCameraManager(PlayerCameraManager);
				Super::Destroy();
				return;
			}
		}
	}

	UE_LOG("[warning] PlayerCameraManager는 삭제할 수 없습니다. (새로운 매니저를 만들고 삭제하면 가능)");
}

// 만약 현재 월드에 카메라가 없었으면 이 카메라가 View로 등록됨
void APlayerCameraManager::RegisterView(UCameraComponent* RegisterViewTarget)
{
	if (!CurrentViewCamera)
	{
		CurrentViewCamera = RegisterViewTarget;
	}
}

// 만약 해당 카메라를 뷰로 사용 중이었다면 해제
void APlayerCameraManager::UnregisterView(UCameraComponent* UnregisterViewTarget)
{
	if (CurrentViewCamera == UnregisterViewTarget)
	{
		CurrentViewCamera = nullptr;
		CurrentViewCamera = GetWorld()->FindComponent<UCameraComponent>();	// 현재 카메라는 PendingDestroy 라서 다른 카메라가 반환됨

		if (!CurrentViewCamera)
		{
			UE_LOG("[warning] 현재 월드에 카메라가 없습니다. (Editor에서만 Editor 전용 카메라로 Fallback 처리됨)");
		}
	}
}

void APlayerCameraManager::SetViewCamera(UCameraComponent* NewViewTarget)
{
	CurrentViewCamera = NewViewTarget;
	BlendTimeRemaining = 0.0f; // 블렌딩 취소
}

void APlayerCameraManager::SetViewCameraWithBlend(UCameraComponent* NewViewTarget, float InBlendTime)
{
	// "현재 뷰 타겟"의 뷰 정보를 스냅샷으로 저장해야 합니다.
	BlendStartViewInfo = CurrentViewInfo;
	CurrentViewCamera = NewViewTarget;

	// 현재 뷰를 블렌딩 시작 뷰로 저장
	BlendTimeTotal = InBlendTime;
	BlendTimeRemaining = InBlendTime;
}

void APlayerCameraManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// 매 틱 모든 카메라 로직을 계산하여 SceneView를 업데이트합니다.
	BuildForFrame(DeltaTime);
}

void APlayerCameraManager::BuildForFrame(float DeltaTime)
{
	// CurrentViewInfo를 현재 카메라를 기준으로 설정 (트렌지션 중에는 사이 값으로 설정)
	UpdateViewInfo(DeltaTime);

	// 모든 Modifier tick Update
	for (UCameraModifierBase* M : ActiveModifiers)
	{
		if (!M || !M->bEnabled || M->Weight <= 0.f) continue;
		M->ApplyToView(DeltaTime, &CurrentViewInfo);
	}

	// 2) 후처리: PP 모디파이어 초기화 + 수집
	Modifiers.clear();
	for (UCameraModifierBase* M : ActiveModifiers)
	{
		if (!M || !M->bEnabled || M->Weight <= 0.f) continue;
		M->CollectPostProcess(Modifiers);
	}

	// 3) 수명 정리
	for (int32 i= ActiveModifiers.Num()-1; i>=0; i--)
	{
		UCameraModifierBase* M = ActiveModifiers[i];
		if (!M) 
		{ 
			ActiveModifiers.RemoveAtSwap(i); 
			continue; 
		}
		
		ActiveModifiers[i]->TickLifetime(DeltaTime);

		if (M->Duration >= 0.f && !M->bEnabled)
		{
			DeleteObject(M);
			ActiveModifiers.RemoveAtSwap(i); 
			continue; 
		}
	}

	if (CachedViewport)
	{
		// 최종 종횡비 (AspectRatio)
		if (CurrentViewInfo.ViewRect.Height() > 0)
		{
			CurrentViewInfo.AspectRatio = (float)CurrentViewInfo.ViewRect.Width() / (float)CurrentViewInfo.ViewRect.Height();
		}
		else
		{
			CurrentViewInfo.AspectRatio = 1.7777f; // 16:9 폴백
		}
	}

	else
	{
		// 폴백 (뷰포트가 아직 캐시 안됨)
		CurrentViewInfo.AspectRatio = 1.7777f;
		CurrentViewInfo.ViewRect = { 0, 0, 1920, 1080 };
	}
}

void APlayerCameraManager::StartCameraShake(float InDuration, float AmpLoc, float AmpRotDeg, float Frequency,
	int32 InPriority)
{
	UCamMod_Shake* ShakeModifier = NewObject<UCamMod_Shake>();
	ShakeModifier->Priority = InPriority;
	ShakeModifier->Initialize(InDuration, AmpLoc, AmpRotDeg, Frequency);
	ActiveModifiers.Add(ShakeModifier);
}

void APlayerCameraManager::StartFade(float InDuration, float FromAlpha, float ToAlpha, const FLinearColor& InColor,
	int32 InPriority)
{
	UCamMod_Fade* FadeModifier = NewObject<UCamMod_Fade>();
	FadeModifier->Priority = InPriority;
	FadeModifier->bEnabled = true;

	FadeModifier->FadeColor = InColor;
	FadeModifier->StartAlpha = FMath::Clamp(FromAlpha, 0.f, 1.f);
	FadeModifier->EndAlpha = FMath::Clamp(ToAlpha, 0.f, 1.f);
	FadeModifier->Duration = FMath::Max(0.f, InDuration);
	FadeModifier->Elapsed = 0.f;
	FadeModifier->CurrentAlpha = FadeModifier->StartAlpha;

	ActiveModifiers.Add(FadeModifier);
	// ActiveModifiers.Sort([](UCameraModifierBase* A, UCameraModifierBase* B){ return *A < *B; });
}

void APlayerCameraManager::StartLetterBox(float InDuration, float Aspect, float BarHeight, const FLinearColor& InColor, int32 InPriority)
{
	UCamMod_LetterBox* LetterBoxModifier = NewObject<UCamMod_LetterBox>();
	LetterBoxModifier->Duration = InDuration;
	LetterBoxModifier->Priority = InPriority;
	LetterBoxModifier->AspectRatio = Aspect;
	LetterBoxModifier->HeightBarSize = BarHeight;
	LetterBoxModifier->BoxColor = InColor;

	ActiveModifiers.Add(LetterBoxModifier);
}

int APlayerCameraManager::StartVignette(float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor, int32 InPriority)
{
	UCamMod_Vignette* VignetteModifier = NewObject<UCamMod_Vignette>();
	VignetteModifier->Duration = InDuration;
	VignetteModifier->Priority = InPriority;
	VignetteModifier->Radius = Radius;
	VignetteModifier->Softness = Softness;
	VignetteModifier->Intensity = Intensity;
	VignetteModifier->Roundness = Roundness;
	VignetteModifier->Color = InColor;

	ActiveModifiers.Add(VignetteModifier);
	return (ActiveModifiers.Num() - 1);
}

int APlayerCameraManager::UpdateVignette(int Idx, float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor, int32 InPriority)
{
	if (ActiveModifiers.Num() <= Idx || !ActiveModifiers[Idx] || !Cast<UCamMod_Vignette>(ActiveModifiers[Idx]))
	{
		return -1;
	}

	UCamMod_Vignette* VignetteModifier = Cast<UCamMod_Vignette>(ActiveModifiers[Idx]);
	VignetteModifier->Duration = InDuration;
	VignetteModifier->Priority = InPriority;
	VignetteModifier->Radius = Radius;
	VignetteModifier->Softness = Softness;
	VignetteModifier->Intensity = Intensity;
	VignetteModifier->Roundness = Roundness;
	VignetteModifier->Color = InColor;

	ActiveModifiers[Idx] = VignetteModifier;

	return Idx;
}

void APlayerCameraManager::AdjustVignette(float InDuration, float Radius, float Softness, float Intensity, float Roundness, const FLinearColor& InColor, int32 InPriority)
{
	if (LastVignetteIdx == -1)
	{
		LastVignetteIdx = StartVignette(InDuration, Radius, Softness, Intensity, Roundness, InColor, InPriority);
	}
	else
	{
		LastVignetteIdx = UpdateVignette(LastVignetteIdx, InDuration, Radius, Softness, Intensity, Roundness, InColor, InPriority);
	}
}

void APlayerCameraManager::DeleteVignette()
{
	if (ActiveModifiers.Num() <= LastVignetteIdx || !ActiveModifiers[LastVignetteIdx] || !Cast<UCamMod_Vignette>(ActiveModifiers[LastVignetteIdx]))
		return;
	ActiveModifiers[LastVignetteIdx]->bEnabled = false;
}

void APlayerCameraManager::StartGamma(float Gamma)
{
	//if (ActiveModifiers.size() > 0) return;

	UCamMod_Gamma* GammaModifier = NewObject<UCamMod_Gamma>();
	GammaModifier->Gamma = Gamma;

	ActiveModifiers.Add(GammaModifier);
}

void APlayerCameraManager::StartDOF(
	float FocalDistance,
	float FocalRegion,
	float NearTransitionRegion,
	float FarTransitionRegion,
	float MaxNearBlurSize,
	float MaxFarBlurSize,
	int32 InPriority)
{
	// 기존 DOF Modifier 제거 (중복 방지)
	for (int32 i = ActiveModifiers.Num() - 1; i >= 0; --i)
	{
		if (UCamMod_DOF* ExistingDOF = Cast<UCamMod_DOF>(ActiveModifiers[i]))
		{
			DeleteObject(ExistingDOF);
			ActiveModifiers.RemoveAt(i);
		}
	}

	UCamMod_DOF* DOFModifier = NewObject<UCamMod_DOF>();
	DOFModifier->Priority = InPriority;
	DOFModifier->bEnabled = true;

	DOFModifier->FocalDistance = FocalDistance;
	DOFModifier->FocalRegion = FocalRegion;
	DOFModifier->NearTransitionRegion = NearTransitionRegion;
	DOFModifier->FarTransitionRegion = FarTransitionRegion;
	DOFModifier->MaxNearBlurSize = MaxNearBlurSize;
	DOFModifier->MaxFarBlurSize = MaxFarBlurSize;

	ActiveModifiers.Add(DOFModifier);
}

// CurrentViewInfo를 현재 카메라를 기준으로 설정 (트렌지션 중에는 사이 값으로 설정)
void APlayerCameraManager::UpdateViewInfo(float DeltaTime)
{
	if (CachedViewport)
	{
		// 최종 뷰 영역 (ViewRect)
		CurrentViewInfo.ViewRect.MinX = CachedViewport->GetStartX();
		CurrentViewInfo.ViewRect.MinY = CachedViewport->GetStartY();
		CurrentViewInfo.ViewRect.MaxX = CurrentViewInfo.ViewRect.MinX + CachedViewport->GetSizeX();
		CurrentViewInfo.ViewRect.MaxY = CurrentViewInfo.ViewRect.MinY + CachedViewport->GetSizeY();
	}

	if (0.0 < BlendTimeRemaining) // 1. 트렌지션 중일 때
	{
		float V = 1.0f;
		if (BlendTimeTotal > KINDA_SMALL_NUMBER)
		{
			V = 1.0f - (BlendTimeRemaining / BlendTimeTotal);
		}
		V = FMath::Clamp(V, 0.0f, 1.0f);

		V = GetBezierValueY(TransitionCurve, V);

		// --- 시작 값 ---
		FVector StartLocation = BlendStartViewInfo.ViewLocation;
		FQuat StartRotation = BlendStartViewInfo.ViewRotation;
		float StartFOV = BlendStartViewInfo.FieldOfView;

		// --- 목표 값 ---
		FVector TargetLocation = CurrentViewCamera->GetWorldLocation();
		FQuat TargetRotation = CurrentViewCamera->GetWorldRotation();
		float TargetFOV = CurrentViewCamera->GetFOV();

		// --- SceneView 보간 ---
		CurrentViewInfo.ViewLocation = FVector::Lerp(StartLocation, TargetLocation, V);
		CurrentViewInfo.ViewRotation = FQuat::Slerp(StartRotation, TargetRotation, V);
		CurrentViewInfo.FieldOfView = FMath::Lerp(StartFOV, TargetFOV, V); // FOV 보간

		// Near/Far 등은 최종 타겟의 것을 즉시 따름
		CurrentViewInfo.NearClip = CurrentViewCamera->GetNearClip();
		CurrentViewInfo.FarClip = CurrentViewCamera->GetFarClip();
		CurrentViewInfo.ZoomFactor = CurrentViewCamera->GetZoomFactor();
		CurrentViewInfo.ProjectionMode = CurrentViewCamera->GetProjectionMode();

		// 남은 시간 계산
		BlendTimeRemaining -= DeltaTime;
		if (BlendTimeRemaining <= 0.0f)
		{
			// 최종 값으로 고정
			CurrentViewInfo.ViewLocation = TargetLocation;
			CurrentViewInfo.ViewRotation = TargetRotation;
			CurrentViewInfo.FieldOfView = TargetFOV; // FOV 고정
		}
	}
	else if (CurrentViewCamera) // 2. 트렌지션 중이 아닐 때
	{
		// SceneView의 모든 기본 값을 현재 뷰 카메라의 값으로 설정
		CurrentViewInfo.ViewLocation = CurrentViewCamera->GetWorldLocation();
		CurrentViewInfo.ViewRotation = CurrentViewCamera->GetWorldRotation();
		CurrentViewInfo.NearClip = CurrentViewCamera->GetNearClip();
		CurrentViewInfo.FarClip = CurrentViewCamera->GetFarClip();
		CurrentViewInfo.FieldOfView = CurrentViewCamera->GetFOV();
		CurrentViewInfo.ZoomFactor = CurrentViewCamera->GetZoomFactor();
		CurrentViewInfo.ProjectionMode = CurrentViewCamera->GetProjectionMode();
	}
}