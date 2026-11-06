#include "Game/PeregonGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Game/PeregonHUD.h"
#include "Game/PeregonPlayerController.h"
#include "Game/PeregonSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Player/PeregonCharacter.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
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
}

void APeregonGameMode::ClearSession()
{
	if (APlayerController* PC = GetPlayer())
	{
		PC->UnPossess();
	}
	for (AActor* Actor : TArray<AActor*>{Car.Get(), Walker.Get(), RoadWorld.Get(), ProvingGround.Get(), MenuCamera.Get()})
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Car = nullptr;
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
	return FString::Printf(TEXT("%s · день %d, %02d:%02d · %.1f км"), *Save->PlaceName, Save->Day, Hours, Minutes, Save->RouteS / 1000.f);
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
	}
	StartSession(Save->Config, Save);
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

	Car = GetWorld()->SpawnActorDeferred<APeregonCar>(APeregonCar::StaticClass(), CarTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Car->PaintColor = Config.CarColor;
	Car->FinishSpawning(CarTransform);
	ApplyAssists(Config);
	if (Save)
	{
		Car->GetVehicleMovement()->ApplyCarState(Save->CarState);
	}

	// Игрок стоит у водительской двери, лицом по ходу машины.
	const FRotator Facing(0.f, CarTransform.Rotator().Yaw, 0.f);
	const FVector WalkerLocation = CarTransform.TransformPosition(FVector(20.f, -170.f, 40.f));
	Walker = GetWorld()->SpawnActor<APeregonCharacter>(APeregonCharacter::StaticClass(), WalkerLocation, Facing, Params);

	if (APeregonPlayerController* PC = Cast<APeregonPlayerController>(GetPlayer()))
	{
		PC->CloseMenu();
		PC->Possess(Walker);
		PC->SetControlRotation(Facing);
	}

	if (Mode == EPeregonMode::Road)
	{
		Toast(Save ? FString::Printf(TEXT("С возвращением! %s"), *Save->PlaceName)
				   : FString(TEXT("Новосибирск → Заречный, 15 км. Подойдите к водительской двери и нажмите E")), 7.f);
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
