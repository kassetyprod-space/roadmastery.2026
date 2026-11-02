#include "Game/PeregonGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/PeregonHUD.h"
#include "GameFramework/PlayerStart.h"
#include "Vehicle/PeregonCar.h"
#include "World/PeregonProvingGround.h"

APeregonGameMode::APeregonGameMode()
{
	DefaultPawnClass = APeregonCar::StaticClass();
	HUDClass = APeregonHUD::StaticClass();
}

void APeregonGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	TActorIterator<APeregonProvingGround> Existing(GetWorld());
	if (Existing)
	{
		ProvingGround = *Existing;
	}
	else if (bSpawnProvingGround)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ProvingGround = GetWorld()->SpawnActor<APeregonProvingGround>(APeregonProvingGround::StaticClass(), FTransform::Identity, Params);
	}
}

void APeregonGameMode::RestartPlayer(AController* NewPlayer)
{
	// На пустом уровне нет PlayerStart — ставим машину в начало прямой полигона.
	if (ProvingGround && !TActorIterator<APlayerStart>(GetWorld()))
	{
		RestartPlayerAtTransform(NewPlayer, ProvingGround->GetSpawnTransform());
		return;
	}
	Super::RestartPlayer(NewPlayer);
}
