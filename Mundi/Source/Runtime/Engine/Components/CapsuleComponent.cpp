#include "pch.h"
#include "CapsuleComponent.h"
// IMPLEMENT_CLASS is now auto-generated in .generated.cpp
//BEGIN_PROPERTIES(UCapsuleComponent)
//MARK_AS_COMPONENT("캡슐 충돌 컴포넌트", "캡슐 모양의 충돌체를 생성하는 컴포넌트입니다.")
//ADD_PROPERTY(float , CapsuleHalfHeight, "CapsuleHalfHeight", true, "박스 충돌체의 크기입니다.")
//ADD_PROPERTY(float , CapsuleRadius, "CapsuleHalfHeight", true, "박스 충돌체의 크기입니다.")
//END_PROPERTIES()

UCapsuleComponent::UCapsuleComponent()
{
    CapsuleHalfHeight = 0.5f;
    CapsuleRadius = 0.5f;
}

void UCapsuleComponent::OnRegister(UWorld* World)
{
    Super::OnRegister(World);

    if (AActor* Owner = GetOwner())
    {
        FAABB ActorBounds = Owner->GetBounds();
        FVector WorldHalfExtent = ActorBounds.GetHalfExtent();

        // Owner의 bounds가 유효한 경우에만 캡슐 크기를 재계산
        // Character 같은 경우 GetBounds()가 빈 AABB를 반환하므로 기존 값 유지
        if (!WorldHalfExtent.IsZero())
        {
            // World scale로 나눠서 local 값 계산
            const FTransform WorldTransform = GetWorldTransform();
            const FVector S = FVector(
                std::fabs(WorldTransform.Scale3D.X),
                std::fabs(WorldTransform.Scale3D.Y),
                std::fabs(WorldTransform.Scale3D.Z)
            );

            constexpr float Eps = 1e-6f;

            // Z축 = 높이, XY축 = 반지름
            float LocalHalfHeight = S.Z > Eps ? WorldHalfExtent.Z / S.Z : WorldHalfExtent.Z;
            float LocalRadiusX = S.X > Eps ? WorldHalfExtent.X / S.X : WorldHalfExtent.X;
            float LocalRadiusY = S.Y > Eps ? WorldHalfExtent.Y / S.Y : WorldHalfExtent.Y;

            CapsuleHalfHeight = LocalHalfHeight;
            CapsuleRadius = FMath::Max(LocalRadiusX, LocalRadiusY);
        }
    }
}

void UCapsuleComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
}

void UCapsuleComponent::GetShape(FShape& Out) const
{
	Out.Kind = EShapeKind::Capsule;
	Out.Capsule.CapsuleHalfHeight = CapsuleHalfHeight;
	Out.Capsule.CapsuleRadius = CapsuleRadius;
}

FAABB UCapsuleComponent::GetWorldAABB() const
{
    const FTransform WorldTransform = GetWorldTransform();
    const FVector Center = WorldTransform.Translation;
    const FQuat Rotation = WorldTransform.Rotation;
    const FVector Scale3D = WorldTransform.Scale3D;

    // 1. 월드 스케일 절댓값 (음수 스케일 대응)
    const float AbsScaleX = std::fabs(Scale3D.X);
    const float AbsScaleY = std::fabs(Scale3D.Y);
    const float AbsScaleZ = std::fabs(Scale3D.Z);

    // 2. 월드 공간에서의 반지름과 높이 계산
    // 캡슐의 반지름은 XY 스케일 중 큰 값을 따르고, 높이는 Z축 스케일을 따름
    const float WorldRadius = CapsuleRadius * FMath::Max(AbsScaleX, AbsScaleY);
    const float WorldHalfHeight = CapsuleHalfHeight * AbsScaleZ;

    // 3. 캡슐 내부의 '중심 선분' 길이 계산
    // 캡슐은 [0, 0, -Len] ~ [0, 0, +Len] 선분에 반지름 R을 더한 모양
    // HalfHeight가 Radius보다 작으면 그냥 구체(Sphere)로 취급 (Max(0, ...))
    const float CylinderHalfLen = FMath::Max(0.0f, WorldHalfHeight - WorldRadius);

    // 4. 회전된 Z축(Up Vector) 구하기
    // 로컬 Z축(0,0,1)을 월드 회전으로 변환
    const FVector AxisZ = Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));

    // 5. 중심 선분의 위/아래 끝점 계산 (Center 기준)
    const FVector TopPoint = Center + (AxisZ * CylinderHalfLen);
    const FVector BottomPoint = Center - (AxisZ * CylinderHalfLen);

    // 6. 선분의 AABB(Min/Max) 구하기
    
    FVector Min(
        std::min({ TopPoint.X, BottomPoint.X }),
        std::min({ TopPoint.Y, BottomPoint.Y }),
        std::min({ TopPoint.Z, BottomPoint.Z })
    );
    FVector Max(
        std::max({ TopPoint.X, BottomPoint.X }),
        std::max({ TopPoint.Y, BottomPoint.Y }),
        std::max({ TopPoint.Z, BottomPoint.Z })
    );

    // 7. AABB를 반지름만큼 모든 축 방향으로 확장
    const FVector RadiusExtent(WorldRadius, WorldRadius, WorldRadius);

    return FAABB(Min - RadiusExtent, Max + RadiusExtent);
}

void UCapsuleComponent::RenderDebugVolume(URenderer* Renderer) const
{
    if (!bShapeIsVisible) return;
    if (GWorld->bPie)
    {
        if (bShapeHiddenInGame)
            return;
    }

    const FTransform WorldTransform = GetWorldTransform();
    const FVector Scale3D = WorldTransform.Scale3D;
    const float AbsScaleX = std::fabs(Scale3D.X);
    const float AbsScaleY = std::fabs(Scale3D.Y);
    const float AbsScaleZ = std::fabs(Scale3D.Z);

    // 월드 치수 계산
    const float WorldRadius = CapsuleRadius * FMath::Max(AbsScaleX, AbsScaleY);
    const float WorldHalfHeight = CapsuleHalfHeight * AbsScaleZ;
    
    // 계산된 월드 치수 사용
    const float Radius = WorldRadius;
    const float HalfHeightAABB = WorldHalfHeight;
    const float HalfHeightCylinder = FMath::Max(0.0f, HalfHeightAABB - Radius);

    // 위치, 회전 변환만 가져와서 사용, Scale은 사용자가 조정 
    const FMatrix WorldNoScale = FMatrix::FromTRS(GetWorldLocation(), 
        GetWorldRotation(), FVector(1.0f, 1.0f, 1.0f));
     
    const int NumOfSphereSlice = 4;
    const int NumHemisphereSegments = 8; 

    TArray<FVector> StartPoints;
    TArray<FVector> EndPoints;
    TArray<FVector4> Colors;

    TArray<FVector> TopRingLocal;
    TArray<FVector> BottomRingLocal;
    TopRingLocal.Reserve(NumOfSphereSlice);
    BottomRingLocal.Reserve(NumOfSphereSlice);

     
    //윗면 아랫면 
    for (int i = 0; i < NumOfSphereSlice; ++i)
    {
        const float a0 = (static_cast<float>(i) / NumOfSphereSlice) * TWO_PI;
        const float x = Radius * std::sin(a0);
        const float y = Radius * std::cos(a0);
        TopRingLocal.Add(FVector(x, y, +HalfHeightCylinder));
        BottomRingLocal.Add(FVector(x, y, -HalfHeightCylinder));
    }
     
    for (int i = 0; i < NumOfSphereSlice; ++i)
    {
        const int j = (i + 1) % NumOfSphereSlice;

        //윗면
        StartPoints.Add(TopRingLocal[i] * WorldNoScale);
        EndPoints.Add(TopRingLocal[j] * WorldNoScale);
        Colors.Add(ShapeColor);

        // 아랫면
        StartPoints.Add(BottomRingLocal[i] * WorldNoScale);
        EndPoints.Add(BottomRingLocal[j] * WorldNoScale);
        Colors.Add(ShapeColor);
    }
     
    //윗면 아랫면 잇는 선분
    for (int i = 0; i < NumOfSphereSlice; ++i)
    {
        StartPoints.Add(TopRingLocal[i] * WorldNoScale);
        EndPoints.Add(BottomRingLocal[i] * WorldNoScale);
        Colors.Add(ShapeColor);
    }
    
    // 반구 위아래 
    auto AddHemisphereArcs = [&](float CenterZSign)
    {
        const float CenterZ = CenterZSign * HalfHeightCylinder;

        for (int i = 0; i < NumHemisphereSegments; ++i)
        {
            const float t0 = (static_cast<float>(i) / NumHemisphereSegments) * PI;
            const float t1 = (static_cast<float>(i + 1) / NumHemisphereSegments) * PI;
    
            FVector PlaneXZ0(Radius * std::cos(t0), 0.0f, CenterZ + CenterZSign* Radius * std::sin(t0));
            FVector PlaneXZ1(Radius * std::cos(t1), 0.0f, CenterZ + CenterZSign* Radius * std::sin(t1));
            
            StartPoints.Add(PlaneXZ0 * WorldNoScale);
            EndPoints.Add(PlaneXZ1 * WorldNoScale);
            Colors.Add(ShapeColor);
            
            FVector PlaneYZ0(0.0f, Radius * std::cos(t0), CenterZ + CenterZSign * Radius * std::sin(t0));
            FVector PlaneYZ1(0.0f, Radius * std::cos(t1), CenterZ + CenterZSign * Radius * std::sin(t1));

            StartPoints.Add(PlaneYZ0 * WorldNoScale);
            EndPoints.Add(PlaneYZ1 * WorldNoScale);
            Colors.Add(ShapeColor);
        }
    };
     
    AddHemisphereArcs(+1.0f);
    AddHemisphereArcs(-1.0f);

    Renderer->AddLines(StartPoints, EndPoints, Colors);
}
