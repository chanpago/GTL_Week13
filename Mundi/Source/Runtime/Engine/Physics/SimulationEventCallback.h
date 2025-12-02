#pragma once
#include <PxPhysicsAPI.h>

#include <atomic>

namespace physx
{
    class PxSimulationEventCallback;
    struct PxContactPairHeader;
    struct PxContactPair;
    struct PxTriggerPair;
    struct PxConstraintInfo;
    class PxActor;
    class PxRigidBody;
    class PxTransform;
}

class FPhysScene;
class FSimulationEventCallback : public PxSimulationEventCallback
{
public:
	FSimulationEventCallback(FPhysScene* owner);
    virtual ~FSimulationEventCallback() = default;

    void OnPreShutdown();

    virtual void onContact(const physx::PxContactPairHeader& pairHeader, const physx::PxContactPair* pairs, physx::PxU32 nbPairs) override;
    virtual void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override;

    virtual void onConstraintBreak(physx::PxConstraintInfo* constraints, physx::PxU32 count) override {};
    virtual void onWake(physx::PxActor** actors, physx::PxU32 count) override {};
    virtual void onSleep(physx::PxActor** actors, physx::PxU32 count) override {};
    virtual void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) override {};

private:
	FPhysScene* OwnerScene;
    std::atomic<bool> bIsShuttingDown;
};