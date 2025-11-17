#pragma once
#include "Actor.h"
#include "AGameModeBase.generated.h"

class APawn;
class AController;
class APlayerController;


class AGameModeBase : public AActor
{
public:
	GENERATED_REFLECTION_BODY()

	AGameModeBase();
	~AGameModeBase() override;
	
	APawn* DefaultPawn;
	APlayerController* PlayerController;
	
	UClass* DefaultPawnClass;
	UClass* PlayerControllerClass;

	// TODO: Creat GameState
	// 원래는 GameState가 관리해야되지만, 임시로 GameMode가 관리하도록 만듦
	UClass* GameStateClass;

	// 게임 시작 시 호출
	virtual void StartPlay();
	  

	// 플레이어 리스폰할 때 사용
	//virtual void RestartPlayer(APlayerController* NewPlayer); 		 

protected:
	
	// 플레이어 접속 처리(PlayerController생성)
	virtual APlayerController* Login();

	// 접속 후 폰 스폰 및 빙의
	virtual void PostLogin(APlayerController* NewPlayer);

	// 기본 폰 스폰
	APawn* SpawnDefaultPawnFor(AController* NewPlayer, AActor* StartSpot);

	// 시작 지점 찾기
	AActor* FindPlayerStart(AController* Player); 
};