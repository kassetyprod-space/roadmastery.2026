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

// Выцветшие советские панели: белые, бежевые, голубоватые, жёлтые, розоватые.
const FLinearColor PanelColors[] = {FLinearColor(0.93f, 0.93f, 0.9f), FLinearColor(0.96f, 0.88f, 0.74f), FLinearColor(0.8f, 0.88f, 0.96f),
	FLinearColor(0.96f, 0.92f, 0.68f), FLinearColor(0.96f, 0.82f, 0.8f), FLinearColor(0.82f, 0.83f, 0.84f)};

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
void APeregonRoadWorld::FMeshBuffers::AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal,
	const FVector2D& UvA, const FVector2D& UvC, const FColor& Color, uint8 TopAlpha)
{
	const int32 Base = Vertices.Num();
	Vertices.Append({A, B, C, D});
	UVs.Append({UvA, FVector2D(UvC.X, UvA.Y), UvC, FVector2D(UvA.X, UvC.Y)});
	const FVector N = Normal.GetSafeNormal();
	for (int32 I = 0; I < 4; ++I)
	{
		Normals.Add(N);
	}
	// Низ (A, B) стоит на месте, верх (C, D) гнётся ветром — вес в альфе цвета.
	FColor Bottom = Color;
	Bottom.A = 0;
	FColor Top = Color;
	Top.A = TopAlpha;
	Colors.Append({Bottom, Bottom, Top, Top});
	// Лицевая сторона в Unreal — куда смотрит (C − A) × (B − A).
	if (FVector::DotProduct(FVector::CrossProduct(C - A, B - A), N) >= 0.0)
	{
		Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
	}
	else
	{
		Triangles.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
	}
}

void APeregonRoadWorld::FMeshBuffers::AddBox(const FVector& BaseCenter, float YawDeg, const FVector& SizeCm, const FVector2D& TileCm, const FColor& Color,
	bool bTop, bool bBottom)
{
	const FRotator Rot(0.f, YawDeg, 0.f);
	const FVector Ax = Rot.RotateVector(FVector::ForwardVector);
	const FVector Ay = Rot.RotateVector(FVector::RightVector);
	const FVector Hx = Ax * SizeCm.X * 0.5;
	const FVector Hy = Ay * SizeCm.Y * 0.5;
	const FVector Up(0.f, 0.f, SizeCm.Z);
	const float Vh = SizeCm.Z / TileCm.Y;
	auto Wall = [&](const FVector& From, const FVector& To, const FVector& Out) {
		const float Len = (To - From).Size() / TileCm.X;
		AddQuad(From, To, To + Up, From + Up, Out, FVector2D(0.f, Vh), FVector2D(Len, 0.f), Color, 0);
	};
	const FVector P00 = BaseCenter - Hx - Hy;
	const FVector P10 = BaseCenter + Hx - Hy;
	const FVector P11 = BaseCenter + Hx + Hy;
	const FVector P01 = BaseCenter - Hx + Hy;
	Wall(P00, P10, -Ay);
	Wall(P10, P11, Ax);
	Wall(P11, P01, Ay);
	Wall(P01, P00, -Ax);
	if (bTop)
	{
		AddQuad(P00 + Up, P10 + Up, P11 + Up, P01 + Up, FVector::UpVector, FVector2D(0.f, 0.f), FVector2D(SizeCm.X / TileCm.X, SizeCm.Y / TileCm.X), Color, 0);
	}
	if (bBottom)
	{
		AddQuad(P00, P01, P11, P10, FVector::DownVector, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), Color, 0);
	}
}

UProceduralMeshComponent* APeregonRoadWorld::AddProc(TArray<TWeakObjectPtr<UActorComponent>>& OutList, const FMeshBuffers& Data, UMaterialInterface* Material,
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
			FMeshBuffers Tile;
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
	FMeshBuffers Data;
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

void APeregonRoadWorld::BuildCity(const rgen::Settlement& Town)
{
	using namespace PeregonScenery;
	FMeshBuffers PanelWalls;
	FMeshBuffers BrickWalls;
	FMeshBuffers Roofs;
	FMeshBuffers Windows;
	FMeshBuffers Plant;
	FMeshBuffers PlantRoof;
	UInstancedStaticMeshComponent* Red = nullptr;
	UInstancedStaticMeshComponent* White = nullptr;
	auto Stripes = [&]() {
		if (!Red)
		{
			auto MakeIsm = [&](EPeregonWorldMat MatType) {
				UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
				Comp->SetMobility(EComponentMobility::Movable);
				Comp->SetupAttachment(RootComponent);
				Comp->SetStaticMesh(CylinderMesh);
				Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				Comp->SetMaterial(0, Mat(MatType));
				Comp->RegisterComponent();
				Scenery.Add(Comp);
				return Comp;
			};
			Red = MakeIsm(EPeregonWorldMat::RedPaint);
			White = MakeIsm(EPeregonWorldMat::WhitePaint);
		}
	};

	for (const rgen::Building& B : Town.Buildings)
	{
		const FVector Base = FVector(B.X, B.Y, B.Z) * M;
		const float Yaw = FMath::RadiansToDegrees(static_cast<float>(B.Yaw));
		const float H = static_cast<float>(B.Height() * M);
		const FVector Size(B.Length * M, B.Width * M, H);
		rgen::Rng Rand(B.Seed);
		switch (B.Kind)
		{
		case rgen::BuildingKind::Panel5:
		case rgen::BuildingKind::Panel9:
		case rgen::BuildingKind::Tower:
		case rgen::BuildingKind::Brick:
		{
			const bool bBrick = B.Kind == rgen::BuildingKind::Brick;
			const FLinearColor Tint = bBrick ? FLinearColor::White : PanelColors[B.Seed % UE_ARRAY_COUNT(PanelColors)];
			(bBrick ? BrickWalls : PanelWalls).AddBox(Base, Yaw, Size, FVector2D(300.f, 280.f), ToColor(Tint), false, false);
			Roofs.AddBox(Base + FVector(0.f, 0.f, H), Yaw, FVector(Size.X, Size.Y, 45.f), FVector2D(400.f, 400.f), FColor::White, true, false);
			// Свет в окнах вечером: окна на месте окон текстуры (пролёт 3 м, этаж 2.8 м от верха стены).
			const FRotator Rot(0.f, Yaw, 0.f);
			const FVector Ax = Rot.RotateVector(FVector::ForwardVector);
			const FVector Ay = Rot.RotateVector(FVector::RightVector);
			const int32 Floors = FMath::FloorToInt(H / 280.f);
			const float LitShare = 0.1f + 0.1f * static_cast<float>(Rand.Unit());
			for (int32 Face = 0; Face < 4; ++Face)
			{
				const bool bLong = Face % 2 == 0;
				const FVector Out = Face == 0 ? -Ay : (Face == 1 ? Ax : (Face == 2 ? Ay : -Ax));
				const FVector Along = Face == 0 ? Ax : (Face == 1 ? Ay : (Face == 2 ? -Ax : -Ay));
				const float Width = static_cast<float>(bLong ? Size.X : Size.Y);
				const float Depth = static_cast<float>(bLong ? Size.Y : Size.X);
				const FVector Corner = Base + Out * (Depth * 0.5f + 4.f) - Along * Width * 0.5f;
				const int32 Bays = FMath::FloorToInt(Width / 300.f);
				for (int32 Bay = 0; Bay < Bays; ++Bay)
				{
					for (int32 Floor = 0; Floor < Floors; ++Floor)
					{
						if (static_cast<float>(Rand.Unit()) > LitShare)
						{
							continue;
						}
						const float U0 = Bay * 300.f + (bBrick ? 94.f : 84.f);
						const float U1 = Bay * 300.f + (bBrick ? 206.f : 216.f);
						const float Z1 = H - Floor * 280.f - (bBrick ? 56.f : 61.f);
						const float Z0 = H - Floor * 280.f - (bBrick ? 206.f : 184.f);
						Windows.AddQuad(Corner + Along * U0 + FVector(0, 0, Z0), Corner + Along * U1 + FVector(0, 0, Z0), Corner + Along * U1 + FVector(0, 0, Z1),
							Corner + Along * U0 + FVector(0, 0, Z1), Out, FVector2D(0, 1), FVector2D(1, 0), FColor::White, 0);
					}
				}
			}
			break;
		}
		case rgen::BuildingKind::Industrial:
			Plant.AddBox(Base, Yaw, Size, FVector2D(400.f, 400.f), ToColor(FLinearColor(0.75f, 0.73f, 0.7f)), false, false);
			PlantRoof.AddBox(Base + FVector(0.f, 0.f, H), Yaw, FVector(Size.X, Size.Y, 60.f), FVector2D(400.f, 400.f), FColor::White, true, false);
			break;
		case rgen::BuildingKind::Chimney:
		case rgen::BuildingKind::Mast:
		{
			// Красно-белые кольца, к верху тоньше.
			Stripes();
			const bool bMast = B.Kind == rgen::BuildingKind::Mast;
			constexpr int32 Bands = 8;
			const float R0 = bMast ? 1.6f : 3.4f;
			const float R1 = bMast ? 0.5f : 2.2f;
			for (int32 K = 0; K < Bands; ++K)
			{
				const float T = (K + 0.5f) / Bands;
				const float Radius = FMath::Lerp(R0, R1, T);
				const float BandH = H / Bands;
				const FTransform Xf(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, BandH * (K + 0.5f)), FVector(Radius * 2.f, Radius * 2.f, BandH / 100.f));
				(K % 2 == 0 ? Red : White)->AddInstance(Xf, true);
			}
			if (bMast)
			{
				for (float Level : {0.45f, 0.75f})
				{
					White->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, H * Level), FVector(8.f, 8.f, 0.8f)), true);
				}
			}
			break;
		}
		case rgen::BuildingKind::Church:
		{
			// Белый храм: четверик, барабан, пять золотых глав, колокольня с шатром.
			const FRotator Rot(0.f, Yaw, 0.f);
			auto At = [&](float Lx, float Ly, float Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.f); };
			UMaterialInterface* WhiteMat = Mat(EPeregonWorldMat::Stele);
			UMaterialInterface* GoldMat = Mat(EPeregonWorldMat::Gold);
			AddSceneryMesh(CubeMesh, FTransform(Rot, At(0, 0, 5.5f), FVector(16.f, 14.f, 11.f)), WhiteMat);
			AddSceneryMesh(CylinderMesh, FTransform(Rot, At(0, 0, 13.f), FVector(6.f, 6.f, 4.f)), WhiteMat);
			AddSceneryMesh(SphereMesh, FTransform(Rot, At(0, 0, 16.5f), FVector(6.6f, 6.6f, 7.5f)), GoldMat);
			AddSceneryMesh(ConeMesh, FTransform(Rot, At(0, 0, 21.f), FVector(1.2f, 1.2f, 3.f)), GoldMat);
			for (float Sx : {-5.f, 5.f})
			{
				for (float Sy : {-4.5f, 4.5f})
				{
					AddSceneryMesh(CylinderMesh, FTransform(Rot, At(Sx, Sy, 12.f), FVector(2.6f, 2.6f, 2.f)), WhiteMat);
					AddSceneryMesh(SphereMesh, FTransform(Rot, At(Sx, Sy, 14.f), FVector(3.f, 3.f, 3.6f)), GoldMat);
					AddSceneryMesh(ConeMesh, FTransform(Rot, At(Sx, Sy, 16.4f), FVector(0.6f, 0.6f, 1.6f)), GoldMat);
				}
			}
			AddSceneryMesh(CubeMesh, FTransform(Rot, At(-12.f, 0, 10.f), FVector(6.f, 6.f, 20.f)), WhiteMat);
			AddSceneryMesh(ConeMesh, FTransform(Rot, At(-12.f, 0, 23.5f), FVector(5.f, 5.f, 7.f)), GoldMat);
			break;
		}
		default:
			break;
		}
	}
	AddProc(Scenery, PanelWalls, Mat(EPeregonWorldMat::Panel), true);
	AddProc(Scenery, BrickWalls, Mat(EPeregonWorldMat::Brick), true);
	AddProc(Scenery, Roofs, Mat(EPeregonWorldMat::CityRoof), false);
	AddProc(Scenery, Windows, Mat(EPeregonWorldMat::LitWindow), false, false);
	AddProc(Scenery, Plant, Mat(EPeregonWorldMat::Concrete), true);
	AddProc(Scenery, PlantRoof, Mat(EPeregonWorldMat::Roof), false);

	// Фонари вдоль дороги в черте города: ночью светятся.
	UInstancedStaticMeshComponent* Poles = NewObject<UInstancedStaticMeshComponent>(this);
	Poles->SetMobility(EComponentMobility::Movable);
	Poles->SetupAttachment(RootComponent);
	Poles->SetStaticMesh(CylinderMesh);
	Poles->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Poles->SetMaterial(0, Mat(EPeregonWorldMat::Pole));
	Poles->RegisterComponent();
	Scenery.Add(Poles);
	UInstancedStaticMeshComponent* Lamps = NewObject<UInstancedStaticMeshComponent>(this);
	Lamps->SetMobility(EComponentMobility::Movable);
	Lamps->SetupAttachment(RootComponent);
	Lamps->SetStaticMesh(CubeMesh);
	Lamps->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Lamps->SetMaterial(0, Mat(EPeregonWorldMat::LampGlow));
	Lamps->SetCastShadow(false);
	Lamps->RegisterComponent();
	Scenery.Add(Lamps);
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
		Poles->AddInstance(FTransform(FRotator::ZeroRotator, Foot + FVector(0, 0, 450), FVector(0.16, 0.16, 9.0)), true);
		const FVector Toward = (RouteToWorld(S, 0.0, 0.0) - Foot).GetSafeNormal2D();
		Poles->AddInstance(FTransform(FRotationMatrix::MakeFromZ(Toward).Rotator(), Foot + FVector(0, 0, 890) + Toward * 90.0, FVector(0.1, 0.1, 1.8)), true);
		Lamps->AddInstance(FTransform(FRotator(0.0, FMath::RadiansToDegrees(Smp.Heading), 0.0), Foot + FVector(0, 0, 880) + Toward * 175.0,
			FVector(0.6, 0.3, 0.12)), true);
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
	const FString Year = PeregonScenery::TownYear(Name);
	const bool bLeaving = Town.S < 1.0; // выезд из Новосибирска
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	const double Half = Route.RoadHalfWidth();
	const rgen::RouteSample Smp = Route.Sample(S);
	const double T = Half + 11.0;
	const double Yaw = FMath::RadiansToDegrees(Smp.Heading) + 180.0; // лицом к водителю
	const FRotator Rot(0.0, Yaw, 0.0);
	FVector Base = RouteToWorld(S, T, 0.0);
	Base.Z = FMath::Max(Base.Z, RouteToWorld(S, T - 2.0, 0.0).Z) - 20.0;
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	UMaterialInterface* Concrete = Mat(EPeregonWorldMat::Stele);
	UMaterialInterface* Accent = Mat(EPeregonWorldMat::SteleAccent);
	auto Label = [&](const FString& Text, double Lx, double Ly, double Lz, float WidthCm, float HeightCm, int32 FontPx, const FLinearColor& Ink,
					 const FLinearColor& Paper, bool bBack) {
		if (!Art)
		{
			return;
		}
		UMaterialInstanceDynamic* LabelMat = Art->MakeLabelMaterial(this, Text, Ink, Paper, 512, FMath::Max(64, FMath::RoundToInt(512.f * HeightCm / WidthCm)),
			FontPx, EPeregonFont::Display);
		const FRotator Face = bBack ? FRotator(0.0, Yaw + 180.0, 0.0) : Rot;
		if (UProceduralMeshComponent* Quad = UPeregonArtSubsystem::AddLabelQuad(this, RootComponent, FTransform(Face, At(bBack ? -Lx : Lx, bBack ? -Ly : Ly, Lz)),
				WidthCm, HeightCm, LabelMat))
		{
			Chunk.Components.Add(Quad);
		}
	};
	const FLinearColor WhiteInk(0.92f, 0.92f, 0.9f);
	const FLinearColor Blue(0.08f, 0.22f, 0.5f);
	const FLinearColor Dark(0.05f, 0.05f, 0.06f);
	const FLinearColor Light(0.9f, 0.9f, 0.87f);

	if (Town.Size == 2)
	{
		// Областной центр: широкий цоколь, два пилона и ригель с названием, год — на цоколе.
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 0.4), FVector(1.6, 15.0, 0.8)), Concrete, true);
		for (double Y : {-5.6, 5.6})
		{
			AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, Y, 4.8), FVector(1.3, 1.3, 8.8)), Concrete, true);
		}
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 7.6), FVector(1.0, 12.6, 2.4)), Accent, false);
		Label(Name.ToUpper(), 0.52, 0, 7.6, 1080.f, 190.f, 96, WhiteInk, Blue, false);
		Label(bLeaving ? FString(TEXT("Счастливого пути!")) : Year, 0.82, 0, 0.4, bLeaving ? 900.f : 360.f, 60.f, 44, Dark, Light, false);
		// Обратная сторона — для тех, кто выезжает.
		Label(TEXT("Счастливого пути!"), 0.52, 0, 7.6, 1080.f, 190.f, 80, WhiteInk, Blue, true);
	}
	else if (Town.Size == 1)
	{
		// Город: высокая плита с цветной вставкой, название крупно, год под ним.
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 0.3), FVector(2.4, 6.0, 0.6)), Concrete, true);
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 4.0), FVector(0.9, 3.4, 7.4)), Concrete, true);
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0.05, 1.25, 4.0), FVector(0.9, 0.5, 7.4)), Accent, false);
		Label(Name.ToUpper(), 0.47, -0.25, 6.0, 280.f, 90.f, 72, Dark, Light, false);
		Label(Year, 0.47, -0.25, 4.6, 200.f, 60.f, 48, Dark, Light, false);
	}
	else
	{
		// Посёлок: кирпичная стенка с табличкой.
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 1.1), FVector(0.7, 7.0, 2.2)), Mat(EPeregonWorldMat::RedPaint), true);
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 2.3), FVector(0.9, 7.3, 0.25)), Concrete, false);
		Label(Name.ToUpper(), 0.37, 0, 1.35, 600.f, 110.f, 72, Dark, Light, false);
		if (!Year.IsEmpty())
		{
			Label(Year, 0.37, 0, 0.55, 160.f, 40.f, 40, Dark, Light, false);
		}
	}
	// Клумба перед стелой.
	AddMesh(Chunk, CubeMesh, FTransform(Rot, At(2.4, 0, 0.15), FVector(1.6, Town.Size == 2 ? 10.0 : 5.0, 0.3)), Mat(EPeregonWorldMat::Concrete), false);
}

void APeregonRoadWorld::BuildRegionStele(FChunk& Chunk, double S, const FString& Before, const FString& After)
{
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	const rgen::RouteSample Smp = Route.Sample(S);
	const double Half = Route.RoadHalfWidth();
	const double Yaw = FMath::RadiansToDegrees(Smp.Heading) + 180.0;
	const FRotator Rot(0.0, Yaw, 0.0);
	FVector Base = RouteToWorld(S, Half + 12.0, 0.0);
	Base.Z -= 20.0;
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	UMaterialInterface* Concrete = Mat(EPeregonWorldMat::Stele);
	// Цоколь, две наклонные плиты-«крыла», между ними — щит с названием области; с обратной стороны — прежняя область.
	AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 0.35), FVector(2.4, 13.0, 0.7)), Concrete, true);
	AddMesh(Chunk, CubeMesh, FTransform(FRotator(0.0, Yaw, 8.0), At(0, -5.2, 3.6), FVector(0.9, 1.6, 6.6)), Concrete, true);
	AddMesh(Chunk, CubeMesh, FTransform(FRotator(0.0, Yaw, -8.0), At(0, 5.2, 3.6), FVector(0.9, 1.6, 6.6)), Concrete, true);
	AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 4.2), FVector(0.6, 8.2, 3.4)), Mat(EPeregonWorldMat::SteleAccent), true);
	// Полосы «флага» области сверху.
	AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 6.2), FVector(0.65, 8.2, 0.35)), Mat(EPeregonWorldMat::WhitePaint), false);
	AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, 6.55), FVector(0.65, 8.2, 0.35)), Mat(EPeregonWorldMat::RedPaint), false);
	if (Art)
	{
		auto Label = [&](const FString& Text, bool bBack) {
			UMaterialInstanceDynamic* LabelMat = Art->MakeLabelMaterial(this, Text.Replace(TEXT(" — "), TEXT("\n")), FLinearColor(0.93f, 0.93f, 0.9f),
				FLinearColor(0.08f, 0.22f, 0.5f), 512, 200, 64, EPeregonFont::Display);
			const FRotator Face = bBack ? FRotator(0.0, Yaw + 180.0, 0.0) : Rot;
			if (UProceduralMeshComponent* Quad = UPeregonArtSubsystem::AddLabelQuad(this, RootComponent, FTransform(Face, At(bBack ? -0.32 : 0.32, 0, 4.2)),
					780.f, 300.f, LabelMat))
			{
				Chunk.Components.Add(Quad);
			}
		};
		Label(After, false);
		if (!Before.IsEmpty())
		{
			Label(Before, true);
		}
	}
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
		return false;
	};
	auto Clump = [&](FMeshBuffers& Out, const FVector& Root, float Radius, float Height, int32 Planes, const FColor& Tint) {
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
		FMeshBuffers Grass;
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
	FMeshBuffers Bushes;
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
