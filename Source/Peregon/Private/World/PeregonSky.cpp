#include "World/PeregonSky.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Game/PeregonGameInstance.h"
#include "Game/PeregonGameUserSettings.h"

APeregonSky::APeregonSky()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void APeregonSky::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();

	auto SpawnLight = [&](bool bIsSun) {
		const FTransform Transform(FRotator(-30.f, 0.f, 0.f), FVector(0.f, 0.f, 1000.f));
		ADirectionalLight* Light = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), Transform);
		if (!Light)
		{
			return Light;
		}
		if (UDirectionalLightComponent* Comp = Cast<UDirectionalLightComponent>(Light->GetLightComponent()))
		{
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetAtmosphereSunLight(true);
			Comp->SetAtmosphereSunLightIndex(bIsSun ? 0 : 1);
			Comp->SetIntensity(bIsSun ? 9.f : 0.35f);
			Comp->SetLightColor(bIsSun ? FLinearColor(1.f, 0.96f, 0.9f) : FLinearColor(0.55f, 0.65f, 1.f));
			Comp->SetCastShadows(bIsSun);
		}
		Light->FinishSpawning(Transform);
		Spawned.Add(Light);
		return Light;
	};
	Sun = SpawnLight(true);
	Moon = SpawnLight(false);

	Spawned.Add(World->SpawnActor<ASkyAtmosphere>());
	Spawned.Add(World->SpawnActor<AVolumetricCloud>());

	const FTransform Identity;
	if (ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), Identity))
	{
		if (USkyLightComponent* SkyComp = Sky->GetLightComponent())
		{
			SkyComp->SetMobility(EComponentMobility::Movable);
			SkyComp->bRealTimeCapture = true;
		}
		Sky->FinishSpawning(Identity);
		Spawned.Add(Sky);
	}

	// Объёмный туман: сибирская дымка, заодно прячет край генерируемого мира.
	Fog = World->SpawnActor<AExponentialHeightFog>();
	if (Fog)
	{
		if (UExponentialHeightFogComponent* FogComp = Fog->GetComponent())
		{
			FogComp->SetFogDensity(0.035f);
			FogComp->SetFogHeightFalloff(0.08f);
			FogComp->SetVolumetricFog(true);
		}
		Spawned.Add(Fog);
	}
	ApplyTime(GetTimeOfDay());
}

void APeregonSky::EndPlay(const EEndPlayReason::Type Reason)
{
	for (AActor* Actor : Spawned)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Spawned.Reset();
	Super::EndPlay(Reason);
}

float APeregonSky::GetTimeOfDay() const
{
	const UPeregonGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<UPeregonGameInstance>() : nullptr;
	return GI ? GI->TimeOfDayHours : 10.f;
}

bool APeregonSky::IsNight() const
{
	const float H = GetTimeOfDay();
	return H < 6.f || H > 20.5f;
}

void APeregonSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	if (!GI)
	{
		return;
	}
	if (!bFreezeTime)
	{
		const float MinutesPerSecond = UPeregonGameUserSettings::Get()->TimeScale;
		GI->TimeOfDayHours = FMath::Fmod(GI->TimeOfDayHours + DeltaSeconds * MinutesPerSecond / 60.f, 24.f);
	}
	ApplyTime(GI->TimeOfDayHours);
}

void APeregonSky::ApplyTime(float Hours)
{
	// Высота солнца: 0° в 6:00 и 18:00, максимум ~52° в полдень (лето, 55° с.ш.).
	const float Elevation = 52.f * FMath::Sin((Hours - 6.f) / 12.f * PI);
	const float Azimuth = Hours / 24.f * 360.f - 90.f;
	if (Sun)
	{
		Sun->SetActorRotation(FRotator(-Elevation, Azimuth, 0.f));
	}
	if (Moon)
	{
		Moon->SetActorRotation(FRotator(-FMath::Max(15.f, -Elevation), Azimuth + 180.f, 0.f));
		Moon->GetLightComponent()->SetVisibility(Elevation < 2.f);
	}
}
