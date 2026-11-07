// Выбор места старта: карта России с регионами и крупными трассами. Белые регионы — можно начать
// (клик — старт в крупнейшем городе региона), серые — в разработке. После выбора карта плавно
// приближается к городу, гаснет в чёрное, показывает «Загрузка» и зовёт OnChosen; мир проявляется
// из темноты (StartCameraFade). В кооперативе все игроки появятся там же, где хост.
#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"

class UTexture2D;

DECLARE_DELEGATE_OneParam(FOnPeregonRegionChosen, int32 /*Region*/);

class PEREGON_API SPeregonRussiaMap : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SPeregonRussiaMap) {}
		SLATE_EVENT(FOnPeregonRegionChosen, OnChosen)
		SLATE_EVENT(FSimpleDelegate, OnBack)
		SLATE_EVENT(FSimpleDelegate, OnOptions)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Регионы: название, крупнейший город, доступен ли.
	static int32 RegionCount();
	static FString RegionName(int32 Region);
	static FString RegionCity(int32 Region);
	static bool IsPlayable(int32 Region);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1280.0, 720.0); }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SPeregonRussiaMap"); }

private:
	void BuildTexture();
	void PaintTexture();
	// Где на экране рисовать карту (с учётом приближения).
	FVector2f MapToScreen(const FVector2f& MapUV, const FVector2f& WidgetSize) const;
	FVector2f ScreenToMap(const FVector2f& Screen, const FVector2f& WidgetSize) const;
	int32 RegionAt(const FVector2f& MapUV) const;

	FOnPeregonRegionChosen OnChosen;
	FSimpleDelegate OnBack;
	FSimpleDelegate OnOptions;

	TObjectPtr<UTexture2D> Texture = nullptr;
	FSlateBrush Brush;
	TArray<uint8> PixelRegion; // регион каждого пикселя (255 — море/за границей)
	int32 TexW = 1024;
	int32 TexH = 560;
	int32 Hovered = -1;
	int32 PaintedHover = -2;

	// Анимация выбора.
	int32 Chosen = -1;
	float Anim = 0.f;  // секунды с момента выбора
	bool bFired = false;
};
