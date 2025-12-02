#include "pch.h"
#include "CharacterMovementComponent.h"
#include "Character.h"
#include "SceneComponent.h"
#include "CapsuleComponent.h"
#include "World.h"
#include "Source/Runtime/Engine/Physics/PhysScene.h"
#include "Source/Runtime/Engine/Collision/Collision.h"

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

	// Sweep 검사를 통한 안전한 이동
	if (!DeltaLoc.IsZero())
	{
		FHitResult Hit;
		bool bMoved = SafeMoveUpdatedComponent(DeltaLoc, Hit);

		// 충돌 시 슬라이딩 처리
		if (!bMoved && Hit.bBlockingHit)
		{
			FVector SlideVector = SlideAlongSurface(DeltaLoc, Hit);
			if (!SlideVector.IsZero())
			{
				FHitResult SlideHit;
				SafeMoveUpdatedComponent(SlideVector, SlideHit);
			}
		}
	}

	// 바닥 검사
	FHitResult FloorHit;
	if (!CheckFloor(FloorHit))
	{
		bIsFalling = true;
	}
}

void UCharacterMovementComponent::PhysFalling(float DeltaSecond)
{
	// 중력 적용
	float ActualGravity = GLOBAL_GRAVITY_Z * GravityScale;
	Velocity.Z += ActualGravity * DeltaSecond;

	// 위치 이동 (Sweep 검사)
	FVector DeltaLoc = Velocity * DeltaSecond;

	FHitResult Hit;
	bool bMoved = SafeMoveUpdatedComponent(DeltaLoc, Hit);

	// 충돌 처리
	if (!bMoved && Hit.bBlockingHit)
	{
		// 바닥에 착지했는지 확인 (충돌 노말이 위를 향하면 바닥)
		if (Hit.ImpactNormal.Z > 0.7f)
		{
			// 착지
			Velocity.Z = 0.0f;
			bIsFalling = false;

			// 충돌 지점으로 위치 조정
			UpdatedComponent->SetWorldLocation(Hit.Location);
		}
		else
		{
			// 벽에 부딪힘 - 슬라이딩
			FVector SlideVector = SlideAlongSurface(DeltaLoc, Hit);
			if (!SlideVector.IsZero())
			{
				FHitResult SlideHit;
				SafeMoveUpdatedComponent(SlideVector, SlideHit);
			}
		}
	}
	else
	{
		// 바닥 검사
		FHitResult FloorHit;
		if (CheckFloor(FloorHit))
		{
			// 바닥에 닿음
			Velocity.Z = 0.0f;
			bIsFalling = false;

			// 바닥으로 스냅 (SkinWidth 여유를 두고 이동)
			const float SkinWidth = 0.04f;
			float SnapDistance = FloorHit.Distance - SkinWidth;
			if (SnapDistance > KINDA_SMALL_NUMBER)
			{
				FVector CurrentLoc = UpdatedComponent->GetWorldLocation();
				CurrentLoc.Z -= SnapDistance;
				UpdatedComponent->SetWorldLocation(CurrentLoc);
			}
		}
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

bool UCharacterMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, FHitResult& OutHit)
{
	OutHit.Reset();

	if (!UpdatedComponent || Delta.IsZero())
		return true;

	FPhysScene* PhysScene = GetPhysScene();
	if (!PhysScene)
	{
		// PhysScene이 없으면 그냥 이동
		UE_LOG("[CharacterMovement] PhysScene is null - moving without sweep");
		UpdatedComponent->AddRelativeLocation(Delta);
		return true;
	}

	float Radius, HalfHeight;
	GetCapsuleSize(Radius, HalfHeight);

	FVector Start = UpdatedComponent->GetWorldLocation();
	FVector End = Start + Delta;

	// 자기 자신은 무시
	AActor* OwnerActor = CharacterOwner;

	bool bHit = PhysScene->SweepCapsule(Start, End, Radius, HalfHeight, OutHit, OwnerActor);

	// 디버깅 로그
	if (bHit)
	{
		UE_LOG("[CharacterMovement] Sweep HIT! Distance: %.3f, Normal: (%.2f, %.2f, %.2f)",
			OutHit.Distance, OutHit.ImpactNormal.X, OutHit.ImpactNormal.Y, OutHit.ImpactNormal.Z);
	}

	if (bHit && OutHit.bBlockingHit)
	{
		// 충돌 지점 직전까지만 이동 (약간의 여유 거리)
		const float SkinWidth = 0.01f;
		float SafeDistance = FMath::Max(0.0f, OutHit.Distance - SkinWidth);
		FVector SafeLocation = Start + Delta.GetNormalized() * SafeDistance;
		UpdatedComponent->SetWorldLocation(SafeLocation);
		return false;
	}
	else
	{
		// 충돌 없음 - 전체 이동
		UpdatedComponent->AddRelativeLocation(Delta);
		return true;
	}
}

FVector UCharacterMovementComponent::SlideAlongSurface(const FVector& Delta, const FHitResult& Hit)
{
	if (!Hit.bBlockingHit)
		return Delta;

	// 충돌 노말에 수직인 방향으로 슬라이딩
	FVector Normal = Hit.ImpactNormal;

	// Delta에서 노말 방향 성분을 제거
	float DotProduct = FVector::Dot(Delta, Normal);
	FVector SlideVector = Delta - Normal * DotProduct;

	return SlideVector;
}

bool UCharacterMovementComponent::CheckFloor(FHitResult& OutHit)
{
	OutHit.Reset();

	if (!UpdatedComponent)
		return false;

	FPhysScene* PhysScene = GetPhysScene();
	if (!PhysScene)
	{
		// PhysScene이 없으면 임시로 Z=0을 바닥으로 취급
		UE_LOG("[CharacterMovement] CheckFloor: PhysScene is null, using Z=0 fallback");
		return UpdatedComponent->GetWorldLocation().Z <= 0.001f;
	}

	float Radius, HalfHeight;
	GetCapsuleSize(Radius, HalfHeight);

	FVector Start = UpdatedComponent->GetWorldLocation();
	// 바닥 검사는 캡슐 바닥에서 약간 아래로 Sweep
	const float FloorCheckDistance = 0.5f;  // 검사 거리 증가
	FVector End = Start - FVector(0, 0, FloorCheckDistance);

	AActor* OwnerActor = CharacterOwner;

	bool bHit = PhysScene->SweepCapsule(Start, End, Radius, HalfHeight, OutHit, OwnerActor);

	// 디버깅 로그
	UE_LOG("[CharacterMovement] CheckFloor: Capsule(R=%.2f, H=%.2f), Start=(%.2f, %.2f, %.2f), Hit=%s",
		Radius, HalfHeight, Start.X, Start.Y, Start.Z, bHit ? "YES" : "NO");

	// 바닥으로 인정하려면 노말이 위를 향해야 함
	if (bHit && OutHit.ImpactNormal.Z > 0.7f)
	{
		return true;
	}

	return false;
}

void UCharacterMovementComponent::GetCapsuleSize(float& OutRadius, float& OutHalfHeight) const
{
	// 기본값
	OutRadius = 0.5f;
	OutHalfHeight = 1.0f;

	if (CharacterOwner)
	{
		if (UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent())
		{
			// 월드 스케일 적용 (디버그 렌더링과 일치시키기 위함)
			const FTransform WorldTransform = Capsule->GetWorldTransform();
			const float AbsScaleX = std::fabs(WorldTransform.Scale3D.X);
			const float AbsScaleY = std::fabs(WorldTransform.Scale3D.Y);
			const float AbsScaleZ = std::fabs(WorldTransform.Scale3D.Z);

			OutRadius = Capsule->CapsuleRadius * FMath::Max(AbsScaleX, AbsScaleY);
			OutHalfHeight = Capsule->CapsuleHalfHeight * AbsScaleZ;
		}
	}
}

FPhysScene* UCharacterMovementComponent::GetPhysScene() const
{
	if (CharacterOwner)
	{
		if (UWorld* World = CharacterOwner->GetWorld())
		{
			return World->GetPhysScene();
		}
	}
	return nullptr;
}
