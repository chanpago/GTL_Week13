#pragma once
#include "PxPhysicsAPI.h"

struct FKShapeElem
{
    EAggCollisionShapeType ShapeType = EAggCollisionShapeType::Unknown;

    EAggCollisionShapeType GetShapeType() const { return ShapeType; }
};

struct FKSphereElem
{
    FVector Center = FVector::Zero();
    float   Radius = 50.0f;

    // 디버그 / 에디터용 색상, 아이디 등 필요하면 추가
};

struct FKBoxElem
{
    FVector Center = FVector::Zero();
    FVector Extents = FVector(50.0f, 50.0f, 50.0f); // half size
    FQuat   Rotation = FQuat::Identity();
};

// 캡슐은 특별함. PxCapsuleGeometry는 hieght를 x축으로 뻗는 걸로 생각한다. 캡슐을 생성할 때 이걸 지키면서하기보다는 우리는 z축으로 뻗는 캡슐을 보통 생각하니까.
// 축변환을 해줘야함
struct FKCapsuleElem // Capsule
{
    FVector Center = FVector::Zero();
    float   Radius = 30.0f;
    float   HalfLength = 50.0f;
    FQuat   Rotation = FQuat::Identity();
};

// TODO : 추후 구현
struct FKConvexElem
{
    // Convex Hull 데이터
    TArray<FVector> Vertices;

    // Serialized PhysX convex mesh
    TArray<uint8> CookedData;

    // Runtime convex mesh
    physx::PxConvexMesh* ConvexMesh = nullptr;
};