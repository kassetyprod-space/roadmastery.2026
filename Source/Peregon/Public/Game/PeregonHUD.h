// Минималистичный интерфейс: точка прицела, подсказка взаимодействия, маленькая приборка в углу
// и короткие предупреждения. Подробная телеметрия — по F3.
// Вёрстка: всё меряется по шрифту и масштабируется от высоты экрана (база — 1080 px), блоки лежат
// на полупрозрачных плашках с одинаковыми отступами — ничего не «плывёт» на других разрешениях.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PeregonHUD.generated.h"

class APeregonCar;
class UFont;
struct FPeregonFocus;

UCLASS()
class PEREGON_API APeregonHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	void ToggleDetails() { bShowDetails = !bShowDetails; }
	void ToggleTasks() { bShowTasks = !bShowTasks; }

private:
	enum class EUiFont : uint8
	{
		Small,  // подписи
		Body,   // обычный текст
		Title,  // заголовки, часы
		Gear,   // передача
		Speed   // скорость
	};

	void DrawCrosshair(const FPeregonFocus* Focus, bool bFirstPerson, const TCHAR* InteractKey);
	void DrawCluster(const APeregonCar* Car);
	void DrawWarnings(const APeregonCar* Car);
	void DrawDetails(const APeregonCar* Car);
	void DrawTrip();
	void DrawToasts();
	void DrawHeld();

	// Помощники вёрстки.
	float Px(float Value) const { return Value * UiScale; }
	UFont* GetUiFont(EUiFont Kind) const;
	FVector2f Measure(const FString& Text, EUiFont Kind);
	void Label(const FString& Text, EUiFont Kind, const FLinearColor& Color, float X, float Y, float Alpha = 1.f);
	void Panel(float X, float Y, float W, float H, float Alpha = 1.f);
	void Bar(float X, float Y, float W, float Fraction, const FLinearColor& Color, float Alpha = 1.f);
	void KeyCap(const FString& Key, float X, float Y, float& OutW, float& OutH);

	float UiScale = 1.f;
	float TripBottom = 0.f; // низ блока маршрута — под ним рисуется F3
	bool bShowDetails = false;
	bool bShowTasks = true;
	void DrawTasks();
};
