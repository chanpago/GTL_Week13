#pragma once
#include "MovementComponent.h"
#include "UPawnMovementComponent.generated.h"

class APawn;

class UPawnMovementComponent : public UMovementComponent
{
public:
	GENERATED_REFLECTION_BODY()

	UPawnMovementComponent();
	virtual ~UPawnMovementComponent() override;

	virtual void InitializeComponent() override;

	void TickComponent(float DeltaSeconds) override;

protected:
	APawn* PawnOwner;
};