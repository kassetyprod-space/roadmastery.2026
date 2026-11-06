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

namespace PeregonCabin
{
// Глаза сидящего: водитель, переднее правое, заднее левое, заднее правое.
const FVector SeatEyes[] = {FVector(-22.f, -35.f, 74.f), FVector(-22.f, 35.f, 74.f), FVector(-98.f, -35.f, 72.f), FVector(-98.f, 35.f, 72.f)};
const TCHAR* SeatNames[] = {TEXT("за руль"), TEXT("на переднее сиденье"), TEXT("назад, слева"), TEXT("назад, справа")};
const FVector DomeLocation(-35.f, 0.f, 83.f);
const FVector RadioLocation(74.f, 0.f, 30.f);
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
	// Плафон на потолке и лампа в нём.
	FPeregonLook DomeLook(EPeregonTex::Plastic, FLinearColor(0.95f, 0.92f, 0.8f), 0.4f);
	DomeLook.WetMask = 0.f;
	DomeLook.SnowMask = 0.f;
	DomeMaterial = AddPlate(TEXT("DomeLamp"), PeregonCabin::DomeLocation, FVector(16.f, 10.f, 1.6f), DomeLook);
	DomeLight = NewObject<UPointLightComponent>(this, TEXT("DomeLight"));
	DomeLight->SetupAttachment(Body);
	DomeLight->SetRelativeLocation(PeregonCabin::DomeLocation - FVector(0.f, 0.f, 4.f));
	DomeLight->SetIntensityUnits(ELightUnits::Candelas);
	DomeLight->SetIntensity(0.9f);
	DomeLight->SetAttenuationRadius(240.f);
	DomeLight->SetLightColor(FLinearColor(1.f, 0.85f, 0.6f));
	DomeLight->SetCastShadows(false);
	DomeLight->SetVisibility(false);
	DomeLight->RegisterComponent();

	// Магнитола: корпус и шкала (светится, когда включена).
	FPeregonLook Case(EPeregonTex::Plastic, FLinearColor(0.08f, 0.08f, 0.09f), 0.6f);
	Case.WetMask = 0.f;
	Case.SnowMask = 0.f;
	AddPlate(TEXT("RadioCase"), PeregonCabin::RadioLocation, FVector(4.f, 18.f, 6.f), Case);
	FPeregonLook Dial(EPeregonTex::White, FLinearColor(0.95f, 0.6f, 0.2f), 0.4f);
	Dial.WetMask = 0.f;
	Dial.SnowMask = 0.f;
	RadioDialMaterial = AddPlate(TEXT("RadioDial"), PeregonCabin::RadioLocation + FVector(-2.1f, 0.f, 1.2f), FVector(0.3f, 12.f, 1.6f), Dial);

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
		const float Volume = bListenerInside ? 0.75f : 0.75f * FMath::Clamp(1.f - Distance / 2500.f, 0.f, 1.f);
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
		DomeMaterial->SetScalarParameterValue(TEXT("Emissive"), bOn ? 4.f : 0.f);
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

void APeregonCar::OnDome(const FInputActionValue&) { SetDomeLight(!bDome); }
void APeregonCar::OnHornPressed(const FInputActionValue&) { bHorn = CanDrive(); }
void APeregonCar::OnHornReleased(const FInputActionValue&) { bHorn = false; }
void APeregonCar::OnBlinkLeft(const FInputActionValue&) { SetBlinkers(BlinkMode == 1 ? 0 : 1); }
void APeregonCar::OnBlinkRight(const FInputActionValue&) { SetBlinkers(BlinkMode == 2 ? 0 : 2); }
void APeregonCar::OnHazard(const FInputActionValue&) { SetBlinkers(BlinkMode == 3 ? 0 : 3); }
void APeregonCar::OnNextSeat(const FInputActionValue&) { SwitchSeat((Seat + 1) % 4); }
