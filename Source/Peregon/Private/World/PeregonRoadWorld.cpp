#include "World/PeregonRoadWorld.h"

#include "Algo/Reverse.h"

#include "Art/PeregonArtSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/PeregonGameMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProceduralMeshComponent.h"
#include "rgen/Atlas.h"
#include "rgen/Noise.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/PeregonServicePoint.h"

namespace PeregonRoad
{
constexpr double M = 100.0; // м → см

// Совпадает с Config/DefaultEngine.ini → PhysicalSurfaces.
constexpr EPhysicalSurface AsphaltSurface = SurfaceType1;
constexpr EPhysicalSurface GravelSurface = SurfaceType2;
constexpr EPhysicalSurface GrassSurface = SurfaceType7;

// Поперечные отметки рельефа (м от оси): густо у дороги, редко вдали.
const double TerrainT[] = {5.5, 7.0, 9.0, 11.0, 14.0, 20.0, 28.0, 40.0, 55.0, 75.0, 100.0, 135.0, 180.0, 230.0, 285.0};

// Цвета домов: выцветшая краска сибирской деревни.
const FLinearColor HouseColors[] = {
	FLinearColor(0.20f, 0.33f, 0.55f), FLinearColor(0.25f, 0.45f, 0.30f), FLinearColor(0.70f, 0.55f, 0.25f),
	FLinearColor(0.45f, 0.30f, 0.20f), FLinearColor(0.75f, 0.72f, 0.62f), FLinearColor(0.42f, 0.55f, 0.58f)};

FString PoiName(const rgen::Poi& Poi)
{
	return FString(UTF8_TO_TCHAR(Poi.Name.c_str()));
}

bool IsSettlement(const rgen::Poi& Poi)
{
	return Poi.Type == rgen::PoiType::Village || Poi.Type == rgen::PoiType::Finish || (Poi.Type == rgen::PoiType::Town && Poi.S > 1.0);
}

// Точная геометрия трассы из OpenStreetMap (Tools/MapPipeline/fetch_route.py → Content/Data/Routes/r255.json):
// частые точки дороги с километражем. Именованные пункты атласа «прилипают» к линии по своему километражу.
void ApplyOsmGeometry(rgen::RouteParams& Params)
{
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/Routes/r255.json");
	FString Text;
	if (!FPaths::FileExists(File) || !FFileHelper::LoadFileToString(Text, *File))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("points"), Points) || Points->Num() < 10)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Peregon] %s не прочитан — используется встроенный атлас"), *File);
		return;
	}
	struct FGeo
	{
		double Lat, Lon, Km, Elev;
	};
	TArray<FGeo> Line;
	for (const TSharedPtr<FJsonValue>& V : *Points)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (V.IsValid() && V->TryGetObject(Obj))
		{
			FGeo G{0.0, 0.0, 0.0, -1.0};
			(*Obj)->TryGetNumberField(TEXT("lat"), G.Lat);
			(*Obj)->TryGetNumberField(TEXT("lon"), G.Lon);
			(*Obj)->TryGetNumberField(TEXT("km"), G.Km);
			(*Obj)->TryGetNumberField(TEXT("elev"), G.Elev);
			Line.Add(G);
		}
	}
	const rgen::GeoPoint Origin = rgen::NovosibirskKemerovoOrigin();
	auto AtKm = [&Line](double Km) {
		for (int32 I = 1; I < Line.Num(); ++I)
		{
			if (Line[I].Km >= Km)
			{
				const double T = FMath::Clamp((Km - Line[I - 1].Km) / FMath::Max(1e-6, Line[I].Km - Line[I - 1].Km), 0.0, 1.0);
				return FGeo{FMath::Lerp(Line[I - 1].Lat, Line[I].Lat, T), FMath::Lerp(Line[I - 1].Lon, Line[I].Lon, T), Km, -1.0};
			}
		}
		return Line.Last();
	};
	// std::vector, а не TArray: в Anchor есть std::string, его нельзя перемещать побайтно.
	std::vector<rgen::Anchor> Named;
	for (const rgen::Anchor& A : Params.Anchors)
	{
		if (A.Type != rgen::PoiType::Waypoint)
		{
			Named.push_back(A);
		}
	}
	const int32 NamedCount = static_cast<int32>(Named.size());
	std::vector<rgen::Anchor> Merged;
	int32 NamedIndex = 0;
	for (const FGeo& G : Line)
	{
		while (NamedIndex < NamedCount && Named[static_cast<size_t>(NamedIndex)].Km <= G.Km)
		{
			rgen::Anchor A = Named[static_cast<size_t>(NamedIndex++)];
			const FGeo Snap = AtKm(A.Km);
			rgen::GeoToGame(Origin, Snap.Lat, Snap.Lon, A.X, A.Y);
			Merged.push_back(A);
		}
		bool bNearNamed = false;
		for (const rgen::Anchor& A : Named)
		{
			bNearNamed |= FMath::Abs(A.Km - G.Km) < 1.0;
		}
		if (bNearNamed)
		{
			continue;
		}
		rgen::Anchor W;
		rgen::GeoToGame(Origin, G.Lat, G.Lon, W.X, W.Y);
		W.Km = G.Km;
		W.Z = Merged.empty() ? 0.0 : Merged.back().Z;
		if (G.Elev >= 0.0)
		{
			W.Z = (G.Elev - 150.0) * rgen::kGameScale;
		}
		Merged.push_back(W);
	}
	for (; NamedIndex < NamedCount; ++NamedIndex)
	{
		rgen::Anchor A = Named[static_cast<size_t>(NamedIndex)];
		const FGeo Snap = AtKm(A.Km);
		rgen::GeoToGame(Origin, Snap.Lat, Snap.Lon, A.X, A.Y);
		Merged.push_back(A);
	}
	Params.Anchors = Merged;
	Params.WanderAmplitude = 0.02; // по OSM дорога и так настоящая
	UE_LOG(LogTemp, Log, TEXT("[Peregon] Геометрия Р-255 из OSM: %d точек"), Line.Num());
}
} // namespace PeregonRoad

APeregonRoadWorld::APeregonRoadWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);

	FarGround = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FarGround"));
	FarGround->SetupAttachment(RootComponent);
	FarGround->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FarGround->SetMobility(EComponentMobility::Movable);
	FarGround->SetRelativeScale3D(FVector(400.f, 400.f, 0.1f)); // 40 × 40 км
	FarGround->CastShadow = false;
}

void APeregonRoadWorld::SetupAssets()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	auto MakePhys = [this](EPhysicalSurface Surface) {
		UPhysicalMaterial* Phys = NewObject<UPhysicalMaterial>(this);
		Phys->SurfaceType = Surface;
		return Phys;
	};
	AsphaltPhys = MakePhys(PeregonRoad::AsphaltSurface);
	GravelPhys = MakePhys(PeregonRoad::GravelSurface);
	GrassPhys = MakePhys(PeregonRoad::GrassSurface);

	Materials.SetNum(static_cast<int32>(EPeregonWorldMat::Count));
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	if (!Art)
	{
		return;
	}
	auto Set = [&](EPeregonWorldMat Slot, const FPeregonLook& Look) { Materials[static_cast<int32>(Slot)] = Art->MakeMaterial(this, Look); };
	Set(EPeregonWorldMat::Road, FPeregonLook(EPeregonTex::RoadMarked, FLinearColor::White, 0.85f));
	Set(EPeregonWorldMat::Shoulder, FPeregonLook(EPeregonTex::Gravel, FLinearColor::White, 0.95f));
	Set(EPeregonWorldMat::Grass, FPeregonLook(EPeregonTex::Grass, FLinearColor::White, 0.95f));
	Set(EPeregonWorldMat::Pad, FPeregonLook(EPeregonTex::Asphalt, FLinearColor(0.95f, 0.95f, 0.95f), 0.85f));
	Set(EPeregonWorldMat::Wood, FPeregonLook(EPeregonTex::WoodPlanks, FLinearColor(0.62f, 0.48f, 0.34f), 0.9f));
	Set(EPeregonWorldMat::Roof, FPeregonLook(EPeregonTex::RoofMetal, FLinearColor(0.55f, 0.58f, 0.6f), 0.5f, 0.6f));
	Set(EPeregonWorldMat::WhitePaint, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.85f, 0.85f, 0.82f), 0.6f));
	Set(EPeregonWorldMat::DarkPaint, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.05f, 0.05f, 0.05f), 0.6f));
	Set(EPeregonWorldMat::Concrete, FPeregonLook(EPeregonTex::Concrete, FLinearColor::White, 0.9f));
	FPeregonLook GlassLook(EPeregonTex::Glass, FLinearColor::White, 0.1f);
	GlassLook.WetMask = 0.f;
	GlassLook.SnowMask = 0.f;
	Set(EPeregonWorldMat::Glass, GlassLook);
	Set(EPeregonWorldMat::BirchBark, FPeregonLook(EPeregonTex::BirchBark, FLinearColor::White, 0.9f));
	Set(EPeregonWorldMat::PineBark, FPeregonLook(EPeregonTex::PineBark, FLinearColor::White, 0.95f));
	Set(EPeregonWorldMat::Pole, FPeregonLook(EPeregonTex::PineBark, FLinearColor(0.45f, 0.4f, 0.35f), 0.95f));
	FPeregonLook FarLook(EPeregonTex::Needles, FLinearColor(0.8f, 0.9f, 0.8f), 1.f);
	FarLook.WorldTileCm = 1200.f;
	Set(EPeregonWorldMat::Far, FarLook);

	// Трава и кусты качаются на ветру (вес — альфа цвета вершин: корни стоят, верхушки гнутся).
	FPeregonLook Tuft(EPeregonTex::GrassTuft, FLinearColor(0.55f, 0.62f, 0.38f), 0.95f);
	Tuft.Sway = 1.f;
	Set(EPeregonWorldMat::GrassTuft, Tuft);
	FPeregonLook BushLook(EPeregonTex::Bush, FLinearColor(0.42f, 0.55f, 0.3f), 0.9f);
	BushLook.Sway = 0.45f;
	Set(EPeregonWorldMat::Bush, BushLook);
	FPeregonLook TwigLook(EPeregonTex::Bush, FLinearColor(0.32f, 0.28f, 0.25f), 0.95f);
	TwigLook.Sway = 0.25f;
	Set(EPeregonWorldMat::Twigs, TwigLook);
	FPeregonLook FarLand(EPeregonTex::Grass, FLinearColor::White, 1.f);
	FarLand.WorldTileCm = 2400.f;
	Set(EPeregonWorldMat::FarLand, FarLand);
	FPeregonLook RidgeLook(EPeregonTex::Needles, FLinearColor(0.75f, 0.85f, 0.85f), 1.f);
	RidgeLook.WorldTileCm = 6000.f;
	RidgeLook.SnowMask = 0.f;
	Set(EPeregonWorldMat::Ridge, RidgeLook);
	Set(EPeregonWorldMat::Panel, FPeregonLook(EPeregonTex::PanelFacade, FLinearColor::White, 0.85f));
	Set(EPeregonWorldMat::Brick, FPeregonLook(EPeregonTex::BrickFacade, FLinearColor::White, 0.9f));
	Set(EPeregonWorldMat::CityRoof, FPeregonLook(EPeregonTex::Concrete, FLinearColor(0.42f, 0.42f, 0.44f), 0.9f));
	// Окна, в которых вечером горит свет: днём — шторы, ночью — тёплый свет.
	FPeregonLook Window(EPeregonTex::White, FLinearColor(0.85f, 0.68f, 0.38f), 0.6f);
	Window.NightGlow = 1.6f;
	Window.WetMask = 0.f;
	Window.SnowMask = 0.f;
	Set(EPeregonWorldMat::LitWindow, Window);
	FPeregonLook Gold(EPeregonTex::Chrome, FLinearColor(1.f, 0.72f, 0.28f), 0.25f, 1.f);
	Gold.SnowMask = 0.f;
	Set(EPeregonWorldMat::Gold, Gold);
	Set(EPeregonWorldMat::RedPaint, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.55f, 0.06f, 0.04f), 0.6f));
	FPeregonLook Lamp(EPeregonTex::White, FLinearColor(1.f, 0.78f, 0.45f), 0.5f);
	Lamp.NightGlow = 12.f;
	Lamp.WetMask = 0.f;
	Lamp.SnowMask = 0.f;
	Set(EPeregonWorldMat::LampGlow, Lamp);
	Set(EPeregonWorldMat::VertexPaint, FPeregonLook(EPeregonTex::Paint, FLinearColor::White, 0.6f));
	Set(EPeregonWorldMat::Stele, FPeregonLook(EPeregonTex::Concrete, FLinearColor(0.95f, 0.95f, 0.93f), 0.8f));
	Set(EPeregonWorldMat::SteleAccent, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.08f, 0.22f, 0.5f), 0.5f));
	FPeregonLook Leaves(EPeregonTex::Leaves, FLinearColor::White, 0.9f);
	Leaves.Sway = 0.3f;
	Set(EPeregonWorldMat::Leaves, Leaves);
	FPeregonLook Needles(EPeregonTex::Needles, FLinearColor::White, 0.95f);
	Needles.Sway = 0.12f;
	Set(EPeregonWorldMat::Needles, Needles);
	Set(EPeregonWorldMat::PanelTile, FPeregonLook(EPeregonTex::PanelTile, FLinearColor::White, 0.8f));
	Set(EPeregonWorldMat::PanelLoggia, FPeregonLook(EPeregonTex::PanelLoggia, FLinearColor::White, 0.85f));
	Set(EPeregonWorldMat::Silicate, FPeregonLook(EPeregonTex::SilicateBrick, FLinearColor::White, 0.9f));
	Set(EPeregonWorldMat::Stucco, FPeregonLook(EPeregonTex::Stucco, FLinearColor::White, 0.85f));
	Set(EPeregonWorldMat::Ribbon, FPeregonLook(EPeregonTex::Ribbon, FLinearColor::White, 0.7f));
	Set(EPeregonWorldMat::Marking, FPeregonLook(EPeregonTex::RoadPaint, FLinearColor(0.92f, 0.92f, 0.9f), 0.55f));
	Set(EPeregonWorldMat::RoofGreen, FPeregonLook(EPeregonTex::RoofMetal, FLinearColor(0.25f, 0.42f, 0.3f), 0.5f, 0.5f));
	Set(EPeregonWorldMat::RoofRed, FPeregonLook(EPeregonTex::RoofMetal, FLinearColor(0.55f, 0.18f, 0.14f), 0.5f, 0.5f));
	Set(EPeregonWorldMat::RoofBlue, FPeregonLook(EPeregonTex::RoofMetal, FLinearColor(0.2f, 0.32f, 0.55f), 0.5f, 0.5f));
	Set(EPeregonWorldMat::Water, FPeregonLook(EPeregonTex::Glass, FLinearColor(0.1f, 0.16f, 0.2f), 0.04f, 0.f));
	Set(EPeregonWorldMat::Sidewalk, FPeregonLook(EPeregonTex::Concrete, FLinearColor(0.78f, 0.76f, 0.72f), 0.85f));
	Set(EPeregonWorldMat::Steel, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.34f, 0.42f, 0.5f), 0.35f, 0.6f));
	// Двор: та же трава, но выгоревшая и пыльная — городская земля не должна выглядеть дачным газоном.
	Set(EPeregonWorldMat::Yard, FPeregonLook(EPeregonTex::Grass, FLinearColor(0.74f, 0.7f, 0.55f), 0.97f));
}

UMaterialInterface* APeregonRoadWorld::Mat(EPeregonWorldMat Type) const
{
	const int32 Index = static_cast<int32>(Type);
	return Materials.IsValidIndex(Index) ? Materials[Index].Get() : nullptr;
}

void APeregonRoadWorld::EndPlay(const EEndPlayReason::Type Reason)
{
	// АЗС и кафе — отдельные акторы, их нужно убрать вместе с миром.
	TArray<int32> Keys;
	Chunks.GetKeys(Keys);
	for (int32 Key : Keys)
	{
		DestroyChunk(Key);
	}
	for (TWeakObjectPtr<UActorComponent>& Comp : Scenery)
	{
		if (Comp.IsValid())
		{
			Comp->DestroyComponent();
		}
	}
	Scenery.Reset();
	Super::EndPlay(Reason);
}

void APeregonRoadWorld::Generate(int32 Seed)
{
	TArray<int32> Keys;
	Chunks.GetKeys(Keys);
	for (int32 Key : Keys)
	{
		DestroyChunk(Key);
	}
	if (!CubeMesh)
	{
		SetupAssets();
	}

	// Настоящий маршрут: Новосибирск — Кемерово по Р-255 (дороги 1:2, города — 1:1 из OSM).
	rgen::RouteParams Params = rgen::NovosibirskKemerovo(static_cast<uint32_t>(Seed));
	PeregonRoad::ApplyOsmGeometry(Params);
	Route.Build(Params);
	UE_LOG(LogTemp, Log, TEXT("[Peregon] Маршрут «%s»: %.1f км в игре, %d точек"), UTF8_TO_TCHAR(Params.Name.c_str()), Route.Length() / 1000.0,
		static_cast<int32>(Route.Pois().size()));
	TrackedS = 0.0;
	bGenerated = true;

	FarGround->SetStaticMesh(CubeMesh);
	FarGround->SetMaterial(0, Mat(EPeregonWorldMat::Far));

	// Пейзаж до горизонта и города — один раз на весь маршрут.
	for (TWeakObjectPtr<UActorComponent>& Comp : Scenery)
	{
		if (Comp.IsValid())
		{
			Comp->DestroyComponent();
		}
	}
	Scenery.Reset();
	const double StartTime = FPlatformTime::Seconds();
	LoadRealCity();
	Landscape.Build(Route);
	BuildFarTerrain();
	BuildCities();
	BuildRealCity();
	RidgeCenter = FVector(1e12);
	bSeasonApplied = false;
	UpdateSeason(true);
	UE_LOG(LogTemp, Log, TEXT("[Peregon] Пейзаж и города построены за %.0f мс"), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void APeregonRoadWorld::BuildAround(double S)
{
	TrackedS = FMath::Clamp(S, 0.0, Route.Length());
	UpdateStreaming(true);
}

double APeregonRoadWorld::FindS(const FVector& WorldLocation, double Hint) const
{
	return Route.FindNearestS(WorldLocation.X / PeregonRoad::M, WorldLocation.Y / PeregonRoad::M, Hint);
}

double APeregonRoadWorld::LateralOffset(const FVector& WorldLocation, double S) const
{
	return Route.LateralOffset(WorldLocation.X / PeregonRoad::M, WorldLocation.Y / PeregonRoad::M, S);
}

FVector APeregonRoadWorld::RouteToWorld(double S, double T, double ZOffsetM) const
{
	double X = 0.0;
	double Y = 0.0;
	Route.ToWorld(S, T, X, Y);
	return FVector(X, Y, Route.GroundHeight(S, T) + ZOffsetM) * PeregonRoad::M;
}

FTransform APeregonRoadWorld::RoadTransform(double S, double T, double ZOffsetM) const
{
	const rgen::RouteSample Smp = Route.Sample(S);
	const FRotator Rot(FMath::RadiansToDegrees(FMath::Atan(Smp.Grade)), FMath::RadiansToDegrees(Smp.Heading), 0.0);
	return FTransform(Rot, RouteToWorld(S, T, ZOffsetM));
}

FString APeregonRoadWorld::GetPlaceName(double S) const
{
	for (const rgen::Poi& Poi : Route.Pois())
	{
		if (Poi.Type == rgen::PoiType::Border || Poi.Type == rgen::PoiType::Waypoint)
		{
			continue;
		}
		const double Radius = PeregonRoad::IsSettlement(Poi) ? 230.0 : 80.0;
		if (FMath::Abs(Poi.S - S) < Radius || (Poi.Type == rgen::PoiType::Town && Poi.S <= 1.0 && S < 150.0))
		{
			return PeregonRoad::PoiName(Poi);
		}
	}
	return TEXT("Трасса");
}

FVector APeregonRoadWorld::FocusLocation() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return FVector::ZeroVector;
	}
	if (const APawn* Pawn = PC->GetPawn())
	{
		return Pawn->GetActorLocation();
	}
	return PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
}

void APeregonRoadWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bGenerated)
	{
		return;
	}
	const FVector Focus = FocusLocation();
	const double PrevS = TrackedS;
	TrackedS = FindS(Focus, TrackedS);
	if (bAnnouncePlaces)
	{
		AnnouncePlaces(PrevS, TrackedS);
	}
	UpdateStreaming(false);
	FarGround->SetWorldLocation(FVector(Focus.X, Focus.Y, (Route.MinZ() - 40.0) * PeregonRoad::M));
	UpdateRidges(Focus);
	SeasonCheckTimer -= DeltaSeconds;
	if (SeasonCheckTimer <= 0.f)
	{
		SeasonCheckTimer = 1.f;
		UpdateSeason(false);
	}
}

void APeregonRoadWorld::AnnouncePlaces(double PrevS, double NewS)
{
	APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	if (!GM || NewS <= PrevS || NewS - PrevS > 100.0)
	{
		return;
	}
	for (const rgen::Poi& Poi : Route.Pois())
	{
		const double Entry = Poi.S - 220.0;
		if (PeregonRoad::IsSettlement(Poi) && PrevS < Entry && NewS >= Entry)
		{
			GM->Toast(PeregonRoad::PoiName(Poi));
		}
		if (Poi.Type == rgen::PoiType::Border && PrevS < Poi.S && NewS >= Poi.S)
		{
			GM->Toast(FString::Printf(TEXT("Въезд: %s. Нужна карта этой области"), *PeregonRoad::PoiName(Poi)), 6.f);
		}
		if (Poi.Type == rgen::PoiType::Finish && PrevS < Poi.S && NewS >= Poi.S)
		{
			GM->Toast(FString::Printf(TEXT("%s! Участок Р-255 пройден. Продолжение — дальше на восток."), *PeregonRoad::PoiName(Poi)), 8.f);
		}
	}
}

void APeregonRoadWorld::UpdateStreaming(bool bBuildAll)
{
	const int32 Last = FMath::FloorToInt(Route.Length() / ChunkLength) + 1;
	const int32 Current = FMath::FloorToInt(TrackedS / ChunkLength);
	const int32 From = FMath::Max(-1, Current - ChunksBehind);
	const int32 To = FMath::Min(Last, Current + ChunksAhead);

	TArray<int32> ToRemove;
	for (const auto& Pair : Chunks)
	{
		if (Pair.Key < From - 1 || Pair.Key > To + 1)
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (int32 Index : ToRemove)
	{
		DestroyChunk(Index);
	}

	// Сначала ближайшие к игроку чанки, по одному за кадр.
	for (int32 Distance = 0; Distance <= FMath::Max(ChunksAhead, ChunksBehind); ++Distance)
	{
		for (int32 Index : {Current + Distance, Current - Distance})
		{
			if (Index < From || Index > To || Chunks.Contains(Index))
			{
				continue;
			}
			BuildChunk(Index);
			if (!bBuildAll)
			{
				return;
			}
		}
	}
}

void APeregonRoadWorld::DestroyChunk(int32 Index)
{
	if (FChunk* Chunk = Chunks.Find(Index))
	{
		for (TWeakObjectPtr<UActorComponent>& Comp : Chunk->Components)
		{
			if (Comp.IsValid())
			{
				Comp->DestroyComponent();
			}
		}
		for (TWeakObjectPtr<AActor>& Actor : Chunk->Actors)
		{
			if (Actor.IsValid())
			{
				Actor->Destroy();
			}
		}
	}
	Chunks.Remove(Index);
}

// ---------------------------------------------------------------------------------------------
// Геометрия
// ---------------------------------------------------------------------------------------------
void APeregonRoadWorld::BuildGrid(FPeregonMeshBuffers& Out, const TArray<double>& Ss, const TArray<double>& Ts, double ZOffsetM, double UvTileM, bool bRoadUV) const
{
	const int32 Rows = Ss.Num();
	const int32 Cols = Ts.Num();
	const int32 Base = Out.Vertices.Num();
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 C = 0; C < Cols; ++C)
		{
			double X = 0.0;
			double Y = 0.0;
			Route.ToWorld(Ss[R], Ts[C], X, Y);
			const double Z = (bRoadUV ? Route.Sample(Ss[R]).Z : Route.GroundHeight(Ss[R], Ts[C])) + ZOffsetM;
			Out.Vertices.Add(FVector(X, Y, Z) * PeregonRoad::M);
			if (bRoadUV)
			{
				const double Half = Route.RoadHalfWidth();
				Out.UVs.Add(FVector2D((Ts[C] + Half) / (2.0 * Half), Ss[R] / 12.0));
			}
			else
			{
				Out.UVs.Add(FVector2D(X / UvTileM, Y / UvTileM));
			}
		}
	}
	// Нормали по соседям сетки: вдоль S × вдоль T (T растёт вправо) = вверх.
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 C = 0; C < Cols; ++C)
		{
			const FVector& Fwd0 = Out.Vertices[Base + FMath::Max(R - 1, 0) * Cols + C];
			const FVector& Fwd1 = Out.Vertices[Base + FMath::Min(R + 1, Rows - 1) * Cols + C];
			const FVector& Rt0 = Out.Vertices[Base + R * Cols + FMath::Max(C - 1, 0)];
			const FVector& Rt1 = Out.Vertices[Base + R * Cols + FMath::Min(C + 1, Cols - 1)];
			FVector N = FVector::CrossProduct(Fwd1 - Fwd0, Rt1 - Rt0).GetSafeNormal();
			if (N.Z < 0.0)
			{
				N = -N;
			}
			Out.Normals.Add(N.IsNearlyZero() ? FVector::UpVector : N);
		}
	}
	// Лицевая сторона треугольника в Unreal — та, для которой (P2 − P0) × (P1 − P0) смотрит наружу
	// (против часовой стрелки при взгляде на неё). Земля должна смотреть вверх: иначе солнце светит
	// ей «в спину» (дорога чёрная), а трассировки колёс проходят сквозь односторонний триангл-меш.
	for (int32 R = 0; R + 1 < Rows; ++R)
	{
		for (int32 C = 0; C + 1 < Cols; ++C)
		{
			const int32 A = Base + R * Cols + C;
			const int32 B = A + 1;
			const int32 Cn = A + Cols;
			const int32 D = Cn + 1;
			Out.Triangles.Append({A, B, Cn, B, D, Cn});
		}
	}
}

UProceduralMeshComponent* APeregonRoadWorld::AddSurface(FChunk& Chunk, const FPeregonMeshBuffers& Data, EPeregonWorldMat MatType, UPhysicalMaterial* Phys)
{
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetupAttachment(RootComponent);
	Mesh->bUseAsyncCooking = false;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionProfileName(Phys ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	if (Phys)
	{
		Mesh->SetPhysMaterialOverride(Phys);
	}
	Mesh->RegisterComponent();
	Mesh->CreateMeshSection(0, Data.Vertices, Data.Triangles, Data.Normals, Data.UVs, Data.Colors, TArray<FProcMeshTangent>(), Phys != nullptr);
	if (UMaterialInterface* Material = Mat(MatType))
	{
		Mesh->SetMaterial(0, Material);
	}
	Chunk.Components.Add(Mesh);
	return Mesh;
}

UStaticMeshComponent* APeregonRoadWorld::AddMesh(FChunk& Chunk, UStaticMesh* InMesh, const FTransform& Transform, UMaterialInterface* Material, bool bCollision)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(RootComponent);
	Comp->SetStaticMesh(InMesh);
	Comp->SetRelativeTransform(Transform);
	Comp->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->RegisterComponent();
	Chunk.Components.Add(Comp);
	return Comp;
}

UInstancedStaticMeshComponent* APeregonRoadWorld::AddInstanced(FChunk& Chunk, UStaticMesh* InMesh, EPeregonWorldMat MatType, bool bCollision)
{
	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(RootComponent);
	Comp->SetStaticMesh(InMesh);
	Comp->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	if (UMaterialInterface* Material = Mat(MatType))
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->RegisterComponent();
	Chunk.Components.Add(Comp);
	return Comp;
}

void APeregonRoadWorld::AddSign(FChunk& Chunk, double S, double T, const FString& Text, bool bCrossed, bool bFacingDriver)
{
	const rgen::RouteSample Smp = Route.Sample(S);
	const double Yaw = FMath::RadiansToDegrees(Smp.Heading) + (bFacingDriver ? 180.0 : 0.0);
	const FRotator Rot(0.0, Yaw, 0.0);
	const FVector Base = RouteToWorld(S, T, 0.0);

	AddMesh(Chunk, CubeMesh, FTransform(Rot, Base + FVector(0, 0, 110), FVector(0.08, 0.08, 2.2)), Mat(EPeregonWorldMat::DarkPaint), false);
	// Белая табличка с чёрной окантовкой (как настоящий знак населённого пункта).
	AddMesh(Chunk, CubeMesh, FTransform(Rot, Base + FVector(0, 0, 230), FVector(0.04, 2.3, 0.75)), Mat(EPeregonWorldMat::DarkPaint), false);
	// Белое поле с надписью шрифтом Golos (кириллица рисуется в текстуру).
	if (UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this))
	{
		const int32 FontPx = Text.Len() > 12 ? 24 : 38;
		UMaterialInstanceDynamic* LabelMat = Art->MakeLabelMaterial(this, Text, FLinearColor(0.01f, 0.01f, 0.01f), FLinearColor(0.85f, 0.85f, 0.82f), 256, 72, FontPx);
		if (UProceduralMeshComponent* Quad = UPeregonArtSubsystem::AddLabelQuad(this, RootComponent,
				FTransform(Rot, Base + FVector(0, 0, 230) + Rot.Vector() * 2.5), 215.f, 62.f, LabelMat))
		{
			Chunk.Components.Add(Quad);
		}
	}

	if (bCrossed)
	{
		// Красная полоса «конец населённого пункта».
		const FRotator Slash(0.0, Yaw, 18.0);
		AddMesh(Chunk, CubeMesh, FTransform(Slash, Base + FVector(0, 0, 230) + Rot.Vector() * 5.0, FVector(0.01, 2.2, 0.06)),
			Mat(EPeregonWorldMat::Roof), false);
	}
}

void APeregonRoadWorld::BuildChunk(int32 Index)
{
	using namespace PeregonRoad;
	FChunk& Chunk = Chunks.Add(Index);
	const double S0 = Index * static_cast<double>(ChunkLength);
	const double S1 = S0 + ChunkLength;

	TArray<double> Ss;
	const int32 Rows = FMath::Max(2, FMath::CeilToInt(ChunkLength / 5.0) + 1);
	for (int32 R = 0; R < Rows; ++R)
	{
		Ss.Add(S0 + (S1 - S0) * R / (Rows - 1));
	}
	const double Half = Route.RoadHalfWidth();
	const double Shoulder = Route.ShoulderOuter();

	// Полотно, обочины, рельеф.
	{
		FPeregonMeshBuffers Road;
		BuildGrid(Road, Ss, {-Half, Half}, 0.0, 1.0, true);
		AddSurface(Chunk, Road, EPeregonWorldMat::Road, AsphaltPhys);

		FPeregonMeshBuffers Shoulders;
		BuildGrid(Shoulders, Ss, {-Shoulder, -(Half + Shoulder) * 0.5, -Half}, 0.0, 3.5, false);
		BuildGrid(Shoulders, Ss, {Half, (Half + Shoulder) * 0.5, Shoulder}, 0.0, 3.5, false);
		AddSurface(Chunk, Shoulders, EPeregonWorldMat::Shoulder, GravelPhys);

		TArray<double> Right;
		TArray<double> Left;
		for (double T : TerrainT)
		{
			Right.Add(T);
			Left.Insert(-T, 0);
		}
		FPeregonMeshBuffers Terrain;
		BuildGrid(Terrain, Ss, Left, 0.0, 8.0, false);
		BuildGrid(Terrain, Ss, Right, 0.0, 8.0, false);
		AddSurface(Chunk, Terrain, EPeregonWorldMat::Grass, GrassPhys);
	}

	// Придорожная обстановка: столбики, ЛЭП, километровые знаки.
	UInstancedStaticMeshComponent* Posts = AddInstanced(Chunk, CubeMesh, EPeregonWorldMat::WhitePaint, false);
	UInstancedStaticMeshComponent* Poles = AddInstanced(Chunk, CylinderMesh, EPeregonWorldMat::Pole, true);
	UInstancedStaticMeshComponent* Wires = AddInstanced(Chunk, CubeMesh, EPeregonWorldMat::DarkPaint, false);
	for (double S = FMath::CeilToDouble(S0 / 50.0) * 50.0; S < S1; S += 50.0)
	{
		for (int32 Side : {-1, 1})
		{
			const FVector P = RouteToWorld(S, Side * 7.0, 0.0);
			Posts->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0, 0, 45), FVector(0.12, 0.12, 0.9)));
		}
		const FVector Pole = RouteToWorld(S, -16.0, 0.0);
		Poles->AddInstance(FTransform(FRotator::ZeroRotator, Pole + FVector(0, 0, 450), FVector(0.26, 0.26, 9.0)));
		Posts->AddInstance(FTransform(FRotator(0.0, FMath::RadiansToDegrees(Route.Sample(S).Heading) + 90.0, 0.0),
			Pole + FVector(0, 0, 840), FVector(0.12, 1.6, 0.12))); // траверса
		const FVector Next = RouteToWorld(S + 50.0, -16.0, 0.0);
		for (double Offset : {-60.0, 60.0})
		{
			const FVector Right = FVector::CrossProduct(FVector::UpVector, (Next - Pole).GetSafeNormal()) * Offset;
			const FVector A = Pole + FVector(0, 0, 845) + Right;
			const FVector B = Next + FVector(0, 0, 845) + Right;
			const FVector Mid = (A + B) * 0.5 - FVector(0, 0, 40); // провис
			Wires->AddInstance(FTransform(FRotationMatrix::MakeFromX(B - A).Rotator(), Mid, FVector((B - A).Size() / 100.0, 0.02, 0.02)));
		}
	}
	// Километровые знаки — настоящий километраж Р-255, через каждые 5 км.
	const double Km0 = Route.RealKmAt(FMath::Max(S0, 1.0));
	const double Km1 = Route.RealKmAt(FMath::Min(S1, Route.Length()));
	for (double Km = FMath::CeilToDouble(Km0 / 5.0) * 5.0; Km < Km1; Km += 5.0)
	{
		const double S = Route.SAtRealKm(Km);
		if (S >= S0 && S < S1)
		{
			AddSign(Chunk, S, 6.8, FString::Printf(TEXT("%d"), FMath::RoundToInt(Km)), false);
		}
	}

	// Начало и конец участка: бетонные блоки поперёк дороги (в начале — только если нет настоящего города,
	// иначе дорога уходит в его улицы).
	for (double EndS : {-12.0, Route.Length() + 12.0})
	{
		if (EndS < 0.0 && bRealCity)
		{
			continue;
		}
		if (EndS >= S0 && EndS < S1)
		{
			const rgen::RouteSample Smp = Route.Sample(EndS);
			for (double T = -Half + 0.6; T <= Half; T += 1.75)
			{
				AddMesh(Chunk, CubeMesh, FTransform(FRotator(0.0, FMath::RadiansToDegrees(Smp.Heading) + 90.0, 0.0), RouteToWorld(EndS, T, 0.45),
					FVector(1.5, 0.6, 0.9)), Mat(EPeregonWorldMat::Concrete), true);
			}
			AddSign(Chunk, EndS + (EndS < 0.0 ? 3.0 : -3.0), Half + 2.5, EndS < 0.0 ? TEXT("ДОРОГА ЗАКРЫТА") : TEXT("ПРОДОЛЖЕНИЕ СЛЕДУЕТ"), false, EndS > 0.0);
		}
	}

	// Точки интереса.
	for (const rgen::Poi& Poi : Route.Pois())
	{
		const FString Name = PoiName(Poi);
		switch (Poi.Type)
		{
		case rgen::PoiType::Town:
			if (Poi.S > 1.0)
			{
				BuildVillage(Chunk, Poi, S0, S1);
			}
			else if (S0 <= 120.0 && 120.0 < S1)
			{
				AddSign(Chunk, 120.0, Half + 3.0, Name, true);
			}
			break;
		case rgen::PoiType::Village:
		case rgen::PoiType::Finish:
			BuildVillage(Chunk, Poi, S0, S1);
			break;
		case rgen::PoiType::Border:
			if (Poi.S >= S0 && Poi.S < S1)
			{
				// Стела на границе: с одной стороны — область, из которой выезжаем, с другой — в которую въезжаем.
				const int32 Before = FMath::Max(0, Route.RegionAt(Poi.S - 5.0));
				const FString BeforeName = Route.Regions().size() > static_cast<size_t>(Before)
					? FString(UTF8_TO_TCHAR(Route.Regions()[static_cast<size_t>(Before)].Name.c_str())) : FString();
				BuildRegionStele(Chunk, Poi.S, BeforeName, Name);
			}
			break;
		case rgen::PoiType::Fuel:
		case rgen::PoiType::Cafe:
		case rgen::PoiType::Shop:
		case rgen::PoiType::Parts:
		{
			if (Poi.S < S0 || Poi.S >= S1)
			{
				break;
			}
			// Асфальтовая площадка от края полотна до здания.
			TArray<double> PadS;
			for (double S = Poi.S - 40.0; S <= Poi.S + 40.0; S += 5.0)
			{
				PadS.Add(S);
			}
			TArray<double> PadT;
			for (double T : {Half + 0.1, 8.0, 16.0, 26.0, 36.0})
			{
				PadT.Add(T);
			}
			if (Poi.Side < 0)
			{
				for (double& T : PadT)
				{
					T = -T;
				}
				Algo::Reverse(PadT);
			}
			FPeregonMeshBuffers Pad;
			BuildGrid(Pad, PadS, PadT, 0.0, 4.0, false);
			for (int32 V = 0; V < Pad.Vertices.Num(); ++V)
			{
				// Площадка ровная, вровень с дорогой (рельеф под ней выровнен генератором).
				const double S = PadS[V / PadT.Num()];
				Pad.Vertices[V].Z = (Route.Sample(S).Z + 0.01) * M;
				Pad.Normals[V] = FVector::UpVector;
			}
			AddSurface(Chunk, Pad, EPeregonWorldMat::Pad, AsphaltPhys);

			const rgen::RouteSample Smp = Route.Sample(Poi.S);
			const double Yaw = FMath::RadiansToDegrees(Smp.Heading) + (Poi.Side > 0 ? -90.0 : 90.0);
			FVector Location = RouteToWorld(Poi.S, Poi.Side * 24.0, 0.0);
			Location.Z = (Smp.Z + 0.01) * M;
			APeregonServicePoint* Service = GetWorld()->SpawnActorDeferred<APeregonServicePoint>(APeregonServicePoint::StaticClass(),
				FTransform(FRotator(0.0, Yaw, 0.0), Location), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Service)
			{
				Service->Kind = Poi.Type == rgen::PoiType::Fuel ? EPeregonService::Fuel
					: (Poi.Type == rgen::PoiType::Shop ? EPeregonService::Shop
					: (Poi.Type == rgen::PoiType::Parts ? EPeregonService::Parts : EPeregonService::Cafe));
				Service->Region = Poi.Region;
				Service->DisplayName = Name;
				Service->FinishSpawning(FTransform(FRotator(0.0, Yaw, 0.0), Location));
				Chunk.Actors.Add(Service);
			}
			break;
		}
		default:
			break;
		}
	}

	// Стелы городов на въезде.
	for (int32 Town = 0; Town < SteleS.Num(); ++Town)
	{
		if (SteleS[Town] >= S0 && SteleS[Town] < S1)
		{
			BuildStele(Chunk, SteleS[Town], Town);
		}
	}

	BuildMarkings(Chunk, S0, S1);
	BuildRoadsideSigns(Chunk, S0, S1);
	BuildTrees(Chunk, Index, S0, S1);
	BuildFoliage(Chunk, Index, S0, S1);
}

bool APeregonRoadWorld::IsClearZone(double S, double T) const
{
	// Без деревьев: у дороги, под ЛЭП, на площадках и улицах сёл.
	if (FMath::Abs(T) < 13.0 || (T < -11.0 && T > -21.0))
	{
		return true;
	}
	for (const rgen::FlattenZone& Z : Route.Flatten())
	{
		if (S > Z.S0 - 30.0 && S < Z.S1 + 30.0 && T > Z.TMin - 15.0 && T < Z.TMax + 15.0)
		{
			return true;
		}
	}
	// В городах — не сквозь дома.
	if (FMath::Abs(T) > 25.0)
	{
		double X = 0.0;
		double Y = 0.0;
		Route.ToWorld(S, T, X, Y);
		if (Landscape.UrbanMask(X, Y) > 0.0 && Landscape.InsideBuilding(X, Y, 4.0))
		{
			return true;
		}
	}
	return false;
}

void APeregonRoadWorld::BuildTrees(FChunk& Chunk, int32 Index, double S0, double S1)
{
	UInstancedStaticMeshComponent* PineTrunks = AddInstanced(Chunk, CylinderMesh, EPeregonWorldMat::PineBark, true);
	UInstancedStaticMeshComponent* PineCrowns = AddInstanced(Chunk, ConeMesh, EPeregonWorldMat::Needles, false);
	UInstancedStaticMeshComponent* BirchTrunks = AddInstanced(Chunk, CylinderMesh, EPeregonWorldMat::BirchBark, true);
	UInstancedStaticMeshComponent* BirchCrowns = AddInstanced(Chunk, SphereMesh, EPeregonWorldMat::Leaves, false);
	for (UInstancedStaticMeshComponent* Crowns : {PineCrowns, BirchCrowns})
	{
		Crowns->SetCastShadow(true);
	}

	// Берёзы по сезону: крона гуще летом, реже осенью, зимой — голые ветки (плоские «веники»).
	const double LeafAmount = bSeasonApplied ? AppliedSeason.Leaves : 1.0;
	const bool bBare = LeafAmount < 0.15;
	FPeregonMeshBuffers Twigs;
	rgen::Rng Rand(rgen::Hash(static_cast<uint32_t>(Index + 100000), Route.Params().Seed));
	const uint32_t ForestSeed = rgen::Hash(Route.Params().Seed, 0xF0E57u);
	for (double S = S0; S < S1; S += 9.0)
	{
		for (int32 Side : {-1, 1})
		{
			for (double A = 14.0; A < 280.0; A += (A < 80.0 ? 10.0 : 16.0))
			{
				const double T = Side * (A + Rand.Range(-3.0, 3.0));
				const double Sj = S + Rand.Range(-3.5, 3.5);
				if (IsClearZone(Sj, T))
				{
					continue;
				}
				double X = 0.0;
				double Y = 0.0;
				Route.ToWorld(Sj, T, X, Y);
				// Лес пятнами: перелески, поля, опушки.
				// Та же густота, что у дальних массивов (rgen::Landscape); в городах деревьев меньше.
				const double Density = Landscape.ForestDensity(X, Y) + 0.03 * Landscape.UrbanMask(X, Y);
				if (!Rand.Chance(Density))
				{
					continue;
				}
				const FVector Base = FVector(X, Y, Route.GroundHeight(Sj, T) - 0.2) * PeregonRoad::M;
				const double Scale = Rand.Range(0.75, 1.3);
				const double Yaw = Rand.Range(0.0, 360.0);
				const bool bPine = rgen::Noise2D(X / 160.0, Y / 160.0, ForestSeed + 3) + Rand.Range(-0.4, 0.4) > -0.1;
				if (bPine)
				{
					const double H = 13.0 * Scale;
					PineTrunks->AddInstance(FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, H * 50.0), FVector(0.38 * Scale, 0.38 * Scale, H)));
					PineCrowns->AddInstance(FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, H * 100.0 * 0.55), FVector(4.6 * Scale, 4.6 * Scale, H * 0.7)));
					PineCrowns->AddInstance(FTransform(FRotator(0, Yaw + 40, 0), Base + FVector(0, 0, H * 100.0 * 0.85), FVector(3.0 * Scale, 3.0 * Scale, H * 0.45)));
				}
				else
				{
					const double H = 10.0 * Scale;
					BirchTrunks->AddInstance(FTransform(FRotator(Rand.Range(-3.0, 3.0), Yaw, 0), Base + FVector(0, 0, H * 50.0), FVector(0.26 * Scale, 0.26 * Scale, H)));
					if (bBare)
					{
						const FVector Crown = Base + FVector(0, 0, H * 100.0 * 0.7);
						for (int32 K = 0; K < 3; ++K)
						{
							const float Ang = FMath::DegreesToRadians(static_cast<float>(Yaw) + 60.f * K);
							const FVector Axis(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
							const float R = static_cast<float>(220.0 * Scale);
							const float Hh = static_cast<float>(330.0 * Scale);
							Twigs.AddQuad(Crown - Axis * R - FVector(0, 0, Hh), Crown + Axis * R - FVector(0, 0, Hh), Crown + Axis * R + FVector(0, 0, Hh),
								Crown - Axis * R + FVector(0, 0, Hh), FVector::CrossProduct(Axis, FVector::UpVector), FVector2D(0, 1), FVector2D(1, 0),
								FColor::White, 255);
						}
					}
					else
					{
						const double Fullness = 0.55 + 0.45 * LeafAmount;
						BirchCrowns->AddInstance(FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, H * 100.0 * 0.78),
							FVector(4.2 * Scale * Fullness, 4.2 * Scale * Fullness, 6.0 * Scale * Fullness)));
					}
				}
			}
		}
	}
	if (Twigs.Vertices.Num() > 0)
	{
		AddProc(Chunk.Components, Twigs, Mat(EPeregonWorldMat::Twigs), false);
	}
}

void APeregonRoadWorld::BuildVillage(FChunk& Chunk, const rgen::Poi& Poi, double S0, double S1)
{
	const FString Name = PeregonRoad::PoiName(Poi);
	const double Half = Route.RoadHalfWidth();
	const double EntryS = Poi.S - 220.0;
	const double ExitS = Poi.S + 220.0;
	if (EntryS >= S0 && EntryS < S1)
	{
		// Знак «начало населённого пункта» стоит всегда; у городов перед ним ещё и стела (BuildStele).
		AddSign(Chunk, EntryS, Half + 3.0, Name, false);
	}
	if (ExitS >= S0 && ExitS < S1 && Poi.Type != rgen::PoiType::Finish)
	{
		AddSign(Chunk, ExitS, Half + 3.0, Name, true);
	}

	FPeregonKit Kit(this, RootComponent, Chunk.Components, KitMaterials());
	// Остановки по обе стороны: в сторону Кемерово — справа до центра, обратно — слева после.
	for (int32 Side : {1, -1})
	{
		const double StopS = Poi.S + (Side > 0 ? -70.0 : 70.0);
		if (StopS >= S0 && StopS < S1)
		{
			const rgen::RouteSample Smp = Route.Sample(StopS);
			const FVector Base = RouteToWorld(StopS, Side * (Half + 4.2), 0.0);
			// Лицом к дороге.
			const int32 Variant = static_cast<int32>(rgen::Hash(static_cast<uint32>(Poi.S), static_cast<uint32>(Side + 3)) % 4u);
			Kit.BusStop(Base, FRotator(0.0, FMath::RadiansToDegrees(Smp.Heading) + (Side > 0 ? -90.0 : 90.0), 0.0), Name, Variant);
		}
	}
	// Магазин «Продукты» на въезде (до первых домов) и грунтовка в поля в конце села.
	const double StoreS = Poi.S - 195.0;
	if (StoreS >= S0 && StoreS < S1)
	{
		const rgen::RouteSample Smp = Route.Sample(StoreS);
		const double StoreT = Half + 12.0;
		rgen::Building Store;
		Store.Kind = rgen::BuildingKind::Shop;
		Route.ToWorld(StoreS, StoreT, Store.X, Store.Y);
		Store.Z = FMath::Min(Route.GroundHeight(StoreS, StoreT - 4.5), Route.GroundHeight(StoreS, StoreT + 4.5)) - 0.2;
		// Вывеска на длинной стороне −Y здания: при Yaw = курсу дороги она смотрит на дорогу (влево).
		Store.Yaw = Smp.Heading;
		Store.Length = 14.0;
		Store.Width = 9.0;
		Store.Seed = rgen::Hash(static_cast<uint32_t>(Poi.S), 77u);
		Store.Floors = 1000 + static_cast<int>(Store.Seed % 6u); // сельская вывеска: «Магазин», «Сельпо», «Хлеб · Продукты»…
		Kit.Buildings({Store});
	}
	const double TrackS = Poi.S + 200.0;
	if (TrackS >= S0 && TrackS < S1)
	{
		TArray<FVector> Track;
		for (double T = Half + 1.0; T < 160.0; T += 8.0)
		{
			Track.Add(RouteToWorld(TrackS + FMath::Sin(T * 0.03) * 6.0, T, 0.04));
		}
		Kit.DirtRoad(Track, 400.f);
	}
	// Колодец и лавка у дороги в середине села.
	if (Poi.S + 25.0 >= S0 && Poi.S + 25.0 < S1)
	{
		const rgen::RouteSample Smp = Route.Sample(Poi.S + 25.0);
		const FRotator Along(0.0, FMath::RadiansToDegrees(Smp.Heading), 0.0);
		Kit.Well(RouteToWorld(Poi.S + 25.0, 12.0, 0.0), Along);
		Kit.Bench(RouteToWorld(Poi.S + 31.0, 12.4, 0.0), Along + FRotator(0.0, -90.0, 0.0));
	}

	TArray<UInstancedStaticMeshComponent*> Roofs;
	UInstancedStaticMeshComponent* Fence = nullptr;
	FPeregonMeshBuffers Props;
	rgen::Rng Rand(rgen::Hash(static_cast<uint32_t>(Poi.S), Route.Params().Seed));
	for (double S = Poi.S - 180.0; S < Poi.S + 180.0; S += Rand.Range(26.0, 38.0))
	{
		for (int32 Side : {-1, 1})
		{
			const uint32 HouseSeed = rgen::Hash(static_cast<uint32_t>(S * 10.0), static_cast<uint32_t>(Side + 7), Route.Params().Seed);
			const bool bHouse = rgen::HashToUnit(HouseSeed) < 0.82;
			if (S < S0 || S >= S1 || !bHouse)
			{
				continue;
			}
			if (Roofs.Num() == 0)
			{
				for (EPeregonWorldMat RoofMat : {EPeregonWorldMat::Roof, EPeregonWorldMat::RoofGreen, EPeregonWorldMat::RoofRed, EPeregonWorldMat::RoofBlue})
				{
					Roofs.Add(AddInstanced(Chunk, CubeMesh, RoofMat, false));
				}
				Fence = AddInstanced(Chunk, CubeMesh, EPeregonWorldMat::Wood, false);
			}
			BuildHouse(Chunk, S, Side, HouseSeed, Roofs, Props);
			// Двор: сарай, поленница, иногда лавка у калитки.
			{
				const rgen::RouteSample Smp = Route.Sample(S);
				const FRotator Along(0.0, FMath::RadiansToDegrees(Smp.Heading), 0.0);
				const float Pick = rgen::HashToUnit(rgen::Hash(HouseSeed, 31u));
				if (Pick < 0.6f)
				{
					Kit.Shed(RouteToWorld(S + 6.0, Side * 41.0, 0.0), Along);
				}
				if (Pick > 0.3f)
				{
					Kit.Woodpile(RouteToWorld(S - 6.0, Side * 40.0, 0.0), Along);
				}
				// Огород за домом, иногда теплица; у крайних домов — стог сена.
				if (Pick > 0.2f)
				{
					Kit.Garden(RouteToWorld(S - 1.0, Side * 52.0, 0.0), Along);
				}
				if (Pick > 0.5f)
				{
					Kit.Greenhouse(RouteToWorld(S + 5.0, Side * 60.0, 0.0), Along);
				}
				if (Pick < 0.15f)
				{
					Kit.Haystack(RouteToWorld(S, Side * 70.0, 0.0));
				}
				// Подъезд к калитке.
				Kit.DirtRoad({RouteToWorld(S + 3.0, Side * (Half + 0.5), 0.03), RouteToWorld(S + 3.0, Side * 14.0, 0.03)}, 260.f);
				if (Pick > 0.75f)
				{
					Kit.Bench(RouteToWorld(S + 4.0, Side * 14.6, 0.0), Along + FRotator(0.0, Side > 0 ? 90.0 : -90.0, 0.0));
				}
			}

			// Забор вдоль улицы: деревянный штакетник, крашеный штакетник или профлист на столбах.
			const uint32 FenceKind = rgen::Hash(HouseSeed, 57u) % 4u;
			static const FLinearColor FencePaint[] = {FLinearColor(0.25f, 0.45f, 0.3f), FLinearColor(0.2f, 0.35f, 0.6f), FLinearColor(0.55f, 0.35f, 0.2f),
				FLinearColor(0.6f, 0.62f, 0.6f)};
			const FColor Paint = FencePaint[rgen::Hash(HouseSeed, 59u) % 4u].ToFColor(false);
			for (double Fs = S - 12.0; Fs < S + 12.0; Fs += 2.0)
			{
				const FVector P = RouteToWorld(Fs, Side * 14.0, 0.0);
				const FVector Q = RouteToWorld(Fs + 2.0, Side * 14.0, 0.0);
				const float Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2((Q - P).Y, (Q - P).X)));
				if (FenceKind == 0)
				{
					Fence->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0, 0, 60), FVector(0.1, 0.1, 1.2)));
					for (double H : {35.0, 95.0})
					{
						Fence->AddInstance(FTransform(FRotationMatrix::MakeFromX(Q - P).Rotator(), (P + Q) * 0.5 + FVector(0, 0, H),
							FVector((Q - P).Size() / 100.0, 0.04, 0.12)));
					}
				}
				else if (FenceKind == 3)
				{
					// Профлист 1.8 м на трубах.
					Props.AddBox(P, Yaw, FVector(8.f, 8.f, 190.f), FVector2D(100.f, 100.f), FColor(70, 70, 72), true, false);
					Props.AddBox((P + Q) * 0.5 + FVector(0, 0, 10), Yaw, FVector(static_cast<float>((Q - P).Size()), 3.f, 170.f), FVector2D(100.f, 100.f), Paint, true, false);
				}
				else
				{
					// Крашеный штакетник: столб и пять штакетин на пролёт.
					Props.AddBox(P, Yaw, FVector(10.f, 10.f, 125.f), FVector2D(100.f, 100.f), Paint, true, false);
					for (int32 K = 1; K < 6; ++K)
					{
						const FVector Picket = FMath::Lerp(P, Q, K / 6.0);
						Props.AddBox(Picket + FVector(0, 0, 10), Yaw, FVector(8.f, 2.5f, 105.f + (K % 2) * 6.f), FVector2D(100.f, 100.f), Paint, true, false);
					}
					Props.AddBox((P + Q) * 0.5 + FVector(0, 0, 40), Yaw, FVector(static_cast<float>((Q - P).Size()), 4.f, 8.f), FVector2D(100.f, 100.f), Paint, true, true);
					Props.AddBox((P + Q) * 0.5 + FVector(0, 0, 95), Yaw, FVector(static_cast<float>((Q - P).Size()), 4.f, 8.f), FVector2D(100.f, 100.f), Paint, true, true);
				}
			}
		}
	}
	if (Props.Vertices.Num() > 0)
	{
		AddProc(Chunk.Components, Props, Mat(EPeregonWorldMat::VertexPaint), true);
	}
}

void APeregonRoadWorld::BuildHouse(FChunk& Chunk, double S, int32 Side, uint32 Seed, const TArray<UInstancedStaticMeshComponent*>& Roofs,
	FPeregonMeshBuffers& Props)
{
	rgen::Rng Rand(Seed);
	const double Length = Rand.Range(7.0, 11.0);  // вдоль улицы
	const double Depth = Rand.Range(6.0, 8.0);    // поперёк
	const double Wall = Rand.Range(2.6, 3.1);
	const double T = Side * (Rand.Range(20.0, 26.0) + Depth * 0.5);
	// Вид дома: 0 — сруб под краской, 1 — кирпичный, 2 — сруб с мансардой, 3 — обшитый сайдингом «финский».
	const uint32 Kind = rgen::Hash(Seed, 3u) % 4u;

	double Ground = 1e9;
	for (double Ds : {-Length * 0.5, Length * 0.5})
	{
		for (double Dt : {-Depth * 0.5, Depth * 0.5})
		{
			Ground = FMath::Min(Ground, Route.GroundHeight(S + Ds, T + Dt));
		}
	}
	double X = 0.0;
	double Y = 0.0;
	Route.ToWorld(S, T, X, Y);
	const double Yaw = FMath::RadiansToDegrees(Route.Sample(S).Heading);
	const FTransform House(FRotator(0.0, Yaw, 0.0), FVector(X, Y, Ground - 0.25) * PeregonRoad::M);
	auto At = [&House](double Lx, double Ly, double Lz) { return House.TransformPosition(FVector(Lx, Ly, Lz) * PeregonRoad::M); };
	const FRotator Rot = House.Rotator();
	const float YawF = static_cast<float>(Yaw);
	auto PBox = [&](double Lx, double Ly, double Lz, const FVector& SizeM, const FColor& C) {
		Props.AddBox(At(Lx, Ly, Lz), YawF, SizeM * PeregonRoad::M, FVector2D(100.f, 100.f), C, true, true);
	};

	const FLinearColor Color = PeregonRoad::HouseColors[Seed % UE_ARRAY_COUNT(PeregonRoad::HouseColors)];
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	UMaterialInterface* WallMat = nullptr;
	if (Kind == 1)
	{
		WallMat = Mat(EPeregonWorldMat::Brick);
	}
	else if (Art)
	{
		WallMat = Art->MakeMaterial(this, FPeregonLook(Kind == 3 ? EPeregonTex::Plaster : EPeregonTex::WoodPlanks, Kind == 3 ? FLinearColor(0.92f, 0.9f, 0.82f) : Color, 0.9f));
	}

	// Стены + фронтон (ромб, нижняя половина спрятана в стенах). У кирпичного — без окон текстуры (дом маленький).
	AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, (Wall + 0.25) * 0.5), FVector(Length, Depth, Wall + 0.25)), WallMat, true);
	const double Pitch = Kind == 2 ? 60.0 : 45.0;
	const double Rise = Depth * 0.5 * FMath::Tan(FMath::DegreesToRadians(Pitch));
	AddMesh(Chunk, CubeMesh, FTransform(FRotator(0.0, Yaw, 45.0), At(0, 0, Wall + 0.25), FVector(Length - 0.05, Depth / FMath::Sqrt(2.0), Depth / FMath::Sqrt(2.0))),
		WallMat, false);
	if (Kind == 2)
	{
		// Мансарда: высокие фронтоны — треугольники в цвет дома на обоих торцах.
		const FColor Gable = Color.ToFColor(false);
		for (double Ex : {-Length * 0.5 + 0.02, Length * 0.5 - 0.02})
		{
			const FVector N = House.TransformVector(FVector(Ex > 0.0 ? 1.0 : -1.0, 0.0, 0.0));
			Props.AddQuad(At(Ex, -Depth * 0.5, Wall + 0.25), At(Ex, Depth * 0.5, Wall + 0.25), At(Ex, 0, Wall + 0.25 + Rise), At(Ex, 0, Wall + 0.25 + Rise), N,
				FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), Gable, 0);
		}
	}
	// Крыша — одного из цветов.
	UInstancedStaticMeshComponent* RoofComp = Roofs.Num() > 0 ? Roofs[static_cast<int32>(rgen::Hash(Seed, 11u) % static_cast<uint32>(Roofs.Num()))] : nullptr;
	if (RoofComp)
	{
		const double SlabWidth = (Kind == 2 ? Depth * 0.5 / FMath::Cos(FMath::DegreesToRadians(Pitch)) : Depth * 0.5 * FMath::Sqrt(2.0)) + 0.5;
		const double Ridge = Kind == 2 ? Rise : Depth * 0.5;
		for (int32 Slope : {-1, 1})
		{
			RoofComp->AddInstance(FTransform(FRotator(0.0, Yaw, Pitch * Slope), At(0, Slope * Depth * 0.25, Wall + 0.25 + Ridge * 0.5 + 0.08),
				FVector(Length + 0.6, SlabWidth, 0.1)));
		}
	}
	// Окна в резных наличниках на улицу (цвет наличников — свой у каждого дома), ставни у срубов.
	static const FLinearColor Trim[] = {FLinearColor(0.92f, 0.92f, 0.9f), FLinearColor(0.2f, 0.4f, 0.75f), FLinearColor(0.25f, 0.55f, 0.35f),
		FLinearColor(0.85f, 0.75f, 0.3f)};
	const FColor TrimC = Trim[rgen::Hash(Seed, 13u) % 4u].ToFColor(false);
	const double FaceY = -Side * (Depth * 0.5 + 0.03);
	const int32 Windows = Length > 9.0 ? 3 : 2;
	for (int32 K = 0; K < Windows; ++K)
	{
		const double Wx = -Length * 0.5 + Length * (K + 0.5) / Windows;
		PBox(Wx, FaceY, 0.85, FVector(1.25, 0.06, 1.35), TrimC);
		PBox(Wx, FaceY - Side * 0.02, 2.25, FVector(1.5, 0.08, 0.18), TrimC); // резной кокошник
		const bool bLit = rgen::HashToUnit(rgen::Hash(Seed, static_cast<uint32>(K * 10 + 50))) < 0.55;
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(Wx, FaceY - Side * 0.03, 1.55), FVector(0.95, 0.06, 1.05)),
			Mat(bLit ? EPeregonWorldMat::LitWindow : EPeregonWorldMat::Glass), false);
		PBox(Wx, FaceY - Side * 0.04, 1.5, FVector(0.95, 0.03, 0.06), TrimC); // переплёт
		if (Kind == 0 && (Seed >> K) % 2 == 0)
		{
			for (double Sh : {-0.75, 0.75})
			{
				PBox(Wx + Sh, FaceY - Side * 0.02, 0.95, FVector(0.5, 0.04, 1.15), TrimC); // ставни
			}
		}
	}
	// Слуховое окно во фронтоне торца, труба, крыльцо с навесом, тарелка антенны.
	PBox(Length * 0.5 + 0.03, 0, Wall + 0.6, FVector(0.06, 0.7, 0.6), TrimC);
	const FColor BrickC(140, 70, 50);
	PBox(-Length * 0.2, Side * Depth * 0.15, Wall, FVector(0.5, 0.5, (Kind == 2 ? Rise : Depth * 0.5) + 0.9), BrickC);
	const double PorchX = Length * 0.5 - 1.0;
	PBox(PorchX, FaceY - Side * 0.9, 0.0, FVector(1.6, 1.6, 0.45), FColor(110, 85, 60));
	PBox(PorchX, FaceY - Side * 0.9, 2.3, FVector(1.9, 1.9, 0.08), FColor(90, 90, 92));
	for (double Px : {-0.7, 0.7})
	{
		PBox(PorchX + Px, FaceY - Side * 1.7, 0.45, FVector(0.1, 0.1, 1.85), TrimC);
	}
	if (rgen::HashToUnit(rgen::Hash(Seed, 17u)) < 0.3)
	{
		PBox(Length * 0.3, FaceY, Wall - 0.3, FVector(0.08, 0.6, 0.6), FColor(225, 225, 222)); // тарелка
	}
}
