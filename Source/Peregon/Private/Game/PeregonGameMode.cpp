#include "Game/PeregonGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/PeregonHUD.h"
#include "Game/PeregonPlayerController.h"
#include "Game/PeregonSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Player/PeregonCharacter.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "World/PeregonGarageYard.h"
#include "World/PeregonNightLights.h"
#include "World/PeregonTraffic.h"
#include "World/PeregonPickup.h"
#include "World/PeregonProvingGround.h"
#include "World/PeregonRoadWorld.h"
#include "World/PeregonSky.h"

namespace PeregonFlow
{
constexpr double StartS = 60.0;      // м от начала трассы
constexpr double CarT = 4.2;         // правая обочина
constexpr double MenuCameraSpeed = 9.0; // м/с
} // namespace PeregonFlow

APeregonGameMode::APeregonGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APeregonPlayerController::StaticClass();
	HUDClass = APeregonHUD::StaticClass();
}

APlayerController* APeregonGameMode::GetPlayer() const
{
	return GetWorld()->GetFirstPlayerController();
}

void APeregonGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Пешку не спавним: её создаёт режим (меню, дорога, полигон).
	if (bSkipMenu)
	{
		UPeregonGameInstance* GI = GetGameInstance<UPeregonGameInstance>();
		StartNewGame(GI ? GI->Config : FPeregonRunConfig());
	}
	else
	{
		EnterMenu();
	}
}

void APeregonGameMode::RestartPlayer(AController* /*NewPlayer*/)
{
	// Пешки создаются в StartSession.
}

void APeregonGameMode::EnsureSky()
{
	if (!Sky)
	{
		Sky = GetWorld()->SpawnActor<APeregonSky>();
	}
	if (!NightLights)
	{
		NightLights = GetWorld()->SpawnActor<APeregonNightLights>();
	}
}

void APeregonGameMode::ClearSession()
{
	if (APlayerController* PC = GetPlayer())
	{
		PC->UnPossess();
	}
	TArray<AActor*> Doomed{Car.Get(), Walker.Get(), RoadWorld.Get(), ProvingGround.Get(), MenuCamera.Get(), GarageYard.Get(), Traffic.Get()};
	for (APeregonCar* Other : Cars)
	{
		Doomed.AddUnique(Other);
	}
	// Вещи, разбросанные по миру (хлам на помойке, то, что игрок положил на землю).
	for (TActorIterator<APeregonPickup> It(GetWorld()); It; ++It)
	{
		Doomed.Add(*It);
	}
	for (AActor* Actor : Doomed)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Car = nullptr;
	Cars.Reset();
	GarageYard = nullptr;
	Traffic = nullptr;
	Walker = nullptr;
	RoadWorld = nullptr;
	ProvingGround = nullptr;
	MenuCamera = nullptr;
	Toasts.Reset();
	UGameplayStatics::SetGamePaused(this, false);
}

void APeregonGameMode::EnterMenu()
{
	ClearSession();
	EnsureSky();
	Mode = EPeregonMode::Menu;

	UPeregonGameInstance* GI = GetGameInstance<UPeregonGameInstance>();
	if (GI)
	{
		GI->TimeOfDayHours = 18.3f; // вечернее солнце на фоне меню
	}
	Sky->bFreezeTime = true;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RoadWorld = GetWorld()->SpawnActor<APeregonRoadWorld>(APeregonRoadWorld::StaticClass(), FTransform::Identity, Params);
	RoadWorld->bAnnouncePlaces = false;
	RoadWorld->Generate(GI ? GI->Config.Seed : 2026);
	MenuCameraS = 300.0;
	RoadWorld->BuildAround(MenuCameraS);

	MenuCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
	if (MenuCamera && MenuCamera->GetCameraComponent())
	{
		MenuCamera->GetCameraComponent()->SetFieldOfView(70.f);
		MenuCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	Tick(0.f);

	if (APeregonPlayerController* PC = Cast<APeregonPlayerController>(GetPlayer()))
	{
		PC->SetViewTarget(MenuCamera);
		PC->ShowMainMenu();
	}
}

void APeregonGameMode::StartNewGame(const FPeregonRunConfig& Config)
{
	if (UPeregonGameInstance* GI = GetGameInstance<UPeregonGameInstance>())
	{
		GI->Config = Config;
		GI->Money = Config.StartMoney;
		GI->TimeOfDayHours = Config.StartHour;
		GI->Day = 1;
		GI->ResetInventory();
		GI->ResetClimate(Config.StartDayOfYear, Config.Seed);
	}
	StartSession(Config, nullptr);
}

bool APeregonGameMode::HasSave() const
{
	return UGameplayStatics::DoesSaveGameExist(UPeregonSaveGame::SlotName, 0);
}

FString APeregonGameMode::DescribeSave() const
{
	const UPeregonSaveGame* Save = HasSave() ? Cast<UPeregonSaveGame>(UGameplayStatics::LoadGameFromSlot(UPeregonSaveGame::SlotName, 0)) : nullptr;
	if (!Save)
	{
		return FString();
	}
	const int32 Hours = FMath::FloorToInt(Save->TimeOfDayHours);
	const int32 Minutes = FMath::FloorToInt((Save->TimeOfDayHours - Hours) * 60.f);
	return FString::Printf(TEXT("%s · день %d, %02d:%02d · %.0f км по Р-255"), *Save->PlaceName, Save->Day, Hours, Minutes, Save->RouteS * 0.005f);
}

bool APeregonGameMode::ContinueGame()
{
	UPeregonSaveGame* Save = HasSave() ? Cast<UPeregonSaveGame>(UGameplayStatics::LoadGameFromSlot(UPeregonSaveGame::SlotName, 0)) : nullptr;
	if (!Save)
	{
		return false;
	}
	if (UPeregonGameInstance* GI = GetGameInstance<UPeregonGameInstance>())
	{
		GI->Config = Save->Config;
		GI->Money = Save->Money;
		GI->TimeOfDayHours = Save->TimeOfDayHours;
		GI->Day = Save->Day;
		if (Save->Version >= 3)
		{
			GI->Climate = Save->Climate;
		}
		else
		{
			GI->ResetClimate(Save->Config.StartDayOfYear + Save->Day - 1, Save->Config.Seed);
		}
		if (Save->Version >= 2)
		{
			GI->Pockets = Save->Pockets; // бардачок и багажник уходят в машину в StartSession
			GI->Glovebox = Save->Glovebox;
			GI->Trunk = Save->Trunk;
			GI->TraveledMaxS = Save->TraveledMaxS;
			if (Save->Version >= 5)
			{
				GI->Journal = Save->Journal;
			}
		}
		else
		{
			GI->ResetInventory();
		}
	}
	// Сохранения до версии 2 — со старой тестовой трассы: машина начинает заново в Новосибирске.
	StartSession(Save->Config, Save->Version >= 2 ? Save : nullptr);
	if (Save->Version < 2 && Car)
	{
		Car->GetVehicleMovement()->ApplyCarState(Save->CarState);
	}
	return true;
}

bool APeregonGameMode::SaveGame(const FString& PlaceName)
{
	UPeregonGameInstance* GI = GetGameInstance<UPeregonGameInstance>();
	if (Mode != EPeregonMode::Road || !Car || !RoadWorld || !GI)
	{
		return false;
	}
	UPeregonSaveGame* Save = Cast<UPeregonSaveGame>(UGameplayStatics::CreateSaveGameObject(UPeregonSaveGame::StaticClass()));
	if (!Save)
	{
		return false;
	}
	Save->Config = GI->Config;
	Save->CarTransform = Car->GetActorTransform();
	Save->CarState = Car->GetVehicleMovement()->GetCarState();
	Save->Money = GI->Money;
	Save->TimeOfDayHours = GI->TimeOfDayHours;
	Save->Day = GI->Day;
	Save->RouteS = static_cast<float>(RoadWorld->FindS(Car->GetActorLocation(), RoadWorld->GetTrackedS()));
	Save->SavedAt = FDateTime::Now();
	Save->PlaceName = PlaceName.IsEmpty() ? RoadWorld->GetPlaceName(Save->RouteS) : PlaceName;
	Save->Pockets = GI->Pockets;
	Save->Glovebox = Car->GloveStore;
	Save->Trunk = Car->TrunkStore;
	Save->CarColor = Car->PaintColor;
	const std::vector<double> Packed = vsim::parts::Pack(Car->GetParts());
	Save->Parts.Append(Packed.data(), static_cast<int32>(Packed.size()));
	Save->Journal = GI->Journal;
	for (APeregonCar* Other : Cars)
	{
		if (!IsValid(Other) || Other == Car)
		{
			continue;
		}
		FPeregonSavedCar& Saved = Save->Convoy.AddDefaulted_GetRef();
		Saved.Transform = Other->GetActorTransform();
		Saved.CarState = Other->GetVehicleMovement()->GetCarState();
		const std::vector<double> OtherParts = vsim::parts::Pack(Other->GetParts());
		Saved.Parts.Append(OtherParts.data(), static_cast<int32>(OtherParts.size()));
		Saved.Color = Other->PaintColor;
		Saved.Glovebox = Other->GloveStore;
		Saved.Trunk = Other->TrunkStore;
	}
	Save->TraveledMaxS = GI->TraveledMaxS;
	Save->Climate = GI->Climate;
	return UGameplayStatics::SaveGameToSlot(Save, UPeregonSaveGame::SlotName, 0);
}

void APeregonGameMode::ApplyAssists(const FPeregonRunConfig& Config)
{
	if (!Car || !Car->GetVehicleMovement())
	{
		return;
	}
	UPeregonVehicleMovementComponent* Move = Car->GetVehicleMovement();
	Move->bAutoGearbox = Config.Assists == EPeregonAssists::Casual;
	Move->bAutoClutch = Config.Assists != EPeregonAssists::Hardcore;
	Move->bAbs = Config.Assists == EPeregonAssists::Casual;
}

void APeregonGameMode::StartSession(const FPeregonRunConfig& Config, const UPeregonSaveGame* Save)
{
	ClearSession();
	EnsureSky();
	Sky->bFreezeTime = false;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FTransform CarTransform;
	if (Config.bProvingGround)
	{
		Mode = EPeregonMode::ProvingGround;
		ProvingGround = GetWorld()->SpawnActorDeferred<APeregonProvingGround>(APeregonProvingGround::StaticClass(), FTransform::Identity);
		ProvingGround->bSpawnEnvironment = false;
		ProvingGround->FinishSpawning(FTransform::Identity);
		const FTransform Start = ProvingGround->GetSpawnTransform();
		CarTransform = FTransform(Start.Rotator(), Start.GetLocation() + Start.Rotator().RotateVector(FVector(0.f, 350.f, 0.f)) + FVector(0, 0, 20));
	}
	else
	{
		Mode = EPeregonMode::Road;
		RoadWorld = GetWorld()->SpawnActor<APeregonRoadWorld>(APeregonRoadWorld::StaticClass(), FTransform::Identity, Params);
		RoadWorld->Generate(Config.Seed);
		const double S = Save ? static_cast<double>(Save->RouteS) : PeregonFlow::StartS;
		RoadWorld->BuildAround(S);
		CarTransform = Save ? Save->CarTransform : RoadWorld->RoadTransform(S, PeregonFlow::CarT, 0.75);
		if (Save)
		{
			CarTransform.AddToTranslation(FVector(0, 0, 40)); // уронить на колёса, а не в землю
		}
	}

	if (Mode == EPeregonMode::Road)
	{
		APeregonTraffic* Flow = GetWorld()->SpawnActor<APeregonTraffic>(APeregonTraffic::StaticClass(), FTransform::Identity, Params);
		if (Flow)
		{
			Flow->RoadWorld = RoadWorld;
			Traffic = Flow;
		}
	}

	// Сюжетное начало: двор с гаражами, машина без половины деталей.
	const bool bGarage = Mode == EPeregonMode::Road && !Save && Config.bGarageStart;
	APeregonGarageYard* Yard = nullptr;
	if (bGarage)
	{
		Yard = GetWorld()->SpawnActor<APeregonGarageYard>(APeregonGarageYard::StaticClass(), FTransform::Identity, Params);
		Yard->Build(RoadWorld, Config.Seed);
		GarageYard = Yard;
		CarTransform = Yard->GetCarSpot();
	}

	vsim::parts::CarState Parts;
	const vsim::parts::CarState* PartsPtr = nullptr;
	FLinearColor Color = Config.CarColor;
	if (bGarage)
	{
		Parts = vsim::parts::MakeGarageWreck(static_cast<uint32>(Config.Seed));
		const float Faded[3] = {Color.R * 0.85f, Color.G * 0.85f, Color.B * 0.85f};
		for (vsim::parts::PanelState& P : Parts.Panels)
		{
			FMemory::Memcpy(P.Color, Faded, sizeof(Faded));
		}
		PartsPtr = &Parts;
	}
	else if (Save && Save->Version >= 4)
	{
		const std::vector<double> Packed(Save->Parts.GetData(), Save->Parts.GetData() + Save->Parts.Num());
		if (vsim::parts::Unpack(Packed, Parts))
		{
			PartsPtr = &Parts;
		}
		Color = Save->CarColor;
	}
	Car = SpawnCar(CarTransform, Color, PartsPtr);
	ApplyAssists(Config);
	if (UPeregonGameInstance* GI = GetGameInstance<UPeregonGameInstance>())
	{
		Car->GloveStore = GI->Glovebox;
		Car->TrunkStore = GI->Trunk;
	}
	if (Save)
	{
		Car->GetVehicleMovement()->ApplyCarState(Save->CarState);
		// Остальная колонна — где её оставили.
		for (const FPeregonSavedCar& Saved : Save->Convoy)
		{
			vsim::parts::CarState OtherParts;
			const std::vector<double> Data(Saved.Parts.GetData(), Saved.Parts.GetData() + Saved.Parts.Num());
			const bool bParts = vsim::parts::Unpack(Data, OtherParts);
			FTransform Where = Saved.Transform;
			Where.AddToTranslation(FVector(0, 0, 40));
			if (APeregonCar* Other = SpawnCar(Where, Saved.Color, bParts ? &OtherParts : nullptr))
			{
				Other->GetVehicleMovement()->ApplyCarState(Saved.CarState);
				Other->GloveStore = Saved.Glovebox;
				Other->TrunkStore = Saved.Trunk;
				Other->GetVehicleMovement()->SetParkingBrake(true);
			}
		}
	}
	else if (bGarage)
	{
		FPeregonCarState Tank = Car->GetVehicleMovement()->GetCarState();
		Tank.FuelL = 6.f; // на донышке — до первой АЗС хватит
		Car->GetVehicleMovement()->ApplyCarState(Tank);
		if (UPeregonGameInstance* TutorialGI = GetGameInstance<UPeregonGameInstance>())
		{
			TutorialGI->Journal.bTutorial = true; // задачи «первой машины» на экране (F1)
		}
		// Брошенный «Сокол» на разборке: всё на месте, но всё уставшее. Донор или вторая машина колонны.
		const vsim::parts::CarState Donor = vsim::parts::MakeAbandoned(static_cast<uint32>(Config.Seed) * 31u + 7u, 0.32);
		if (APeregonCar* Second = SpawnCar(Yard->GetDonorSpot(), FLinearColor(0.45f, 0.38f, 0.22f), &Donor))
		{
			FPeregonCarState Dry = Second->GetVehicleMovement()->GetCarState();
			Dry.FuelL = 2.f;
			Second->GetVehicleMovement()->ApplyCarState(Dry);
			Second->GetVehicleMovement()->SetParkingBrake(true);
		}
	}

	// Игрок стоит у водительской двери, лицом по ходу машины (или выходит из подъезда пятиэтажки).
	FRotator Facing(0.f, CarTransform.Rotator().Yaw, 0.f);
	FVector WalkerLocation = CarTransform.TransformPosition(FVector(20.f, -170.f, 40.f));
	if (Yard)
	{
		const FTransform Door = Yard->GetSpawnSpot(0);
		Facing = FRotator(0.f, Door.Rotator().Yaw, 0.f);
		WalkerLocation = Door.GetLocation() + FVector(0.f, 0.f, 60.f);
	}
	Walker = GetWorld()->SpawnActor<APeregonCharacter>(APeregonCharacter::StaticClass(), WalkerLocation, Facing, Params);

	if (APeregonPlayerController* PC = Cast<APeregonPlayerController>(GetPlayer()))
	{
		PC->CloseMenu();
		PC->Possess(Walker);
		PC->SetControlRotation(Facing);
	}

	if (Mode == EPeregonMode::Road)
	{
		if (bGarage)
		{
			Toast(TEXT("Утро. «Сокол» ждёт в гаражном кооперативе — но без аккумулятора, свечей, карбюратора и фильтра."), 9.f);
			Toast(TEXT("Инструмент — на верстаке у гаража. Хлам — на помойке у дороги, б/у узлы — на разборке за забором."), 9.f);
			Toast(TEXT("1–0 — взять вещь в руки, E (держать) — работать, G — положить. Капот открывается E."), 9.f);
		}
		else
		{
			Toast(Save ? FString::Printf(TEXT("С возвращением! %s"), *Save->PlaceName)
					   : FString(TEXT("Новосибирск → Заречный, 15 км. Подойдите к водительской двери и нажмите E")), 7.f);
		}
	}
	else
	{
		Toast(TEXT("Испытательный полигон"), 4.f);
	}
}

void APeregonGameMode::Toast(const FString& Text, float Seconds)
{
	FPeregonToast Item;
	Item.Text = Text;
	Item.Duration = Seconds;
	Item.Until = GetWorld()->GetRealTimeSeconds() + Seconds;
	Toasts.Add(Item);
	if (Toasts.Num() > 4)
	{
		Toasts.RemoveAt(0);
	}
}

void APeregonGameMode::RecoverFallen()
{
	// Порог: на 20 м ниже самой низкой точки рельефа трассы (или −20 м на полигоне).
	const double KillZ = (Mode == EPeregonMode::Road && RoadWorld) ? (RoadWorld->GetRoute().MinZ() - 20.0) * 100.0 : -2000.0;
	auto SafeTransform = [this](const FVector& Where) -> FTransform
	{
		if (Mode == EPeregonMode::Road && RoadWorld)
		{
			const double S = RoadWorld->GetRoute().FindNearestS(Where.X / 100.0, Where.Y / 100.0, -1.0, 1.0e9);
			RoadWorld->BuildAround(S);
			return RoadWorld->RoadTransform(S, 2.0, 1.0);
		}
		return FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 150.0));
	};
	if (Car && Car->GetActorLocation().Z < KillZ)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Peregon] Машина провалилась (Z=%.0f см) — возвращаю на дорогу"), Car->GetActorLocation().Z);
		Car->PlaceOnGround(SafeTransform(Car->GetActorLocation()));
		Toast(TEXT("Машина провалилась под землю — вернули на дорогу"));
	}
	if (Walker && !Walker->IsHidden() && Walker->GetActorLocation().Z < KillZ)
	{
		const FTransform T = SafeTransform(Walker->GetActorLocation());
		Walker->TeleportTo(T.GetLocation() + FVector(0.0, 0.0, 100.0), T.Rotator());
		Toast(TEXT("Вы провалились под землю — вернули на дорогу"));
	}
}

void APeregonGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetRealTimeSeconds();
	Toasts.RemoveAll([Now](const FPeregonToast& T) { return T.Until < Now; });
	if (Mode == EPeregonMode::Road || Mode == EPeregonMode::ProvingGround)
	{
		RecoverFallen();
	}
	if (Mode == EPeregonMode::Road && RoadWorld)
	{
		if (UPeregonGameInstance* GI = GetGameInstance<UPeregonGameInstance>())
		{
			GI->TraveledMaxS = FMath::Max(GI->TraveledMaxS, static_cast<float>(RoadWorld->GetTrackedS()));
		}
	}

	// Облёт трассы на фоне главного меню.
	if (Mode == EPeregonMode::Menu && MenuCamera && RoadWorld)
	{
		MenuCameraS += DeltaSeconds * PeregonFlow::MenuCameraSpeed;
		if (MenuCameraS > RoadWorld->GetRoute().Length() - 600.0)
		{
			MenuCameraS = 300.0;
			RoadWorld->BuildAround(MenuCameraS);
		}
		const FVector Eye = RoadWorld->RouteToWorld(MenuCameraS, -9.0, 4.5);
		const FVector Target = RoadWorld->RouteToWorld(MenuCameraS + 45.0, 2.0, 1.5);
		MenuCamera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
	}
}

APeregonCar* APeregonGameMode::SpawnCar(const FTransform& Where, const FLinearColor& Color, const vsim::parts::CarState* Parts)
{
	APeregonCar* NewCar = GetWorld()->SpawnActorDeferred<APeregonCar>(APeregonCar::StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!NewCar)
	{
		return nullptr;
	}
	NewCar->PaintColor = Color;
	if (Parts)
	{
		NewCar->SetParts(*Parts);
	}
	NewCar->GloveStore.Capacity = 4;
	NewCar->TrunkStore.Capacity = 12;
	NewCar->FinishSpawning(Where);
	Cars.Add(NewCar);
	return NewCar;
}

void APeregonGameMode::SetActiveCar(APeregonCar* InCar)
{
	if (InCar && InCar != Car)
	{
		Car = InCar;
		Cars.AddUnique(InCar);
		ApplyAssists(GetGameInstance<UPeregonGameInstance>() ? GetGameInstance<UPeregonGameInstance>()->Config : FPeregonRunConfig());
	}
}

APeregonCar* APeregonGameMode::NearestCar(const FVector& Where, float MaxDistCm) const
{
	APeregonCar* Best = nullptr;
	float BestDist = MaxDistCm;
	for (APeregonCar* Other : Cars)
	{
		if (!IsValid(Other))
		{
			continue;
		}
		const float D = FVector::Dist(Other->GetActorLocation(), Where);
		if (D < BestDist)
		{
			BestDist = D;
			Best = Other;
		}
	}
	return Best;
}
