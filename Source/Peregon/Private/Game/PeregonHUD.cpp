#include "Game/PeregonHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Game/PeregonGameInstance.h"
#include "Game/PeregonGameMode.h"
#include "Interaction/PeregonInteractable.h"
#include "World/PeregonRoadWorld.h"
#include "Player/PeregonCharacter.h"
#include "Vehicle/PeregonCar.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"

namespace PeregonUi
{
const FLinearColor Text(0.92f, 0.92f, 0.90f, 0.9f);
const FLinearColor Dim(0.92f, 0.92f, 0.90f, 0.45f);
const FLinearColor Track(1.f, 1.f, 1.f, 0.12f);
const FLinearColor Amber(1.f, 0.72f, 0.28f, 0.95f);
const FLinearColor Red(1.f, 0.32f, 0.25f, 0.95f);
const FLinearColor Shadow(0.f, 0.f, 0.f, 0.55f);
constexpr float Margin = 32.f;

FString GearName(int32 Gear)
{
	return Gear < 0 ? TEXT("R") : (Gear == 0 ? TEXT("N") : FString::FromInt(Gear));
}

UFont* Font(int32 Size)
{
	if (!GEngine)
	{
		return nullptr;
	}
	switch (Size)
	{
	case 0: return GEngine->GetTinyFont();
	case 1: return GEngine->GetSmallFont();
	case 2: return GEngine->GetMediumFont();
	default: return GEngine->GetLargeFont();
	}
}
} // namespace PeregonUi

void APeregonHUD::DrawTextShadowed(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* InFont, float Scale)
{
	DrawText(Text, PeregonUi::Shadow, X + 1.f, Y + 1.f, InFont, Scale);
	DrawText(Text, Color, X, Y, InFont, Scale);
}

void APeregonHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const APawn* Pawn = GetOwningPawn();
	if (!Pawn)
	{
		return; // главное меню
	}
	DrawTrip();
	DrawToasts();
	if (const APeregonCar* Car = Cast<APeregonCar>(Pawn))
	{
		DrawCrosshair(&Car->GetFocus(), Car->IsFirstPerson(), TEXT("ЛКМ"));
		DrawCluster(Car);
		DrawWarnings(Car);
		if (bShowDetails)
		{
			DrawDetails(Car);
		}
	}
	else if (const APeregonCharacter* Walker = Cast<APeregonCharacter>(Pawn))
	{
		DrawCrosshair(&Walker->GetFocus(), true, TEXT("E"));
	}
}

void APeregonHUD::DrawCrosshair(const FPeregonFocus* Focus, bool bFirstPerson, const TCHAR* InteractKey)
{
	const float Cx = Canvas->ClipX * 0.5f;
	const float Cy = Canvas->ClipY * 0.5f;
	const bool bFocused = Focus && Focus->IsValid();
	if (bFirstPerson || bFocused)
	{
		const float Size = bFocused ? 6.f : 4.f;
		DrawRect(PeregonUi::Shadow, Cx - Size * 0.5f + 1.f, Cy - Size * 0.5f + 1.f, Size, Size);
		DrawRect(bFocused ? PeregonUi::Text : PeregonUi::Dim, Cx - Size * 0.5f, Cy - Size * 0.5f, Size, Size);
	}
	if (bFocused)
	{
		const FString Prompt = FString::Printf(TEXT("%s   %s"), InteractKey, *Focus->Prompt);
		float W = 0.f, H = 0.f;
		GetTextSize(Prompt, W, H, PeregonUi::Font(1));
		DrawTextShadowed(Prompt, PeregonUi::Text, Cx - W * 0.5f, Cy + 22.f, PeregonUi::Font(1));
	}
}

void APeregonHUD::DrawCluster(const APeregonCar* Car)
{
	using namespace PeregonUi;
	const UPeregonVehicleMovementComponent* Move = Car->GetVehicleMovement();
	if (!Move)
	{
		return;
	}
	const FPeregonTelemetry T = Move->GetTelemetry();
	const float Right = Canvas->ClipX - Margin;
	const float Bottom = Canvas->ClipY - Margin;

	// Скорость крупно, передача рядом.
	const FString Speed = FString::Printf(TEXT("%d"), FMath::RoundToInt(FMath::Abs(T.SpeedKmh)));
	float SpeedW = 0.f, SpeedH = 0.f;
	GetTextSize(Speed, SpeedW, SpeedH, Font(3), 1.6f);
	const float SpeedY = Bottom - 54.f - SpeedH;
	DrawTextShadowed(Speed, Text, Right - SpeedW, SpeedY, Font(3), 1.6f);
	DrawTextShadowed(TEXT("км/ч"), Dim, Right - 34.f, SpeedY + SpeedH - 4.f, Font(0));

	const FString Gear = GearName(T.Gear);
	float GearW = 0.f, GearH = 0.f;
	GetTextSize(Gear, GearW, GearH, Font(3), 1.2f);
	const FLinearColor GearColor = T.bGearGrind ? Red : (Move->IsAutoGearbox() ? Dim : Text);
	DrawTextShadowed(Gear, GearColor, Right - SpeedW - 18.f - GearW, SpeedY + SpeedH - GearH, Font(3), 1.2f);
	if (Move->IsAutoGearbox())
	{
		DrawTextShadowed(TEXT("АВТ"), Dim, Right - SpeedW - 18.f - GearW, SpeedY + SpeedH - 2.f, Font(0));
	}

	// Тонкая полоса оборотов, красная зона с 6000.
	const float BarW = 170.f;
	const float BarX = Right - BarW;
	float Y = Bottom - 44.f;
	const float RpmFrac = FMath::Clamp(T.Rpm / 7000.f, 0.f, 1.f);
	DrawRect(Track, BarX, Y, BarW, 3.f);
	DrawRect(T.Rpm > 6000.f ? Red : Text, BarX, Y, BarW * RpmFrac, 3.f);
	DrawRect(Red, BarX + BarW * (6000.f / 7000.f), Y - 2.f, 1.f, 7.f);

	// Бензин и температура — две короткие полоски.
	auto MiniBar = [&](const TCHAR* Label, float Fraction, const FLinearColor& Color, float RowY) {
		DrawTextShadowed(Label, Dim, BarX, RowY - 5.f, Font(0));
		DrawRect(Track, BarX + 44.f, RowY, BarW - 44.f, 3.f);
		DrawRect(Color, BarX + 44.f, RowY, (BarW - 44.f) * FMath::Clamp(Fraction, 0.f, 1.f), 3.f);
	};
	const float FuelFrac = T.FuelCapacityL > 0.f ? T.FuelL / T.FuelCapacityL : 0.f;
	Y += 16.f;
	MiniBar(TEXT("БЕНЗ"), FuelFrac, T.FuelL < 4.f ? Amber : Text, Y);
	Y += 14.f;
	MiniBar(TEXT("ТЕМП"), (T.EngineTempC - 40.f) / 90.f, T.EngineTempC > 105.f ? Red : Text, Y);
}

void APeregonHUD::DrawWarnings(const APeregonCar* Car)
{
	using namespace PeregonUi;
	const UPeregonVehicleMovementComponent* Move = Car->GetVehicleMovement();
	if (!Move)
	{
		return;
	}
	const FPeregonTelemetry T = Move->GetTelemetry();
	TArray<TPair<FString, FLinearColor>> Lines;
	if (!T.bEngineRunning)
	{
		Lines.Emplace(FString(T.FuelL <= 0.f ? TEXT("Бак пуст") : TEXT("Двигатель заглушен  ·  R — стартер")), Amber);
	}
	if (Move->IsParkingBrake() && Move->GetThrottleInput() > 0.1f)
	{
		Lines.Emplace(FString(TEXT("Поднят ручник  ·  P")), Amber);
	}
	if (T.EngineTempC > 105.f)
	{
		Lines.Emplace(FString(TEXT("Перегрев двигателя")), Red);
	}
	if (T.bGearGrind)
	{
		Lines.Emplace(FString(Move->IsAutoGearbox() ? TEXT("Передача не включилась") : TEXT("Хруст — выжми сцепление (Shift)")), Red);
	}
	if (T.EngineCondition < 0.3f)
	{
		Lines.Emplace(FString(TEXT("Двигатель изношен")), Red);
	}

	float Y = Canvas->ClipY - Margin - 18.f;
	for (int32 I = Lines.Num() - 1; I >= 0; --I)
	{
		float W = 0.f, H = 0.f;
		GetTextSize(Lines[I].Key, W, H, Font(1));
		DrawTextShadowed(Lines[I].Key, Lines[I].Value, (Canvas->ClipX - W) * 0.5f, Y, Font(1));
		Y -= H + 4.f;
	}
}

void APeregonHUD::DrawDetails(const APeregonCar* Car)
{
	using namespace PeregonUi;
	const UPeregonVehicleMovementComponent* Move = Car->GetVehicleMovement();
	if (!Move)
	{
		return;
	}
	const FPeregonTelemetry T = Move->GetTelemetry();
	float Y = Margin;
	auto Line = [&](const FString& S, const FLinearColor& Color = PeregonUi::Text) {
		DrawTextShadowed(S, Color, Margin, Y, Font(1));
		Y += 16.f;
	};
	Line(FString::Printf(TEXT("%.0f об/мин   расход %.1f л/100   одометр %.2f км"), T.Rpm, T.InstantLPer100Km, T.OdometerKm));
	Line(FString::Printf(TEXT("Двигатель %.0f°C%s   сцепление %.0f%%   руль %.0f°"), T.EngineTempC, T.bFanOn ? TEXT(" [вент]") : TEXT(""),
		T.ClutchEngagement * 100.f, T.SteeringWheelDeg));
	Line(FString::Printf(TEXT("Состояние: мотор %.0f%%  сцепление %.0f%%  радиатор %.0f%%"),
		T.EngineCondition * 100.f, T.ClutchCondition * 100.f, T.RadiatorCondition * 100.f));
	static const TCHAR* Names[] = {TEXT("ПЛ"), TEXT("ПП"), TEXT("ЗЛ"), TEXT("ЗП")};
	for (int32 I = 0; I < T.TreadMm.Num() && I < 4; ++I)
	{
		Line(FString::Printf(TEXT("%s  нагрузка %4.0f Н  скольж. %+.2f  увод %+5.1f°  протектор %.2f мм  колодки %.2f мм  тормоз %3.0f°C  аморт. %.0f%%"),
			Names[I], T.WheelLoadN[I], T.SlipRatio[I], T.SlipAngleDeg[I], T.TreadMm[I], T.PadMm[I], T.BrakeTempC[I], T.ShockCondition[I] * 100.f), Dim);
	}
	Line(TEXT("W/S газ-тормоз  A/D руль  E/Q передачи  Shift сцепление  Space ручник  P стояночный  L фары"), Dim);
	Line(TEXT("R стартер  I зажигание  G автомат/механика  C камера  F выйти  ЛКМ взаимодействие  Backspace на колёса"), Dim);
}

void APeregonHUD::DrawTrip()
{
	using namespace PeregonUi;
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();

	// Справа сверху: время и деньги.
	if (GI)
	{
		const int32 Hours = FMath::FloorToInt(GI->TimeOfDayHours);
		const int32 Minutes = FMath::FloorToInt((GI->TimeOfDayHours - Hours) * 60.f);
		const FString Clock = FString::Printf(TEXT("День %d  %02d:%02d"), GI->Day, Hours, Minutes);
		const FString Money = FString::Printf(TEXT("%d ₽"), FMath::RoundToInt(GI->Money));
		float W = 0.f, H = 0.f;
		GetTextSize(Clock, W, H, Font(1));
		DrawTextShadowed(Clock, Text, Canvas->ClipX - Margin - W, Margin, Font(1));
		GetTextSize(Money, W, H, Font(1));
		DrawTextShadowed(Money, Dim, Canvas->ClipX - Margin - W, Margin + 18.f, Font(1));
	}

	// Сверху по центру: где мы и что впереди.
	const APeregonRoadWorld* World = GM ? GM->GetRoadWorld() : nullptr;
	if (!World || GM->GetMode() != EPeregonMode::Road)
	{
		return;
	}
	const double S = World->GetTrackedS();
	const rgen::Route& Route = World->GetRoute();
	FString Line = World->GetPlaceName(S);
	if (const rgen::Poi* Next = Route.NextPoi(S))
	{
		Line += FString::Printf(TEXT("   →   %s  %.1f км"), UTF8_TO_TCHAR(Next->Name.c_str()), (Next->S - S) / 1000.0);
	}
	float W = 0.f, H = 0.f;
	GetTextSize(Line, W, H, Font(1));
	DrawTextShadowed(Line, Text, (Canvas->ClipX - W) * 0.5f, Margin, Font(1));

	// Тонкая полоса прогресса маршрута.
	const float BarW = 260.f;
	const float X = (Canvas->ClipX - BarW) * 0.5f;
	const float Y = Margin + H + 6.f;
	const float Progress = static_cast<float>(FMath::Clamp(S / Route.Length(), 0.0, 1.0));
	DrawRect(Track, X, Y, BarW, 2.f);
	DrawRect(Text, X, Y, BarW * Progress, 2.f);
	for (const rgen::Poi& Poi : Route.Pois())
	{
		const float Px = X + BarW * static_cast<float>(Poi.S / Route.Length());
		const bool bService = Poi.Type == rgen::PoiType::Fuel || Poi.Type == rgen::PoiType::Cafe;
		DrawRect(bService ? Amber : Text, Px - 1.f, Y - 3.f, 2.f, 8.f);
	}
}

void APeregonHUD::DrawToasts()
{
	using namespace PeregonUi;
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	if (!GM)
	{
		return;
	}
	const double Now = GetWorld()->GetRealTimeSeconds();
	float Y = Canvas->ClipY * 0.24f;
	for (const FPeregonToast& Toast : GM->GetToasts())
	{
		const float Left = static_cast<float>(Toast.Until - Now);
		const float Alpha = FMath::Clamp(Left / 0.6f, 0.f, 1.f) * FMath::Clamp(static_cast<float>(Toast.Duration - Left) / 0.25f, 0.f, 1.f);
		float W = 0.f, H = 0.f;
		GetTextSize(Toast.Text, W, H, Font(2));
		FLinearColor C = Text;
		C.A *= Alpha;
		FLinearColor Sh = Shadow;
		Sh.A *= Alpha;
		DrawText(Toast.Text, Sh, (Canvas->ClipX - W) * 0.5f + 1.f, Y + 1.f, Font(2));
		DrawText(Toast.Text, C, (Canvas->ClipX - W) * 0.5f, Y, Font(2));
		Y += H + 6.f;
	}
}
