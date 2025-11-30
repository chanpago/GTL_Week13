#include "pch.h"
#include "DirectionalLightComponent.h"
#include "BillboardComponent.h"
#include "Gizmo/GizmoArrowComponent.h"
#include "SceneView.h"
#include "FViewport.h"

#include "RenderManager.h"
#include "D3D11RHI.h"
// IMPLEMENT_CLASS is now auto-generated in .generated.cpp
//BEGIN_PROPERTIES(UDirectionalLightComponent)
//	MARK_AS_COMPONENT("디렉셔널 라이트", "방향성 라이트 (태양광 같은 평행광) 컴포넌트입니다.")
//	ADD_PROPERTY(bool, bCascaded, "ShadowMap", true, "Cascaded 사용 여부")
//	ADD_PROPERTY_RANGE(int, CascadedCount, "ShadowMap", 1, 8, true, "Cascaded 갯수")
//	ADD_PROPERTY_RANGE(float, CascadedLinearBlendingValue, "ShadowMap", 0, 1, true, "Cascaded Log~Linear 가중치 : 0~1")
//	ADD_PROPERTY_RANGE(float, CascadedOverlapValue, "ShadowMap", 0, 0.5f, true, "Cascaded 확장 범위 크기")
//	ADD_PROPERTY_RANGE(float, CascadedAreaColorDebugValue, "ShadowMap", 0, 1.0f, true, "Cascaded 범위 시각화")
//	ADD_PROPERTY_RANGE(int, CascadedAreaShadowDebugValue, "ShadowMap", -1, 8, true, "Cascaded 쉐도우 구역 설정 (-1 : 전체 쉐도우)")
//	ADD_PROPERTY_SRV(ID3D11ShaderResourceView*, ShadowMapSRV, "ShadowMap", true, "쉐도우 맵 Far Plane")
//	ADD_PROPERTY(bool, bOverrideCameraLightPerspective, "ShadowMap", true, "Override Camera Light Perspective")
//END_PROPERTIES()

namespace
{
	TArray<float> GetCascadedSliceDepth(int CascadedCount, float LinearBlending, float NearClip, float FarClip)
	{
		TArray<float> CascadedSliceDepth;
		CascadedSliceDepth.reserve(CascadedCount + 1);
		LinearBlending = LinearBlending < 0 ? 0 : (LinearBlending > 1 ? 1 : LinearBlending); //clamp(0,1,LinearBlending);
		for (int i = 0; i < CascadedCount + 1; i++)
		{
			float CurDepth = 0;
			float LogValue = NearClip * pow((FarClip / NearClip), (float)i / CascadedCount);
			float LinearValue = NearClip + (FarClip - NearClip) * (float)i / CascadedCount;
			CascadedSliceDepth.push_back((1 - LinearBlending) * LogValue + LinearBlending * LinearValue);
		}
		return CascadedSliceDepth;
	}

	TArray<FVector> GetFrustumVertices(ECameraProjectionMode ProjectionMode, FViewportRect ViewRect, float FieldOfView, float AspectRatio, float Near, float Far, float ZoomFactor)
	{
		TArray<FVector> Vertices;
		Vertices.reserve(8);
		float NearHalfHeight, NearHalfWidth, FarHalfHeight, FarHalfWidth;
		if (ProjectionMode == ECameraProjectionMode::Perspective)
		{
			const float Tan = tan(DegreesToRadians(FieldOfView) * 0.5f);
			NearHalfHeight = Tan * Near;
			NearHalfWidth = NearHalfHeight * AspectRatio;
			FarHalfHeight = Tan * Far;
			FarHalfWidth = FarHalfHeight * AspectRatio;
		}
		else
		{
			const float pixelsPerWorldUnit = 10.0f;

			NearHalfHeight = (ViewRect.MinY / pixelsPerWorldUnit) * ZoomFactor;
			NearHalfWidth = (ViewRect.MinX / pixelsPerWorldUnit) * ZoomFactor;
			FarHalfHeight = NearHalfHeight;
			FarHalfWidth = NearHalfWidth;
		}
		Vertices.emplace_back(FVector(-NearHalfWidth, -NearHalfHeight, Near));
		Vertices.emplace_back(FVector(NearHalfWidth, -NearHalfHeight, Near));
		Vertices.emplace_back(FVector(NearHalfWidth, NearHalfHeight, Near));
		Vertices.emplace_back(FVector(-NearHalfWidth, NearHalfHeight, Near));
		Vertices.emplace_back(FVector(-FarHalfWidth, -FarHalfHeight, Far));
		Vertices.emplace_back(FVector(FarHalfWidth, -FarHalfHeight, Far));
		Vertices.emplace_back(FVector(FarHalfWidth, FarHalfHeight, Far));
		Vertices.emplace_back(FVector(-FarHalfWidth, FarHalfHeight, Far));

		return Vertices;
	}

}

UDirectionalLightComponent::UDirectionalLightComponent()
{
	ShadowResolutionScale = 2048;
}

UDirectionalLightComponent::~UDirectionalLightComponent()
{
}

void UDirectionalLightComponent::GetShadowRenderRequests(FSceneView* View, TArray<FShadowRenderRequest>& OutRequests)
{
	FMatrix ShadowMapView = GetWorldRotation().Inverse().ToMatrix() * FMatrix::ZUpToYUp;
	FMatrix ViewInv = View->ViewMatrix.InverseAffine();
	if (bCascaded == false)
	{
		TArray<FVector> CameraFrustum = GetFrustumVertices(View->ProjectionMode, View->ViewRect, View->FieldOfView, View->AspectRatio, View->NearClip, View->FarClip, View->ZoomFactor);

		// 프러스텀을 월드 -> 라이트 스페이스로 변환
		CameraFrustum *= ViewInv;
		CameraFrustum *= ShadowMapView;

		// 라이트 스페이스에서 실제 AABB 계산
		FAABB CameraFrustumAABB = FAABB(CameraFrustum);

		// 라이트 스페이스 XY 평면에서 바운딩 서클 계산 (쉐도우맵 커버리지 기준)
		FVector LightSpaceCenter = (CameraFrustumAABB.Min + CameraFrustumAABB.Max) * 0.5f;
		float BoundingCircleRadius = 0.0f;
		for (int v = 0; v < CameraFrustum.Num(); v++)
		{
			// XY 평면에서의 거리만 계산 (Z 제외)
			float DX = CameraFrustum[v].X - LightSpaceCenter.X;
			float DY = CameraFrustum[v].Y - LightSpaceCenter.Y;
			float DistXY = FMath::Sqrt(DX * DX + DY * DY);
			BoundingCircleRadius = FMath::Max(BoundingCircleRadius, DistXY);
		}
		float BoundingCircleDiameter = BoundingCircleRadius * 2.0f;

		// 라이트 스페이스 AABB XY와 바운딩 서클 중 큰 값 사용
		FVector AABBSize = CameraFrustumAABB.Max - CameraFrustumAABB.Min;
		float MaxXYLightSpace = FMath::Max(AABBSize.X, AABBSize.Y);
		float MaxXY = FMath::Max(MaxXYLightSpace, BoundingCircleDiameter);

		float HalfXY = MaxXY * 0.5f;
		float WorldSizePerTexel = MaxXY / ShadowResolutionScale;

		// AABB 중심을 기준으로 정사각형 확장 (프러스텀 전체 커버 보장)
		FVector AABBCenter = (CameraFrustumAABB.Min + CameraFrustumAABB.Max) * 0.5f;
		// 텍셀 안정성을 위해 중심을 스냅
		AABBCenter = AABBCenter.SnapToGrid(FVector(WorldSizePerTexel, WorldSizePerTexel, 0), true);

		CameraFrustumAABB.Min.X = AABBCenter.X - HalfXY;
		CameraFrustumAABB.Min.Y = AABBCenter.Y - HalfXY;
		CameraFrustumAABB.Max.X = AABBCenter.X + HalfXY;
		CameraFrustumAABB.Max.Y = AABBCenter.Y + HalfXY;
		// Z는 뒤쪽으로 확장하여 캐스터 포함
		CameraFrustumAABB.Min.Z -= View->FarClip;

		FMatrix ShadowMapOrtho = FMatrix::OrthoMatrix(CameraFrustumAABB);
		FShadowRenderRequest ShadowRenderRequest;
		ShadowRenderRequest.LightOwner = this;
		ShadowRenderRequest.ViewMatrix = ShadowMapView;
		ShadowRenderRequest.ProjectionMatrix = ShadowMapOrtho;
		ShadowRenderRequest.Size = ShadowResolutionScale;
		ShadowRenderRequest.SubViewIndex = 0;
		ShadowRenderRequest.AtlasScaleOffset = 0;
		OutRequests.Add(ShadowRenderRequest);
	}
	else
	{
		CascadedSliceDepth = GetCascadedSliceDepth(CascadedCount, CascadedLinearBlendingValue, View->NearClip, View->FarClip);
		for (int i = 0; i < CascadedCount; i++)
		{
			float Near = CascadedSliceDepth[i];
			float Far = CascadedSliceDepth[i + 1];
			//Near -= Near * CascadedOverlapValue;
			Far += Far * CascadedOverlapValue;
			TArray<FVector> CameraFrustum = GetFrustumVertices(View->ProjectionMode, View->ViewRect, View->FieldOfView, View->AspectRatio, Near, Far, View->ZoomFactor);

			// 프러스텀을 월드 -> 라이트 스페이스로 변환
			CameraFrustum *= ViewInv;
			CameraFrustum *= ShadowMapView;

			// 라이트 스페이스에서 실제 AABB 계산
			FAABB CameraFrustumAABB = FAABB(CameraFrustum);

			// 라이트 스페이스 XY 평면에서 바운딩 서클 계산 (쉐도우맵 커버리지 기준)
			FVector LightSpaceCenter = (CameraFrustumAABB.Min + CameraFrustumAABB.Max) * 0.5f;
			float BoundingCircleRadius = 0.0f;
			for (int v = 0; v < CameraFrustum.Num(); v++)
			{
				// XY 평면에서의 거리만 계산 (Z 제외)
				float DX = CameraFrustum[v].X - LightSpaceCenter.X;
				float DY = CameraFrustum[v].Y - LightSpaceCenter.Y;
				float DistXY = FMath::Sqrt(DX * DX + DY * DY);
				BoundingCircleRadius = FMath::Max(BoundingCircleRadius, DistXY);
			}
			float BoundingCircleDiameter = BoundingCircleRadius * 2.0f;

			// 라이트 스페이스 AABB XY와 바운딩 서클 중 큰 값 사용
			FVector AABBSize = CameraFrustumAABB.Max - CameraFrustumAABB.Min;
			float MaxXYLightSpace = FMath::Max(AABBSize.X, AABBSize.Y);
			float MaxXY = FMath::Max(MaxXYLightSpace, BoundingCircleDiameter);

			float HalfXY = MaxXY * 0.5f;
			float WorldSizePerTexel = MaxXY / ShadowResolutionScale;

			// AABB 중심을 기준으로 정사각형 확장 (프러스텀 전체 커버 보장)
			FVector AABBCenter = (CameraFrustumAABB.Min + CameraFrustumAABB.Max) * 0.5f;
			// 텍셀 안정성을 위해 중심을 스냅
			AABBCenter = AABBCenter.SnapToGrid(FVector(WorldSizePerTexel, WorldSizePerTexel, 0), true);

			CameraFrustumAABB.Min.X = AABBCenter.X - HalfXY;
			CameraFrustumAABB.Min.Y = AABBCenter.Y - HalfXY;
			CameraFrustumAABB.Max.X = AABBCenter.X + HalfXY;
			CameraFrustumAABB.Max.Y = AABBCenter.Y + HalfXY;
			// Z는 뒤쪽으로 확장하여 캐스터 포함
			CameraFrustumAABB.Min.Z -= View->FarClip;

			FMatrix ShadowMapOrtho = FMatrix::OrthoMatrix(CameraFrustumAABB);
			FShadowRenderRequest ShadowRenderRequest;
			ShadowRenderRequest.LightOwner = this;
			ShadowRenderRequest.ViewMatrix = ShadowMapView;
			ShadowRenderRequest.ProjectionMatrix = ShadowMapOrtho;
			ShadowRenderRequest.Size = ShadowResolutionScale;
			ShadowRenderRequest.SubViewIndex = i;
			ShadowRenderRequest.AtlasScaleOffset = 0;
			OutRequests.Add(ShadowRenderRequest);
		}
	}
}

FVector UDirectionalLightComponent::GetLightDirection() const
{
	// Z-Up Left-handed 좌표계에서 Forward는 X축
	FQuat Rotation = GetWorldRotation();
	return Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
}

FDirectionalLightInfo UDirectionalLightComponent::GetLightInfo() const
{
	FDirectionalLightInfo Info;
	// Use GetLightColorWithIntensity() to include Temperature + Intensity
	Info.Color = GetLightColorWithIntensity();
	Info.Direction = GetLightDirection();
	Info.bCastShadows = bCastShadows ? 1 : 0;

	Info.bCascaded = bCascaded ? 1 : 0;
	Info.CascadeCount = CascadedCount;
	Info.CascadedOverlapValue = CascadedOverlapValue;
	Info.CascadedAreaColorDebugValue = CascadedAreaColorDebugValue;
	Info.CascadedAreaShadowDebugValue = CascadedAreaShadowDebugValue;
	for (int i = 0; i < CascadedSliceDepth.Num(); i++)
	{
		Info.CascadedSliceDepth[i] = CascadedSliceDepth[i];
	}
	return Info;
}

void UDirectionalLightComponent::OnRegister(UWorld* InWorld)
{
	Super::OnRegister(InWorld);

	if (SpriteComponent)
	{
		SpriteComponent->SetTexture(GDataDir + "/UI/Icons/S_LightDirectional.dds");
	}

	// Create Direction Gizmo if not already created
	if (!DirectionGizmo && !InWorld->bPie)
	{
		CREATE_EDITOR_COMPONENT(DirectionGizmo, UGizmoArrowComponent);

		// Set gizmo mesh (using the same mesh as GizmoActor's arrow)
		DirectionGizmo->SetStaticMesh(GDataDir + "/Gizmo/TranslationHandle.obj");
		DirectionGizmo->SetMaterialByName(0, "Shaders/UI/Gizmo.hlsl");

		// Use world-space scale (not screen-constant scale like typical gizmos)
		DirectionGizmo->SetUseScreenConstantScale(false);

		// Set default scale
		DirectionGizmo->SetDefaultScale(FVector(2.5f, 2.5f, 2.5f));

		// Update gizmo properties to match light
		UpdateDirectionGizmo();
	}

    if (InWorld && InWorld->GetLightManager())
    {
        InWorld->GetLightManager()->RegisterLight(this);
    }
}

void UDirectionalLightComponent::OnUnregister()
{
    if (UWorld* World = GetWorld())
    {
        if (World->GetLightManager())
        {
            World->GetLightManager()->DeRegisterLight(this);
        }
    }
}

void UDirectionalLightComponent::UpdateLightData()
{
    Super::UpdateLightData();
    // 방향성 라이트 특화 업데이트 로직
    if (UWorld* World = GetWorld())
    {
        if (World->GetLightManager())
        {
            World->GetLightManager()->UpdateLight(this);
        }
    }
    // Update direction gizmo to reflect any changes
    UpdateDirectionGizmo();
}

void UDirectionalLightComponent::OnTransformUpdated()
{
    Super::OnTransformUpdated();
    if (UWorld* World = GetWorld())
    {
        if (World->GetLightManager())
        {
            World->GetLightManager()->UpdateLight(this);
        }
    }
}

void UDirectionalLightComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

}

void UDirectionalLightComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
	DirectionGizmo = nullptr;
}

void UDirectionalLightComponent::UpdateDirectionGizmo()
{
	if (!DirectionGizmo)
		return;

	// Set direction to match light direction (used for hovering/picking, not for rendering)
	FVector LightDir = GetLightDirection();
	DirectionGizmo->SetDirection(LightDir);

	// Set color to match base light color (without intensity or temperature multipliers)
	const FLinearColor& BaseColor = GetLightColor();
	DirectionGizmo->SetColor(FVector(BaseColor.R, BaseColor.G, BaseColor.B));

	// DirectionGizmo is a child of DirectionalLightComponent, so it inherits the parent's rotation automatically.
	// Arrow mesh points along +X axis by default, which matches the DirectionalLight's forward direction.
	// No need to set RelativeRotation - it should remain identity (0, 0, 0)
}
