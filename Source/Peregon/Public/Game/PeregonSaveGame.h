// Сохранение: параметры партии, положение и состояние машины, деньги, время.
#pragma once

#include "CoreMinimal.h"
#include "Game/PeregonGameInstance.h"
#include "GameFramework/SaveGame.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "PeregonSaveGame.generated.h"

// Машина колонны, кроме той, в которой ехали.
USTRUCT()
struct FPeregonSavedCar
{
	GENERATED_BODY()

	UPROPERTY() FTransform Transform;
	UPROPERTY() FPeregonCarState CarState;
	UPROPERTY() TArray<double> Parts;
	UPROPERTY() FLinearColor Color = FLinearColor::White;
	UPROPERTY() FPeregonContainer Glovebox;
	UPROPERTY() FPeregonContainer Trunk;
};

UCLASS()
class PEREGON_API UPeregonSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const FString SlotName;

	UPROPERTY() int32 Version = 5;
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
	// С версии 4: детали и кузов машины (vsim::parts::Pack) и её цвет.
	UPROPERTY() TArray<double> Parts;
	UPROPERTY() FLinearColor CarColor = FLinearColor(0.32f, 0.04f, 0.04f);
	// С версии 5: вся колонна и бортовой журнал.
	UPROPERTY() TArray<FPeregonSavedCar> Convoy;
	UPROPERTY() FPeregonJournal Journal;
};
