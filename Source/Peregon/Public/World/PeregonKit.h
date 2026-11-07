// Набор построек, общий для дороги и испытательного полигона: многоэтажки, цеха, трубы, храм, стелы,
// остановки, дворовые мелочи (заборы, колодцы, лавки, поленницы), трава и кусты. Всё из примитивов и
// процедурных мешей — без ассетов. Компоненты создаются у владельца (Owner) и складываются в Out,
// чтобы владелец мог их потом удалить.
#pragma once

#include "CoreMinimal.h"
#include "rgen/Landscape.h"

#include <vector>

class AActor;
class UActorComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

// Геометрия для процедурного меша (вершины, треугольники, нормали, UV, цвет вершин).
// Источник настоящего света ночью: фонарь (светит вниз) или горящее окно (светит из фасада наружу).
// Набор построек регистрирует их здесь; APeregonNightLights ставит на ближайшие настоящие прожекторы.
struct FPeregonLightSpot
{
	FVector Location = FVector::ZeroVector;
	FVector Direction = FVector::DownVector;
	FVector2D SizeCm = FVector2D(100.f, 100.f);
	bool bWindow = false;
	uint32 Seed = 0;
	TWeakObjectPtr<UActorComponent> Anchor; // компонент, с которым свет живёт и исчезает (чанк выгрузился — света нет)
};

namespace PeregonLights
{
PEREGON_API TArray<FPeregonLightSpot>& Spots();
// Убрать источники, чьи компоненты уже уничтожены.
PEREGON_API void Prune();
} // namespace PeregonLights

struct PEREGON_API FPeregonMeshBuffers
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;

	// Четырёхугольник A→B→C→D (по контуру), лицом к Normal; UV — по углам. Альфа цвета: низ 0, верх TopAlpha (ветер).
	void AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal, const FVector2D& UvA,
		const FVector2D& UvC, const FColor& Color = FColor::White, uint8 TopAlpha = 255);
	// Коробка: основание в BaseCenter (низ), поворот YawDeg; стены с UV в сантиметрах / Tile.
	void AddBox(const FVector& BaseCenter, float YawDeg, const FVector& SizeCm, const FVector2D& TileCm, const FColor& Color, bool bTop, bool bBottom);
	// Пучок из Planes скрещённых плоскостей (трава, кусты).
	void AddClump(const FVector& Root, float Radius, float Height, int32 Planes, float Yaw0, const FColor& Tint);
};

// Материалы набора. Дорога передаёт свои (их красит сезон), полигон создаёт сам — Create().
struct PEREGON_API FPeregonKitMaterials
{
	UMaterialInterface* Panel = nullptr;
	UMaterialInterface* Brick = nullptr;
	UMaterialInterface* Roof = nullptr;       // плоские крыши
	UMaterialInterface* MetalRoof = nullptr;  // цеха, остановки
	UMaterialInterface* LitWindow = nullptr;
	UMaterialInterface* Concrete = nullptr;
	UMaterialInterface* Stele = nullptr;      // белый бетон
	UMaterialInterface* Accent = nullptr;     // синяя вставка стел
	UMaterialInterface* Red = nullptr;
	UMaterialInterface* White = nullptr;
	UMaterialInterface* Gold = nullptr;
	UMaterialInterface* Wood = nullptr;
	UMaterialInterface* DarkWood = nullptr;
	UMaterialInterface* Pole = nullptr;
	UMaterialInterface* Lamp = nullptr;
	UMaterialInterface* GrassTuft = nullptr;
	UMaterialInterface* Bush = nullptr;
	UMaterialInterface* Glass = nullptr;
	UMaterialInterface* Leaves = nullptr;
	UMaterialInterface* Needles = nullptr;
	UMaterialInterface* BirchBark = nullptr;
	UMaterialInterface* PineBark = nullptr;
	UMaterialInterface* Asphalt = nullptr;
	UMaterialInterface* Dirt = nullptr;
	UMaterialInterface* Paint = nullptr;      // белая краска — оттенок задаёт цвет вершин
	// Фасады (оттенок — цвет вершин): плитка, лоджии, силикатный кирпич, штукатурка, ленточные окна.
	UMaterialInterface* PanelTile = nullptr;
	UMaterialInterface* PanelLoggia = nullptr;
	UMaterialInterface* Silicate = nullptr;
	UMaterialInterface* Stucco = nullptr;
	UMaterialInterface* Ribbon = nullptr;

	static FPeregonKitMaterials Create(UObject* Outer);
	// Фасадные материалы (общие для дороги и полигона).
	static void AddFacades(FPeregonKitMaterials& M, UObject* Outer);
};

// Буферы городской застройки: всё одного материала — одним мешем на город.
struct FPeregonTownBuffers
{
	FPeregonMeshBuffers Panel, PanelTile, PanelLoggia, Brick, Silicate, Stucco, Ribbon;
	FPeregonMeshBuffers Roofs, MetalRoofs, Windows, Props, Wheels, Concrete, Glass, Wood, Lamps;
	TArray<FPeregonLightSpot> WindowSpots;
	TArray<FPeregonLightSpot> LampSpots;
};

class PEREGON_API FPeregonKit
{
public:
	FPeregonKit(AActor* InOwner, USceneComponent* InRoot, TArray<TWeakObjectPtr<UActorComponent>>& InOut, const FPeregonKitMaterials& InMats);

	UProceduralMeshComponent* AddProc(const FPeregonMeshBuffers& Data, UMaterialInterface* Material, bool bCollision, bool bShadow = true);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material, bool bCollision);
	// Куб с размерами в метрах, центр — в Center (см), поворот Rot.
	UStaticMeshComponent* Box(const FVector& Center, const FRotator& Rot, const FVector& SizeM, UMaterialInterface* Material, bool bCollision);
	// Надпись на плоском квадрате (лицом по +X поворота).
	// WidthPx — ширина текстуры (мелкие таблички — 128–256, чтобы сотни номеров домов не ели память).
	void Label(const FTransform& Transform, const FString& Text, float WidthCm, float HeightCm, int32 FontPx, const FLinearColor& Ink,
		const FLinearColor& Paper, int32 WidthPx = 512);

	// Город: многоэтажки (со светом в окнах), цеха, трубы, храм, мачта.
	void Buildings(const std::vector<rgen::Building>& List);
	// Дворовые проезды (асфальт с бортиком вниз, по ним можно ехать) и фонари вдоль них.
	void Streets(const std::vector<rgen::Street>& List);
	// Деревня: огород с грядками, теплица, стог сена, грунтовка от A до B (по точкам земли).
	void Garden(const FVector& Base, const FRotator& Rot);
	void Greenhouse(const FVector& Base, const FRotator& Rot);
	void Haystack(const FVector& Base);
	void DirtRoad(const TArray<FVector>& Points, float WidthCm);
	// Стела на въезде в город. Base — точка у земли, Rot — лицом к водителю. Size как у rgen::Settlement.
	void CityStele(const FVector& Base, const FRotator& Rot, const FString& Name, int32 Size, const FString& Year, bool bLeaving);
	// Стела на границе областей: спереди — After, сзади — Before.
	void RegionStele(const FVector& Base, const FRotator& Rot, const FString& Before, const FString& After);
	// Остановка: павильон одного из видов (Variant: бетон с мозаикой, металл со стеклом, кирпичный под
	// двускатной крышей, деревянный сельский), лавка, урна, табличка «А» с названием и расписанием.
	void BusStop(const FVector& Base, const FRotator& Rot, const FString& Name, int32 Variant = 0);
	// Рекламный щит у трассы на двух опорах (лицом по +X).
	void Billboard(const FVector& Base, const FRotator& Rot, const FString& Text, const FLinearColor& Paper, const FLinearColor& Ink);
	// Дворовое: колодец-журавль/сруб, лавка, поленница, сарай, штакетник от A до B.
	void Well(const FVector& Base, const FRotator& Rot);
	void Bench(const FVector& Base, const FRotator& Rot);
	void Woodpile(const FVector& Base, const FRotator& Rot);
	void Shed(const FVector& Base, const FRotator& Rot);
	void Fence(const FVector& A, const FVector& B);
	// Фонарь: столб, кронштейн к Toward, светящийся плафон.
	void StreetLamp(const FVector& Foot, const FVector& Toward);
	// Дерево: берёза (шар кроны) или сосна (конус), Scale ~1.
	void Tree(const FVector& Base, bool bPine, float Scale);
	// Клочок травы и кустов на ровной земле (полигон).
	void GrassPatch(const FVector& Center, float RadiusCm, int32 Count, uint32 Seed);

	static UStaticMesh* Cube();
	static UStaticMesh* Cylinder();
	static UStaticMesh* Sphere();
	static UStaticMesh* Cone();

private:
	class UInstancedStaticMeshComponent* Instanced(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCollision, bool bShadow = true);
	// Двор многоэтажки: машины у подъездов, лавочки, мусорные баки, берёзы.
	void Yard(const rgen::Building& B, FPeregonMeshBuffers& Props, FPeregonMeshBuffers& Wheels);
	// Здания по видам (PeregonKitTown.cpp).
	void Housing(const rgen::Building& B, FPeregonTownBuffers& T);
	void ShopBuilding(const rgen::Building& B, FPeregonTownBuffers& T);
	void GarageRow(const rgen::Building& B, FPeregonTownBuffers& T);
	void Factory(const rgen::Building& B, FPeregonTownBuffers& T);
	void School(const rgen::Building& B, FPeregonTownBuffers& T, bool bKindergarten);
	void CultureHouse(const rgen::Building& B, FPeregonTownBuffers& T);
	void Office(const rgen::Building& B, FPeregonTownBuffers& T, bool bClinic);
	void Monument(const rgen::Building& B, FPeregonTownBuffers& T);
	void WaterTower(const rgen::Building& B, FPeregonTownBuffers& T);
	void Kiosk(const rgen::Building& B, FPeregonTownBuffers& T);
	void Market(const rgen::Building& B, FPeregonTownBuffers& T);
	// Окна, которые вечером горят: на фасаде пролётами 3 × 2.8 м (как в текстуре).
	void LitWindows(const rgen::Building& B, FPeregonTownBuffers& T, float WinU0, float WinU1, float WinZ0, float WinZ1, float Share);
	// Табличка с адресом на углу дома.
	void HouseNumber(const rgen::Building& B, const FVector& At, const FVector& Outward);
	// Деревья во дворах — инстансами (сотни штук на город).
	class UInstancedStaticMeshComponent* YardTrunks = nullptr;
	class UInstancedStaticMeshComponent* YardCrowns = nullptr;
	AActor* Owner;
	USceneComponent* Root;
	TArray<TWeakObjectPtr<UActorComponent>>& Out;
	FPeregonKitMaterials Mats;
};
