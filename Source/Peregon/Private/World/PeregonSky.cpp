#include "World/PeregonSky.h"

#include "Art/PeregonArtSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Game/PeregonGameInstance.h"
#include "Game/PeregonGameUserSettings.h"
#include "Audio/PeregonAudio.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "World/PeregonAmbience.h"

namespace PeregonSkyTuning
{
// Освещённость (люксы в единицах проекта): солнце 10, полная луна 0.22.
constexpr float SunLux = 10.f;
constexpr float MoonLux = 0.22f;
// Слои облаков снизу вверх по смыслу: 0 — кучевые, 1 — разорванные нижние, 2 — сплошная пелена непогоды,
// 3 — высококучевые «барашки», 4 — перистые полосы. Каждый плывёт со своей скоростью — небо живое и глубокое.
constexpr int32 CloudLayerCount = 5;
constexpr float CloudHeightCm[CloudLayerCount] = {180000.f, 120000.f, 150000.f, 320000.f, 600000.f};
constexpr float CloudTileCm[CloudLayerCount] = {320000.f, 140000.f, 200000.f, 180000.f, 700000.f};
constexpr float CloudSpeed[CloudLayerCount] = {3.5f, 4.5f, 2.f, 2.8f, 6.f};
// Растяжение текстуры: перистые — длинные полосы.
constexpr float CloudStretch[CloudLayerCount] = {1.f, 1.f, 1.f, 1.3f, 5.f};
constexpr float CloudHalfSizeCm = 3000000.f;

float Smooth(float A, float B, float X)
{
	return FMath::SmoothStep(A, B, X);
}

FVector DirectionFromAngles(float ElevationDeg, float AzimuthDeg)
{
	// X — север, Y — восток (как в атласе), Z — вверх.
	const float E = FMath::DegreesToRadians(ElevationDeg);
	const float A = FMath::DegreesToRadians(AzimuthDeg);
	return FVector(FMath::Cos(E) * FMath::Cos(A), FMath::Cos(E) * FMath::Sin(A), FMath::Sin(E));
}
} // namespace PeregonSkyTuning

static TAutoConsoleVariable<float> CVarPeregonCloudWind(TEXT("peregon.CloudWindScale"), 1.f,
	TEXT("Скорость движения объёмных облаков движка (множитель параметра WindVector)."), ECVF_Default);

APeregonSky::APeregonSky()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void APeregonSky::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();

	// Солнце и луна никогда не светят одновременно: днём — солнце, ночью — луна (она же тогда «солнце»
	// атмосферы, поэтому ночное небо тёмно-синее, а не дневное). Раньше обе лампы спорили за атмосферу.
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
			Comp->SetAtmosphereSunLightIndex(0);
			Comp->SetIntensity(bIsSun ? PeregonSkyTuning::SunLux : PeregonSkyTuning::MoonLux);
			Comp->SetLightColor(bIsSun ? FLinearColor(1.f, 0.96f, 0.9f) : FLinearColor(0.62f, 0.72f, 1.f));
			Comp->SetCastShadows(true);
			Comp->SetLightSourceAngle(bIsSun ? 0.6f : 0.8f);
		}
		Light->FinishSpawning(Transform);
		Spawned.Add(Light);
		return Light;
	};
	Sun = SpawnLight(true);
	Moon = SpawnLight(false);
	Spawned.Add(World->SpawnActor<ASkyAtmosphere>());
	Cloud = World->SpawnActor<AVolumetricCloud>();
	Spawned.Add(Cloud);
	const FTransform Identity;
	SkyLight = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), Identity);
	if (SkyLight)
	{
		if (USkyLightComponent* SkyComp = SkyLight->GetLightComponent())
		{
			SkyComp->SetMobility(EComponentMobility::Movable);
			SkyComp->bRealTimeCapture = true;
			// Снизу — отражённый от земли свет, а не чернота: тени и нижние грани больше не «проваливаются».
			SkyComp->bLowerHemisphereIsBlack = false;
			SkyComp->SetLowerHemisphereColor(FLinearColor(0.09f, 0.1f, 0.08f));
		}
		SkyLight->FinishSpawning(Identity);
		Spawned.Add(SkyLight);
	}
	Fog = World->SpawnActor<AExponentialHeightFog>();
	if (Fog)
	{
		if (UExponentialHeightFogComponent* FogComp = Fog->GetComponent())
		{
			FogComp->SetFogDensity(0.003f);
			FogComp->SetFogHeightFalloff(0.05f);
			FogComp->SetVolumetricFog(true);
			FogComp->SetVolumetricFogScatteringDistribution(0.6f);
		}
		Spawned.Add(Fog);
	}

	// Экспозиция: замер по яркой трети кадра (то, что за окнами), локальная экспозиция поднимает тени салона.
	// Пределы яркости зависят от времени суток (ApplyTime): ночью глаз не «вытягивает» темноту до дня.
	Post = World->SpawnActor<APostProcessVolume>();
	if (Post)
	{
		Post->bUnbound = true;
		Post->Priority = 10.f;
		FPostProcessSettings& P = Post->Settings;
		P.bOverride_AutoExposureMethod = true;
		P.AutoExposureMethod = AEM_Histogram;
		P.bOverride_AutoExposureLowPercent = true;
		P.AutoExposureLowPercent = 65.f;
		P.bOverride_AutoExposureHighPercent = true;
		P.AutoExposureHighPercent = 97.f;
		P.bOverride_AutoExposureSpeedUp = true;
		P.AutoExposureSpeedUp = 1.6f;
		P.bOverride_AutoExposureSpeedDown = true;
		P.AutoExposureSpeedDown = 1.2f;
		P.bOverride_AutoExposureMinBrightness = true;
		P.bOverride_AutoExposureMaxBrightness = true;
		P.bOverride_LocalExposureHighlightContrastScale = true;
		P.LocalExposureHighlightContrastScale = 0.75f;
		P.bOverride_LocalExposureShadowContrastScale = true;
		P.LocalExposureShadowContrastScale = 0.65f;
		// Lumen: немного неба просачивается в закрытые места — в тени видно детали, а не чёрную дыру.
		P.bOverride_LumenSkylightLeaking = true;
		P.LumenSkylightLeaking = 0.12f;
		P.bOverride_BloomIntensity = true;
		P.BloomIntensity = 0.3f;
		P.bOverride_LensFlareIntensity = true;
		P.LensFlareIntensity = 0.f;
		P.bOverride_MotionBlurAmount = true;
		P.MotionBlurAmount = 0.f;
		P.bOverride_VignetteIntensity = true;
		P.VignetteIntensity = 0.25f;
		Spawned.Add(Post);
	}

	SetupClouds();
	BuildSkyMeshes();

	FActorSpawnParameters Params;
	Params.Owner = this;
	Ambience = World->SpawnActor<APeregonAmbience>(APeregonAmbience::StaticClass(), FTransform::Identity, Params);
	if (Ambience)
	{
		Ambience->Sky = this;
		Spawned.Add(Ambience);
	}
	ApplyTime(GetTimeOfDay());
}

void APeregonSky::SetupClouds()
{
	// Объёмные облака движка двигаются, только если их материал умеет ветер (WindVector). Ищем параметры
	// по именам и пишем в лог — так видно, что нашлось. Нет ветра — прячем их, небо рисуют PSX-слои.
	UVolumetricCloudComponent* Comp = Cloud ? Cloud->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
	UMaterialInterface* Base = nullptr;
	if (Comp)
	{
		if (const FObjectProperty* Prop = FindFProperty<FObjectProperty>(UVolumetricCloudComponent::StaticClass(), TEXT("Material")))
		{
			Base = Cast<UMaterialInterface>(Prop->GetObjectPropertyValue_InContainer(Comp));
		}
	}
	if (Base)
	{
		CloudMid = UMaterialInstanceDynamic::Create(Base, this);
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		CloudMid->GetAllVectorParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			UE_LOG(LogTemp, Log, TEXT("[Peregon] Облака: вектор %s"), *Info.Name.ToString());
			if (Info.Name.ToString().Contains(TEXT("Wind")))
			{
				CloudWindParam = Info.Name;
			}
		}
		Infos.Reset();
		Ids.Reset();
		CloudMid->GetAllScalarParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			const FString Name = Info.Name.ToString();
			UE_LOG(LogTemp, Log, TEXT("[Peregon] Облака: число %s"), *Name);
			if (Name.Contains(TEXT("Cover")) || Name.Contains(TEXT("Density")))
			{
				CloudCoverParams.Add({Info.Name, CloudMid->K2_GetScalarParameterValue(Info.Name)});
			}
		}
	}
	if (Comp && CloudMid && !CloudWindParam.IsNone())
	{
		Comp->SetMaterial(CloudMid);
		UE_LOG(LogTemp, Log, TEXT("[Peregon] Облака движка двигаются по ветру (%s)"), *CloudWindParam.ToString());
	}
	else if (Cloud)
	{
		UE_LOG(LogTemp, Log, TEXT("[Peregon] У объёмных облаков нет параметра ветра — небо рисуют PSX-слои"));
		Cloud->SetActorHiddenInGame(true);
		if (Comp)
		{
			Comp->SetVisibility(false);
		}
		CloudMid = nullptr;
	}
}

void APeregonSky::BuildSkyMeshes()
{
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	auto NewMesh = [this](const TCHAR* Name) {
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, Name);
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetAffectDistanceFieldLighting(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->bUseAsyncCooking = true;
		Mesh->RegisterComponent();
		Mesh->SetAbsolute(true, true, true);
		return Mesh;
	};

	// Облачные слои: один большой квадрат с повтором текстуры; плывут по ветру (UpdateSkyMeshes).
	if (Art && Art->HasWeatherMaterials())
	{
		for (int32 Layer = 0; Layer < PeregonSkyTuning::CloudLayerCount; ++Layer)
		{
			UProceduralMeshComponent* Mesh = NewMesh(*FString::Printf(TEXT("CloudLayer%d"), Layer));
			const float H = PeregonSkyTuning::CloudHalfSizeCm;
			const float Repeat = 2.f * H / PeregonSkyTuning::CloudTileCm[Layer];
			const float Rx = Repeat / PeregonSkyTuning::CloudStretch[Layer];
			const float Ry = Repeat * FMath::Sqrt(PeregonSkyTuning::CloudStretch[Layer]);
			const TArray<FVector> V = {FVector(-H, -H, 0.f), FVector(H, -H, 0.f), FVector(H, H, 0.f), FVector(-H, H, 0.f)};
			const TArray<FVector2D> Uv = {FVector2D(0.f, 0.f), FVector2D(Rx, 0.f), FVector2D(Rx, Ry), FVector2D(0.f, Ry)};
			const TArray<int32> Tri = {0, 2, 1, 0, 3, 2};
			const TArray<FVector> N = {FVector::DownVector, FVector::DownVector, FVector::DownVector, FVector::DownVector};
			Mesh->CreateMeshSection(0, V, Tri, N, Uv, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
			UMaterialInstanceDynamic* Mid = Art->MakeTranslucent(this, Layer == 2 ? EPeregonTex::White : EPeregonTex::Clouds, FLinearColor::White, 0.f, 1.f);
			Mesh->SetMaterial(0, Mid);
			// Дальние (высокие) слои рисуются первыми.
			static const int32 Sort[PeregonSkyTuning::CloudLayerCount] = {-18, -16, -14, -20, -22};
			Mesh->SetTranslucentSortPriority(Sort[Layer]);
			CloudLayers.Add(Mesh);
			CloudLayerMids.Add(Mid);
		}
	}

	if (!Art)
	{
		return;
	}
	// Звёзды: квадратики на сфере вокруг камеры; поворачиваются вокруг полюса мира.
	{
		Stars = NewMesh(TEXT("Stars"));
		TArray<FVector> V;
		TArray<int32> Tri;
		TArray<FVector> N;
		TArray<FVector2D> Uv;
		TArray<FColor> Colors;
		FRandomStream Rand(1987);
		constexpr float Radius = 160000.f;
		for (int32 K = 0; K < 1400; ++K)
		{
			const FVector Dir = Rand.GetUnitVector();
			const float Bright = FMath::Pow(Rand.FRand(), 3.f);
			const float Size = 260.f + 420.f * Bright;
			const FVector Center = Dir * Radius;
			FVector Right = FVector::CrossProduct(Dir, FMath::Abs(Dir.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal() * Size;
			FVector Up = FVector::CrossProduct(Right.GetSafeNormal(), Dir) * Size;
			const int32 B = V.Num();
			V.Append({Center - Right - Up, Center + Right - Up, Center + Right + Up, Center - Right + Up});
			Tri.Append({B, B + 1, B + 2, B, B + 2, B + 3});
			const uint8 Level = static_cast<uint8>(90 + 165 * Bright);
			const uint8 Warm = static_cast<uint8>(Level / 2 + 60);
			const FColor Tint = Rand.FRand() < 0.2f ? FColor(Level, Level, 255) : (Rand.FRand() < 0.15f ? FColor(255, Level, Warm) : FColor(Level, Level, Level));
			for (int32 I = 0; I < 4; ++I)
			{
				N.Add(-Dir);
				Uv.Add(FVector2D(0.5f, 0.5f));
				Colors.Add(Tint);
			}
		}
		Stars->CreateMeshSection(0, V, Tri, N, Uv, Colors, TArray<FProcMeshTangent>(), false);
		FPeregonLook Look(EPeregonTex::White, FLinearColor::Black, 1.f);
		Look.WetMask = 0.f;
		Look.SnowMask = 0.f;
		StarsMid = Art->MakeMaterial(this, Look);
		Stars->SetMaterial(0, StarsMid);
	}
	// Луна: светящийся диск (крупнее настоящей — так её видно в низком разрешении).
	{
		MoonDisc = NewMesh(TEXT("MoonDisc"));
		TArray<FVector> V = {FVector::ZeroVector};
		TArray<int32> Tri;
		TArray<FVector> N = {FVector::ForwardVector};
		TArray<FVector2D> Uv = {FVector2D(0.5f, 0.5f)};
		constexpr int32 Segments = 20;
		for (int32 K = 0; K < Segments; ++K)
		{
			const float A = 2.f * PI * K / Segments;
			V.Add(FVector(0.f, FMath::Cos(A), FMath::Sin(A)) * 2600.f);
			N.Add(FVector::ForwardVector);
			Uv.Add(FVector2D(0.5f, 0.5f));
			Tri.Append({0, 1 + K, 1 + (K + 1) % Segments});
		}
		MoonDisc->CreateMeshSection(0, V, Tri, N, Uv, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		FPeregonLook Look(EPeregonTex::White, FLinearColor(0.85f, 0.88f, 0.95f), 1.f);
		Look.WetMask = 0.f;
		Look.SnowMask = 0.f;
		MoonMid = Art->MakeMaterial(this, Look);
		MoonDisc->SetMaterial(0, MoonMid);
	}
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

float APeregonSky::GetDayOfYear() const
{
	const UPeregonGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<UPeregonGameInstance>() : nullptr;
	return GI ? GI->Climate.DayOfYear : 258.f;
}

bool APeregonSky::IsNight() const
{
	return SunElevation < -3.f;
}

FString APeregonSky::DescribeWeather() const
{
	const UPeregonGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<UPeregonGameInstance>() : nullptr;
	if (!GI)
	{
		return FString();
	}
	const rgen::ClimateState State = GI->Climate.ToState();
	const rgen::Weather Shown = State.Blend >= 0.5 ? State.Next : State.Current;
	const int32 Temp = FMath::RoundToInt(Temperature);
	return FString::Printf(TEXT("%s%d °C · %s"), Temp > 0 ? TEXT("+") : TEXT(""), Temp, UTF8_TO_TCHAR(rgen::WeatherName(Shown, Mix.Snow > Mix.Rain)));
}

FString APeregonSky::DescribeDate() const
{
	static const TCHAR* Months[] = {TEXT("января"), TEXT("февраля"), TEXT("марта"), TEXT("апреля"), TEXT("мая"), TEXT("июня"), TEXT("июля"),
		TEXT("августа"), TEXT("сентября"), TEXT("октября"), TEXT("ноября"), TEXT("декабря")};
	static const int32 Days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int32 D = FMath::Clamp(FMath::FloorToInt(GetDayOfYear()), 1, 365);
	int32 M = 0;
	while (M < 11 && D > Days[M])
	{
		D -= Days[M++];
	}
	return FString::Printf(TEXT("%d %s"), D, Months[M]);
}

void APeregonSky::CycleWeather()
{
	if (UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>())
	{
		rgen::ClimateState State = GI->Climate.ToState();
		const rgen::Weather From = State.Blend >= 0.5 ? State.Next : State.Current;
		rgen::Climate::Force(State, static_cast<rgen::Weather>((static_cast<int32>(From) + 1) % static_cast<int32>(rgen::Weather::Count)));
		GI->Climate.FromState(State);
	}
}

void APeregonSky::AdvanceDays(int32 Days)
{
	if (UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>())
	{
		GI->Climate.DayOfYear = FMath::Fmod(GI->Climate.DayOfYear - 1.f + static_cast<float>(Days) + 365.f * 4.f, 365.f) + 1.f;
	}
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
		const float DtHours = DeltaSeconds * MinutesPerSecond / 60.f;
		float Hours = GI->TimeOfDayHours + DtHours;
		if (Hours >= 24.f)
		{
			// Полночь: новый день и в дороге, и в календаре.
			Hours -= 24.f;
			GI->Day += 1;
			GI->Climate.DayOfYear = GI->Climate.DayOfYear >= 365.f ? 1.f : GI->Climate.DayOfYear + 1.f;
		}
		GI->TimeOfDayHours = Hours;
		rgen::ClimateState State = GI->Climate.ToState();
		rgen::Climate::Step(State, Hours, DtHours);
		GI->Climate.FromState(State);
	}
	ApplyTime(GI->TimeOfDayHours);

	FVector Camera = GetActorLocation();
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			Camera = PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	UpdateSkyMeshes(Camera, DeltaSeconds);
	UpdateStorm(Camera, DeltaSeconds);

	// Общая погода в материалах мира: мокрые дороги, снег на земле и крышах, ветер в траве и кронах.
	if (UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this))
	{
		const float Snow = FMath::Clamp(static_cast<float>(FMath::Max(Season.GroundSnow, GI->Climate.SnowCover * 0.85)), 0.f, 1.f);
		// Ветер в материале — смещение вершин, см: лёгкий бриз ~6 см у верхушек травы, буря — до 40.
		const FVector WindCm = Wind.GetSafeNormal2D() * (4.f + 2.2f * Wind.Size2D());
		Art->SetWorldParams(GI->Climate.Wetness * (1.f - Snow * 0.6f), Snow, WindCm, Night);
	}
}

void APeregonSky::ApplyTime(float Hours)
{
	using namespace PeregonSkyTuning;
	const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	const rgen::ClimateState State = GI ? GI->Climate.ToState() : rgen::ClimateState();
	const double DayOfYear = State.DayOfYear;
	Mix = rgen::Climate::Mix(State, Hours);
	Season = rgen::Climate::Season(DayOfYear);
	Temperature = static_cast<float>(rgen::Climate::TemperatureC(DayOfYear, Hours, Mix.Cloud));

	double SunElev = 0.0;
	double SunAz = 0.0;
	rgen::Climate::SunPosition(DayOfYear, Hours, SunElev, SunAz);
	double MoonElev = 0.0;
	double MoonAz = 0.0;
	double Phase = 0.5;
	rgen::Climate::MoonPosition(DayOfYear, Hours, MoonElev, MoonAz, Phase);
	SunElevation = static_cast<float>(SunElev);
	MoonPhase = static_cast<float>(Phase);
	const float Day = Smooth(-6.f, 4.f, SunElevation);
	Night = 1.f - Smooth(-12.f, -3.f, SunElevation);
	const float Cover = static_cast<float>(Mix.Cloud);
	const float SunShare = static_cast<float>(Mix.Sun);

	// Ветер «куда дует» (метеорологи пишут «откуда»). Порывы — медленный шум.
	const float WindFrom = FMath::DegreesToRadians(static_cast<float>(State.WindDirDeg));
	const float Gust = 0.75f + 0.25f * FMath::Sin(GetWorld()->GetTimeSeconds() * 0.37f) * FMath::Sin(GetWorld()->GetTimeSeconds() * 0.11f + 1.f);
	Wind = -FVector(FMath::Cos(WindFrom), FMath::Sin(WindFrom), 0.f) * (1.f + 13.f * static_cast<float>(Mix.Wind)) * Gust;

	const bool bSunUp = SunElevation > -4.f;
	if (Sun)
	{
		// Свет летит ОТ солнца: направление — противоположное направлению на солнце.
		Sun->SetActorRotation((-DirectionFromAngles(SunElevation, static_cast<float>(SunAz))).Rotation());
		if (UDirectionalLightComponent* Comp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Comp->SetVisibility(bSunUp);
			Comp->SetIntensity(SunLux * (0.12f + 0.88f * SunShare) * Smooth(-4.f, 2.f, SunElevation));
			// Под облаками тени мягкие и размытые.
			Comp->SetLightSourceAngle(FMath::Lerp(0.6f, 8.f, 1.f - SunShare));
		}
	}
	if (Moon)
	{
		// Луна ниже горизонта — остаётся слабое свечение неба «сверху» (звёзды, засветка), чтобы ночь не была чёрной.
		const bool bMoonUp = MoonElev > 4.0;
		const float Brightness = bMoonUp ? static_cast<float>(rgen::Climate::MoonBrightness(Phase)) * Smooth(4.f, 15.f, static_cast<float>(MoonElev)) : 0.f;
		const float MoonShown = FMath::Max(static_cast<float>(MoonElev), bMoonUp ? 8.f : 40.f);
		MoonDir = DirectionFromAngles(static_cast<float>(MoonElev), static_cast<float>(MoonAz));
		Moon->SetActorRotation((-DirectionFromAngles(MoonShown, static_cast<float>(MoonAz))).Rotation());
		if (UDirectionalLightComponent* Comp = Cast<UDirectionalLightComponent>(Moon->GetLightComponent()))
		{
			Comp->SetVisibility(!bSunUp);
			// Без луны — почти ничего (настройка «Темнота ночи»): звёзды и засветка от городов.
			const float Dark = UPeregonGameUserSettings::Get()->NightDarkness;
			Comp->SetIntensity((0.025f * (1.f - 0.85f * Dark) + MoonLux * Brightness * (1.f - 0.4f * Dark)) * (1.f - 0.75f * Cover));
			Comp->SetCastShadows(Brightness > 0.2f && Cover < 0.7f);
		}
	}
	if (SkyLight)
	{
		if (USkyLightComponent* SkyComp = SkyLight->GetLightComponent())
		{
			// Ночью небо тёмное — небесный свет усиливаем, чтобы в тенях оставались очертания.
			const float Dark = UPeregonGameUserSettings::Get()->NightDarkness;
			SkyComp->SetIntensity(FMath::Lerp(1.f + 1.6f * Night, 1.f - 0.75f * Night, Dark) + 0.35f * Cover * Day);
		}
	}
	if (Spawned.Num() > 0)
	{
		for (AActor* Actor : Spawned)
		{
			if (ASkyAtmosphere* Atmosphere = Cast<ASkyAtmosphere>(Actor))
			{
				if (USkyAtmosphereComponent* AtmoComp = Atmosphere->FindComponentByClass<USkyAtmosphereComponent>())
				{
					// Пасмурное небо серее и темнее.
					const float Lum = FMath::Lerp(1.f, 0.45f, Cover * Cover);
					AtmoComp->SetSkyLuminanceFactor(FLinearColor(Lum, Lum, Lum * (1.f + 0.05f * Cover)));
				}
				break;
			}
		}
	}

	// Туман: лёгкая дымка в ясную погоду (видно горизонт и горы), густой — в туман, дождь, метель.
	if (Fog)
	{
		if (UExponentialHeightFogComponent* FogComp = Fog->GetComponent())
		{
			const float Morning = FMath::Exp(-FMath::Square((Hours - 6.5f) / 1.5f)) * (1.f - SunShare * 0.5f);
			const float Density = 0.0016f + 0.006f * Morning + 0.0015f * Night + 0.06f * static_cast<float>(Mix.Fog) + 0.008f * static_cast<float>(Mix.Rain)
				+ 0.02f * static_cast<float>(Mix.Snow);
			FogComp->SetFogDensity(Density);
			// Утренний туман лежит в низинах: плотный у земли, а выше — ясно (крыши и верхушки леса торчат из пелены).
			FogComp->SetFogHeightFalloff(0.05f + 0.35f * Morning * (1.f - static_cast<float>(Mix.Rain)));
			// В мороз — искрящаяся дымка: голубее и светлее.
			const float Frosty = (1.f - FMath::SmoothStep(-25.f, -5.f, Temperature)) * Day;
			// Цвет дымки — от света неба: ночью не «светится».
			const float FogLight = FMath::Lerp(0.015f, 1.f, Day) * FMath::Lerp(1.f, 0.6f, Cover);
			const FLinearColor Clear(0.45f, 0.6f, 0.85f);
			const FLinearColor Grey(0.55f, 0.58f, 0.62f);
			const FLinearColor Haze = FMath::Lerp(FMath::Lerp(Clear, Grey, FMath::Clamp(Cover * 1.2f, 0.f, 1.f)), FLinearColor(0.75f, 0.85f, 1.f), Frosty * 0.6f);
			FogComp->SetFogInscatteringColor(Haze * FogLight);
			FogComp->SetVolumetricFogExtinctionScale(1.f + 3.f * static_cast<float>(Mix.Fog));
		}
	}

	// Пределы экспозиции (EV100): днём кадр не пересвечен, ночью — тёмный, но читаемый, с фарами.
	if (Post)
	{
		FPostProcessSettings& P = Post->Settings;
		// Тёмная ночь: глаз (автоэкспозиция) не может «вытянуть» темноту — без фар и фонарей почти ничего не видно.
		const float Dark = UPeregonGameUserSettings::Get()->NightDarkness;
		P.AutoExposureMinBrightness = FMath::Lerp(FMath::Lerp(-0.9f, 0.9f, Dark), 1.4f - 0.9f * Cover, Day);
		P.AutoExposureMaxBrightness = FMath::Lerp(FMath::Lerp(0.7f, 1.8f, Dark), 4.6f, Day);
		P.LumenSkylightLeaking = 0.04f + (0.06f + 0.12f * Night) * (1.f - Dark);

		// Стиль «живопись» (как «Заря»): тёплые света, прохладные синие тени, мягкий контраст, плёночное зерно.
		const bool bPainted = UPeregonGameUserSettings::Get()->VisualStyle == 0;
		const float Warm = bPainted ? 1.f : 0.f;
		P.bOverride_ColorSaturation = true;
		P.ColorSaturation = FVector4(1.f + 0.12f * Warm, 1.f + 0.12f * Warm, 1.f + 0.12f * Warm, 1.f);
		P.bOverride_ColorContrast = true;
		P.ColorContrast = FVector4(1.f - 0.08f * Warm, 1.f - 0.08f * Warm, 1.f - 0.08f * Warm, 1.f);
		P.bOverride_ColorGainShadows = true;
		P.ColorGainShadows = FVector4(1.f - 0.08f * Warm, 1.f - 0.03f * Warm, 1.f + 0.08f * Warm, 1.f);
		P.bOverride_ColorGainHighlights = true;
		P.ColorGainHighlights = FVector4(1.f + 0.05f * Warm, 1.f, 1.f - 0.07f * Warm, 1.f);
		const UPeregonGameUserSettings* Settings = UPeregonGameUserSettings::Get();
		P.bOverride_FilmGrainIntensity = true;
		P.FilmGrainIntensity = Settings->bFilmGrain ? 0.12f * Warm + 0.05f : 0.f;
		P.BloomIntensity = bPainted ? 0.5f : 0.3f;
		P.VignetteIntensity = bPainted ? 0.38f : 0.25f;
		// Мягкий «плёночный» тон: пологий носок и плечо, тени не проваливаются в чёрное днём.
		P.bOverride_FilmToe = true;
		P.FilmToe = bPainted ? 0.62f : 0.55f;
		P.bOverride_FilmShoulder = true;
		P.FilmShoulder = bPainted ? 0.3f : 0.26f;
		P.bOverride_AmbientOcclusionIntensity = true;
		P.AmbientOcclusionIntensity = bPainted ? 0.7f : 0.5f;
		P.bOverride_AmbientOcclusionRadius = true;
		P.AmbientOcclusionRadius = 120.f;
		P.bOverride_SceneFringeIntensity = true;
		P.SceneFringeIntensity = bPainted ? 0.35f : 0.f;
		// Сумерки и ночь — синеватые, как на картинах; днём — тёплые.
		P.bOverride_WhiteTemp = true;
		P.WhiteTemp = FMath::Lerp(bPainted ? 7600.f : 6500.f, bPainted ? 5900.f : 6500.f, Day);
		// Живописный фильтр (M_PeregonPainted): мазки, бумага, контур по глубине.
		if (UMaterialInstanceDynamic* Paint = GetPaintedPost())
		{
			Paint->SetScalarParameterValue(TEXT("Strength"), bPainted ? Settings->PaintStrength : 0.f);
			Paint->SetScalarParameterValue(TEXT("Radius"), 2.f + 1.5f * Settings->PaintStrength);
			Paint->SetScalarParameterValue(TEXT("Paper"), bPainted ? 0.06f : 0.f);
			Paint->SetScalarParameterValue(TEXT("Ink"), bPainted ? 0.35f * Settings->PaintStrength : 0.f);
			P.WeightedBlendables.Array.SetNum(1);
			P.WeightedBlendables.Array[0] = FWeightedBlendable(bPainted && Settings->PaintStrength > 0.01f ? 1.f : 0.f, Paint);
		}
	}

	// Объёмные облака движка (если двигаются): ветер и плотность.
	if (CloudMid)
	{
		const FVector2D Flow = FVector2D(Wind.X, Wind.Y) * 0.02f * CVarPeregonCloudWind.GetValueOnGameThread();
		CloudMid->SetVectorParameterValue(CloudWindParam, FLinearColor(Flow.X, Flow.Y, 0.f, 0.f));
		for (const TPair<FName, float>& Param : CloudCoverParams)
		{
			CloudMid->SetScalarParameterValue(Param.Key, Param.Value * FMath::Lerp(0.4f, 1.8f, Cover));
		}
	}
}

void APeregonSky::UpdateSkyMeshes(const FVector& Camera, float DeltaSeconds)
{
	using namespace PeregonSkyTuning;
	const float Day = Smooth(-6.f, 4.f, SunElevation);
	const float Cover = static_cast<float>(Mix.Cloud);
	// Закат подкрашивает облака.
	const float Dusk = (1.f - Smooth(2.f, 14.f, SunElevation)) * Day;
	const FLinearColor Lit = FMath::Lerp(FLinearColor(1.f, 1.f, 1.f), FLinearColor(1.f, 0.7f, 0.5f), Dusk);
	for (int32 Layer = 0; Layer < CloudLayers.Num(); ++Layer)
	{
		UProceduralMeshComponent* Mesh = CloudLayers[Layer];
		UMaterialInstanceDynamic* Mid = CloudLayerMids.IsValidIndex(Layer) ? CloudLayerMids[Layer].Get() : nullptr;
		if (!Mesh || !Mid)
		{
			continue;
		}
		// Верхний ветер сильнее приземного: облака заметно плывут, слои — с разной скоростью.
		const float Speed = CloudSpeed[Layer];
		CloudDrift[Layer] += FVector2D(Wind.X, Wind.Y) * 100.f * Speed * DeltaSeconds;
		const float Tile = CloudTileCm[Layer];
		const FVector2D Drift(FMath::Fmod(CloudDrift[Layer].X, Tile), FMath::Fmod(CloudDrift[Layer].Y, Tile));
		const FVector Snapped(FMath::FloorToFloat((Camera.X - Drift.X) / Tile) * Tile + Drift.X, FMath::FloorToFloat((Camera.Y - Drift.Y) / Tile) * Tile + Drift.Y,
			Camera.Z + CloudHeightCm[Layer]);
		Mesh->SetWorldLocation(Snapped);
		float Opacity = 0.f;
		FLinearColor Tint;
		const int32 Quality = UPeregonGameUserSettings::Get()->CloudQuality;
		// Какие слои включены: 0 — только пелена; 1 — кучевые, нижние и пелена; 2 — все пять.
		const bool bEnabled = Layer == 2 || (Quality >= 1 && Layer <= 1) || (Quality >= 2 && Layer >= 3);
		if (Layer == 2)
		{
			// Сплошная пелена — только в пасмурно и дождь.
			Opacity = 0.97f * Smooth(0.7f, 0.98f, Cover);
			Tint = FLinearColor(0.62f, 0.64f, 0.67f) * FMath::Lerp(1.f, 0.65f, static_cast<float>(Mix.Rain + Mix.Snow) * 0.5f);
		}
		else if (Layer >= 3)
		{
			// Высокие слои: видны и в ясную погоду (перистые — особенно перед сменой погоды), в пелене тонут.
			Opacity = (Layer == 4 ? 0.45f : 0.5f) * Smooth(0.0f, 0.35f, Cover + 0.15f) * (1.f - Smooth(0.75f, 0.95f, Cover));
			Tint = FMath::Lerp(FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.85f, 0.86f, 0.9f), Cover);
		}
		else
		{
			Opacity = (Layer == 0 ? 1.f : 0.6f) * Smooth(0.03f, 0.55f, Cover);
			Tint = FMath::Lerp(FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.55f, 0.57f, 0.6f), Smooth(0.5f, 1.f, Cover));
			// Нижние облака темнее снизу — в непогоду почти свинцовые.
			Tint *= Layer == 1 ? 0.88f : 1.f;
		}
		// Объёмные облака движка заменяют кучевые слои; высокие и пелена остаются.
		const bool bReplaced = CloudMid && Layer <= 1;
		Mesh->SetVisibility(bEnabled && !bReplaced && Opacity > 0.01f);
		Mid->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Mid->SetVectorParameterValue(TEXT("Color"), Tint * Lit);
		// Своё свечение облаков ≈ яркость неба: днём белые, в сумерках розовые, ночью почти не видны.
		Mid->SetScalarParameterValue(TEXT("Emissive"), FMath::Lerp(0.012f, 2.4f, Day) * (1.f - 0.4f * Cover));
	}
	if (CloudMid)
	{
		// Облака движка видны — PSX-слои нужны только как пелена в непогоду.
		if (CloudLayers.IsValidIndex(2) && CloudLayers[2])
		{
			CloudLayers[2]->SetVisibility(Cover > 0.72f);
		}
	}

	// Звёзды и луна видны ночью сквозь просветы облаков.
	const float StarLight = Night * (1.f - Smooth(0.35f, 0.85f, Cover));
	if (Stars && StarsMid)
	{
		Stars->SetWorldLocation(Camera);
		const float Hours = GetTimeOfDay();
		const float Sidereal = (Hours / 24.f + GetDayOfYear() / 365.f) * 360.f;
		const FVector Pole(FMath::Cos(FMath::DegreesToRadians(55.f)), 0.f, FMath::Sin(FMath::DegreesToRadians(55.f)));
		Stars->SetWorldRotation(FQuat(Pole, FMath::DegreesToRadians(-Sidereal)));
		Stars->SetVisibility(StarLight > 0.02f);
		StarsMid->SetScalarParameterValue(TEXT("Emissive"), 1.6f * StarLight);
		StarsMid->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
	}
	if (MoonDisc && MoonMid)
	{
		MoonDisc->SetWorldLocation(Camera + MoonDir * 150000.f);
		MoonDisc->SetWorldRotation((-MoonDir).Rotation());
		const float Shown = (1.f - Smooth(0.5f, 0.95f, Cover)) * Smooth(-2.f, 3.f, MoonDir.Z * 57.3f);
		MoonDisc->SetVisibility(Shown > 0.02f && MoonPhase > 0.06f && MoonPhase < 0.94f);
		MoonMid->SetScalarParameterValue(TEXT("Emissive"), (0.6f + 5.f * Night) * Shown * (0.3f + 0.7f * static_cast<float>(rgen::Climate::MoonBrightness(MoonPhase))));
	}
}

UMaterialInstanceDynamic* APeregonSky::GetPaintedPost()
{
	if (!PaintedPost && !bPaintedTried)
	{
		bPaintedTried = true;
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PeregonPainted.M_PeregonPainted")))
		{
			PaintedPost = UMaterialInstanceDynamic::Create(Base, this);
		}
	}
	return PaintedPost;
}

void APeregonSky::UpdateStorm(const FVector& Camera, float DeltaSeconds)
{
	// Гроза: в ливень (не в снег) раз в 8–40 с молния — вспышка неба, через расстояние/340 м/с — раскат.
	const float Storm = FMath::Clamp((static_cast<float>(Mix.Rain) - 0.5f) * 2.f, 0.f, 1.f) * (Temperature > 2.f ? 1.f : 0.f);
	Flash = FMath::Max(0.f, Flash - DeltaSeconds * 6.f);
	if (Storm > 0.05f)
	{
		LightningTimer -= DeltaSeconds;
		if (LightningTimer <= 0.f)
		{
			LightningTimer = FMath::FRandRange(8.f, 40.f) / (0.4f + Storm);
			const float DistanceM = FMath::FRandRange(400.f, 6000.f);
			Flash = FMath::Lerp(1.f, 0.25f, DistanceM / 6000.f);
			FlashFlicker = FMath::FRandRange(0.f, 1.f) < 0.5f ? 0.12f : 0.f; // двойная вспышка
			Thunders.Add({DistanceM / 340.f, FMath::Lerp(1.f, 0.35f, DistanceM / 6000.f)});
		}
	}
	if (FlashFlicker > 0.f)
	{
		FlashFlicker -= DeltaSeconds;
		if (FlashFlicker <= 0.f)
		{
			Flash = FMath::Max(Flash, 0.7f);
		}
	}
	for (int32 I = Thunders.Num() - 1; I >= 0; --I)
	{
		Thunders[I].Key -= DeltaSeconds;
		if (Thunders[I].Key <= 0.f)
		{
			// Звук «оттуда» — с направления ветра, в паре десятков метров (чтобы было слышно).
			const FVector From = Camera + FVector(Wind.GetSafeNormal2D().X, Wind.GetSafeNormal2D().Y, 0.3f) * -3000.f;
			PeregonAudio::PlayEvent(this, TEXT("thunder"), From, 1.4f * Thunders[I].Value, FMath::FRandRange(0.85f, 1.1f));
			Thunders.RemoveAt(I);
		}
	}
	if (Flash > 0.001f && SkyLight)
	{
		if (USkyLightComponent* SkyComp = SkyLight->GetLightComponent())
		{
			SkyComp->SetIntensity(SkyComp->Intensity + 9.f * Flash);
		}
	}
	for (UMaterialInstanceDynamic* Mid : CloudLayerMids)
	{
		if (Mid && Flash > 0.001f)
		{
			Mid->SetScalarParameterValue(TEXT("Emissive"), 3.f * Flash);
		}
	}
}
