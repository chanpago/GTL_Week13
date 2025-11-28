#pragma once

#include <PxPhysicsAPI.h>
#include <DirectXMath.h>
#include <vector>

using namespace physx;
using namespace DirectX;

// PhysX Global Variables - Declarations
extern PxDefaultAllocator      gAllocator;
extern PxDefaultErrorCallback  gErrorCallback;
extern PxFoundation*           gFoundation;
extern PxPhysics*              gPhysics;
extern PxScene*                gScene;
extern PxMaterial*             gMaterial;
extern PxDefaultCpuDispatcher* gDispatcher;

// Game Object Structure
struct GameObject {
    PxRigidDynamic* rigidBody = nullptr;
    XMMATRIX worldMatrix = XMMatrixIdentity();

    void UpdateFromPhysics() {
        PxTransform t = rigidBody->getGlobalPose();
        PxMat44 mat(t);
        worldMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&mat));
    }
};

extern std::vector<GameObject> gObjects;

// PhysX Functions
void InitPhysX();
GameObject CreateBox(const PxVec3& pos, const PxVec3& halfExtents);
void Simulate(float dt);
