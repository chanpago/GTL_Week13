#pragma once

#include "ShapeComponent.h"
#include "UBoxComponent.generated.h"

UCLASS(DisplayName="박스 컴포넌트", Description="박스 모양 충돌 컴포넌트입니다")
class UBoxComponent : public UShapeComponent
{
public:

	GENERATED_REFLECTION_BODY();

	UBoxComponent(); 
	void OnRegister(UWorld* InWorld) override;

	void SetBoxExtent(const FVector& InExtent) { BoxExtent = InExtent; }

	// Duplication
	virtual void DuplicateSubObjects() override;
	
	UPROPERTY(EditAnywhere, Category="BoxExtent")
	FVector BoxExtent; // Half Extent

	void GetShape(FShape& Out) const override;

public:
	//GetReenderCollection 
	void RenderDebugVolume(class URenderer* Renderer) const override;
};