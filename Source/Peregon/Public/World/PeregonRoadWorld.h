// Генерируемый мир вдоль трассы. Маршрут (rgen::Route) режется на чанки по 250 м,
// вокруг игрока держится ~1.5 км: полотно, обочины, рельеф, лес, ЛЭП, сёла, АЗС, кафе, знаки.
// Чанки строятся по одному за кадр, чтобы не было рывков.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "rgen/Route.h"
#include "PeregonRoadWorld.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPhysicalMaterial;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM()
enum class EPeregonWorldMat : uint8
{
	Road,
	Shoulder,
	Grass,
	Pad,
	Wood,
	Roof,
	WhitePaint,
	DarkPaint,
	Concrete,
	Glass,
	BirchBark,
	PineBark,
	Needles,
	Leaves,
	Pole,
	Far,
	Count UMETA(Hidden)
};

UCLASS()
class PEREGON_API APeregonRoadWorld : public AActor
{
	GENERATED_BODY()

public:
	APeregonRoadWorld();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void Generate(int32 Seed);
	// Построить все чанки вокруг S сразу (перед появлением игрока).
	void BuildAround(double S);

	const rgen::Route& GetRoute() const { return Route; }
	double GetTrackedS() const { return TrackedS; }
	double FindS(const FVector& WorldLocation, double Hint = -1.0) const;
	double LateralOffset(const FVector& WorldLocation, double S) const;

	// Точка мира (см) по координатам дороги (м). Z — земля + ZOffsetM.
	FVector RouteToWorld(double S, double T, double ZOffsetM = 0.0) const;
	// Положение «на дороге, лицом по ходу» (для машины и игрока).
	FTransform RoadTransform(double S, double T, double ZOffsetM) const;
	// Название ближайшего населённого пункта/места.
	FString GetPlaceName(double S) const;

	UPROPERTY(EditAnywhere, Category = "Peregon|Streaming")
	float ChunkLength = 250.f;

	UPROPERTY(EditAnywhere, Category = "Peregon|Streaming")
	int32 ChunksBehind = 2;

	UPROPERTY(EditAnywhere, Category = "Peregon|Streaming")
	int32 ChunksAhead = 4;

	// В главном меню — не показывать уведомления о въезде в сёла.
	UPROPERTY(EditAnywhere, Category = "Peregon")
	bool bAnnouncePlaces = true;

private:
	struct FChunk
	{
		TArray<TWeakObjectPtr<UActorComponent>> Components;
		TArray<TWeakObjectPtr<AActor>> Actors;
	};

	struct FMeshBuffers
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
	};

	void SetupAssets();
	FVector FocusLocation() const;
	void UpdateStreaming(bool bBuildAll);
	void BuildChunk(int32 Index);
	void DestroyChunk(int32 Index);
	void AnnouncePlaces(double PrevS, double NewS);

	// Сетка (S × T) → треугольники. Pos(s, t) возвращает точку в см, UV — свою.
	void BuildGrid(FMeshBuffers& Out, const TArray<double>& Ss, const TArray<double>& Ts, double ZOffsetM, double UvTileM, bool bRoadUV) const;
	UProceduralMeshComponent* AddSurface(FChunk& Chunk, const FMeshBuffers& Data, EPeregonWorldMat Mat, UPhysicalMaterial* Phys);
	UStaticMeshComponent* AddMesh(FChunk& Chunk, UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material, bool bCollision);
	UInstancedStaticMeshComponent* AddInstanced(FChunk& Chunk, UStaticMesh* Mesh, EPeregonWorldMat Mat, bool bCollision);
	void AddSign(FChunk& Chunk, double S, double T, const FString& Text, bool bCrossed, bool bFacingDriver = true);

	void BuildVillage(FChunk& Chunk, const rgen::Poi& Poi, double S0, double S1);
	void BuildHouse(FChunk& Chunk, double S, int32 Side, uint32 Seed, UInstancedStaticMeshComponent* Roofs);
	void BuildTrees(FChunk& Chunk, int32 Index, double S0, double S1);
	bool IsClearZone(double S, double T) const;

	UMaterialInterface* Mat(EPeregonWorldMat Type) const;

	rgen::Route Route;
	TMap<int32, FChunk> Chunks;
	double TrackedS = 0.0;
	bool bGenerated = false;

	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> Materials;
	UPROPERTY(Transient) TObjectPtr<UPhysicalMaterial> AsphaltPhys;
	UPROPERTY(Transient) TObjectPtr<UPhysicalMaterial> GravelPhys;
	UPROPERTY(Transient) TObjectPtr<UPhysicalMaterial> GrassPhys;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY(VisibleAnywhere, Category = "Peregon")
	TObjectPtr<UStaticMeshComponent> FarGround;
};
