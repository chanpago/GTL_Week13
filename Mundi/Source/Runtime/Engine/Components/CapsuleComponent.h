#pragma once

#include "ShapeComponent.h"
#include "UCapsuleComponent.generated.h"

UCLASS(DisplayName="캡슐 컴포넌트", Description="캡슐 모양 충돌 컴포넌트입니다")
class UCapsuleComponent : public UShapeComponent
{
public:

	GENERATED_REFLECTION_BODY();

	UCapsuleComponent();
	void OnRegister(UWorld* World) override;

	// Duplication
	virtual void DuplicateSubObjects() override;
	
	UPROPERTY(EditAnywhere, Category="CapsuleHalfHeight")
	float CapsuleHalfHeight;

	UPROPERTY(EditAnywhere, Category="CapsuleHalfHeight")
	float CapsuleRadius;

	void GetShape(FShape& Out) const override; 

public:
	void RenderDebugVolume(class URenderer* Renderer) const override;
};