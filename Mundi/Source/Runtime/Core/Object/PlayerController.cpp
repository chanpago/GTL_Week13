#include "pch.h"
#include "PlayerController.h"
#include "Pawn.h"
#include "CameraComponent.h"
#include <windows.h>
#include  "Character.h"

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

		// controller의 회전을  inputDir에  적용시켜준다.
		FMatrix RotMatrix = GetControlRotation().ToMatrix(); 
		FVector WorldDir = RotMatrix.TransformVector(InputDir);
	 	
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

    if (MouseDelta.X != 0.0f || MouseDelta.Y != 0.0f)
    {
        const float Sensitivity = 0.1f;

        FVector Euler = GetControlRotation().ToEulerZYXDeg(); 
        //Yaw 
        Euler.Z += MouseDelta.X * Sensitivity;
        
        //Pitch
        Euler.Y += MouseDelta.Y * Sensitivity;

		Euler.Y = FMath::Clamp(Euler.Y, -89.0f, 89.0f);
		
		//Roll 방지 
		Euler.X = 0.0f;

        FQuat NewControlRotation = FQuat::MakeFromEulerZYX(Euler);
        SetControlRotation(NewControlRotation);

		// Pawn Rotate
		FQuat PawnRotation = FQuat::MakeFromEulerZYX(FVector(0, 0, Euler.Z));
		Pawn->SetActorRotation(PawnRotation);
			
		// Camera Rotate  
        if (UActorComponent* C = Pawn->GetComponent(UCameraComponent::StaticClass()))
        {
            if (UCameraComponent* CamComp = Cast<UCameraComponent>(C))  
            {
                FQuat CameraLocalRot = FQuat::MakeFromEulerZYX(FVector(0.0f, Euler.Y, 0.0f));
                CamComp->SetRelativeRotation(CameraLocalRot);
            }
        }

    } 
}
