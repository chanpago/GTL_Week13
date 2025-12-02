#include "pch.h"
#include "SimulationEventCallback.h"
#include "PhysScene.h"
#include "BodyInstance.h"
#include "../Components/PrimitiveComponent.h"
#include "../Object/Actor.h"

inline FVector PxToFVector(const PxVec3& V)
{
	return FVector(V.x, V.y, V.z);
}

FSimulationEventCallback::FSimulationEventCallback(FPhysScene* owner)
	: OwnerScene(owner), bIsShuttingDown(false)
{
}

void FSimulationEventCallback::OnPreShutdown()
{
	bIsShuttingDown.store(true);
}


void FSimulationEventCallback::onContact(const PxContactPairHeader& PairHeader, const PxContactPair* Pairs, PxU32 NumPairs)
{
	if (bIsShuttingDown.load() || !OwnerScene)
	{
		return;
	}

	// 1. 저수준 physx actor 가져옴
	PxRigidActor* ActorA_px = PairHeader.actors[0];
	PxRigidActor* ActorB_px = PairHeader.actors[1];

	// 2. userData에서 BodyInstance 포인터를 찾아 매핑해줌 (BodyInstance에서 this로 정의해둠)
	FBodyInstance* BodyInstA = static_cast<FBodyInstance*>(ActorA_px->userData);
	FBodyInstance* BodyInstB = static_cast<FBodyInstance*>(ActorB_px->userData);

	if (BodyInstA && BodyInstB)
	{
		UPrimitiveComponent* PrimCompA = BodyInstA->OwnerComponent;
		UPrimitiveComponent* PrimCompB = BodyInstB->OwnerComponent;

		if (PrimCompA && PrimCompB)
		{
			AActor* OwnerA = PrimCompA->GetOwner();
			AActor* OwnerB = PrimCompB->GetOwner();

			if (OwnerA && OwnerB)
			{
				// 3. 충돌 정보 채우기 
				FContactHit ContactHit;
				ContactHit.ActorTo = OwnerA;
				ContactHit.ActorFrom = OwnerB;
				ContactHit.Position = FVector(0, 0, 0);
				ContactHit.Normal = FVector(0, 0, 1);
				ContactHit.ImpulseOnActorTo = FVector(0, 0, 0);

				for (PxU32 i = 0; i < NumPairs; ++i)
				{
					const PxContactPair& Pair = Pairs[i];

					// 4. 실제로 contact가 있는 이벤트만 처리 (found/persists/ccd 등)
					if (!(Pair.events & (PxPairFlag::eNOTIFY_TOUCH_FOUND |
						PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
						PxPairFlag::eNOTIFY_TOUCH_CCD)))
					{
						continue;
					}

					PxContactPairPoint ContactPoints[16];
					PxU32 NumContacts = Pair.extractContacts(ContactPoints, 16);
					if (NumContacts == 0)
					{
						continue;
					}

					const PxContactPairPoint& CP = ContactPoints[0]; // 첫번째 지점만 사용

					// 위치/법선은 월드 좌표 기준
					ContactHit.Position = PxToFVector(CP.position);
					ContactHit.Normal = PxToFVector(CP.normal);
					ContactHit.ImpulseOnActorTo = PxToFVector(CP.impulse);
					
					// 한 페어만 써도 충분하니 여기서 break
					break;
				}

				OwnerScene->OnContactDelegate.Broadcast(ContactHit);
			}
		}
	}
}

void FSimulationEventCallback::onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count)
{
	if (bIsShuttingDown.load() || !OwnerScene)
	{
		return;
	}

	for (PxU32 i = 0; i < count; ++i)
	{
		const PxTriggerPair& Pair = pairs[i];

		// 1. 저수준 physx actor 가져옴
		PxRigidActor* TriggerActor_px = Pair.triggerActor;
		PxRigidActor* OtherActor_px = Pair.otherActor;

		// 2. userData에서 BodyInstance 포인터를 찾아 매핑
		FBodyInstance* TriggerBodyInst = static_cast<FBodyInstance*>(TriggerActor_px->userData);
		FBodyInstance* OtherBodyInst = static_cast<FBodyInstance*>(OtherActor_px->userData);

		if (!TriggerBodyInst || !OtherBodyInst)
		{
			continue;
		}

		UPrimitiveComponent* TriggerComp = TriggerBodyInst->OwnerComponent;
		UPrimitiveComponent* OtherComp = OtherBodyInst->OwnerComponent;

		if (!TriggerComp || !OtherComp)
		{
			continue;
		}

		AActor* TriggerOwner = TriggerComp->GetOwner();
		AActor* OtherOwner = OtherComp->GetOwner();

		if (!TriggerOwner || !OtherOwner)
		{
			continue;
		}

		// 3. 트리거 정보 채우기
		FTriggerHit TriggerHit;
		TriggerHit.TriggerActor = TriggerOwner;
		TriggerHit.OtherActor = OtherOwner;

		// 4. Enter/Leave 이벤트 구분
		if (Pair.status & PxPairFlag::eNOTIFY_TOUCH_FOUND)
		{
			// Trigger Enter
			TriggerHit.bIsEnter = true;
			OwnerScene->OnTriggerDelegate.Broadcast(TriggerHit);
		}
		else if (Pair.status & PxPairFlag::eNOTIFY_TOUCH_LOST)
		{
			// Trigger Leave
			TriggerHit.bIsEnter = false;
			OwnerScene->OnTriggerDelegate.Broadcast(TriggerHit);
		}
	}
}