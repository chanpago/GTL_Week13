#include "pch.h"
#include "Pawn.h"
#include "SkeletalMeshComponent.h"

APawn::APawn()
{
	SkeletalMeshComp = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SetRootComponent(SkeletalMeshComp);

	/*SkeletalMeshComp->SetSkeletalMesh("James.fbx");*/
}

void APawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 수집한 이동처리 
	AddActorWorldLocation(ConsumeMovementInputVector());
}

void APawn::BeginPlay()
{
	Super::BeginPlay();
}

void APawn::PossessedBy(AController* NewController)
{
	Controller = NewController;
}

void APawn::UnPossessed()
{
	Controller = nullptr;
}

void APawn::AddMovementInput(FVector Direction, float Scale)
{
	InternalMovementInputVector += Direction * Scale;
}

FVector APawn::ConsumeMovementInputVector()
{
	FVector Ret = InternalMovementInputVector;
	InternalMovementInputVector = FVector(0, 0, 0);
	
	return Ret;
}

APawn::~APawn()
{
}
