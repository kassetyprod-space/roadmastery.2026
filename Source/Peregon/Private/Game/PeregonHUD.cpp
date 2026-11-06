#include "Game/PeregonHUD.h"

#include "Art/PeregonArtSubsystem.h"

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
const FLinearColor Text(0.93f, 0.93f, 0.9f, 0.95f);
const FLinearColor Dim(0.93f, 0.93f, 0.9f, 0.55f);
const FLinearColor Track(1.f, 1.f, 1.f, 0.14f);
const FLinearColor Amber(1.f, 0.72f, 0.28f, 0.95f);
const FLinearColor Red(1.f, 0.34f, 0.27f, 0.95f);
const FLinearColor PanelColor(0.015f, 0.016f, 0.02f, 0.42f);
const FLinearColor Shadow(0.f, 0.f, 0.f, 0.5f);

// Базовые размеры (при высоте экрана 1080 px).
constexpr float Margin = 28.f;
constexpr float Pad = 12.f;
constexpr float Gap = 8.f;
constexpr float Baseline = 0.79f; // доля высоты строки до базовой линии (Golos/Unbounded)

FString GearName(int32 Gear)
{
	return Gear < 0 ? TEXT("R") : (Gear == 0 ? TEXT("N") : FString::FromInt(Gear));
}

// 3000 → «3 000» (узкий неразрывный пробел, как принято в русском).
FString Thousands(int32 Value)
{
	FString Digits = FString::FromInt(FMath::Abs(Value));
	FString Out;
	for (int32 I = 0; I < Digits.Len(); ++I)
	{
		if (I > 0 && (Digits.Len() - I) % 3 == 0)
		{
			Out.AppendChar(TEXT('\x202F'));
		}
		Out.AppendChar(Digits[I]);
	}
	return Value < 0 ? TEXT("−") + Out : Out;
}
} // namespace PeregonUi

// ---------------------------------------------------------------------------------------------
// Помощники вёрстки
// ---------------------------------------------------------------------------------------------
UFont* APeregonHUD::GetUiFont(EUiFont Kind) const
{
	struct FSpec
	{
		EPeregonFont Style;
		float Size;
	};
	static const FSpec Specs[] = {{EPeregonFont::Regular, 10.f}, {EPeregonFont::Regular, 12.f}, {EPeregonFont::SemiBold, 15.f},
		{EPeregonFont::Display, 24.f}, {EPeregonFont::Display, 38.f}};
	const FSpec& Spec = Specs[static_cast<int32>(Kind)];
	if (UFont* Custom = UPeregonArtSubsystem::CanvasFont(Spec.Style, FMath::Max(6, FMath::RoundToInt(Spec.Size * UiScale))))
	{
		return Custom;
	}
	if (!GEngine)
	{
		return nullptr;
	}
	switch (Kind)
	{
	case EUiFont::Small: return GEngine->GetTinyFont();
	case EUiFont::Body: return GEngine->GetSmallFont();
	case EUiFont::Title: return GEngine->GetMediumFont();
	default: return GEngine->GetLargeFont();
	}
}

FVector2f APeregonHUD::Measure(const FString& Text, EUiFont Kind)
{
	float W = 0.f;
	float H = 0.f;
	GetTextSize(Text, W, H, GetUiFont(Kind));
	return FVector2f(W, H);
}

void APeregonHUD::Label(const FString& Text, EUiFont Kind, const FLinearColor& Color, float X, float Y, float Alpha)
{
	UFont* F = GetUiFont(Kind);
	FLinearColor Sh = PeregonUi::Shadow;
	Sh.A *= Alpha;
	FLinearColor C = Color;
	C.A *= Alpha;
	const float Off = FMath::Max(1.f, FMath::RoundToFloat(UiScale));
	DrawText(Text, Sh, FMath::RoundToFloat(X) + Off, FMath::RoundToFloat(Y) + Off, F);
	DrawText(Text, C, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), F);
}

void APeregonHUD::Panel(float X, float Y, float W, float H, float Alpha)
{
	FLinearColor C = PeregonUi::PanelColor;
	C.A *= Alpha;
	DrawRect(C, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::RoundToFloat(W), FMath::RoundToFloat(H));
}

void APeregonHUD::Bar(float X, float Y, float W, float Fraction, const FLinearColor& Color, float Alpha)
{
	const float H = FMath::Max(2.f, FMath::RoundToFloat(Px(3.f)));
	FLinearColor T = PeregonUi::Track;
	T.A *= Alpha;
	FLinearColor C = Color;
	C.A *= Alpha;
	DrawRect(T, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::RoundToFloat(W), H);
	DrawRect(C, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::RoundToFloat(W * FMath::Clamp(Fraction, 0.f, 1.f)), H);
}

void APeregonHUD::KeyCap(const FString& Key, float X, float Y, float& OutW, float& OutH)
{
	const FVector2f Size = Measure(Key, EUiFont::Small);
	OutW = Size.X + Px(10.f);
	OutH = Size.Y + Px(4.f);
	const float Line = FMath::Max(1.f, FMath::RoundToFloat(UiScale));
	DrawRect(PeregonUi::Dim, X, Y, OutW, Line);
	DrawRect(PeregonUi::Dim, X, Y + OutH - Line, OutW, Line);
	DrawRect(PeregonUi::Dim, X, Y, Line, OutH);
	DrawRect(PeregonUi::Dim, X + OutW - Line, Y, Line, OutH);
	Label(Key, EUiFont::Small, PeregonUi::Text, X + Px(5.f), Y + Px(2.f));
}

// ---------------------------------------------------------------------------------------------
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
	UiScale = FMath::Clamp(Canvas->ClipY / 1080.f, 0.75f, 3.f);
	TripBottom = Px(PeregonUi::Margin);
	DrawTrip();
	DrawToasts();
	if (UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this))
	{
		if (!Art->HasPsxMaterial())
		{
			Label(TEXT("Нет материала M_PSX2 — текстуры выключены. Перезапустите редактор (его создаёт модуль PeregonEditor)."), EUiFont::Small,
				PeregonUi::Amber, Px(PeregonUi::Margin), Canvas->ClipY - Px(PeregonUi::Margin) - Measure(TEXT("Н"), EUiFont::Small).Y);
		}
	}
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
	using namespace PeregonUi;
	const float Cx = FMath::RoundToFloat(Canvas->ClipX * 0.5f);
	const float Cy = FMath::RoundToFloat(Canvas->ClipY * 0.5f);
	const bool bFocused = Focus && Focus->IsValid();
	if (bFirstPerson || bFocused)
	{
		const float Size = FMath::RoundToFloat(Px(bFocused ? 6.f : 4.f));
		DrawRect(Shadow, Cx - Size * 0.5f + 1.f, Cy - Size * 0.5f + 1.f, Size, Size);
		DrawRect(bFocused ? Text : Dim, Cx - Size * 0.5f, Cy - Size * 0.5f, Size, Size);
	}
	if (!bFocused)
	{
		return;
	}
	// [клавиша]  действие — на плашке под прицелом.
	const FVector2f TextSize = Measure(Focus->Prompt, EUiFont::Body);
	const FVector2f KeySize = Measure(InteractKey, EUiFont::Small) + FVector2f(Px(10.f), Px(4.f));
	const float W = KeySize.X + Px(Gap) + TextSize.X + 2.f * Px(Pad);
	const float H = FMath::Max(KeySize.Y, TextSize.Y) + Px(Pad);
	const float X = Cx - W * 0.5f;
	const float Y = Cy + Px(22.f);
	Panel(X, Y, W, H);
	float KW = 0.f;
	float KH = 0.f;
	KeyCap(InteractKey, X + Px(Pad), Y + (H - KeySize.Y) * 0.5f, KW, KH);
	Label(Focus->Prompt, EUiFont::Body, Text, X + Px(Pad) + KW + Px(Gap), Y + (H - TextSize.Y) * 0.5f);
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

	// Колонки подписей и значений — по реальной ширине текста этим шрифтом, чтобы ничего не налезало.
	float LabelW = 0.f;
	for (const TCHAR* Name : {TEXT("Обороты"), TEXT("Бензин"), TEXT("Темп.")})
	{
		LabelW = FMath::Max(LabelW, Measure(Name, EUiFont::Small).X);
	}
	LabelW += Px(10.f);
	const float ValueW = Measure(TEXT("8888 л"), EUiFont::Small).X + Px(8.f);
	const float W = FMath::Max(Px(232.f), LabelW + Px(96.f) + ValueW + 2.f * Px(Pad));
	const float Inner = W - 2.f * Px(Pad);
	const FString Speed = FString::FromInt(FMath::RoundToInt(FMath::Abs(T.SpeedKmh)));
	const FString Gear = GearName(T.Gear);
	const FVector2f SpeedSize = Measure(Speed, EUiFont::Speed);
	const FVector2f GearSize = Measure(Gear, EUiFont::Gear);
	const float SmallH = Measure(TEXT("Бк"), EUiFont::Small).Y;
	const float RowH = FMath::Max(SmallH, Px(6.f));
	const float H = Px(Pad) + SpeedSize.Y + SmallH + Px(Gap) + 3.f * RowH + 2.f * Px(4.f) + Px(Pad);

	const float X = Canvas->ClipX - Px(Margin) - W;
	const float Y = Canvas->ClipY - Px(Margin) - H;
	Panel(X, Y, W, H);
	const float L = X + Px(Pad);
	const float R = X + W - Px(Pad);

	// Скорость справа, передача слева — по общей базовой линии; подписи под ними.
	float Cy = Y + Px(Pad);
	const float Base = Cy + SpeedSize.Y * Baseline;
	Label(Speed, EUiFont::Speed, Text, R - SpeedSize.X, Cy);
	const FLinearColor GearColor = T.bGearGrind ? Red : Text;
	Label(Gear, EUiFont::Gear, GearColor, L, Base - GearSize.Y * Baseline);
	Cy += SpeedSize.Y;
	const FString Unit = TEXT("км/ч");
	Label(Unit, EUiFont::Small, Dim, R - Measure(Unit, EUiFont::Small).X, Cy);
	Label(Move->IsAutoGearbox() ? TEXT("автомат") : TEXT("механика"), EUiFont::Small, Dim, L, Cy);
	Cy += SmallH + Px(Gap);

	// Строки: подпись | полоса | значение.
	auto Row = [&](const TCHAR* Name, float Fraction, const FLinearColor& Color, const FString& Value, float Redline) {
		Label(Name, EUiFont::Small, Dim, L, Cy);
		const float BarX = L + LabelW;
		const float BarW = Inner - LabelW - ValueW;
		const float BarY = Cy + RowH * 0.5f - Px(1.5f);
		Bar(BarX, BarY, BarW, Fraction, Color);
		if (Redline > 0.f)
		{
			DrawRect(Red, FMath::RoundToFloat(BarX + BarW * Redline), BarY - Px(2.f), FMath::Max(1.f, Px(1.f)), Px(7.f));
		}
		Label(Value, EUiFont::Small, Text, R - Measure(Value, EUiFont::Small).X, Cy);
		Cy += RowH + Px(4.f);
	};
	Row(TEXT("Обороты"), T.Rpm / 7000.f, T.Rpm > 6000.f ? Red : Text, FString::FromInt(FMath::RoundToInt(T.Rpm / 50.f) * 50), 6000.f / 7000.f);
	const float FuelFrac = T.FuelCapacityL > 0.f ? T.FuelL / T.FuelCapacityL : 0.f;
	Row(TEXT("Бензин"), FuelFrac, T.FuelL < 4.f ? Amber : Text, FString::Printf(TEXT("%d л"), FMath::RoundToInt(T.FuelL)), 0.f);
	Row(TEXT("Темп."), (T.EngineTempC - 40.f) / 90.f, T.EngineTempC > 105.f ? Red : Text, FString::Printf(TEXT("%d°"), FMath::RoundToInt(T.EngineTempC)),
		0.f);
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

	// Снизу по центру, каждая строка — на своей плашке, с цветной меткой слева.
	float Bottom = Canvas->ClipY - Px(Margin);
	for (int32 I = Lines.Num() - 1; I >= 0; --I)
	{
		const FVector2f Size = Measure(Lines[I].Key, EUiFont::Body);
		const float W = Size.X + 2.f * Px(Pad) + Px(6.f);
		const float H = Size.Y + Px(Pad);
		const float X = (Canvas->ClipX - W) * 0.5f;
		const float Y = Bottom - H;
		Panel(X, Y, W, H);
		DrawRect(Lines[I].Value, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::Max(2.f, Px(3.f)), FMath::RoundToFloat(H));
		Label(Lines[I].Key, EUiFont::Body, Lines[I].Value, X + Px(Pad) + Px(6.f), Y + (H - Size.Y) * 0.5f);
		Bottom = Y - Px(4.f);
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
	TArray<TPair<FString, FLinearColor>> Lines;
	Lines.Emplace(FString::Printf(TEXT("%.0f об/мин   расход %.1f л/100   одометр %.2f км"), T.Rpm, T.InstantLPer100Km, T.OdometerKm), Text);
	Lines.Emplace(FString::Printf(TEXT("Двигатель %.0f°C%s   сцепление %.0f%%   руль %.0f°"), T.EngineTempC, T.bFanOn ? TEXT(" [вент]") : TEXT(""),
		T.ClutchEngagement * 100.f, T.SteeringWheelDeg), Text);
	Lines.Emplace(FString::Printf(TEXT("Состояние: мотор %.0f%%  сцепление %.0f%%  радиатор %.0f%%"), T.EngineCondition * 100.f,
		T.ClutchCondition * 100.f, T.RadiatorCondition * 100.f), Text);
	static const TCHAR* Names[] = {TEXT("ПЛ"), TEXT("ПП"), TEXT("ЗЛ"), TEXT("ЗП")};
	for (int32 I = 0; I < T.TreadMm.Num() && I < 4; ++I)
	{
		Lines.Emplace(FString::Printf(TEXT("%s  нагрузка %4.0f Н  скольж. %+.2f  увод %+5.1f°  протектор %.2f мм  колодки %.2f мм  тормоз %3.0f°C  аморт. %.0f%%"),
			Names[I], T.WheelLoadN[I], T.SlipRatio[I], T.SlipAngleDeg[I], T.TreadMm[I], T.PadMm[I], T.BrakeTempC[I], T.ShockCondition[I] * 100.f), Dim);
	}
	Lines.Emplace(FString(TEXT("W/S газ-тормоз  A/D руль  E/Q передачи  Shift сцепление  Space ручник  P стояночный  L фары")), Dim);
	Lines.Emplace(FString(TEXT("R стартер  I зажигание  G автомат/механика  V камера  C приблизить  F выйти  ЛКМ взаимодействие  Backspace на колёса  Tab вещи  M карта")), Dim);

	float W = 0.f;
	float LineH = 0.f;
	for (const TPair<FString, FLinearColor>& Line : Lines)
	{
		const FVector2f Size = Measure(Line.Key, EUiFont::Small);
		W = FMath::Max(W, Size.X);
		LineH = FMath::Max(LineH, Size.Y);
	}
	const float X = Px(Margin);
	const float Y = TripBottom + Px(Gap);
	Panel(X, Y, W + 2.f * Px(Pad), Lines.Num() * (LineH + Px(2.f)) + 2.f * Px(Pad) - Px(2.f));
	float Cy = Y + Px(Pad);
	for (const TPair<FString, FLinearColor>& Line : Lines)
	{
		Label(Line.Key, EUiFont::Small, Line.Value, X + Px(Pad), Cy);
		Cy += LineH + Px(2.f);
	}
}

void APeregonHUD::DrawTrip()
{
	using namespace PeregonUi;
	const APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();

	// Справа сверху: часы крупно, под ними день и деньги.
	if (GI)
	{
		const int32 Hours = FMath::FloorToInt(GI->TimeOfDayHours);
		const int32 Minutes = FMath::FloorToInt((GI->TimeOfDayHours - Hours) * 60.f);
		const FString Clock = FString::Printf(TEXT("%02d:%02d"), Hours, Minutes);
		const FString Sub = FString::Printf(TEXT("День %d  ·  %s ₽"), GI->Day, *Thousands(FMath::RoundToInt(GI->Money)));
		const FVector2f ClockSize = Measure(Clock, EUiFont::Title);
		const FVector2f SubSize = Measure(Sub, EUiFont::Small);
		const float W = FMath::Max(ClockSize.X, SubSize.X) + 2.f * Px(Pad);
		const float H = ClockSize.Y + SubSize.Y + 2.f * Px(Pad);
		const float X = Canvas->ClipX - Px(Margin) - W;
		const float Y = Px(Margin);
		Panel(X, Y, W, H);
		Label(Clock, EUiFont::Title, Text, X + W - Px(Pad) - ClockSize.X, Y + Px(Pad));
		Label(Sub, EUiFont::Small, Dim, X + W - Px(Pad) - SubSize.X, Y + Px(Pad) + ClockSize.Y);
	}

	// Слева сверху: где мы, что впереди и полоса маршрута с отметками остановок.
	const APeregonRoadWorld* World = GM ? GM->GetRoadWorld() : nullptr;
	if (!World || GM->GetMode() != EPeregonMode::Road)
	{
		return;
	}
	const double S = World->GetTrackedS();
	const rgen::Route& Route = World->GetRoute();
	const FString Place = World->GetPlaceName(S);
	FString Next;
	if (const rgen::Poi* Poi = Route.NextPoi(S))
	{
		Next = FString::Printf(TEXT("→  %s  ·  %.1f км"), UTF8_TO_TCHAR(Poi->Name.c_str()), (Poi->S - S) / 1000.0);
	}
	const FVector2f PlaceSize = Measure(Place, EUiFont::Title);
	const FVector2f NextSize = Next.IsEmpty() ? FVector2f::ZeroVector : Measure(Next, EUiFont::Body);
	const float BarW = FMath::Max(Px(240.f), FMath::Max(PlaceSize.X, NextSize.X));
	const float W = BarW + 2.f * Px(Pad);
	const float H = Px(Pad) + PlaceSize.Y + NextSize.Y + Px(Gap) + Px(8.f) + Px(Pad);
	const float X = Px(Margin);
	const float Y = Px(Margin);
	Panel(X, Y, W, H);
	float Cy = Y + Px(Pad);
	Label(Place, EUiFont::Title, Text, X + Px(Pad), Cy);
	Cy += PlaceSize.Y;
	if (!Next.IsEmpty())
	{
		Label(Next, EUiFont::Body, Dim, X + Px(Pad), Cy);
		Cy += NextSize.Y;
	}
	Cy += Px(Gap) + Px(2.f);
	const float Progress = static_cast<float>(FMath::Clamp(S / Route.Length(), 0.0, 1.0));
	Bar(X + Px(Pad), Cy, BarW, Progress, Text);
	for (const rgen::Poi& Poi : Route.Pois())
	{
		const float Tx = X + Px(Pad) + BarW * static_cast<float>(Poi.S / Route.Length());
		const bool bService = Poi.Type == rgen::PoiType::Fuel || Poi.Type == rgen::PoiType::Cafe;
		DrawRect(bService ? Amber : Dim, FMath::RoundToFloat(Tx), FMath::RoundToFloat(Cy - Px(2.5f)), FMath::Max(1.f, Px(2.f)), FMath::RoundToFloat(Px(8.f)));
	}
	TripBottom = Y + H;
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
	float Y = Canvas->ClipY * 0.22f;
	for (const FPeregonToast& Toast : GM->GetToasts())
	{
		const float Left = static_cast<float>(Toast.Until - Now);
		const float Alpha = FMath::Clamp(Left / 0.6f, 0.f, 1.f) * FMath::Clamp(static_cast<float>(Toast.Duration - Left) / 0.25f, 0.f, 1.f);
		const FVector2f Size = Measure(Toast.Text, EUiFont::Title);
		const float W = Size.X + 2.f * Px(Pad + 4.f);
		const float H = Size.Y + Px(Pad);
		const float X = (Canvas->ClipX - W) * 0.5f;
		Panel(X, Y, W, H, Alpha);
		Label(Toast.Text, EUiFont::Title, Text, X + Px(Pad + 4.f), Y + (H - Size.Y) * 0.5f, Alpha);
		Y += H + Px(6.f);
	}
}
