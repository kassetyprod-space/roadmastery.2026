// Салон «Сокола»: радио, плафон, сигнал, поворотники и аварийка, пересадка на любое место, виды камеры.
#include "Vehicle/PeregonCar.h"

#include "Art/PeregonArtSubsystem.h"
#include "Audio/PeregonAudio.h"
#include "Audio/PeregonRadio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Game/PeregonGameMode.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "World/PeregonRoadWorld.h"
#include "World/PeregonSky.h"
#include "ProceduralMeshComponent.h"

namespace PeregonCabin
{
// Глаза сидящего: водитель, переднее правое, заднее левое, заднее правое.
const FVector SeatEyes[] = {FVector(-10.f, -35.f, 74.f), FVector(-10.f, 35.f, 74.f), FVector(-78.f, -32.f, 70.f), FVector(-78.f, 32.f, 70.f)};
const TCHAR* SeatNames[] = {TEXT("за руль"), TEXT("на переднее сиденье"), TEXT("назад, слева"), TEXT("назад, справа")};
// Плафон — тот, что в модели на потолке (между передними сиденьями).
const FVector DomeLocation(-10.f, 0.f, 86.f);
const FVector RadioLocation(73.6f, 2.f, 30.f); // «Урал»: корпус в нише торпедо, лицо — заподлицо с панелью
} // namespace PeregonCabin

FVector APeregonCar::SeatEye(int32 InSeat) const
{
	return PeregonCabin::SeatEyes[FMath::Clamp(InSeat, 0, 3)];
}

FName APeregonCar::SeatDoor(int32 InSeat) const
{
	static const FName Doors[] = {FName(TEXT("DoorFL")), FName(TEXT("DoorFR")), FName(TEXT("DoorRL")), FName(TEXT("DoorRR"))};
	return Doors[FMath::Clamp(InSeat, 0, 3)];
}

void APeregonCar::BuildCabinExtras()
{
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto AddPlate = [&](const TCHAR* Name, const FVector& Location, const FVector& Size, const FPeregonLook& Look) -> UMaterialInstanceDynamic* {
		if (!Cube)
		{
			return nullptr;
		}
		UStaticMeshComponent* Plate = NewObject<UStaticMeshComponent>(this, Name);
		Plate->SetupAttachment(Body);
		Plate->SetStaticMesh(Cube);
		Plate->SetRelativeLocation(Location);
		Plate->SetRelativeScale3D(Size / 100.f);
		Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Plate->SetCastShadow(false);
		Plate->RegisterComponent();
		UMaterialInstanceDynamic* Mid = Art ? Art->MakeMaterial(this, Look) : nullptr;
		if (Mid)
		{
			Plate->SetMaterial(0, Mid);
		}
		return Mid;
	};
	// Лампа — прямо в плафоне модели (его материал светится, когда она горит): жёлтая и тусклая, как лампочка накаливания.
	DomeLight = NewObject<UPointLightComponent>(this, TEXT("DomeLight"));
	DomeLight->SetupAttachment(Body);
	DomeLight->SetRelativeLocation(PeregonCabin::DomeLocation - FVector(0.f, 0.f, 2.5f));
	DomeLight->SetIntensityUnits(ELightUnits::Candelas);
	DomeLight->SetIntensity(0.45f);
	DomeLight->SetAttenuationRadius(200.f);
	DomeLight->SetLightColor(FLinearColor(1.f, 0.72f, 0.32f));
	DomeLight->SetCastShadows(false);
	DomeLight->SetVisibility(false);
	DomeLight->RegisterComponent();

	// Магнитола: корпус и шкала (светится, когда включена).
	FPeregonLook Case(EPeregonTex::Plastic, FLinearColor(0.08f, 0.08f, 0.09f), 0.6f);
	Case.WetMask = 0.f;
	Case.SnowMask = 0.f;
	AddPlate(TEXT("RadioCase"), PeregonCabin::RadioLocation, FVector(2.4f, 18.f, 5.f), Case);
	// Ручки: громкость слева, настройка справа.
	FPeregonLook KnobLook(EPeregonTex::Chrome, FLinearColor(0.7f, 0.7f, 0.72f), 0.3f, 1.f);
	KnobLook.WetMask = 0.f;
	KnobLook.SnowMask = 0.f;
	AddPlate(TEXT("RadioKnobL"), PeregonCabin::RadioLocation + FVector(-1.9f, -6.f, -0.4f), FVector(1.4f, 2.f, 2.f), KnobLook);
	AddPlate(TEXT("RadioKnobR"), PeregonCabin::RadioLocation + FVector(-1.9f, 6.f, -0.4f), FVector(1.4f, 2.f, 2.f), KnobLook);
	// Печка управляется рычажками на панели под магнитолой (она уже есть в модели кузова) — отдельной коробки не нужно.
	FPeregonLook Dial(EPeregonTex::White, FLinearColor(0.95f, 0.6f, 0.2f), 0.4f);
	Dial.WetMask = 0.f;
	Dial.SnowMask = 0.f;
	RadioDialMaterial = AddPlate(TEXT("RadioDial"), PeregonCabin::RadioLocation + FVector(-1.25f, 0.f, 0.9f), FVector(0.2f, 8.f, 1.3f), Dial);

	Radio = NewObject<UPeregonRadio>(this, TEXT("Radio"));
	Radio->SetupAttachment(Body);
	Radio->SetRelativeLocation(PeregonCabin::RadioLocation);
	Radio->bAllowSpatialization = false;
	Radio->RegisterComponent();
	Radio->Start();

	HornSound = NewObject<UPeregonSynth>(this, TEXT("HornSound"));
	HornSound->SetupAttachment(Body);
	HornSound->SetRelativeLocation(FVector(180.f, 0.f, 20.f));
	HornSound->SetupLoop(TEXT("horn"), EPeregonProcedural::Horn);
	HornSound->SetLevel(0.f, 1.f);
	HornSound->RegisterComponent();
	HornSound->Start();
}

void APeregonCar::UpdateCabin(float DeltaSeconds)
{
	// Поворотники: 85 миганий в минуту, щелчок реле на каждое включение.
	if (BlinkMode != 0)
	{
		BlinkTime += DeltaSeconds;
		const bool bLit = FMath::Fmod(BlinkTime, 0.7f) < 0.36f;
		if (bLit != bBlinkLit)
		{
			bBlinkLit = bLit;
			PeregonAudio::PlayEvent(this, bLit ? TEXT("blinker.on") : TEXT("blinker.off"), GetActorLocation() + GetActorForwardVector() * 60.f, 0.35f, bLit ? 1.6f : 1.3f);
		}
	}
	else
	{
		bBlinkLit = false;
	}
	const bool bLeft = bBlinkLit && (BlinkMode == 1 || BlinkMode == 3);
	const bool bRight = bBlinkLit && (BlinkMode == 2 || BlinkMode == 3);
	if (BlinkerLeft)
	{
		BlinkerLeft->SetScalarParameterValue(TEXT("Emissive"), bLeft ? 8.f : 0.f);
	}
	if (BlinkerRight)
	{
		BlinkerRight->SetScalarParameterValue(TEXT("Emissive"), bRight ? 8.f : 0.f);
	}

	if (HornSound)
	{
		HornSound->SetLevel(bHorn ? 1.f : 0.f, 1.f);
	}

	// Радио: в салоне — громко и чисто, снаружи — глухо и тише с расстоянием. Шум эфира — вдали от городов.
	// Плафон «по дверям»: горит, пока открыта любая дверь.
	if (DomeMode == 0)
	{
		bool bOpenDoor = false;
		for (const FPeregonMovingPart& Moving : MovingParts)
		{
			bOpenDoor |= Moving.Tag.ToString().StartsWith(TEXT("Door")) && Moving.Alpha > 0.05f;
		}
		if (bOpenDoor != bDome)
		{
			bDome = bOpenDoor;
			if (DomeLight)
			{
				DomeLight->SetVisibility(bDome);
			}
			if (DomeMaterial)
			{
				DomeMaterial->SetScalarParameterValue(TEXT("Emissive"), bDome ? 2.f : 0.f);
			}
		}
	}

	if (Radio)
	{
		FVector Listener = GetActorLocation();
		bool bListenerInside = false;
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				Listener = PC->PlayerCameraManager->GetCameraLocation();
			}
			bListenerInside = PC->GetPawn() == this && bFirstPerson;
		}
		const float Distance = static_cast<float>(FVector::Dist(Listener, GetActorLocation()));
		const float Volume = RadioVolume * (bListenerInside ? 1.f : FMath::Clamp(1.f - Distance / 2500.f, 0.f, 1.f));
		float Static = 0.1f;
		if (!RoadWorldRef.IsValid())
		{
			for (TActorIterator<APeregonRoadWorld> It(GetWorld()); It; ++It)
			{
				RoadWorldRef = *It;
				break;
			}
		}
		if (const APeregonRoadWorld* Road = Cast<APeregonRoadWorld>(RoadWorldRef.Get()))
		{
			double Nearest = 1e9;
			const FVector P = GetActorLocation() / 100.0;
			for (const rgen::Settlement& Town : Road->GetLandscape().Settlements())
			{
				Nearest = FMath::Min(Nearest, FMath::Sqrt(FMath::Square(P.X - Town.X) + FMath::Square(P.Y - Town.Y)) - Town.Radius * (1.0 + Town.Size));
			}
			Static = 0.06f + 0.55f * FMath::SmoothStep(1500.f, 9000.f, static_cast<float>(Nearest));
			// Областная станция в чужой области — почти один шум.
			const int32 StationRegion = Radio->GetStationRegion();
			if (StationRegion >= 0 && StationRegion != Road->GetRoute().RegionAt(Road->GetTrackedS()))
			{
				Static = FMath::Max(Static, 0.85f);
			}
		}
		Radio->SetReception(Static, bListenerInside ? 0.f : 1.f, Volume);
	}
	if (RadioDialMaterial)
	{
		RadioDialMaterial->SetScalarParameterValue(TEXT("Emissive"), Radio && Radio->IsPowered() ? 2.5f : 0.f);
	}
}

void APeregonCar::SetDomeLight(bool bOn)
{
	bDome = bOn;
	if (DomeLight)
	{
		DomeLight->SetVisibility(bOn);
	}
	if (DomeMaterial)
	{
		DomeMaterial->SetScalarParameterValue(TEXT("Emissive"), bOn ? 2.f : 0.f);
	}
	PeregonAudio::PlayEvent(this, TEXT("lights.switch"), GetActorTransform().TransformPosition(PeregonCabin::DomeLocation), 0.4f, 1.4f);
}

void APeregonCar::SetBlinkers(int32 Mode)
{
	BlinkMode = Mode;
	BlinkTime = 0.f;
	bBlinkLit = false;
	PeregonAudio::PlayEvent(this, TEXT("lights.switch"), GetActorLocation(), 0.35f, 0.9f);
}

void APeregonCar::SwitchSeat(int32 NewSeat)
{
	if (FMath::Abs(Movement->GetForwardSpeed()) > 3.f)
	{
		if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
		{
			GM->Toast(TEXT("Пересесть можно, только остановившись"), 1.5f);
		}
		return;
	}
	Seat = FMath::Clamp(NewSeat, 0, 3);
	HeadBaseLocation = SeatEye(Seat);
	HeadPivot->SetRelativeLocation(HeadBaseLocation);
	HeadYaw = 0.f;
	HeadPitch = 0.f;
	if (!CanDrive())
	{
		// С пассажирского места педали отпущены, руль — сам по себе.
		ThrottleAxis = 0.f;
		BrakeAxis = 0.f;
		Movement->SetSteering(0.f);
		Movement->SetClutchPedal(0.f);
		Movement->SetHandbrake(0.f);
	}
	SetCameraMode(0);
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		GM->Toast(FString::Printf(TEXT("Пересели %s"), PeregonCabin::SeatNames[Seat]), 1.5f);
	}
}

void APeregonCar::SetCameraMode(int32 Mode)
{
	CameraMode = FMath::Clamp(Mode, 0, 2);
	SetFirstPerson(CameraMode == 0);
	CameraMode = FMath::Clamp(Mode, 0, 2);
}

void APeregonCar::OnRadioPower(const FInputActionValue&)
{
	if (!Radio)
	{
		return;
	}
	if (!Radio->IsPowered() && (!PartsEffects.bRadio || PartsState.BatteryCharge < 0.03))
	{
		if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
		{
			GM->Toast(!PartsEffects.bRadio ? TEXT("Магнитолы нет — на её месте торчат провода") : TEXT("Магнитола молчит: сел аккумулятор"), 2.f);
		}
		return;
	}
	Radio->SetPower(!Radio->IsPowered());
	PeregonAudio::PlayEvent(this, TEXT("lights.switch"), GetActorTransform().TransformPosition(PeregonCabin::RadioLocation), 0.4f, 1.1f);
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		if (!Radio->IsPowered())
		{
			GM->Toast(TEXT("Радио выключено"), 1.2f);
		}
		else if (Radio->GetTrackCount() == 0)
		{
			GM->Toast(FString::Printf(TEXT("Радио: только шум. Положите MP3 в %s"), *UPeregonRadio::GetFolder()), 4.f);
		}
		else
		{
			GM->Toast(FString::Printf(TEXT("Радио: %s"), *Radio->GetTrackName()), 2.5f);
		}
	}
}

void APeregonCar::OnRadioNext(const FInputActionValue&)
{
	if (!Radio || !Radio->IsPowered())
	{
		return;
	}
	Radio->NextTrack();
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		GM->Toast(FString::Printf(TEXT("Радио: %s"), *Radio->GetTrackName()), 2.5f);
	}
}

// O: по дверям → горит → выключен.
void APeregonCar::OnDome(const FInputActionValue&)
{
	DomeMode = (DomeMode + 1) % 3;
	SetDomeLight(DomeMode == 1);
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		static const TCHAR* Modes[] = {TEXT("Плафон: по дверям"), TEXT("Плафон: горит"), TEXT("Плафон: выключен")};
		GM->Toast(Modes[DomeMode], 1.2f);
	}
}

int32 APeregonCar::WindowIndex(const UPrimitiveComponent* Part) const
{
	static const FName WindowTags[] = {FName(TEXT("WindowFL")), FName(TEXT("WindowFR")), FName(TEXT("WindowRL")), FName(TEXT("WindowRR"))};
	for (int32 K = 0; K < 4; ++K)
	{
		if (Part && Part->ComponentHasTag(WindowTags[K]))
		{
			return K;
		}
	}
	return -1;
}

void APeregonCar::OnWheel(const FInputActionValue& Value)
{
	// Колесо мыши по тому, на что смотрим: ручка стеклоподъёмника, громкость, переключение песни.
	const float Delta = Value.Get<float>();
	const UPrimitiveComponent* Part = Focus.Part.Get();
	if (!Part || FMath::IsNearlyZero(Delta) || Focus.Actor.Get() != this)
	{
		return;
	}
	APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	const int32 Win = WindowIndex(Part);
	if (Win >= 0)
	{
		const float Prev = WindowOpen[Win];
		WindowOpen[Win] = FMath::Clamp(WindowOpen[Win] - Delta * 0.1f, 0.f, 1.f);
		if (!FMath::IsNearlyEqual(Prev, WindowOpen[Win]))
		{
			PeregonAudio::PlayEvent(this, TEXT("window.crank"), Part->GetComponentLocation(), 0.3f, 1.f + 0.2f * Delta);
		}
		return;
	}
	if (Radio && Part->ComponentHasTag(TEXT("RadioVol")))
	{
		if (!Radio->IsPowered())
		{
			Radio->SetPower(true);
		}
		RadioVolume = FMath::Clamp(RadioVolume + Delta * 0.05f, 0.f, 1.f);
		if (GM)
		{
			GM->Toast(FString::Printf(TEXT("Громкость %d%%"), FMath::RoundToInt(RadioVolume * 100.f)), 0.8f);
		}
		return;
	}
	if (Radio && Part->ComponentHasTag(TEXT("RadioTune")))
	{
		if (!Radio->IsPowered())
		{
			Radio->SetPower(true);
		}
		Delta > 0.f ? Radio->NextTrack() : Radio->PrevTrack();
		if (GM)
		{
			GM->Toast(FString::Printf(TEXT("Радио: %s"), *Radio->GetTrackName()), 2.f);
		}
	}
}

void APeregonCar::UpdateGlassAndDirt(float DeltaSeconds)
{
	// Стёкла дверей ходят вниз до 36 см (в дверь).
	for (int32 K = 0; K < WindowGlass.Num() && K < 4; ++K)
	{
		if (UProceduralMeshComponent* Win = WindowGlass[K])
		{
			// Вдоль своей плоскости: вниз и наружу (стекло наклонено) — в полость двери, а не сквозь карту.
			// Ручку крутят — стекло едет плавно (около 3 с на весь ход), ручка вертится вместе с ним: 4 оборота.
			WindowShown[K] = FMath::FInterpConstantTo(WindowShown[K], WindowOpen[K], DeltaSeconds, 0.35f);
			const float Drop = 38.f * WindowShown[K];
			if (Cranks.IsValidIndex(K) && Cranks[K])
			{
				Cranks[K]->SetRelativeRotation(CrankBase[K] * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-1440.f * WindowShown[K])));
			}
			const float Side = (K == 0 || K == 2) ? -1.f : 1.f;
			Win->SetRelativeLocation(FVector(0.f, Side * 0.26f * Drop, -Drop));
		}
	}
	const FPeregonTelemetry T = Movement->GetTelemetry();
	EngineWarm = FMath::Clamp(EngineWarm + DeltaSeconds * (T.bEngineRunning ? 1.f / 240.f : -1.f / 900.f), 0.f, 1.f);
	float Rain = 0.f;
	float Temperature = 15.f;
	for (TActorIterator<APeregonSky> It(GetWorld()); It; ++It)
	{
		Rain = static_cast<float>(It->GetWeather().Rain + It->GetWeather().Snow);
		Temperature = It->GetTemperature();
		break;
	}
	OutsideTempC = Temperature;
	if (!bClimateInit)
	{
		CabinTempC = Temperature; // машина простояла на улице
		bClimateInit = true;
	}
	// Печка: греет, только когда мотор прогрет; открытые окна выстуживают салон.
	const float Open = (WindowOpen[0] + WindowOpen[1] + WindowOpen[2] + WindowOpen[3]) * 0.25f;
	const float OpenMax = FMath::Max(FMath::Max(WindowOpen[0], WindowOpen[1]), FMath::Max(WindowOpen[2], WindowOpen[3]));
	const float Heat = HeaterMode * 0.5f * EngineWarm * (PartsEffects.CoolingFactor > 0.05 ? 1.f : 0.3f);
	const float CabinTarget = FMath::Lerp(Temperature, FMath::Max(Temperature, 24.f + 4.f * (HeaterMode - 1)), Heat) + (Controller ? 1.5f : 0.f);
	const float CabinRate = 0.004f + 0.01f * Heat + 0.05f * Open;
	CabinTempC = FMath::Lerp(CabinTempC, CabinTarget, FMath::Clamp(DeltaSeconds * CabinRate, 0.f, 1.f));
	// Холод: сидеть в салоне ниже +10 °C тяжело, ниже нуля — замерзаешь.
	const float Freeze = Controller ? (1.f - FMath::SmoothStep(-10.f, 10.f, CabinTempC)) : 0.f;
	Chill = FMath::Clamp(Chill + DeltaSeconds * (Freeze * 0.01f - (1.f - Freeze) * 0.02f), 0.f, 1.f);

	// Запотевание: дыхание в холодной машине с закрытыми окнами; тёплый салон и холодное стекло потеют сильнее
	// (печка без проветривания только хуже). Чуть приоткрытое окно (от 8%) — и стёкла сохнут, печка тогда помогает.
	const float Cold = (1.f - FMath::SmoothStep(0.f, 16.f, Temperature));
	const float Humid = Rain > 0.05f ? 1.f : 0.5f;
	const float Breath = Controller ? 1.f : 0.f;
	const float Contrast = FMath::Clamp((CabinTempC - Temperature) / 20.f, 0.f, 1.5f);
	const bool bVented = OpenMax > 0.08f;
	const float Gain = Breath * Humid * (0.4f * Cold + 0.6f * Contrast) * (bVented ? 0.f : 1.f) * 0.012f * (1.f + 0.6f * HeaterMode);
	const float Dry = (bVented ? 0.01f + 0.08f * Open + 0.012f * HeaterMode * EngineWarm : 0.f) + 0.0012f;
	FogLevel = FMath::Clamp(FogLevel + DeltaSeconds * (Gain - Dry), 0.f, 1.f);

	// Грязь: пыль с гравия, брызги с мокрой и грунтовой дороги; дождь понемногу смывает.
	const float Speed = FMath::Abs(Movement->GetForwardSpeed());
	const float Rough = Movement->GetSurfaceRoughness();
	const UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	const float Wet = Art ? Art->GetWetness() : 0.f;
	DirtLevel = FMath::Clamp(DirtLevel + DeltaSeconds * (FMath::Min(Speed, 30.f) / 25.f * (0.0015f + 0.02f * Rough + 0.008f * Wet) - Rain * 0.003f), 0.f, 1.f);
	if (FMath::Abs(DirtLevel - AppliedDirt) > 0.03f || bBodyDirty)
	{
		ApplyDirt();
	}
	// Стёкла потеют от рамки к центру; в мороз по углам — белые ледяные узоры.
	const float Frost = (1.f - FMath::SmoothStep(-8.f, 0.f, Temperature));
	for (int32 B = 0; B < GlassBands.Num(); ++B)
	{
		UMaterialInstanceDynamic* Band = GlassBands[B];
		if (!Band)
		{
			continue;
		}
		static const float Lead[3] = {1.9f, 1.25f, 0.85f};
		static const float Lag[3] = {0.f, 0.15f, 0.35f};
		const float F = FMath::Clamp(FogLevel * Lead[B] - Lag[B], 0.f, 1.f);
		const float Ice = FMath::Clamp(Frost * (B == 0 ? 0.9f : (B == 1 ? 0.45f : 0.1f)) * (1.f - EngineWarm * HeaterMode * 0.5f), 0.f, 1.f);
		const FLinearColor Clean(0.42f, 0.5f, 0.52f);
		FLinearColor Tint = FMath::Lerp(Clean, FLinearColor(0.85f, 0.87f, 0.88f), F);
		Tint = FMath::Lerp(Tint, FLinearColor(0.93f, 0.96f, 1.f), Ice);
		Tint = FMath::Lerp(Tint, FLinearColor(0.45f, 0.38f, 0.3f), DirtLevel * 0.35f);
		Band->SetVectorParameterValue(TEXT("Color"), Tint);
		Band->SetScalarParameterValue(TEXT("Opacity"), FMath::Min(0.95f, 0.3f + 0.55f * F + 0.5f * Ice + 0.15f * DirtLevel));
	}
}

void APeregonCar::SetHeater(int32 Mode)
{
	HeaterMode = FMath::Clamp(Mode, 0, 2);
	PeregonAudio::PlayEvent(this, TEXT("lights.switch"), GetActorTransform().TransformPosition(FVector(74.f, 12.f, 14.f)), 0.4f, 0.9f);
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		static const TCHAR* Names[] = {TEXT("Печка выключена"), TEXT("Печка: слабо"), TEXT("Печка: сильно")};
		FString Note = Names[HeaterMode];
		if (HeaterMode > 0 && EngineWarm < 0.3f)
		{
			Note += TEXT(" — мотор ещё холодный, тепло пойдёт позже");
		}
		GM->Toast(Note, 2.f);
	}
}

void APeregonCar::OnHeater(const FInputActionValue&) { SetHeater((HeaterMode + 1) % 3); }

void APeregonCar::ApplyDirt()
{
	AppliedDirt = DirtLevel;
	bBodyDirty = false;
	const FLinearColor Mud(0.42f, 0.34f, 0.25f);
	const FLinearColor Rust(0.32f, 0.13f, 0.05f);
	const FLinearColor Hole(0.025f, 0.02f, 0.018f);
	const FLinearColor Metal(0.42f, 0.42f, 0.44f);
	const FLinearColor Primer(0.36f, 0.37f, 0.38f);
	const FLinearColor Putty(0.62f, 0.58f, 0.42f);
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	const bool bVertexPaint = Art && Art->HasWeatherMaterials();
	// Цвет панели: краска → (выгорание) → голый металл / грунт / шпаклёвка → ржавчина пятнами → дыры.
	auto PanelColor = [&](int32 Index, float Noise) -> FLinearColor {
		const vsim::parts::PanelState& P = PartsState.Panels[static_cast<size_t>(Index)];
		const FLinearColor Paint(P.Color[0], P.Color[1], P.Color[2]);
		FLinearColor C = bVertexPaint ? Paint : FLinearColor::White;
		const FLinearColor Under = P.bPrimed ? Primer : Metal;
		// Уставшая краска: тусклая и местами до грунта.
		const float PaintLeft = static_cast<float>(P.Paint);
		const float Faded = FMath::Clamp((1.f - PaintLeft) * 1.6f - (1.f - Noise) * 0.6f, 0.f, 1.f);
		C = FMath::Lerp(C, bVertexPaint ? Under : FLinearColor(0.8f, 0.8f, 0.8f), Faded);
		C = FMath::Lerp(C, C * 0.75f + FLinearColor(0.08f, 0.08f, 0.08f), (1.f - PaintLeft) * 0.4f);
		if (P.Putty > 0.01)
		{
			C = FMath::Lerp(C, Putty, FMath::Clamp(static_cast<float>(P.Putty) * 1.5f - (1.f - Noise) * 0.5f, 0.f, 1.f));
		}
		const float RustSpot = FMath::Clamp(static_cast<float>(P.Rust) * 1.8f - (1.f - Noise) * 1.1f, 0.f, 1.f);
		C = FMath::Lerp(C, Rust, RustSpot);
		const float HoleSpot = FMath::Clamp((static_cast<float>(P.Holes) - (1.f - Noise) * 0.9f) * 6.f, 0.f, 1.f);
		return FMath::Lerp(C, Hole, HoleSpot);
	};
	for (const FDirtSection& Dirty : DirtSections)
	{
		UProceduralMeshComponent* Mesh = Dirty.Mesh.Get();
		if (!Mesh)
		{
			continue;
		}
		TArray<FColor> Colors;
		Colors.SetNumUninitialized(Dirty.Vertices.Num());
		const bool bPanels = Dirty.PanelIndex.Num() == Colors.Num() && Dirty.Noise.Num() == Colors.Num();
		for (int32 K = 0; K < Colors.Num(); ++K)
		{
			const float A = FMath::Clamp(DirtLevel * Dirty.Mask[K] * 1.3f, 0.f, 0.85f);
			FLinearColor Base = FLinearColor::White;
			if (bPanels && Dirty.PanelIndex[K] < vsim::parts::PanelCount)
			{
				Base = PanelColor(Dirty.PanelIndex[K], Dirty.Noise[K]);
			}
			Colors[K] = FMath::Lerp(Base, Mud, A).ToFColor(false);
		}
		Mesh->UpdateMeshSection(Dirty.Section, Dirty.Vertices, Dirty.Normals, Dirty.UVs, Colors, TArray<FProcMeshTangent>());
	}
}
void APeregonCar::OnHornPressed(const FInputActionValue&) { bHorn = CanDrive(); }
void APeregonCar::OnHornReleased(const FInputActionValue&) { bHorn = false; }
void APeregonCar::OnBlinkLeft(const FInputActionValue&) { SetBlinkers(BlinkMode == 1 ? 0 : 1); }
void APeregonCar::OnBlinkRight(const FInputActionValue&) { SetBlinkers(BlinkMode == 2 ? 0 : 2); }
void APeregonCar::OnHazard(const FInputActionValue&) { SetBlinkers(BlinkMode == 3 ? 0 : 3); }
// Пересесть можно только в своём ряду (вперёд-назад — через улицу).
void APeregonCar::OnNextSeat(const FInputActionValue&) { SwitchSeat(Seat < 2 ? 1 - Seat : 5 - Seat); }
