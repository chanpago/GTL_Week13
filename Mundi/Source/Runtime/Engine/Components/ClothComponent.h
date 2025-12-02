#pragma once

#include "SceneComponent.h"
#include "UClothComponent.generated.h"

// Forward declarations - NvCloth
namespace nv { namespace cloth {
    class Fabric;
    class Cloth;
}}

class FClothSimulationSystem;

/**
 * @brief Cloth 시뮬레이션을 위한 컴포넌트
 *
 * NvCloth 라이브러리를 사용하여 옷감 시뮬레이션을 수행합니다.
 * SkeletalMeshComponent에 연결하여 사용하거나, 독립적으로 메쉬 데이터를 설정할 수 있습니다.
 */
UCLASS(DisplayName="클로스 컴포넌트", Description="옷감 시뮬레이션을 수행하는 컴포넌트입니다")
class UClothComponent : public USceneComponent
{
public:
    GENERATED_REFLECTION_BODY()

    UClothComponent();

protected:
    ~UClothComponent() override;

public:
    // Lifecycle
    void BeginPlay() override;
    void TickComponent(float DeltaTime) override;
    void EndPlay() override;

    // Registration
    void OnRegister(UWorld* InWorld) override;
    void OnUnregister() override;

    // Serialization
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

    // ──────────────────────────────
    // Cloth Setup API
    // ──────────────────────────────

    /**
     * @brief 메쉬 데이터로부터 Cloth를 생성합니다.
     * @param Vertices 정점 위치 배열
     * @param Indices 삼각형 인덱스 배열 (3개씩)
     * @param InvMasses 역질량 배열 (0 = 고정, >0 = 시뮬레이션)
     * @return 성공 여부
     */
    bool CreateClothFromMesh(
        const TArray<FVector>& Vertices,
        const TArray<uint32>& Indices,
        const TArray<float>& InvMasses
    );

    /**
     * @brief 기존 Cloth를 제거합니다.
     */
    void DestroyCloth();

    /**
     * @brief Cloth가 유효한지 확인합니다.
     */
    bool IsClothValid() const { return Cloth != nullptr; }

    // ──────────────────────────────
    // Simulation Parameters
    // ──────────────────────────────

    /**
     * @brief 중력을 설정합니다.
     */
    void SetGravity(const FVector& InGravity);

    /**
     * @brief 바람을 설정합니다.
     */
    void SetWind(const FVector& InWindVelocity, float InDragCoefficient = 0.5f, float InLiftCoefficient = 0.3f);

    /**
     * @brief 댐핑을 설정합니다.
     */
    void SetDamping(const FVector& InDamping);

    /**
     * @brief 현재 멤버 변수 설정을 Cloth에 적용합니다.
     */
    void ApplyCurrentSettings();

    // ──────────────────────────────
    // Runtime State
    // ──────────────────────────────

    /**
     * @brief 시뮬레이션된 파티클 위치를 가져옵니다.
     * @param OutPositions 출력 위치 배열
     */
    void GetSimulatedPositions(TArray<FVector>& OutPositions) const;

    /**
     * @brief 파티클 개수를 가져옵니다.
     */
    uint32 GetNumParticles() const;

    /**
     * @brief 시뮬레이션을 일시정지/재개합니다.
     */
    void SetSimulationEnabled(bool bEnabled) { bSimulationEnabled = bEnabled; }
    bool IsSimulationEnabled() const { return bSimulationEnabled; }

    /**
     * @brief Cloth의 Transform을 업데이트합니다 (애니메이션 추적용).
     */
    void UpdateClothTransform(const FVector& Translation, const FQuat& Rotation);

    // ──────────────────────────────
    // Collision
    // ──────────────────────────────

    /**
     * @brief 충돌 구체를 추가합니다.
     * @param Center 구체 중심
     * @param Radius 구체 반지름
     */
    void AddCollisionSphere(const FVector& Center, float Radius);

    /**
     * @brief 충돌 캡슐을 추가합니다 (두 구체를 연결).
     * @param SphereIndex0 첫 번째 구체 인덱스
     * @param SphereIndex1 두 번째 구체 인덱스
     */
    void AddCollisionCapsule(uint32 SphereIndex0, uint32 SphereIndex1);

    /**
     * @brief 모든 충돌체를 제거합니다.
     */
    void ClearCollision();

protected:
    /**
     * @brief ClothSimulationSystem을 가져옵니다.
     */
    FClothSimulationSystem* GetClothSystem() const;

protected:
    // NvCloth 객체들
    nv::cloth::Fabric* Fabric = nullptr;
    nv::cloth::Cloth* Cloth = nullptr;

    // 초기 파티클 데이터 (리셋용)
    TArray<FVector> InitialPositions;
    TArray<float> InitialInvMasses;

    // 시뮬레이션 활성화
    UPROPERTY(EditAnywhere, Category="Cloth")
    bool bSimulationEnabled = true;

    // Physics
    UPROPERTY(EditAnywhere, Category="Cloth|Physics")
    FVector Gravity = FVector(0.0f, 0.0f, -980.0f);

    UPROPERTY(EditAnywhere, Category="Cloth|Physics")
    FVector Damping = FVector(0.2f, 0.2f, 0.2f);

    UPROPERTY(EditAnywhere, Category="Cloth|Physics")
    float SolverFrequency = 120.0f;

    // Wind
    UPROPERTY(EditAnywhere, Category="Cloth|Wind")
    FVector WindVelocity = FVector(0.0f, 0.0f, 0.0f);

    UPROPERTY(EditAnywhere, Category="Cloth|Wind")
    float DragCoefficient = 0.5f;

    UPROPERTY(EditAnywhere, Category="Cloth|Wind")
    float LiftCoefficient = 0.3f;

    // Collision
    UPROPERTY(EditAnywhere, Category="Cloth|Collision")
    float Friction = 0.5f;

    UPROPERTY(EditAnywhere, Category="Cloth|Collision")
    float SelfCollisionDistance = 0.0f;

    UPROPERTY(EditAnywhere, Category="Cloth|Collision")
    float SelfCollisionStiffness = 1.0f;

    // Constraints
    UPROPERTY(EditAnywhere, Category="Cloth|Constraints")
    float TetherConstraintScale = 1.0f;

    UPROPERTY(EditAnywhere, Category="Cloth|Constraints")
    float TetherConstraintStiffness = 1.0f;

    // Inertia
    UPROPERTY(EditAnywhere, Category="Cloth|Inertia")
    FVector LinearInertia = FVector(1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category="Cloth|Inertia")
    FVector AngularInertia = FVector(1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category="Cloth|Inertia")
    FVector CentrifugalInertia = FVector(1.0f, 1.0f, 1.0f);

    // 충돌 데이터
    TArray<FVector> CollisionSpheres;
    TArray<float> CollisionRadii;
    TArray<uint32> CollisionCapsuleIndices;

    // 캐시된 Transform
    FVector CachedTranslation;
    FQuat CachedRotation;

    // 내부 상태
    bool bClothInitialized = false;
};
