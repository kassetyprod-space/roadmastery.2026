#include "Game/PeregonPlayerController.h"
#include "rgen/Route.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Audio/PeregonAudio.h"
#include "Game/PeregonHUD.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/PeregonGameMode.h"
#include "Game/PeregonGameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SPeregonMenu.h"
#include "UI/SPeregonPanels.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "World/PeregonRoadWorld.h"
#include "World/PeregonPickup.h"
#include "vsim/CarParts.h"
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
	UPeregonGameUserSettings::Get()->ApplyRuntime();
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
	InputComponent->BindKey(EKeys::F8, IE_Pressed, this, &APeregonPlayerController::SkipHour);
	// B — журнал, F9 — фото, F1 — задачи обучения. Клавиши не «съедаются»: машина и пешеход их тоже видят.
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &APeregonPlayerController::OpenJournal).bConsumeInput = false;
	InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &APeregonPlayerController::TakePhoto).bConsumeInput = false;
	InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &APeregonPlayerController::ToggleTasks).bConsumeInput = false;
	// Что в руках: 1–0 — ячейка карманов, G — положить на землю (в машине G — коробка передач, её не трогаем).
	for (const FKey& Key : {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine,
			 EKeys::Zero, EKeys::G})
	{
		FInputKeyBinding& Binding = InputComponent->BindKey(Key, IE_Pressed, this, &APeregonPlayerController::OnHotkey);
		Binding.bConsumeInput = false;
	}
}

void APeregonPlayerController::OnHotkey(FKey Key)
{
	if (IsMenuOpen() || IsPanelOpen())
	{
		return;
	}
	if (Key == EKeys::G)
	{
		if (!(GetPawn() && GetPawn()->IsA<APeregonCar>()))
		{
			DropHeld();
		}
		return;
	}
	static const FKey Digits[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight,
		EKeys::Nine, EKeys::Zero};
	for (int32 I = 0; I < 10; ++I)
	{
		if (Key == Digits[I])
		{
			SelectHeld(I);
			return;
		}
	}
}

void APeregonPlayerController::SelectHeld(int32 PocketIndex)
{
	const FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	if (!Pockets)
	{
		return;
	}
	if (PocketIndex == GetHeldIndex() || !Pockets->Slots.IsValidIndex(PocketIndex))
	{
		HeldId = NAME_None; // та же клавиша ещё раз — руки свободны
		if (!Pockets->Slots.IsValidIndex(PocketIndex) && PocketIndex >= 0)
		{
			Toast(FString::Printf(TEXT("Ячейка %d пуста — руки свободны"), PocketIndex + 1));
		}
		return;
	}
	const FPeregonItemStack& S = Pockets->Slots[PocketIndex];
	HeldId = S.Id;
	HeldCondition = S.Condition;
	Toast(FString::Printf(TEXT("В руках: %s"), *PeregonItems::StackLabel(S)));
}

int32 APeregonPlayerController::GetHeldIndex() const
{
	const FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	if (!Pockets || HeldId.IsNone())
	{
		return INDEX_NONE;
	}
	// Ячейки сдвигаются, когда что-то берут или кладут, — ищем «ту же» вещь: тот же предмет, ближайшее состояние.
	int32 Found = INDEX_NONE;
	float BestDiff = 2.f;
	for (int32 I = 0; I < Pockets->Slots.Num(); ++I)
	{
		const FPeregonItemStack& S = Pockets->Slots[I];
		const float Diff = FMath::Abs(S.Condition - HeldCondition);
		if (S.Id == HeldId && Diff < BestDiff)
		{
			BestDiff = Diff;
			Found = I;
		}
	}
	return Found;
}

const FPeregonItemStack* APeregonPlayerController::GetHeld() const
{
	const FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	const int32 Index = GetHeldIndex();
	return Pockets && Pockets->Slots.IsValidIndex(Index) ? &Pockets->Slots[Index] : nullptr;
}

bool APeregonPlayerController::TakeHeld(FPeregonItemStack& Out)
{
	FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	const int32 Index = GetHeldIndex();
	if (!Pockets || !Pockets->TakeAt(Index, Out))
	{
		return false;
	}
	if (GetHeldIndex() == INDEX_NONE)
	{
		HeldId = NAME_None;
	}
	return true;
}

void APeregonPlayerController::UseHeld(float Amount)
{
	FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	const int32 Index = GetHeldIndex();
	if (!Pockets || !Pockets->Slots.IsValidIndex(Index))
	{
		return;
	}
	FPeregonItemStack& S = Pockets->Slots[Index];
	S.Condition = FMath::Max(0.f, S.Condition - Amount);
	HeldCondition = S.Condition;
	if (S.Condition <= 0.02f)
	{
		Toast(FString::Printf(TEXT("%s — закончилось"), *PeregonItems::DisplayName(S.Id)));
		Pockets->Slots.RemoveAt(Index);
		HeldId = NAME_None;
	}
}

bool APeregonPlayerController::HasTool(int32 Tool) const
{
	if (Tool <= 0)
	{
		return true;
	}
	const FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	return Pockets && Pockets->Has(PeregonItems::ToolItem(Tool));
}

bool APeregonPlayerController::HasConsumable(int32 Consumable) const
{
	const FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	const FName Id = PeregonItems::ConsumableItem(Consumable);
	const int32 Index = Pockets ? Pockets->Best(Id) : INDEX_NONE;
	return Index != INDEX_NONE && Pockets->Slots[Index].Condition > 0.02f;
}

bool APeregonPlayerController::UseConsumable(int32 Consumable, float Amount)
{
	FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	const FName Id = PeregonItems::ConsumableItem(Consumable);
	const int32 Index = Pockets ? Pockets->Best(Id) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	FPeregonItemStack& S = Pockets->Slots[Index];
	S.Condition = FMath::Max(0.f, S.Condition - Amount);
	if (S.Condition <= 0.02f)
	{
		Toast(FString::Printf(TEXT("%s — закончились"), *PeregonItems::DisplayName(Id)));
		Pockets->Slots.RemoveAt(Index);
	}
	return true;
}

void APeregonPlayerController::GiveOrDrop(const FPeregonItemStack& Stack)
{
	FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	if (Pockets && Pockets->AddStack(Stack))
	{
		Toast(FString::Printf(TEXT("В карманах: %s"), *PeregonItems::StackLabel(Stack)));
		return;
	}
	if (DropOnGround(Stack))
	{
		Toast(FString::Printf(TEXT("Карманы полны — %s на земле"), *PeregonItems::StackLabel(Stack)));
	}
}

bool APeregonPlayerController::DropOnGround(const FPeregonItemStack& Stack)
{
	const APawn* Me = GetPawn();
	if (!Me)
	{
		return false;
	}
	const FRotator Facing(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Ahead = Me->GetActorLocation() + Facing.Vector() * 70.f;
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PeregonDrop), false, Me);
	FVector Ground = Ahead - FVector(0.f, 0.f, 90.f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Ahead + FVector(0.f, 0.f, 50.f), Ahead - FVector(0.f, 0.f, 400.f), ECC_Visibility, Query))
	{
		Ground = Hit.ImpactPoint;
	}
	return APeregonPickup::Place(GetWorld(), Stack, FTransform(Facing, Ground)) != nullptr;
}

void APeregonPlayerController::DropHeld()
{
	const FPeregonItemStack* Held = GetHeld();
	if (!Held)
	{
		Toast(TEXT("В руках ничего нет (1–0 — взять вещь из карманов)"));
		return;
	}
	const FPeregonItemDef* Def = PeregonItems::Find(Held->Id);
	if (Def && Def->bQuest)
	{
		Toast(TEXT("Документы лучше не бросать"));
		return;
	}
	FPeregonItemStack Item;
	if (TakeHeld(Item) && !DropOnGround(Item))
	{
		GetContainer(EPeregonStorage::None)->AddStack(Item);
	}
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

void APeregonPlayerController::SkipHour()
{
	UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	if (!GI)
	{
		return;
	}
	GI->TimeOfDayHours += 1.f;
	if (GI->TimeOfDayHours >= 24.f)
	{
		GI->TimeOfDayHours -= 24.f;
		GI->Day += 1;
		GI->Climate.DayOfYear = GI->Climate.DayOfYear >= 365.f ? 1.f : GI->Climate.DayOfYear + 1.f;
	}
	const int32 Hours = FMath::FloorToInt(GI->TimeOfDayHours);
	const int32 Minutes = FMath::FloorToInt((GI->TimeOfDayHours - Hours) * 60.f);
	Toast(FString::Printf(TEXT("Время: %02d:%02d"), Hours, Minutes));
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

void APeregonPlayerController::OpenInventory(EPeregonStorage Storage, APeregonCar* FromCar)
{
	StorageCar = FromCar;
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
	const APeregonCar* Car = Cast<APeregonCar>(GetPawn());
	return GI->Pockets.Has(Id) || (bInCar && Car && Car->GloveStore.Has(Id));
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
	if (Storage == EPeregonStorage::None)
	{
		return &GI->Pockets;
	}
	// Бардачок и багажник — у той машины, которую открыли (или в которой сидим, или «своей»).
	APeregonCar* Car = StorageCar.Get();
	if (!Car)
	{
		Car = Cast<APeregonCar>(GetPawn());
	}
	if (!Car)
	{
		const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
		Car = GM ? GM->GetCar() : nullptr;
	}
	return Car ? Car->GetStorage(Storage) : GI->GetStorage(Storage);
}

bool APeregonPlayerController::CanMoveSlot(int32 Index, EPeregonStorage From, EPeregonStorage To) const
{
	const FPeregonContainer* A = GetContainer(From);
	const FPeregonContainer* B = GetContainer(To);
	if (!A || !B || A == B || !A->Slots.IsValidIndex(Index))
	{
		return false;
	}
	FPeregonItemStack One = A->Slots[Index];
	One.Count = 1;
	return B->CanAddStack(One);
}

void APeregonPlayerController::MoveSlot(int32 Index, EPeregonStorage From, EPeregonStorage To)
{
	if (!CanMoveSlot(Index, From, To))
	{
		return;
	}
	FPeregonItemStack One;
	if (GetContainer(From)->TakeAt(Index, One))
	{
		GetContainer(To)->AddStack(One);
	}
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
	MoveSlot(GetContainer(From)->Best(Id), From, To);
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

bool APeregonPlayerController::BuyItemAt(FName Id, float Price)
{
	UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	FPeregonContainer* Pockets = GetContainer(EPeregonStorage::None);
	if (!GI || !Pockets)
	{
		return false;
	}
	if (!Pockets->CanAdd(Id, 1))
	{
		Toast(TEXT("Карманы полны — оставьте что-нибудь в машине"));
		return false;
	}
	if (!GI->TrySpend(Price))
	{
		Toast(TEXT("Не хватает денег"));
		return false;
	}
	Pockets->Add(Id, 1);
	Toast(FString::Printf(TEXT("Взято: %s"), *PeregonItems::DisplayName(Id)));
	return true;
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
		const APawn* Me = GetPawn();
		APeregonCar* Car = Cast<APeregonCar>(GetPawn());
		if (!Car && GM && Me)
		{
			Car = GM->NearestCar(Me->GetActorLocation(), 450.f);
		}
		const bool bNear = Car != nullptr;
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
	if (Id == PeregonItems::Manual)
	{
		ClosePanel();
		OpenManual();
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

void APeregonPlayerController::OpenManual()
{
	APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	APeregonCar* Car = Cast<APeregonCar>(GetPawn());
	if (!Car && GM && GetPawn())
	{
		Car = GM->NearestCar(GetPawn()->GetActorLocation(), 800.f);
	}
	TArray<FPeregonManualLine> Lines;
	auto Line = [&Lines](const FString& Text, int32 Style) { Lines.Add({Text, Style}); };
	if (!Car)
	{
		Line(TEXT("Машины рядом нет — подойдите к «Соколу» и откройте техничку ещё раз."), 3);
	}
	else
	{
		const vsim::parts::CarState& State = Car->GetParts();
		const float Fuel = Car->GetVehicleMovement() ? Car->GetVehicleMovement()->GetTelemetry().FuelL : 0.f;
		const std::vector<vsim::parts::Finding> Found = vsim::parts::Diagnose(State, Fuel);
		Line(TEXT("Чего не хватает, чтобы завестись и ехать"), 1);
		bool bAny = false;
		for (int32 Pass = 2; Pass >= 1; --Pass)
		{
			for (const vsim::parts::Finding& F : Found)
			{
				if (F.Severity == Pass)
				{
					Line(UTF8_TO_TCHAR(F.Text.c_str()), Pass == 2 ? 2 : 3);
					bAny = true;
				}
			}
		}
		if (!bAny)
		{
			Line(TEXT("Всё на месте — заводите: ключ (держать ЛКМ) или R. Не забудьте опустить ручник."), 4);
		}
		Line(TEXT("Узлы"), 1);
		for (int32 I = 0; I < vsim::parts::SlotCount; ++I)
		{
			const vsim::parts::Slot S = static_cast<vsim::parts::Slot>(I);
			const vsim::parts::SlotInfo& Info = vsim::parts::Info(S);
			const vsim::parts::PartItem* P = State.Get(S);
			const FString Name = UTF8_TO_TCHAR(Info.Name);
			if (!P)
			{
				Line(FString::Printf(TEXT("%s — нет"), *Name), Info.bOptional ? 0 : 3);
				continue;
			}
			const int32 Pct = FMath::RoundToInt(static_cast<float>(P->Condition) * 100.f);
			Line(FString::Printf(TEXT("%s — %d%%"), *Name, Pct), Pct < 25 ? 3 : (Pct > 70 ? 4 : 0));
		}
		Line(FString::Printf(TEXT("Масло %.1f / 3,75 л · антифриз %.1f / 7,8 л · заряд аккумулятора %d%%"), State.OilL, State.CoolantL,
				 FMath::RoundToInt(static_cast<float>(State.BatteryCharge) * 100.f)),
			0);
		Line(TEXT("Кузов"), 1);
		for (int32 I = 0; I < vsim::parts::PanelCount; ++I)
		{
			const vsim::parts::PanelState& P = State.Panels[static_cast<size_t>(I)];
			const int32 Score = FMath::RoundToInt(static_cast<float>(vsim::parts::PanelScore(P)) * 100.f);
			FString Note = FString::Printf(TEXT("%s — %d%%"), UTF8_TO_TCHAR(vsim::parts::PanelName(static_cast<vsim::parts::Panel>(I))), Score);
			if (Score < 95)
			{
				Note += FString::Printf(TEXT(" · дальше: %s"), *FString(UTF8_TO_TCHAR(vsim::parts::OpName(vsim::parts::NextStep(P)))).ToLower());
			}
			Line(Note, P.Holes > 0.3 ? 2 : (Score < 50 ? 3 : 0));
		}
	}
	Line(TEXT("Как работать"), 1);
	Line(TEXT("Вещь из карманов — в руки клавишами 1–0 (номер ячейки в окне Tab), G — положить на землю."), 0);
	Line(TEXT("Капот: E по капоту. Наведите на узел: «снять» — нужен его инструмент в карманах, «поставить» — деталь в руках. Держите E, пока идёт работа."), 0);
	Line(TEXT("Масло и антифриз: канистру — в руки, наведите на горловину под капотом и держите E."), 0);
	Line(TEXT("Кузов: болгарка + отрезной диск — вырезать гниль; лепестковый — зачистить и ошкурить; затем шпаклёвка, грунт, краска."), 0);
	Line(TEXT("Голый металл под дождём ржавеет за часы — грунтуйте сразу после зачистки."), 0);
	Line(TEXT("Печка (U) греет только с прогретым мотором. Чтобы стёкла не потели — приоткройте окно (колесо мыши на ручке)."), 0);
	TSharedRef<SPeregonManualPanel> Panel = SNew(SPeregonManualPanel).Owner(this).Title(TEXT("Техничка «Сокол-1500»")).Lines(Lines);
	ShowPanel(Panel, Panel);
}

void APeregonPlayerController::ToggleTasks()
{
	if (APeregonHUD* Hud = Cast<APeregonHUD>(GetHUD()))
	{
		Hud->ToggleTasks();
	}
}

void APeregonPlayerController::TakePhoto()
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	if (!GM || !GI || GM->GetMode() == EPeregonMode::Menu || IsMenuOpen() || IsPanelOpen())
	{
		return;
	}
	const APeregonRoadWorld* World = GM->GetRoadWorld();
	const FString Place = World ? World->GetPlaceName(World->GetTrackedS()) : FString(TEXT("Полигон"));
	const int32 Hours = FMath::FloorToInt(GI->TimeOfDayHours);
	const int32 Minutes = FMath::FloorToInt((GI->TimeOfDayHours - Hours) * 60.f);
	const FString Note = FString::Printf(TEXT("День %d, %02d:%02d · %s"), GI->Day, Hours, Minutes, *Place);
	const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots/Peregon") /
		FString::Printf(TEXT("%03d_day%02d_%02d%02d.png"), GI->Journal.Photos + 1, GI->Day, Hours, Minutes);
	FScreenshotRequest::RequestScreenshot(File, false, false); // без интерфейса
	++GI->Journal.Photos;
	GI->Journal.PhotoNotes.Add(Note);
	PeregonAudio::PlayEvent(this, TEXT("camera.shutter"), GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector, 0.6f);
	Toast(FString::Printf(TEXT("Фото %d: %s (журнал — B)"), GI->Journal.Photos, *Note));
}

void APeregonPlayerController::OpenJournal()
{
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	if (!GM || !GI || GM->GetMode() == EPeregonMode::Menu || IsMenuOpen())
	{
		return;
	}
	if (IsPanelOpen())
	{
		ClosePanel();
		return;
	}
	const FPeregonJournal& J = GI->Journal;
	TArray<FPeregonManualLine> Lines;
	auto Line = [&Lines](const FString& Text, int32 Style) { Lines.Add({Text, Style}); };
	Line(TEXT("Дорога"), 1);
	Line(FString::Printf(TEXT("День %d. Пройдено за рулём: %.1f км. Машин в колонне: %d."), GI->Day, J.Km, GM->GetCars().Num()), 0);
	Line(FString::Printf(TEXT("Куплено бензина: %.0f л. Потрачено: %.0f ₽. На руках: %.0f ₽."), J.FuelBoughtL, J.MoneySpent, GI->Money), 0);
	Line(FString::Printf(TEXT("Поставлено деталей: %d. Кузовных работ: %d."), J.PartsInstalled, J.BodyJobs), 0);
	if (const APeregonRoadWorld* World = GM->GetRoadWorld())
	{
		Line(TEXT("Пройденные места"), 1);
		int32 Passed = 0;
		for (const rgen::Poi& P : World->GetRoute().Pois())
		{
			if (P.Name.empty() || P.Type == rgen::PoiType::Waypoint)
			{
				continue;
			}
			const bool bDone = P.S <= GI->TraveledMaxS;
			if (bDone || Passed < 64)
			{
				Line(FString::Printf(TEXT("%s — %.0f км"), UTF8_TO_TCHAR(P.Name.c_str()), P.RealKm), bDone ? 4 : 0);
				Passed += bDone ? 1 : 0;
			}
			if (!bDone)
			{
				Line(TEXT("…дальше — впереди."), 0);
				break;
			}
		}
	}
	Line(TEXT("Фотографии"), 1);
	if (J.PhotoNotes.Num() == 0)
	{
		Line(TEXT("Пока ни одной. F9 — сфотографировать (кадр без интерфейса)."), 0);
	}
	for (int32 I = FMath::Max(0, J.PhotoNotes.Num() - 20); I < J.PhotoNotes.Num(); ++I)
	{
		Line(FString::Printf(TEXT("%d. %s"), I + 1, *J.PhotoNotes[I]), 0);
	}
	if (J.Photos > 0)
	{
		Line(FString::Printf(TEXT("Снимки лежат в папке %s"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/Peregon"))), 0);
	}
	TSharedRef<SPeregonManualPanel> Panel = SNew(SPeregonManualPanel).Owner(this).Title(TEXT("Бортовой журнал")).Lines(Lines);
	ShowPanel(Panel, Panel);
}
