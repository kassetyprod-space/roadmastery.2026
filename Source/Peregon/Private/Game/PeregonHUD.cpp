#include "Game/PeregonHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"

namespace PeregonHud
{
FString GearName(int32 Gear)
{
	return Gear < 0 ? TEXT("R") : (Gear == 0 ? TEXT("N") : FString::FromInt(Gear));
}

FLinearColor ConditionColor(float Value)
{
	return Value > 0.6f ? FLinearColor(0.7f, 1.f, 0.7f) : (Value > 0.3f ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(1.f, 0.3f, 0.25f));
}
} // namespace PeregonHud

void APeregonHUD::DrawHUD()
{
	Super::DrawHUD();

	const APeregonCar* Car = Cast<APeregonCar>(GetOwningPawn());
	if (!Car || !Car->GetVehicleMovement() || !Canvas)
	{
		return;
	}
	using namespace PeregonHud;
	const UPeregonVehicleMovementComponent* Move = Car->GetVehicleMovement();
	const FPeregonTelemetry T = Move->GetTelemetry();
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;

	const float X = 24.f;
	float Y = Canvas->SizeY - 250.f;
	const float Line = 22.f;
	auto Print = [&](const FString& Text, const FLinearColor& Color = FLinearColor::White) {
		DrawText(Text, Color, X, Y, Font, 1.f);
		Y += Line;
	};

	Print(FString::Printf(TEXT("%3.0f км/ч   %s   %4.0f об/мин"), FMath::Abs(T.SpeedKmh), *GearName(T.Gear), T.Rpm),
		T.Rpm > 6000.f ? FLinearColor(1.f, 0.3f, 0.25f) : FLinearColor::White);
	Print(FString::Printf(TEXT("Бензин %.1f / %.0f л   расход %.1f л/100   одометр %.1f км"),
		T.FuelL, T.FuelCapacityL, T.InstantLPer100Km, T.OdometerKm),
		T.FuelL < 5.f ? FLinearColor(1.f, 0.6f, 0.2f) : FLinearColor::White);
	Print(FString::Printf(TEXT("Двигатель %.0f°C%s   сцепление %.0f%%"), T.EngineTempC, T.bFanOn ? TEXT(" [вентилятор]") : TEXT(""), T.ClutchEngagement * 100.f),
		T.EngineTempC > 105.f ? FLinearColor(1.f, 0.3f, 0.25f) : FLinearColor::White);
	Print(FString::Printf(TEXT("Мотор %.0f%%  Сцепление %.0f%%  Радиатор %.0f%%"),
		T.EngineCondition * 100.f, T.ClutchCondition * 100.f, T.RadiatorCondition * 100.f),
		ConditionColor(FMath::Min3(T.EngineCondition, T.ClutchCondition, T.RadiatorCondition)));

	static const TCHAR* WheelNames[] = {TEXT("ПЛ"), TEXT("ПП"), TEXT("ЗЛ"), TEXT("ЗП")};
	FString Wheels;
	for (int32 I = 0; I < T.TreadMm.Num() && I < 4; ++I)
	{
		Wheels += FString::Printf(TEXT("%s: протектор %.1f мм, колодки %.1f мм   "), WheelNames[I], T.TreadMm[I], T.PadMm[I]);
	}
	Print(Wheels, FLinearColor(0.8f, 0.8f, 0.8f));

	if (!T.bEngineRunning)
	{
		Print(TEXT("Мотор заглушен — держи R (стартер)"), FLinearColor(1.f, 0.85f, 0.3f));
	}
	if (T.bGearGrind)
	{
		Print(TEXT("ХРУСТ! Выжми сцепление (Shift)"), FLinearColor(1.f, 0.3f, 0.25f));
	}
	Print(FString::Printf(TEXT("КПП: %s (G)   W/S газ/тормоз   A/D руль   Q/E передачи   Space ручник   C камера   Backspace — на колёса"),
		Move->IsAutoGearbox() ? TEXT("автомат") : TEXT("механика")), FLinearColor(0.6f, 0.6f, 0.6f));
}
