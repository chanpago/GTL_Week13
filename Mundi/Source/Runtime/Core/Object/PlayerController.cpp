#include "pch.h"
#include "PlayerController.h"
#include "Pawn.h"
#include "CameraComponent.h"
#include "SpringArmComponent.h"
#include <windows.h>
#include <cmath>
#include "Character.h"

APlayerController::APlayerController()
{
}

APlayerController::~APlayerController()
{
}

void APlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

    if (Pawn == nullptr) return;

	// F11을 통해서, Detail Panel 클릭이 가능해짐
    {
        UInputManager& InputManager = UInputManager::GetInstance();
        if (InputManager.IsKeyPressed(VK_F11))
        {
            bMouseLookEnabled = !bMouseLookEnabled;
            if (bMouseLookEnabled)
            {
                InputManager.SetCursorVisible(false);
                InputManager.LockCursor();
                InputManager.LockCursorToCenter();
            }
            else
            {
                InputManager.SetCursorVisible(true);
                InputManager.ReleaseCursor();
            }
        }
    }

	// 입력 처리 (Move)
	ProcessMovementInput(DeltaSeconds);
	  
	// 입력 처리 (Look/Turn)
	ProcessRotationInput(DeltaSeconds);
}

void APlayerController::SetupInput()
{
	// InputManager에 키 바인딩
}

void APlayerController::ProcessMovementInput(float DeltaTime)
{
	FVector InputDir = FVector::Zero();

	// InputManager 사용
	UInputManager& InputManager =  UInputManager::GetInstance();
	if (InputManager.IsKeyDown('W'))
	{
		InputDir.X += 1.0f;
	}
	if (InputManager.IsKeyDown('S'))
	{
		InputDir.X -= 1.0f;
	}
	if (InputManager.IsKeyDown('D'))
	{
		InputDir.Y += 1.0f;
	}
	if (InputManager.IsKeyDown('A'))
	{
		InputDir.Y -= 1.0f;
	}

	if (!InputDir.IsZero())
	{
		InputDir.Normalize();

		// 카메라(ControlRotation) 기준으로 월드 이동 방향 계산
		FVector ControlEuler = GetControlRotation().ToEulerZYXDeg();
		FQuat YawOnlyRotation = FQuat::MakeFromEulerZYX(FVector(0.0f, 0.0f, ControlEuler.Z));
		FVector WorldDir = YawOnlyRotation.RotateVector(InputDir);
		WorldDir.Z = 0.0f; // 수평 이동만
		WorldDir.Normalize();

		// 이동 방향으로 캐릭터 회전 (목표 방향까지만)
		float TargetYaw = std::atan2(WorldDir.Y, WorldDir.X) * (180.0f / PI);
		FQuat TargetRotation = FQuat::MakeFromEulerZYX(FVector(0.0f, 0.0f, TargetYaw));

		// 부드러운 회전 (보간) - 목표에 도달하면 멈춤
		FQuat CurrentRotation = Pawn->GetActorRotation();
		FQuat NewRotation = FQuat::Slerp(CurrentRotation, TargetRotation, FMath::Clamp(DeltaTime * 3.0f, 0.0f, 1.0f));
		Pawn->SetActorRotation(NewRotation);

		// 이동 적용
		Pawn->AddMovementInput(WorldDir * (Pawn->GetVelocity() * DeltaTime));
	}

    // 점프 처리
    if (InputManager.IsKeyPressed(VK_SPACE)) {          // 눌린 순간 1회
        if (auto* Character = Cast<ACharacter>(Pawn)) {
            Character->Jump();
        }
    }
    if (InputManager.IsKeyReleased(VK_SPACE)) {         // 뗀 순간 1회 (있다면)
        if (auto* Character = Cast<ACharacter>(Pawn)) {
            Character->StopJumping();
        }
    }
}

void APlayerController::ProcessRotationInput(float DeltaTime)
{
    UInputManager& InputManager = UInputManager::GetInstance();
    if (!bMouseLookEnabled)
        return;

    FVector2D MouseDelta = InputManager.GetMouseDelta();

    // 마우스 입력이 있을 때만 ControlRotation 업데이트
    if (MouseDelta.X != 0.0f || MouseDelta.Y != 0.0f)
    {
        const float Sensitivity = 0.1f;

        FVector Euler = GetControlRotation().ToEulerZYXDeg();
        // Yaw (좌우 회전)
        Euler.Z += MouseDelta.X * Sensitivity;

        // Pitch (상하 회전)
        Euler.Y += MouseDelta.Y * Sensitivity;
        Euler.Y = FMath::Clamp(Euler.Y, -89.0f, 89.0f);

        // Roll 방지
        Euler.X = 0.0f;

        FQuat NewControlRotation = FQuat::MakeFromEulerZYX(Euler);
        SetControlRotation(NewControlRotation);
    }

    // 매 프레임 SpringArm 월드 회전을 ControlRotation으로 동기화 (캐릭터 회전과 독립)
    if (UActorComponent* C = Pawn->GetComponent(USpringArmComponent::StaticClass()))
    {
        if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(C))
        {
            FVector Euler = GetControlRotation().ToEulerZYXDeg();
            FQuat SpringArmRot = FQuat::MakeFromEulerZYX(FVector(0.0f, Euler.Y, Euler.Z));
            SpringArm->SetWorldRotation(SpringArmRot);
        }
    }
}
