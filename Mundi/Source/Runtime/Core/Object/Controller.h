#pragma once
#include "Actor.h"
#include "AController.generated.h"

class APawn;

class AController : public AActor
{
public: 
	GENERATED_REFLECTION_BODY()

	AController();
	virtual ~AController() override;


	virtual void Tick(float DeltaSeconds) override;

	// 폰에 빙의
	virtual void Possess(APawn* InPawn);
	virtual void UnPossess();

	// 현재 빙의 된 폰 가져오기
	APawn* GetPawn() const { return Pawn;}
	
	// 컨트롤러의 시선 (카메라 회전 값 등 ) 
	FQuat GetControlRotation() const { return ControlRotation; }
	void SetControlRotation(const FQuat& NewRotation) { ControlRotation = NewRotation; }

protected:
	APawn* Pawn = nullptr;

	// 캐릭터의 회전과 별개로, 플레이거ㅏ 바라보는 방향 ( 마우스 시점 ) 
	FQuat ControlRotation;
};	