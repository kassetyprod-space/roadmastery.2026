// Сохранение: параметры партии, положение и состояние машины, деньги, время.
#pragma once

#include "CoreMinimal.h"
#include "Game/PeregonGameInstance.h"
#include "GameFramework/SaveGame.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "PeregonSaveGame.generated.h"

UCLASS()
class PEREGON_API UPeregonSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const FString SlotName;

	UPROPERTY() int32 Version = 3;
	UPROPERTY() FPeregonRunConfig Config;
	UPROPERTY() FTransform CarTransform;
	UPROPERTY() FPeregonCarState CarState;
	UPROPERTY() float Money = 0.f;
	UPROPERTY() float TimeOfDayHours = 10.f;
	UPROPERTY() int32 Day = 1;
	UPROPERTY() float RouteS = 0.f;
	UPROPERTY() FDateTime SavedAt;
	UPROPERTY() FString PlaceName;
	UPROPERTY() FPeregonContainer Pockets;
	UPROPERTY() FPeregonContainer Glovebox;
	UPROPERTY() FPeregonContainer Trunk;
	UPROPERTY() float TraveledMaxS = 0.f;
	// С версии 3: погода и время года.
	UPROPERTY() FPeregonClimate Climate;
};
