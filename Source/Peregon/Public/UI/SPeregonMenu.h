// Главное меню / пауза / новая игра / настройки. Чистый Slate: работает без единого .uasset.
// Стиль — минимализм: тёмная полупрозрачная колонка слева, крупный заголовок, текстовые кнопки.
#pragma once

#include "CoreMinimal.h"
#include "Game/PeregonGameInstance.h"
#include "Widgets/SCompoundWidget.h"

class APeregonPlayerController;
class SWidgetSwitcher;
class SVerticalBox;

class PEREGON_API SPeregonMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPeregonMenu) : _bPauseMenu(false) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APeregonPlayerController>, Owner)
		SLATE_ARGUMENT(bool, bPauseMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	enum class EPage : int32
	{
		Main = 0,
		NewGame = 1,
		Settings = 2,
		Pause = 3
	};

	void ShowPage(EPage Page);
	void GoBack();

	TSharedRef<SWidget> BuildMainPage();
	TSharedRef<SWidget> BuildPausePage();
	TSharedRef<SWidget> BuildNewGamePage();
	TSharedRef<SWidget> BuildSettingsPage();

	// Элементы.
	TSharedRef<SWidget> MakeButton(const FText& Label, TFunction<void()> OnClick, TAttribute<bool> Enabled = true, bool bLarge = true);
	TSharedRef<SWidget> MakeHeader(const FText& Text);
	TSharedRef<SWidget> MakeRow(const FText& Label, TSharedRef<SWidget> Control);
	TSharedRef<SWidget> MakeSlider(float* Value, float Min, float Max, int32 Decimals, const FString& Suffix);
	TSharedRef<SWidget> MakeToggle(bool* Value);
	TSharedRef<SWidget> MakeChoice(int32* Index, TArray<FText> Options);

	void LoadSettings();
	void ApplySettings();
	void StartNewGame(bool bProvingGround);

	TWeakObjectPtr<APeregonPlayerController> Owner;
	TSharedPtr<SWidgetSwitcher> Switcher;
	EPage Current = EPage::Main;
	bool bPause = false;

	// Новая игра.
	FPeregonRunConfig NewConfig;
	int32 AssistIndex = 0;
	int32 ColorIndex = 0;
	FString SeedText;

	// Настройки (копия до «Применить»).
	float Sensitivity = 1.f;
	bool bInvertY = false;
	float Fov = 80.f;
	float HeadMotion = 1.f;
	int32 StartViewIndex = 0;
	int32 TimeIndex = 1;
	int32 WindowModeIndex = 0;
	int32 ResolutionIndex = 0;
	int32 QualityIndex = 2;
	float RenderScale = 100.f;
	bool bVSync = false;
	int32 FpsIndex = 0;
	TArray<FIntPoint> Resolutions;
};
