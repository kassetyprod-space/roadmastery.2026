#include "World/PeregonServicePoint.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/PeregonGameInstance.h"
#include "Game/PeregonGameMode.h"
#include "Game/PeregonPlayerController.h"
#include "Player/PeregonCharacter.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"

namespace PeregonService
{
const FName PumpTag(TEXT("Pump"));
const FName RestTag(TEXT("Rest"));
const FName ShopTag(TEXT("Shop"));
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
	switch (Kind)
	{
	case EPeregonService::Fuel: BuildFuel(); break;
	case EPeregonService::Shop: BuildShop(); break;
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

void APeregonServicePoint::BuildFuel()
{
	const FPeregonLook White(EPeregonTex::Paint, FLinearColor(0.85f, 0.85f, 0.83f), 0.5f);
	const FPeregonLook Red(EPeregonTex::Paint, FLinearColor(0.6f, 0.05f, 0.04f), 0.45f);
	const FPeregonLook Concrete(EPeregonTex::Concrete, FLinearColor::White, 0.9f);
	const FPeregonLook Plaster(EPeregonTex::Plaster, FLinearColor::White, 0.9f);
	const FPeregonLook Glass(EPeregonTex::Glass, FLinearColor::White, 0.1f);

	// Навес над колонками.
	for (double X : {7.5, 14.5})
	{
		for (double Y : {-6.5, 6.5})
		{
			AddBox(FVector(X, Y, 2.6), FVector(0.35, 0.35, 5.2), White, true);
		}
	}
	AddBox(FVector(11.0, 0.0, 5.4), FVector(9.0, 16.0, 0.4), White, false);
	AddBox(FVector(11.0, 0.0, 5.0), FVector(9.2, 16.2, 0.35), Red, false);
	AddLabel(FVector(15.75, 0.0, 5.0), TEXT("АЗС"), 22.f, FColor::White, FLinearColor(0.6f, 0.05f, 0.04f));

	// Островки с колонками.
	for (const FVector& Pump : PeregonService::PumpLocal)
	{
		AddBox(FVector(Pump.X, Pump.Y, 0.1), FVector(1.4, 3.2, 0.2), Concrete, true);
		AddBox(Pump, FVector(0.7, 1.0, 1.8), Red, true, PeregonService::PumpTag);
		AddBox(Pump + FVector(0.36, 0.0, 0.35), FVector(0.02, 0.6, 0.4), Glass, false);
		AddLabel(Pump + FVector(0.37, 0.0, -0.25), TEXT("АИ-92"), 9.f, FColor::White, FLinearColor(0.6f, 0.05f, 0.04f));
	}

	// Магазин.
	AddBox(FVector(-4.0, 0.0, 1.75), FVector(8.0, 14.0, 3.5), Plaster, true);
	AddBox(FVector(-4.0, 0.0, 3.6), FVector(8.6, 14.6, 0.25), Red, false);
	AddBox(FVector(0.02, -1.5, 1.4), FVector(0.06, 7.0, 1.8), Glass, false);
	AddBox(FVector(0.05, 4.2, 1.1), FVector(0.1, 1.2, 2.2), FPeregonLook(EPeregonTex::Paint, FLinearColor(0.25f, 0.25f, 0.27f), 0.5f), true,
		PeregonService::ShopTag);
	AddLabel(FVector(0.12, 4.2, 2.45), TEXT("МАГАЗИН"), 7.f, FColor::White, FLinearColor(0.6f, 0.05f, 0.04f));
	AddLabel(FVector(0.08, 0.0, 3.0), DisplayName, 30.f, FColor(30, 30, 30), FLinearColor(0.8f, 0.78f, 0.72f));

	// Стела с ценой у дороги.
	AddBox(FVector(17.0, 9.5, 3.0), FVector(0.4, 1.8, 6.0), White, true);
	AddLabel(FVector(17.22, 9.5, 4.6), TEXT("АИ-92"), 20.f, FColor(160, 20, 15), FLinearColor(0.85f, 0.85f, 0.83f));
	AddLabel(FVector(17.22, 9.5, 3.9), FString::Printf(TEXT("%.2f"), FuelPrice), 24.f, FColor(20, 20, 20), FLinearColor(0.85f, 0.85f, 0.83f));
}

void APeregonServicePoint::BuildCafe()
{
	const FPeregonLook Wood(EPeregonTex::WoodPlanks, FLinearColor(0.78f, 0.55f, 0.28f), 0.9f);
	const FPeregonLook Roof(EPeregonTex::RoofMetal, FLinearColor(0.35f, 0.15f, 0.1f), 0.5f, 0.5f);
	const FPeregonLook Door(EPeregonTex::WoodPlanks, FLinearColor(0.35f, 0.22f, 0.12f), 0.9f);
	const FPeregonLook Glass(EPeregonTex::Glass, FLinearColor(1.2f, 1.1f, 0.8f), 0.1f);
	const FPeregonLook Board(EPeregonTex::Paint, FLinearColor(0.8f, 0.75f, 0.6f), 0.6f);
	const FPeregonLook TableLook(EPeregonTex::WoodPlanks, FLinearColor(0.55f, 0.42f, 0.3f), 0.9f);

	AddBox(FVector(-2.0, 0.0, 1.6), FVector(9.0, 12.0, 3.2), Wood, true);
	AddBox(FVector(-2.0, 0.0, 3.35), FVector(10.0, 13.0, 0.3), Roof, false);
	AddBox(FVector(2.53, 0.0, 1.1), FVector(0.1, 1.3, 2.2), Door, true, PeregonService::RestTag);
	for (double Y : {-3.5, 3.5})
	{
		AddBox(FVector(2.52, Y, 1.6), FVector(0.06, 2.2, 1.2), Glass, false);
	}
	AddBox(FVector(2.6, 0.0, 2.75), FVector(0.1, 6.0, 0.6), Board, false);
	AddLabel(FVector(2.66, 0.0, 2.78), DisplayName, 20.f, FColor(60, 25, 10), FLinearColor(0.8f, 0.75f, 0.6f));
	AddLabel(FVector(2.66, 0.0, 2.55), TEXT("ОБЕДЫ · НОЧЛЕГ"), 8.f, FColor(60, 25, 10), FLinearColor(0.8f, 0.75f, 0.6f));

	// Окошко раздачи: чай в дорогу.
	AddBox(FVector(2.55, -5.0, 1.25), FVector(0.12, 1.2, 0.9), Board, true, PeregonService::ShopTag);
	AddLabel(FVector(2.63, -5.0, 1.85), TEXT("ЧАЙ С СОБОЙ"), 6.f, FColor(60, 25, 10), FLinearColor(0.8f, 0.75f, 0.6f));
	for (double Y : {-4.0, 4.0})
	{
		AddBox(FVector(6.0, Y, 0.75), FVector(1.3, 1.3, 0.08), TableLook, true);
		AddBox(FVector(6.0, Y, 0.37), FVector(0.12, 0.12, 0.74), TableLook, false);
		AddBox(FVector(6.0, Y - 1.1, 0.45), FVector(1.3, 0.35, 0.06), TableLook, false);
		AddBox(FVector(6.0, Y + 1.1, 0.45), FVector(1.3, 0.35, 0.06), TableLook, false);
	}
}

void APeregonServicePoint::BuildShop()
{
	const FPeregonLook Walls(EPeregonTex::Plaster, FLinearColor(0.75f, 0.78f, 0.62f), 0.9f);
	const FPeregonLook Roof(EPeregonTex::RoofMetal, FLinearColor(0.2f, 0.32f, 0.22f), 0.5f, 0.5f);
	const FPeregonLook Door(EPeregonTex::WoodPlanks, FLinearColor(0.4f, 0.28f, 0.16f), 0.9f);
	const FPeregonLook Glass(EPeregonTex::Glass, FLinearColor(1.1f, 1.05f, 0.85f), 0.1f);
	const FPeregonLook Board(EPeregonTex::Paint, FLinearColor(0.12f, 0.25f, 0.5f), 0.6f);

	AddBox(FVector(-2.0, 0.0, 1.5), FVector(6.0, 8.0, 3.0), Walls, true);
	AddBox(FVector(-2.0, 0.0, 3.15), FVector(6.6, 8.6, 0.3), Roof, false);
	AddBox(FVector(1.03, 1.8, 1.05), FVector(0.1, 1.2, 2.1), Door, true, PeregonService::ShopTag);
	AddBox(FVector(1.02, -1.8, 1.5), FVector(0.06, 2.4, 1.2), Glass, false);
	AddBox(FVector(1.1, 0.0, 2.6), FVector(0.1, 5.6, 0.6), Board, false);
	AddLabel(FVector(1.16, 0.0, 2.6), TEXT("ПРОДУКТЫ"), 22.f, FColor::White, FLinearColor(0.12f, 0.25f, 0.5f));
	AddLabel(FVector(1.1, -1.8, 0.65), TEXT("КАРТЫ · ЧАЙ · КАНИСТРЫ"), 5.f, FColor(30, 30, 30), FLinearColor(0.75f, 0.78f, 0.62f));
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
		break;
	case EPeregonService::Shop:
		AddIf(PeregonItems::MapForRegion(Region));
		AddIf(PeregonItems::MapForRegion(Region + 1));
		AddIf(PeregonItems::Thermos);
		AddIf(PeregonItems::CanisterEmpty);
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
	if (Part->ComponentHasTag(PeregonService::ShopTag))
	{
		return User && User->IsA<APeregonCharacter>() ? FString::Printf(TEXT("Зайти: %s"), *DisplayName) : FString();
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
