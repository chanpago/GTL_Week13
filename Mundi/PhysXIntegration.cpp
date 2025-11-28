#include "pch.h"
#include "PhysXIntegration.h"

// PhysX Global Variables - Definitions
PxDefaultAllocator      gAllocator;
PxDefaultErrorCallback  gErrorCallback;
PxFoundation*           gFoundation = nullptr;
PxPhysics*              gPhysics = nullptr;
PxScene*                gScene = nullptr;
PxMaterial*             gMaterial = nullptr;
PxDefaultCpuDispatcher* gDispatcher = nullptr;

// Game Objects
std::vector<GameObject> gObjects;

// PhysX Initialization
void InitPhysX() {
    gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, physx::PxTolerancesScale());
    gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.6f);
    
    PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0, -9.81f, 0);
    
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    int NumCores = SysInfo.dwNumberOfProcessors;
    int NumWorkerThreads = PxMax(1, NumCores - 1);
    
    gDispatcher = PxDefaultCpuDispatcherCreate(NumWorkerThreads);
    sceneDesc.cpuDispatcher = gDispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;
    gScene = gPhysics->createScene(sceneDesc);	
}

GameObject CreateBox(const PxVec3& pos, const PxVec3& halfExtents) {
    GameObject obj;
    PxTransform pose(pos);
    obj.rigidBody = gPhysics->createRigidDynamic(pose);
    PxShape* shape = gPhysics->createShape(PxBoxGeometry(halfExtents), *gMaterial);
    obj.rigidBody->attachShape(*shape);
    PxRigidBodyExt::updateMassAndInertia(*obj.rigidBody, 10.0f);
    gScene->addActor(*obj.rigidBody);
    obj.UpdateFromPhysics();
    return obj;
}

void Simulate(float dt) {
    gScene->simulate(dt);
    gScene->fetchResults(true);
    for (auto& obj : gObjects) obj.UpdateFromPhysics();
}
