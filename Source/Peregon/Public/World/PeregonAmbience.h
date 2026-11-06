// Жизнь вокруг игрока: дождь и снег (сетка штрихов вокруг камеры, «падает» сдвигом и не идёт в салоне),
// листопад у деревьев, шум дождя и ветра. Параметры — от неба (APeregonSky).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PeregonAmbience.generated.h"

class APeregonRoadWorld;
class APeregonSky;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPeregonSynth;
class UProceduralMeshComponent;

UCLASS()
class PEREGON_API APeregonAmbience : public AActor
{
	GENERATED_BODY()

public:
	APeregonAmbience();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Transient)
	TObjectPtr<APeregonSky> Sky;

private:
	struct FLeaf
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		FRotator Spin = FRotator::ZeroRotator;
		float GroundZ = 0.f;
		float Rest = 0.f;  // сколько ещё лежит на земле
		float Phase = 0.f;
		bool bActive = false;
		bool bLanded = false;
	};

	UProceduralMeshComponent* BuildPrecipitation(bool bSnow);
	void UpdatePrecipitation(UProceduralMeshComponent* Mesh, UMaterialInstanceDynamic* Mid, FVector2D& Drift, float& Fall, bool bSnow, float Amount,
		const FVector& Camera, float DeltaSeconds);
	void UpdateLeaves(const FVector& Camera, const FVector& CameraVelocity, float DeltaSeconds);
	APeregonRoadWorld* FindRoadWorld();
	bool IsInsideCar() const;

	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Rain;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Snow;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RainMid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SnowMid;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> LeafMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LeafMid;
	UPROPERTY(Transient) TObjectPtr<UPeregonSynth> RainSound;
	UPROPERTY(Transient) TObjectPtr<UPeregonSynth> WindSound;
	TWeakObjectPtr<APeregonRoadWorld> RoadWorld;

	TArray<FLeaf> Leaves;
	TArray<FTransform> LeafTransforms;
	FVector2D RainDrift = FVector2D::ZeroVector;
	FVector2D SnowDrift = FVector2D::ZeroVector;
	float RainFall = 0.f;
	float SnowFall = 0.f;
	float LeafBudget = 0.f;
	FVector LastCamera = FVector::ZeroVector;
	FRandomStream Random;
};
