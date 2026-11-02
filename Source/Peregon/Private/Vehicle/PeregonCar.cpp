#include "Vehicle/PeregonCar.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Game/PeregonHUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/PeregonCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"

namespace PeregonCarLook
{
const FLinearColor Paint(0.32f, 0.04f, 0.04f);      // выцветшая «вишня»
const FLinearColor Dark(0.025f, 0.025f, 0.03f);
const FLinearColor Interior(0.07f, 0.065f, 0.06f);
const FLinearColor Seat(0.16f, 0.08f, 0.045f);
const FLinearColor Chrome(0.55f, 0.55f, 0.53f);
const FLinearColor EngineColor(0.10f, 0.10f, 0.11f);
const FLinearColor LampColor(0.9f, 0.85f, 0.6f);
const FLinearColor TailColor(0.55f, 0.02f, 0.02f);
const FLinearColor GaugeFace(0.85f, 0.83f, 0.75f);
const FLinearColor NeedleColor(0.9f, 0.25f, 0.05f);
const FLinearColor TireColor(0.015f, 0.015f, 0.015f);

const FName DoorFL(TEXT("DoorFL"));
const FName DoorFR(TEXT("DoorFR"));
const FName DoorRL(TEXT("DoorRL"));
const FName DoorRR(TEXT("DoorRR"));
const FName Hood(TEXT("Hood"));
const FName Trunk(TEXT("Trunk"));
const FName Glovebox(TEXT("Glovebox"));
const FName Key(TEXT("Key"));
const FName Lights(TEXT("Lights"));
const FName Handbrake(TEXT("Handbrake"));
const FName Fuel(TEXT("Fuel"));

const TCHAR* PartNoun(FName Tag)
{
	if (Tag == DoorFR) return TEXT("дверь");
	if (Tag == DoorRL || Tag == DoorRR) return TEXT("заднюю дверь");
	if (Tag == Hood) return TEXT("капот");
	if (Tag == Trunk) return TEXT("багажник");
	if (Tag == Glovebox) return TEXT("бардачок");
	return TEXT("");
}

constexpr float MaxExitSpeed = 3.f; // м/с
} // namespace PeregonCarLook

// ---------------------------------------------------------------------------------------------
// Конструктор: собираем машину из примитивов. Начало координат — центр кузова (≈ центр масс, 52 см над землёй).
// X — вперёд, Y — вправо, Z — вверх. Размеры в сантиметрах.
// ---------------------------------------------------------------------------------------------
APeregonCar::APeregonCar()
{
	using namespace PeregonCarLook;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cube = CubeFinder.Object;
	UStaticMesh* Cylinder = CylinderFinder.Object;
	UStaticMesh* Sphere = SphereFinder.Object;
	const FRotator Zero = FRotator::ZeroRotator;
	const FRotator AlongX(90.f, 0.f, 0.f); // ось цилиндра → вдоль X

	// --- Физика: один бокс нижней части кузова. Колёса — сферы-трассировки в компоненте движения.
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(205.f, 81.f, 30.f));
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Body->SetSimulatePhysics(true);
	Body->SetLinearDamping(0.01f);
	Body->SetAngularDamping(0.05f);
	// У плоского бокса крен «лёгкий»; у настоящего седана моменты инерции по крену и тангажу больше.
	Body->BodyInstance.InertiaTensorScale = FVector(1.7f, 1.15f, 1.0f);
	RootComponent = Body;

	CabinBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("CabinBlocker"));
	CabinBlocker->SetupAttachment(Body);
	CabinBlocker->SetRelativeLocation(FVector(-10.f, 0.f, 62.f));
	CabinBlocker->SetBoxExtent(FVector(100.f, 72.f, 28.f));
	CabinBlocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CabinBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	CabinBlocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CabinBlocker->BodyInstance.bAutoWeld = false;
	CabinBlocker->SetGenerateOverlapEvents(false);

	Movement = CreateDefaultSubobject<UPeregonVehicleMovementComponent>(TEXT("VehicleMovement"));

	// --- Моторный отсек и капот
	AddPart(TEXT("EngineBayFloor"), Body, Cube, FVector(150.f, 0.f, -25.f), FVector(110.f, 150.f, 4.f), Zero, Dark);
	AddPart(TEXT("FenderL"), Body, Cube, FVector(150.f, -78.f, 2.f), FVector(110.f, 6.f, 66.f), Zero, Paint);
	AddPart(TEXT("FenderR"), Body, Cube, FVector(150.f, 78.f, 2.f), FVector(110.f, 6.f, 66.f), Zero, Paint);
	AddPart(TEXT("FrontPanel"), Body, Cube, FVector(202.f, 0.f, 2.f), FVector(6.f, 150.f, 66.f), Zero, Paint);
	AddPart(TEXT("Grille"), Body, Cube, FVector(205.5f, 0.f, 10.f), FVector(2.f, 86.f, 20.f), Zero, Chrome);
	AddPart(TEXT("EngineBlock"), Body, Cube, FVector(148.f, 0.f, 0.f), FVector(60.f, 55.f, 45.f), Zero, EngineColor);
	AddPart(TEXT("Radiator"), Body, Cube, FVector(190.f, 0.f, 5.f), FVector(5.f, 110.f, 45.f), Zero, Dark);
	AddPart(TEXT("AirFilter"), Body, Cylinder, FVector(145.f, 0.f, 27.f), FVector(32.f, 32.f, 8.f), Zero, Chrome);
	AddPart(TEXT("Battery"), Body, Cube, FVector(170.f, 52.f, 0.f), FVector(18.f, 24.f, 20.f), Zero, Dark);
	AddPart(TEXT("Cowl"), Body, Cube, FVector(90.f, 0.f, 36.f), FVector(12.f, 156.f, 4.f), Zero, Paint);

	USceneComponent* HoodPivot = AddPivot(TEXT("HoodPivot"), Body, FVector(95.f, 0.f, 35.f));
	UStaticMeshComponent* HoodPanel = AddPart(TEXT("HoodPanel"), HoodPivot, Cube, FVector(55.f, 0.f, 1.5f), FVector(110.f, 156.f, 3.f), Zero, Paint);
	PeregonInteraction::MakeInteractable(HoodPanel, Hood);
	AddMovingPart(Hood, HoodPivot, FRotator(55.f, 0.f, 0.f), 1.6f);

	// --- Багажник
	AddPart(TEXT("TrunkFloor"), Body, Cube, FVector(-157.f, 0.f, -25.f), FVector(95.f, 150.f, 4.f), Zero, Dark);
	AddPart(TEXT("QuarterL"), Body, Cube, FVector(-157.f, -78.f, 2.f), FVector(95.f, 6.f, 66.f), Zero, Paint);
	AddPart(TEXT("QuarterR"), Body, Cube, FVector(-157.f, 78.f, 2.f), FVector(95.f, 6.f, 66.f), Zero, Paint);
	AddPart(TEXT("RearPanel"), Body, Cube, FVector(-203.f, 0.f, 2.f), FVector(6.f, 156.f, 66.f), Zero, Paint);
	AddPart(TEXT("SpareWheel"), Body, Cylinder, FVector(-165.f, 30.f, -18.f), FVector(55.f, 55.f, 16.f), Zero, TireColor);
	USceneComponent* TrunkPivot = AddPivot(TEXT("TrunkPivot"), Body, FVector(-110.f, 0.f, 35.f));
	UStaticMeshComponent* TrunkLid = AddPart(TEXT("TrunkLid"), TrunkPivot, Cube, FVector(-47.5f, 0.f, 1.5f), FVector(95.f, 156.f, 3.f), Zero, Paint);
	PeregonInteraction::MakeInteractable(TrunkLid, Trunk);
	AddMovingPart(Trunk, TrunkPivot, FRotator(-65.f, 0.f, 0.f), 1.6f);
	UStaticMeshComponent* FuelCap = AddPart(TEXT("FuelCap"), Body, Cube, FVector(-150.f, 81.5f, 18.f), FVector(7.f, 2.f, 7.f), Zero, Dark);
	PeregonInteraction::MakeInteractable(FuelCap, Fuel);

	// --- Бамперы, фары, фонари
	AddPart(TEXT("BumperF"), Body, Cube, FVector(210.f, 0.f, -18.f), FVector(8.f, 166.f, 9.f), Zero, Chrome);
	AddPart(TEXT("BumperR"), Body, Cube, FVector(-210.f, 0.f, -18.f), FVector(8.f, 166.f, 9.f), Zero, Chrome);
	AddPart(TEXT("LampL"), Body, Cylinder, FVector(205.5f, -55.f, 12.f), FVector(17.f, 17.f, 3.f), AlongX, LampColor);
	AddPart(TEXT("LampR"), Body, Cylinder, FVector(205.5f, 55.f, 12.f), FVector(17.f, 17.f, 3.f), AlongX, LampColor);
	AddPart(TEXT("TailL"), Body, Cube, FVector(-206.f, -60.f, 15.f), FVector(2.f, 28.f, 10.f), Zero, TailColor);
	AddPart(TEXT("TailR"), Body, Cube, FVector(-206.f, 60.f, 15.f), FVector(2.f, 28.f, 10.f), Zero, TailColor);

	HeadlightL = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightL"));
	HeadlightR = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightR"));
	for (USpotLightComponent* Lamp : {HeadlightL.Get(), HeadlightR.Get()})
	{
		Lamp->SetupAttachment(Body);
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->SetRelativeRotation(FRotator(-3.f, 0.f, 0.f));
		Lamp->SetIntensityUnits(ELightUnits::Candelas);
		Lamp->SetIntensity(9000.f);
		Lamp->SetAttenuationRadius(6000.f);
		Lamp->SetInnerConeAngle(12.f);
		Lamp->SetOuterConeAngle(30.f);
		Lamp->SetLightColor(FLinearColor(1.f, 0.92f, 0.75f));
		Lamp->SetVisibility(false);
	}
	HeadlightL->SetRelativeLocation(FVector(212.f, -55.f, 12.f));
	HeadlightR->SetRelativeLocation(FVector(212.f, 55.f, 12.f));

	// --- Каркас салона
	AddPart(TEXT("FloorPan"), Body, Cube, FVector(-7.5f, 0.f, -28.f), FVector(205.f, 156.f, 4.f), Zero, Interior);
	AddPart(TEXT("SillL"), Body, Cube, FVector(-10.f, -79.f, -25.f), FVector(205.f, 6.f, 10.f), Zero, Paint);
	AddPart(TEXT("SillR"), Body, Cube, FVector(-10.f, 79.f, -25.f), FVector(205.f, 6.f, 10.f), Zero, Paint);
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const float Y = static_cast<float>(Side);
		const TCHAR* S = Side < 0 ? TEXT("L") : TEXT("R");
		AddBeam(*FString::Printf(TEXT("PillarA%s"), S), Body, Cube, FVector(93.f, 70.f * Y, 43.f), FVector(55.f, 64.f * Y, 87.f), 6.f, Paint);
		AddBeam(*FString::Printf(TEXT("PillarB%s"), S), Body, Cube, FVector(-15.f, 74.f * Y, 43.f), FVector(-18.f, 67.f * Y, 88.f), 7.f, Paint);
		AddBeam(*FString::Printf(TEXT("PillarC%s"), S), Body, Cube, FVector(-108.f, 70.f * Y, 43.f), FVector(-75.f, 64.f * Y, 88.f), 9.f, Paint);
	}
	AddBeam(TEXT("WindshieldHeader"), Body, Cube, FVector(55.f, -64.f, 87.f), FVector(55.f, 64.f, 87.f), 5.f, Paint);
	AddBeam(TEXT("RearHeader"), Body, Cube, FVector(-75.f, -64.f, 88.f), FVector(-75.f, 64.f, 88.f), 5.f, Paint);
	AddPart(TEXT("Roof"), Body, Cube, FVector(-10.f, 0.f, 90.f), FVector(132.f, 136.f, 4.f), Zero, Paint);
	AddPart(TEXT("Headliner"), Body, Cube, FVector(-10.f, 0.f, 87.5f), FVector(128.f, 130.f, 1.f), Zero, Interior);
	AddPart(TEXT("ParcelShelf"), Body, Cube, FVector(-105.f, 0.f, 42.f), FVector(20.f, 140.f, 3.f), Zero, Interior);
	AddPart(TEXT("Mirror"), Body, Cube, FVector(57.f, 0.f, 80.f), FVector(2.f, 22.f, 6.f), Zero, Dark);

	// --- Двери: петля спереди, открываются наружу. Внутренняя ручка тоже интерактивна.
	auto MakeDoor = [&](FName Tag, float HingeX, float Side, float Length) {
		const FString Base = Tag.ToString();
		USceneComponent* Pivot = AddPivot(*(Base + TEXT("Pivot")), Body, FVector(HingeX, Side * 80.f, 0.f));
		UStaticMeshComponent* Panel = AddPart(*(Base + TEXT("Panel")), Pivot, Cube, FVector(-Length * 0.5f, 0.f, 10.f), FVector(Length, 5.f, 70.f), Zero, Paint);
		AddPart(*(Base + TEXT("Trim")), Pivot, Cube, FVector(-Length * 0.5f, -Side * 3.5f, 12.f), FVector(Length - 6.f, 2.f, 48.f), Zero, Interior);
		UStaticMeshComponent* Inner = AddPart(*(Base + TEXT("InnerHandle")), Pivot, Cube, FVector(-Length * 0.72f, -Side * 5.5f, 22.f), FVector(9.f, 3.f, 3.f), Zero, Chrome);
		UStaticMeshComponent* Outer = AddPart(*(Base + TEXT("OuterHandle")), Pivot, Cube, FVector(-Length * 0.8f, Side * 3.5f, 28.f), FVector(10.f, 2.f, 2.5f), Zero, Chrome);
		PeregonInteraction::MakeInteractable(Panel, Tag);
		PeregonInteraction::MakeInteractable(Inner, Tag);
		PeregonInteraction::MakeInteractable(Outer, Tag);
		AddMovingPart(Tag, Pivot, FRotator(0.f, -65.f * Side, 0.f), 3.f);
	};
	MakeDoor(DoorFL, 90.f, -1.f, 105.f);
	MakeDoor(DoorFR, 90.f, 1.f, 105.f);
	MakeDoor(DoorRL, -15.f, -1.f, 90.f);
	MakeDoor(DoorRR, -15.f, 1.f, 90.f);

	// --- Торпедо и приборы
	AddPart(TEXT("Dashboard"), Body, Cube, FVector(84.f, 0.f, 32.f), FVector(22.f, 148.f, 22.f), Zero, Interior);
	AddPart(TEXT("DashPad"), Body, Cube, FVector(80.f, 0.f, 44.f), FVector(24.f, 148.f, 3.f), Zero, Dark);
	AddPart(TEXT("Binnacle"), Body, Cube, FVector(73.f, -35.f, 47.f), FVector(8.f, 34.f, 13.f), Zero, Dark);

	auto MakeGauge = [&](const TCHAR* Name, const FVector& Center, float Diameter) {
		AddPart(*(FString(Name) + TEXT("Face")), Body, Cylinder, Center + FVector(0.8f, 0.f, 0.f), FVector(Diameter, Diameter, 1.f), AlongX, GaugeFace);
		USceneComponent* Needle = AddPivot(Name, Body, Center);
		AddPart(*(FString(Name) + TEXT("Arrow")), Needle, Cube, FVector(0.f, 0.f, Diameter * 0.22f), FVector(0.4f, 0.6f, Diameter * 0.44f), Zero, NeedleColor);
		return Needle;
	};
	SpeedoNeedle = MakeGauge(TEXT("SpeedoNeedle"), FVector(68.f, -43.f, 48.f), 12.f);
	TachoNeedle = MakeGauge(TEXT("TachoNeedle"), FVector(68.f, -27.f, 48.f), 12.f);
	FuelNeedle = MakeGauge(TEXT("FuelNeedle"), FVector(68.f, -38.f, 42.f), 4.5f);
	TempNeedle = MakeGauge(TEXT("TempNeedle"), FVector(68.f, -32.f, 42.f), 4.5f);

	// Рулевое колесо: обод из 12 сегментов, ось колонки смотрит на водителя.
	SteeringBaseRotation = FRotationMatrix::MakeFromX(FVector(-1.f, 0.f, 0.45f)).Rotator();
	SteeringPivot = AddPivot(TEXT("SteeringPivot"), Body, FVector(60.f, -35.f, 40.f), SteeringBaseRotation);
	for (int32 I = 0; I < 12; ++I)
	{
		const float Phi = FMath::DegreesToRadians(30.f * I);
		const FVector Tangent(0.f, -FMath::Sin(Phi), FMath::Cos(Phi));
		const FRotator SegRot = FRotationMatrix::MakeFromXY(FVector::ForwardVector, Tangent).Rotator();
		AddPart(*FString::Printf(TEXT("SteeringRim%d"), I), SteeringPivot, Cube, FVector(0.f, 18.f * FMath::Cos(Phi), 18.f * FMath::Sin(Phi)),
			FVector(2.5f, 10.f, 2.5f), SegRot, Dark);
	}
	AddPart(TEXT("SteeringSpoke"), SteeringPivot, Cube, FVector::ZeroVector, FVector(2.f, 34.f, 3.f), Zero, Dark);
	AddPart(TEXT("SteeringHub"), SteeringPivot, Cube, FVector(-1.f, 0.f, 0.f), FVector(3.f, 8.f, 8.f), Zero, Chrome);
	AddPart(TEXT("SteeringColumn"), SteeringPivot, Cylinder, FVector(-14.f, 0.f, 0.f), FVector(5.f, 5.f, 26.f), FRotator(-90.f, 0.f, 0.f), Dark);

	KeyPivot = AddPivot(TEXT("KeyPivot"), Body, FVector(68.f, -21.f, 33.f));
	UStaticMeshComponent* KeyMesh = AddPart(TEXT("KeyMesh"), KeyPivot, Cube, FVector(-2.f, 0.f, 0.f), FVector(4.f, 1.f, 5.f), Zero, Chrome);
	PeregonInteraction::MakeInteractable(KeyMesh, Key);

	UStaticMeshComponent* LightKnob = AddPart(TEXT("LightKnob"), Body, Cylinder, FVector(72.5f, -56.f, 36.f), FVector(4.f, 4.f, 3.f), AlongX, Chrome);
	PeregonInteraction::MakeInteractable(LightKnob, Lights);

	USceneComponent* GlovePivot = AddPivot(TEXT("GlovePivot"), Body, FVector(72.5f, 32.f, 23.f));
	UStaticMeshComponent* GloveLid = AddPart(TEXT("GloveLid"), GlovePivot, Cube, FVector(0.f, 0.f, 8.f), FVector(2.f, 36.f, 16.f), Zero, Interior);
	PeregonInteraction::MakeInteractable(GloveLid, Glovebox);
	AddMovingPart(Glovebox, GlovePivot, FRotator(70.f, 0.f, 0.f), 3.f);

	// --- Сиденья, тоннель, рычаги, педали
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const float Y = 35.f * Side;
		const TCHAR* S = Side < 0 ? TEXT("L") : TEXT("R");
		AddPart(*FString::Printf(TEXT("SeatBase%s"), S), Body, Cube, FVector(-8.f, Y, -12.f), FVector(52.f, 48.f, 14.f), Zero, Seat);
		AddPart(*FString::Printf(TEXT("SeatBack%s"), S), Body, Cube, FVector(-37.f, Y, 18.f), FVector(10.f, 48.f, 58.f), FRotator(12.f, 0.f, 0.f), Seat);
		AddPart(*FString::Printf(TEXT("Headrest%s"), S), Body, Cube, FVector(-43.f, Y, 52.f), FVector(8.f, 26.f, 14.f), FRotator(12.f, 0.f, 0.f), Seat);
	}
	AddPart(TEXT("RearBench"), Body, Cube, FVector(-82.f, 0.f, -12.f), FVector(45.f, 140.f, 14.f), Zero, Seat);
	AddPart(TEXT("RearBack"), Body, Cube, FVector(-104.f, 0.f, 18.f), FVector(10.f, 140.f, 55.f), FRotator(12.f, 0.f, 0.f), Seat);
	AddPart(TEXT("Tunnel"), Body, Cube, FVector(20.f, 0.f, -20.f), FVector(110.f, 22.f, 12.f), Zero, Dark);

	GearLever = AddPivot(TEXT("GearLever"), Body, FVector(32.f, 0.f, -14.f));
	AddPart(TEXT("GearStick"), GearLever, Cylinder, FVector(0.f, 0.f, 15.f), FVector(2.f, 2.f, 30.f), Zero, Chrome);
	AddPart(TEXT("GearKnob"), GearLever, Sphere, FVector(0.f, 0.f, 30.f), FVector(6.f, 6.f, 6.f), Zero, Dark);

	HandbrakeLever = AddPivot(TEXT("HandbrakeLever"), Body, FVector(-2.f, 10.f, -13.f));
	UStaticMeshComponent* HandbrakeMesh = AddPart(TEXT("HandbrakeMesh"), HandbrakeLever, Cube, FVector(-13.f, 0.f, 0.f), FVector(26.f, 3.f, 4.f), Zero, Dark);
	PeregonInteraction::MakeInteractable(HandbrakeMesh, Handbrake);

	auto MakePedal = [&](const TCHAR* Name, float Y) {
		USceneComponent* Pivot = AddPivot(Name, Body, FVector(90.f, Y, 5.f));
		AddPart(*(FString(Name) + TEXT("Arm")), Pivot, Cube, FVector(0.f, 0.f, -9.f), FVector(1.5f, 1.5f, 18.f), Zero, Dark);
		AddPart(*(FString(Name) + TEXT("Pad")), Pivot, Cube, FVector(-1.f, 0.f, -18.f), FVector(2.f, 6.f, 8.f), Zero, Chrome);
		return Pivot;
	};
	ClutchPedal = MakePedal(TEXT("ClutchPedal"), -50.f);
	BrakePedal = MakePedal(TEXT("BrakePedal"), -38.f);
	GasPedal = MakePedal(TEXT("GasPedal"), -26.f);

	// --- Колёса (позиции берём у физики)
	for (int32 I = 0; I < Movement->GetNumWheels(); ++I)
	{
		USceneComponent* Pivot = AddPivot(*FString::Printf(TEXT("WheelPivot%d"), I), Body, Movement->GetWheelAttachLocal(I));
		const float Diameter = Movement->GetWheelRadiusCm(I) * 2.f;
		const float Width = Movement->GetWheelWidthCm(I);
		AddPart(*FString::Printf(TEXT("Tire%d"), I), Pivot, Cylinder, FVector::ZeroVector, FVector(Diameter, Diameter, Width), FRotator(0.f, 0.f, 90.f), TireColor);
		// Перекладина на диске — видно вращение.
		AddPart(*FString::Printf(TEXT("Hub%d"), I), Pivot, Cube, FVector::ZeroVector, FVector(Diameter * 0.8f, Width + 2.f, 6.f), Zero, Chrome);
		WheelPivots.Add(Pivot);
		Movement->WheelVisuals.Add(Pivot);
	}

	// --- Камеры
	HeadPivot = AddPivot(TEXT("HeadPivot"), Body, FVector(-22.f, -35.f, 74.f));
	HeadBaseLocation = HeadPivot->GetRelativeLocation();
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(HeadPivot);
	FirstPersonCamera->SetFieldOfView(80.f);
	FirstPersonCamera->bUsePawnControlRotation = false;

	ChaseArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseArm"));
	ChaseArm->SetupAttachment(Body);
	ChaseArm->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
	ChaseArm->TargetArmLength = 620.f;
	ChaseArm->bUsePawnControlRotation = true;
	ChaseArm->bEnableCameraLag = true;
	ChaseArm->CameraLagSpeed = 10.f;
	ChaseArm->bDoCollisionTest = true;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(ChaseArm, USpringArmComponent::SocketName);
	ChaseCamera->SetFieldOfView(80.f);
	ChaseCamera->SetAutoActivate(false);
}

// ---------------------------------------------------------------------------------------------
// Помощники построения
// ---------------------------------------------------------------------------------------------
UStaticMeshComponent* APeregonCar::AddPart(const TCHAR* Name, USceneComponent* Parent, UStaticMesh* InMesh, const FVector& Location,
	const FVector& SizeCm, const FRotator& Rotation, const FLinearColor& Color)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(Parent);
	if (InMesh)
	{
		Part->SetStaticMesh(InMesh);
	}
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetRelativeScale3D(SizeCm / 100.f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	PartColors.Add(TPair<UStaticMeshComponent*, FLinearColor>(Part, Color));
	return Part;
}

UStaticMeshComponent* APeregonCar::AddBeam(const TCHAR* Name, USceneComponent* Parent, UStaticMesh* InMesh, const FVector& From, const FVector& To,
	float Thickness, const FLinearColor& Color)
{
	const FVector Dir = To - From;
	return AddPart(Name, Parent, InMesh, (From + To) * 0.5f, FVector(Dir.Size(), Thickness, Thickness), FRotationMatrix::MakeFromX(Dir).Rotator(), Color);
}

USceneComponent* APeregonCar::AddPivot(const TCHAR* Name, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation)
{
	USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(Name);
	Pivot->SetupAttachment(Parent);
	Pivot->SetRelativeLocationAndRotation(Location, Rotation);
	return Pivot;
}

void APeregonCar::AddMovingPart(FName Tag, USceneComponent* Pivot, const FRotator& Open, float Speed)
{
	FPeregonMovingPart Part;
	Part.Tag = Tag;
	Part.Pivot = Pivot;
	Part.Closed = Pivot->GetRelativeRotation();
	Part.Open = Part.Closed + Open;
	Part.Speed = Speed;
	MovingParts.Add(Part);
}

FPeregonMovingPart* APeregonCar::FindMovingPart(FName Tag)
{
	return MovingParts.FindByPredicate([Tag](const FPeregonMovingPart& P) { return P.Tag == Tag; });
}

const FPeregonMovingPart* APeregonCar::FindMovingPart(FName Tag) const
{
	return MovingParts.FindByPredicate([Tag](const FPeregonMovingPart& P) { return P.Tag == Tag; });
}

void APeregonCar::ToggleMovingPart(FName Tag)
{
	if (FPeregonMovingPart* Part = FindMovingPart(Tag))
	{
		Part->Target = Part->Target > 0.5f ? 0.f : 1.f;
		Part->AutoCloseTimer = 0.f;
	}
}

// ---------------------------------------------------------------------------------------------
// Жизненный цикл
// ---------------------------------------------------------------------------------------------
void APeregonCar::BeginPlay()
{
	Super::BeginPlay();
	for (const TPair<UStaticMeshComponent*, FLinearColor>& Entry : PartColors)
	{
		if (Entry.Key && Entry.Key->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* Mid = Entry.Key->CreateDynamicMaterialInstance(0))
			{
				Mid->SetVectorParameterValue(TEXT("Color"), Entry.Value);
			}
		}
	}
	HeadBaseLocation = HeadPivot->GetRelativeLocation();
	SetFirstPerson(true);
	SetHeadlights(false);
}

void APeregonCar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Movement)
	{
		return;
	}
	CockpitTime += DeltaSeconds;

	// Клавиатура — это 0 или 1. Педаль «дожимается» за долю секунды, как ногой; геймпад и так аналоговый.
	ThrottleSmoothed = FMath::FInterpConstantTo(ThrottleSmoothed, ThrottleAxis, DeltaSeconds, ThrottleAxis > ThrottleSmoothed ? 3.5f : 7.f);
	BrakeSmoothed = FMath::FInterpConstantTo(BrakeSmoothed, BrakeAxis, DeltaSeconds, BrakeAxis > BrakeSmoothed ? 5.f : 9.f);
	float Throttle = ThrottleSmoothed;
	float Brake = BrakeSmoothed;

	// Аркадный автомат: тормоз на месте включает заднюю, в задней педали меняются местами.
	if (Movement->IsAutoGearbox() && Controller)
	{
		const float Speed = Movement->GetForwardSpeed();
		const int32 Gear = Movement->GetGear();
		if (Gear == 0 && ThrottleAxis > 0.1f)
		{
			Movement->RequestGear(1);
		}
		if (Gear >= 0 && BrakeAxis > 0.5f && ThrottleAxis < 0.1f && FMath::Abs(Speed) < 0.5f)
		{
			ReverseHoldTime += DeltaSeconds;
			if (ReverseHoldTime > 0.3f)
			{
				Movement->RequestGear(-1);
				ReverseHoldTime = 0.f;
			}
		}
		else
		{
			ReverseHoldTime = 0.f;
		}
		if (Gear == -1)
		{
			if (ThrottleAxis > 0.1f && Speed > -0.5f)
			{
				Movement->RequestGear(1);
			}
			else
			{
				Swap(Throttle, Brake);
			}
		}
	}
	Movement->SetThrottle(Throttle);
	Movement->SetBrake(Brake);

	const FPeregonTelemetry T = Movement->GetTelemetry();
	UpdateMovingParts(DeltaSeconds);
	UpdateCockpit(DeltaSeconds, T);
	if (IsPlayerControlled())
	{
		UpdateCameras(DeltaSeconds, T);
		UpdateFocus();
	}
	else
	{
		Focus = FPeregonFocus();
	}
}

void APeregonCar::UpdateMovingParts(float DeltaSeconds)
{
	for (FPeregonMovingPart& Part : MovingParts)
	{
		if (Part.AutoCloseTimer > 0.f)
		{
			Part.AutoCloseTimer -= DeltaSeconds;
			if (Part.AutoCloseTimer <= 0.f)
			{
				Part.Target = 0.f;
			}
		}
		if (FMath::IsNearlyEqual(Part.Alpha, Part.Target) || !Part.Pivot)
		{
			continue;
		}
		Part.Alpha = FMath::FInterpConstantTo(Part.Alpha, Part.Target, DeltaSeconds, Part.Speed);
		const float Eased = FMath::SmoothStep(0.f, 1.f, Part.Alpha);
		Part.Pivot->SetRelativeRotation(FQuat::Slerp(Part.Closed.Quaternion(), Part.Open.Quaternion(), Eased));
	}
}

void APeregonCar::UpdateCockpit(float DeltaSeconds, const FPeregonTelemetry& T)
{
	// Руль: ~1.5 оборота от центра до упора.
	SteeringPivot->SetRelativeRotation(SteeringBaseRotation.Quaternion() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-T.SteeringWheelDeg)));

	auto SetNeedle = [](USceneComponent* Needle, float Fraction, float Sweep) {
		const float Angle = FMath::Lerp(-Sweep, Sweep, FMath::Clamp(Fraction, 0.f, 1.f));
		Needle->SetRelativeRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Angle)));
	};
	SetNeedle(SpeedoNeedle, FMath::Abs(T.SpeedKmh) / 180.f, 125.f);
	SetNeedle(TachoNeedle, T.Rpm / 8000.f, 125.f);
	SetNeedle(FuelNeedle, T.FuelCapacityL > 0.f ? T.FuelL / T.FuelCapacityL : 0.f, 60.f);
	SetNeedle(TempNeedle, (T.EngineTempC - 40.f) / 90.f, 60.f);

	// Кулиса: Н-схема (1-2 слева, 3-4 по центру, 5-R справа).
	FVector2D Tilt(0.f, 0.f); // вперёд, вбок
	switch (T.Gear)
	{
	case 1: Tilt = FVector2D(12.f, -10.f); break;
	case 2: Tilt = FVector2D(-12.f, -10.f); break;
	case 3: Tilt = FVector2D(12.f, 0.f); break;
	case 4: Tilt = FVector2D(-12.f, 0.f); break;
	case 5: Tilt = FVector2D(12.f, 10.f); break;
	case -1: Tilt = FVector2D(-12.f, 10.f); break;
	default: break;
	}
	const FRotator LeverTarget(-Tilt.X, 0.f, -Tilt.Y);
	GearLever->SetRelativeRotation(FMath::RInterpTo(GearLever->GetRelativeRotation(), LeverTarget, DeltaSeconds, 18.f));

	HandbrakeLever->SetRelativeRotation(FRotator(-25.f * Movement->GetHandbrakeInput(), 0.f, 0.f));

	const float KeyAngle = Movement->IsStarterOn() ? 70.f : (Movement->GetIgnition() ? 35.f : 0.f);
	KeyPivot->SetRelativeRotation(FRotator(0.f, 0.f, KeyAngle));

	const float ClutchInput = Movement->IsAutoGearbox() ? 0.f : Movement->GetClutchInput();
	ClutchPedal->SetRelativeRotation(FRotator(25.f * ClutchInput, 0.f, 0.f));
	BrakePedal->SetRelativeRotation(FRotator(20.f * Movement->GetBrakeInput(), 0.f, 0.f));
	GasPedal->SetRelativeRotation(FRotator(20.f * Movement->GetThrottleInput(), 0.f, 0.f));
}

void APeregonCar::UpdateCameras(float DeltaSeconds, const FPeregonTelemetry& T)
{
	const float Dt = FMath::Max(DeltaSeconds, 1.e-3f);
	const FVector Velocity = Body->GetPhysicsLinearVelocity();
	const FVector AccelWorld = (Velocity - LastVelocity) / Dt * 0.01f; // м/с²
	LastVelocity = Velocity;
	SmoothedAccel = FMath::VInterpTo(SmoothedAccel, AccelWorld, Dt, 10.f);

	if (bFirstPerson)
	{
		// Голова водителя по инерции: на торможении — вперёд, в повороте — наружу, на кочках — вверх-вниз.
		const FVector A = GetActorTransform().InverseTransformVectorNoScale(SmoothedAccel);
		FVector Target(-A.X * 0.6f, -A.Y * 0.5f, -A.Z * 0.25f);
		Target = Target.BoundToBox(FVector(-6.f, -6.f, -4.f), FVector(6.f, 6.f, 4.f));
		// Мелкая дрожь мотора: на холостых заметнее, на оборотах — частая и мелкая.
		if (T.bEngineRunning)
		{
			const float Amp = 0.06f + 0.04f * FMath::Clamp(T.Rpm / 6000.f, 0.f, 1.f);
			const float Freq = 15.f + T.Rpm / 200.f;
			Target += FVector(0.f, FMath::PerlinNoise1D(CockpitTime * Freq), FMath::PerlinNoise1D(CockpitTime * Freq + 17.f)) * Amp;
		}
		HeadOffset = FMath::VInterpTo(HeadOffset, Target, Dt, 12.f);
		HeadPivot->SetRelativeLocation(HeadBaseLocation + HeadOffset);
		FirstPersonCamera->SetRelativeRotation(FRotator(HeadPitch, HeadYaw, 0.f));
	}
	else
	{
		// Камера сама возвращается за машину, если игрок не крутит мышью.
		TimeSinceLook += Dt;
		if (Controller && TimeSinceLook > 2.f && FMath::Abs(Movement->GetForwardSpeed()) > 1.f)
		{
			const FRotator Desired(-12.f, GetActorRotation().Yaw, 0.f);
			Controller->SetControlRotation(FMath::RInterpTo(Controller->GetControlRotation(), Desired, Dt, 2.f));
		}
	}
}

void APeregonCar::UpdateFocus()
{
	const UCameraComponent* Cam = bFirstPerson ? FirstPersonCamera.Get() : ChaseCamera.Get();
	Focus = PeregonInteraction::Trace(GetWorld(), Cam->GetComponentLocation(), Cam->GetForwardVector(), bFirstPerson ? 140.f : 900.f, this);
}

void APeregonCar::SetFirstPerson(bool bEnable)
{
	bFirstPerson = bEnable;
	FirstPersonCamera->SetActive(bEnable);
	ChaseCamera->SetActive(!bEnable);
	TimeSinceLook = 10.f;
}

void APeregonCar::SetHeadlights(bool bOn)
{
	bHeadlights = bOn;
	HeadlightL->SetVisibility(bOn);
	HeadlightR->SetVisibility(bOn);
}

// ---------------------------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------------------------
FString APeregonCar::GetInteractPrompt(const UPrimitiveComponent* Part, const APawn* User) const
{
	using namespace PeregonCarLook;
	if (!Part || !Movement)
	{
		return FString();
	}
	const bool bInside = User == this;
	const bool bOnFoot = User && User->IsA<APeregonCharacter>();

	if (Part->ComponentHasTag(DoorFL))
	{
		if (bOnFoot)
		{
			return Driver ? FString() : FString(TEXT("Сесть за руль"));
		}
		if (bInside)
		{
			return FMath::Abs(Movement->GetForwardSpeed()) < MaxExitSpeed ? FString(TEXT("Выйти из машины")) : FString(TEXT("Сначала остановитесь"));
		}
	}
	for (const FName& Tag : {DoorFR, DoorRL, DoorRR, Hood, Trunk, Glovebox})
	{
		if (Part->ComponentHasTag(Tag))
		{
			const FPeregonMovingPart* Moving = FindMovingPart(Tag);
			const bool bOpen = Moving && Moving->Target > 0.5f;
			return FString(bOpen ? TEXT("Закрыть ") : TEXT("Открыть ")) + PartNoun(Tag);
		}
	}
	if (bInside)
	{
		if (Part->ComponentHasTag(Key))
		{
			return Movement->GetTelemetry().bEngineRunning ? FString(TEXT("Заглушить двигатель")) : FString(TEXT("Завести (держать)"));
		}
		if (Part->ComponentHasTag(Lights))
		{
			return bHeadlights ? FString(TEXT("Выключить фары")) : FString(TEXT("Включить фары"));
		}
		if (Part->ComponentHasTag(Handbrake))
		{
			return Movement->IsParkingBrake() ? FString(TEXT("Опустить ручник")) : FString(TEXT("Поднять ручник"));
		}
	}
	if (bOnFoot && Part->ComponentHasTag(Fuel))
	{
		const FPeregonTelemetry T = Movement->GetTelemetry();
		return FString::Printf(TEXT("Залить канистру 10 л (в баке %.0f из %.0f л)"), T.FuelL, T.FuelCapacityL);
	}
	return FString();
}

void APeregonCar::InteractStart(UPrimitiveComponent* Part, APawn* User)
{
	using namespace PeregonCarLook;
	if (!Part || !Movement)
	{
		return;
	}
	const bool bInside = User == this;
	APeregonCharacter* Walker = Cast<APeregonCharacter>(User);

	if (Part->ComponentHasTag(DoorFL))
	{
		if (Walker && !Driver)
		{
			EnterCar(Walker);
		}
		else if (bInside && FMath::Abs(Movement->GetForwardSpeed()) < MaxExitSpeed)
		{
			ExitCar();
		}
		return;
	}
	for (const FName& Tag : {DoorFR, DoorRL, DoorRR, Hood, Trunk, Glovebox})
	{
		if (Part->ComponentHasTag(Tag))
		{
			ToggleMovingPart(Tag);
			return;
		}
	}
	if (bInside)
	{
		if (Part->ComponentHasTag(Key))
		{
			if (Movement->GetTelemetry().bEngineRunning)
			{
				Movement->SetIgnition(false);
			}
			else
			{
				Movement->SetIgnition(true);
				Movement->SetStarter(true);
				bKeyCranking = true;
			}
		}
		else if (Part->ComponentHasTag(Lights))
		{
			SetHeadlights(!bHeadlights);
		}
		else if (Part->ComponentHasTag(Handbrake))
		{
			Movement->SetParkingBrake(!Movement->IsParkingBrake());
		}
		return;
	}
	if (Walker && Part->ComponentHasTag(Fuel))
	{
		Movement->Refuel(10.f);
	}
}

void APeregonCar::InteractStop(UPrimitiveComponent* Part, APawn* /*User*/)
{
	if (bKeyCranking && Part && Part->ComponentHasTag(PeregonCarLook::Key))
	{
		Movement->SetStarter(false);
		bKeyCranking = false;
	}
}

void APeregonCar::EnterCar(APeregonCharacter* Character)
{
	if (!Character || Driver)
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (!PC)
	{
		return;
	}
	Driver = Character;
	Character->HideForVehicle();
	if (FPeregonMovingPart* Door = FindMovingPart(PeregonCarLook::DoorFL))
	{
		Door->Target = 1.f;
		Door->AutoCloseTimer = 0.9f;
	}
	PC->Possess(this);
	PC->SetControlRotation(FRotator(-12.f, GetActorRotation().Yaw, 0.f));
	HeadYaw = 0.f;
	HeadPitch = 0.f;
	SetFirstPerson(true);
}

void APeregonCar::ExitCar()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!Driver || !PC)
	{
		return;
	}
	APeregonCharacter* Walker = Driver;
	Driver = nullptr;
	if (FPeregonMovingPart* Door = FindMovingPart(PeregonCarLook::DoorFL))
	{
		Door->Target = 1.f;
		Door->AutoCloseTimer = 0.9f;
	}
	// Выходим слева от водительской двери, лицом по ходу машины.
	const FVector ExitLocation = GetActorTransform().TransformPosition(FVector(-25.f, -150.f, 60.f));
	const FRotator ExitRotation(0.f, GetActorRotation().Yaw, 0.f);
	Walker->ShowAt(ExitLocation, ExitRotation);
	PC->Possess(Walker);
	PC->SetControlRotation(ExitRotation);
}

// ---------------------------------------------------------------------------------------------
// Ввод
// ---------------------------------------------------------------------------------------------
void APeregonCar::BuildInput()
{
	if (Mapping)
	{
		return;
	}
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type) {
		UInputAction* Action = NewObject<UInputAction>(this, FName(Name));
		Action->ValueType = Type;
		return Action;
	};
	ThrottleAction = MakeAction(TEXT("IA_Car_Throttle"), EInputActionValueType::Axis1D);
	BrakeAction = MakeAction(TEXT("IA_Car_Brake"), EInputActionValueType::Axis1D);
	SteerAction = MakeAction(TEXT("IA_Car_Steer"), EInputActionValueType::Axis1D);
	ClutchAction = MakeAction(TEXT("IA_Car_Clutch"), EInputActionValueType::Axis1D);
	HandbrakeAction = MakeAction(TEXT("IA_Car_Handbrake"), EInputActionValueType::Axis1D);
	LookAction = MakeAction(TEXT("IA_Car_Look"), EInputActionValueType::Axis2D);
	StarterAction = MakeAction(TEXT("IA_Car_Starter"), EInputActionValueType::Boolean);
	ShiftUpAction = MakeAction(TEXT("IA_Car_ShiftUp"), EInputActionValueType::Boolean);
	ShiftDownAction = MakeAction(TEXT("IA_Car_ShiftDown"), EInputActionValueType::Boolean);
	IgnitionAction = MakeAction(TEXT("IA_Car_Ignition"), EInputActionValueType::Boolean);
	GearboxModeAction = MakeAction(TEXT("IA_Car_GearboxMode"), EInputActionValueType::Boolean);
	CameraAction = MakeAction(TEXT("IA_Car_Camera"), EInputActionValueType::Boolean);
	ResetAction = MakeAction(TEXT("IA_Car_Reset"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(TEXT("IA_Car_Interact"), EInputActionValueType::Boolean);
	ExitAction = MakeAction(TEXT("IA_Car_Exit"), EInputActionValueType::Boolean);
	LightsAction = MakeAction(TEXT("IA_Car_Lights"), EInputActionValueType::Boolean);
	ParkingBrakeAction = MakeAction(TEXT("IA_Car_ParkingBrake"), EInputActionValueType::Boolean);
	DetailsAction = MakeAction(TEXT("IA_Car_Details"), EInputActionValueType::Boolean);

	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Car"));
	auto Map = [this](UInputAction* Action, const FKey& Key, bool bNegate = false, bool bDeadZone = false) {
		FEnhancedActionKeyMapping& M = Mapping->MapKey(Action, Key);
		if (bNegate)
		{
			M.Modifiers.Add(NewObject<UInputModifierNegate>(this));
		}
		if (bDeadZone)
		{
			M.Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
		}
	};

	Map(ThrottleAction, EKeys::W);
	Map(ThrottleAction, EKeys::Up);
	Map(ThrottleAction, EKeys::Gamepad_RightTriggerAxis, false, true);
	Map(BrakeAction, EKeys::S);
	Map(BrakeAction, EKeys::Down);
	Map(BrakeAction, EKeys::Gamepad_LeftTriggerAxis, false, true);
	Map(SteerAction, EKeys::D);
	Map(SteerAction, EKeys::Right);
	Map(SteerAction, EKeys::A, true);
	Map(SteerAction, EKeys::Left, true);
	Map(SteerAction, EKeys::Gamepad_LeftX, false, true);
	Map(ClutchAction, EKeys::LeftShift);
	Map(ClutchAction, EKeys::Gamepad_FaceButton_Left);
	Map(HandbrakeAction, EKeys::SpaceBar);
	Map(HandbrakeAction, EKeys::Gamepad_FaceButton_Right);
	Map(LookAction, EKeys::Mouse2D);
	Map(LookAction, EKeys::Gamepad_Right2D, false, true);
	Map(StarterAction, EKeys::R);
	Map(StarterAction, EKeys::Gamepad_FaceButton_Top);
	Map(ShiftUpAction, EKeys::E);
	Map(ShiftUpAction, EKeys::Gamepad_RightShoulder);
	Map(ShiftDownAction, EKeys::Q);
	Map(ShiftDownAction, EKeys::Gamepad_LeftShoulder);
	Map(IgnitionAction, EKeys::I);
	Map(GearboxModeAction, EKeys::G);
	Map(GearboxModeAction, EKeys::Gamepad_Special_Left);
	Map(CameraAction, EKeys::C);
	Map(CameraAction, EKeys::Gamepad_RightThumbstick);
	Map(ResetAction, EKeys::BackSpace);
	Map(ResetAction, EKeys::Gamepad_Special_Right);
	Map(InteractAction, EKeys::LeftMouseButton);
	Map(InteractAction, EKeys::Gamepad_FaceButton_Bottom);
	Map(ExitAction, EKeys::F);
	Map(ExitAction, EKeys::Gamepad_DPad_Down);
	Map(LightsAction, EKeys::L);
	Map(LightsAction, EKeys::Gamepad_DPad_Up);
	Map(ParkingBrakeAction, EKeys::P);
	Map(ParkingBrakeAction, EKeys::Gamepad_DPad_Left);
	Map(DetailsAction, EKeys::F3);
}

void APeregonCar::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error, TEXT("Peregon: нужен EnhancedInputComponent (Config/DefaultInput.ini)"));
		return;
	}
	EIC->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &APeregonCar::OnThrottle);
	EIC->BindAction(BrakeAction, ETriggerEvent::Triggered, this, &APeregonCar::OnBrake);
	EIC->BindAction(SteerAction, ETriggerEvent::Triggered, this, &APeregonCar::OnSteer);
	EIC->BindAction(ClutchAction, ETriggerEvent::Triggered, this, &APeregonCar::OnClutch);
	EIC->BindAction(HandbrakeAction, ETriggerEvent::Triggered, this, &APeregonCar::OnHandbrake);
	EIC->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &APeregonCar::OnThrottleReleased);
	EIC->BindAction(BrakeAction, ETriggerEvent::Completed, this, &APeregonCar::OnBrakeReleased);
	EIC->BindAction(SteerAction, ETriggerEvent::Completed, this, &APeregonCar::OnSteerReleased);
	EIC->BindAction(ClutchAction, ETriggerEvent::Completed, this, &APeregonCar::OnClutchReleased);
	EIC->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &APeregonCar::OnHandbrakeReleased);
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &APeregonCar::OnLook);
	EIC->BindAction(StarterAction, ETriggerEvent::Started, this, &APeregonCar::OnStarterPressed);
	EIC->BindAction(StarterAction, ETriggerEvent::Completed, this, &APeregonCar::OnStarterReleased);
	EIC->BindAction(ShiftUpAction, ETriggerEvent::Started, this, &APeregonCar::OnShiftUp);
	EIC->BindAction(ShiftDownAction, ETriggerEvent::Started, this, &APeregonCar::OnShiftDown);
	EIC->BindAction(IgnitionAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleIgnition);
	EIC->BindAction(GearboxModeAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleGearbox);
	EIC->BindAction(CameraAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleCamera);
	EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &APeregonCar::OnResetCar);
	EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &APeregonCar::OnInteractPressed);
	EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &APeregonCar::OnInteractReleased);
	EIC->BindAction(ExitAction, ETriggerEvent::Started, this, &APeregonCar::OnExit);
	EIC->BindAction(LightsAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleLights);
	EIC->BindAction(ParkingBrakeAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleParkingBrake);
	EIC->BindAction(DetailsAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleDetails);
}

void APeregonCar::PawnClientRestart()
{
	Super::PawnClientRestart();
	BuildInput();
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->ClearAllMappings();
			Subsystem->AddMappingContext(Mapping, 0);
		}
	}
}

void APeregonCar::UnPossessed()
{
	Super::UnPossessed();
	// Водитель вышел: ноги с педалей, руль отпущен. Мотор, если работал, продолжает работать.
	ThrottleAxis = BrakeAxis = ThrottleSmoothed = BrakeSmoothed = 0.f;
	if (Movement)
	{
		Movement->SetThrottle(0.f);
		Movement->SetBrake(0.f);
		Movement->SetSteering(0.f);
		Movement->SetClutchPedal(0.f);
		Movement->SetHandbrake(0.f);
		Movement->SetStarter(false);
	}
	bKeyCranking = false;
	Focus = FPeregonFocus();
}

void APeregonCar::OnThrottle(const FInputActionValue& Value) { ThrottleAxis = FMath::Clamp(Value.Get<float>(), 0.f, 1.f); }
void APeregonCar::OnBrake(const FInputActionValue& Value) { BrakeAxis = FMath::Clamp(Value.Get<float>(), 0.f, 1.f); }
void APeregonCar::OnSteer(const FInputActionValue& Value) { Movement->SetSteering(Value.Get<float>()); }
void APeregonCar::OnClutch(const FInputActionValue& Value) { Movement->SetClutchPedal(Value.Get<float>()); }
void APeregonCar::OnHandbrake(const FInputActionValue& Value) { Movement->SetHandbrake(Value.Get<float>()); }
void APeregonCar::OnThrottleReleased(const FInputActionValue&) { ThrottleAxis = 0.f; }
void APeregonCar::OnBrakeReleased(const FInputActionValue&) { BrakeAxis = 0.f; }
void APeregonCar::OnSteerReleased(const FInputActionValue&) { Movement->SetSteering(0.f); }
void APeregonCar::OnClutchReleased(const FInputActionValue&) { Movement->SetClutchPedal(0.f); }
void APeregonCar::OnHandbrakeReleased(const FInputActionValue&) { Movement->SetHandbrake(0.f); }

void APeregonCar::OnLook(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	if (bFirstPerson)
	{
		HeadYaw = FMath::Clamp(HeadYaw + V.X * 0.8f, -140.f, 140.f);
		HeadPitch = FMath::Clamp(HeadPitch + V.Y * 0.8f, -70.f, 50.f);
	}
	else
	{
		AddControllerYawInput(V.X);
		AddControllerPitchInput(-V.Y);
		TimeSinceLook = 0.f;
	}
}

void APeregonCar::OnStarterPressed(const FInputActionValue&)
{
	Movement->SetIgnition(true);
	Movement->SetStarter(true);
}

void APeregonCar::OnStarterReleased(const FInputActionValue&) { Movement->SetStarter(false); }
void APeregonCar::OnShiftUp(const FInputActionValue&) { Movement->ShiftUp(); }
void APeregonCar::OnShiftDown(const FInputActionValue&) { Movement->ShiftDown(); }
void APeregonCar::OnToggleIgnition(const FInputActionValue&) { Movement->SetIgnition(!Movement->GetIgnition()); }
void APeregonCar::OnToggleGearbox(const FInputActionValue&) { Movement->SetAutoGearbox(!Movement->IsAutoGearbox()); }
void APeregonCar::OnToggleCamera(const FInputActionValue&) { SetFirstPerson(!bFirstPerson); }
void APeregonCar::OnToggleLights(const FInputActionValue&) { SetHeadlights(!bHeadlights); }
void APeregonCar::OnToggleParkingBrake(const FInputActionValue&) { Movement->SetParkingBrake(!Movement->IsParkingBrake()); }

void APeregonCar::OnInteractPressed(const FInputActionValue&)
{
	if (IPeregonInteractable* Target = Focus.IsValid() ? Focus.Target() : nullptr)
	{
		InteractingActor = Focus.Actor;
		InteractingPart = Focus.Part;
		Target->InteractStart(Focus.Part.Get(), this);
	}
}

void APeregonCar::OnInteractReleased(const FInputActionValue&)
{
	if (IPeregonInteractable* Target = Cast<IPeregonInteractable>(InteractingActor.Get()))
	{
		Target->InteractStop(InteractingPart.Get(), this);
	}
	InteractingActor.Reset();
	InteractingPart.Reset();
}

void APeregonCar::OnExit(const FInputActionValue&)
{
	if (FMath::Abs(Movement->GetForwardSpeed()) < PeregonCarLook::MaxExitSpeed)
	{
		ExitCar();
	}
}

void APeregonCar::OnToggleDetails(const FInputActionValue&)
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (APeregonHUD* Hud = Cast<APeregonHUD>(PC->GetHUD()))
		{
			Hud->ToggleDetails();
		}
	}
}

void APeregonCar::OnResetCar(const FInputActionValue&)
{
	// Поставить на колёса (перевернулись / застряли).
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = 0.f;
	Rotation.Roll = 0.f;
	SetActorLocationAndRotation(GetActorLocation() + FVector(0.f, 0.f, 150.f), Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}
