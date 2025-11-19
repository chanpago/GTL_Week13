#pragma once
#include "Pawn.h"
#include "ACharacter.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UCharacterMovementComponent;
 
UCLASS(DisplayName = "캐릭터", Description = "캐릭터 액터")
class ACharacter : public APawn
{ 
public:
	GENERATED_REFLECTION_BODY()

	ACharacter();
	virtual ~ACharacter() override;

	virtual void Tick(float DeltaSecond) override;
	virtual void BeginPlay() override;

	// 캐릭터 고유 기능
	virtual void Jump();
	virtual void StopJumping();
	 
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
	UCharacterMovementComponent* GetCharacterMovement() const { return CharacterMovement; }

	// APawn 인터페이스: 파생 클래스의 MovementComponent를 노출
	virtual UPawnMovementComponent* GetMovementComponent() const override { return reinterpret_cast<UPawnMovementComponent*>(CharacterMovement); }

	//APawn에서 정의 됨
	USkeletalMeshComponent* GetMesh() const { return SkeletalMeshComp; }

protected:
	UCapsuleComponent* CapsuleComponent;
	UCharacterMovementComponent* CharacterMovement;

};
