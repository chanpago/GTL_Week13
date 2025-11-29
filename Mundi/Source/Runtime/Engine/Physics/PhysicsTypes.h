#pragma once

#include <PxPhysicsAPI.h>
#include <DirectXMath.h>

using namespace DirectX;   // 이건 그나마 괜찮

struct FConstraintLimitData
{
    float TwistMin;   // rad, 예: -Pi/4
    float TwistMax;   // rad, 예:  Pi/4
    float Swing1;     // rad, 예:  Pi/6
    float Swing2;     // rad, 예:  Pi/6

    bool  bSoftLimit = false;
    float Stiffness  = 0.0f;
    float Damping    = 0.0f;

    // 이 플래그가 있으면 관절로 연결된 두 바디는 서로 충돌하지 않게 만듦
    bool  bEnableCollision = false;   
};

// FVector -> PxVec3
inline physx::PxVec3 ToPx(const FVector& v)
{
    return physx::PxVec3(v.X, v.Y, v.Z); // 지금은 엔진과 축 동일하게 사용
}

// PxVec3 -> FVector
inline FVector FromPx(const physx::PxVec3& v)
{
    return FVector(v.x, v.y, v.z);
}

// FQuat -> PxQuat
inline physx::PxQuat ToPx(const FQuat& q)
{
    return physx::PxQuat(q.X, q.Y, q.Z, q.W);
}

inline FQuat FromPx(const physx::PxQuat& q)
{
    return FQuat(q.x, q.y, q.z, q.w);
}

// FTransform -> PxTransform
inline physx::PxTransform ToPx(const FTransform& t)
{
    return physx::PxTransform(
        ToPx(t.Translation),
        ToPx(t.Rotation)
    );
}

inline FTransform FromPx(const physx::PxTransform& t)
{
    return FTransform(FromPx(t.p), FromPx(t.q), FVector(1, 1, 1));
}

static FQuat FromAxisAngle(const FVector& Axis, float AngleRad)
{
    FVector N = Axis.GetNormalized();      // 단위벡터
    float half = AngleRad * 0.5f;
    float s = sinf(half);
    float c = cosf(half);

    return FQuat(N.X * s,N.Y * s,N.Z * s,c);
}

// Z축으로 뻗는 캡슐 → PhysX의 X축 캡슐로 보정
static FQuat GetZCapsuleToPhysXRotation()
{
    // 축 : (0,1,0) = Y축
    // 각도 : -90도 (라디안 단위)
    return FromAxisAngle(FVector(0, 1, 0), -XM_PIDIV2); // -PI/2
}
