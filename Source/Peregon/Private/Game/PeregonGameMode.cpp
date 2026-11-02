#include "Game/PeregonGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/PeregonHUD.h"
#include "GameFramework/PlayerStart.h"
#include "Player/PeregonCharacter.h"
#include "Vehicle/PeregonCar.h"
#include "World/PeregonProvingGround.h"

APeregonGameMode::APeregonGameMode()
{
	// Игрок начинает пешком рядом с машиной: подойти, открыть дверь, сесть, завести.
	DefaultPawnClass = APeregonCharacter::StaticClass();
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
	// Точка старта: PlayerStart уровня, иначе — начало прямой полигона.
	FTransform Start(FRotator::ZeroRotator, FVector(0.f, 0.f, 200.f));
	TActorIterator<APlayerStart> PlayerStart(GetWorld());
	if (PlayerStart)
	{
		Start = PlayerStart->GetActorTransform();
	}
	else if (ProvingGround)
	{
		Start = ProvingGround->GetSpawnTransform();
	}

	// Машина стоит в 3.5 м справа от игрока, мордой туда же, куда он смотрит.
	if (!TActorIterator<APeregonCar>(GetWorld()))
	{
		const FRotator Facing(0.f, Start.Rotator().Yaw, 0.f);
		const FVector CarLocation = Start.GetLocation() + Facing.RotateVector(FVector(0.f, 350.f, 0.f)) + FVector(0.f, 0.f, 20.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<APeregonCar>(APeregonCar::StaticClass(), FTransform(Facing, CarLocation), Params);
	}

	RestartPlayerAtTransform(NewPlayer, Start);
}
