#include "pch.h"
#include "PhysScene.h"
#include <Windows.h>


FPhysScene::FPhysScene()
{
}

FPhysScene::~FPhysScene()
{
    Shutdown();
}

bool FPhysScene::Initialize()
{
    // 1) Foundation
    // PhysX SDK의 최상위 루트 객체
    Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Allocator, ErrorCallback);

    if (!Foundation)
    {
        return false;
    }

    // 2) PVD (PhysX Visual Debugger) 연결
    // PVD 프로그램이 실행 중이면 자동 연결, 없으면 무시
    Pvd = PxCreatePvd(*Foundation);
    if (Pvd)
    {
        PvdTransport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
        if (PvdTransport)
        {
            bool bConnected = Pvd->connect(*PvdTransport, PxPvdInstrumentationFlag::eALL);
            if (bConnected)
            {
                UE_LOG("[PhysScene] PVD connected successfully");
            }
            else
            {
                UE_LOG("[PhysScene] PVD connection failed (PVD application not running?)");
            }
        }
    }

    // 3) Physics
    // PhysX SDK의 메인 엔트리 포인트, 앞으로 우리가 만드는 모든 물리 객체는 Physics를 통해 생성됨.
    // PxTolerancesScale()은 길이/질량/속도 스케일 과 같은 기본값을 넘겨주는 구조체
    // SceneDesc, joint limit, contact offset 같은 곳에 "이 게임 세계의 단위가 어느 정도냐"를 추정할 때 사용
    // 한줄요약 : "씬, 머티리얼, 쉐이프 같은 물리 객체를 전부 찍어내는 공장(Factory)"
    // PVD를 전달하여 디버깅 지원
    Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, PxTolerancesScale(), true, Pvd);

    if (!Physics)
    {
        return false;
    }

    // 3) Default Material
    // 기본 표면 성질
    DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.6f);

    if (!DefaultMaterial)
    {
        return false;
    }

    // 4) SceneDesc Setting
    // PxSceneDesc는 PxScene을 만들기 위한 설정 구조체
    // 중요한요소 : gravity, CpuDispatcher, filterShader(충돌 필터링/마스크)
    // 한줄요약 : 이 Scene은 Z-up월드고, 중력은 -Z방향으로 9.81m/s^2로 떨어진다라고 PhysX에게 알려주는 단계
    PxSceneDesc sceneDesc(Physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0, 0, -9.81); // LH Z-up이기 때문에 중력처리는 Z축에서 진행

    // 5) CPU Thread Setting
    // PhysX용 워커 쓰레드 풀
    // simulate() 호출 시, 이 Dispatcher에 등록된 워커 스레드가 병렬로 처리함
    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;

    // 메인 쓰레드는 게임/렌더용으로 남겨두고 나머지 코어는 물리작업시키겠다는 의도
    // TODO : 하지만 이 엔진에서는 파티클에 비동기를 적용하므로 -2가 되어야할 수도 있음. 이거는 나중에 고민하고 일단은 -1로
    int numWorkerTreads = std::max(1, numCores - 1); 

    Dispatcher = PxDefaultCpuDispatcherCreate(numWorkerTreads);

    if (!Dispatcher)
    {
        return false;
    }

    sceneDesc.cpuDispatcher = Dispatcher;

    // 충돌 필터링 로직을 담당하는 함수포인터
    // actor/shape의 PxFilterData를 보고 "이 둘을 충돌시킬지? 트리거로 볼지?"를 결정
    // 한줄요약 : Dispatcher : "이 씬의 물리 계산을 몇 개의 쓰레드에서 돌릴지"  filterShader : "어떤 객체들 끼리 충돌을 계산/무시 할지 결정"
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;

    // 6) Scene 생성
    // PxScene = 실제 물리 시뮬레이션 월드하나
    // 모든 RigidDynamic, PxRigidStatic, PxShape(Collider), PxJoint(Joint), 중력, 시뮬레이션 옵션, 콜백
    // 한줄요약 : 월드 안에서 물리만 전담하는 UWorld같은 개념
    Scene = Physics->createScene(sceneDesc);
    if (!Scene)
    {
        return false;
    }

    // 7) PVD Scene 클라이언트 설정
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

    return true;
    
}

void FPhysScene::Shutdown()
{
    // 테스트용 오브젝트 리스트 비우기
    Objects.clear();

    // 생성 역순으로 해제
    if (Scene)
    {
        Scene->release();
        Scene = nullptr;
    }

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
        Physics->release();
        Physics = nullptr;
    }

    // PVD 연결 해제
    if (Pvd)
    {
        if (Pvd->isConnected())
        {
            Pvd->disconnect();
        }
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
}

void FPhysScene::StepSimulation(float dt)
{
    if (!Scene)
        return;

    Scene->simulate(dt);
    Scene->fetchResults(true);

    // 결과를 GameObject에 반영
    for (auto& obj : Objects)
    {
        //obj.UpdateFromPhysics();
    }
}

FPhysScene::GameObject& FPhysScene::CreateBox(const PxVec3& pos, const PxVec3& halfExtents)
{
    GameObject obj;

    PxTransform pose(pos);
    obj.rigidBody = Physics->createRigidDynamic(pose);

    PxShape* shape = Physics->createShape(
        PxBoxGeometry(halfExtents),
        *DefaultMaterial
    );

    obj.rigidBody->attachShape(*shape);

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
    return Physics;

}

PxScene* FPhysScene::GetScene() const
{
    return Scene;
}

PxMaterial* FPhysScene::GetDefaultMaterial() const
{
    return DefaultMaterial;
}

PxDefaultCpuDispatcher* FPhysScene::GetDispatcher() const
{
    return Dispatcher;
}
