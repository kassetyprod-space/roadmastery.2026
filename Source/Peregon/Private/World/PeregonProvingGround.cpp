#include "World/PeregonProvingGround.h"

#include "Art/PeregonArtSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "World/PeregonKit.h"
#include "World/PeregonServicePoint.h"

namespace PeregonGround
{
// Совпадает с Config/DefaultEngine.ini → PhysicalSurfaces.
constexpr EPhysicalSurface Asphalt = SurfaceType1;
constexpr EPhysicalSurface Gravel = SurfaceType2;
constexpr EPhysicalSurface Mud = SurfaceType4;
constexpr EPhysicalSurface Snow = SurfaceType5;
constexpr EPhysicalSurface Ice = SurfaceType6;

const FLinearColor AsphaltColor(0.045f, 0.045f, 0.05f);
const FLinearColor GravelColor(0.26f, 0.23f, 0.19f);
const FLinearColor SnowColor(0.82f, 0.84f, 0.88f);
const FLinearColor IceColor(0.55f, 0.70f, 0.82f);
const FLinearColor MudColor(0.11f, 0.07f, 0.04f);
const FLinearColor MarkerColor(0.9f, 0.85f, 0.2f);

constexpr float Thickness = 100.f;
} // namespace PeregonGround

APeregonProvingGround::APeregonProvingGround()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void APeregonProvingGround::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Build();
}

void APeregonProvingGround::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld() && GetWorld()->IsGameWorld() && CubeMesh)
	{
		BuildShowcase();
	}
}

void APeregonProvingGround::EndPlay(const EEndPlayReason::Type Reason)
{
	for (TWeakObjectPtr<AActor>& Actor : ShowActors)
	{
		if (Actor.IsValid())
		{
			Actor->Destroy();
		}
	}
	ShowActors.Reset();
	Super::EndPlay(Reason);
}

void APeregonProvingGround::BuildShowcase()
{
	using namespace PeregonGround;
	// Улица выставки: асфальт вдоль X на Y = 90 м, газон к северу.
	constexpr float RoadY = 9000.f;
	const float Z = -Thickness * 0.5f;
	AddBox(TEXT("ShowRoad"), FVector(14000.f, RoadY, Z + 2.f), FVector(32000.f, 800.f, Thickness), FRotator::ZeroRotator, Asphalt, AsphaltColor);
	if (UStaticMeshComponent* Lawn = AddBox(TEXT("ShowLawn"), FVector(14000.f, RoadY + 9000.f, Z), FVector(32000.f, 17200.f, Thickness), FRotator::ZeroRotator,
			Gravel, GravelColor))
	{
		FPeregonLook Grass(EPeregonTex::Grass, FLinearColor::White, 0.95f);
		Grass.WorldTileCm = 400.f;
		UPeregonArtSubsystem::ApplyLook(this, Lawn, Grass);
	}
	if (UStaticMeshComponent* Verge = AddBox(TEXT("ShowVerge"), FVector(14000.f, RoadY - 1000.f, Z), FVector(32000.f, 1200.f, Thickness), FRotator::ZeroRotator,
			Gravel, GravelColor))
	{
		FPeregonLook Grass(EPeregonTex::Grass, FLinearColor::White, 0.95f);
		Grass.WorldTileCm = 400.f;
		UPeregonArtSubsystem::ApplyLook(this, Verge, Grass);
	}

	FPeregonKit Kit(this, RootComponent, Showcase, FPeregonKitMaterials::Create(this));
	const FTransform Me = GetActorTransform();
	// Сервис — как на трассе: в 24 м от оси, лицом (+X) к дороге.
	struct FServiceSpot
	{
		EPeregonService Kind;
		const TCHAR* Name;
		float X;
	};
	const FServiceSpot Services[] = {{EPeregonService::Fuel, TEXT("АЗС «Полигон»"), 1500.f}, {EPeregonService::Cafe, TEXT("Кафе «Испытатель»"), 5500.f},
		{EPeregonService::Shop, TEXT("Магазин"), 8500.f}};
	for (const FServiceSpot& Spot : Services)
	{
		const FTransform Local(FRotator(0.f, -90.f, 0.f), FVector(Spot.X, RoadY + 2400.f, 1.f));
		const FTransform World = Local * Me;
		APeregonServicePoint* Service = GetWorld()->SpawnActorDeferred<APeregonServicePoint>(APeregonServicePoint::StaticClass(), World, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Service)
		{
			Service->Kind = Spot.Kind;
			Service->DisplayName = Spot.Name;
			Service->Region = 0;
			Service->FinishSpawning(World);
			ShowActors.Add(Service);
		}
	}
	// Остановка напротив кафе, на южной стороне, лицом к дороге (+Y).
	Kit.BusStop(FVector(5500.f, RoadY - 820.f, 0.f), FRotator(0.f, 90.f, 0.f), TEXT("Полигон"));
	// Стелы вдоль южной обочины: областной центр, город, посёлок, граница областей. Лицом к едущему по +X.
	Kit.CityStele(FVector(11500.f, RoadY - 1500.f, 0.f), FRotator(0.f, 180.f, 0.f), TEXT("Кемерово"), 2, TEXT("1918"), false);
	Kit.CityStele(FVector(14500.f, RoadY - 1300.f, 0.f), FRotator(0.f, 180.f, 0.f), TEXT("Юрга"), 1, TEXT("1949"), false);
	Kit.CityStele(FVector(16500.f, RoadY - 1300.f, 0.f), FRotator(0.f, 180.f, 0.f), TEXT("Топки"), 0, TEXT("1933"), false);
	Kit.RegionStele(FVector(19000.f, RoadY - 1600.f, 0.f), FRotator(0.f, 180.f, 0.f), TEXT("Новосибирская область"), TEXT("Кемеровская область — Кузбасс"));

	// Город: все типы зданий в ряд к северу.
	std::vector<rgen::Building> City;
	auto Add = [&City](rgen::BuildingKind Kind, double X, double Y, double L, double W, int Floors, uint32 Seed) {
		rgen::Building B;
		B.Kind = Kind;
		B.X = X;
		B.Y = Y;
		B.Z = 0.0;
		B.Yaw = 0.0;
		B.Length = L;
		B.Width = W;
		B.Floors = Floors;
		B.Seed = Seed;
		City.push_back(B);
	};
	const double Row = (RoadY + 6000.0) / 100.0; // м
	Add(rgen::BuildingKind::Panel5, 20.0, Row, 60.0, 12.0, 5, 1);
	Add(rgen::BuildingKind::Panel9, 100.0, Row, 90.0, 13.0, 9, 2);
	Add(rgen::BuildingKind::Tower, 170.0, Row, 22.0, 20.0, 16, 3);
	Add(rgen::BuildingKind::Brick, 215.0, Row, 40.0, 15.0, 4, 4);
	Add(rgen::BuildingKind::Industrial, 275.0, Row, 55.0, 25.0, 1, 5);
	Add(rgen::BuildingKind::Chimney, 320.0, Row, 7.0, 7.0, 1, 6);
	Add(rgen::BuildingKind::Church, 20.0, Row + 45.0, 26.0, 18.0, 1, 7);
	Add(rgen::BuildingKind::Mast, 320.0, Row + 45.0, 12.0, 12.0, 1, 8);
	Add(rgen::BuildingKind::Garages, 100.0, Row + 50.0, 54.0, 7.0, 1, 9);
	Add(rgen::BuildingKind::Shop, 170.0, Row + 45.0, 16.0, 9.0, 0, 10);
	Add(rgen::BuildingKind::Shop, 200.0, Row + 45.0, 14.0, 9.0, 1, 11);
	Add(rgen::BuildingKind::Playground, 260.0, Row + 50.0, 22.0, 18.0, 1, 12);
	Kit.Buildings(City);
	// Дворовый проезд вдоль ряда домов и поперечный — к гаражам.
	std::vector<rgen::Street> Lanes;
	auto Lane = [&Lanes](double X0, double Y0, double X1, double Y1) {
		rgen::Street St;
		St.X0 = X0;
		St.Y0 = Y0;
		St.X1 = X1;
		St.Y1 = Y1;
		Lanes.push_back(St);
	};
	for (double X = -20.0; X < 340.0; X += 60.0)
	{
		Lane(X, Row + 25.0, X + 60.0, Row + 25.0);
	}
	Lane(60.0, (RoadY + 450.0) / 100.0, 60.0, Row - 15.0);
	Kit.Streets(Lanes);

	// Деревенский двор: колодец, лавка, поленница, сарай, штакетник; деревья и трава вокруг.
	const FVector Yard(12500.f, RoadY + 2600.f, 0.f);
	Kit.Fence(Yard + FVector(-1500.f, -1200.f, 0.f), Yard + FVector(1500.f, -1200.f, 0.f));
	Kit.Well(Yard + FVector(-600.f, 0.f, 0.f), FRotator::ZeroRotator);
	Kit.Bench(Yard + FVector(200.f, -900.f, 0.f), FRotator(0.f, 90.f, 0.f));
	Kit.Woodpile(Yard + FVector(900.f, 600.f, 0.f), FRotator::ZeroRotator);
	Kit.Shed(Yard + FVector(-800.f, 900.f, 0.f), FRotator::ZeroRotator);
	Kit.GrassPatch(Yard + FVector(3500.f, 0.f, 0.f), 2500.f, 900, 11u);
	Kit.GrassPatch(FVector(23000.f, RoadY + 4000.f, 0.f), 3500.f, 1400, 12u);
	for (int32 K = 0; K < 14; ++K)
	{
		Kit.Tree(FVector(20000.f + K * 700.f, RoadY + 2500.f + (K % 3) * 900.f, 0.f), K % 2 == 0, 0.8f + 0.05f * (K % 5));
	}
	// Фонари вдоль улицы (ночью светятся).
	for (float X = 0.f; X < 28000.f; X += 4000.f)
	{
		Kit.StreetLamp(FVector(X, RoadY + 620.f, 0.f), FVector(0.f, -1.f, 0.f));
	}
}

FTransform APeregonProvingGround::GetSpawnTransform() const
{
	return FTransform(FRotator::ZeroRotator, GetActorLocation() + FVector(-10000.f, 0.f, 120.f));
}

UStaticMeshComponent* APeregonProvingGround::AddBox(const FName& Name, const FVector& Center, const FVector& SizeCm,
	const FRotator& Rotation, EPhysicalSurface Surface, const FLinearColor& Color, bool bCollision)
{
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, Name);
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetStaticMesh(CubeMesh);
	Mesh->SetRelativeLocationAndRotation(Center, Rotation);
	Mesh->SetRelativeScale3D(SizeCm / 100.f);
	Mesh->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Mesh->RegisterComponent();

	if (bCollision)
	{
		// Тип покрытия читает подвеска машины из результата трассировки.
		UPhysicalMaterial* PhysMat = NewObject<UPhysicalMaterial>(this);
		PhysMat->SurfaceType = Surface;
		Mesh->SetPhysMaterialOverride(PhysMat);
	}
	// Текстура по типу покрытия, мировые UV — плитка 4 м не растягивается на километровую плиту.
	EPeregonTex Tex = EPeregonTex::Asphalt;
	switch (Surface)
	{
	case PeregonGround::Gravel: Tex = EPeregonTex::Gravel; break;
	case PeregonGround::Mud: Tex = EPeregonTex::Mud; break;
	case PeregonGround::Snow: Tex = EPeregonTex::Snow; break;
	case PeregonGround::Ice: Tex = EPeregonTex::Ice; break;
	default: break;
	}
	FPeregonLook Look(bCollision ? Tex : EPeregonTex::Paint, bCollision ? FLinearColor::White : Color, bCollision ? 0.9f : 0.5f);
	Look.WorldTileCm = bCollision ? 400.f : 0.f;
	if (Surface == PeregonGround::Ice)
	{
		Look.Roughness = 0.08f;
	}
	if (UPeregonArtSubsystem::Get(this))
	{
		UPeregonArtSubsystem::ApplyLook(this, Mesh, Look);
	}
	else if (UMaterialInstanceDynamic* Mid = Mesh->CreateDynamicMaterialInstance(0))
	{
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
	}
	return Mesh;
}

void APeregonProvingGround::AddHill(const FName& Name, const FVector& Start, float RampLengthCm, float Grade, float PlateauCm, float WidthCm)
{
	using namespace PeregonGround;
	const float Angle = FMath::Atan(Grade);
	const float A = RampLengthCm * 0.5f; // половина длины рампы
	const float B = Thickness * 0.5f;
	const float Height = RampLengthCm * FMath::Sin(Angle);
	const float RunX = RampLengthCm * FMath::Cos(Angle);
	const float Deg = FMath::RadiansToDegrees(Angle);

	// Центр бокса так, чтобы нижний край верхней грани лёг ровно на землю в точке Start.
	const FVector UpCenter = Start + FVector(A * FMath::Cos(Angle) + B * FMath::Sin(Angle), 0.f, A * FMath::Sin(Angle) - B * FMath::Cos(Angle));
	AddBox(FName(*(Name.ToString() + TEXT("_Up"))), UpCenter, FVector(RampLengthCm, WidthCm, Thickness), FRotator(Deg, 0.f, 0.f), Asphalt, AsphaltColor);

	const FVector PlateauCenter = Start + FVector(RunX + PlateauCm * 0.5f, 0.f, Height - B);
	AddBox(FName(*(Name.ToString() + TEXT("_Top"))), PlateauCenter, FVector(PlateauCm + 20.f, WidthCm, Thickness), FRotator::ZeroRotator, Asphalt, AsphaltColor);

	const FVector DownStart = Start + FVector(RunX * 2.f + PlateauCm, 0.f, 0.f);
	const FVector DownCenter = DownStart + FVector(-A * FMath::Cos(Angle) - B * FMath::Sin(Angle), 0.f, A * FMath::Sin(Angle) - B * FMath::Cos(Angle));
	AddBox(FName(*(Name.ToString() + TEXT("_Down"))), DownCenter, FVector(RampLengthCm, WidthCm, Thickness), FRotator(-Deg, 0.f, 0.f), Asphalt, AsphaltColor);
}

void APeregonProvingGround::Build()
{
	if (bBuilt || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	bBuilt = true;
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("Peregon: не найден /Engine/BasicShapes/Cube"));
		return;
	}

	using namespace PeregonGround;
	const float Z = -Thickness * 0.5f; // верх всех плит — на нуле

	// Прямая: 1.3 км × 24 м — разгон, максималка, торможение.
	AddBox(TEXT("Runway"), FVector(50000.f, 0.f, Z), FVector(130000.f, 2400.f, Thickness), FRotator::ZeroRotator, Asphalt, AsphaltColor);
	// Площадка для управляемости (скидпад, змейка).
	AddBox(TEXT("Skidpad"), FVector(5000.f, -6200.f, Z), FVector(20000.f, 10000.f, Thickness), FRotator::ZeroRotator, Asphalt, AsphaltColor);
	// Полосы покрытий вдоль прямой.
	AddBox(TEXT("Gravel"), FVector(30000.f, 2700.f, Z), FVector(60000.f, 3000.f, Thickness), FRotator::ZeroRotator, Gravel, GravelColor);
	AddBox(TEXT("Snow"), FVector(15000.f, 5700.f, Z), FVector(30000.f, 3000.f, Thickness), FRotator::ZeroRotator, Snow, SnowColor);
	AddBox(TEXT("Ice"), FVector(40000.f, 5700.f, Z), FVector(20000.f, 3000.f, Thickness), FRotator::ZeroRotator, Ice, IceColor);
	AddBox(TEXT("Mud"), FVector(55000.f, 5700.f, Z), FVector(10000.f, 3000.f, Thickness), FRotator::ZeroRotator, Mud, MudColor);

	// Холм 15% на краю площадки — проверка ручника и трогания в гору.
	AddHill(TEXT("Hill15"), FVector(-4000.f, -9500.f, 0.f), 4000.f, 0.15f, 1500.f, 600.f);

	// «Стиральная доска» на гравии: поперечные гребни по 6 см.
	for (int32 I = 0; I < 50; ++I)
	{
		AddBox(FName(*FString::Printf(TEXT("Washboard%d"), I)), FVector(40000.f + I * 120.f, 2700.f, 0.f), FVector(40.f, 3000.f, 12.f),
			FRotator::ZeroRotator, Gravel, GravelColor);
	}

	// Столбики каждые 100 м вдоль прямой — ощущение скорости и замер дистанции.
	for (int32 I = 0; I <= 13; ++I)
	{
		AddBox(FName(*FString::Printf(TEXT("Marker%d"), I)), FVector(-10000.f + I * 10000.f, -1350.f, 60.f), FVector(15.f, 15.f, 120.f),
			FRotator::ZeroRotator, Asphalt, MarkerColor, false);
	}

	if (bSpawnEnvironment)
	{
		SpawnEnvironmentIfMissing();
	}
}

void APeregonProvingGround::SpawnEnvironmentIfMissing()
{
	UWorld* World = GetWorld();
	if (TActorIterator<ADirectionalLight>(World))
	{
		return; // на уровне уже есть своё освещение
	}

	const FTransform SunTransform(FRotator(-28.f, 35.f, 0.f), FVector(0.f, 0.f, 1000.f));
	if (ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), SunTransform))
	{
		if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetAtmosphereSunLight(true);
		}
		Sun->FinishSpawning(SunTransform);
	}

	World->SpawnActor<ASkyAtmosphere>();
	World->SpawnActor<AVolumetricCloud>();

	const FTransform Identity;
	if (ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), Identity))
	{
		if (USkyLightComponent* SkyComp = Sky->GetLightComponent())
		{
			SkyComp->SetMobility(EComponentMobility::Movable);
			SkyComp->bRealTimeCapture = true;
		}
		Sky->FinishSpawning(Identity);
	}

	// Объёмный туман: сибирская дымка над трассой. Плотность потом будет управляться погодой.
	if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>())
	{
		if (UExponentialHeightFogComponent* FogComp = Fog->GetComponent())
		{
			FogComp->SetFogDensity(0.02f);
			FogComp->SetVolumetricFog(true);
		}
	}
}
