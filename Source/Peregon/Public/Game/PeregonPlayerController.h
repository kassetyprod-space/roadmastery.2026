// Контроллер игрока: показывает главное меню и меню паузы (Slate, без бинарных ассетов).
#pragma once

#include "CoreMinimal.h"
#include "Game/PeregonGameInstance.h"
#include "GameFramework/PlayerController.h"
#include "PeregonPlayerController.generated.h"

class SPeregonMenu;
class SWidget;

UCLASS()
class PEREGON_API APeregonPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APeregonPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void ShowMainMenu();
	void ShowPauseMenu();
	void CloseMenu();
	bool IsMenuOpen() const { return MenuWidget.IsValid(); }
	void TogglePause();

	// Вызывает меню.
	void UiNewGame(const FPeregonRunConfig& Config);
	void UiContinue();
	void UiResume();
	void UiSave();
	void UiToMainMenu();
	void UiQuit();
	bool UiHasSave() const;
	FString UiSaveDescription() const;
	bool UiCanSave() const;

private:
	void OpenMenu(bool bPause);

	TSharedPtr<SPeregonMenu> MenuWidget;
	TSharedPtr<SWidget> MenuContainer;
	bool bPauseMenu = false;
};
