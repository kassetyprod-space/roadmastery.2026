// Живой трафик Р-255: легковушки, «Нивы», «буханки», грузовики, автобусы — попутные и встречные.
// Машины кинематические (не физика): едут по своей полосе, держат дистанцию, тормозят на поворотах и в сёлах,
// встают за игроком и обгоняют его по встречке, когда она свободна. Рождаются за пределами взгляда
// (~1 км впереди и сзади) и исчезают, когда отстали. Ночью горят фары и фонари.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeregonTraffic.generated.h"

class APeregonRoadWorld;
class UBoxComponent;
class UProceduralMeshComponent;

UCLASS()
class PEREGON_API APeregonTrafficCar : public AActor
{
	GENERATED_BODY()

public:
	APeregonTrafficCar();
	// Kind: 0 — легковая, 1 — «Нива», 2 — «буханка», 3 — грузовик, 4 — автобус.
	void Build(int32 Kind, const FLinearColor& Color, uint32 Seed);
	float GetLengthCm() const { return LengthCm; }
	float GetHalfHeightCm() const { return HalfHeightCm; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Peregon") TObjectPtr<UBoxComponent> Collision;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> BodyMesh;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> LampMesh;
	float LengthCm = 420.f;
	float HalfHeightCm = 80.f;
};

UCLASS()
class PEREGON_API APeregonTraffic : public AActor
{
	GENERATED_BODY()

public:
	APeregonTraffic();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	TWeakObjectPtr<APeregonRoadWorld> RoadWorld;

private:
	struct FMover
	{
		TWeakObjectPtr<APeregonTrafficCar> Actor;
		double S = 0.0;        // м по трассе
		int32 Dir = 1;         // 1 — попутная, −1 — встречная
		double Speed = 20.0;   // м/с
		double Cruise = 22.0;  // желаемая скорость
		double Lane = 1.75;    // текущее смещение от оси, м (со знаком)
		double LaneTarget = 1.75;
		double Length = 4.2;   // м
		float Blocked = 0.f;   // сколько секунд упёрлись
		bool bPassedPlayer = false;
	};
	void Spawn(double PlayerS, bool bInitial);
	TArray<FMover> Movers;
	float SpawnTimer = 0.f;
	uint32 Counter = 1;
};
