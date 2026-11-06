// Настоящий свет ночью: на ближайшие к камере фонари ставятся прожекторы вниз (пятно света на асфальте и
// тротуаре, тени от столбов и машины), на горящие окна — прямоугольные источники из фасада наружу
// (тёплые прямоугольники на земле, деревьях и машинах у подъезда). Дальние — только светятся (NightGlow).
// Сколько настоящих источников — Настройки → Графика → «Свет фонарей и окон».
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeregonNightLights.generated.h"

class URectLightComponent;
class USpotLightComponent;

UCLASS()
class PEREGON_API APeregonNightLights : public AActor
{
	GENERATED_BODY()

public:
	APeregonNightLights();
	virtual void Tick(float DeltaSeconds) override;

private:
	void Reassign(const FVector& Camera, float Night);

	UPROPERTY(VisibleAnywhere, Category = "Peregon") TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TArray<TObjectPtr<USpotLightComponent>> Lamps;
	UPROPERTY(Transient) TArray<TObjectPtr<URectLightComponent>> Windows;
	float Timer = 0.f;
	float Fade = 0.f;
};
