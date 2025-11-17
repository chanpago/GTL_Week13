#include "pch.h"
#include "Controller.h"
#include "Pawn.h"

AController::AController()
{
}

AController::~AController()
{
}

void AController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void AController::Possess(APawn* InPawn)
{
	if (Pawn != nullptr)
	{
		UnPossess();
	}

	if (InPawn == nullptr)
	{
		return;
	}

	Pawn = InPawn;

	// 폰의 controller도 수정 
	Pawn->PossessedBy(this);

	// Pawn의 회전과 Controller의 회전을 일치
	SetControlRotation(Pawn->GetActorRotation());

}

void AController::UnPossess()
{
	if (Pawn != nullptr)
	{
		Pawn->UnPossessed();
		Pawn = nullptr;
	}
}
