#pragma once
#include <PxPhysicsAPI.h>
#include "Delegates.h"

struct FHitResult;

using namespace physx;
using namespace DirectX;

// ===== Vehicle Surface Types =====
// PhysX Vehicle SDK에서 사용하는 표면 타입 정의
enum ESurfaceType : PxU32
{
    SURFACE_TYPE_UNKNOWN = 0,
    SURFACE_TYPE_DRIVABLE = 1,       // 차량이 운전할 수 있는 표면 (도로, 지면 등)
    SURFACE_TYPE_UNDRIVABLE = 2,     // 차량이 운전할 수 없는 표면 (벽, 장애물 등)
    SURFACE_TYPE_VEHICLE = 4         // 차량 자체
};

// ===== Vehicle Filter Groups =====
// 차량과 일반 오브젝트를 구분하는 충돌 그룹
enum ECollisionGroup : PxU32
{
    COLLISION_GROUP_DEFAULT = 1,
    COLLISION_GROUP_VEHICLE_CHASSIS = 2,
    COLLISION_GROUP_VEHICLE_WHEEL = 4,
    COLLISION_GROUP_DRIVABLE_SURFACE = 8,
    COLLISION_GROUP_UNDRIVABLE_SURFACE = 16
};

// ===== Helper Functions for Surface Setup =====
void SetupDrivableSurface(PxFilterData& filterData);
void SetupUndrivableSurface(PxFilterData& filterData);
void SetupVehicleSurface(PxFilterData& filterData, bool bIsChassis = true);

// 차량 서스펜션 레이캐스트용 Pre-Filter 셰이더
PxQueryHitType::Enum VehicleWheelRaycastPreFilter(
    PxFilterData filterData0,
    PxFilterData filterData1,
    const void* constantBlock,
    PxU32 constantBlockSize,
    PxHitFlags& queryFlags);

// 물리 데이터 접근 시 Thread-Safe Lock 매크로
#define SCOPED_PHYSX_READ_LOCK(scene) PxSceneReadLock scopedReadLock(scene)
#define SCOPED_PHYSX_WRITE_LOCK(scene) PxSceneWriteLock scopedWriteLock(scene)

// PhysX Assert를 로그로 출력하는 커스텀 핸들러
class FPhysXAssertHandler : public PxAssertHandler
{
public:
    virtual void operator()(const char* exp, const char* file, int line, bool& ignore) override
    {
        UE_LOG("[PhysX ASSERT FAILED] Expression: %s", exp);
        UE_LOG("[PhysX ASSERT FAILED] File: %s, Line: %d", file, line);
        // ignore = true로 설정하면 assert를 무시하고 계속 진행
        // ignore = false면 기본 동작 (크래시)
        ignore = true;  // 일단 무시하고 계속 진행하도록
    }
};

class FSimulationEventCallback;

/**
 * @brief PhysX 공유 리소스 관리자
 *
 * Foundation, Physics, Dispatcher 등은 프로세스당 하나만 존재해야 하므로
 * static으로 관리하고 참조 카운트로 생성/해제를 제어합니다.
 */
class FPhysXSharedResources
{
public:
    static bool Initialize();
    static void Shutdown();
    static void AddRef();
    static void Release();

    static PxFoundation* GetFoundation() { return Foundation; }
    static PxPhysics* GetPhysics() { return Physics; }
    static PxPvd* GetPvd() { return Pvd; }
    static PxDefaultCpuDispatcher* GetDispatcher() { return Dispatcher; }
    static PxMaterial* GetDefaultMaterial() { return DefaultMaterial; }
    static bool IsInitialized() { return bInitialized; }

private:
    static PxDefaultAllocator Allocator;
    static PxDefaultErrorCallback ErrorCallback;
    static FPhysXAssertHandler AssertHandler;
    static PxFoundation* Foundation;
    static PxPvd* Pvd;
    static PxPvdTransport* PvdTransport;
    static PxPhysics* Physics;
    static PxDefaultCpuDispatcher* Dispatcher;
    static PxMaterial* DefaultMaterial;
    static int32 RefCount;
    static bool bInitialized;
};

class FPhysScene
{
public:
    struct GameObject
    {
        PxRigidDynamic* rigidBody = nullptr;
        XMMATRIX        worldMatrix = XMMatrixIdentity();

        //void UpdateFromPhysics();
    };

public:
    FPhysScene();
    ~FPhysScene();

    bool Initialize();         // PxScene 생성 (공유 리소스는 자동 초기화)
    void Shutdown();           // PxScene 해제

    /*
     * @brief physX 내부의 진짜 물리엔진 씬 = PxScene* Scene
     *          이걸 이용해서 매 프레임 Scene->simulate(dt) 한다
     *          PhysScene은 그저 통합 매니저이다.
     *
     *          simultate(dt) 호출 -> PhysX(PxScene)가 내부에 등록된
     *          모든 RigidActor/Shape를 가지고  충돌검사
     */
    void StepSimulation(float dt);                 // 매 프레임 시뮬레이션 (non-blocking)
    bool IsSimulationComplete() const;             // 시뮬레이션 완료 여부 확인
    void WaitForSimulation();                      // 시뮬레이션 완료 대기
    GameObject& CreateBox(const PxVec3& pos, const PxVec3& halfExtents); // 테스트용 박스 생성

    const std::vector<GameObject>& GetObjects() const;
    std::vector<GameObject>&       GetObjects();

    PxPhysics*              GetPhysics()         const;
    PxScene*                GetScene()           const;
    PxMaterial*             GetDefaultMaterial() const;
    PxDefaultCpuDispatcher* GetDispatcher()      const;

    // ===== Surface Setup for Vehicles =====
    /**
     * @brief 일반 지면/도로를 운전 가능한 표면으로 설정
     */
    void SetupActorAsDrivableSurface(PxRigidActor* actor);
    
    /**
     * @brief 벽/장애물을 운전 불가능한 표면으로 설정
     */
    void SetupActorAsUndrivableSurface(PxRigidActor* actor);

    // ===== Sweep Query =====
    /**
     * @brief 캡슐로 Sweep하여 Static 콜라이더와 충돌 검사
     * @param Start 시작 위치
     * @param End 끝 위치
     * @param Radius 캡슐 반지름
     * @param HalfHeight 캡슐 반높이
     * @param OutHit 충돌 결과
     * @param IgnoreActor 무시할 액터 (자기 자신 등)
     * @return 충돌 여부
     */
    bool SweepCapsule(
        const FVector& Start,
        const FVector& End,
        float Radius,
        float HalfHeight,
        FHitResult& OutHit,
        AActor* IgnoreActor = nullptr
    ) const;

    /**
     * @brief 박스로 Sweep하여 Static 콜라이더와 충돌 검사
     */
    bool SweepBox(
        const FVector& Start,
        const FVector& End,
        const FVector& HalfExtents,
        const FQuat& Rotation,
        FHitResult& OutHit,
        AActor* IgnoreActor = nullptr
    ) const;

    /**
     * @brief 스피어로 Sweep하여 Static 콜라이더와 충돌 검사
     */
    bool SweepSphere(
        const FVector& Start,
        const FVector& End,
        float Radius,
        FHitResult& OutHit,
        AActor* IgnoreActor = nullptr
    ) const;

private:
    // Per-Scene 리소스 (인스턴스별로 고유)
    PxScene*                Scene           = nullptr;

    std::vector<GameObject> Objects; // 간단 테스트용

    bool bSimulating = false;  // 시뮬레이션 진행 중 여부
    FSimulationEventCallback* SimulationEventCallback = nullptr;
};