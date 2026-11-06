// Сюжетное начало: двор у начала маршрута (справа от Р-255, rgen::StartYard*).
// Пятиэтажка (игроки выходят из подъезда), гаражный кооператив с «Соколом» без половины деталей,
// помойка (бесплатный хлам: старый аккумулятор, свечи, фильтр, шпаклёвка) и разборка (б/у детали за деньги,
// там же брошенный второй «Сокол» — донор или машина для колонны).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeregonGarageYard.generated.h"

class APeregonRoadWorld;

UCLASS()
class PEREGON_API APeregonGarageYard : public AActor
{
	GENERATED_BODY()

public:
	APeregonGarageYard();

	// Построить двор вдоль дороги. Seed — для выбора хлама и его состояния.
	void Build(const APeregonRoadWorld* World, int32 Seed);

	// Где стоит машина у гаража, где брошенный донор и откуда выходит игрок (у подъезда).
	FTransform GetCarSpot() const { return CarSpot; }
	FTransform GetDonorSpot() const { return DonorSpot; }
	FTransform GetSpawnSpot(int32 Player = 0) const;

private:
	FVector At(double S, double T, double Lift = 0.0) const;
	FRotator Facing(double S, double YawFromRoadDeg) const;
	void Loot(double S, double T, FName Id, float Condition, float Price = 0.f, float Charge = -1.f);

	UPROPERTY(VisibleAnywhere, Category = "Peregon") TObjectPtr<USceneComponent> Root;
	TArray<TWeakObjectPtr<UActorComponent>> Spawned;
	const APeregonRoadWorld* RoadWorld = nullptr;
	FTransform CarSpot;
	FTransform DonorSpot;
	FTransform Spawn;
};
