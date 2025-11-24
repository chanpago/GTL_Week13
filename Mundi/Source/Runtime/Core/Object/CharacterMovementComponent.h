#pragma once
#include "PawnMovementComponent.h"
#include "UCharacterMovementComponent.generated.h"

class ACharacter;
 
class UCharacterMovementComponent : public UPawnMovementComponent
{	
public:
	GENERATED_REFLECTION_BODY()

	UCharacterMovementComponent();
	virtual ~UCharacterMovementComponent() override;

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaSeconds) override;

	// 캐릭터 전용 설정 값
	float MaxWalkSpeed;
	float MaxAcceleration;
	float JumpZVelocity; 

	float BrackingDeceleration; // 입력이 없을 때 감속도
	float GroundFriction; //바닥 마찰 계수 

	//TODO
	//float MaxWalkSpeedCrouched = 6.0f;
	//float MaxSwimSpeed;
	//float MaxFlySpeed;

	// 상태 제어 
	void DoJump();
	void StopJump();
	bool IsFalling() const { return bIsFalling; }

protected:
	void PhysWalking(float DeltaSecond);
	void PhysFalling(float DeltaSecond);

	void CalcVelocity(const FVector& Input, float DeltaSecond, float Friction, float BrackingDecel);
protected:
	ACharacter* CharacterOwner = nullptr;
	bool bIsFalling = false;

	const float GLOBAL_GRAVITY_Z = -9.8f;
	const float GravityScale = 1.0f;


};
