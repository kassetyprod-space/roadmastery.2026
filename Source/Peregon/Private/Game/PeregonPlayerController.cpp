#include "Game/PeregonPlayerController.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/PeregonGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SPeregonMenu.h"
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
	// Esc в редакторе останавливает Play — поэтому дублируем на Tab.
	for (const FKey& Key : {EKeys::Escape, EKeys::Tab, EKeys::Gamepad_Special_Right})
	{
		FInputKeyBinding& Binding = InputComponent->BindKey(Key, IE_Pressed, this, &APeregonPlayerController::TogglePause);
		Binding.bExecuteWhenPaused = true;
	}
}

void APeregonPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	CloseMenu();
	Super::EndPlay(Reason);
}

void APeregonPlayerController::OpenMenu(bool bPause)
{
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
