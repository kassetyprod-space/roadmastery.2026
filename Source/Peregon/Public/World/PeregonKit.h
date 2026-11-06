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

	static FPeregonKitMaterials Create(UObject* Outer);
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
	void Label(const FTransform& Transform, const FString& Text, float WidthCm, float HeightCm, int32 FontPx, const FLinearColor& Ink,
		const FLinearColor& Paper);

	// Город: многоэтажки (со светом в окнах), цеха, трубы, храм, мачта.
	void Buildings(const std::vector<rgen::Building>& List);
	// Стела на въезде в город. Base — точка у земли, Rot — лицом к водителю. Size как у rgen::Settlement.
	void CityStele(const FVector& Base, const FRotator& Rot, const FString& Name, int32 Size, const FString& Year, bool bLeaving);
	// Стела на границе областей: спереди — After, сзади — Before.
	void RegionStele(const FVector& Base, const FRotator& Rot, const FString& Before, const FString& After);
	// Остановка: бетонный павильон с лавкой, мозаичной стенкой и табличкой «А» с названием.
	void BusStop(const FVector& Base, const FRotator& Rot, const FString& Name);
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
	AActor* Owner;
	USceneComponent* Root;
	TArray<TWeakObjectPtr<UActorComponent>>& Out;
	FPeregonKitMaterials Mats;
};
