// Пейзаж за пределами придорожной полосы: дальний рельеф до горизонта, горы, города (панельные дома,
// высотки, трубы, храмы), стелы на въездах, трава и кусты, смена времён года.
#include "World/PeregonRoadWorld.h"

#include "Art/PeregonArtSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/PeregonGameInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "World/PeregonSky.h"
#include "rgen/Noise.h"

namespace PeregonScenery
{
constexpr double M = 100.0;

FColor ToColor(const FLinearColor& C)
{
	// Цвет вершин без гамма-коррекции: множитель оттенка в материале.
	return FColor(static_cast<uint8>(FMath::Clamp(C.R, 0.f, 1.f) * 255.f), static_cast<uint8>(FMath::Clamp(C.G, 0.f, 1.f) * 255.f),
		static_cast<uint8>(FMath::Clamp(C.B, 0.f, 1.f) * 255.f), static_cast<uint8>(FMath::Clamp(C.A, 0.f, 1.f) * 255.f));
}

// Годы получения статуса города (для стел).
const TCHAR* TownYear(const FString& Name)
{
	if (Name.Contains(TEXT("Кемерово")))
	{
		return TEXT("1918");
	}
	if (Name.Contains(TEXT("Новосибирск")))
	{
		return TEXT("1893");
	}
	if (Name.Contains(TEXT("Юрга")))
	{
		return TEXT("1949");
	}
	if (Name.Contains(TEXT("Топки")))
	{
		return TEXT("1933");
	}
	if (Name.Contains(TEXT("Болотное")))
	{
		return TEXT("1943");
	}
	return TEXT("");
}
} // namespace PeregonScenery

// ---------------------------------------------------------------------------------------------
// Сборка мешей
// ---------------------------------------------------------------------------------------------
UProceduralMeshComponent* APeregonRoadWorld::AddProc(TArray<TWeakObjectPtr<UActorComponent>>& OutList, const FPeregonMeshBuffers& Data, UMaterialInterface* Material,
	bool bCollision, bool bShadow)
{
	if (Data.Vertices.Num() == 0)
	{
		return nullptr;
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetupAttachment(RootComponent);
	Mesh->bUseAsyncCooking = true;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Mesh->SetCastShadow(bShadow);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->RegisterComponent();
	const bool bColors = Data.Colors.Num() == Data.Vertices.Num();
	Mesh->CreateMeshSection(0, Data.Vertices, Data.Triangles, Data.Normals, Data.UVs, bColors ? Data.Colors : TArray<FColor>(), TArray<FProcMeshTangent>(),
		bCollision);
	if (Material)
	{
		Mesh->SetMaterial(0, Material);
	}
	OutList.Add(Mesh);
	return Mesh;
}

UStaticMeshComponent* APeregonRoadWorld::AddSceneryMesh(UStaticMesh* InMesh, const FTransform& Transform, UMaterialInterface* Material)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(RootComponent);
	Comp->SetStaticMesh(InMesh);
	Comp->SetRelativeTransform(Transform);
	Comp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Comp->SetCanEverAffectNavigation(false);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->RegisterComponent();
	Scenery.Add(Comp);
	return Comp;
}

APeregonSky* APeregonRoadWorld::FindSky()
{
	if (!SkyRef.IsValid())
	{
		for (TActorIterator<APeregonSky> It(GetWorld()); It; ++It)
		{
			SkyRef = *It;
			break;
		}
	}
	return SkyRef.Get();
}

// ---------------------------------------------------------------------------------------------
// Дальний рельеф: сетка 140 м вдоль всего маршрута (полоса ±6.6 км), холмы, поля, лесные массивы
// ---------------------------------------------------------------------------------------------
void APeregonRoadWorld::BuildFarTerrain()
{
	using namespace PeregonScenery;
	double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0;
	Landscape.Bounds(MinX, MinY, MaxX, MaxY);
	constexpr double Step = 140.0;
	constexpr double Corridor = 6600.0;
	constexpr int32 TileCells = 48;
	MinX -= Corridor + Step;
	MinY -= Corridor + Step;
	MaxX += Corridor + Step;
	MaxY += Corridor + Step;
	const int32 Nx = FMath::CeilToInt((MaxX - MinX) / Step) + 1;
	const int32 Ny = FMath::CeilToInt((MaxY - MinY) / Step) + 1;
	TArray<float> Heights;
	TArray<float> Dist;
	TArray<FColor> Tints;
	Heights.SetNumZeroed(Nx * Ny);
	Dist.SetNumZeroed(Nx * Ny);
	Tints.SetNumZeroed(Nx * Ny);
	const uint32 FieldSeed = rgen::Hash(Route.Params().Seed, 0xF1E1Du);
	for (int32 J = 0; J < Ny; ++J)
	{
		for (int32 I = 0; I < Nx; ++I)
		{
			const double X = MinX + I * Step;
			const double Y = MinY + J * Step;
			double S = 0.0, T = 0.0, D = 0.0;
			Landscape.Nearest(X, Y, S, T, D);
			const int32 K = J * Nx + I;
			Dist[K] = static_cast<float>(D);
			if (D > Corridor + Step * 2.0)
			{
				continue;
			}
			// Лес — «шапкой» над землёй вдали; у дороги стоят настоящие деревья, шапки там нет.
			const double Away = FMath::SmoothStep(330.0, 520.0, D);
			const double Forest = FMath::SmoothStep(0.42, 0.62, Landscape.ForestDensity(X, Y)) * Away;
			Heights[K] = static_cast<float>(Landscape.FarHeight(X, Y) + Landscape.CanopyHeight(X, Y) * Away);
			// Поля пятнами: луг, спелое поле, пашня; лес тёмный; город серый.
			const double Field = rgen::Noise2D(X / 900.0, Y / 900.0, FieldSeed);
			const double Patch = rgen::Noise2D(X / 260.0, Y / 260.0, FieldSeed + 1);
			FLinearColor Ground = Field > 0.25 ? FLinearColor(0.86f, 0.78f, 0.45f) : (Field < -0.35 ? FLinearColor(0.55f, 0.45f, 0.34f) : FLinearColor(0.62f, 0.72f, 0.5f));
			Ground *= 0.9f + 0.15f * static_cast<float>(Patch);
			FLinearColor Color = FMath::Lerp(Ground, FLinearColor(0.26f, 0.38f, 0.27f), static_cast<float>(Forest));
			Color = FMath::Lerp(Color, FLinearColor(0.55f, 0.55f, 0.53f), static_cast<float>(Landscape.UrbanMask(X, Y)) * 0.7f);
			Color.A = 0.f;
			Tints[K] = ToColor(Color);
		}
	}
	for (int32 Tj = 0; Tj < Ny - 1; Tj += TileCells)
	{
		for (int32 Ti = 0; Ti < Nx - 1; Ti += TileCells)
		{
			FPeregonMeshBuffers Tile;
			const int32 Ci = FMath::Min(TileCells, Nx - 1 - Ti);
			const int32 Cj = FMath::Min(TileCells, Ny - 1 - Tj);
			for (int32 J = 0; J <= Cj; ++J)
			{
				for (int32 I = 0; I <= Ci; ++I)
				{
					const int32 Gx = Ti + I;
					const int32 Gy = Tj + J;
					const int32 K = Gy * Nx + Gx;
					Tile.Vertices.Add(FVector(MinX + Gx * Step, MinY + Gy * Step, Heights[K]) * M);
					auto H = [&](int32 X, int32 Y) { return static_cast<double>(Heights[FMath::Clamp(Y, 0, Ny - 1) * Nx + FMath::Clamp(X, 0, Nx - 1)]); };
					const FVector N(-(H(Gx + 1, Gy) - H(Gx - 1, Gy)) / (2.0 * Step), -(H(Gx, Gy + 1) - H(Gx, Gy - 1)) / (2.0 * Step), 1.0);
					Tile.Normals.Add(N.GetSafeNormal());
					Tile.UVs.Add(FVector2D(Gx * Step / 24.0, Gy * Step / 24.0));
					Tile.Colors.Add(Tints[K]);
				}
			}
			const int32 Row = Ci + 1;
			for (int32 J = 0; J < Cj; ++J)
			{
				for (int32 I = 0; I < Ci; ++I)
				{
					const int32 K00 = (Tj + J) * Nx + Ti + I;
					const float Near = FMath::Min(FMath::Min(Dist[K00], Dist[K00 + 1]), FMath::Min(Dist[K00 + Nx], Dist[K00 + Nx + 1]));
					if (Near > Corridor)
					{
						continue;
					}
					const int32 A = J * Row + I;
					const int32 B = A + 1;
					const int32 C = A + Row;
					const int32 D = C + 1;
					// Сетка по X (вперёд) и Y (вправо), лицом вверх.
					Tile.Triangles.Append({A, C, B, B, C, D});
				}
			}
			if (Tile.Triangles.Num() > 0)
			{
				AddProc(Scenery, Tile, Mat(EPeregonWorldMat::FarLand), false, false);
			}
		}
	}
}

void APeregonRoadWorld::UpdateRidges(const FVector& Focus)
{
	using namespace PeregonScenery;
	if (!Ridges)
	{
		Ridges = NewObject<UProceduralMeshComponent>(this, TEXT("Ridges"));
		Ridges->SetupAttachment(RootComponent);
		Ridges->SetMobility(EComponentMobility::Movable);
		Ridges->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ridges->SetCastShadow(false);
		Ridges->SetCanEverAffectNavigation(false);
		Ridges->SetAffectDistanceFieldLighting(false);
		Ridges->RegisterComponent();
		Ridges->SetAbsolute(true, true, true);
	}
	const double GroundZ = Route.Sample(TrackedS).Z;
	Ridges->SetWorldLocation(FVector(Focus.X, Focus.Y, (GroundZ - 60.0) * M));
	if (FVector::Dist2D(Focus, RidgeCenter) < 150000.0)
	{
		return;
	}
	RidgeCenter = Focus;
	// Три гряды: ближние увалы, дальние гривы и горы на юге и востоке. Каждая — «стена» лицом к игроку;
	// синеву и дымку даёт атмосфера (воздушная перспектива).
	FPeregonMeshBuffers Data;
	constexpr int32 Segments = 180;
	const double Radii[] = {10500.0, 15500.0, 23000.0};
	for (int32 Layer = 0; Layer < 3; ++Layer)
	{
		const double R = Radii[Layer];
		const int32 Base = Data.Vertices.Num();
		for (int32 K = 0; K <= Segments; ++K)
		{
			const double Az = 360.0 * K / Segments;
			// Азимут от севера (X) к востоку (Y).
			const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Az)), FMath::Sin(FMath::DegreesToRadians(Az)), 0.0);
			const double Height = Landscape.RidgeHeight(Az, Layer, Focus.X / M, Focus.Y / M) + 60.0;
			const FVector Bottom = Dir * R * M;
			const FVector Top = Bottom + FVector(0.0, 0.0, Height * M);
			Data.Vertices.Append({Bottom, Top});
			const FVector Inward = (-Dir + FVector(0.0, 0.0, 0.35)).GetSafeNormal();
			Data.Normals.Append({Inward, Inward});
			Data.UVs.Append({FVector2D(Az / 3.0, Height / 30.0), FVector2D(Az / 3.0, 0.0)});
			// Вершины высоких гор — в снегу.
			const float Snowcap = FMath::SmoothStep(170.f, 260.f, static_cast<float>(Height));
			const FLinearColor Foot = Layer == 0 ? FLinearColor(0.3f, 0.42f, 0.32f) : FLinearColor(0.34f, 0.44f, 0.42f);
			FLinearColor Peak = FMath::Lerp(Foot, FLinearColor(0.95f, 0.96f, 1.f), Snowcap);
			Peak.A = 0.f;
			FLinearColor FootA = Foot;
			FootA.A = 0.f;
			Data.Colors.Append({ToColor(FootA), ToColor(Peak)});
		}
		for (int32 K = 0; K < Segments; ++K)
		{
			const int32 A = Base + K * 2;
			// Лицом внутрь кольца: обход Bottom(K) → Bottom(K+1) → Top(K+1) против часовой, если смотреть из центра.
			const FVector Center = (Data.Vertices[A] + Data.Vertices[A + 3]) * 0.5;
			const FVector Face = FVector::CrossProduct(Data.Vertices[A + 3] - Data.Vertices[A], Data.Vertices[A + 2] - Data.Vertices[A]);
			if (FVector::DotProduct(Face, -Center) >= 0.0)
			{
				Data.Triangles.Append({A, A + 2, A + 3, A, A + 3, A + 1});
			}
			else
			{
				Data.Triangles.Append({A, A + 3, A + 2, A, A + 1, A + 3});
			}
		}
	}
	Ridges->CreateMeshSection(0, Data.Vertices, Data.Triangles, Data.Normals, Data.UVs, Data.Colors, TArray<FProcMeshTangent>(), false);
	Ridges->SetMaterial(0, Mat(EPeregonWorldMat::Ridge));
}

// ---------------------------------------------------------------------------------------------
// Города
// ---------------------------------------------------------------------------------------------
void APeregonRoadWorld::BuildCities()
{
	SteleS.Reset();
	for (const rgen::Settlement& Town : Landscape.Settlements())
	{
		BuildCity(Town);
		// Стела — там, где дорога входит в город (или при выезде из Новосибирска — «Счастливого пути!»).
		double S = 90.0;
		if (Town.S > 1.0)
		{
			S = Town.S;
			while (S > 40.0)
			{
				const rgen::RouteSample Smp = Route.Sample(S);
				if (FMath::Sqrt(FMath::Square(Smp.X - Town.X) + FMath::Square(Smp.Y - Town.Y)) > Town.Radius * 0.9 + 60.0)
				{
					break;
				}
				S -= 10.0;
			}
			S = FMath::Min(S, Town.S - 270.0);
		}
		SteleS.Add(FMath::Max(S, 40.0));
	}
}

FPeregonKitMaterials APeregonRoadWorld::KitMaterials() const
{
	FPeregonKitMaterials K;
	K.Panel = Mat(EPeregonWorldMat::Panel);
	K.Brick = Mat(EPeregonWorldMat::Brick);
	K.Roof = Mat(EPeregonWorldMat::CityRoof);
	K.MetalRoof = Mat(EPeregonWorldMat::Roof);
	K.LitWindow = Mat(EPeregonWorldMat::LitWindow);
	K.Concrete = Mat(EPeregonWorldMat::Concrete);
	K.Stele = Mat(EPeregonWorldMat::Stele);
	K.Accent = Mat(EPeregonWorldMat::SteleAccent);
	K.Red = Mat(EPeregonWorldMat::RedPaint);
	K.White = Mat(EPeregonWorldMat::WhitePaint);
	K.Gold = Mat(EPeregonWorldMat::Gold);
	K.Wood = Mat(EPeregonWorldMat::Wood);
	K.DarkWood = Mat(EPeregonWorldMat::Wood);
	K.Pole = Mat(EPeregonWorldMat::Pole);
	K.Lamp = Mat(EPeregonWorldMat::LampGlow);
	K.GrassTuft = Mat(EPeregonWorldMat::GrassTuft);
	K.Bush = Mat(EPeregonWorldMat::Bush);
	K.Glass = Mat(EPeregonWorldMat::Glass);
	K.Leaves = Mat(EPeregonWorldMat::Leaves);
	K.Needles = Mat(EPeregonWorldMat::Needles);
	K.BirchBark = Mat(EPeregonWorldMat::BirchBark);
	K.PineBark = Mat(EPeregonWorldMat::PineBark);
	K.Asphalt = Mat(EPeregonWorldMat::Pad);
	K.Dirt = Mat(EPeregonWorldMat::Shoulder);
	K.Paint = Mat(EPeregonWorldMat::VertexPaint);
	K.PanelTile = Mat(EPeregonWorldMat::PanelTile);
	K.PanelLoggia = Mat(EPeregonWorldMat::PanelLoggia);
	K.Silicate = Mat(EPeregonWorldMat::Silicate);
	K.Stucco = Mat(EPeregonWorldMat::Stucco);
	K.Ribbon = Mat(EPeregonWorldMat::Ribbon);
	return K;
}

void APeregonRoadWorld::BuildCity(const rgen::Settlement& Town)
{
	if (Town.Real)
	{
		return; // строит BuildRealCity из OSM
	}
	FPeregonKit Kit(this, RootComponent, Scenery, KitMaterials());
	Kit.Buildings(Town.Buildings);
	Kit.Streets(Town.Streets);

	// Фонари вдоль дороги в черте города: ночью светятся.
	const double Half = Route.RoadHalfWidth();
	int32 Side = 1;
	for (double S = FMath::Max(0.0, Town.S - Town.Radius * 1.1); S < FMath::Min(Route.Length(), Town.S + Town.Radius * 1.1); S += 45.0)
	{
		const rgen::RouteSample Smp = Route.Sample(S);
		if (FMath::Sqrt(FMath::Square(Smp.X - Town.X) + FMath::Square(Smp.Y - Town.Y)) > Town.Radius)
		{
			continue;
		}
		Side = -Side;
		const FVector Foot = RouteToWorld(S, Side * (Half + 3.2), 0.0);
		Kit.StreetLamp(Foot, RouteToWorld(S, 0.0, 0.0) - Foot);
	}
}

// ---------------------------------------------------------------------------------------------
// Стелы
// ---------------------------------------------------------------------------------------------
void APeregonRoadWorld::BuildStele(FChunk& Chunk, double S, int32 TownIndex)
{
	if (TownIndex < 0 || static_cast<size_t>(TownIndex) >= Landscape.Settlements().size())
	{
		return;
	}
	const rgen::Settlement& Town = Landscape.Settlements()[static_cast<size_t>(TownIndex)];
	const FString Name = FString(UTF8_TO_TCHAR(Town.Name.c_str()));
	const double Half = Route.RoadHalfWidth();
	const rgen::RouteSample Smp = Route.Sample(S);
	const double T = Half + 11.0;
	FVector Base = RouteToWorld(S, T, 0.0);
	Base.Z = FMath::Max(Base.Z, RouteToWorld(S, T - 2.0, 0.0).Z) - 20.0;
	// Лицом к водителю.
	const FRotator Rot(0.0, FMath::RadiansToDegrees(Smp.Heading) + 180.0, 0.0);
	FPeregonKit Kit(this, RootComponent, Chunk.Components, KitMaterials());
	if (Town.Real)
	{
		// Настоящая стела города — у въезда, по правую руку тех, кто едет в город (против хода S).
		FVector In = RouteToWorld(S, -T, 0.0);
		In.Z = FMath::Min(In.Z, RouteToWorld(S, -T + 2.0, 0.0).Z) - 20.0;
		Kit.Landmark(TEXT("stele"), In, static_cast<float>(FMath::RadiansToDegrees(Smp.Heading)), 0.f, FVector::ZeroVector, Name);
		return;
	}
	Kit.CityStele(Base, Rot, Name, Town.Size, PeregonScenery::TownYear(Name), Town.S < 1.0);
}

void APeregonRoadWorld::BuildRegionStele(FChunk& Chunk, double S, const FString& Before, const FString& After)
{
	const rgen::RouteSample Smp = Route.Sample(S);
	FVector Base = RouteToWorld(S, Route.RoadHalfWidth() + 12.0, 0.0);
	Base.Z -= 20.0;
	FPeregonKit Kit(this, RootComponent, Chunk.Components, KitMaterials());
	Kit.RegionStele(Base, FRotator(0.0, FMath::RadiansToDegrees(Smp.Heading) + 180.0, 0.0), Before, After);
}

// ---------------------------------------------------------------------------------------------
// Трава и кусты
// ---------------------------------------------------------------------------------------------
void APeregonRoadWorld::BuildFoliage(FChunk& Chunk, int32 Index, double S0, double S1)
{
	const rgen::SeasonLook& Look = AppliedSeason;
	const bool bSnowy = bSeasonApplied && Look.GroundSnow > 0.6;
	const bool bBare = bSeasonApplied && Look.Leaves < 0.2;
	rgen::Rng Rand(rgen::Hash(static_cast<uint32_t>(Index + 7000), Route.Params().Seed + 91u));
	const double Shoulder = Route.ShoulderOuter();
	auto InFlatten = [this](double S, double T) {
		for (const rgen::FlattenZone& Z : Route.Flatten())
		{
			if (S > Z.S0 - 5.0 && S < Z.S1 + 5.0 && T > Z.TMin - 3.0 && T < Z.TMax + 3.0)
			{
				return true;
			}
		}
		if (FMath::Abs(T) > 25.0)
		{
			double X = 0.0;
			double Y = 0.0;
			Route.ToWorld(S, T, X, Y);
			return Landscape.UrbanMask(X, Y) > 0.0 && Landscape.InsideBuilding(X, Y, 1.5);
		}
		return false;
	};
	auto Clump = [&](FPeregonMeshBuffers& Out, const FVector& Root, float Radius, float Height, int32 Planes, const FColor& Tint) {
		const float Yaw0 = static_cast<float>(Rand.Range(0.0, PI));
		for (int32 K = 0; K < Planes; ++K)
		{
			const float A = Yaw0 + PI * K / Planes;
			const FVector Axis(FMath::Cos(A), FMath::Sin(A), 0.f);
			const FVector Side = Axis * Radius;
			const FVector Up(0.f, 0.f, Height);
			Out.AddQuad(Root - Side, Root + Side, Root + Side + Up, Root - Side + Up, FVector::CrossProduct(Axis, FVector::UpVector) + FVector(0, 0, 0.6f),
				FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), Tint, 255);
		}
	};

	// Трава: пучки вдоль кюветов и по полю; под снегом не видно.
	if (!bSnowy)
	{
		FPeregonMeshBuffers Grass;
		for (double S = S0; S < S1; S += 1.4)
		{
			for (int32 Side : {-1, 1})
			{
				for (int32 Try = 0; Try < 3; ++Try)
				{
					const double A = Shoulder + 0.4 + FMath::Square(Rand.Unit()) * 60.0;
					const double T = Side * A;
					const double Sj = S + Rand.Range(-0.7, 0.7);
					if (InFlatten(Sj, T))
					{
						continue;
					}
					const FVector Root = RouteToWorld(Sj, T, -0.05);
					const float Shade = static_cast<float>(Rand.Range(0.75, 1.0));
					const FColor Tint(static_cast<uint8>(255 * Shade), static_cast<uint8>(255 * FMath::Min(1.f, Shade * 1.05f)), static_cast<uint8>(230 * Shade), 255);
					Clump(Grass, Root, static_cast<float>(Rand.Range(30.0, 60.0)), static_cast<float>(Rand.Range(35.0, 80.0)), 3, Tint);
				}
			}
		}
		AddProc(Chunk.Components, Grass, Mat(EPeregonWorldMat::GrassTuft), false, false);
	}

	// Кусты (ива, черёмуха, шиповник) — по опушкам и вдоль кюветов.
	FPeregonMeshBuffers Bushes;
	for (double S = S0; S < S1; S += 5.0)
	{
		for (int32 Side : {-1, 1})
		{
			const double T = Side * Rand.Range(11.0, 75.0);
			const double Sj = S + Rand.Range(-2.5, 2.5);
			if (InFlatten(Sj, T) || (T < -11.0 && T > -21.0))
			{
				continue;
			}
			double X = 0.0, Y = 0.0;
			Route.ToWorld(Sj, T, X, Y);
			const double Edge = Landscape.ForestDensity(X, Y);
			const double Chance = 0.08 + 0.45 * FMath::SmoothStep(0.1, 0.35, Edge) * (1.0 - FMath::SmoothStep(0.6, 0.8, Edge));
			if (!Rand.Chance(Chance))
			{
				continue;
			}
			const float Size = static_cast<float>(Rand.Range(0.8, 1.6));
			const uint8 Shade = static_cast<uint8>(Rand.Range(190.0, 255.0));
			Clump(Bushes, RouteToWorld(Sj, T, -0.1), 140.f * Size, 210.f * Size, 3, FColor(Shade, Shade, Shade, 255));
		}
	}
	AddProc(Chunk.Components, Bushes, Mat(bBare ? EPeregonWorldMat::Twigs : EPeregonWorldMat::Bush), false, true);
}

// ---------------------------------------------------------------------------------------------
// Времена года
// ---------------------------------------------------------------------------------------------
void APeregonRoadWorld::UpdateSeason(bool bForce)
{
	const UPeregonGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<UPeregonGameInstance>() : nullptr;
	const rgen::SeasonLook Look = rgen::Climate::Season(GI ? GI->Climate.DayOfYear : 258.0);
	const bool bChanged = !bSeasonApplied || FMath::Abs(Look.Leaves - AppliedSeason.Leaves) > 0.01 || FMath::Abs(Look.Yellow - AppliedSeason.Yellow) > 0.01 ||
		FMath::Abs(Look.DryGrass - AppliedSeason.DryGrass) > 0.01 || FMath::Abs(Look.GroundSnow - AppliedSeason.GroundSnow) > 0.01;
	if (!bForce && !bChanged)
	{
		return;
	}
	// Резкая смена (облетела листва, лёг снег) — перестраиваем дальние чанки; ближние обновятся сами.
	const bool bRebuild = bSeasonApplied && ((Look.Leaves < 0.15) != (AppliedSeason.Leaves < 0.15) || (Look.GroundSnow > 0.6) != (AppliedSeason.GroundSnow > 0.6) ||
		FMath::Abs(Look.Leaves - AppliedSeason.Leaves) > 0.3);
	AppliedSeason = Look;
	bSeasonApplied = true;

	auto SetColor = [this](EPeregonWorldMat Slot, const FLinearColor& Color) {
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mat(Slot)))
		{
			Mid->SetVectorParameterValue(TEXT("Color"), Color);
		}
	};
	const float Dry = static_cast<float>(Look.DryGrass);
	const float Yellow = static_cast<float>(Look.Yellow);
	const FLinearColor GrassTint = FMath::Lerp(FLinearColor::White, FLinearColor(1.35f, 1.05f, 0.58f), Dry);
	SetColor(EPeregonWorldMat::Grass, GrassTint);
	SetColor(EPeregonWorldMat::FarLand, GrassTint * 1.4f);
	SetColor(EPeregonWorldMat::GrassTuft, FMath::Lerp(FLinearColor(0.55f, 0.62f, 0.38f), FLinearColor(0.82f, 0.7f, 0.42f), Dry));
	SetColor(EPeregonWorldMat::Leaves, FMath::Lerp(FLinearColor::White, FLinearColor(2.f, 1.3f, 0.45f), Yellow));
	SetColor(EPeregonWorldMat::Bush, FMath::Lerp(FLinearColor(0.42f, 0.55f, 0.3f), FLinearColor(0.85f, 0.5f, 0.2f), Yellow));
	const float Winter = static_cast<float>(Look.GroundSnow);
	SetColor(EPeregonWorldMat::Needles, FMath::Lerp(FLinearColor::White, FLinearColor(0.75f, 0.8f, 0.8f), Winter));

	if (bRebuild)
	{
		const int32 Current = FMath::FloorToInt(TrackedS / ChunkLength);
		TArray<int32> Keys;
		Chunks.GetKeys(Keys);
		for (int32 Key : Keys)
		{
			if (FMath::Abs(Key - Current) >= 2)
			{
				DestroyChunk(Key);
			}
		}
	}
}
