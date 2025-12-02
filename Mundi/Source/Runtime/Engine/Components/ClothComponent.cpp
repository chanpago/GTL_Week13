#include "pch.h"
#include "ClothComponent.h"
#include "Source/Runtime/Engine/Physics/ClothSimulationSystem.h"

// NvCloth includes
#include <NvCloth/Factory.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Cloth.h>
#include <NvCloth/Solver.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <NvClothExt/ClothMeshDesc.h>
#include <foundation/PxVec3.h>
#include <foundation/PxVec4.h>

// ──────────────────────────────
// Constructor / Destructor
// ──────────────────────────────

UClothComponent::UClothComponent()
{
    bCanEverTick = false;  // World에서 시뮬레이션 처리
    bTickEnabled = false;
}

UClothComponent::~UClothComponent()
{
    DestroyCloth();
}

// ──────────────────────────────
// Lifecycle
// ──────────────────────────────

void UClothComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UClothComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);
    // 시뮬레이션은 World의 ClothSimulationSystem에서 처리
}

void UClothComponent::EndPlay()
{
    DestroyCloth();
    Super::EndPlay();
}

void UClothComponent::OnRegister(UWorld* InWorld)
{
    Super::OnRegister(InWorld);
}

void UClothComponent::OnUnregister()
{
    DestroyCloth();
    Super::OnUnregister();
}

void UClothComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    // TODO: ClothConfig 직렬화
}

// ──────────────────────────────
// Helper: ClothSimulationSystem 접근
// ──────────────────────────────

FClothSimulationSystem* UClothComponent::GetClothSystem() const
{
    UWorld* World = const_cast<UClothComponent*>(this)->GetWorld();
    if (World)
    {
        return World->GetClothSimulationSystem();
    }
    return nullptr;
}

// ──────────────────────────────
// Cloth Setup
// ──────────────────────────────

bool UClothComponent::CreateClothFromMesh(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    const TArray<float>& InvMasses)
{
    // 기존 Cloth 제거
    DestroyCloth();

    if (Vertices.Num() < 3 || Indices.Num() < 3)
    {
        return false;
    }

    // ClothSimulationSystem에서 Factory 가져오기
    FClothSimulationSystem* ClothSystem = GetClothSystem();
    if (ClothSystem == nullptr || !ClothSystem->IsInitialized())
    {
        return false;
    }

    nv::cloth::Factory* Factory = ClothSystem->GetFactory();
    if (Factory == nullptr)
    {
        return false;
    }

    // 초기 데이터 저장
    InitialPositions = Vertices;
    InitialInvMasses = InvMasses;

    // ClothMeshDesc 설정
    nv::cloth::ClothMeshDesc MeshDesc;

    // 정점 데이터 (PxVec3로 변환)
    TArray<physx::PxVec3> PxVertices;
    PxVertices.SetNum(Vertices.Num());
    for (int32 i = 0; i < Vertices.Num(); i++)
    {
        PxVertices[i] = physx::PxVec3(Vertices[i].X, Vertices[i].Y, Vertices[i].Z);
    }
    MeshDesc.points.data = PxVertices.GetData();
    MeshDesc.points.count = static_cast<physx::PxU32>(PxVertices.Num());
    MeshDesc.points.stride = sizeof(physx::PxVec3);

    // 인덱스 데이터
    MeshDesc.triangles.data = Indices.GetData();
    MeshDesc.triangles.count = static_cast<physx::PxU32>(Indices.Num() / 3);
    MeshDesc.triangles.stride = sizeof(uint32) * 3;

    // 역질량 데이터
    TArray<float> InvMassData;
    if (InvMasses.Num() == Vertices.Num())
    {
        InvMassData = InvMasses;
    }
    else
    {
        // 기본값: 모든 파티클 시뮬레이션
        InvMassData.SetNum(Vertices.Num());
        for (int32 i = 0; i < Vertices.Num(); i++)
        {
            InvMassData[i] = 1.0f;
        }
    }
    MeshDesc.invMasses.data = InvMassData.GetData();
    MeshDesc.invMasses.count = static_cast<physx::PxU32>(InvMassData.Num());
    MeshDesc.invMasses.stride = sizeof(float);

    // Fabric 쿠킹
    physx::PxVec3 GravityDir(
        ClothConfig.Gravity.X,
        ClothConfig.Gravity.Y,
        ClothConfig.Gravity.Z
    );
    GravityDir.normalize();

    Fabric = NvClothCookFabricFromMesh(Factory, MeshDesc, GravityDir, nullptr, true);
    if (Fabric == nullptr)
    {
        return false;
    }

    // 초기 파티클 설정 (xyz = 위치, w = 역질량)
    TArray<physx::PxVec4> Particles;
    Particles.SetNum(Vertices.Num());
    for (int32 i = 0; i < Vertices.Num(); i++)
    {
        Particles[i] = physx::PxVec4(
            Vertices[i].X,
            Vertices[i].Y,
            Vertices[i].Z,
            InvMassData[i]
        );
    }

    // Cloth 생성
    Cloth = Factory->createCloth(
        nv::cloth::Range<physx::PxVec4>(Particles.GetData(), Particles.GetData() + Particles.Num()),
        *Fabric
    );

    if (Cloth == nullptr)
    {
        Fabric->decRefCount();
        Fabric = nullptr;
        return false;
    }

    // 기본 설정 적용
    ApplyClothConfig(ClothConfig);

    // ClothSimulationSystem에 등록
    ClothSystem->RegisterCloth(Cloth);

    bClothInitialized = true;
    return true;
}

void UClothComponent::DestroyCloth()
{
    if (Cloth != nullptr)
    {
        // ClothSimulationSystem에서 해제
        FClothSimulationSystem* ClothSystem = GetClothSystem();
        if (ClothSystem != nullptr)
        {
            ClothSystem->UnregisterCloth(Cloth);
        }

        delete Cloth;
        Cloth = nullptr;
    }

    if (Fabric != nullptr)
    {
        Fabric->decRefCount();
        Fabric = nullptr;
    }

    bClothInitialized = false;

    InitialPositions.Empty();
    InitialInvMasses.Empty();
    CollisionSpheres.Empty();
    CollisionRadii.Empty();
    CollisionCapsuleIndices.Empty();
}

// ──────────────────────────────
// Simulation Parameters
// ──────────────────────────────

void UClothComponent::ApplyClothConfig(const FClothConfig& Config)
{
    ClothConfig = Config;

    if (Cloth == nullptr)
    {
        return;
    }

    // 중력
    Cloth->setGravity(physx::PxVec3(Config.Gravity.X, Config.Gravity.Y, Config.Gravity.Z));

    // 댐핑
    Cloth->setDamping(physx::PxVec3(Config.Damping.X, Config.Damping.Y, Config.Damping.Z));

    // Solver 주파수
    Cloth->setSolverFrequency(Config.SolverFrequency);

    // 바람
    Cloth->setWindVelocity(physx::PxVec3(Config.WindVelocity.X, Config.WindVelocity.Y, Config.WindVelocity.Z));
    Cloth->setDragCoefficient(Config.DragCoefficient);
    Cloth->setLiftCoefficient(Config.LiftCoefficient);

    // 마찰
    Cloth->setFriction(Config.Friction);

    // Self Collision
    Cloth->setSelfCollisionDistance(Config.SelfCollisionDistance);
    Cloth->setSelfCollisionStiffness(Config.SelfCollisionStiffness);

    // Tether
    Cloth->setTetherConstraintScale(Config.TetherConstraintScale);
    Cloth->setTetherConstraintStiffness(Config.TetherConstraintStiffness);

    // 관성
    Cloth->setLinearInertia(physx::PxVec3(Config.LinearInertia.X, Config.LinearInertia.Y, Config.LinearInertia.Z));
    Cloth->setAngularInertia(physx::PxVec3(Config.AngularInertia.X, Config.AngularInertia.Y, Config.AngularInertia.Z));
    Cloth->setCentrifugalInertia(physx::PxVec3(Config.CentrifugalInertia.X, Config.CentrifugalInertia.Y, Config.CentrifugalInertia.Z));
}

void UClothComponent::SetGravity(const FVector& InGravity)
{
    ClothConfig.Gravity = InGravity;
    if (Cloth != nullptr)
    {
        Cloth->setGravity(physx::PxVec3(InGravity.X, InGravity.Y, InGravity.Z));
    }
}

void UClothComponent::SetWind(const FVector& InWindVelocity, float InDragCoefficient, float InLiftCoefficient)
{
    ClothConfig.WindVelocity = InWindVelocity;
    ClothConfig.DragCoefficient = InDragCoefficient;
    ClothConfig.LiftCoefficient = InLiftCoefficient;

    if (Cloth != nullptr)
    {
        Cloth->setWindVelocity(physx::PxVec3(InWindVelocity.X, InWindVelocity.Y, InWindVelocity.Z));
        Cloth->setDragCoefficient(InDragCoefficient);
        Cloth->setLiftCoefficient(InLiftCoefficient);
    }
}

void UClothComponent::SetDamping(const FVector& InDamping)
{
    ClothConfig.Damping = InDamping;
    if (Cloth != nullptr)
    {
        Cloth->setDamping(physx::PxVec3(InDamping.X, InDamping.Y, InDamping.Z));
    }
}

// ──────────────────────────────
// Runtime State
// ──────────────────────────────

void UClothComponent::GetSimulatedPositions(TArray<FVector>& OutPositions) const
{
    OutPositions.Empty();

    if (Cloth == nullptr)
    {
        return;
    }

    auto Particles = Cloth->getCurrentParticles();
    uint32 NumParticles = Cloth->getNumParticles();

    OutPositions.SetNum(NumParticles);
    for (uint32 i = 0; i < NumParticles; i++)
    {
        const physx::PxVec4& P = Particles[i];
        OutPositions[i] = FVector(P.x, P.y, P.z);
    }
}

uint32 UClothComponent::GetNumParticles() const
{
    return Cloth != nullptr ? Cloth->getNumParticles() : 0;
}

void UClothComponent::UpdateClothTransform(const FVector& Translation, const FQuat& Rotation)
{
    if (Cloth == nullptr)
    {
        return;
    }

    CachedTranslation = Translation;
    CachedRotation = Rotation;

    Cloth->setTranslation(physx::PxVec3(Translation.X, Translation.Y, Translation.Z));
    Cloth->setRotation(physx::PxQuat(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W));
}

// ──────────────────────────────
// Collision
// ──────────────────────────────

void UClothComponent::AddCollisionSphere(const FVector& Center, float Radius)
{
    if (Cloth == nullptr)
    {
        return;
    }

    CollisionSpheres.Add(Center);
    CollisionRadii.Add(Radius);

    // NvCloth에 적용
    TArray<physx::PxVec4> Spheres;
    Spheres.SetNum(CollisionSpheres.Num());
    for (int32 i = 0; i < CollisionSpheres.Num(); i++)
    {
        const FVector& C = CollisionSpheres[i];
        Spheres[i] = physx::PxVec4(C.X, C.Y, C.Z, CollisionRadii[i]);
    }

    Cloth->setSpheres(
        nv::cloth::Range<const physx::PxVec4>(Spheres.GetData(), Spheres.GetData() + Spheres.Num()),
        0,
        static_cast<uint32_t>(Spheres.Num())
    );
}

void UClothComponent::AddCollisionCapsule(uint32 SphereIndex0, uint32 SphereIndex1)
{
    if (Cloth == nullptr || SphereIndex0 >= static_cast<uint32>(CollisionSpheres.Num())
        || SphereIndex1 >= static_cast<uint32>(CollisionSpheres.Num()))
    {
        return;
    }

    CollisionCapsuleIndices.Add(SphereIndex0);
    CollisionCapsuleIndices.Add(SphereIndex1);

    Cloth->setCapsules(
        nv::cloth::Range<const uint32_t>(CollisionCapsuleIndices.GetData(), CollisionCapsuleIndices.GetData() + CollisionCapsuleIndices.Num()),
        0,
        static_cast<uint32_t>(CollisionCapsuleIndices.Num() / 2)
    );
}

void UClothComponent::ClearCollision()
{
    CollisionSpheres.Empty();
    CollisionRadii.Empty();
    CollisionCapsuleIndices.Empty();

    if (Cloth != nullptr)
    {
        // 빈 배열로 설정하여 모든 충돌체 제거
        Cloth->setSpheres(nv::cloth::Range<const physx::PxVec4>(), 0, Cloth->getNumSpheres());
        Cloth->setCapsules(nv::cloth::Range<const uint32_t>(), 0, Cloth->getNumCapsules());
    }
}
