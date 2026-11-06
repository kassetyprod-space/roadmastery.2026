// Контроллер игрока: главное меню, пауза и игровые окна (инвентарь, магазин, карта). Slate, без бинарных ассетов.
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

	// Игровые окна (игра на паузе, пока окно открыто).
	void OpenInventory(EPeregonStorage Storage = EPeregonStorage::None);
	void OpenShop(const TArray<FName>& Items, const FString& Title, float FuelPrice);
	void OpenMap();
	void OpenMapRegion(int32 Region);
	void ClosePanel();
	bool IsPanelOpen() const { return PanelContainer.IsValid(); }
	void ToggleInventory();
	void ToggleMap();
	// Проверка погоды и сезонов: F6 — следующая погода, F7 — на 15 дней вперёд.
	void NextWeather();
	void SkipDays();
	// F8 — часы на час вперёд (временно, для проверки освещения).
	void SkipHour();

	// Вещи. Storage::None — карманы.
	FPeregonContainer* GetContainer(EPeregonStorage Storage) const;
	bool CanMoveItem(FName Id, EPeregonStorage From, EPeregonStorage To) const;
	void MoveItem(FName Id, EPeregonStorage From, EPeregonStorage To);
	void UseItem(FName Id, EPeregonStorage From);
	float GetShopPrice(FName Id) const;
	bool CanBuyItem(FName Id) const;
	void BuyItem(FName Id);
	// Взять товар с полки и заплатить на месте. false — не хватило денег или места в карманах.
	bool BuyItemAt(FName Id, float Price);
	float GetMoney() const;
	// Карта области под рукой: в карманах или (если сидим в машине) в бардачке.
	bool HasMapForRegion(int32 Region) const;

private:
	void OpenMenu(bool bPause);
	void ShowPanel(TSharedRef<SWidget> Panel, TSharedPtr<SWidget> Focus);
	void OnEscape();
	void Toast(const FString& Text) const;

	TSharedPtr<SPeregonMenu> MenuWidget;
	TSharedPtr<SWidget> MenuContainer;
	bool bPauseMenu = false;

	TSharedPtr<SWidget> PanelWidget;
	TSharedPtr<SWidget> PanelContainer;
	float ShopFuelPrice = 52.f;
};
