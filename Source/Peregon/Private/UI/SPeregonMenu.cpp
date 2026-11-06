#include "UI/SPeregonMenu.h"

#include "Art/PeregonArtSubsystem.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/PeregonGameUserSettings.h"
#include "Game/PeregonPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace PeregonMenuStyle
{
const FLinearColor TextColor(0.93f, 0.92f, 0.88f, 1.f);
const FLinearColor DimColor(0.93f, 0.92f, 0.88f, 0.5f);
const FLinearColor AccentColor(0.95f, 0.62f, 0.25f, 1.f);

FSlateFontInfo Font(int32 Size, bool bBold = false)
{
	return UPeregonArtSubsystem::SlateFont(bBold ? EPeregonFont::SemiBold : EPeregonFont::Regular, Size);
}

FSlateFontInfo TitleFont(int32 Size)
{
	return UPeregonArtSubsystem::SlateFont(EPeregonFont::Display, Size);
}

const FButtonStyle& Button()
{
	static const FButtonStyle Style = []() {
		FButtonStyle S;
		S.SetNormal(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.f)));
		S.SetHovered(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.10f)));
		S.SetPressed(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.18f)));
		S.SetDisabled(FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f)));
		S.SetNormalPadding(FMargin(0.f));
		S.SetPressedPadding(FMargin(0.f));
		return S;
	}();
	return Style;
}

const FSlateBrush* White()
{
	return FCoreStyle::Get().GetBrush("WhiteBrush");
}

// Пресеты цвета кузова: выцветшие советские эмали.
struct FPaint
{
	const TCHAR* Name;
	FLinearColor Color;
};
const FPaint Paints[] = {
	{TEXT("Вишня"), FLinearColor(0.32f, 0.04f, 0.04f)},
	{TEXT("Белая ночь"), FLinearColor(0.72f, 0.72f, 0.68f)},
	{TEXT("Морская волна"), FLinearColor(0.05f, 0.22f, 0.26f)},
	{TEXT("Липа зелёная"), FLinearColor(0.30f, 0.38f, 0.10f)},
	{TEXT("Охра"), FLinearColor(0.55f, 0.33f, 0.06f)},
	{TEXT("Мокрый асфальт"), FLinearColor(0.08f, 0.09f, 0.10f)},
};

const float TimeScales[] = {0.25f, 0.5f, 1.f, 4.f};
const float FpsLimits[] = {0.f, 30.f, 60.f, 120.f, 144.f, 165.f, 240.f};
} // namespace PeregonMenuStyle

void SPeregonMenu::Construct(const FArguments& InArgs)
{
	using namespace PeregonMenuStyle;
	Owner = InArgs._Owner;
	bPause = InArgs._bPauseMenu;
	LoadSettings();
	NewConfig.Seed = FMath::RandRange(1, 999999);
	SeedText = FString::FromInt(NewConfig.Seed);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(White())
			.ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, bPause ? 0.5f : 0.12f))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Fill)
		[
			SNew(SBox)
			.WidthOverride(580.f)
			[
				SNew(SBorder)
				.BorderImage(White())
				.BorderBackgroundColor(FLinearColor(0.02f, 0.025f, 0.03f, 0.8f))
				.Padding(FMargin(56.f, 56.f, 48.f, 36.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(INVTEXT("ПЕРЕГОН"))
						.Font(TitleFont(40))
						.ColorAndOpacity(TextColor)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2.f, 0.f, 0.f, 36.f)
					[
						SNew(STextBlock)
						.Text(INVTEXT("Новосибирск — Владивосток"))
						.Font(Font(15))
						.ColorAndOpacity(DimColor)
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						SAssignNew(Switcher, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot() [ BuildMainPage() ]
						+ SWidgetSwitcher::Slot() [ BuildNewGamePage() ]
						+ SWidgetSwitcher::Slot() [ BuildSettingsPage() ]
						+ SWidgetSwitcher::Slot() [ BuildPausePage() ]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(INVTEXT("Прототип · Esc / F10 — назад · данные карт © участники OpenStreetMap"))
						.Font(Font(10))
						.ColorAndOpacity(DimColor)
					]
				]
			]
		]
	];
	ShowPage(bPause ? EPage::Pause : EPage::Main);
}

void SPeregonMenu::ShowPage(EPage Page)
{
	Current = Page;
	if (Switcher.IsValid())
	{
		Switcher->SetActiveWidgetIndex(static_cast<int32>(Page));
	}
	if (Page == EPage::Settings)
	{
		LoadSettings();
	}
}

void SPeregonMenu::GoBack()
{
	if (Current == EPage::NewGame || Current == EPage::Settings)
	{
		ShowPage(bPause ? EPage::Pause : EPage::Main);
	}
	else if (Current == EPage::Pause && Owner.IsValid())
	{
		Owner->UiResume();
	}
}

FReply SPeregonMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::F10 || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		GoBack();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// ---------------------------------------------------------------------------------------------
// Элементы
// ---------------------------------------------------------------------------------------------
TSharedRef<SWidget> SPeregonMenu::MakeButton(const FText& Label, TFunction<void()> OnClick, TAttribute<bool> Enabled, bool bLarge)
{
	using namespace PeregonMenuStyle;
	return SNew(SButton)
		.ButtonStyle(&Button())
		.ContentPadding(FMargin(14.f, bLarge ? 8.f : 4.f))
		.IsEnabled(Enabled)
		.OnClicked_Lambda([OnClick]() {
			if (OnClick)
			{
				OnClick();
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Font(bLarge ? 22 : 16))
			.ColorAndOpacity_Lambda([Enabled]() { return FSlateColor(Enabled.Get() ? TextColor : DimColor); })
		];
}

TSharedRef<SWidget> SPeregonMenu::MakeHeader(const FText& Text)
{
	using namespace PeregonMenuStyle;
	return SNew(SBox)
		.Padding(FMargin(14.f, 18.f, 0.f, 8.f))
		[
			SNew(STextBlock)
			.Text(Text)
			.Font(Font(13, true))
			.ColorAndOpacity(AccentColor)
		];
}

TSharedRef<SWidget> SPeregonMenu::MakeRow(const FText& Label, TSharedRef<SWidget> Control)
{
	using namespace PeregonMenuStyle;
	return SNew(SBox)
		.Padding(FMargin(14.f, 3.f, 0.f, 3.f))
		.MinDesiredHeight(34.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.46f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font(14))
				.ColorAndOpacity(TextColor)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.54f)
			.VAlign(VAlign_Center)
			[
				Control
			]
		];
}

TSharedRef<SWidget> SPeregonMenu::MakeSlider(float* Value, float Min, float Max, int32 Decimals, const FString& Suffix)
{
	using namespace PeregonMenuStyle;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(SSlider)
			.MinValue(Min)
			.MaxValue(Max)
			.Value_Lambda([Value]() { return *Value; })
			.OnValueChanged_Lambda([Value](float NewValue) { *Value = NewValue; })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(10.f, 0.f, 0.f, 0.f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(70.f)
			[
				SNew(STextBlock)
				.Font(Font(13))
				.ColorAndOpacity(TextColor)
				.Text_Lambda([Value, Decimals, Suffix]() {
					FNumberFormattingOptions Options;
					Options.MinimumFractionalDigits = Decimals;
					Options.MaximumFractionalDigits = Decimals;
					return FText::Format(INVTEXT("{0}{1}"), FText::AsNumber(*Value, &Options), FText::FromString(Suffix));
				})
			]
		];
}

TSharedRef<SWidget> SPeregonMenu::MakeToggle(bool* Value)
{
	return SNew(SCheckBox)
		.IsChecked_Lambda([Value]() { return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([Value](ECheckBoxState State) { *Value = State == ECheckBoxState::Checked; });
}

TSharedRef<SWidget> SPeregonMenu::MakeChoice(int32* Index, TArray<FText> Options)
{
	using namespace PeregonMenuStyle;
	TSharedRef<TArray<FText>> Opts = MakeShared<TArray<FText>>(MoveTemp(Options));
	auto Step = [Index, Opts](int32 Delta) {
		const int32 N = Opts->Num();
		if (N > 0)
		{
			*Index = ((*Index + Delta) % N + N) % N;
		}
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeButton(INVTEXT("‹"), [Step]() { Step(-1); }, true, false)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(Font(14))
			.ColorAndOpacity(TextColor)
			.Text_Lambda([Index, Opts]() { return Opts->IsValidIndex(*Index) ? (*Opts)[*Index] : FText::GetEmpty(); })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeButton(INVTEXT("›"), [Step]() { Step(1); }, true, false)
		];
}

// ---------------------------------------------------------------------------------------------
// Страницы
// ---------------------------------------------------------------------------------------------
TSharedRef<SWidget> SPeregonMenu::BuildMainPage()
{
	using namespace PeregonMenuStyle;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const bool bHasSave = Owner.IsValid() && Owner->UiHasSave();
	if (bHasSave)
	{
		Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Продолжить"), [this]() { if (Owner.IsValid()) { Owner->UiContinue(); } })];
		Box->AddSlot().AutoHeight().Padding(16.f, 0.f, 0.f, 10.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Owner->UiSaveDescription()))
			.Font(Font(12))
			.ColorAndOpacity(DimColor)
		];
	}
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Новая игра"), [this]() { ShowPage(EPage::NewGame); })];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Испытательный полигон"), [this]() { StartNewGame(true); })];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Настройки"), [this]() { ShowPage(EPage::Settings); })];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Выход"), [this]() { if (Owner.IsValid()) { Owner->UiQuit(); } })];
	return Box;
}

TSharedRef<SWidget> SPeregonMenu::BuildPausePage()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Продолжить"), [this]() { if (Owner.IsValid()) { Owner->UiResume(); } })];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Сохранить"), [this]() { if (Owner.IsValid()) { Owner->UiSave(); Owner->UiResume(); } },
		TAttribute<bool>::CreateLambda([this]() { return Owner.IsValid() && Owner->UiCanSave(); }))];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Настройки"), [this]() { ShowPage(EPage::Settings); })];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("В главное меню"), [this]() { if (Owner.IsValid()) { Owner->UiToMainMenu(); } })];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Выйти из игры"), [this]() { if (Owner.IsValid()) { Owner->UiQuit(); } })];
	return Box;
}

TSharedRef<SWidget> SPeregonMenu::BuildNewGamePage()
{
	using namespace PeregonMenuStyle;
	TArray<FText> PaintNames;
	for (const FPaint& Paint : Paints)
	{
		PaintNames.Add(FText::FromString(Paint.Name));
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()[MakeHeader(INVTEXT("НОВАЯ ИГРА"))];
	Box->AddSlot().AutoHeight()
	[
		MakeRow(INVTEXT("Маршрут"), SNew(STextBlock).Text(INVTEXT("Новосибирск → Заречный · 15 км")).Font(Font(14)).ColorAndOpacity(DimColor))
	];
	Box->AddSlot().AutoHeight()
	[
		MakeRow(INVTEXT("Сид мира"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.Font(Font(14))
				.Text_Lambda([this]() { return FText::FromString(SeedText); })
				.OnTextChanged_Lambda([this](const FText& NewText) { SeedText = NewText.ToString(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeButton(INVTEXT("Случайный"), [this]() { SeedText = FString::FromInt(FMath::RandRange(1, 999999)); }, true, false)
			])
	];
	Box->AddSlot().AutoHeight()
	[
		MakeRow(INVTEXT("Управление"), MakeChoice(&AssistIndex, {INVTEXT("Автомат + ABS"), INVTEXT("Механика"), INVTEXT("Хардкор")}))
	];
	Box->AddSlot().AutoHeight().Padding(14.f, 0.f, 0.f, 6.f)
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.Font(Font(12))
		.ColorAndOpacity(DimColor)
		.Text_Lambda([this]() {
			switch (AssistIndex)
			{
			case 1: return INVTEXT("Передачи вручную (E / Q), сцепление автоматическое, без ABS.");
			case 2: return INVTEXT("Передачи и сцепление вручную (Shift). Можно заглохнуть и сжечь сцепление.");
			default: return INVTEXT("Автоматическая коробка, автосцепление и ABS. Для знакомства с игрой.");
			}
		})
	];
	Box->AddSlot().AutoHeight()[MakeRow(INVTEXT("Цвет машины"), MakeChoice(&ColorIndex, PaintNames))];
	Box->AddSlot().AutoHeight()[MakeRow(INVTEXT("Начало"), MakeChoice(&StartIndex, {INVTEXT("Гараж: собрать «Сокол» из хлама"), INVTEXT("Сразу на трассу")}))];
	Box->AddSlot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)[MakeButton(INVTEXT("В путь"), [this]() { StartNewGame(false); })];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Назад"), [this]() { GoBack(); }, true, false)];
	return Box;
}

TSharedRef<SWidget> SPeregonMenu::BuildSettingsPage()
{
	using namespace PeregonMenuStyle;
	TArray<FText> ResolutionNames;
	for (const FIntPoint& R : Resolutions)
	{
		ResolutionNames.Add(FText::FromString(FString::Printf(TEXT("%d × %d"), R.X, R.Y)));
	}

	TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
	auto Add = [&Scroll](TSharedRef<SWidget> Widget) { Scroll->AddSlot()[Widget]; };

	Add(MakeHeader(INVTEXT("КАМЕРА И УПРАВЛЕНИЕ")));
	Add(MakeRow(INVTEXT("Чувствительность"), MakeSlider(&Sensitivity, 0.2f, 3.f, 2, TEXT(""))));
	Add(MakeRow(INVTEXT("Инвертировать по вертикали"), MakeToggle(&bInvertY)));
	Add(MakeRow(INVTEXT("Поле зрения"), MakeSlider(&Fov, 60.f, 110.f, 0, TEXT("°"))));
	Add(MakeRow(INVTEXT("Инерция головы и тряска"), MakeSlider(&HeadMotion, 0.f, 1.5f, 1, TEXT(""))));
	Add(MakeRow(INVTEXT("Вид по умолчанию"), MakeChoice(&StartViewIndex, {INVTEXT("Из салона"), INVTEXT("Снаружи")})));
	Add(MakeRow(INVTEXT("Длина суток"), MakeChoice(&TimeIndex, {INVTEXT("96 минут"), INVTEXT("48 минут"), INVTEXT("24 минуты"), INVTEXT("6 минут")})));

	Add(MakeHeader(INVTEXT("ГРАФИКА")));
	Add(MakeRow(INVTEXT("Стиль (текстуры — после перезапуска)"), MakeChoice(&StyleIndex, {INVTEXT("Живопись"), INVTEXT("Пиксели PSX")})));
	Add(MakeRow(INVTEXT("Режим окна"), MakeChoice(&WindowModeIndex, {INVTEXT("Полный экран"), INVTEXT("Окно без рамки"), INVTEXT("Окно")})));
	Add(MakeRow(INVTEXT("Разрешение"), MakeChoice(&ResolutionIndex, ResolutionNames)));
	Add(MakeRow(INVTEXT("Качество"), MakeChoice(&QualityIndex,
		{INVTEXT("Низкое"), INVTEXT("Среднее"), INVTEXT("Высокое"), INVTEXT("Эпическое"), INVTEXT("Кинематографичное")})));
	Add(MakeRow(INVTEXT("Масштаб рендера"), MakeSlider(&RenderScale, 50.f, 100.f, 0, TEXT("%"))));
	Add(MakeRow(INVTEXT("Вертикальная синхронизация"), MakeToggle(&bVSync)));
	Add(MakeRow(INVTEXT("Лимит кадров"), MakeChoice(&FpsIndex,
		{INVTEXT("Без лимита"), INVTEXT("30"), INVTEXT("60"), INVTEXT("120"), INVTEXT("144"), INVTEXT("165"), INVTEXT("240")})));

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().FillHeight(1.f)[Scroll];
	Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)[MakeButton(INVTEXT("Применить"), [this]() { ApplySettings(); })];
	Box->AddSlot().AutoHeight()[MakeButton(INVTEXT("Назад"), [this]() { GoBack(); }, true, false)];
	return Box;
}

// ---------------------------------------------------------------------------------------------
// Настройки и запуск
// ---------------------------------------------------------------------------------------------
void SPeregonMenu::LoadSettings()
{
	using namespace PeregonMenuStyle;
	const UPeregonGameUserSettings* S = UPeregonGameUserSettings::Get();
	Sensitivity = S->MouseSensitivity;
	bInvertY = S->bInvertY;
	Fov = S->FieldOfView;
	HeadMotion = S->HeadMotion;
	StartViewIndex = S->bStartFirstPerson ? 0 : 1;
	StyleIndex = FMath::Clamp(S->VisualStyle, 0, 1);
	TimeIndex = 1;
	for (int32 I = 0; I < static_cast<int32>(UE_ARRAY_COUNT(TimeScales)); ++I)
	{
		if (FMath::IsNearlyEqual(TimeScales[I], S->TimeScale))
		{
			TimeIndex = I;
		}
	}

	switch (S->GetFullscreenMode())
	{
	case EWindowMode::Fullscreen: WindowModeIndex = 0; break;
	case EWindowMode::WindowedFullscreen: WindowModeIndex = 1; break;
	default: WindowModeIndex = 2; break;
	}

	if (Resolutions.Num() == 0)
	{
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
		const FIntPoint CurrentRes = S->GetScreenResolution();
		if (!Resolutions.Contains(CurrentRes) && CurrentRes.X > 0)
		{
			Resolutions.Add(CurrentRes);
		}
		if (Resolutions.Num() == 0)
		{
			Resolutions = {FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(2560, 1440)};
		}
		Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y < B.X * B.Y; });
	}
	ResolutionIndex = FMath::Max(0, Resolutions.IndexOfByKey(S->GetScreenResolution()));

	QualityIndex = FMath::Clamp(S->GetOverallScalabilityLevel(), 0, 4);
	if (S->GetOverallScalabilityLevel() < 0)
	{
		QualityIndex = 2; // «своё» качество — показываем как высокое
	}
	RenderScale = FMath::Clamp(S->GetResolutionScaleNormalized() * 100.f, 50.f, 100.f);
	bVSync = S->IsVSyncEnabled();
	FpsIndex = 0;
	for (int32 I = 0; I < static_cast<int32>(UE_ARRAY_COUNT(FpsLimits)); ++I)
	{
		if (FMath::IsNearlyEqual(FpsLimits[I], S->GetFrameRateLimit(), 0.5f))
		{
			FpsIndex = I;
		}
	}
}

void SPeregonMenu::ApplySettings()
{
	using namespace PeregonMenuStyle;
	UPeregonGameUserSettings* S = UPeregonGameUserSettings::Get();
	S->MouseSensitivity = Sensitivity;
	S->bInvertY = bInvertY;
	S->FieldOfView = Fov;
	S->HeadMotion = HeadMotion;
	S->bStartFirstPerson = StartViewIndex == 0;
	S->VisualStyle = StyleIndex;
	S->TimeScale = TimeScales[FMath::Clamp(TimeIndex, 0, static_cast<int32>(UE_ARRAY_COUNT(TimeScales)) - 1)];

	const EWindowMode::Type Modes[] = {EWindowMode::Fullscreen, EWindowMode::WindowedFullscreen, EWindowMode::Windowed};
	S->SetFullscreenMode(Modes[FMath::Clamp(WindowModeIndex, 0, 2)]);
	if (Resolutions.IsValidIndex(ResolutionIndex))
	{
		S->SetScreenResolution(Resolutions[ResolutionIndex]);
	}
	S->SetOverallScalabilityLevel(QualityIndex);
	S->SetResolutionScaleNormalized(RenderScale / 100.f);
	S->SetVSyncEnabled(bVSync);
	S->SetFrameRateLimit(FpsLimits[FMath::Clamp(FpsIndex, 0, static_cast<int32>(UE_ARRAY_COUNT(FpsLimits)) - 1)]);
	S->ApplySettings(false);
	S->SaveSettings();
}

void SPeregonMenu::StartNewGame(bool bProvingGround)
{
	using namespace PeregonMenuStyle;
	FPeregonRunConfig Config = NewConfig;
	const FString Trimmed = SeedText.TrimStartAndEnd();
	Config.Seed = Trimmed.IsNumeric() ? FMath::Abs(FCString::Atoi(*Trimmed)) : static_cast<int32>(GetTypeHash(Trimmed) & 0x7FFFFFFF);
	Config.Assists = static_cast<EPeregonAssists>(FMath::Clamp(AssistIndex, 0, 2));
	Config.CarColor = Paints[FMath::Clamp(ColorIndex, 0, static_cast<int32>(UE_ARRAY_COUNT(Paints)) - 1)].Color;
	Config.bProvingGround = bProvingGround;
	Config.bGarageStart = !bProvingGround && StartIndex == 0;
	if (Owner.IsValid())
	{
		Owner->UiNewGame(Config);
	}
}
