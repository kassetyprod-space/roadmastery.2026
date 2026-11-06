// Небо, свет и погода: солнце и луна по настоящей дате (55° с. ш.), атмосфера, облака, туман,
// небесный свет, экспозиция, звёзды. Погода и времена года — в rgen::Climate, состояние — в GameInstance.
// Осадки, листопад и звуки природы — в APeregonAmbience (его создаёт небо).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "rgen/Climate.h"
#include "PeregonSky.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class APeregonAmbience;
class APostProcessVolume;
class ASkyLight;
class AVolumetricCloud;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;

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
	// 0 — день, 1 — глубокая ночь (для окон, фонарей, звёзд).
	float GetNight() const { return Night; }
	float GetSunElevation() const { return SunElevation; }
	const rgen::WeatherMix& GetWeather() const { return Mix; }
	const rgen::SeasonLook& GetSeason() const { return Season; }
	float GetTemperature() const { return Temperature; }
	// Ветер у земли, м/с, вектор «куда дует».
	FVector GetWind() const { return Wind; }
	float GetDayOfYear() const;
	FString DescribeWeather() const;
	FString DescribeDate() const;

	// Отладка и проверка: следующая погода, перемотка даты.
	void CycleWeather();
	void AdvanceDays(int32 Days);

	// Остановить ход времени (главное меню).
	UPROPERTY(EditAnywhere, Category = "Peregon")
	bool bFreezeTime = false;

private:
	void ApplyTime(float Hours);
	void SetupClouds();
	void BuildSkyMeshes();
	void UpdateSkyMeshes(const FVector& Camera, float DeltaSeconds);

	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Moon;
	UPROPERTY(Transient) TObjectPtr<AExponentialHeightFog> Fog;
	UPROPERTY(Transient) TObjectPtr<ASkyLight> SkyLight;
	UPROPERTY(Transient) TObjectPtr<AVolumetricCloud> Cloud;
	UPROPERTY(Transient) TObjectPtr<APostProcessVolume> Post;
	UPROPERTY(Transient) TObjectPtr<APeregonAmbience> Ambience;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CloudMid;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Spawned;

	// PSX-облака: слои-«потолки» с текстурой облаков, плывут по ветру; сплошная пелена — в пасмурно.
	UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> CloudLayers;
	// Живописный пост-эффект (создаёт модуль PeregonEditor); нет материала — без него.
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PaintedPost;
	bool bPaintedTried = false;
	UMaterialInstanceDynamic* GetPaintedPost();
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> CloudLayerMids;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Stars;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> StarsMid;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> MoonDisc;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MoonMid;

	FName CloudWindParam;
	TArray<TPair<FName, float>> CloudCoverParams; // имя → значение по умолчанию
	FVector2D CloudDrift[5] = {FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector};

	rgen::WeatherMix Mix;
	rgen::SeasonLook Season;
	FVector Wind = FVector::ZeroVector;
	FVector MoonDir = FVector::UpVector;
	float Night = 0.f;
	float SunElevation = 30.f;
	float Temperature = 10.f;
	float MoonPhase = 0.5f;
};
