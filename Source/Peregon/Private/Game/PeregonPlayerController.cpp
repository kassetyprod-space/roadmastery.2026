#include "Game/PeregonPlayerController.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/PeregonGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SPeregonMenu.h"
#include "UI/SPeregonPanels.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "World/PeregonRoadWorld.h"
#include "World/PeregonSky.h"
#include "EngineUtils.h"
#include "Widgets/SWeakWidget.h"

APeregonPlayerController::APeregonPlayerController()
{
	bShowMouseCursor = false;
}

void APeregonPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// Подстраховка: если режим вошёл в меню раньше, чем появился вьюпорт, — показываем меню сейчас.
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	if (IsLocalController() && GM && GM->GetMode() == EPeregonMode::Menu && !IsMenuOpen())
	{
		ShowMainMenu();
	}
}

void APeregonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// Пауза: Esc (в редакторе Esc останавливает Play — там F10). Tab — вещи, M — карта.
	for (const FKey& Key : {EKeys::Escape, EKeys::F10, EKeys::Gamepad_Special_Right})
	{
		FInputKeyBinding& Binding = InputComponent->BindKey(Key, IE_Pressed, this, &APeregonPlayerController::OnEscape);
		Binding.bExecuteWhenPaused = true;
	}
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &APeregonPlayerController::ToggleInventory);
	InputComponent->BindKey(EKeys::M, IE_Pressed, this, &APeregonPlayerController::ToggleMap);
	InputComponent->BindKey(EKeys::F6, IE_Pressed, this, &APeregonPlayerController::NextWeather);
	InputComponent->BindKey(EKeys::F7, IE_Pressed, this, &APeregonPlayerController::SkipDays);
}

void APeregonPlayerController::NextWeather()
{
	for (TActorIterator<APeregonSky> It(GetWorld()); It; ++It)
	{
		It->CycleWeather();
		if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
		{
			GM->Toast(FString::Printf(TEXT("Погода меняется… %s"), *It->DescribeWeather()), 2.5f);
		}
		break;
	}
}

void APeregonPlayerController::SkipDays()
{
	for (TActorIterator<APeregonSky> It(GetWorld()); It; ++It)
	{
		It->AdvanceDays(15);
		if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
		{
			GM->Toast(FString::Printf(TEXT("Дата: %s"), *It->DescribeDate()), 2.5f);
		}
		break;
	}
}

void APeregonPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	ClosePanel();
	CloseMenu();
	Super::EndPlay(Reason);
}

void APeregonPlayerController::OpenMenu(bool bPause)
{
	ClosePanel();
	CloseMenu();
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport)
	{
		return;
	}
	bPauseMenu = bPause;
	MenuWidget = SNew(SPeregonMenu).Owner(this).bPauseMenu(bPause);
	MenuContainer = SNew(SWeakWidget).PossiblyNullContent(MenuWidget.ToSharedRef());
	Viewport->AddViewportWidgetContent(MenuContainer.ToSharedRef(), 100);

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(MenuWidget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	if (bPause)
	{
		SetPause(true);
	}
}

void APeregonPlayerController::ShowMainMenu()
{
	OpenMenu(false);
}

void APeregonPlayerController::ShowPauseMenu()
{
	OpenMenu(true);
}

void APeregonPlayerController::CloseMenu()
{
	if (MenuContainer.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(MenuContainer.ToSharedRef());
		}
	}
	MenuContainer.Reset();
	MenuWidget.Reset();
	if (GetWorld() && !GetWorld()->bIsTearingDown)
	{
		SetPause(false);
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}
}

void APeregonPlayerController::TogglePause()
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	if (GM && GM->GetMode() == EPeregonMode::Menu)
	{
		return; // в главном меню назад ведёт само меню
	}
	if (IsMenuOpen())
	{
		UiResume();
	}
	else
	{
		ShowPauseMenu();
	}
}

void APeregonPlayerController::UiNewGame(const FPeregonRunConfig& Config)
{
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		GM->StartNewGame(Config);
	}
}

void APeregonPlayerController::UiContinue()
{
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		GM->ContinueGame();
	}
}

void APeregonPlayerController::UiResume()
{
	CloseMenu();
}

void APeregonPlayerController::UiSave()
{
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		GM->Toast(GM->SaveGame() ? TEXT("Игра сохранена") : TEXT("Сохранение недоступно"));
	}
}

void APeregonPlayerController::UiToMainMenu()
{
	CloseMenu();
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		GM->EnterMenu();
	}
}

void APeregonPlayerController::UiQuit()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

bool APeregonPlayerController::UiHasSave() const
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	return GM && GM->HasSave();
}

FString APeregonPlayerController::UiSaveDescription() const
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	return GM ? GM->DescribeSave() : FString();
}

bool APeregonPlayerController::UiCanSave() const
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	return GM && GM->GetMode() == EPeregonMode::Road;
}

// ---------------------------------------------------------------------------------------------
// Игровые окна
// ---------------------------------------------------------------------------------------------
void APeregonPlayerController::Toast(const FString& Text) const
{
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		GM->Toast(Text);
	}
}

void APeregonPlayerController::OnEscape()
{
	if (IsPanelOpen())
	{
		ClosePanel();
		return;
	}
	TogglePause();
}

void APeregonPlayerController::ShowPanel(TSharedRef<SWidget> Panel, TSharedPtr<SWidget> Focus)
{
	ClosePanel();
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport || IsMenuOpen())
	{
		return;
	}
	PanelWidget = Panel;
	PanelContainer = SNew(SWeakWidget).PossiblyNullContent(Panel);
	Viewport->AddViewportWidgetContent(PanelContainer.ToSharedRef(), 90);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Focus.IsValid() ? Focus : PanelWidget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	SetPause(true);
}

void APeregonPlayerController::ClosePanel()
{
	if (!PanelContainer.IsValid())
	{
		return;
	}
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(PanelContainer.ToSharedRef());
	}
	PanelContainer.Reset();
	PanelWidget.Reset();
	if (GetWorld() && !GetWorld()->bIsTearingDown && !IsMenuOpen())
	{
		SetPause(false);
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}
}

void APeregonPlayerController::OpenInventory(EPeregonStorage Storage)
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	if (!GM || GM->GetMode() == EPeregonMode::Menu || IsMenuOpen())
	{
		return;
	}
	TSharedRef<SPeregonInventoryPanel> Panel = SNew(SPeregonInventoryPanel).Owner(this).Storage(Storage);
	ShowPanel(Panel, Panel);
}

void APeregonPlayerController::OpenShop(const TArray<FName>& Items, const FString& Title, float FuelPrice)
{
	ShopFuelPrice = FuelPrice;
	TSharedRef<SPeregonInventoryPanel> Panel = SNew(SPeregonInventoryPanel).Owner(this).ShopItems(Items).ShopTitle(Title);
	ShowPanel(Panel, Panel);
}

void APeregonPlayerController::ToggleInventory()
{
	if (IsPanelOpen())
	{
		ClosePanel();
	}
	else
	{
		OpenInventory(EPeregonStorage::None);
	}
}

void APeregonPlayerController::ToggleMap()
{
	if (IsPanelOpen())
	{
		ClosePanel();
	}
	else
	{
		OpenMap();
	}
}

bool APeregonPlayerController::HasMapForRegion(int32 Region) const
{
	const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	const FName Id = PeregonItems::MapForRegion(Region);
	if (!GI || Id.IsNone())
	{
		return false;
	}
	const bool bInCar = GetPawn() && GetPawn()->IsA<APeregonCar>();
	return GI->Pockets.Has(Id) || (bInCar && GI->Glovebox.Has(Id));
}

void APeregonPlayerController::OpenMap()
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	const APeregonRoadWorld* World = GM ? GM->GetRoadWorld() : nullptr;
	if (!World || GM->GetMode() != EPeregonMode::Road || IsMenuOpen())
	{
		return;
	}
	const rgen::Route& Route = World->GetRoute();
	const int32 Here = Route.RegionAt(World->GetTrackedS());
	if (HasMapForRegion(Here))
	{
		OpenMapRegion(Here);
		return;
	}
	for (int32 R = 0; R < static_cast<int32>(Route.Regions().size()); ++R)
	{
		if (HasMapForRegion(R))
		{
			Toast(TEXT("Это карта соседней области — своей пока нет"));
			OpenMapRegion(R);
			return;
		}
	}
	const bool bInCar = GetPawn() && GetPawn()->IsA<APeregonCar>();
	Toast(bInCar ? TEXT("Карты нет ни в карманах, ни в бардачке. Купите на АЗС или в магазине")
				 : TEXT("Карты в карманах нет. Купите на АЗС или в магазине (или посмотрите в бардачке)"));
}

void APeregonPlayerController::OpenMapRegion(int32 Region)
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	const APeregonRoadWorld* World = GM ? GM->GetRoadWorld() : nullptr;
	const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	if (!World || !GI)
	{
		return;
	}
	TSharedRef<SPeregonMapPanel> Panel = SNew(SPeregonMapPanel)
		.Owner(this)
		.Route(&World->GetRoute())
		.Region(Region)
		.TraveledS(GI->TraveledMaxS)
		.CurrentS(static_cast<float>(World->GetTrackedS()));
	ShowPanel(Panel, Panel);
}

FPeregonContainer* APeregonPlayerController::GetContainer(EPeregonStorage Storage) const
{
	UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	if (!GI)
	{
		return nullptr;
	}
	return Storage == EPeregonStorage::None ? &GI->Pockets : GI->GetStorage(Storage);
}

bool APeregonPlayerController::CanMoveItem(FName Id, EPeregonStorage From, EPeregonStorage To) const
{
	const FPeregonContainer* A = GetContainer(From);
	const FPeregonContainer* B = GetContainer(To);
	return A && B && A != B && A->Has(Id) && B->CanAdd(Id, 1);
}

void APeregonPlayerController::MoveItem(FName Id, EPeregonStorage From, EPeregonStorage To)
{
	if (!CanMoveItem(Id, From, To))
	{
		return;
	}
	GetContainer(From)->Remove(Id, 1);
	GetContainer(To)->Add(Id, 1);
}

float APeregonPlayerController::GetMoney() const
{
	const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	return GI ? GI->Money : 0.f;
}

float APeregonPlayerController::GetShopPrice(FName Id) const
{
	const FPeregonItemDef* Def = PeregonItems::Find(Id);
	if (!Def)
	{
		return 0.f;
	}
	if (Id == PeregonItems::CanisterFull)
	{
		const FPeregonItemDef* Empty = PeregonItems::Find(PeregonItems::CanisterEmpty);
		return (Empty ? Empty->Price : 450.f) + PeregonItems::CanisterLiters * ShopFuelPrice;
	}
	return Def->Price;
}

bool APeregonPlayerController::CanBuyItem(FName Id) const
{
	const FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	return Pockets && Pockets->CanAdd(Id, 1) && GetMoney() + 0.01f >= GetShopPrice(Id);
}

void APeregonPlayerController::BuyItem(FName Id)
{
	UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	if (!GI || !Pockets)
	{
		return;
	}
	if (!Pockets->CanAdd(Id, 1))
	{
		Toast(TEXT("Карманы полны — оставьте что-нибудь в машине"));
		return;
	}
	if (!GI->TrySpend(GetShopPrice(Id)))
	{
		Toast(TEXT("Не хватает денег"));
		return;
	}
	Pockets->Add(Id, 1);
	Toast(FString::Printf(TEXT("Куплено: %s"), *PeregonItems::DisplayName(Id)));
}

void APeregonPlayerController::UseItem(FName Id, EPeregonStorage From)
{
	FPeregonContainer* C = GetContainer(From);
	const FPeregonItemDef* Def = PeregonItems::Find(Id);
	if (!C || !Def || !C->Has(Id))
	{
		return;
	}
	if (Def->Region >= 0)
	{
		ClosePanel();
		OpenMapRegion(Def->Region);
		return;
	}
	if (Id == PeregonItems::CanisterFull)
	{
		APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
		APeregonCar* Car = GM ? GM->GetCar() : nullptr;
		const APawn* Me = GetPawn();
		const bool bNear = Car && Me && (Me == Car || FVector::Dist(Me->GetActorLocation(), Car->GetActorLocation()) < 450.f);
		if (!bNear || !Car->GetVehicleMovement())
		{
			Toast(TEXT("Подойдите к машине, чтобы залить бензин"));
			return;
		}
		const FPeregonTelemetry T = Car->GetVehicleMovement()->GetTelemetry();
		const float Liters = FMath::Min(PeregonItems::CanisterLiters, T.FuelCapacityL - T.FuelL);
		if (Liters < 0.5f)
		{
			Toast(TEXT("Бак полон"));
			return;
		}
		Car->GetVehicleMovement()->Refuel(Liters);
		C->Remove(Id, 1);
		C->Add(PeregonItems::CanisterEmpty, 1);
		Toast(FString::Printf(TEXT("Залито из канистры %.0f л"), Liters));
		return;
	}
	if (Id == PeregonItems::Thermos)
	{
		C->Remove(Id, 1);
		Toast(TEXT("Горячий чай. Дорога кажется короче"));
		return;
	}
	Toast(Def->Description);
}
