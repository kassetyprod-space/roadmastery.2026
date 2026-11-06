#include "UI/SPeregonPanels.h"

#include "Art/PeregonArtSubsystem.h"
#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/PeregonPlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "rgen/Atlas.h"
#include "rgen/Route.h"

#include <cmath>

namespace PeregonPanelStyle
{
const FLinearColor TextColor(0.93f, 0.92f, 0.88f, 1.f);
const FLinearColor DimColor(0.93f, 0.92f, 0.88f, 0.5f);
const FLinearColor AccentColor(0.95f, 0.62f, 0.25f, 1.f);
const FLinearColor PanelColor(0.03f, 0.032f, 0.035f, 0.94f);
const FLinearColor RowColor(1.f, 1.f, 1.f, 0.04f);

FSlateFontInfo Font(int32 Size, bool bBold = false)
{
	return UPeregonArtSubsystem::SlateFont(bBold ? EPeregonFont::SemiBold : EPeregonFont::Regular, Size);
}

const FSlateBrush* White()
{
	return FCoreStyle::Get().GetBrush("WhiteBrush");
}

const FButtonStyle& Button()
{
	static const FButtonStyle Style = []() {
		FButtonStyle S;
		S.SetNormal(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.06f)));
		S.SetHovered(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.16f)));
		S.SetPressed(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.24f)));
		S.SetDisabled(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.02f)));
		S.SetNormalPadding(FMargin(0.f));
		S.SetPressedPadding(FMargin(0.f));
		return S;
	}();
	return Style;
}

FString StorageTitle(EPeregonStorage Storage)
{
	switch (Storage)
	{
	case EPeregonStorage::Glovebox: return TEXT("Бардачок");
	case EPeregonStorage::Trunk: return TEXT("Багажник");
	default: return FString();
	}
}

bool IsCloseKey(const FKey& Key)
{
	return Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::M || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right;
}
} // namespace PeregonPanelStyle

// ---------------------------------------------------------------------------------------------
// Инвентарь / магазин
// ---------------------------------------------------------------------------------------------
void SPeregonInventoryPanel::Construct(const FArguments& InArgs)
{
	using namespace PeregonPanelStyle;
	Owner = InArgs._Owner;
	Storage = InArgs._Storage;
	ShopItems = InArgs._ShopItems;
	ShopTitle = InArgs._ShopTitle;
	const bool bShop = ShopItems.Num() > 0;
	const FString RightTitle = bShop ? ShopTitle : StorageTitle(Storage);

	auto Column = [&](const FString& Title, TSharedPtr<SVerticalBox>& List) -> TSharedRef<SWidget> {
		return SNew(SBox)
			.WidthOverride(470.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
				[
					SNew(STextBlock).Text(FText::FromString(Title)).Font(Font(14, true)).ColorAndOpacity(AccentColor)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(List, SVerticalBox)
				]
			];
	};

	TSharedRef<SHorizontalBox> Columns = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Column(TEXT("Карманы"), LeftList)];
	if (!RightTitle.IsEmpty())
	{
		Columns->AddSlot().AutoWidth().Padding(FMargin(28.f, 0.f, 0.f, 0.f))[Column(RightTitle, RightList)];
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.45f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(White())
			.BorderBackgroundColor(PanelColor)
			.Padding(FMargin(28.f, 22.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 18.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(bShop ? TEXT("Покупки") : TEXT("Вещи")))
						.Font(UPeregonArtSubsystem::SlateFont(EPeregonFont::Display, 22))
						.ColorAndOpacity(TextColor)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SAssignNew(MoneyText, STextBlock).Font(Font(14, true)).ColorAndOpacity(TextColor)
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Columns
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Tab / Esc — закрыть   ·   M — карта")))
					.Font(Font(11))
					.ColorAndOpacity(DimColor)
				]
			]
		]
	];
	Refresh();
}

TSharedRef<SWidget> SPeregonInventoryPanel::MakeSmallButton(const FText& Label, TFunction<void()> OnClick, bool bEnabled)
{
	using namespace PeregonPanelStyle;
	TWeakPtr<SPeregonInventoryPanel> Self = SharedThis(this);
	return SNew(SButton)
		.ButtonStyle(&Button())
		.ContentPadding(FMargin(10.f, 4.f))
		.IsEnabled(bEnabled)
		.OnClicked_Lambda([OnClick, Self]() {
			if (OnClick)
			{
				OnClick();
			}
			if (TSharedPtr<SPeregonInventoryPanel> Pinned = Self.Pin())
			{
				Pinned->Refresh();
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text(Label).Font(Font(12)).ColorAndOpacity(bEnabled ? TextColor : DimColor)
		];
}

TSharedRef<SWidget> SPeregonInventoryPanel::MakeItemRow(const FPeregonItemStack& Stack, bool bPockets, int32 Index)
{
	using namespace PeregonPanelStyle;
	const FPeregonItemDef* Def = PeregonItems::Find(Stack.Id);
	FString Label = PeregonItems::StackLabel(Stack);
	const FName Id = Stack.Id;
	const bool bHeld = bPockets && Owner.IsValid() && Owner->GetHeldIndex() == Index;
	if (bPockets && Index < 10)
	{
		Label = FString::Printf(TEXT("[%d] %s%s"), (Index + 1) % 10, *Label, bHeld ? TEXT("  — в руках") : TEXT(""));
	}
	const EPeregonStorage From = bPockets ? EPeregonStorage::None : Storage;
	const EPeregonStorage To = bPockets ? Storage : EPeregonStorage::None;
	TWeakObjectPtr<APeregonPlayerController> PC = Owner;

	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	if (Def && Def->bUsable && PC.IsValid())
	{
		Buttons->AddSlot().AutoWidth().Padding(FMargin(6.f, 0.f, 0.f, 0.f))
		[
			MakeSmallButton(FText::FromString(Def->Region >= 0 ? TEXT("Смотреть") : TEXT("Применить")), [PC, Id, From]() {
				if (PC.IsValid())
				{
					PC->UseItem(Id, From);
				}
			})
		];
	}
	if (bPockets && ShopItems.Num() == 0 && PC.IsValid())
	{
		Buttons->AddSlot().AutoWidth().Padding(FMargin(6.f, 0.f, 0.f, 0.f))
		[
			MakeSmallButton(FText::FromString(bHeld ? TEXT("Убрать из рук") : TEXT("В руки")), [PC, Index]() {
				if (PC.IsValid())
				{
					PC->SelectHeld(Index);
				}
			})
		];
	}
	if (Storage != EPeregonStorage::None && ShopItems.Num() == 0)
	{
		const bool bCanMove = PC.IsValid() && PC->CanMoveSlot(Index, From, To);
		Buttons->AddSlot().AutoWidth().Padding(FMargin(6.f, 0.f, 0.f, 0.f))
		[
			MakeSmallButton(FText::FromString(bPockets ? TEXT("Положить →") : TEXT("← Взять")), [PC, Index, From, To]() {
				if (PC.IsValid())
				{
					PC->MoveSlot(Index, From, To);
				}
			}, bCanMove)
		];
	}

	return SNew(SBorder)
		.BorderImage(White())
		.BorderBackgroundColor(RowColor)
		.Padding(FMargin(10.f, 7.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 10.f, 0.f))
			[
				SNew(SBox).WidthOverride(10.f).HeightOverride(26.f)
				[
					SNew(SImage).Image(White()).ColorAndOpacity(Def ? Def->Color : FLinearColor::Gray)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(13, true)).ColorAndOpacity(TextColor)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Def ? Def->Description : FString()))
					.Font(Font(10))
					.ColorAndOpacity(DimColor)
					.AutoWrapText(true)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Buttons
			]
		];
}

TSharedRef<SWidget> SPeregonInventoryPanel::MakeShopRow(FName Id)
{
	using namespace PeregonPanelStyle;
	const FPeregonItemDef* Def = PeregonItems::Find(Id);
	TWeakObjectPtr<APeregonPlayerController> PC = Owner;
	const float Price = PC.IsValid() ? PC->GetShopPrice(Id) : (Def ? Def->Price : 0.f);
	const bool bCan = PC.IsValid() && PC->CanBuyItem(Id);
	return SNew(SBorder)
		.BorderImage(White())
		.BorderBackgroundColor(RowColor)
		.Padding(FMargin(10.f, 7.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 10.f, 0.f))
			[
				SNew(SBox).WidthOverride(10.f).HeightOverride(26.f)
				[
					SNew(SImage).Image(White()).ColorAndOpacity(Def ? Def->Color : FLinearColor::Gray)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(PeregonItems::DisplayName(Id))).Font(Font(13, true)).ColorAndOpacity(TextColor)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(Def ? Def->Description : FString())).Font(Font(10)).ColorAndOpacity(DimColor).AutoWrapText(true)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f))
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%d ₽"), FMath::RoundToInt(Price)))).Font(Font(13, true)).ColorAndOpacity(TextColor)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeSmallButton(FText::FromString(TEXT("Купить")), [PC, Id]() {
					if (PC.IsValid())
					{
						PC->BuyItem(Id);
					}
				}, bCan)
			]
		];
}

void SPeregonInventoryPanel::Refresh()
{
	using namespace PeregonPanelStyle;
	APeregonPlayerController* PC = Owner.Get();
	if (!PC)
	{
		return;
	}
	if (MoneyText.IsValid())
	{
		MoneyText->SetText(FText::FromString(FString::Printf(TEXT("%d ₽"), FMath::RoundToInt(PC->GetMoney()))));
	}
	auto Fill = [&](TSharedPtr<SVerticalBox> List, const FPeregonContainer* C, bool bPockets) {
		if (!List.IsValid())
		{
			return;
		}
		List->ClearChildren();
		if (!C)
		{
			return;
		}
		for (int32 Index = 0; Index < C->Slots.Num(); ++Index)
		{
			List->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))[MakeItemRow(C->Slots[Index], bPockets, Index)];
		}
		const int32 Free = FMath::Max(0, C->Capacity - C->Slots.Num());
		List->AddSlot().AutoHeight().Padding(FMargin(2.f, 4.f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(C->Slots.Num() == 0 ? TEXT("Пусто") : FString::Printf(TEXT("Свободно мест: %d"), Free)))
			.Font(Font(11))
			.ColorAndOpacity(DimColor)
		];
	};
	Fill(LeftList, PC->GetContainer(EPeregonStorage::None), true);
	if (ShopItems.Num() > 0)
	{
		if (RightList.IsValid())
		{
			RightList->ClearChildren();
			for (FName Id : ShopItems)
			{
				RightList->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))[MakeShopRow(Id)];
			}
		}
	}
	else if (Storage != EPeregonStorage::None)
	{
		Fill(RightList, PC->GetContainer(Storage), false);
	}
}

FReply SPeregonInventoryPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (APeregonPlayerController* PC = Owner.Get())
	{
		if (InKeyEvent.GetKey() == EKeys::M)
		{
			PC->ClosePanel();
			PC->OpenMap();
			return FReply::Handled();
		}
		if (PeregonPanelStyle::IsCloseKey(InKeyEvent.GetKey()))
		{
			PC->ClosePanel();
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// ---------------------------------------------------------------------------------------------
// Бумажная карта
// ---------------------------------------------------------------------------------------------
namespace PeregonMapDraw
{
const FLinearColor Paper(0.87f, 0.83f, 0.72f, 1.f);
const FLinearColor PaperDark(0.78f, 0.73f, 0.61f, 1.f);
const FLinearColor Ink(0.16f, 0.13f, 0.1f, 1.f);
const FLinearColor InkDim(0.16f, 0.13f, 0.1f, 0.45f);
const FLinearColor Road(0.78f, 0.16f, 0.1f, 1.f);
const FLinearColor RoadOther(0.55f, 0.35f, 0.3f, 0.35f);
const FLinearColor Water(0.28f, 0.48f, 0.68f, 0.85f);
const FLinearColor Pencil(0.22f, 0.24f, 0.3f, 0.85f);
const FLinearColor Forest(0.45f, 0.58f, 0.38f, 0.25f);

// Реки примерно (широта, долгота): Обь у Новосибирска, Томь от Кемерова через Юргу на север.
const rgen::GeoPoint Ob[] = {{54.90, 82.98}, {55.02, 82.93}, {55.12, 82.90}, {55.25, 82.83}, {55.40, 82.72}, {55.60, 82.55}, {55.80, 82.45}};
const rgen::GeoPoint Tom[] = {{55.30, 86.20}, {55.36, 86.05}, {55.42, 85.85}, {55.50, 85.55}, {55.60, 85.25}, {55.68, 85.05}, {55.73, 84.93},
	{55.85, 84.90}, {56.05, 84.95}};

struct FView
{
	double MinX = 0.0, MaxX = 0.0, MinY = 0.0, MaxY = 0.0;
	FVector2f Origin;
	float Scale = 1.f;

	// Мир игры: X — север, Y — восток. Экран: восток вправо, север вверх.
	FVector2f ToScreen(double X, double Y) const
	{
		return FVector2f(Origin.X + static_cast<float>((Y - MinY) * Scale), Origin.Y + static_cast<float>((MaxX - X) * Scale));
	}
};

void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& At, const FString& S, const FSlateFontInfo& Font,
	const FLinearColor& Color)
{
	FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(800.f, 40.f), FSlateLayoutTransform(At)), S, Font, ESlateDrawEffect::None, Color);
}

void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& At, const FVector2f& Size, const FLinearColor& Color)
{
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(At)), PeregonPanelStyle::White(), ESlateDrawEffect::None, Color);
}

FVector2f Measure(const FString& S, const FSlateFontInfo& Font)
{
	if (!FSlateApplication::IsInitialized())
	{
		return FVector2f(static_cast<float>(S.Len()) * 7.f, 14.f);
	}
	const auto Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, Font);
	return FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y));
}
} // namespace PeregonMapDraw

void SPeregonPaperMap::Construct(const FArguments& InArgs)
{
	Route = InArgs._Route;
	Region = InArgs._Region;
	TraveledS = InArgs._TraveledS;
	CurrentS = InArgs._CurrentS;
}

int32 SPeregonPaperMap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace PeregonMapDraw;
	const FVector2f Size(static_cast<float>(AllottedGeometry.GetLocalSize().X), static_cast<float>(AllottedGeometry.GetLocalSize().Y));
	int32 Layer = LayerId;

	// Бумага со сгибами.
	Box(OutDrawElements, Layer, AllottedGeometry, FVector2f(0.f, 0.f), Size, Paper);
	++Layer;
	for (int32 I = 1; I < 3; ++I)
	{
		Box(OutDrawElements, Layer, AllottedGeometry, FVector2f(Size.X * static_cast<float>(I) / 3.f - 1.f, 0.f), FVector2f(2.f, Size.Y), PaperDark);
	}
	Box(OutDrawElements, Layer, AllottedGeometry, FVector2f(0.f, Size.Y * 0.5f - 1.f), FVector2f(Size.X, 2.f), PaperDark);
	if (!Route || Route->Length() <= 0.0 || Route->Regions().empty())
	{
		return Layer;
	}
	const std::vector<rgen::Region>& Regions = Route->Regions();
	const int32 R = FMath::Clamp(Region, 0, static_cast<int32>(Regions.size()) - 1);
	const rgen::Region& Reg = Regions[static_cast<size_t>(R)];

	// Рамка вида: участок области + поля.
	FView View;
	View.MinX = 1e18;
	View.MinY = 1e18;
	View.MaxX = -1e18;
	View.MaxY = -1e18;
	for (double S = Reg.S0; S <= Reg.S1; S += 100.0)
	{
		const rgen::RouteSample C = Route->Sample(S);
		View.MinX = FMath::Min(View.MinX, C.X);
		View.MaxX = FMath::Max(View.MaxX, C.X);
		View.MinY = FMath::Min(View.MinY, C.Y);
		View.MaxY = FMath::Max(View.MaxY, C.Y);
	}
	const double PadM = 1500.0;
	View.MinX -= PadM;
	View.MaxX += PadM;
	View.MinY -= PadM;
	View.MaxY += PadM;
	const float Margin = 56.f;
	const float TopBand = 70.f;
	const FVector2f Avail(Size.X - 2.f * Margin, Size.Y - 2.f * Margin - TopBand);
	View.Scale = FMath::Min(Avail.X / static_cast<float>(View.MaxY - View.MinY), Avail.Y / static_cast<float>(View.MaxX - View.MinX));
	const FVector2f Used(static_cast<float>(View.MaxY - View.MinY) * View.Scale, static_cast<float>(View.MaxX - View.MinX) * View.Scale);
	View.Origin = FVector2f(Margin + (Avail.X - Used.X) * 0.5f, Margin + TopBand + (Avail.Y - Used.Y) * 0.5f);

	// Сетка через 10 км (2 км в игре).
	++Layer;
	const double Grid = 2000.0;
	for (double X = FMath::CeilToDouble(View.MinX / Grid) * Grid; X < View.MaxX; X += Grid)
	{
		const FVector2f A = View.ToScreen(X, View.MinY);
		Box(OutDrawElements, Layer, AllottedGeometry, FVector2f(A.X, A.Y), FVector2f(Used.X, 1.f), InkDim * FLinearColor(1.f, 1.f, 1.f, 0.35f));
	}
	for (double Y = FMath::CeilToDouble(View.MinY / Grid) * Grid; Y < View.MaxY; Y += Grid)
	{
		const FVector2f A = View.ToScreen(View.MaxX, Y);
		Box(OutDrawElements, Layer, AllottedGeometry, FVector2f(A.X, A.Y), FVector2f(1.f, Used.Y), InkDim * FLinearColor(1.f, 1.f, 1.f, 0.35f));
	}

	// Реки.
	++Layer;
	const rgen::GeoPoint Origin = rgen::NovosibirskKemerovoOrigin();
	auto River = [&](const rgen::GeoPoint* Points, int32 Count, const TCHAR* Name) {
		TArray<FVector2f> Line;
		for (int32 I = 0; I < Count; ++I)
		{
			double X = 0.0;
			double Y = 0.0;
			rgen::GeoToGame(Origin, Points[I].Lat, Points[I].Lon, X, Y);
			Line.Add(View.ToScreen(X, Y));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, Water, true, 3.f);
		const FVector2f Mid = Line[Count / 2];
		if (Mid.X > Margin && Mid.X < Size.X - Margin && Mid.Y > Margin + TopBand && Mid.Y < Size.Y - Margin)
		{
			Text(OutDrawElements, Layer, AllottedGeometry, Mid + FVector2f(6.f, -4.f), Name, PeregonPanelStyle::Font(11), Water);
		}
	};
	River(Ob, static_cast<int32>(UE_ARRAY_COUNT(Ob)), TEXT("р. Обь"));
	River(Tom, static_cast<int32>(UE_ARRAY_COUNT(Tom)), TEXT("р. Томь"));

	// Трасса: в области — красная, за её пределами — бледная.
	++Layer;
	auto RouteLine = [&](double S0, double S1, const FLinearColor& Color, float Thickness) {
		TArray<FVector2f> Line;
		for (double S = S0; S <= S1; S += 60.0)
		{
			const rgen::RouteSample C = Route->Sample(S);
			Line.Add(View.ToScreen(C.X, C.Y));
		}
		if (Line.Num() >= 2)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, Color, true, Thickness);
		}
	};
	RouteLine(0.0, Route->Length(), RoadOther, 3.f);
	RouteLine(Reg.S0, Reg.S1, Road, 4.f);

	// Пройденный путь карандашом и отметка «я здесь».
	++Layer;
	const double TravelEnd = FMath::Min(static_cast<double>(TraveledS), Reg.S1);
	if (TravelEnd > Reg.S0)
	{
		TArray<FVector2f> Line;
		for (double S = Reg.S0; S <= TravelEnd; S += 60.0)
		{
			const rgen::RouteSample C = Route->Sample(S);
			// Карандаш идёт чуть выше красной линии.
			Line.Add(View.ToScreen(C.X, C.Y) + FVector2f(0.f, -5.f));
		}
		if (Line.Num() >= 2)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, Pencil, true, 1.6f);
		}
	}
	if (CurrentS >= Reg.S0 && CurrentS <= Reg.S1)
	{
		const rgen::RouteSample C = Route->Sample(CurrentS);
		const FVector2f P = View.ToScreen(C.X, C.Y);
		const TArray<FVector2f> A = {P + FVector2f(-7.f, -7.f), P + FVector2f(7.f, 7.f)};
		const TArray<FVector2f> B = {P + FVector2f(-7.f, 7.f), P + FVector2f(7.f, -7.f)};
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), A, ESlateDrawEffect::None, Pencil, true, 2.2f);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), B, ESlateDrawEffect::None, Pencil, true, 2.2f);
		Text(OutDrawElements, Layer, AllottedGeometry, P + FVector2f(10.f, 4.f), TEXT("я здесь"), PeregonPanelStyle::Font(10), Pencil);
	}

	// Населённые пункты, АЗС, кафе, граница, километры.
	++Layer;
	const FSlateFontInfo TownFont = PeregonPanelStyle::Font(13, true);
	const FSlateFontInfo VillageFont = PeregonPanelStyle::Font(11);
	const FSlateFontInfo SmallFont = PeregonPanelStyle::Font(9);
	int32 LabelIndex = 0;
	for (const rgen::Poi& Poi : Route->Pois())
	{
		if (Poi.S < Reg.S0 - 200.0 || Poi.S > Reg.S1 + 200.0)
		{
			continue;
		}
		const rgen::RouteSample C = Route->Sample(Poi.S);
		const FVector2f P = View.ToScreen(C.X, C.Y);
		const FString Name = UTF8_TO_TCHAR(Poi.Name.c_str());
		// Подписи по очереди сверху и снизу от дороги — меньше налезают.
		const float Side = (LabelIndex++ % 2 == 0) ? -1.f : 1.f;
		switch (Poi.Type)
		{
		case rgen::PoiType::Town:
		case rgen::PoiType::Finish:
		{
			Box(OutDrawElements, Layer, AllottedGeometry, P - FVector2f(7.f, 7.f), FVector2f(14.f, 14.f), Ink);
			Box(OutDrawElements, Layer, AllottedGeometry, P - FVector2f(4.f, 4.f), FVector2f(8.f, 8.f), Paper);
			const FVector2f TS = Measure(Name, TownFont);
			Text(OutDrawElements, Layer, AllottedGeometry, P + FVector2f(-TS.X * 0.5f, Side < 0.f ? -TS.Y - 10.f : 10.f), Name, TownFont, Ink);
			break;
		}
		case rgen::PoiType::Village:
		{
			Box(OutDrawElements, Layer, AllottedGeometry, P - FVector2f(4.f, 4.f), FVector2f(8.f, 8.f), Ink);
			const FVector2f TS = Measure(Name, VillageFont);
			Text(OutDrawElements, Layer, AllottedGeometry, P + FVector2f(-TS.X * 0.5f, Side < 0.f ? -TS.Y - 8.f : 8.f), Name, VillageFont, Ink);
			break;
		}
		case rgen::PoiType::Fuel:
		case rgen::PoiType::Cafe:
		case rgen::PoiType::Shop:
		case rgen::PoiType::Parts:
		{
			const TCHAR* Mark = Poi.Type == rgen::PoiType::Fuel ? TEXT("АЗС")
				: (Poi.Type == rgen::PoiType::Cafe ? TEXT("К") : (Poi.Type == rgen::PoiType::Parts ? TEXT("З/Ч") : TEXT("М")));
			const FVector2f TS = Measure(Mark, SmallFont);
			const FVector2f At = P + FVector2f(-TS.X * 0.5f - 3.f, Side < 0.f ? -TS.Y - 12.f : 8.f);
			Box(OutDrawElements, Layer, AllottedGeometry, At, TS + FVector2f(6.f, 2.f), FLinearColor(0.2f, 0.32f, 0.6f, 0.9f));
			Text(OutDrawElements, Layer + 1, AllottedGeometry, At + FVector2f(3.f, 1.f), Mark, SmallFont, Paper);
			break;
		}
		case rgen::PoiType::Border:
		{
			// Граница — штрих-пунктир поперёк дороги.
			// Направление дороги на экране: север вверх, восток вправо.
			const FVector2f Dir(static_cast<float>(std::sin(C.Heading)), static_cast<float>(-std::cos(C.Heading)));
			const FVector2f Perp(-Dir.Y, Dir.X);
			for (int32 K = -6; K <= 6; K += 2)
			{
				const TArray<FVector2f> Dash = {P + Perp * (static_cast<float>(K) * 9.f), P + Perp * (static_cast<float>(K) * 9.f + 6.f)};
				FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Dash, ESlateDrawEffect::None, Ink, true, 2.f);
			}
			Text(OutDrawElements, Layer, AllottedGeometry, P + FVector2f(10.f, 18.f), TEXT("граница области"), SmallFont, InkDim);
			break;
		}
		default:
			break;
		}
	}
	for (double Km = FMath::CeilToDouble(Route->RealKmAt(Reg.S0) / 25.0) * 25.0; Km < Route->RealKmAt(Reg.S1); Km += 25.0)
	{
		const rgen::RouteSample C = Route->Sample(Route->SAtRealKm(Km));
		const FVector2f P = View.ToScreen(C.X, C.Y);
		Box(OutDrawElements, Layer, AllottedGeometry, P - FVector2f(1.5f, 1.5f), FVector2f(3.f, 3.f), Paper);
		Text(OutDrawElements, Layer, AllottedGeometry, P + FVector2f(5.f, 2.f), FString::Printf(TEXT("%d"), FMath::RoundToInt(Km)), SmallFont, Road);
	}

	// Заголовок, масштаб, роза ветров.
	++Layer;
	const FString Title = FString::Printf(TEXT("%s"), UTF8_TO_TCHAR(Reg.Name.c_str()));
	Text(OutDrawElements, Layer, AllottedGeometry, FVector2f(Margin, 22.f), Title.ToUpper(), UPeregonArtSubsystem::SlateFont(EPeregonFont::Display, 20), Ink);
	Text(OutDrawElements, Layer, AllottedGeometry, FVector2f(Margin, 56.f), TEXT("Дорожная карта · автомобильная дорога Р-255 «Сибирь»"),
		PeregonPanelStyle::Font(12), InkDim);
	const float TenKm = 2000.f * View.Scale; // 10 км = 2 км в игре
	const FVector2f ScaleAt(Size.X - Margin - TenKm, Size.Y - Margin + 16.f);
	Box(OutDrawElements, Layer, AllottedGeometry, ScaleAt, FVector2f(TenKm * 0.5f, 4.f), Ink);
	Box(OutDrawElements, Layer, AllottedGeometry, ScaleAt + FVector2f(TenKm * 0.5f, 0.f), FVector2f(TenKm * 0.5f, 4.f), Paper * 0.7f);
	Text(OutDrawElements, Layer, AllottedGeometry, ScaleAt + FVector2f(0.f, 6.f), TEXT("0        5       10 км"), SmallFont, Ink);
	const FVector2f North(Size.X - Margin - 10.f, 30.f);
	const TArray<FVector2f> Arrow = {North + FVector2f(0.f, 34.f), North, North + FVector2f(-6.f, 12.f), North, North + FVector2f(6.f, 12.f)};
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Arrow, ESlateDrawEffect::None, Ink, true, 2.f);
	Text(OutDrawElements, Layer, AllottedGeometry, North + FVector2f(-24.f, 6.f), TEXT("С"), PeregonPanelStyle::Font(12, true), Ink);
	Text(OutDrawElements, Layer, AllottedGeometry, FVector2f(Margin, Size.Y - Margin + 12.f),
		TEXT("■ город   ▪ село   АЗС — заправка   К — кафе   М — магазин   — карандашом: пройденный путь"), SmallFont, InkDim);
	return Layer;
}

void SPeregonMapPanel::Construct(const FArguments& InArgs)
{
	using namespace PeregonPanelStyle;
	Owner = InArgs._Owner;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(40.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).WidthOverride(1200.f).HeightOverride(820.f)
				[
					SNew(SPeregonPaperMap)
					.Route(InArgs._Route)
					.Region(InArgs._Region)
					.TraveledS(InArgs._TraveledS)
					.CurrentS(InArgs._CurrentS)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 10.f, 0.f, 0.f))
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("M / Esc — убрать карту"))).Font(Font(11)).ColorAndOpacity(DimColor)
			]
		]
	];
}

FReply SPeregonMapPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (APeregonPlayerController* PC = Owner.Get())
	{
		if (PeregonPanelStyle::IsCloseKey(InKeyEvent.GetKey()))
		{
			PC->ClosePanel();
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// ---------------------------------------------------------------------------------------------
// Техничка
// ---------------------------------------------------------------------------------------------
void SPeregonManualPanel::Construct(const FArguments& InArgs)
{
	using namespace PeregonPanelStyle;
	Owner = InArgs._Owner;
	// Пожелтевшая бумага, чернила, пометки карандашом.
	const FLinearColor Paper(0.86f, 0.81f, 0.68f, 0.98f);
	const FLinearColor Ink(0.14f, 0.12f, 0.1f, 1.f);
	const FLinearColor Red(0.62f, 0.1f, 0.06f, 1.f);
	const FLinearColor Amber(0.55f, 0.36f, 0.05f, 1.f);
	const FLinearColor Green(0.12f, 0.38f, 0.14f, 1.f);
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FPeregonManualLine& Line : InArgs._Lines)
	{
		const bool bHead = Line.Style == 1;
		const FLinearColor C = Line.Style == 2 ? Red : (Line.Style == 3 ? Amber : (Line.Style == 4 ? Green : Ink));
		const FString Mark = Line.Style == 2 ? TEXT("×  ") : (Line.Style == 3 ? TEXT("!  ") : (Line.Style == 4 ? TEXT("•  ") : TEXT("")));
		List->AddSlot().AutoHeight().Padding(FMargin(0.f, bHead ? 14.f : 2.f, 0.f, bHead ? 4.f : 2.f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Mark + Line.Text))
			.Font(Font(bHead ? 15 : 12, bHead))
			.ColorAndOpacity(C)
			.AutoWrapText(true)
		];
	}
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(760.f)
		.HeightOverride(820.f)
		[
			SNew(SBorder)
			.BorderImage(White())
			.BorderBackgroundColor(Paper)
			.Padding(FMargin(34.f, 26.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(InArgs._Title)).Font(Font(22, true)).ColorAndOpacity(Ink)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f, 0.f, 8.f))
				[
					SNew(STextBlock).Text(INVTEXT("Руководство по эксплуатации и ремонту · Esc — закрыть")).Font(Font(10)).ColorAndOpacity(Ink * 0.7f)
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SScrollBox) + SScrollBox::Slot()[List]
				]
			]
		]
	];
}

FReply SPeregonManualPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (APeregonPlayerController* PC = Owner.Get())
	{
		if (PeregonPanelStyle::IsCloseKey(InKeyEvent.GetKey()))
		{
			PC->ClosePanel();
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}
