#include "World/PeregonTraffic.h"

#include "Art/PeregonArtSubsystem.h"
#include "Audio/PeregonAudio.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Game/PeregonGameMode.h"
#include "Game/PeregonGameUserSettings.h"
#include "ProceduralMeshComponent.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "World/PeregonKit.h"
#include "World/PeregonRoadWorld.h"
#include "rgen/Landscape.h"
#include "rgen/Noise.h"
#include "rgen/Route.h"

namespace PeregonTrafficTuning
{
constexpr double LaneT = 1.75;          // середина полосы, м
constexpr double SpawnAhead = 1000.0;   // где рождаются (за пределами видимости в дымке)
constexpr double Despawn = 1400.0;
constexpr double MinGap = 9.0;          // м между машинами стоя
constexpr double Headway = 1.6;         // с — дистанция по времени
const FLinearColor Colors[] = {FLinearColor(0.75f, 0.74f, 0.7f), FLinearColor(0.12f, 0.2f, 0.42f), FLinearColor(0.32f, 0.05f, 0.05f),
	FLinearColor(0.15f, 0.3f, 0.15f), FLinearColor(0.55f, 0.45f, 0.28f), FLinearColor(0.05f, 0.05f, 0.06f), FLinearColor(0.6f, 0.6f, 0.62f),
	FLinearColor(0.7f, 0.5f, 0.05f)};
} // namespace PeregonTrafficTuning

// ---------------------------------------------------------------------------------------------
// Машина
// ---------------------------------------------------------------------------------------------
APeregonTrafficCar::APeregonTrafficCar()
{
	PrimaryActorTick.bCanEverTick = false;
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->SetMobility(EComponentMobility::Movable);
	Collision->SetSimulatePhysics(false);
}

void APeregonTrafficCar::Build(int32 Kind, const FLinearColor& Color, uint32 Seed)
{
	FPeregonMeshBuffers Body;
	FPeregonMeshBuffers Lamps;
	const FColor Paint = Color.ToFColor(false);
	const FColor Dark = FLinearColor(0.03f, 0.03f, 0.035f).ToFColor(false);
	const FColor Glass = FLinearColor(0.08f, 0.1f, 0.12f).ToFColor(false);
	const FColor Chrome = FLinearColor(0.6f, 0.6f, 0.6f).ToFColor(false);
	const FVector2D Tile(100.f, 100.f);
	// Размеры (см): длина, ширина, высота кузова, высота «оранжереи», колёса.
	float L = 420.f, W = 165.f, H = 75.f, Cab = 62.f, CabLen = 220.f, CabX = -20.f, Wheel = 30.f;
	switch (Kind)
	{
	case 1: L = 372.f; W = 168.f; H = 85.f; Cab = 70.f; CabLen = 200.f; CabX = -30.f; Wheel = 36.f; break; // «Нива»
	case 2: L = 450.f; W = 200.f; H = 175.f; Cab = 0.f; Wheel = 35.f; break;                             // «буханка»
	case 3: L = 820.f; W = 250.f; H = 120.f; Cab = 0.f; Wheel = 52.f; break;                             // грузовик
	case 4: L = 700.f; W = 240.f; H = 230.f; Cab = 0.f; Wheel = 45.f; break;                             // автобус
	default: break;
	}
	LengthCm = L;
	const float Floor = Wheel * 0.9f;
	if (Kind == 3)
	{
		// Кабина спереди, кузов-фургон сзади.
		Body.AddBox(FVector(L * 0.5f - 110.f, 0.f, Floor), 0.f, FVector(200.f, W, 210.f), Tile, Paint, true, true);
		Body.AddBox(FVector(L * 0.5f - 12.f, 0.f, Floor + 120.f), 0.f, FVector(6.f, W - 30.f, 70.f), Tile, Glass, true, true);
		Body.AddBox(FVector(-110.f, 0.f, Floor + 10.f), 0.f, FVector(L - 240.f, W + 4.f, 250.f), Tile,
			FLinearColor(0.45f, 0.48f, 0.42f).ToFColor(false), true, true);
	}
	else
	{
		Body.AddBox(FVector(0.f, 0.f, Floor), 0.f, FVector(L, W, H), Tile, Paint, true, true);
		if (Cab > 0.f)
		{
			Body.AddBox(FVector(CabX, 0.f, Floor + H), 0.f, FVector(CabLen, W - 16.f, Cab), Tile, Paint, true, true);
			// Стёкла поясом по «оранжерее».
			Body.AddBox(FVector(CabX, 0.f, Floor + H + 8.f), 0.f, FVector(CabLen + 2.f, W - 14.f, Cab - 18.f), Tile, Glass, false, false);
		}
		else
		{
			// Окна поясом по салону автобуса/«буханки».
			Body.AddBox(FVector(0.f, 0.f, Floor + H * 0.55f), 0.f, FVector(L - 40.f, W + 2.f, H * 0.3f), Tile, Glass, false, false);
			Body.AddBox(FVector(L * 0.5f - 2.f, 0.f, Floor + H * 0.5f), 0.f, FVector(6.f, W - 30.f, H * 0.35f), Tile, Glass, true, true);
		}
	}
	// Бамперы и колёса.
	Body.AddBox(FVector(L * 0.5f + 3.f, 0.f, Floor + 5.f), 0.f, FVector(8.f, W, 14.f), Tile, Chrome, true, true);
	Body.AddBox(FVector(-L * 0.5f - 3.f, 0.f, Floor + 5.f), 0.f, FVector(8.f, W, 14.f), Tile, Chrome, true, true);
	const float Ax = L * 0.5f - Wheel * 2.2f;
	for (float X : {Ax, -Ax})
	{
		for (float Y : {-W * 0.5f + 12.f, W * 0.5f - 12.f})
		{
			Body.AddBox(FVector(X, Y, 0.f), 0.f, FVector(Wheel * 2.f, 24.f, Wheel * 2.f), Tile, Dark, true, true);
			Body.AddBox(FVector(X, Y + (Y < 0.f ? -12.5f : 12.5f), Wheel * 0.6f), 0.f, FVector(Wheel * 0.9f, 1.f, Wheel * 0.9f), Tile, Chrome, true, true);
		}
	}
	// Фары спереди, фонари сзади — светящийся материал (ночью горят).
	const float LampZ = Floor + 30.f;
	for (float Y : {-W * 0.5f + 25.f, W * 0.5f - 25.f})
	{
		Lamps.AddBox(FVector(L * 0.5f + 1.f, Y, LampZ - 8.f), 0.f, FVector(4.f, 24.f, 16.f), Tile, FColor(255, 245, 220), true, true);
		Lamps.AddBox(FVector(-L * 0.5f - 1.f, Y, LampZ - 6.f), 0.f, FVector(4.f, 22.f, 12.f), Tile, FColor(200, 20, 15), true, true);
	}

	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	auto MakeMesh = [this](const TCHAR* Name, const FPeregonMeshBuffers& Data, UMaterialInterface* Mat) {
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, Name);
		Mesh->SetupAttachment(Collision);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->RegisterComponent();
		Mesh->CreateMeshSection(0, Data.Vertices, Data.Triangles, Data.Normals, Data.UVs, Data.Colors, TArray<FProcMeshTangent>(), false);
		if (Mat)
		{
			Mesh->SetMaterial(0, Mat);
		}
		// Меш построен от земли, а коробка коллизии — по центру.
		return Mesh;
	};
	const float HalfH = (Kind == 3 ? 260.f : (Cab > 0.f ? H + Cab : H)) * 0.5f + Floor * 0.5f;
	Collision->SetBoxExtent(FVector(L * 0.5f, W * 0.5f, HalfH));
	HalfHeightCm = HalfH;
	BodyMesh = MakeMesh(TEXT("Body"), Body, Art ? Art->MakeMaterial(this, FPeregonLook(EPeregonTex::Paint, FLinearColor::White, 0.4f, 0.2f)) : nullptr);
	FPeregonLook LampLook(EPeregonTex::White, FLinearColor::White, 0.3f);
	LampLook.Emissive = 0.2f;
	LampLook.NightGlow = 8.f;
	LampMesh = MakeMesh(TEXT("Lamps"), Lamps, Art ? Art->MakeMaterial(this, LampLook) : nullptr);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -HalfH));
	LampMesh->SetRelativeLocation(FVector(0.f, 0.f, -HalfH));
	(void)Seed;
}

// ---------------------------------------------------------------------------------------------
// Поток
// ---------------------------------------------------------------------------------------------
APeregonTraffic::APeregonTraffic()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void APeregonTraffic::EndPlay(const EEndPlayReason::Type Reason)
{
	for (FMover& M : Movers)
	{
		if (M.Actor.IsValid())
		{
			M.Actor->Destroy();
		}
	}
	Movers.Reset();
	Super::EndPlay(Reason);
}

void APeregonTraffic::Spawn(double PlayerS, bool bInitial)
{
	using namespace PeregonTrafficTuning;
	const APeregonRoadWorld* World = RoadWorld.Get();
	if (!World)
	{
		return;
	}
	const rgen::Route& Route = World->GetRoute();
	rgen::Rng Rand(static_cast<uint64>(Counter++) * 2654435761ull + static_cast<uint64>(PlayerS));
	FMover M;
	M.Dir = Rand.Chance(0.55) ? -1 : 1;
	const int32 Roll = static_cast<int32>(Rand.Next() % 100u);
	const int32 Kind = Roll < 55 ? 0 : (Roll < 68 ? 1 : (Roll < 76 ? 2 : (Roll < 92 ? 3 : 4)));
	const double KindSpeed[] = {24.0, 21.0, 18.0, 19.0, 18.0}; // м/с
	M.Cruise = KindSpeed[Kind] * Rand.Range(0.85, 1.15);
	M.Speed = M.Cruise;
	// Встречные рождаются впереди, попутные — впереди (медленные) или сзади (быстрые); в начале — где угодно.
	double Offset = SpawnAhead * Rand.Range(0.8, 1.1);
	if (bInitial)
	{
		Offset = Rand.Range(150.0, SpawnAhead);
		if (Rand.Chance(0.5))
		{
			Offset = -Offset;
		}
	}
	else if (M.Dir > 0 && Rand.Chance(0.5))
	{
		Offset = -Offset;
	}
	M.S = PlayerS + Offset;
	if (M.S < 30.0 || M.S > Route.Length() - 30.0)
	{
		return;
	}
	M.Lane = M.LaneTarget = M.Dir > 0 ? LaneT : -LaneT;
	for (const FMover& Other : Movers)
	{
		if (Other.Dir == M.Dir && FMath::Abs(Other.S - M.S) < 60.0)
		{
			return; // слишком близко к другой машине
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APeregonTrafficCar* Car = GetWorld()->SpawnActor<APeregonTrafficCar>(APeregonTrafficCar::StaticClass(), FTransform::Identity, Params);
	if (!Car)
	{
		return;
	}
	const FLinearColor Color = Kind == 4 ? FLinearColor(0.75f, 0.55f, 0.1f) : Colors[Rand.Next() % UE_ARRAY_COUNT(Colors)];
	Car->Build(Kind, Color, Rand.Next());
	M.Actor = Car;
	M.Length = Car->GetLengthCm() / 100.0;
	Movers.Add(M);
}

void APeregonTraffic::Tick(float DeltaSeconds)
{
	using namespace PeregonTrafficTuning;
	Super::Tick(DeltaSeconds);
	const APeregonRoadWorld* World = RoadWorld.Get();
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	if (!World || !GM || DeltaSeconds <= 0.f)
	{
		return;
	}
	const rgen::Route& Route = World->GetRoute();
	const rgen::Landscape& Land = World->GetLandscape();
	static const int32 Densities[] = {0, 6, 12, 20};
	const int32 Want = Densities[FMath::Clamp(UPeregonGameUserSettings::Get()->TrafficDensity, 0, 3)];

	// Где игрок (машина или пешеход): S, T и скорость вдоль дороги.
	const APawn* Pawn = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	const APeregonCar* PlayerCar = GM->GetCar();
	const FVector PlayerLoc = Pawn ? Pawn->GetActorLocation() : (PlayerCar ? PlayerCar->GetActorLocation() : FVector::ZeroVector);
	const double PlayerS = World->FindS(PlayerLoc, World->GetTrackedS());
	auto TOf = [&Route](double S, const FVector& Loc) {
		const rgen::RouteSample C = Route.Sample(S);
		return (Loc.X / 100.0 - C.X) * -FMath::Sin(C.Heading) + (Loc.Y / 100.0 - C.Y) * FMath::Cos(C.Heading);
	};
	// Машины игрока на дороге — препятствия (вся колонна).
	struct FObstacle
	{
		double S, T, V;
	};
	TArray<FObstacle> Obstacles;
	for (const APeregonCar* Car : GM->GetCars())
	{
		if (!IsValid(Car))
		{
			continue;
		}
		const double S = World->FindS(Car->GetActorLocation(), PlayerS);
		const FVector Vel = Car->GetVelocity();
		const rgen::RouteSample C = Route.Sample(S);
		const double Along = (Vel.X * FMath::Cos(C.Heading) + Vel.Y * FMath::Sin(C.Heading)) / 100.0;
		Obstacles.Add({S, TOf(S, Car->GetActorLocation()), Along});
	}
	if (Pawn && !Pawn->IsA<APeregonCar>())
	{
		Obstacles.Add({PlayerS, TOf(PlayerS, PlayerLoc), 0.0}); // пешеход на дороге — тоже тормозим
	}

	// Рождение и уход.
	SpawnTimer -= DeltaSeconds;
	if (Movers.Num() < Want && SpawnTimer <= 0.f)
	{
		Spawn(PlayerS, Movers.Num() == 0);
		SpawnTimer = Movers.Num() < Want / 2 ? 0.2f : 2.5f;
	}
	for (int32 I = Movers.Num() - 1; I >= 0; --I)
	{
		FMover& M = Movers[I];
		if (!M.Actor.IsValid() || FMath::Abs(M.S - PlayerS) > Despawn || M.S < 5.0 || M.S > Route.Length() - 5.0 || Movers.Num() > Want + 2)
		{
			if (M.Actor.IsValid())
			{
				M.Actor->Destroy();
			}
			Movers.RemoveAtSwap(I);
		}
	}

	for (int32 I = 0; I < Movers.Num(); ++I)
	{
		FMover& M = Movers[I];
		const rgen::RouteSample Here = Route.Sample(M.S);
		// Желаемая скорость: круиз, меньше на поворотах (боковое ≈ 2.8 м/с²) и в населённых пунктах.
		double X = 0.0, Y = 0.0;
		Route.ToWorld(M.S, M.Lane, X, Y);
		double Want2 = M.Cruise;
		const double Curv = FMath::Max(FMath::Abs(Route.Sample(M.S + M.Dir * 40.0).Curvature), FMath::Abs(Here.Curvature));
		if (Curv > 1e-4)
		{
			Want2 = FMath::Min(Want2, FMath::Sqrt(2.8 / Curv));
		}
		if (Land.UrbanMask(X, Y) > 0.2)
		{
			Want2 = FMath::Min(Want2, 15.0);
		}
		// Ближайший впереди в своей полосе: другая машина потока или машина игрока.
		double Gap = 1e9;
		double LeadV = Want2;
		auto Consider = [&](double S, double T, double V, double Len) {
			const double Ahead = (S - M.S) * M.Dir - 0.5 * (Len + M.Length);
			if (Ahead > -2.0 && Ahead < Gap && FMath::Abs(T - M.Lane) < 2.2)
			{
				Gap = FMath::Max(0.0, Ahead);
				LeadV = V * M.Dir;
			}
		};
		for (int32 K = 0; K < Movers.Num(); ++K)
		{
			if (K != I)
			{
				Consider(Movers[K].S, Movers[K].Lane, Movers[K].Speed * Movers[K].Dir, Movers[K].Length);
			}
		}
		for (const FObstacle& O : Obstacles)
		{
			Consider(O.S, O.T, O.V, 4.2);
		}
		const double Safe = MinGap + Headway * M.Speed;
		double Target = Want2;
		if (Gap < Safe * 2.0)
		{
			Target = FMath::Min(Target, FMath::Max(0.0, LeadV) + (Gap - Safe) * 0.5);
		}
		Target = FMath::Max(0.0, Target);
		const double Accel = Target > M.Speed ? 1.8 : 6.5;
		M.Speed = FMath::Clamp(M.Speed + FMath::Clamp(Target - M.Speed, -Accel * DeltaSeconds, Accel * DeltaSeconds), 0.0, 40.0);

		// Обгон: попутный упёрся в медленного (или стоящего игрока) — выезжает на встречку, если она свободна.
		const double Home = M.Dir > 0 ? LaneT : -LaneT;
		M.Blocked = (Gap < Safe * 1.2 && LeadV * M.Dir < M.Cruise - 4.0) ? M.Blocked + DeltaSeconds : 0.f;
		if (M.LaneTarget == Home && M.Blocked > 3.f && Curv < 1.0 / 900.0)
		{
			bool bClear = true;
			for (const FMover& Other : Movers)
			{
				const double Rel = (Other.S - M.S) * M.Dir;
				if (&Other != &M && FMath::Abs(Other.Lane + Home) < 2.0 && Rel > -10.0 && Rel < 260.0)
				{
					bClear = false;
				}
			}
			for (const FObstacle& O : Obstacles)
			{
				const double Rel = (O.S - M.S) * M.Dir;
				if (FMath::Abs(O.T + Home) < 2.2 && Rel > -10.0 && Rel < 200.0)
				{
					bClear = false;
				}
			}
			if (bClear)
			{
				M.LaneTarget = -Home;
				M.Blocked = 0.f;
			}
		}
		else if (M.LaneTarget != Home)
		{
			// Вернуться, когда своя полоса впереди свободна на 25 м.
			bool bFree = true;
			for (const FObstacle& O : Obstacles)
			{
				const double Rel = (O.S - M.S) * M.Dir;
				if (FMath::Abs(O.T - Home) < 2.2 && Rel > -12.0 && Rel < 25.0)
				{
					bFree = false;
				}
			}
			for (const FMover& Other : Movers)
			{
				const double Rel = (Other.S - M.S) * M.Dir;
				if (&Other != &M && FMath::Abs(Other.Lane - Home) < 2.0 && Rel > -12.0 && Rel < 25.0)
				{
					bFree = false;
				}
			}
			if (bFree)
			{
				M.LaneTarget = Home;
			}
		}
		M.Lane = FMath::FInterpConstantTo(static_cast<float>(M.Lane), static_cast<float>(M.LaneTarget), DeltaSeconds, 1.1f);

		M.S += M.Speed * M.Dir * DeltaSeconds;
		const rgen::RouteSample At = Route.Sample(M.S);
		Route.ToWorld(M.S, M.Lane, X, Y);
		APeregonTrafficCar* Actor = M.Actor.Get();
		if (!Actor)
		{
			continue;
		}
		const float HalfH = Actor->GetHalfHeightCm();
		const double Yaw = FMath::RadiansToDegrees(At.Heading) + (M.Dir < 0 ? 180.0 : 0.0);
		const double Pitch = FMath::RadiansToDegrees(FMath::Atan(At.Grade)) * M.Dir;
		Actor->SetActorLocationAndRotation(FVector(X * 100.0, Y * 100.0, At.Z * 100.0 + HalfH + 2.0), FRotator(Pitch, Yaw, 0.0));

		// Проезд мимо игрока — звук.
		const double Rel = (M.S - PlayerS) * M.Dir;
		if (!M.bPassedPlayer && Rel > 0.0 && Rel < 12.0)
		{
			M.bPassedPlayer = true;
			PeregonAudio::PlayEvent(this, TEXT("traffic.pass"), Actor->GetActorLocation(), Actor->GetLengthCm() > 600.f ? 1.2f : 0.7f,
				Actor->GetLengthCm() > 600.f ? 0.7f : 1.f);
		}
		else if (Rel < -20.0 || Rel > 40.0)
		{
			M.bPassedPlayer = Rel > 0.0;
		}
	}
}
