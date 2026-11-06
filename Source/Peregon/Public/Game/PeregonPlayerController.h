// Контроллер игрока: главное меню, пауза и игровые окна (инвентарь, магазин, карта). Slate, без бинарных ассетов.
#pragma once

#include "CoreMinimal.h"
#include "Game/PeregonGameInstance.h"
#include "GameFramework/PlayerController.h"
#include "PeregonPlayerController.generated.h"

class APeregonCar;
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
	// FromCar — чей бардачок или багажник открыт (у каждой машины колонны свои).
	void OpenInventory(EPeregonStorage Storage = EPeregonStorage::None, APeregonCar* FromCar = nullptr);
	void OpenShop(const TArray<FName>& Items, const FString& Title, float FuelPrice);
	void OpenMap();
	void OpenMapRegion(int32 Region);
	// Техничка: что не так с машиной рядом (или той, в которой сидим).
	void OpenManual();
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
	// Переложить конкретную ячейку (детали с разным состоянием — разные ячейки).
	bool CanMoveSlot(int32 Index, EPeregonStorage From, EPeregonStorage To) const;
	void MoveSlot(int32 Index, EPeregonStorage From, EPeregonStorage To);

	// --- В руках: одна ячейка карманов (клавиши 1–0), G — положить на землю ---
	void SelectHeld(int32 PocketIndex);
	int32 GetHeldIndex() const;
	const FPeregonItemStack* GetHeld() const;
	// Забрать то, что в руках, из карманов (поставить деталь, отдать). false — руки пусты.
	bool TakeHeld(FPeregonItemStack& Out);
	// Израсходовать долю того, что в руках (краска, шпаклёвка, масло). Кончилось — пустая тара выбрасывается.
	void UseHeld(float Amount);
	// Инструмент vsim::parts::Tool в карманах (None — всегда есть).
	bool HasTool(int32 Tool) const;
	bool HasConsumable(int32 Consumable) const;
	bool UseConsumable(int32 Consumable, float Amount);
	// В карманы, а если там нет места — на землю под ноги.
	void GiveOrDrop(const FPeregonItemStack& Stack);
	void DropHeld();
	// Положить предмет на землю перед игроком (возвращает false, если не нашлось земли).
	bool DropOnGround(const FPeregonItemStack& Stack);
	void UseItem(FName Id, EPeregonStorage From);
	float GetShopPrice(FName Id) const;
	bool CanBuyItem(FName Id) const;
	void BuyItem(FName Id);
	// Взять товар с полки и заплатить на месте. false — не хватило денег или места в карманах.
	bool BuyItemAt(FName Id, float Price);
	float GetMoney() const;
	// Карта области под рукой: в карманах или (если сидим в машине) в бардачке.
	bool HasMapForRegion(int32 Region) const;
	void Toast(const FString& Text) const;

private:
	void OpenMenu(bool bPause);
	void ShowPanel(TSharedRef<SWidget> Panel, TSharedPtr<SWidget> Focus);
	void OnEscape();
	void OnHotkey(FKey Key);

	TSharedPtr<SPeregonMenu> MenuWidget;
	TSharedPtr<SWidget> MenuContainer;
	bool bPauseMenu = false;

	TSharedPtr<SWidget> PanelWidget;
	TSharedPtr<SWidget> PanelContainer;
	float ShopFuelPrice = 52.f;
	TWeakObjectPtr<APeregonCar> StorageCar;
	FName HeldId;
	float HeldCondition = 1.f;
};
