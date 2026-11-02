// Минималистичный интерфейс: точка прицела, подсказка взаимодействия, маленькая приборка в углу
// и короткие предупреждения. Подробная телеметрия — по F3.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PeregonHUD.generated.h"

class APeregonCar;
struct FPeregonFocus;

UCLASS()
class PEREGON_API APeregonHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	void ToggleDetails() { bShowDetails = !bShowDetails; }

private:
	void DrawCrosshair(const FPeregonFocus* Focus, bool bFirstPerson, const TCHAR* InteractKey);
	void DrawCluster(const APeregonCar* Car);
	void DrawWarnings(const APeregonCar* Car);
	void DrawDetails(const APeregonCar* Car);
	void DrawTextShadowed(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale = 1.f);

	bool bShowDetails = false;
};
