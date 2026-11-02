#include "World/PeregonProvingGround.h"

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
	if (UMaterialInstanceDynamic* Mid = Mesh->CreateDynamicMaterialInstance(0))
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
