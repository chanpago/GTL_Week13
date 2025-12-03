#include "pch.h"
#include "PhysScene.h"
#include "SimulationEventCallback.h"
#include "Source/Runtime/Engine/Collision/Collision.h"
#include "Source/Runtime/Engine/Physics/BodyInstance.h"
#include "Source/Runtime/Engine/Components/PrimitiveComponent.h"
#include "Actor.h"
#include "BodySetup.h"
#include "ObjectIterator.h"
#include "StaticMesh.h"
#include <Windows.h>

/**
 * @brief 차량 서스펜션 레이캐스트용 Pre-Filter 셰이더
 * 
 * 차량의 레이캐스트가 자기 자신을 감지하지 않고, 
 * 운전 가능한 표면만 감지하도록 하는 필터링 함수
 */
PxQueryHitType::Enum VehicleWheelRaycastPreFilter(
    PxFilterData filterData0, // 레이캐스트의 필터 데이터
    PxFilterData filterData1, // 충돌 대상 Shape의 필터 데이터  
    const void* constantBlock, 
    PxU32 constantBlockSize,
    PxHitFlags& queryFlags)
{
    PX_UNUSED(constantBlockSize);
    PX_UNUSED(constantBlock);
    PX_UNUSED(filterData0);
    PX_UNUSED(queryFlags);

    // 차량 자체의 Shape는 무시 (word3에서 SURFACE_TYPE_VEHICLE 체크)
    if (filterData1.word3 & SURFACE_TYPE_VEHICLE)
    {
        return PxQueryHitType::eNONE;
    }

    // 운전 가능한 표면만 블록으로 처리 (바퀴가 감지해야 하는 표면)
    if (filterData1.word3 & SURFACE_TYPE_DRIVABLE)
    {
        return PxQueryHitType::eBLOCK;
    }

    // 운전 불가능한 표면도 블록으로 처리 (벽 등과 충돌)
    if (filterData1.word3 & SURFACE_TYPE_UNDRIVABLE)
    {
        return PxQueryHitType::eBLOCK;
    }

    // 알 수 없는 표면은 무시
    return PxQueryHitType::eBLOCK;
}

/**
 * @brief PhysX 에러 콜백을 UE_LOG로 출력하는 커스텀 에러 핸들러 구현
 */
void FPhysXCustomErrorCallback::reportError(PxErrorCode::Enum code, const char* message, const char* file, int line)
{
    const char* errorType = GetErrorTypeString(code);
    
    // 에러 코드별로 다른 로그 레벨 적용
    switch (code)
    {
    case PxErrorCode::eNO_ERROR:
        UE_LOG("[PhysX][info] %s: %s (File: %s, Line: %d)", errorType, message, file, line);
        break;
        
    case PxErrorCode::eDEBUG_INFO:
    case PxErrorCode::eDEBUG_WARNING:
        UE_LOG("[PhysX][warning] %s: %s (File: %s, Line: %d)", errorType, message, file, line);
        break;
        
    case PxErrorCode::eINVALID_PARAMETER:
    case PxErrorCode::eINVALID_OPERATION:
    case PxErrorCode::eOUT_OF_MEMORY:
    case PxErrorCode::eINTERNAL_ERROR:
    case PxErrorCode::eABORT:
    case PxErrorCode::ePERF_WARNING:
        UE_LOG("[PhysX][error] %s: %s (File: %s, Line: %d)", errorType, message, file, line);
        break;
        
    default:
        UE_LOG("[PhysX][error] UNKNOWN_ERROR: %s (File: %s, Line: %d)", message, file, line);
        break;
    }
}

const char* FPhysXCustomErrorCallback::GetErrorTypeString(PxErrorCode::Enum code)
{
    switch (code)
    {
    case PxErrorCode::eNO_ERROR:       return "NO_ERROR";
    case PxErrorCode::eDEBUG_INFO:     return "DEBUG_INFO";
    case PxErrorCode::eDEBUG_WARNING:  return "DEBUG_WARNING";
    case PxErrorCode::eINVALID_PARAMETER: return "INVALID_PARAMETER";
    case PxErrorCode::eINVALID_OPERATION:  return "INVALID_OPERATION";
    case PxErrorCode::eOUT_OF_MEMORY:      return "OUT_OF_MEMORY";
    case PxErrorCode::eINTERNAL_ERROR:     return "INTERNAL_ERROR";
    case PxErrorCode::eABORT:              return "ABORT";
    case PxErrorCode::ePERF_WARNING:       return "PERF_WARNING";
    default:                               return "UNKNOWN_ERROR";
    }
}

// ===== FPhysXSharedResources Static Members =====
PxDefaultAllocator FPhysXSharedResources::Allocator;
FPhysXCustomErrorCallback FPhysXSharedResources::ErrorCallback;  // 커스텀 에러 콜백 사용
FPhysXAssertHandler FPhysXSharedResources::AssertHandler;
PxFoundation* FPhysXSharedResources::Foundation = nullptr;
PxPvd* FPhysXSharedResources::Pvd = nullptr;
PxPvdTransport* FPhysXSharedResources::PvdTransport = nullptr;
PxPhysics* FPhysXSharedResources::Physics = nullptr;
PxDefaultCpuDispatcher* FPhysXSharedResources::Dispatcher = nullptr;
PxMaterial* FPhysXSharedResources::DefaultMaterial = nullptr;
int32 FPhysXSharedResources::RefCount = 0;
bool FPhysXSharedResources::bInitialized = false;

// 차량용 공유 리소스
PxVehicleDrivableSurfaceToTireFrictionPairs* FPhysXSharedResources::VehicleFrictionPairs = nullptr;
TArray<PxBatchQuery*> FPhysXSharedResources::ActiveBatchQueries;

bool FPhysXSharedResources::Initialize()
{
    if (bInitialized)
        return true;

    // 커스텀 Assert 핸들러 등록
    PxSetAssertHandler(AssertHandler);

    // 1) Foundation - PhysX SDK의 최상위 루트 객체
    Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Allocator, ErrorCallback);
    if (!Foundation)
    {
        UE_LOG("[PhysXSharedResources] PxCreateFoundation failed!");
        return false;
    }

    // 2) PVD (PhysX Visual Debugger) 연결
    Pvd = PxCreatePvd(*Foundation);
    if (Pvd)
    {
        PvdTransport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
        if (PvdTransport)
        {
            bool bConnected = Pvd->connect(*PvdTransport, PxPvdInstrumentationFlag::eALL);
            if (bConnected)
            {
                UE_LOG("[PhysXSharedResources] PVD connected successfully");
            }
            else
            {
                UE_LOG("[PhysXSharedResources] PVD connection failed (PVD application not running?)");
                Pvd->release();
                Pvd = nullptr;
                PvdTransport->release();
                PvdTransport = nullptr;
            }
        }
        else
        {
            Pvd->release();
            Pvd = nullptr;
        }
    }

    // 3) Physics - 모든 물리 객체를 생성하는 팩토리
    Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, PxTolerancesScale(), true, Pvd);
    if (!Physics)
    {
        UE_LOG("[PhysXSharedResources] PxCreatePhysics failed!");
        return false;
    }

    // Extensions 초기화 (Joint, Character Controller 등)
    if (!PxInitExtensions(*Physics, Pvd))
    {
        UE_LOG("[PhysXSharedResources] PxInitExtensions failed!");
        return false;
    }

    // 4) Default Material
    DefaultMaterial = Physics->createMaterial(0.5f, 0.4f, 0.5f);
    if (!DefaultMaterial)
    {
        UE_LOG("[PhysXSharedResources] createMaterial failed!");
        return false;
    }

    // 5) CPU Dispatcher - 멀티쓰레드 물리 계산용
    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;
    int numWorkerThreads = std::max(1, numCores - 1);

    Dispatcher = PxDefaultCpuDispatcherCreate(numWorkerThreads);
    if (!Dispatcher)
    {
        UE_LOG("[PhysXSharedResources] PxDefaultCpuDispatcherCreate failed!");
        return false;
    }

    // Initialize PhysX Vehicle SDK
    if (!PxInitVehicleSDK(*Physics))
    {
        UE_LOG("[MyCarComponent] Failed to initialize PhysX Vehicle SDK");
        return false;
    }

    // 차량용 공유 리소스 초기화
    InitializeVehicleResources();

    bInitialized = true;
    UE_LOG("[PhysXSharedResources] Initialized successfully (Workers: %d)", numWorkerThreads);
    return true;


}

void FPhysXSharedResources::Shutdown()
{
    if (!bInitialized)
        return;

    // 차량용 공유 리소스 정리
    ShutdownVehicleResources();

    if (Dispatcher)
    {
        Dispatcher->release();
        Dispatcher = nullptr;
    }

    if (DefaultMaterial)
    {
        DefaultMaterial->release();
        DefaultMaterial = nullptr;
    }

    if (Physics)
    {
        PxCloseExtensions();
        Physics->release();
        Physics = nullptr;

        PxCloseVehicleSDK();
    }

    if (Pvd)
    {
        if (Pvd->isConnected())
            Pvd->disconnect();
        Pvd->release();
        Pvd = nullptr;
    }

    if (PvdTransport)
    {
        PvdTransport->release();
        PvdTransport = nullptr;
    }

    if (Foundation)
    {
        Foundation->release();
        Foundation = nullptr;
    }

    bInitialized = false;
    UE_LOG("[PhysXSharedResources] Shutdown complete");
}

void FPhysXSharedResources::AddRef()
{
    if (RefCount == 0)
    {
        Initialize();
    }
    RefCount++;
}

void FPhysXSharedResources::Release()
{
    RefCount--;
    if (RefCount <= 0)
    {
        Shutdown();
        RefCount = 0;
    }
}

// ===== Helper Functions for Surface Setup =====

/**
 * @brief 운전 가능한 표면 설정 (도로, 지면 등)
 */
void SetupDrivableSurface(PxFilterData& filterData)
{
    filterData.word0 = COLLISION_GROUP_DRIVABLE_SURFACE;
    filterData.word1 = ~0; // 모든 것과 충돌
    filterData.word2 = 0;
    filterData.word3 = SURFACE_TYPE_DRIVABLE;
}

/**
 * @brief 운전 불가능한 표면 설정 (벽, 장애물 등)  
 */
void SetupUndrivableSurface(PxFilterData& filterData)
{
    filterData.word0 = COLLISION_GROUP_UNDRIVABLE_SURFACE;
    filterData.word1 = ~0; // 모든 것과 충돌
    filterData.word2 = 0;
    filterData.word3 = SURFACE_TYPE_UNDRIVABLE;
}

/**
 * @brief 차량 표면 설정
 */
void SetupVehicleSurface(PxFilterData& filterData, bool bIsChassis)
{
    if (bIsChassis)
    {
        filterData.word0 = COLLISION_GROUP_VEHICLE_CHASSIS;
    }
    else
    {
        filterData.word0 = COLLISION_GROUP_VEHICLE_WHEEL;
    }
    filterData.word1 = ~(COLLISION_GROUP_VEHICLE_CHASSIS | COLLISION_GROUP_VEHICLE_WHEEL); // 차량끼리는 충돌 안함
    filterData.word2 = 0;
    filterData.word3 = SURFACE_TYPE_VEHICLE;
}

/**
 * @brief Kinematic-Dynamic 충돌 시 질량 기반 임펄스 조절 콜백
 *
 * Kinematic 바디가 Dynamic 바디를 밀 때, Kinematic의 "가상 질량"과
 * Dynamic의 실제 질량 비율로 임펄스를 스케일링합니다.
 *
 * 예: 70kg Kinematic이 10000kg Dynamic을 밀면 임펄스가 0.7%로 감소
 */
class FContactModifyCallback : public PxContactModifyCallback
{
public:
    virtual void onContactModify(PxContactModifyPair* pairs, PxU32 count) override
    {
        // 콜백 호출 확인
        static int callCount = 0;
        if (callCount++ < 5)
        {
            UE_LOG("[ContactModify] onContactModify called! pairs=%d", count);
        }

        for (PxU32 i = 0; i < count; i++)
        {
            PxContactModifyPair& pair = pairs[i];

            // 두 액터 중 Kinematic과 Dynamic 찾기
            const PxRigidDynamic* actor0 = pair.actor[0]->is<PxRigidDynamic>();
            const PxRigidDynamic* actor1 = pair.actor[1]->is<PxRigidDynamic>();

            if (!actor0 || !actor1)
            {
                static int skipCount = 0;
                if (skipCount++ < 3)
                {
                    UE_LOG("[ContactModify] Skipped: not both RigidDynamic (actor0=%p, actor1=%p)", actor0, actor1);
                }
                continue;
            }

            // Kinematic 여부 확인 (getRigidBodyFlags()로 실시간 체크)
            PxRigidBodyFlags flags0 = actor0->getRigidBodyFlags();
            PxRigidBodyFlags flags1 = actor1->getRigidBodyFlags();
            bool bIsKinematic0 = (flags0 & PxRigidBodyFlag::eKINEMATIC);
            bool bIsKinematic1 = (flags1 & PxRigidBodyFlag::eKINEMATIC);

            // 둘 중 하나만 Kinematic이어야 함 (Kinematic vs Dynamic)
            if (bIsKinematic0 == bIsKinematic1)
                continue;

            // Kinematic과 Dynamic 분리
            const PxRigidDynamic* kinematicActor = bIsKinematic0 ? actor0 : actor1;
            const PxRigidDynamic* dynamicActor = bIsKinematic0 ? actor1 : actor0;

            // BodyInstance에서 BodySetup의 질량 가져오기
            FBodyInstance* kinematicBI = static_cast<FBodyInstance*>(kinematicActor->userData);
            if (!kinematicBI || !kinematicBI->BodySetup)
                continue;

            float kinematicMass = kinematicBI->BodySetup->Mass;
            float dynamicMass = dynamicActor->getMass();

            // 질량 비율 계산 (0에 가까울수록 거의 안 밀림)
            float massRatio = kinematicMass / (kinematicMass + dynamicMass);

            // 디버그 로그 (처음 몇 번만 출력)
            static int logCount = 0;
            if (logCount < 5)
            {
                UE_LOG("[ContactModify] Kinematic(%.1fkg) vs Dynamic(%.1fkg) -> massRatio=%.3f",
                       kinematicMass, dynamicMass, massRatio);
                logCount++;
            }

            // 질량 비율이 너무 낮으면 (가벼운 kinematic이 무거운 dynamic을 밀 때)
            // 접촉을 무시하거나 임펄스를 크게 줄임
            if (massRatio < 0.1f)
            {
                // 무거운 물체는 거의 안 밀리게 - 접촉점 무시
                for (PxU32 j = 0; j < pair.contacts.size(); j++)
                {
                    pair.contacts.ignore(j);
                }
            }
            else
            {
                // 임펄스 제한 (질량 비율 적용)
                for (PxU32 j = 0; j < pair.contacts.size(); j++)
                {
                    float originalMaxImpulse = pair.contacts.getMaxImpulse(j);
                    pair.contacts.setMaxImpulse(j, originalMaxImpulse * massRatio);
                }
            }
        }
    }
};

// 전역 Contact Modify Callback 인스턴스
static FContactModifyCallback GContactModifyCallback;

/**
 * @brief 래그돌용 커스텀 충돌 필터 셰이더
 *
 * PxFilterData 사용법:
 * - word0: 오브젝트 그룹 ID (같은 스켈레탈 메쉬 = 같은 ID)
 * - word1: 충돌 마스크 (어떤 그룹과 충돌할지)
 * - word2: 예약
 * - word3: 예약
 *
 * 같은 word0을 가진 바디들끼리는 충돌하지 않음 (Self-Collision 비활성화)
 */
static PxFilterFlags RagdollFilterShader(
    PxFilterObjectAttributes attributes0, PxFilterData filterData0,
    PxFilterObjectAttributes attributes1, PxFilterData filterData1,
    PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
{
    // 1) Self Collision 처리
    // : word0이 같으면 같은 스켈레탈 메쉬의 바디들 → Self-Collision 비활성화
    // word0 == 0은 일반 오브젝트 (래그돌이 아님) → 서로 충돌함
    if (filterData0.word0 != 0 && filterData0.word0 == filterData1.word0)
    {
        return PxFilterFlag::eSUPPRESS;  // 충돌 무시
    }

    // 2) 트리거 처리 
    // : 물리 처리 없고, CallBack 등으로 Event 발생용
    // Trigger가 Contact보다 우선 순위가 높다. 둘 중 하나만 Trigger라도 Contact 무시하고 Trigger 처리
    if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
    {
        pairFlags = PxPairFlag::eTRIGGER_DEFAULT
            | PxPairFlag::eNOTIFY_TOUCH_FOUND
            | PxPairFlag::eNOTIFY_TOUCH_LOST;
        return PxFilterFlag::eDEFAULT;
    }

    // 3) 기본 충돌 처리
    pairFlags = PxPairFlag::eCONTACT_DEFAULT
        | PxPairFlag::eNOTIFY_TOUCH_FOUND
        | PxPairFlag::eNOTIFY_TOUCH_PERSISTS
        | PxPairFlag::eNOTIFY_TOUCH_LOST;

    // 5) Dynamic 바디가 포함된 모든 충돌에 Contact Modify 활성화
    // Kinematic 플래그는 런타임에 변경될 수 있으므로, 콜백에서 실시간으로 체크
    // PxFilterObjectIsKinematic()은 캐시된 값을 사용해서 부정확할 수 있음
    pairFlags |= PxPairFlag::eMODIFY_CONTACTS;

    return PxFilterFlag::eDEFAULT;
}

FPhysScene::FPhysScene()
{
}

FPhysScene::~FPhysScene()
{
    Shutdown();
}

bool FPhysScene::Initialize()
{
    // 공유 리소스 초기화 (참조 카운트 증가)
    // Foundation, Physics, Dispatcher 등은 FPhysXSharedResources에서 관리
    FPhysXSharedResources::AddRef();

    if (!FPhysXSharedResources::IsInitialized())
    {
        UE_LOG("[PhysScene] Shared resources initialization failed!");
        return false;
    }

    PxPhysics* Physics = FPhysXSharedResources::GetPhysics();

    // SceneDesc Setting
    // PxSceneDesc는 PxScene을 만들기 위한 설정 구조체
    // 중요한요소 : gravity, CpuDispatcher, filterShader(충돌 필터링/마스크)
    // 한줄요약 : 이 Scene은 Z-up월드고, 중력은 -Z방향으로 9.81m/s^2로 떨어진다라고 PhysX에게 알려주는 단계
    PxSceneDesc sceneDesc(Physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0, 0, -9.81); // LH Z-up이기 때문에 중력처리는 Z축에서 진행

    // Create and set the simulation event callback
    SimulationEventCallback = new FSimulationEventCallback(this);
    sceneDesc.simulationEventCallback = SimulationEventCallback;

    // CPU Thread Setting
    // PhysX용 워커 쓰레드 풀 (공유 Dispatcher 사용)
    // simulate() 호출 시, 이 Dispatcher에 등록된 워커 스레드가 병렬로 처리함
    sceneDesc.cpuDispatcher = FPhysXSharedResources::GetDispatcher();

    // 업데이트된 충돌 필터링 로직을 담당하는 함수포인터
    // actor/shape의 PxFilterData를 보고 "이 둘을 충돌시킬지? 트리거로 볼지?"를 결정
    // 차량 표면 타입도 고려하는 필터 셰이더 사용
    sceneDesc.filterShader = RagdollFilterShader;

    // Scene 생성
    // PxScene = 실제 물리 시뮬레이션 월드하나
    // 모든 RigidDynamic, PxRigidStatic, PxShape(Collider), PxJoint(Joint), 중력, 시뮬레이션 옵션, 콜백
    // 한줄요약 : 월드 안에서 물리만 전담하는 UWorld같은 개념
    Scene = Physics->createScene(sceneDesc);
    if (!Scene)
    {
        UE_LOG("[PhysScene] createScene failed!");
        return false;
    }

    // Contact Modify Callback 등록 (Scene 생성 후 설정)
    // Kinematic vs Dynamic 충돌 시 질량 기반 임펄스 조절
    Scene->setContactModifyCallback(&GContactModifyCallback);
    UE_LOG("[PhysScene] Contact Modify Callback registered");

    // PVD Scene 클라이언트 설정
    PxPvd* Pvd = FPhysXSharedResources::GetPvd();
    if (Pvd && Pvd->isConnected())
    {
        PxPvdSceneClient* PvdClient = Scene->getScenePvdClient();
        if (PvdClient)
        {
            PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
            PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
            PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
        }
    }

    UE_LOG("[PhysScene] Initialized successfully with vehicle surface filtering");
    return true;
}

void FPhysScene::Shutdown()
{
    // 콜백 객체에 우리가 셧다운 중임을 알림
    if (SimulationEventCallback)
    {
        SimulationEventCallback->OnPreShutdown();
    }

    // 테스트용 오브젝트 리스트 비우기
    Objects.clear();

    // 시뮬레이션이 진행 중이면 완료 대기 (fetchResults 호출)
    // Scene->release() 전에 반드시 호출해야 fireQueuedContactCallbacks 크래시 방지
    if (Scene && bSimulating)
    {
        Scene->fetchResults(true);
        bSimulating = false;
    }

    // Scene을 먼저 해제해야 함
    // Scene->release()가 내부적으로 SimulationEventCallback을 참조하므로
    // SimulationEventCallback보다 Scene을 먼저 해제해야 함
    if (Scene)
    {
        Scene->setSimulationEventCallback(nullptr); // Call Back 연결 해제
        Scene->release();
        Scene = nullptr;
    }

    // Scene 해제 후 콜백 삭제
    if (SimulationEventCallback)
    {
        delete SimulationEventCallback;
        SimulationEventCallback = nullptr;
    }

    for (TObjectIterator<UStaticMesh> It; It; ++It)
    {
        UStaticMesh* Mesh = *It;
        if (Mesh && Mesh->BodySetup)
        {
            for (FKConvexElem& Convex : Mesh->BodySetup->AggGeom.ConvexElements)
            {
                Convex.ConvexMesh = nullptr;
            }
        }
    }
    // 공유 리소스 참조 해제 (마지막 사용자면 자동으로 Shutdown됨)
    FPhysXSharedResources::Release();
}

void FPhysScene::StepSimulation(float dt)
{
    if (!Scene)
        return;

    // 이전 시뮬레이션이 아직 진행 중이면 완료 대기
    if (bSimulating)
    {
        WaitForSimulation();
    }

    Scene->simulate(dt);
    bSimulating = true;

    // Non-blocking: 시뮬레이션 완료를 기다리지 않고 바로 리턴
    // 물리 데이터 접근 시 SCOPED_PHYSX_READ_LOCK 사용 필요
}

bool FPhysScene::IsSimulationComplete() const
{
    if (!Scene || !bSimulating)
        return true;

    return Scene->checkResults();
}

void FPhysScene::WaitForSimulation()
{
    if (!Scene || !bSimulating)
        return;

    Scene->fetchResults(true);  // blocking
    bSimulating = false;
}

FPhysScene::GameObject& FPhysScene::CreateBox(const PxVec3& pos, const PxVec3& halfExtents)
{
    GameObject obj;

    PxPhysics* Physics = FPhysXSharedResources::GetPhysics();
    PxMaterial* DefaultMaterial = FPhysXSharedResources::GetDefaultMaterial();

    PxTransform pose(pos);
    obj.rigidBody = Physics->createRigidDynamic(pose);

    PxShape* shape = Physics->createShape(
        PxBoxGeometry(halfExtents),
        *DefaultMaterial
    );

    if (shape)
    {
        shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
        obj.rigidBody->attachShape(*shape);
    }

    // 질량/관성 설정
    PxRigidBodyExt::updateMassAndInertia(*obj.rigidBody, 10.0f);

    // 씬에 등록
    Scene->addActor(*obj.rigidBody);

    // 초기 worldMatrix 업데이트
    //obj.UpdateFromPhysics();

    Objects.push_back(obj);
    return Objects.back();
}

const std::vector<FPhysScene::GameObject>& FPhysScene::GetObjects() const
{
    return Objects;
}

std::vector<FPhysScene::GameObject>& FPhysScene::GetObjects()
{
    return Objects;
}

PxPhysics* FPhysScene::GetPhysics() const
{
    return FPhysXSharedResources::GetPhysics();
}

PxScene* FPhysScene::GetScene() const
{
    return Scene;
}

PxMaterial* FPhysScene::GetDefaultMaterial() const
{
    return FPhysXSharedResources::GetDefaultMaterial();
}

PxDefaultCpuDispatcher* FPhysScene::GetDispatcher() const
{
    return FPhysXSharedResources::GetDispatcher();
}

// ===== Sweep Query Helper =====
namespace
{
    // PhysX PxSweepHit을 FHitResult로 변환
    void ConvertPxSweepHitToHitResult(
        const PxSweepHit& PxHit,
        const FVector& Start,
        const FVector& End,
        const FVector& Direction,
        float TotalDistance,
        FHitResult& OutHit)
    {
        OutHit.bHit = true;
        OutHit.bBlockingHit = true;
        OutHit.Time = PxHit.distance / TotalDistance;
        OutHit.Distance = PxHit.distance;
        OutHit.Location = Start + Direction * PxHit.distance;
        OutHit.ImpactPoint = FVector(PxHit.position.x, PxHit.position.y, PxHit.position.z);
        OutHit.ImpactNormal = FVector(PxHit.normal.x, PxHit.normal.y, PxHit.normal.z);
        OutHit.TraceStart = Start;
        OutHit.TraceEnd = End;

        // Actor/Component 정보 추출
        if (PxHit.actor)
        {
            void* UserData = PxHit.actor->userData;
            if (UserData)
            {
                // BodyInstance에서 Owner 추출
                FBodyInstance* BodyInst = static_cast<FBodyInstance*>(UserData);
                if (BodyInst && BodyInst->OwnerComponent)
                {
                    OutHit.HitComponent = Cast<UPrimitiveComponent>(BodyInst->OwnerComponent);
                    if (OutHit.HitComponent)
                    {
                        OutHit.HitActor = OutHit.HitComponent->GetOwner();
                    }
                }
            }
        }
    }

    // Static + Dynamic 콜라이더를 대상으로 하는 필터 콜백
    class FSweepQueryFilterCallback : public PxQueryFilterCallback
    {
    public:
        AActor* IgnoreActor = nullptr;

        FSweepQueryFilterCallback(AActor* InIgnoreActor)
            : IgnoreActor(InIgnoreActor)
        {
        }

        virtual PxQueryHitType::Enum preFilter(
            const PxFilterData& filterData,
            const PxShape* shape,
            const PxRigidActor* actor,
            PxHitFlags& queryFlags) override
        {
            // Static 또는 Dynamic 액터만 대상으로 함 (Kinematic 포함)
            PxActorType::Enum actorType = actor->getType();
            if (actorType != PxActorType::eRIGID_STATIC && actorType != PxActorType::eRIGID_DYNAMIC)
            {
                return PxQueryHitType::eNONE;
            }

            // IgnoreActor 처리
            if (IgnoreActor && actor->userData)
            {
                FBodyInstance* BodyInst = static_cast<FBodyInstance*>(actor->userData);
                if (BodyInst && BodyInst->OwnerComponent)
                {
                    AActor* HitActor = BodyInst->OwnerComponent->GetOwner();
                    if (HitActor == IgnoreActor)
                    {
                        return PxQueryHitType::eNONE;
                    }
                }
            }

            return PxQueryHitType::eBLOCK;
        }

        virtual PxQueryHitType::Enum postFilter(
            const PxFilterData& filterData,
            const PxQueryHit& hit) override
        {
            return PxQueryHitType::eBLOCK;
        }
    };
}

bool FPhysScene::SweepCapsule(
    const FVector& Start,
    const FVector& End,
    float Radius,
    float HalfHeight,
    FHitResult& OutHit,
    AActor* IgnoreActor) const
{
    OutHit.Reset();

    if (!Scene)
        return false;

    FVector Direction = End - Start;
    float TotalDistance = Direction.Size();

    if (TotalDistance < KINDA_SMALL_NUMBER)
        return false;

    Direction = Direction / TotalDistance; // Normalize

    // PhysX Capsule Geometry (PhysX 캡슐은 X축 방향이 기본)
    // 우리 캡슐은 Z축 방향이므로 회전 필요
    // PhysX의 halfHeight는 원통 부분만의 반높이 (반구 제외)
    // 전체 캡슐 높이 = 2 * (cylinderHalfHeight + radius)
    float CylinderHalfHeight = FMath::Max(0.0f, HalfHeight - Radius);

    // 1) 캡슐 지오메트리 생성 (PhysX X축 기준)
    PxCapsuleGeometry CapsuleGeom(Radius, CylinderHalfHeight);

    // 2) 캡슐 방향 설정 (Z-up - X-up 변환)
    PxQuat CapsuleRotation(PxHalfPi, PxVec3(0, 1, 0)); // Y축 기준 90도 회전
    PxTransform StartPose(PxVec3(Start.X, Start.Y, Start.Z), CapsuleRotation);

    PxVec3 PxDirection(Direction.X, Direction.Y, Direction.Z);

    // Sweep 쿼리 설정
    PxSweepBuffer HitBuffer;
    PxHitFlags HitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eDEFAULT;

    // Static + Dynamic 모두 대상으로 하는 필터 (Dynamic 바디 위에도 올라갈 수 있도록)
    FSweepQueryFilterCallback FilterCallback(IgnoreActor);
    PxQueryFilterData FilterData;
    FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

    // Read Lock 필요 (시뮬레이션과 동시 접근 방지)
    SCOPED_PHYSX_READ_LOCK(*Scene);

    bool bHit = Scene->sweep(
        CapsuleGeom,
        StartPose,
        PxDirection,
        TotalDistance,
        HitBuffer,
        HitFlags,
        FilterData,
        &FilterCallback
    );

    if (bHit && HitBuffer.hasBlock)
    {
        ConvertPxSweepHitToHitResult(HitBuffer.block, Start, End, Direction, TotalDistance, OutHit);
        return true;
    }

    return false;
}

bool FPhysScene::SweepBox(
    const FVector& Start,
    const FVector& End,
    const FVector& HalfExtents,
    const FQuat& Rotation,
    FHitResult& OutHit,
    AActor* IgnoreActor) const
{
    OutHit.Reset();

    if (!Scene)
        return false;

    FVector Direction = End - Start;
    float TotalDistance = Direction.Size();

    if (TotalDistance < KINDA_SMALL_NUMBER)
        return false;

    Direction = Direction / TotalDistance;

    PxBoxGeometry BoxGeom(HalfExtents.X, HalfExtents.Y, HalfExtents.Z);
    PxQuat PxRot(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
    PxTransform StartPose(PxVec3(Start.X, Start.Y, Start.Z), PxRot);
    PxVec3 PxDirection(Direction.X, Direction.Y, Direction.Z);

    PxSweepBuffer HitBuffer;
    PxHitFlags HitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eDEFAULT;

    FSweepQueryFilterCallback FilterCallback(IgnoreActor);
    PxQueryFilterData FilterData;
    FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

    SCOPED_PHYSX_READ_LOCK(*Scene);

    bool bHit = Scene->sweep(
        BoxGeom,
        StartPose,
        PxDirection,
        TotalDistance,
        HitBuffer,
        HitFlags,
        FilterData,
        &FilterCallback
    );

    if (bHit && HitBuffer.hasBlock)
    {
        ConvertPxSweepHitToHitResult(HitBuffer.block, Start, End, Direction, TotalDistance, OutHit);
        return true;
    }

    return false;
}

bool FPhysScene::SweepSphere(
    const FVector& Start,
    const FVector& End,
    float Radius,
    FHitResult& OutHit,
    AActor* IgnoreActor) const
{
    OutHit.Reset();

    if (!Scene)
        return false;

    FVector Direction = End - Start;
    float TotalDistance = Direction.Size();

    if (TotalDistance < KINDA_SMALL_NUMBER)
        return false;

    Direction = Direction / TotalDistance;

    PxSphereGeometry SphereGeom(Radius);
    PxTransform StartPose(PxVec3(Start.X, Start.Y, Start.Z));
    PxVec3 PxDirection(Direction.X, Direction.Y, Direction.Z);

    PxSweepBuffer HitBuffer;
    PxHitFlags HitFlags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eDEFAULT;

    FSweepQueryFilterCallback FilterCallback(IgnoreActor);
    PxQueryFilterData FilterData;
    FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

    SCOPED_PHYSX_READ_LOCK(*Scene);

    bool bHit = Scene->sweep(
        SphereGeom,
        StartPose,
        PxDirection,
        TotalDistance,
        HitBuffer,
        HitFlags,
        FilterData,
        &FilterCallback
    );

    if (bHit && HitBuffer.hasBlock)
    {
        ConvertPxSweepHitToHitResult(HitBuffer.block, Start, End, Direction, TotalDistance, OutHit);
        return true;
    }

    return false;
}

// ===== Vehicle Surface Setup Functions =====

void FPhysScene::SetupActorAsDrivableSurface(PxRigidActor* actor)
{
    if (!actor) return;

    // 액터의 모든 Shape에 운전 가능한 표면 필터 적용
    PxU32 numShapes = actor->getNbShapes();
    PxShape** shapes = new PxShape*[numShapes];
    actor->getShapes(shapes, numShapes);

    for (PxU32 i = 0; i < numShapes; i++)
    {
        PxFilterData filterData;
        SetupDrivableSurface(filterData);
        shapes[i]->setSimulationFilterData(filterData);
        shapes[i]->setQueryFilterData(filterData);
    }

    delete[] shapes;
    UE_LOG("[PhysScene] Actor set up as drivable surface");
}

void FPhysScene::SetupActorAsUndrivableSurface(PxRigidActor* actor)
{
    if (!actor) return;

    // 액터의 모든 Shape에 운전 불가능한 표면 필터 적용
    PxU32 numShapes = actor->getNbShapes();
    PxShape** shapes = new PxShape*[numShapes];
    actor->getShapes(shapes, numShapes);

    for (PxU32 i = 0; i < numShapes; i++)
    {
        PxFilterData filterData;
        SetupUndrivableSurface(filterData);
        shapes[i]->setSimulationFilterData(filterData);
        shapes[i]->setQueryFilterData(filterData);
    }

    delete[] shapes;
    UE_LOG("[PhysScene] Actor set up as undrivable surface");
}

// ===== 차량용 공유 리소스 관리 함수들 =====

void FPhysXSharedResources::InitializeVehicleResources()
{
    if (!Physics)
    {
        UE_LOG("[PhysXSharedResources] Cannot initialize vehicle resources: Physics not initialized");
        return;
    }

    PxMaterial* DefaultMaterial = GetDefaultMaterial();
    if (!DefaultMaterial)
    {
        UE_LOG("[PhysXSharedResources] Cannot initialize vehicle resources: Default material not found");
        return;
    }

    // Friction Pairs 생성 - 더 많은 타이어 타입과 표면 타입 지원
    const PxU32 numTireTypes = 1;
    const PxU32 numSurfaceTypes = 1;
    
    PxVehicleDrivableSurfaceType surfaceTypes[1];
    surfaceTypes[0].mType = 0;

    const PxMaterial* surfaceMaterials[1];
    surfaceMaterials[0] = DefaultMaterial;

    VehicleFrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(numTireTypes, numSurfaceTypes);
    VehicleFrictionPairs->setup(numTireTypes, numSurfaceTypes, surfaceMaterials, surfaceTypes);

    // 마찰력 강화 - 타이어와 도로 간 마찰력 증가
    for (PxU32 i = 0; i < numTireTypes; i++)
    {
        for (PxU32 j = 0; j < numSurfaceTypes; j++)
        {
            // 마찰력을 1.5로 증가시켜 더 나은 트랙션 제공
            VehicleFrictionPairs->setTypePairFriction(i, j, 1.5f);
        }
    }

    UE_LOG("[PhysXSharedResources] Vehicle resources initialized successfully");
}

void FPhysXSharedResources::ShutdownVehicleResources()
{
    // 활성 BatchQuery들 정리
    for (PxBatchQuery* BatchQuery : ActiveBatchQueries)
    {
        if (BatchQuery)
        {
            BatchQuery->release();
        }
    }
    ActiveBatchQueries.Empty();

    // FrictionPairs 정리
    if (VehicleFrictionPairs)
    {
        VehicleFrictionPairs->release();
        VehicleFrictionPairs = nullptr;
    }

    UE_LOG("[PhysXSharedResources] Vehicle resources shutdown complete");
}

PxBatchQuery* FPhysXSharedResources::CreateVehicleBatchQuery(PxScene* Scene, PxBatchQueryDesc& queryDesc)
{
    if (!Scene)
    {
        UE_LOG("[PhysXSharedResources] CreateVehicleBatchQuery: Scene is null");
        return nullptr;
    }

    // Create batch query for vehicle raycasts with vehicle pre-filter
    queryDesc.preFilterShader = VehicleWheelRaycastPreFilter;  // 차량용 pre-filter 사용
    
    PxBatchQuery* BatchQuery = Scene->createBatchQuery(queryDesc);
    if (BatchQuery)
    {
        ActiveBatchQueries.Add(BatchQuery);
        UE_LOG("[PhysXSharedResources] Vehicle BatchQuery created (NumWheels: %d)", queryDesc.queryMemory.raycastTouchBufferSize);
    }
    else
    {
        UE_LOG("[PhysXSharedResources] Failed to create vehicle BatchQuery");
    }

    return BatchQuery;
}

void FPhysXSharedResources::ReleaseVehicleBatchQuery(PxBatchQuery* BatchQuery)
{
    if (!BatchQuery)
        return;

    // 활성 목록에서 제거 - 직접적인 Remove 사용
    if (!ActiveBatchQueries.empty())
    {
        ActiveBatchQueries.Remove(BatchQuery);
        BatchQuery->release();
        UE_LOG("[PhysXSharedResources] Vehicle BatchQuery released");
    }
}
