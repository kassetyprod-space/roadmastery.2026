#include "World/PeregonAmbience.h"

#include "Art/PeregonArtSubsystem.h"
#include "Audio/PeregonAudio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Vehicle/PeregonCar.h"
#include "World/PeregonRoadWorld.h"
#include "World/PeregonSky.h"
#include "rgen/Landscape.h"

namespace PeregonAmbienceTuning
{
// Ячейка сетки осадков (повторяется 2 × 2 по плану и 2 слоя по высоте).
constexpr float TileCm = 1600.f;
constexpr float HeightCm = 1100.f;
constexpr int32 RainPerTile = 320;
constexpr int32 SnowPerTile = 420;
constexpr float RainSpeedCm = 900.f;  // 9 м/с
constexpr float SnowSpeedCm = 110.f;  // ~1 м/с
constexpr int32 MaxLeaves = 240;
} // namespace PeregonAmbienceTuning

APeregonAmbience::APeregonAmbience()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	Random.Initialize(4242);
}

void APeregonAmbience::BeginPlay()
{
	Super::BeginPlay();
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	if (Art && Art->HasWeatherMaterials())
	{
		Rain = BuildPrecipitation(false);
		Snow = BuildPrecipitation(true);
		// В салоне капель нет: материал гасит всё, что ближе NearFade к камере (крыша над головой ~1.5–2.5 м).
		RainMid = Art->MakeTranslucent(this, EPeregonTex::White, FLinearColor(0.72f, 0.76f, 0.82f), 0.f, 0.04f, 230.f);
		SnowMid = Art->MakeTranslucent(this, EPeregonTex::White, FLinearColor(0.95f, 0.96f, 1.f), 0.f, 0.1f, 160.f);
		Rain->SetMaterial(0, RainMid);
		Snow->SetMaterial(0, SnowMid);
	}

	// Листопад: пул плоских листьев (плоскость движка, масштабируется до ~7 см).
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		LeafMesh = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Leaves"));
		LeafMesh->SetupAttachment(RootComponent);
		LeafMesh->SetMobility(EComponentMobility::Movable);
		LeafMesh->SetStaticMesh(Plane);
		LeafMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LeafMesh->SetCastShadow(false);
		LeafMesh->SetCanEverAffectNavigation(false);
		LeafMesh->RegisterComponent();
		if (Art)
		{
			FPeregonLook Look(EPeregonTex::Leaf, FLinearColor(0.9f, 0.7f, 0.25f), 0.8f);
			Look.WetMask = 0.4f;
			Look.SnowMask = 0.f;
			LeafMid = Art->MakeMaterial(this, Look);
			LeafMesh->SetMaterial(0, LeafMid);
		}
		Leaves.SetNum(PeregonAmbienceTuning::MaxLeaves);
		LeafTransforms.Init(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector), PeregonAmbienceTuning::MaxLeaves);
		LeafMesh->AddInstances(LeafTransforms, false, true);
	}

	// Звук: шелест дождя и ветер в поле (машина — своя, это — «снаружи»).
	auto MakeVoice = [this](const TCHAR* Name, const TCHAR* Event, EPeregonProcedural Fallback) {
		UPeregonSynth* Voice = NewObject<UPeregonSynth>(this, Name);
		Voice->SetupAttachment(RootComponent);
		Voice->SetupLoop(Event, Fallback);
		Voice->SetLevel(0.f, 0.f);
		Voice->bAllowSpatialization = false;
		Voice->RegisterComponent();
		Voice->Start();
		return Voice;
	};
	RainSound = MakeVoice(TEXT("RainSound"), TEXT("ambience.rain"), EPeregonProcedural::Rain);
	WindSound = MakeVoice(TEXT("AmbientWind"), TEXT("ambience.wind"), EPeregonProcedural::Wind);
}

UProceduralMeshComponent* APeregonAmbience::BuildPrecipitation(bool bSnow)
{
	using namespace PeregonAmbienceTuning;
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, bSnow ? TEXT("Snowfall") : TEXT("Rainfall"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetAffectDistanceFieldLighting(false);
	Mesh->bUseAsyncCooking = true;
	Mesh->RegisterComponent();
	Mesh->SetAbsolute(true, true, true);
	Mesh->SetVisibility(false);

	// Один узор на ячейку, повторённый 2 × 2 × 2: при сдвиге на целую ячейку картинка совпадает — сдвиг незаметен.
	FRandomStream Rand(bSnow ? 77 : 33);
	TArray<FVector> Pattern;
	const int32 Count = bSnow ? SnowPerTile : RainPerTile;
	for (int32 K = 0; K < Count; ++K)
	{
		Pattern.Add(FVector(Rand.FRand() * TileCm, Rand.FRand() * TileCm, Rand.FRand() * HeightCm));
	}
	TArray<FVector> V;
	TArray<int32> Tri;
	TArray<FVector> N;
	TArray<FVector2D> Uv;
	const float Len = bSnow ? 4.f : 55.f;
	const float Wid = bSnow ? 4.f : 1.4f;
	for (int32 Ix = 0; Ix < 2; ++Ix)
	{
		for (int32 Iy = 0; Iy < 2; ++Iy)
		{
			for (int32 Iz = 0; Iz < 2; ++Iz)
			{
				const FVector Offset(Ix * TileCm - TileCm, Iy * TileCm - TileCm, Iz * HeightCm - HeightCm);
				for (const FVector& P : Pattern)
				{
					const FVector C = P + Offset;
					// Две скрещённые полоски — видно с любой стороны.
					for (const FVector& Side : {FVector(Wid, 0.f, 0.f), FVector(0.f, Wid, 0.f)})
					{
						const int32 B = V.Num();
						V.Append({C - Side, C + Side, C + Side + FVector(0.f, 0.f, Len), C - Side + FVector(0.f, 0.f, Len)});
						Tri.Append({B, B + 1, B + 2, B, B + 2, B + 3});
						const FVector Normal = FVector::CrossProduct(Side, FVector::UpVector).GetSafeNormal();
						for (int32 I = 0; I < 4; ++I)
						{
							N.Add(Normal);
						}
						Uv.Append({FVector2D(0.f, 1.f), FVector2D(1.f, 1.f), FVector2D(1.f, 0.f), FVector2D(0.f, 0.f)});
					}
				}
			}
		}
	}
	Mesh->CreateMeshSection(0, V, Tri, N, Uv, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Mesh->SetTranslucentSortPriority(5);
	return Mesh;
}

APeregonRoadWorld* APeregonAmbience::FindRoadWorld()
{
	if (!RoadWorld.IsValid())
	{
		for (TActorIterator<APeregonRoadWorld> It(GetWorld()); It; ++It)
		{
			RoadWorld = *It;
			break;
		}
	}
	return RoadWorld.Get();
}

bool APeregonAmbience::IsInsideCar() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APeregonCar* Car = PC ? Cast<APeregonCar>(PC->GetPawn()) : nullptr;
	return Car && Car->IsFirstPerson();
}

void APeregonAmbience::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Sky)
	{
		return;
	}
	FVector Camera = GetActorLocation();
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			Camera = PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	const FVector CameraVelocity = DeltaSeconds > 0.f ? (Camera - LastCamera) / DeltaSeconds : FVector::ZeroVector;
	LastCamera = Camera;
	const rgen::WeatherMix& Mix = Sky->GetWeather();
	if (Rain && RainMid)
	{
		UpdatePrecipitation(Rain, RainMid, RainDrift, RainFall, false, static_cast<float>(Mix.Rain), Camera, DeltaSeconds);
	}
	if (Snow && SnowMid)
	{
		UpdatePrecipitation(Snow, SnowMid, SnowDrift, SnowFall, true, static_cast<float>(Mix.Snow), Camera, DeltaSeconds);
	}
	UpdateLeaves(Camera, CameraVelocity.GetClampedToMaxSize(5000.f), DeltaSeconds);

	const bool bInside = IsInsideCar();
	if (RainSound)
	{
		// В машине дождь барабанит по крыше: глуше, но громче.
		RainSound->SetMuffled(bInside);
		RainSound->SetLevel(static_cast<float>(Mix.Rain) * (bInside ? 1.1f : 0.8f) + static_cast<float>(Mix.Snow) * 0.05f, static_cast<float>(Mix.Rain));
	}
	if (WindSound)
	{
		const float Gale = static_cast<float>(Mix.Wind);
		WindSound->SetMuffled(bInside);
		WindSound->SetLevel((0.12f + 0.6f * Gale) * (bInside ? 0.35f : 1.f), 8.f + 30.f * Gale);
	}
}

void APeregonAmbience::UpdatePrecipitation(UProceduralMeshComponent* Mesh, UMaterialInstanceDynamic* Mid, FVector2D& Drift, float& Fall, bool bSnow,
	float Amount, const FVector& Camera, float DeltaSeconds)
{
	using namespace PeregonAmbienceTuning;
	const bool bVisible = Amount > 0.02f;
	Mesh->SetVisibility(bVisible);
	if (!bVisible)
	{
		return;
	}
	const FVector Wind = Sky->GetWind();
	const float Speed = bSnow ? SnowSpeedCm : RainSpeedCm;
	// Снег сносит ветром сильнее, чем дождь, и он «плывёт» из стороны в сторону.
	const float Carry = bSnow ? 0.9f : 0.35f;
	const float Time = GetWorld()->GetTimeSeconds();
	FVector2D Flow = FVector2D(Wind.X, Wind.Y) * 100.f * Carry;
	if (bSnow)
	{
		Flow += FVector2D(FMath::Sin(Time * 0.7f), FMath::Cos(Time * 0.53f)) * 40.f;
	}
	Drift += Flow * DeltaSeconds;
	Fall += Speed * DeltaSeconds;
	const FVector2D D(FMath::Fmod(Drift.X, TileCm), FMath::Fmod(Drift.Y, TileCm));
	const float Zf = FMath::Fmod(Fall, HeightCm);
	const FVector Center(FMath::FloorToFloat((Camera.X - D.X) / TileCm) * TileCm + D.X + TileCm * 0.5f,
		FMath::FloorToFloat((Camera.Y - D.Y) / TileCm) * TileCm + D.Y + TileCm * 0.5f, Camera.Z + HeightCm * 0.5f - Zf);
	// Сетку не наклоняем: поворот сбил бы совпадение ячеек при сдвиге. Ветер виден по сносу капель.
	Mesh->SetWorldLocation(Center);
	const float Day = FMath::SmoothStep(-6.f, 4.f, Sky->GetSunElevation());
	Mid->SetScalarParameterValue(TEXT("Opacity"), (bSnow ? 0.95f : 0.32f) * FMath::Sqrt(Amount));
	Mid->SetScalarParameterValue(TEXT("Emissive"), (bSnow ? 0.25f : 0.08f) * FMath::Lerp(0.05f, 1.f, Day));
}

void APeregonAmbience::UpdateLeaves(const FVector& Camera, const FVector& CameraVelocity, float DeltaSeconds)
{
	if (!LeafMesh || Leaves.Num() == 0)
	{
		return;
	}
	const rgen::SeasonLook& Season = Sky->GetSeason();
	const FVector Wind = Sky->GetWind() * 100.f;
	APeregonRoadWorld* Road = FindRoadWorld();
	const rgen::Landscape* Land = Road ? &Road->GetLandscape() : nullptr;

	// Цвет листьев по сезону: летом зелёные (редкие, ветром), осенью — золото и ржавчина.
	if (LeafMid)
	{
		const FLinearColor Green(0.38f, 0.55f, 0.2f);
		const FLinearColor Gold(0.95f, 0.72f, 0.2f);
		LeafMid->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(Green, Gold, static_cast<float>(Season.Yellow)));
	}
	const float Rate = (static_cast<float>(Season.LeafFall) * 30.f + 1.5f * static_cast<float>(Season.Leaves)) * (0.4f + static_cast<float>(Sky->GetWeather().Wind));
	LeafBudget += Rate * DeltaSeconds;
	for (int32 I = 0; I < Leaves.Num() && LeafBudget >= 1.f; ++I)
	{
		FLeaf& L = Leaves[I];
		if (L.bActive)
		{
			continue;
		}
		LeafBudget -= 1.f;
		// Появляются в кронах вокруг и впереди по ходу движения.
		const FVector Ahead = CameraVelocity * 1.2f;
		const FVector Spawn = Camera + Ahead + FVector(Random.FRandRange(-2500.f, 2500.f), Random.FRandRange(-2500.f, 2500.f), Random.FRandRange(300.f, 1100.f));
		const double Density = Land ? Land->ForestDensity(Spawn.X / 100.0, Spawn.Y / 100.0) : 0.5;
		if (Random.FRand() > Density * 1.3)
		{
			continue;
		}
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(PeregonLeaf), false);
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Spawn, Spawn - FVector(0.f, 0.f, 4000.f), ECC_WorldStatic, Query);
		L.Pos = Spawn;
		L.GroundZ = bHit ? static_cast<float>(Hit.ImpactPoint.Z) + 1.f : static_cast<float>(Camera.Z) - 400.f;
		L.Vel = FVector(0.f, 0.f, -Random.FRandRange(60.f, 110.f));
		L.Rot = FRotator(Random.FRandRange(0.f, 360.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(0.f, 360.f));
		L.Spin = FRotator(Random.FRandRange(-200.f, 200.f), Random.FRandRange(-120.f, 120.f), Random.FRandRange(-260.f, 260.f));
		L.Phase = Random.FRandRange(0.f, 6.28f);
		L.Rest = Random.FRandRange(6.f, 14.f);
		L.bActive = true;
		L.bLanded = false;
	}
	LeafBudget = FMath::Min(LeafBudget, 5.f);

	const float Time = GetWorld()->GetTimeSeconds();
	for (int32 I = 0; I < Leaves.Num(); ++I)
	{
		FLeaf& L = Leaves[I];
		if (L.bActive && !L.bLanded)
		{
			// Лист кружит: снос ветром + качание из стороны в сторону, падает ~1 м/с.
			const FVector Flutter(FMath::Sin(Time * 2.3f + L.Phase), FMath::Cos(Time * 1.7f + L.Phase * 1.3f), 0.f);
			L.Pos += (Wind * 0.6f + Flutter * 70.f + L.Vel) * DeltaSeconds;
			L.Rot += L.Spin * DeltaSeconds;
			if (L.Pos.Z <= L.GroundZ)
			{
				L.Pos.Z = L.GroundZ;
				L.Rot = FRotator(0.f, L.Rot.Yaw, 0.f);
				L.bLanded = true;
			}
		}
		else if (L.bLanded)
		{
			L.Rest -= DeltaSeconds;
			L.bActive = L.Rest > 0.f;
			L.bLanded = L.bActive;
		}
		if (L.bActive && FVector::DistSquared2D(L.Pos, Camera) > FMath::Square(6000.f))
		{
			L.bActive = false;
			L.bLanded = false;
		}
		LeafTransforms[I] = L.bActive ? FTransform(L.Rot, L.Pos, FVector(0.07f, 0.07f, 0.07f)) : FTransform(FRotator::ZeroRotator, Camera, FVector::ZeroVector);
	}
	LeafMesh->BatchUpdateInstancesTransforms(0, LeafTransforms, true, true, true);
}
