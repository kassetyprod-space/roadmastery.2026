#include "World/PeregonGarageYard.h"

#include "Art/PeregonArtSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Game/PeregonInventory.h"
#include "World/PeregonKit.h"
#include "World/PeregonPickup.h"
#include "World/PeregonRoadWorld.h"
#include "rgen/Landscape.h"
#include "rgen/Noise.h"
#include "rgen/Route.h"
#include "vsim/CarParts.h"

namespace PeregonYard
{
// Раскладка двора (м): S — вдоль дороги от начала маршрута, T — вправо от оси.
constexpr double HouseS = 110.0, HouseT = 70.0;       // пятиэтажка, 60 × 12 м
constexpr double GaragesS = 35.0, GaragesT = 58.0;    // ряд гаражей, 31 × 7 м, ворота к дороге
constexpr double CarS = 31.5, CarT = 48.5;            // «Сокол» перед открытым гаражом
constexpr double DumpS = 66.0, DumpT = 20.0;          // помойка у дороги
constexpr double ScrapS0 = 52.0, ScrapS1 = 84.0;      // разборка за забором
constexpr double ScrapT0 = 38.0, ScrapT1 = 78.0;
} // namespace PeregonYard

APeregonGarageYard::APeregonGarageYard()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

FVector APeregonGarageYard::At(double S, double T, double Lift) const
{
	if (!RoadWorld)
	{
		return FVector::ZeroVector;
	}
	const rgen::Route& R = RoadWorld->GetRoute();
	double X = 0.0;
	double Y = 0.0;
	R.ToWorld(S, T, X, Y);
	return FVector(X, Y, R.GroundHeight(S, T) + Lift) * 100.0;
}

FRotator APeregonGarageYard::Facing(double S, double YawFromRoadDeg) const
{
	const double Heading = RoadWorld ? FMath::RadiansToDegrees(RoadWorld->GetRoute().Sample(S).Heading) : 0.0;
	return FRotator(0.f, static_cast<float>(Heading + YawFromRoadDeg), 0.f);
}

FTransform APeregonGarageYard::GetSpawnSpot(int32 Player) const
{
	// Каждый игрок — из своего подъезда (до пяти в кооперативе).
	FTransform Out = Spawn;
	Out.AddToTranslation(Facing(PeregonYard::HouseS, 0.0).Vector() * (Player * 220.0));
	return Out;
}

void APeregonGarageYard::Loot(double S, double T, FName Id, float Condition, float Price, float Charge)
{
	FPeregonItemStack Stack;
	Stack.Id = Id;
	Stack.Count = 1;
	Stack.Condition = Condition;
	Stack.Charge = Charge;
	const uint32 H = GetTypeHash(Id) ^ static_cast<uint32>(S * 13.0 + T * 7.0);
	APeregonPickup::Place(GetWorld(), Stack, FTransform(Facing(S, static_cast<double>(H % 360u)), At(S, T, 0.02)), Price);
}

void APeregonGarageYard::Build(const APeregonRoadWorld* World, int32 Seed)
{
	using namespace PeregonYard;
	RoadWorld = World;
	if (!RoadWorld)
	{
		return;
	}
	const rgen::Route& R = RoadWorld->GetRoute();
	FPeregonKit Kit(this, Root, Spawned, RoadWorld->KitMaterials());
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	auto Look = [this, Art](const FLinearColor& C, EPeregonTex Tex = EPeregonTex::Paint, float Rough = 0.8f) -> UMaterialInterface* {
		return Art ? Art->MakeMaterial(this, FPeregonLook(Tex, C, Rough)) : nullptr;
	};
	auto Building = [&](rgen::BuildingKind Kind, double S, double T, double Length, double Width, int32 Floors, uint32 BSeed) {
		rgen::Building B;
		B.Kind = Kind;
		double X = 0.0;
		double Y = 0.0;
		R.ToWorld(S, T, X, Y);
		B.X = X;
		B.Y = Y;
		B.Z = R.GroundHeight(S, T) - 0.05;
		B.Yaw = R.Sample(S).Heading;
		B.Length = Length;
		B.Width = Width;
		B.Floors = Floors;
		B.Seed = BSeed;
		return B;
	};

	// Пятиэтажка и ряд гаражей.
	Kit.Buildings({Building(rgen::BuildingKind::Panel5, HouseS, HouseT, 60.0, 12.0, 5, static_cast<uint32>(Seed) * 3u + 1u),
		Building(rgen::BuildingKind::Garages, GaragesS, GaragesT, 31.5, 7.0, 1, static_cast<uint32>(Seed) * 5u + 2u)});
	Kit.Label(FTransform(Facing(GaragesS, 180.0), At(GaragesS + 17.0, GaragesT - 3.0, 2.6)), TEXT("ГСК «МОТОР»"), 220.f, 50.f, 48,
		FLinearColor(0.9f, 0.9f, 0.85f), FLinearColor(0.15f, 0.25f, 0.45f));
	// Открытый бокс: тёмный проём вместо ворот у «Сокола».
	Kit.Box(At(CarS, GaragesT - 3.52, 1.1), Facing(CarS, 0.0), FVector(2.6, 0.04, 2.2), Look(FLinearColor(0.02f, 0.02f, 0.02f)), false);

	// Асфальт двора и дорожки (тёмные плиты чуть над землёй).
	for (double S = 18.0; S < 130.0; S += 14.0)
	{
		Kit.Box(At(S, 52.0 - 8.0, 0.03), Facing(S, 0.0), FVector(14.2, 11.0, 0.06), RoadWorld->KitMaterials().Asphalt, false);
	}
	for (double T = 12.0; T < 46.0; T += 10.0)
	{
		Kit.Box(At(40.0, T, 0.03), Facing(40.0, 0.0), FVector(6.0, 10.2, 0.06), RoadWorld->KitMaterials().Asphalt, false);
	}
	Kit.Box(At(HouseS, HouseT - 8.0, 0.04), Facing(HouseS, 0.0), FVector(60.0, 3.0, 0.08), RoadWorld->KitMaterials().Concrete, false);
	Kit.Bench(At(HouseS - 12.0, HouseT - 10.0), Facing(HouseS, 90.0));
	Kit.Bench(At(HouseS + 12.0, HouseT - 10.0), Facing(HouseS, 90.0));
	Kit.StreetLamp(At(HouseS - 25.0, HouseT - 14.0), At(HouseS - 25.0, HouseT - 30.0));
	Kit.StreetLamp(At(GaragesS + 18.0, GaragesT - 12.0), At(GaragesS + 18.0, GaragesT - 30.0));

	// Помойка: бетонная площадка, три контейнера, куча хлама, покрышки.
	const FRotator DumpRot = Facing(DumpS, 0.0);
	Kit.Box(At(DumpS, DumpT, 0.05), DumpRot, FVector(7.0, 3.4, 0.1), RoadWorld->KitMaterials().Concrete, true);
	static const FLinearColor Bins[] = {FLinearColor(0.12f, 0.3f, 0.15f), FLinearColor(0.15f, 0.22f, 0.4f), FLinearColor(0.3f, 0.3f, 0.3f)};
	for (int32 I = 0; I < 3; ++I)
	{
		Kit.Box(At(DumpS - 2.2 + I * 2.2, DumpT + 0.6, 0.7), DumpRot, FVector(1.9, 1.3, 1.2), Look(Bins[I], EPeregonTex::Paint, 0.7f), true);
	}
	rgen::Rng Rand(static_cast<uint64>(Seed) * 7919ull + 17ull);
	static const FLinearColor Junk[] = {FLinearColor(0.35f, 0.25f, 0.15f), FLinearColor(0.4f, 0.4f, 0.38f), FLinearColor(0.2f, 0.18f, 0.15f),
		FLinearColor(0.5f, 0.45f, 0.35f)};
	for (int32 I = 0; I < 14; ++I)
	{
		const double S = DumpS + 4.5 + Rand.Range(0.0, 3.5);
		const double T = DumpT + Rand.Range(-1.8, 1.8);
		const FVector Size(Rand.Range(0.3, 1.2), Rand.Range(0.3, 1.0), Rand.Range(0.15, 0.6));
		Kit.Box(At(S, T, Size.Z * 0.5), Facing(S, Rand.Range(0.0, 90.0)), Size, Look(Junk[I % 4]), false);
	}
	for (int32 I = 0; I < 4; ++I)
	{
		Kit.AddMesh(FPeregonKit::Cylinder(), FTransform(FRotator(90.f, 0.f, 0.f) + Facing(DumpS, 0.0), At(DumpS - 5.0, DumpT - 1.0 + I * 0.1, 0.12 + I * 0.24),
			FVector(0.62, 0.62, 0.2)), Look(FLinearColor(0.05f, 0.05f, 0.05f), EPeregonTex::Rubber, 0.95f), false);
	}

	// Разборка: забор, будка продавца, кузова-доноры, стопки колёс.
	Kit.Fence(At(ScrapS0, ScrapT0), At(ScrapS1, ScrapT0));
	Kit.Fence(At(ScrapS1, ScrapT0), At(ScrapS1, ScrapT1));
	Kit.Fence(At(ScrapS0, ScrapT1), At(ScrapS1, ScrapT1));
	Kit.Fence(At(ScrapS0, ScrapT0 + 8.0), At(ScrapS0, ScrapT1));
	Kit.Shed(At(ScrapS0 + 4.0, ScrapT0 + 4.5), Facing(ScrapS0, 0.0));
	Kit.Label(FTransform(Facing(ScrapS0, 180.0), At(ScrapS0 + 4.0, ScrapT0 + 2.8, 2.5)), TEXT("РАЗБОРКА · Б/У ЗАПЧАСТИ"), 300.f, 45.f, 40,
		FLinearColor(0.95f, 0.85f, 0.3f), FLinearColor(0.12f, 0.1f, 0.08f));
	static const FLinearColor Shells[] = {FLinearColor(0.3f, 0.14f, 0.07f), FLinearColor(0.25f, 0.28f, 0.3f), FLinearColor(0.35f, 0.3f, 0.2f),
		FLinearColor(0.15f, 0.2f, 0.12f)};
	for (int32 I = 0; I < 5; ++I)
	{
		const double S = ScrapS0 + 10.0 + (I % 3) * 7.0;
		const double T = ScrapT0 + 14.0 + (I / 3) * 12.0 + Rand.Range(-1.0, 1.0);
		const FRotator Rot = Facing(S, 90.0 + Rand.Range(-15.0, 15.0));
		// Кузов без колёс: короб и «оранжерея», стоит на чурках.
		Kit.Box(At(S, T, 0.75), Rot, FVector(4.1, 1.6, 0.7), Look(Shells[I % 4], EPeregonTex::Paint, 0.9f), true);
		Kit.Box(At(S, T, 1.35), Rot, FVector(2.0, 1.4, 0.55), Look(Shells[I % 4] * 0.8f, EPeregonTex::Paint, 0.9f), true);
		for (int32 W = 0; W < 4; ++W)
		{
			const FVector Off = Rot.RotateVector(FVector((W / 2) ? 1.3 : -1.3, (W % 2) ? 0.6 : -0.6, 0.0) * 100.0);
			Kit.Box(At(S, T, 0.2) + Off, Rot, FVector(0.25, 0.25, 0.4), RoadWorld->KitMaterials().DarkWood, false);
		}
	}
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 K = 0; K < 4; ++K)
		{
			Kit.AddMesh(FPeregonKit::Cylinder(), FTransform(Facing(ScrapS1, 0.0), At(ScrapS1 - 3.0, ScrapT1 - 4.0 - I * 1.4, 0.11 + K * 0.22),
				FVector(0.6, 0.6, 0.2)), Look(FLinearColor(0.05f, 0.05f, 0.05f), EPeregonTex::Rubber, 0.95f), false);
		}
	}

	// Где кто стоит.
	CarSpot = FTransform(Facing(CarS, -90.0), At(CarS, CarT, 0.4));
	DonorSpot = FTransform(Facing(ScrapS0 + 24.0, 90.0), At(ScrapS0 + 24.0, ScrapT0 + 22.0, 0.4));
	Spawn = FTransform(Facing(HouseS, 180.0), At(HouseS - 14.0, HouseT - 9.0, 1.0));

	// --- Гараж: инструмент на верстаке у ворот, канистры ---
	using vsim::parts::Consumable;
	using vsim::parts::Slot;
	using vsim::parts::Tool;
	const double Bench = CarS + 3.2;
	Kit.Box(At(Bench, GaragesT - 5.0, 0.45), Facing(Bench, 0.0), FVector(1.6, 0.6, 0.9), RoadWorld->KitMaterials().Wood, true);
	auto OnBench = [&](double Ds, FName Id, float Condition) {
		FPeregonItemStack Stack;
		Stack.Id = Id;
		Stack.Count = 1;
		Stack.Condition = Condition;
		APeregonPickup::Place(GetWorld(), Stack, FTransform(Facing(Bench, 0.0), At(Bench + Ds, GaragesT - 5.0, 0.91)));
	};
	OnBench(-0.6, PeregonItems::ToolItem(static_cast<int32>(Tool::Wrenches)), 1.f);
	OnBench(-0.25, PeregonItems::ToolItem(static_cast<int32>(Tool::PlugSocket)), 1.f);
	OnBench(0.05, PeregonItems::ToolItem(static_cast<int32>(Tool::Screwdriver)), 1.f);
	OnBench(0.35, PeregonItems::ToolItem(static_cast<int32>(Tool::Pliers)), 1.f);
	OnBench(0.62, PeregonItems::Oil, 0.45f);
	Loot(Bench + 1.6, GaragesT - 5.2, PeregonItems::ToolItem(static_cast<int32>(Tool::Jack)), 1.f);
	Loot(Bench + 2.4, GaragesT - 5.0, PeregonItems::ToolItem(static_cast<int32>(Tool::Grinder)), 1.f);
	Loot(Bench + 2.4, GaragesT - 5.8, PeregonItems::Coolant, 0.7f);

	// --- Помойка: бесплатно, но всё старое ---
	const float J = static_cast<float>(Rand.Range(0.0, 0.15));
	Loot(DumpS + 5.0, DumpT - 2.3, PeregonItems::PartItem(static_cast<int32>(Slot::Battery)), 0.38f + J, 0.f, 0.45f);
	Loot(DumpS + 6.4, DumpT - 2.1, PeregonItems::PartItem(static_cast<int32>(Slot::SparkPlugs)), 0.42f + J);
	Loot(DumpS - 3.4, DumpT - 1.6, PeregonItems::PartItem(static_cast<int32>(Slot::AirFilter)), 0.3f);
	Loot(DumpS + 7.8, DumpT + 2.4, PeregonItems::PartItem(static_cast<int32>(Slot::Hoses)), 0.5f);
	Loot(DumpS - 4.5, DumpT + 2.2, PeregonItems::ConsumableItem(static_cast<int32>(Consumable::Putty)), 0.55f);
	Loot(DumpS + 3.3, DumpT + 2.5, PeregonItems::ConsumableItem(static_cast<int32>(Consumable::FlapDisc)), 0.7f);
	Loot(DumpS + 2.0, DumpT - 2.4, PeregonItems::ConsumableItem(static_cast<int32>(Consumable::CutDisc)), 0.6f);
	Loot(DumpS - 1.0, DumpT - 2.5, PeregonItems::ConsumableItem(static_cast<int32>(Consumable::Primer)), 0.5f);
	const TArray<FName>& Paints = PeregonItems::Paints();
	Loot(DumpS + 8.5, DumpT - 0.6, Paints[static_cast<int32>(Rand.Next() % static_cast<uint32>(Paints.Num()))], 0.6f);

	// --- Разборка: б/у узлы за деньги (дешевле, чем в магазине) ---
	struct FScrap
	{
		Slot Kind;
		float Condition;
	};
	const FScrap Scrap[] = {{Slot::Carburetor, 0.58f}, {Slot::Radiator, 0.66f}, {Slot::Battery, 0.72f}, {Slot::SparkPlugs, 0.75f},
		{Slot::Distributor, 0.62f}, {Slot::Alternator, 0.6f}, {Slot::Radio, 0.5f}, {Slot::Headlights, 0.65f}, {Slot::WaterPump, 0.6f},
		{Slot::FuelFilter, 0.7f}, {Slot::OilFilter, 0.65f}, {Slot::BrakePads, 0.55f}};
	for (int32 I = 0; I < static_cast<int32>(UE_ARRAY_COUNT(Scrap)); ++I)
	{
		FPeregonItemStack Stack;
		Stack.Id = PeregonItems::PartItem(static_cast<int32>(Scrap[I].Kind));
		Stack.Condition = Scrap[I].Condition;
		const float Price = FMath::RoundToFloat(PeregonItems::PriceOf(Stack) * 0.5f / 10.f) * 10.f;
		// Разложено на стеллаже-поддоне вдоль будки.
		Loot(ScrapS0 + 8.0 + (I % 6) * 0.9, ScrapT0 + 2.0 + (I / 6) * 0.9, Stack.Id, Stack.Condition, Price,
			Scrap[I].Kind == Slot::Battery ? 0.8f : -1.f);
	}
	Kit.Box(At(ScrapS0 + 10.3, ScrapT0 + 2.45, 0.05), Facing(ScrapS0, 0.0), FVector(6.0, 2.0, 0.1), RoadWorld->KitMaterials().Wood, false);
}
