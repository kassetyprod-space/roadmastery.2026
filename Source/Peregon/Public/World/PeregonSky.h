// Небо и смена дня и ночи: солнце, луна, атмосфера, объёмные облака, туман, небесный свет.
// Время суток хранится в GameInstance (переживает смену режима и попадает в сохранение).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeregonSky.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class ASkyLight;

UCLASS()
class PEREGON_API APeregonSky : public AActor
{
	GENERATED_BODY()

public:
	APeregonSky();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// 0..24
	float GetTimeOfDay() const;
	bool IsNight() const;

	// Остановить ход времени (главное меню).
	UPROPERTY(EditAnywhere, Category = "Peregon")
	bool bFreezeTime = false;

private:
	void ApplyTime(float Hours);

	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Moon;
	UPROPERTY(Transient) TObjectPtr<AExponentialHeightFog> Fog;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Spawned;
};
