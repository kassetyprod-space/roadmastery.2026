// Генерируемый мир вдоль трассы. Маршрут (rgen::Route) режется на чанки по 250 м,
// вокруг игрока держится ~1.5 км: полотно, обочины, рельеф, лес, ЛЭП, сёла, АЗС, кафе, знаки.
// Чанки строятся по одному за кадр, чтобы не было рывков.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/PeregonKit.h"
#include "rgen/City.h"
#include "rgen/Climate.h"
#include "rgen/Landscape.h"
#include "rgen/Route.h"
#include "PeregonRoadWorld.generated.h"

class APeregonSky;
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
	GrassTuft,
	Bush,
	Twigs,
	FarLand,
	Ridge,
	Panel,
	Brick,
	CityRoof,
	LitWindow,
	Gold,
	RedPaint,
	LampGlow,
	Stele,
	SteleAccent,
	VertexPaint,
	PanelTile,
	PanelLoggia,
	Silicate,
	Stucco,
	Ribbon,
	Marking,    // дорожная разметка
	RoofGreen,  // кровля сельских домов разных цветов
	RoofRed,
	RoofBlue,
	Water,      // река в городе
	Sidewalk,   // тротуарная плитка
	Steel,      // перила, арки мостов
	Yard,       // земля городских дворов: вытоптанный газон с пылью
	FarForest,  // кромка дальнего леса (заглушки за пределами настоящих деревьев)
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
	const rgen::Landscape& GetLandscape() const { return Landscape; }
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

public:
	// Материалы набора построек (дома, асфальт, бетон, дерево) — для дворов вне чанков (сюжетный двор).
	FPeregonKitMaterials KitMaterials() const;

private:
	struct FChunk
	{
		TArray<TWeakObjectPtr<UActorComponent>> Components;
		TArray<TWeakObjectPtr<AActor>> Actors;
	};

	void SetupAssets();
	FVector FocusLocation() const;
	void UpdateStreaming(bool bBuildAll);
	void BuildChunk(int32 Index);
	void DestroyChunk(int32 Index);
	void AnnouncePlaces(double PrevS, double NewS);

	// Сетка (S × T) → треугольники. Pos(s, t) возвращает точку в см, UV — свою.
	void BuildGrid(FPeregonMeshBuffers& Out, const TArray<double>& Ss, const TArray<double>& Ts, double ZOffsetM, double UvTileM, bool bRoadUV) const;
	UProceduralMeshComponent* AddSurface(FChunk& Chunk, const FPeregonMeshBuffers& Data, EPeregonWorldMat Mat, UPhysicalMaterial* Phys);
	UStaticMeshComponent* AddMesh(FChunk& Chunk, UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material, bool bCollision);
	UInstancedStaticMeshComponent* AddInstanced(FChunk& Chunk, UStaticMesh* Mesh, EPeregonWorldMat Mat, bool bCollision);
	void AddSign(FChunk& Chunk, double S, double T, const FString& Text, bool bCrossed, bool bFacingDriver = true);

	void BuildVillage(FChunk& Chunk, const rgen::Poi& Poi, double S0, double S1);
	// Дом в селе: сруб, кирпичный или дом с мансардой; крыша одного из цветов (Roofs — по материалу), наличники,
	// крыльцо, труба, иногда тарелка антенны. Цветные мелочи — в Props (краска по цвету вершин).
	void BuildHouse(FChunk& Chunk, double S, int32 Side, uint32 Seed, const TArray<UInstancedStaticMeshComponent*>& Roofs, FPeregonMeshBuffers& Props);
	// Разметка (PeregonRoadWorldDetails.cpp): осевая сплошная/прерывистая/приближения, края, «зебры».
	void BuildMarkings(FChunk& Chunk, double S0, double S1);
	// Знаки у дороги: «опасный поворот», ограничения, указатели расстояний, переходы; рекламные щиты.
	void BuildRoadsideSigns(FChunk& Chunk, double S0, double S1);
	// Где «зебры»: у остановок сёл и в городах.
	void CrosswalksIn(double S0, double S1, TArray<double>& Out) const;
	void BuildTrees(FChunk& Chunk, int32 Index, double S0, double S1);
	bool IsClearZone(double S, double T) const;

	UMaterialInterface* Mat(EPeregonWorldMat Type) const;

	// Пейзаж до горизонта и города (PeregonRoadWorldScenery.cpp).
	void BuildFarTerrain();
	void UpdateRidges(const FVector& Focus);
	void BuildCities();
	void BuildCity(const rgen::Settlement& Town);
	void BuildStele(FChunk& Chunk, double S, int32 TownIndex);
	void BuildRegionStele(FChunk& Chunk, double S, const FString& Before, const FString& After);
	void BuildFoliage(FChunk& Chunk, int32 Index, double S0, double S1);
	void UpdateSeason(bool bForce);
	UProceduralMeshComponent* AddProc(TArray<TWeakObjectPtr<UActorComponent>>& OutList, const FPeregonMeshBuffers& Data, UMaterialInterface* Material, bool bCollision,
		bool bShadow = true);
	UStaticMeshComponent* AddSceneryMesh(UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material);
	APeregonSky* FindSky();

	rgen::Route Route;
	rgen::Landscape Landscape;
	TMap<int32, FChunk> Chunks;
	// Постоянная часть мира: дальний рельеф, города, горы на горизонте.
	TArray<TWeakObjectPtr<UActorComponent>> Scenery;
	TArray<double> SteleS;  // где стоят стелы городов (по индексу в Landscape.Settlements)
	// Настоящий город из OSM (Content/Data/Cities/*.city, PeregonRoadWorldDetails.cpp).
	rgen::CityData CityMap;
	bool bRealCity = false;
	double CityBaseZ = 0.0;
	void LoadRealCity();
	void BuildRealCity();
	// Подгрузка города квадратами (TileSize rgen, 400 м): 0 — нет, 1 — дальний вид, 2 — полностью.
	struct FCityTileSlot
	{
		int32 Index = -1; // в CityRes.Tiles
		int32 Level = 0;
		TArray<int32> Props; // в CityRes.Props
		TArray<TWeakObjectPtr<UActorComponent>> Comps;
	};
	TMap<FIntPoint, FCityTileSlot> CityTiles;
	rgen::CityResult CityRes;
	double CityTileSize = 400.0;
	bool bCityStreamAll = false;
	double CityFullRadius = 650.0;  // м: полный квадрат (коллизия, знаки, фонари)
	double CityLodRadius = 5000.0;  // м: дальний вид (коробки домов)
	void UpdateCityStreaming(const FVector& Focus);
	void BuildCityTile(FCityTileSlot& Slot, int32 Level);
	void ClearCityTile(FCityTileSlot& Slot);
	void AddCityProps(FCityTileSlot& Slot);
	UMaterialInterface* CityMaterial(rgen::CityMat Cm) const;
	FVector RidgeCenter = FVector(1e12);
	rgen::SeasonLook AppliedSeason;
	bool bSeasonApplied = false;
	float SeasonCheckTimer = 0.f;
	TWeakObjectPtr<APeregonSky> SkyRef;
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

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Ridges;
};
