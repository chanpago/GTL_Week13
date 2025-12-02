#pragma once
struct FAABB;
struct FOBB;
struct FBoundingSphere;
struct FShape;
struct FColliderProxy;

class UShapeComponent;

class AActor;
class UPrimitiveComponent;

struct FHitResult
{
    bool bHit = false;                          // 충돌했는가
    bool bBlockingHit = false;                  // 이동을 막는 충돌인가
    float Time = 1.0f;                          // Sweep 시 충돌 시간 (0~1, 1이면 충돌 없음)
    float Distance = 0.0f;                      // 충돌까지의 거리
    float PenetrationDepth = 0.0f;              // 얼마나 파고들었는가
    FVector Location = FVector::Zero();         // Sweep 충돌 시 Shape의 위치
    FVector ImpactPoint = FVector::Zero();      // 충돌 지점
    FVector ImpactNormal{0, 0, 1};              // 밀어내야 할 방향
    FVector TraceStart = FVector::Zero();       // Sweep 시작점
    FVector TraceEnd = FVector::Zero();         // Sweep 끝점
    AActor* HitActor = nullptr;                 // 충돌한 액터
    UPrimitiveComponent* HitComponent = nullptr; // 충돌한 컴포넌트

    void Reset()
    {
        bHit = false;
        bBlockingHit = false;
        Time = 1.0f;
        Distance = 0.0f;
        PenetrationDepth = 0.0f;
        Location = FVector::Zero();
        ImpactPoint = FVector::Zero();
        ImpactNormal = {0, 0, 1};
        TraceStart = FVector::Zero();
        TraceEnd = FVector::Zero();
        HitActor = nullptr;
        HitComponent = nullptr;
    }
};

namespace Collision
{
    bool Intersects(const FAABB& Aabb, const FOBB& Obb);

    bool Intersects(const FAABB& Aabb, const FBoundingSphere& Sphere);

	bool Intersects(const FOBB& Obb, const FBoundingSphere& Sphere);

    // ㅡㅡㅡㅡㅡㅡㅡㅡㅡㅡShapeComponent Helper 함수ㅡㅡㅡㅡㅡㅡㅡㅡㅡㅡ
    FVector AbsVec(const FVector& v);
    float UniformScaleMax(const FVector& S);

    void BuildOBB(const FShape& BoxShape, const FTransform& T, FOBB& Out);
    bool Overlap_OBB_OBB(const FOBB& A, const FOBB& B);
    bool Overlap_Sphere_OBB(const FVector& Center, float Radius, const FOBB& B);

    bool OverlapSphereAndSphere(const FShape& ShapeA, const FTransform& TransformA, const FShape& ShapeB, const FTransform& TransformB);
    
    void BuildCapsule(const FShape& CapsuleShape, const FTransform& TransformCapsule, FVector& OutP0, FVector& OutP1, float& OutRadius);
    void BuildCapsuleCoreOBB(const FShape& CapsuleShape, const FTransform& Transform, FOBB& Out);

    bool OverlapCapsuleAndSphere(const FShape& Capsule, const FTransform& TransformCapsule, const FShape& Sphere, const FTransform& TransformSphere);

    bool OverlapCapsuleAndBox(const FShape& Capsule, const FTransform& TransformCapsule, const FShape& Box, const FTransform& TransformBox);

    bool OverlapCapsuleAndCapsule(const FShape& CapsuleA, const FTransform& TransformA, const FShape& CapsuleB, const FTransform& TransformB);
       
    using OverlapFunc = bool(*) (const FShape&, const FTransform&, const FShape&, const FTransform&);

    extern OverlapFunc OverlapLUT[3][3];
    
    
    bool CheckOverlap(const UShapeComponent* A, const UShapeComponent* B);
    
    // ㅡㅡㅡㅡㅡㅡㅡㅡㅡㅡParticle Collision Helper 함수ㅡㅡㅡㅡㅡㅡㅡㅡㅡㅡ
    bool ComputeSphereToShapePenetration(const FVector& SpherePos, float SphereR, const FColliderProxy& Proxy, FHitResult& OutHit);
    bool ComputeSphereToBoxPenetration(const FVector& SpherePos, float SphereR, const FOBB& Box, FHitResult& OutHit);
    bool ComputeSphereToCapsulePenetration(const FVector& SpherePos, float SphereR, const FVector& PosA, const FVector& PosB, float CapsuleR, FHitResult& OutHit);
    bool ComputeSphereToSpherePenetration(const FVector& SpherePos, float SphereR, const FVector& TargetCenter, float TargetR, FHitResult& OutHit);
    
}