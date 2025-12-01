#include "pch.h"
#include "GameModeBase.h" 
#include "PlayerController.h"
#include "Pawn.h"
#include "World.h"
#include "CameraComponent.h"
#include "PlayerCameraManager.h"
#include "Character.h"
#include "Source/Runtime/Core/Misc/PathUtils.h"

AGameModeBase::AGameModeBase()
{
	//DefaultPawnClass = APawn::StaticClass();
	DefaultPawnClass = ACharacter::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}

AGameModeBase::~AGameModeBase()
{
}

void AGameModeBase::StartPlay()
{
	//TODO 
	//GameState 세팅 
	Login();
	PostLogin(PlayerController);
}

APlayerController* AGameModeBase::Login()
{
	if (PlayerControllerClass)
	{
		PlayerController = Cast<APlayerController>(GWorld->SpawnActor(PlayerControllerClass, FTransform())); 
	}
	else
	{
		PlayerController = GWorld->SpawnActor<APlayerController>(FTransform());
	}

	return PlayerController;
}

void AGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	// 스폰 위치 찾기
	AActor* StartSpot = FindPlayerStart(NewPlayer);
	APawn* NewPawn = NewPlayer->GetPawn();

	// Pawn이 없으면 생성
    if (!NewPawn && NewPlayer)
    {
        // Try spawning a default prefab as the player's pawn first
        {
            FWideString PrefabPath = UTF8ToWide(GDataDir) + L"/Prefabs/FutureEmptyGraph.prefab";
            if (AActor* PrefabActor = GWorld->SpawnPrefabActor(PrefabPath))
            {
                if (APawn* PrefabPawn = Cast<APawn>(PrefabActor))
                {
                    NewPawn = PrefabPawn;
                }
            }
        }

        // Fallback to default pawn class if prefab did not yield a pawn
        if (!NewPawn)
        {
            NewPawn = SpawnDefaultPawnFor(NewPlayer, StartSpot);
        }

        if (NewPawn)
        {
            NewPlayer->Possess(NewPawn);
        }

    }

	// 카메라가 없으면 카메라 부착
	if (NewPawn && !NewPawn->GetComponent(UCameraComponent::StaticClass()))
	{
		if (UActorComponent* ActorComponent = NewPawn->AddNewComponent(UCameraComponent::StaticClass(), NewPawn->GetRootComponent())) {
			auto* Camera = Cast<UCameraComponent>(ActorComponent);
			Camera->SetRelativeLocation(FVector(-4, 0, 2.7));
			Camera->SetRelativeRotationEuler(FVector(0, 14, 0));
		}
	}

	if (auto* PCM = GWorld->GetPlayerCameraManager())
	{
		if (auto* Camera = Cast<UCameraComponent>(NewPawn->GetComponent(UCameraComponent::StaticClass())))
		{
			PCM->SetViewCamera(Camera);
		}
	}

	// Possess를 수행 
	if (NewPlayer)
	{
		NewPlayer->Possess(NewPlayer->GetPawn());
	} 
}

APawn* AGameModeBase::SpawnDefaultPawnFor(AController* NewPlayer, AActor* StartSpot)
{
	// 위치 결정
	FVector SpawnLoc = FVector::Zero();
	FQuat SpawnRot = FQuat::Identity();

	if (StartSpot)
	{
		SpawnLoc = StartSpot->GetActorLocation();
		SpawnRot = StartSpot->GetActorRotation();
	}
	 
	APawn* ResultPawn = nullptr;
	if (DefaultPawnClass)
	{
		ResultPawn = Cast<APawn>(GetWorld()->SpawnActor(DefaultPawnClass, FTransform(SpawnLoc, SpawnRot, FVector(1, 1, 1))));
	}

	return ResultPawn;
}

AActor* AGameModeBase::FindPlayerStart(AController* Player)
{
	// TODO:
	// PlayerStart Actor를 찾아서, 그 위치를 가져와야 함
	// 현재는 0,0,0에 임시로 생성 중 

	AActor* Spot = GWorld->SpawnActor<AActor>();
	Spot->SetActorLocation(FVector(0, 0, 0));

	return Spot;
}
  
