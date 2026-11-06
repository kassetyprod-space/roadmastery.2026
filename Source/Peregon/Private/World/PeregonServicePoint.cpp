#include "World/PeregonServicePoint.h"
#include "World/PeregonKit.h"
#include "Vehicle/PeregonPartMeshes.h"
#include "ProceduralMeshComponent.h"
#include "vsim/CarParts.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/PeregonInteractable.h"
#include "rgen/Noise.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/PeregonGameInstance.h"
#include "Game/PeregonGameMode.h"
#include "Game/PeregonPlayerController.h"
#include "Player/PeregonCharacter.h"
#include "Audio/PeregonAudio.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"

namespace PeregonService
{
const FName PumpTag(TEXT("Pump"));
const FName RestTag(TEXT("Rest"));
const FName ShopTag(TEXT("Shop"));
const FName ItemTag(TEXT("ShelfItem"));
const FVector PumpLocal[] = {FVector(11.0, -3.5, 1.0), FVector(11.0, 3.5, 1.0)};
constexpr float PumpReachCm = 900.f;

FString Rubles(float Value)
{
	return FString::Printf(TEXT("%d ₽"), FMath::RoundToInt(Value));
}
} // namespace PeregonService

APeregonServicePoint::APeregonServicePoint()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void APeregonServicePoint::BeginPlay()
{
	Super::BeginPlay();
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	switch (Kind)
	{
	case EPeregonService::Fuel: BuildFuel(); break;
	case EPeregonService::Shop:
	case EPeregonService::Parts: BuildShop(); break;
	default: BuildCafe(); break;
	}
}

UStaticMeshComponent* APeregonServicePoint::AddBox(const FVector& Center, const FVector& SizeM, const FPeregonLook& Look, bool bCollision, FName InteractTag)
{
	UStaticMeshComponent* Box = NewObject<UStaticMeshComponent>(this);
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetupAttachment(RootComponent);
	Box->SetStaticMesh(CubeMesh);
	Box->SetRelativeLocation(Center * 100.0);
	Box->SetRelativeScale3D(SizeM);
	Box->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	if (!InteractTag.IsNone())
	{
		PeregonInteraction::MakeInteractable(Box, InteractTag);
		// Интерактивная деталь всё равно твёрдая: машина и пешеход в неё упираются.
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Box->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	}
	Box->RegisterComponent();
	UPeregonArtSubsystem::ApplyLook(this, Box, Look);
	return Box;
}

void APeregonServicePoint::AddLabel(const FVector& Location, const FString& Text, float SizeCm, const FColor& Ink, const FLinearColor& Paper)
{
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	if (!Art)
	{
		return;
	}
	// Текст рисуется в текстуру шрифтом Golos (кириллица), табличка — плоский квад.
	// Пропорции таблички — по реальной ширине надписи этим шрифтом (а не по числу букв).
	const FVector2D Measured = UPeregonArtSubsystem::MeasureText(Text, EPeregonFont::SemiBold, 34);
	const float Aspect = Measured.Y > 0.0 ? FMath::Clamp(static_cast<float>(Measured.X / Measured.Y) * 1.08f + 0.35f, 1.2f, 16.f)
										 : FMath::Max(1.5f, Text.Len() * 0.62f + 0.6f);
	const float HeightCm = SizeCm * 1.7f;
	const int32 HeightPx = 64;
	const int32 WidthPx = FMath::Clamp(FMath::RoundToInt(HeightPx * Aspect), 64, 1024);
	UMaterialInstanceDynamic* Mat = Art->MakeLabelMaterial(this, Text, FLinearColor(Ink), Paper, WidthPx, HeightPx, 34);
	UPeregonArtSubsystem::AddLabelQuad(this, RootComponent, FTransform(FRotator::ZeroRotator, Location * 100.0), HeightCm * Aspect, HeightCm, Mat);
}

UStaticMeshComponent* APeregonServicePoint::AddShape(UStaticMesh* Mesh, const FVector& Center, const FVector& SizeM, UMaterialInterface* Material, bool bCollision)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(RootComponent);
	Comp->SetStaticMesh(Mesh);
	Comp->SetRelativeLocation(Center * 100.0);
	Comp->SetRelativeScale3D(SizeM);
	Comp->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Comp->RegisterComponent();
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	return Comp;
}

void APeregonServicePoint::AddLight(const FVector& Location, float Candela, const FLinearColor& Color)
{
	// Свет в помещении: без теней (дёшево), радиус — комната.
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(Location * 100.0);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(Candela);
	Light->SetAttenuationRadius(900.f);
	Light->SetLightColor(Color);
	Light->SetCastShadows(false);
	Light->RegisterComponent();
	// Плафон: светится всегда (лампы горят и днём).
	FPeregonLook Panel(EPeregonTex::White, FLinearColor(0.95f, 0.97f, 0.92f), 0.5f);
	Panel.Emissive = 2.5f;
	Panel.WetMask = 0.f;
	Panel.SnowMask = 0.f;
	AddBox(Location + FVector(0.0, 0.0, 0.08), FVector(1.2, 0.3, 0.05), Panel, false);
}

void APeregonServicePoint::AddRoom(const FVector& FloorCenter, const FVector& SizeM, float DoorY, const TArray<FVector2D>& WindowsY, const FPeregonLook& Walls,
	const FPeregonLook& Floor, const FPeregonLook& Inner)
{
	// Помещение с дверным проёмом и окнами в передней стене (+X). Стены 20 см; внутрь можно войти.
	const double Wall = 0.2;
	const double Fx = FloorCenter.X + SizeM.X * 0.5 - Wall * 0.5;
	const double Bx = FloorCenter.X - SizeM.X * 0.5 + Wall * 0.5;
	const double H = SizeM.Z;
	const double Y0 = FloorCenter.Y - SizeM.Y * 0.5;
	const double Y1 = FloorCenter.Y + SizeM.Y * 0.5;
	const double Z = FloorCenter.Z;
	// Пол, потолок, задняя и боковые стены (снаружи — фасад, внутри — светлая отделка).
	AddBox(FVector(FloorCenter.X, FloorCenter.Y, Z + 0.05), FVector(SizeM.X, SizeM.Y, 0.1), Floor, true);
	AddBox(FVector(FloorCenter.X, FloorCenter.Y, Z + H - 0.05), FVector(SizeM.X, SizeM.Y, 0.1), Inner, true);
	AddBox(FVector(Bx, FloorCenter.Y, Z + H * 0.5), FVector(Wall, SizeM.Y, H), Walls, true);
	AddBox(FVector(Bx + Wall * 0.55, FloorCenter.Y, Z + H * 0.5), FVector(0.02, SizeM.Y - 0.5, H - 0.2), Inner, false);
	for (double Y : {Y0 + Wall * 0.5, Y1 - Wall * 0.5})
	{
		AddBox(FVector(FloorCenter.X, Y, Z + H * 0.5), FVector(SizeM.X, Wall, H), Walls, true);
		AddBox(FVector(FloorCenter.X, Y + (Y < FloorCenter.Y ? 1.0 : -1.0) * Wall * 0.55, Z + H * 0.5), FVector(SizeM.X - 0.5, 0.02, H - 0.2), Inner, false);
	}
	// Передняя стена: проёмы (дверь 1.3 × 2.2 м, окна 0.9–2.3 м), между ними — сплошная стена.
	struct FOpening
	{
		double A, B, Low, High;
		bool bGlass;
	};
	TArray<FOpening> Openings;
	Openings.Add({DoorY - 0.65, DoorY + 0.65, 0.0, 2.2, false});
	for (const FVector2D& W : WindowsY)
	{
		Openings.Add({W.X, W.Y, 0.9, 2.3, true});
	}
	Openings.Sort([](const FOpening& L, const FOpening& R) { return L.A < R.A; });
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	UMaterialInstanceDynamic* GlassMat = Art ? Art->MakeGlassMaterial(this, FLinearColor(0.55f, 0.65f, 0.68f), 0.25f) : nullptr;
	double Cursor = Y0;
	auto Solid = [&](double A, double B, double Low, double High) {
		if (B - A > 0.01 && High - Low > 0.01)
		{
			AddBox(FVector(Fx, (A + B) * 0.5, Z + (Low + High) * 0.5), FVector(Wall, B - A, High - Low), Walls, true);
		}
	};
	for (const FOpening& O : Openings)
	{
		Solid(Cursor, O.A, 0.0, H);
		Solid(O.A, O.B, 0.0, O.Low);
		Solid(O.A, O.B, O.High, H);
		if (O.bGlass && GlassMat)
		{
			AddShape(CubeMesh, FVector(Fx, (O.A + O.B) * 0.5, Z + (O.Low + O.High) * 0.5), FVector(0.03, O.B - O.A, O.High - O.Low), GlassMat, true);
		}
		Cursor = O.B;
	}
	Solid(Cursor, Y1, 0.0, H);
}

void APeregonServicePoint::AddPerson(const FVector& Feet, const FLinearColor& Clothes)
{
	// Продавец: условная фигура (туловище, голова, руки) — как манекен.
	AddBox(Feet + FVector(0.0, 0.0, 0.45), FVector(0.3, 0.4, 0.9), FPeregonLook(EPeregonTex::Fabric, FLinearColor(0.2f, 0.2f, 0.25f), 0.9f), false);
	AddBox(Feet + FVector(0.0, 0.0, 1.2), FVector(0.32, 0.48, 0.65), FPeregonLook(EPeregonTex::Fabric, Clothes, 0.9f), false);
	for (double Y : {-0.3, 0.3})
	{
		AddBox(Feet + FVector(0.08, Y, 1.2), FVector(0.12, 0.1, 0.55), FPeregonLook(EPeregonTex::Fabric, Clothes, 0.9f), false);
	}
	AddShape(SphereMesh, Feet + FVector(0.0, 0.0, 1.68), FVector(0.24, 0.22, 0.28),
		UPeregonArtSubsystem::Get(this) ? UPeregonArtSubsystem::Get(this)->MakeMaterial(this, FPeregonLook(EPeregonTex::White, FLinearColor(0.85f, 0.62f, 0.5f), 0.7f))
										: nullptr,
		false);
}

void APeregonServicePoint::AddShelf(const FVector& Base, double LengthY, int32 Levels, const TArray<FName>& Items, int32& ItemCursor)
{
	// Стеллаж вдоль Y: стойки, полки; на полках — товар. То, что продаётся, можно взять в руки (ItemTag).
	const FPeregonLook Metal(EPeregonTex::Paint, FLinearColor(0.55f, 0.58f, 0.6f), 0.5f, 0.6f);
	AddBox(Base + FVector(-0.25, 0.0, 0.85), FVector(0.04, LengthY, 1.7), Metal, true);
	for (double Y : {-LengthY * 0.5, LengthY * 0.5})
	{
		AddBox(Base + FVector(0.0, Y, 0.85), FVector(0.5, 0.04, 1.7), Metal, false);
	}
	static const FLinearColor Goods[] = {FLinearColor(0.8f, 0.15f, 0.1f), FLinearColor(0.9f, 0.75f, 0.2f), FLinearColor(0.2f, 0.4f, 0.75f),
		FLinearColor(0.25f, 0.6f, 0.3f), FLinearColor(0.85f, 0.85f, 0.8f), FLinearColor(0.55f, 0.3f, 0.15f)};
	rgen::Rng Rand(static_cast<uint64>(static_cast<int64>(Base.X * 31.0 + Base.Y * 17.0 + 500.0)));
	for (int32 L = 0; L < Levels; ++L)
	{
		const double Z = 0.15 + L * (1.5 / Levels);
		AddBox(Base + FVector(0.0, 0.0, Z), FVector(0.5, LengthY, 0.03), Metal, true);
		double Y = -LengthY * 0.5 + 0.2;
		while (Y < LengthY * 0.5 - 0.2)
		{
			if (Items.Num() > 0 && Rand.Chance(0.3))
			{
				const FName Id = Items[ItemCursor++ % Items.Num()];
				AddItemProp(Id, Base + FVector(0.02, Y, Z + 0.02));
				Y += 0.45;
				continue;
			}
			// Просто товар: банки, пачки, бутылки.
			const FLinearColor C = Goods[Rand.Next() % UE_ARRAY_COUNT(Goods)];
			const double W = Rand.Range(0.08, 0.2);
			const double Hh = Rand.Range(0.12, 0.3);
			if (Rand.Chance(0.35))
			{
				AddShape(CylinderMesh, Base + FVector(0.0, Y, Z + 0.015 + Hh * 0.5), FVector(W, W, Hh),
					UPeregonArtSubsystem::Get(this) ? UPeregonArtSubsystem::Get(this)->MakeMaterial(this, FPeregonLook(EPeregonTex::Paint, C, 0.4f)) : nullptr, false);
			}
			else
			{
				AddBox(Base + FVector(0.0, Y, Z + 0.015 + Hh * 0.5), FVector(0.3, W, Hh), FPeregonLook(EPeregonTex::Paint, C, 0.6f), false);
			}
			Y += W + 0.03;
		}
	}
}

void APeregonServicePoint::AddItemProp(FName Id, const FVector& OnSurface)
{
	const FPeregonItemDef* Def = PeregonItems::Find(Id);
	if (!Def)
	{
		return;
	}
	// Как выглядит товар на полке: карта — сложенный лист, канистра — коробка с ручкой, термос — цилиндр.
	FVector Size(0.3, 0.22, 0.02);
	UStaticMesh* Mesh = CubeMesh;
	if (Id == PeregonItems::CanisterFull || Id == PeregonItems::CanisterEmpty)
	{
		Size = FVector(0.2, 0.36, 0.46);
	}
	else if (Id == PeregonItems::Thermos)
	{
		Size = FVector(0.11, 0.11, 0.32);
		Mesh = CylinderMesh;
	}
	else if (Def->Kind != EPeregonItemKind::Misc)
	{
		// Детали, инструмент, банки — по своим габаритам (на полке чуть меньше, чтобы влезли).
		Size = Def->SizeCm / 100.0 * 0.8;
		Mesh = Def->Kind == EPeregonItemKind::Consumable && Def->SizeCm.X < 15.f ? CylinderMesh : CubeMesh;
	}
	FPeregonLook Look(Id == PeregonItems::CanisterFull || Id == PeregonItems::CanisterEmpty ? EPeregonTex::Paint : EPeregonTex::Plastic, Def->Color, 0.5f);
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	UStaticMeshComponent* Prop = AddShape(Mesh, OnSurface + FVector(0.0, 0.0, Size.Z * 0.5), Size, Art ? Art->MakeMaterial(this, Look) : nullptr, false);
	PeregonInteraction::MakeInteractable(Prop, PeregonService::ItemTag);
	Prop->ComponentTags.Add(Id);
	if (PeregonPartMeshes::Has(Id) && Art && Art->HasWeatherMaterials())
	{
		// Деталь на полке — настоящей моделью (в 0.8 размера); коробка остаётся невидимой мишенью для взгляда.
		FPeregonMeshBuffers Buf;
		PeregonPartMeshes::Build(Id, Buf, FTransform(FRotator(0.f, 90.f, 0.f), FVector::ZeroVector, FVector(0.8f)));
		UProceduralMeshComponent* Model = NewObject<UProceduralMeshComponent>(this);
		Model->SetupAttachment(RootComponent);
		Model->SetRelativeLocation(OnSurface * 100.0);
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Model->RegisterComponent();
		Model->CreateMeshSection(0, Buf.Vertices, Buf.Triangles, Buf.Normals, Buf.UVs, Buf.Colors, TArray<FProcMeshTangent>(), false);
		Model->SetMaterial(0, Art->MakeMaterial(this, FPeregonLook(EPeregonTex::Paint, FLinearColor::White, 0.55f, 0.25f)));
		Prop->SetVisibility(false);
		// Когда товар взяли, мишень прячут — модель исчезает вместе с ней.
		PropModels.Add(Prop, Model);
	}
}

float APeregonServicePoint::ItemPrice(FName Id) const
{
	const FPeregonItemDef* Def = PeregonItems::Find(Id);
	if (!Def)
	{
		return 0.f;
	}
	if (Id == PeregonItems::CanisterFull)
	{
		const FPeregonItemDef* Empty = PeregonItems::Find(PeregonItems::CanisterEmpty);
		return (Empty ? Empty->Price : 450.f) + PeregonItems::CanisterLiters * FuelPrice;
	}
	return Def->Price;
}

void APeregonServicePoint::BuildFuel()
{
	const FPeregonLook White(EPeregonTex::Paint, FLinearColor(0.85f, 0.85f, 0.83f), 0.5f);
	const FPeregonLook Red(EPeregonTex::Paint, FLinearColor(0.6f, 0.05f, 0.04f), 0.45f);
	const FPeregonLook Concrete(EPeregonTex::Concrete, FLinearColor::White, 0.9f);
	const FPeregonLook Plaster(EPeregonTex::Plaster, FLinearColor::White, 0.9f);
	const FPeregonLook Glass(EPeregonTex::Glass, FLinearColor::White, 0.1f);
	const FPeregonLook Tiles(EPeregonTex::Concrete, FLinearColor(0.75f, 0.72f, 0.66f), 0.6f);
	const FPeregonLook InnerWall(EPeregonTex::Plaster, FLinearColor(0.9f, 0.9f, 0.85f), 0.9f);
	const FPeregonLook Dark(EPeregonTex::Paint, FLinearColor(0.25f, 0.25f, 0.27f), 0.5f);
	FPeregonLook CanopyLamp(EPeregonTex::White, FLinearColor(1.f, 0.95f, 0.85f), 0.5f);
	CanopyLamp.NightGlow = 6.f;
	CanopyLamp.WetMask = 0.f;
	CanopyLamp.SnowMask = 0.f;

	// Навес над колонками, лампы под навесом (ночью светятся).
	for (double X : {7.5, 14.5})
	{
		for (double Y : {-6.5, 6.5})
		{
			AddBox(FVector(X, Y, 2.6), FVector(0.35, 0.35, 5.2), White, true);
		}
	}
	AddBox(FVector(11.0, 0.0, 5.4), FVector(9.0, 16.0, 0.4), White, false);
	AddBox(FVector(11.0, 0.0, 5.0), FVector(9.2, 16.2, 0.35), Red, false);
	for (double Y : {-4.0, 0.0, 4.0})
	{
		AddBox(FVector(11.0, Y, 4.8), FVector(2.4, 0.4, 0.06), CanopyLamp, false);
	}
	AddLabel(FVector(15.75, 0.0, 5.0), TEXT("АЗС"), 22.f, FColor::White, FLinearColor(0.6f, 0.05f, 0.04f));

	// Островки с колонками: корпус, табло, пистолет на крючке, шланг, урна и огнетушитель.
	for (const FVector& Pump : PeregonService::PumpLocal)
	{
		AddBox(FVector(Pump.X, Pump.Y, 0.1), FVector(1.4, 3.2, 0.2), Concrete, true);
		AddBox(Pump, FVector(0.7, 1.0, 1.8), Red, true, PeregonService::PumpTag);
		AddBox(Pump + FVector(0.36, 0.0, 0.35), FVector(0.02, 0.6, 0.4), Glass, false);
		AddBox(Pump + FVector(0.0, 0.0, 0.98), FVector(0.72, 1.02, 0.16), White, false);
		AddLabel(Pump + FVector(0.37, 0.0, -0.25), TEXT("АИ-92"), 9.f, FColor::White, FLinearColor(0.6f, 0.05f, 0.04f));
		AddBox(Pump + FVector(0.38, 0.42, -0.1), FVector(0.08, 0.06, 0.25), Dark, false);
		AddBox(Pump + FVector(0.3, 0.55, -0.45), FVector(0.05, 0.05, 0.9), Dark, false);
		AddShape(CylinderMesh, FVector(Pump.X, Pump.Y + 1.35, 0.55), FVector(0.4, 0.4, 0.7),
			UPeregonArtSubsystem::Get(this) ? UPeregonArtSubsystem::Get(this)->MakeMaterial(this, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.2f, 0.35f, 0.2f), 0.6f)) : nullptr,
			true);
		AddShape(CylinderMesh, FVector(Pump.X, Pump.Y - 1.35, 0.6), FVector(0.16, 0.16, 0.6),
			UPeregonArtSubsystem::Get(this) ? UPeregonArtSubsystem::Get(this)->MakeMaterial(this, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.7f, 0.04f, 0.03f), 0.4f)) : nullptr,
			false);
	}

	// Здание: торговый зал с кассой — можно зайти, взять товар с полки (оплата на месте) или подойти к кассе.
	AddRoom(FVector(-4.0, 0.0, 0.0), FVector(8.0, 14.0, 3.5), 4.2f, {FVector2D(-6.2, -3.2), FVector2D(-2.6, 2.6)}, Plaster, Tiles, InnerWall);
	AddBox(FVector(-4.0, 0.0, 3.6), FVector(8.6, 14.6, 0.25), Red, false);
	AddBox(FVector(0.55, 4.2, 2.45), FVector(1.0, 2.0, 0.08), Red, false); // козырёк над входом
	AddLabel(FVector(0.62, 4.2, 2.62), TEXT("МАГАЗИН · КАССА"), 6.f, FColor::White, FLinearColor(0.6f, 0.05f, 0.04f));
	AddLabel(FVector(0.12, -1.8, 3.0), DisplayName, 30.f, FColor(30, 30, 30), FLinearColor(0.8f, 0.78f, 0.72f));
	// Коврик у входа.
	AddBox(FVector(-0.9, 4.2, 0.11), FVector(1.2, 1.4, 0.02), Dark, false);

	// Касса: прилавок, кассовый аппарат, продавец.
	AddBox(FVector(-5.6, 4.3, 0.55), FVector(0.8, 3.4, 1.1), FPeregonLook(EPeregonTex::WoodPlanks, FLinearColor(0.55f, 0.4f, 0.28f), 0.7f), true, PeregonService::ShopTag);
	AddBox(FVector(-5.6, 3.4, 1.22), FVector(0.4, 0.45, 0.25), Dark, false);
	AddBox(FVector(-5.55, 3.4, 1.42), FVector(0.05, 0.3, 0.18), FPeregonLook(EPeregonTex::Glass, FLinearColor(0.3f, 0.9f, 0.4f), 0.2f), false);
	AddPerson(FVector(-6.6, 4.3, 0.1), FLinearColor(0.15f, 0.3f, 0.6f));
	AddLabel(FVector(-7.75, 4.3, 2.6), TEXT("КАССА"), 12.f, FColor::White, FLinearColor(0.6f, 0.05f, 0.04f));
	// Сигареты и мелочь на стене за кассой.
	int32 Cursor = 0;
	const TArray<FName> NoItems;
	AddShelf(FVector(-7.5, 4.3, 0.1), 3.0, 4, NoItems, Cursor);

	// Стеллажи с товаром: что продаётся — лежит на полках.
	const TArray<FName> Catalog = GetShopCatalog();
	AddShelf(FVector(-4.2, -1.6, 0.1), 3.5, 3, Catalog, Cursor);
	AddShelf(FVector(-4.2, -5.0, 0.1), 3.5, 3, Catalog, Cursor);
	// Холодильник с напитками у задней стены.
	AddBox(FVector(-7.45, 0.6, 1.0), FVector(0.6, 1.6, 2.0), White, true);
	AddBox(FVector(-7.13, 0.6, 1.05), FVector(0.03, 1.4, 1.7), Glass, false);
	AddLight(FVector(-4.0, -3.5, 3.3), 16.f, FLinearColor(0.95f, 1.f, 0.95f));
	AddLight(FVector(-4.0, 3.0, 3.3), 16.f, FLinearColor(0.95f, 1.f, 0.95f));
	AddLabel(FVector(-7.75, -3.7, 2.7), TEXT("ПРОДУКТЫ · АВТОТОВАРЫ"), 8.f, FColor(30, 30, 30), FLinearColor(0.9f, 0.88f, 0.8f));

	// Стела с ценой у дороги.
	AddBox(FVector(17.0, 9.5, 3.0), FVector(0.4, 1.8, 6.0), White, true);
	AddLabel(FVector(17.22, 9.5, 4.6), TEXT("АИ-92"), 20.f, FColor(160, 20, 15), FLinearColor(0.85f, 0.85f, 0.83f));
	AddLabel(FVector(17.22, 9.5, 3.9), FString::Printf(TEXT("%.2f"), FuelPrice), 24.f, FColor(20, 20, 20), FLinearColor(0.85f, 0.85f, 0.83f));
	// Колонка «Вода · воздух» и скамейка у входа.
	AddBox(FVector(1.2, -7.6, 0.7), FVector(0.4, 0.5, 1.4), FPeregonLook(EPeregonTex::Paint, FLinearColor(0.15f, 0.3f, 0.6f), 0.5f), true);
	AddLabel(FVector(1.42, -7.6, 1.2), TEXT("ВОЗДУХ"), 4.f, FColor::White, FLinearColor(0.15f, 0.3f, 0.6f));
	AddBox(FVector(1.0, 1.2, 0.45), FVector(0.4, 1.8, 0.06), FPeregonLook(EPeregonTex::WoodPlanks, FLinearColor(0.6f, 0.45f, 0.3f), 0.9f), true);
}

void APeregonServicePoint::BuildCafe()
{
	const FPeregonLook Wood(EPeregonTex::WoodPlanks, FLinearColor(0.78f, 0.55f, 0.28f), 0.9f);
	const FPeregonLook Roof(EPeregonTex::RoofMetal, FLinearColor(0.35f, 0.15f, 0.1f), 0.5f, 0.5f);
	const FPeregonLook Door(EPeregonTex::WoodPlanks, FLinearColor(0.35f, 0.22f, 0.12f), 0.9f);
	const FPeregonLook Board(EPeregonTex::Paint, FLinearColor(0.8f, 0.75f, 0.6f), 0.6f);
	const FPeregonLook TableLook(EPeregonTex::WoodPlanks, FLinearColor(0.55f, 0.42f, 0.3f), 0.9f);
	const FPeregonLook Floor(EPeregonTex::WoodPlanks, FLinearColor(0.5f, 0.36f, 0.24f), 0.8f);
	const FPeregonLook Inner(EPeregonTex::Plaster, FLinearColor(0.92f, 0.86f, 0.72f), 0.9f);
	const FPeregonLook Cloth(EPeregonTex::Fabric, FLinearColor(0.8f, 0.25f, 0.2f), 0.9f);

	// Зал кафе: столы с клеёнкой, стойка с самоваром, повариха, дверь в номера (ночлег).
	AddRoom(FVector(-2.0, 0.0, 0.0), FVector(9.0, 12.0, 3.2), 0.f, {FVector2D(-5.0, -2.0), FVector2D(2.0, 5.0)}, Wood, Floor, Inner);
	AddBox(FVector(-2.0, 0.0, 3.35), FVector(10.0, 13.0, 0.3), Roof, false);
	AddBox(FVector(2.6, 0.0, 2.75), FVector(0.1, 6.0, 0.6), Board, false);
	AddLabel(FVector(2.66, 0.0, 2.78), DisplayName, 20.f, FColor(60, 25, 10), FLinearColor(0.8f, 0.75f, 0.6f));
	AddLabel(FVector(2.66, 0.0, 2.55), TEXT("ОБЕДЫ · НОЧЛЕГ"), 8.f, FColor(60, 25, 10), FLinearColor(0.8f, 0.75f, 0.6f));
	for (double Y : {-3.5, 3.5})
	{
		for (double X : {-0.5, -3.0})
		{
			AddBox(FVector(X, Y, 0.75), FVector(1.1, 1.1, 0.06), Cloth, true);
			AddBox(FVector(X, Y, 0.37), FVector(0.1, 0.1, 0.74), TableLook, false);
			for (double Side : {-0.85, 0.85})
			{
				AddBox(FVector(X, Y + Side, 0.45), FVector(0.45, 0.45, 0.05), TableLook, true);
				AddBox(FVector(X, Y + Side * 1.2, 0.75), FVector(0.45, 0.05, 0.6), TableLook, false);
			}
		}
	}
	// Стойка раздачи: самовар, поднос, меню — здесь чай в дорогу.
	AddBox(FVector(-5.4, 0.0, 0.55), FVector(0.8, 4.0, 1.1), TableLook, true, PeregonService::ShopTag);
	AddShape(CylinderMesh, FVector(-5.4, -1.2, 1.35), FVector(0.35, 0.35, 0.5),
		UPeregonArtSubsystem::Get(this) ? UPeregonArtSubsystem::Get(this)->MakeMaterial(this, FPeregonLook(EPeregonTex::Chrome, FLinearColor(1.f, 0.75f, 0.4f), 0.3f, 1.f)) : nullptr,
		false);
	AddPerson(FVector(-6.3, 0.4, 0.1), FLinearColor(0.9f, 0.9f, 0.88f));
	AddBox(FVector(-6.4, -2.8, 1.8), FVector(0.05, 1.8, 1.0), FPeregonLook(EPeregonTex::Paint, FLinearColor(0.1f, 0.15f, 0.1f), 0.8f), false);
	AddLabel(FVector(-6.34, -2.8, 1.95), TEXT("Борщ  ·  Пельмени"), 6.f, FColor(230, 230, 220), FLinearColor(0.1f, 0.15f, 0.1f));
	AddLabel(FVector(-6.34, -2.8, 1.6), TEXT("Чай  ·  Пирожки"), 6.f, FColor(230, 230, 220), FLinearColor(0.1f, 0.15f, 0.1f));
	// Дверь в номера у задней стены.
	AddBox(FVector(-6.35, 4.6, 1.05), FVector(0.08, 1.0, 2.1), Door, true, PeregonService::RestTag);
	AddLabel(FVector(-6.3, 4.6, 2.35), TEXT("НОМЕРА"), 5.f, FColor(60, 25, 10), FLinearColor(0.8f, 0.75f, 0.6f));
	AddLight(FVector(-2.0, -3.0, 3.0), 14.f, FLinearColor(1.f, 0.85f, 0.65f));
	AddLight(FVector(-2.0, 3.0, 3.0), 14.f, FLinearColor(1.f, 0.85f, 0.65f));

	// Летние столики у входа.
	for (double Y : {-4.5, 4.5})
	{
		AddBox(FVector(6.0, Y, 0.75), FVector(1.3, 1.3, 0.08), TableLook, true);
		AddBox(FVector(6.0, Y, 0.37), FVector(0.12, 0.12, 0.74), TableLook, false);
		AddBox(FVector(6.0, Y - 1.1, 0.45), FVector(1.3, 0.35, 0.06), TableLook, false);
		AddBox(FVector(6.0, Y + 1.1, 0.45), FVector(1.3, 0.35, 0.06), TableLook, false);
	}
	(void)Door;
}

void APeregonServicePoint::BuildShop()
{
	const FPeregonLook Walls(EPeregonTex::Plaster, FLinearColor(0.75f, 0.78f, 0.62f), 0.9f);
	const FPeregonLook Roof(EPeregonTex::RoofMetal, FLinearColor(0.2f, 0.32f, 0.22f), 0.5f, 0.5f);
	const FPeregonLook Board(EPeregonTex::Paint, FLinearColor(0.12f, 0.25f, 0.5f), 0.6f);
	const FPeregonLook Floor(EPeregonTex::WoodPlanks, FLinearColor(0.45f, 0.35f, 0.25f), 0.8f);
	const FPeregonLook Inner(EPeregonTex::Plaster, FLinearColor(0.85f, 0.9f, 0.85f), 0.9f);
	const FPeregonLook Counter(EPeregonTex::WoodPlanks, FLinearColor(0.6f, 0.45f, 0.3f), 0.7f);

	// Сельмаг: прилавок поперёк зала, за ним полки и продавщица; товар для дороги — на прилавке.
	AddRoom(FVector(-2.0, 0.0, 0.0), FVector(6.0, 8.0, 3.0), 1.8f, {FVector2D(-3.0, -0.6)}, Walls, Floor, Inner);
	AddBox(FVector(-2.0, 0.0, 3.15), FVector(6.6, 8.6, 0.3), Roof, false);
	AddBox(FVector(1.1, 0.0, 2.6), FVector(0.1, 5.6, 0.6), Board, false);
	const bool bParts = Kind == EPeregonService::Parts;
	AddLabel(FVector(1.16, 0.0, 2.6), bParts ? TEXT("АВТОЗАПЧАСТИ «ВАЗ»") : TEXT("ПРОДУКТЫ"), bParts ? 14.f : 22.f, FColor::White,
		bParts ? FLinearColor(0.55f, 0.1f, 0.08f) : FLinearColor(0.12f, 0.25f, 0.5f));
	AddLabel(FVector(1.1, -1.8, 0.65), bParts ? TEXT("УЗЛЫ · ИНСТРУМЕНТ · КРАСКА · МАСЛО") : TEXT("КАРТЫ · ЧАЙ · КАНИСТРЫ"), 5.f, FColor(30, 30, 30),
		FLinearColor(0.75f, 0.78f, 0.62f));
	AddBox(FVector(-2.6, -0.6, 0.5), FVector(0.7, 5.6, 1.0), Counter, true, PeregonService::ShopTag);
	AddPerson(FVector(-3.6, -0.6, 0.1), FLinearColor(0.7f, 0.2f, 0.3f));
	int32 Cursor = 0;
	const TArray<FName> NoItems;
	const TArray<FName> Catalog = GetShopCatalog();
	// В магазине запчастей узлы стоят на стеллаже — бери любой и плати на месте.
	AddShelf(FVector(-4.6, -0.6, 0.1), 5.6, 4, bParts ? Catalog : NoItems, Cursor);
	// Товар в дорогу — на прилавке, бери и плати.
	for (int32 K = 0; K < (bParts ? 0 : Catalog.Num()); ++K)
	{
		AddItemProp(Catalog[K], FVector(-2.5, -2.8 + K * 0.8, 1.0));
	}
	AddLight(FVector(-2.0, -1.0, 2.75), 14.f, FLinearColor(1.f, 0.95f, 0.85f));
}

TArray<FName> APeregonServicePoint::GetShopCatalog() const
{
	TArray<FName> Items;
	auto AddIf = [&Items](FName Id) {
		if (!Id.IsNone() && PeregonItems::Find(Id))
		{
			Items.AddUnique(Id);
		}
	};
	switch (Kind)
	{
	case EPeregonService::Fuel:
		AddIf(PeregonItems::MapForRegion(Region));
		AddIf(PeregonItems::CanisterFull);
		AddIf(PeregonItems::CanisterEmpty);
		AddIf(PeregonItems::Thermos);
		// Расходка на АЗС: масло, антифриз, свечи, фильтры.
		AddIf(PeregonItems::Oil);
		AddIf(PeregonItems::Coolant);
		AddIf(PeregonItems::PartItem(static_cast<int32>(vsim::parts::Slot::SparkPlugs)));
		AddIf(PeregonItems::PartItem(static_cast<int32>(vsim::parts::Slot::AirFilter)));
		AddIf(PeregonItems::PartItem(static_cast<int32>(vsim::parts::Slot::OilFilter)));
		AddIf(PeregonItems::PartItem(static_cast<int32>(vsim::parts::Slot::FuelFilter)));
		break;
	case EPeregonService::Shop:
		AddIf(PeregonItems::MapForRegion(Region));
		AddIf(PeregonItems::MapForRegion(Region + 1));
		AddIf(PeregonItems::Thermos);
		AddIf(PeregonItems::CanisterEmpty);
		AddIf(PeregonItems::Oil);
		AddIf(PeregonItems::ConsumableItem(static_cast<int32>(vsim::parts::Consumable::Putty)));
		AddIf(PeregonItems::ConsumableItem(static_cast<int32>(vsim::parts::Consumable::FlapDisc)));
		break;
	case EPeregonService::Parts:
		for (const FName& Id : PeregonItems::PartsShopStock())
		{
			AddIf(Id);
		}
		AddIf(PeregonItems::MapForRegion(Region + 1));
		break;
	default:
		AddIf(PeregonItems::Thermos);
		break;
	}
	return Items;
}

bool APeregonServicePoint::GetCarNeed(float& OutLiters) const
{
	OutLiters = 0.f;
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	const APeregonCar* Car = GM ? GM->GetCar() : nullptr;
	if (!Car || !Car->GetVehicleMovement())
	{
		return false;
	}
	bool bNear = false;
	for (const FVector& Pump : PeregonService::PumpLocal)
	{
		bNear = bNear || FVector::Dist(Car->GetActorLocation(), GetActorTransform().TransformPosition(Pump * 100.0)) < PeregonService::PumpReachCm;
	}
	const FPeregonTelemetry T = Car->GetVehicleMovement()->GetTelemetry();
	OutLiters = FMath::Max(0.f, T.FuelCapacityL - T.FuelL);
	return bNear;
}

FString APeregonServicePoint::GetInteractPrompt(const UPrimitiveComponent* Part, const APawn* User) const
{
	const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	if (!Part || !GI)
	{
		return FString();
	}
	if (Part->ComponentHasTag(PeregonService::ItemTag))
	{
		if (!User || !User->IsA<APeregonCharacter>() || Part->ComponentTags.Num() < 2)
		{
			return FString();
		}
		const FName Id = Part->ComponentTags.Last();
		const float Price = ItemPrice(Id);
		return GI->Money + 0.01f >= Price ? FString::Printf(TEXT("Взять: %s — %s"), *PeregonItems::DisplayName(Id), *PeregonService::Rubles(Price))
										  : FString::Printf(TEXT("%s — %s (не хватает денег)"), *PeregonItems::DisplayName(Id), *PeregonService::Rubles(Price));
	}
	if (Part->ComponentHasTag(PeregonService::ShopTag))
	{
		return User && User->IsA<APeregonCharacter>() ? FString::Printf(TEXT("Касса: %s"), *DisplayName) : FString();
	}
	if (Part->ComponentHasTag(PeregonService::PumpTag))
	{
		float Liters = 0.f;
		const bool bCarHere = GetCarNeed(Liters) && Liters >= 0.5f;
		if (!bCarHere && GI->Pockets.Has(PeregonItems::CanisterEmpty))
		{
			const float Cost = PeregonItems::CanisterLiters * FuelPrice;
			return GI->Money + 0.01f >= Cost ? FString::Printf(TEXT("Наполнить канистру 20 л за %s"), *PeregonService::Rubles(Cost))
											: FString::Printf(TEXT("На канистру не хватает денег (%s)"), *PeregonService::Rubles(GI->Money));
		}
		if (!GetCarNeed(Liters))
		{
			return TEXT("Подгоните машину к колонке");
		}
		if (Liters < 0.5f)
		{
			return TEXT("Бак полон");
		}
		const float Affordable = FMath::Min(Liters, GI->Money / FuelPrice);
		if (Affordable < 0.5f)
		{
			return FString::Printf(TEXT("Не хватает денег (%s)"), *PeregonService::Rubles(GI->Money));
		}
		return FString::Printf(TEXT("Заправить %.0f л за %s"), Affordable, *PeregonService::Rubles(Affordable * FuelPrice));
	}
	if (Part->ComponentHasTag(PeregonService::RestTag))
	{
		if (!User || !User->IsA<APeregonCharacter>())
		{
			return FString();
		}
		const float H = GI->TimeOfDayHours;
		const bool bNight = H >= 20.f || H < 6.f;
		return bNight ? FString::Printf(TEXT("Переночевать до утра и сохраниться (%s)"), *PeregonService::Rubles(NightStayPrice))
					  : FString(TEXT("Отдохнуть и сохранить игру"));
	}
	return FString();
}

void APeregonServicePoint::InteractStart(UPrimitiveComponent* Part, APawn* User)
{
	UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	if (!Part || !GI || !GM)
	{
		return;
	}
	if (Part->ComponentHasTag(PeregonService::ItemTag) && Part->ComponentTags.Num() >= 2)
	{
		// Берём с полки и платим на месте; товар исчезает с полки (до следующего приезда).
		APeregonPlayerController* PC = User ? Cast<APeregonPlayerController>(User->GetController()) : nullptr;
		if (PC && PC->BuyItemAt(Part->ComponentTags.Last(), ItemPrice(Part->ComponentTags.Last())))
		{
			Part->SetVisibility(false);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (const TWeakObjectPtr<UPrimitiveComponent>* Model = PropModels.Find(Part))
			{
				if (Model->IsValid())
				{
					Model->Get()->SetVisibility(false);
				}
			}
			PeregonAudio::PlayEvent(this, TEXT("item.pickup"), Part->GetComponentLocation(), 0.8f);
		}
		return;
	}
	if (Part->ComponentHasTag(PeregonService::ShopTag))
	{
		if (APeregonPlayerController* PC = User ? Cast<APeregonPlayerController>(User->GetController()) : nullptr)
		{
			PC->OpenShop(GetShopCatalog(), DisplayName, FuelPrice);
		}
		return;
	}
	if (Part->ComponentHasTag(PeregonService::PumpTag))
	{
		float Liters = 0.f;
		const bool bCarHere = GetCarNeed(Liters) && Liters >= 0.5f;
		if (!bCarHere && GI->Pockets.Has(PeregonItems::CanisterEmpty))
		{
			if (!GI->TrySpend(PeregonItems::CanisterLiters * FuelPrice))
			{
				GM->Toast(TEXT("Не хватает денег на бензин"));
				return;
			}
			GI->Pockets.Remove(PeregonItems::CanisterEmpty);
			GI->Pockets.Add(PeregonItems::CanisterFull);
			GM->Toast(TEXT("Канистра наполнена: 20 л"));
			return;
		}
		if (!bCarHere)
		{
			return;
		}
		const float Buy = FMath::FloorToFloat(FMath::Min(Liters, GI->Money / FuelPrice) * 10.f) / 10.f;
		if (Buy < 0.5f || !GI->TrySpend(Buy * FuelPrice))
		{
			GM->Toast(TEXT("Не хватает денег на бензин"));
			return;
		}
		GM->GetCar()->GetVehicleMovement()->Refuel(Buy);
		GM->Toast(FString::Printf(TEXT("Заправлено %.1f л — %s"), Buy, *PeregonService::Rubles(Buy * FuelPrice)));
		return;
	}
	if (Part->ComponentHasTag(PeregonService::RestTag) && User && User->IsA<APeregonCharacter>())
	{
		const float H = GI->TimeOfDayHours;
		if (H >= 20.f || H < 6.f)
		{
			if (!GI->TrySpend(NightStayPrice))
			{
				GM->Toast(TEXT("Не хватает денег на ночлег"));
				return;
			}
			if (H >= 20.f)
			{
				++GI->Day;
			}
			GI->TimeOfDayHours = 7.f;
		}
		GM->Toast(GM->SaveGame(DisplayName) ? TEXT("Игра сохранена") : TEXT("Не удалось сохранить"));
	}
}
