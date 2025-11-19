#include "pch.h"
#include "CharacterMovementComponent.h"
#include "Character.h"
#include "SceneComponent.h"

UCharacterMovementComponent::UCharacterMovementComponent()
{
	// 캐릭터 전용 설정 값
 	MaxWalkSpeed = 6.0f;
	MaxAcceleration = 20.0f;
	JumpZVelocity = 4.0;

	BrackingDeceleration = 20.0f; // 입력이 없을 때 감속도
	GroundFriction = 8.0f; //바닥 마찰 계수 
}

UCharacterMovementComponent::~UCharacterMovementComponent()
{
}

void UCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();

	CharacterOwner = Cast<ACharacter>(GetOwner());
}

void UCharacterMovementComponent::TickComponent(float DeltaSeconds)
{
	//Super::TickComponent(DeltaSeconds);

	if (!UpdatedComponent || !CharacterOwner) return;

	if (bIsFalling)
	{
		PhysFalling(DeltaSeconds);
	}
	else
	{
		PhysWalking(DeltaSeconds);
	}
}

void UCharacterMovementComponent::DoJump()
{
	if (!bIsFalling)
	{
			Velocity.Z = JumpZVelocity;
		bIsFalling = true;
	}
}

void UCharacterMovementComponent::StopJump()
{
	//if (bIsFalling && Velocity.Z > 0.0f)
	//{
	//	Velocity.Z *= 0.5f;
	//}
}

void UCharacterMovementComponent::PhysWalking(float DeltaSecond)
{
	// 입력 벡터 가져오기
	FVector InputVector = CharacterOwner->ConsumeMovementInputVector();

	// z축 입력은 걷기에서 무시
	InputVector.Z = 0.0f;
	if (!InputVector.IsZero())
	{
		InputVector.Normalize();
	}

	// 속도 계산 
	CalcVelocity(InputVector, DeltaSecond, GroundFriction, BrackingDeceleration);

	if (!CharacterOwner || !CharacterOwner->GetController())
	{
		Acceleration = FVector::Zero();
		Velocity = FVector::Zero();
		return;
	}

	FVector DeltaLoc = Velocity * DeltaSecond;
	if (!InputVector.IsZero())
	{   
		UpdatedComponent->AddRelativeLocation(DeltaLoc);
	}

	// 바닥처리는 어떻게 하지?
	// 일단 충돌하면 isfalling이 false?
	// 임시로 z가 0일 때로 적용 
	if (UpdatedComponent->GetWorldLocation().Z > 0.001f)
	{
		bIsFalling = true;
	}
}

void UCharacterMovementComponent::PhysFalling(float DeltaSecond)
{
	// 중력 적용
	float ActualGravity = GLOBAL_GRAVITY_Z *  GravityScale;
	Velocity.Z += ActualGravity * DeltaSecond;

	// 위치 이동 
	FVector DeltaLoc = Velocity * DeltaSecond;
	UpdatedComponent->AddRelativeLocation(DeltaLoc);
	
	// 착지 체크 
	if (UpdatedComponent->GetWorldLocation().Z <= 0.0f)
	{
		FVector NewLoc = UpdatedComponent->GetWorldLocation();
		NewLoc.Z = 0.0f;
		UpdatedComponent->SetWorldLocation(NewLoc);

		Velocity.Z = 0.0f;
		bIsFalling = false;
	}  
}

void UCharacterMovementComponent::CalcVelocity(const FVector& Input, float DeltaSecond, float Friction, float BrackingDecel)
{
	//  Z축을 속도에서 제외
	FVector CurrentVelocity = Velocity;
	CurrentVelocity.Z = 0.0f;

	// 입력이 있는 지 확인
	bool bHasInput = !Input.IsZero();

	// 입력이 있으면 가속 
	if (bHasInput)
	{
		FVector AccelerationVec = Input * MaxAcceleration;

		CurrentVelocity += AccelerationVec * DeltaSecond;

		if (CurrentVelocity.Size() > MaxWalkSpeed)
		{
			CurrentVelocity = CurrentVelocity.GetNormalized() * MaxWalkSpeed;
		}
	}

	// 입력이 없으면 감속 
	else
	{
		float CurrentSpeed = CurrentVelocity.Size();
		if (CurrentSpeed > 0.0f)
		{
			float DropAmount = BrackingDecel * DeltaSecond;

			//속도가 0 아래로 내려가지 않도록 방어처리 
			float NewSpeed = FMath::Max(CurrentSpeed -  DropAmount, 0.0f);
			
			CurrentVelocity = CurrentVelocity.GetNormalized() * NewSpeed;
		}  
	}

	Velocity.X = CurrentVelocity.X;
	Velocity.Y = CurrentVelocity.Y;
}
