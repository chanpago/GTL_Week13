#pragma once
#include "PawnMovementComponent.h"
#include "UCharacterMovementComponent.generated.h"

class ACharacter;
struct FHitResult;
struct FPhysScene;
 
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

	/**
	 * @brief Sweep 검사를 통해 안전한 이동 수행
	 * @param Delta 이동할 거리
	 * @param OutHit 충돌 결과
	 * @return 실제 이동 완료 여부 (충돌 시 false)
	 */
	bool SafeMoveUpdatedComponent(const FVector& Delta, FHitResult& OutHit);

	/**
	 * @brief 충돌 후 슬라이딩 처리
	 * @param Delta 원래 이동 벡터
	 * @param Hit 충돌 정보
	 * @return 슬라이딩 후 남은 이동 벡터
	 */
	FVector SlideAlongSurface(const FVector& Delta, const FHitResult& Hit);

	/**
	 * @brief 바닥 검사 (아래로 Sweep)
	 * @param OutHit 바닥 충돌 결과
	 * @return 바닥에 서있는지 여부
	 */
	bool CheckFloor(FHitResult& OutHit);

	/** 캡슐 컴포넌트 크기 가져오기 */
	void GetCapsuleSize(float& OutRadius, float& OutHalfHeight) const;

	/** PhysScene 가져오기 */
	FPhysScene* GetPhysScene() const;

protected:
	ACharacter* CharacterOwner = nullptr;
	bool bIsFalling = false;

	const float GLOBAL_GRAVITY_Z = -9.8f;
	const float GravityScale = 1.0f;


};
