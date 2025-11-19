#pragma once
#include "Actor.h"
#include "Enums.h"
#include "APawn.generated.h"

class AController;
class USkeletalMeshComponent;
class UPawnMovementComponent;

UCLASS(DisplayName = "폰", Description = "폰 액터")
class APawn : public AActor
{
public:
	GENERATED_REFLECTION_BODY()

	APawn();
	virtual ~APawn() override;
	
	virtual void Tick(float DeltaTime) override;
	virtual void BeginPlay() override;

	// 컨트롤러가 이 폰을 제어하도록 설정
	virtual void PossessedBy(AController* NewController);
	virtual void UnPossessed();

	// 이동 입력을 추가 
	virtual void AddMovementInput(FVector Direction, float Scale = 1.0f);
	
	// 현재 이동 입력 벡터 가져오기 
	FVector ConsumeMovementInputVector();
	
	float GetVelocity() { return Velocity; }
	AController* GetController() { return Controller; }

    // Movement component access (virtual so derived types can supply their own)
    virtual UPawnMovementComponent* GetMovementComponent() const { return PawnMovementComponent; }
protected:
	
	// 현재 Pawn을 컨트롤하는 컨트롤러 
	AController* Controller = nullptr;

	// 이번 프레임에 누전된 이동 벡터
	FVector InternalMovementInputVector; 

	USkeletalMeshComponent* SkeletalMeshComp = nullptr;
	
	UPawnMovementComponent* PawnMovementComponent = nullptr;

	float Velocity = 10.0f; 
};
