// Игровые окна поверх мира (игра на паузе): инвентарь с бардачком/багажником, магазин, бумажная карта.
// Чистый Slate, в стиле главного меню.
#pragma once

#include "CoreMinimal.h"
#include "Game/PeregonInventory.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class APeregonPlayerController;
class SVerticalBox;
class STextBlock;

namespace rgen
{
class Route;
}

// Инвентарь: карманы слева, справа — бардачок/багажник (перекладывание) или прилавок магазина.
class PEREGON_API SPeregonInventoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPeregonInventoryPanel) : _Storage(EPeregonStorage::None) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APeregonPlayerController>, Owner)
		SLATE_ARGUMENT(EPeregonStorage, Storage)
		SLATE_ARGUMENT(TArray<FName>, ShopItems)  // не пусто — режим магазина
		SLATE_ARGUMENT(FString, ShopTitle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TSharedRef<SWidget> MakeItemRow(const FPeregonItemStack& Stack, bool bPockets, int32 Index);
	TSharedRef<SWidget> MakeShopRow(FName Id);
	TSharedRef<SWidget> MakeSmallButton(const FText& Label, TFunction<void()> OnClick, bool bEnabled = true);

	TWeakObjectPtr<APeregonPlayerController> Owner;
	EPeregonStorage Storage = EPeregonStorage::None;
	TArray<FName> ShopItems;
	FString ShopTitle;
	TSharedPtr<SVerticalBox> LeftList;
	TSharedPtr<SVerticalBox> RightList;
	TSharedPtr<STextBlock> MoneyText;
};

// Бумажная дорожная карта области: трасса, сёла, АЗС, реки, пройденный путь карандашом.
class PEREGON_API SPeregonPaperMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPeregonPaperMap) : _Route(nullptr), _Region(0), _TraveledS(0.f), _CurrentS(0.f) {}
		SLATE_ARGUMENT(const rgen::Route*, Route)
		SLATE_ARGUMENT(int32, Region)
		SLATE_ARGUMENT(float, TraveledS)
		SLATE_ARGUMENT(float, CurrentS)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1200.0, 820.0); }

private:
	const rgen::Route* Route = nullptr;
	int32 Region = 0;
	float TraveledS = 0.f;
	float CurrentS = 0.f;
};

// Окно с картой: сама карта + подсказка закрытия.
class PEREGON_API SPeregonMapPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPeregonMapPanel) : _Route(nullptr), _Region(0), _TraveledS(0.f), _CurrentS(0.f) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APeregonPlayerController>, Owner)
		SLATE_ARGUMENT(const rgen::Route*, Route)
		SLATE_ARGUMENT(int32, Region)
		SLATE_ARGUMENT(float, TraveledS)
		SLATE_ARGUMENT(float, CurrentS)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TWeakObjectPtr<APeregonPlayerController> Owner;
};
