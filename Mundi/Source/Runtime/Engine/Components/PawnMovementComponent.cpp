#include "pch.h"
#include "PawnMovementComponent.h"
#include "SceneComponent.h"
#include "Pawn.h"

UPawnMovementComponent::UPawnMovementComponent()
{
}

UPawnMovementComponent::~UPawnMovementComponent()
{
}

void UPawnMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();

	PawnOwner = Cast<APawn>(GetOwner());
}

void UPawnMovementComponent::TickComponent(float DeltaSeconds)
{
	//Super::TickComponent(DeltaSeconds); 

	UpdatedComponent->AddRelativeLocation(PawnOwner->ConsumeMovementInputVector());
}
