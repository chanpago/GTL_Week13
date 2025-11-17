#pragma once
#include "Controller.h"
#include "APlayerController.generated.h"	

class APlayerController : public AController
{
public:
	GENERATED_REFLECTION_BODY()

	APlayerController();
	virtual ~APlayerController() override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void SetupInput();

protected:
    void ProcessMovementInput(float DeltaTime);
    void ProcessRotationInput(float DeltaTime);

protected:
    bool bMouseLookEnabled = true;

private:
	float Sensitivity = 0.1;


};
