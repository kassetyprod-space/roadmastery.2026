#include "Vehicle/PeregonCar.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"

namespace PeregonCarVisual
{
const FLinearColor BodyColor(0.32f, 0.04f, 0.04f);   // выцветшая «вишня»
const FLinearColor CabinColor(0.05f, 0.07f, 0.08f);  // тёмные стёкла
const FLinearColor TireColor(0.02f, 0.02f, 0.02f);
const FLinearColor HubColor(0.45f, 0.45f, 0.42f);

void Tint(UStaticMeshComponent* Mesh, const FLinearColor& Color)
{
	if (Mesh && Mesh->GetMaterial(0))
	{
		UMaterialInstanceDynamic* Mid = Mesh->CreateDynamicMaterialInstance(0);
		if (Mid)
		{
			Mid->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}
} // namespace PeregonCarVisual

APeregonCar::APeregonCar()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// Кузов: простой бокс — единственное твёрдое тело машины. Колёса — лучи, не коллизия.
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(205.f, 81.f, 30.f));
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Body->SetSimulatePhysics(true);
	Body->SetLinearDamping(0.01f);
	Body->SetAngularDamping(0.05f);
	RootComponent = Body;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Body);
	BodyMesh->SetRelativeScale3D(FVector(4.1f, 1.62f, 0.6f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CabinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CabinMesh"));
	CabinMesh->SetupAttachment(Body);
	CabinMesh->SetRelativeLocation(FVector(-20.f, 0.f, 55.f));
	CabinMesh->SetRelativeScale3D(FVector(2.1f, 1.4f, 0.5f));
	CabinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (CubeMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeMesh.Object);
		CabinMesh->SetStaticMesh(CubeMesh.Object);
	}

	Movement = CreateDefaultSubobject<UPeregonVehicleMovementComponent>(TEXT("VehicleMovement"));

	for (int32 I = 0; I < Movement->GetNumWheels(); ++I)
	{
		USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("WheelPivot%d"), I));
		Pivot->SetupAttachment(Body);
		Pivot->SetRelativeLocation(Movement->GetWheelAttachLocal(I));

		const float Diameter = Movement->GetWheelRadiusCm(I) * 2.f;
		const float Width = Movement->GetWheelWidthCm(I);

		UStaticMeshComponent* Tire = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Tire%d"), I));
		Tire->SetupAttachment(Pivot);
		Tire->SetRelativeRotation(FRotator(0.f, 0.f, 90.f)); // ось цилиндра → поперёк машины
		Tire->SetRelativeScale3D(FVector(Diameter, Diameter, Width) / 100.f);
		Tire->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// Перекладина на диске — чтобы было видно вращение колеса.
		UStaticMeshComponent* Hub = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Hub%d"), I));
		Hub->SetupAttachment(Pivot);
		Hub->SetRelativeScale3D(FVector(Diameter * 0.8f, Width + 2.f, 6.f) / 100.f);
		Hub->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		if (CylinderMesh.Succeeded())
		{
			Tire->SetStaticMesh(CylinderMesh.Object);
		}
		if (CubeMesh.Succeeded())
		{
			Hub->SetStaticMesh(CubeMesh.Object);
		}
		WheelPivots.Add(Pivot);
		Movement->WheelVisuals.Add(Pivot);
	}

	ChaseArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseArm"));
	ChaseArm->SetupAttachment(Body);
	ChaseArm->SetRelativeRotation(FRotator(-12.f, 0.f, 0.f));
	ChaseArm->TargetArmLength = 650.f;
	ChaseArm->SocketOffset = FVector(0.f, 0.f, 120.f);
	ChaseArm->bEnableCameraLag = true;
	ChaseArm->CameraLagSpeed = 8.f;
	ChaseArm->bEnableCameraRotationLag = true;
	ChaseArm->CameraRotationLagSpeed = 5.f;
	ChaseArm->bInheritRoll = false;
	ChaseArm->bInheritPitch = false;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(ChaseArm, USpringArmComponent::SocketName);
	ChaseCamera->SetFieldOfView(80.f);

	// Место водителя: левый руль, как у советских машин.
	HoodCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
	HoodCamera->SetupAttachment(Body);
	HoodCamera->SetRelativeLocation(FVector(-15.f, -35.f, 85.f));
	HoodCamera->SetFieldOfView(85.f);
	HoodCamera->SetAutoActivate(false);
}

void APeregonCar::BeginPlay()
{
	Super::BeginPlay();

	using namespace PeregonCarVisual;
	Tint(BodyMesh, BodyColor);
	Tint(CabinMesh, CabinColor);
	for (USceneComponent* Pivot : WheelPivots)
	{
		TArray<USceneComponent*> PivotChildren;
		Pivot->GetChildrenComponents(false, PivotChildren);
		for (USceneComponent* Child : PivotChildren)
		{
			UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Child);
			Tint(Mesh, Mesh && Mesh->GetName().StartsWith(TEXT("Hub")) ? HubColor : TireColor);
		}
	}
}

void APeregonCar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Movement)
	{
		return;
	}

	float Throttle = ThrottleAxis;
	float Brake = BrakeAxis;

	// Аркадный автомат: тормоз на месте включает заднюю, в задней педали меняются местами.
	if (Movement->IsAutoGearbox())
	{
		const float Speed = Movement->GetForwardSpeed();
		const int32 Gear = Movement->GetGear();
		if (Gear == 0 && Throttle > 0.1f)
		{
			Movement->RequestGear(1);
		}
		if (Gear >= 0 && Brake > 0.5f && Throttle < 0.1f && FMath::Abs(Speed) < 0.5f)
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
			if (Throttle > 0.1f && Speed > -0.5f)
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
}

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
	ThrottleAction = MakeAction(TEXT("IA_Throttle"), EInputActionValueType::Axis1D);
	BrakeAction = MakeAction(TEXT("IA_Brake"), EInputActionValueType::Axis1D);
	SteerAction = MakeAction(TEXT("IA_Steer"), EInputActionValueType::Axis1D);
	ClutchAction = MakeAction(TEXT("IA_Clutch"), EInputActionValueType::Axis1D);
	HandbrakeAction = MakeAction(TEXT("IA_Handbrake"), EInputActionValueType::Axis1D);
	StarterAction = MakeAction(TEXT("IA_Starter"), EInputActionValueType::Boolean);
	ShiftUpAction = MakeAction(TEXT("IA_ShiftUp"), EInputActionValueType::Boolean);
	ShiftDownAction = MakeAction(TEXT("IA_ShiftDown"), EInputActionValueType::Boolean);
	IgnitionAction = MakeAction(TEXT("IA_Ignition"), EInputActionValueType::Boolean);
	GearboxModeAction = MakeAction(TEXT("IA_GearboxMode"), EInputActionValueType::Boolean);
	CameraAction = MakeAction(TEXT("IA_Camera"), EInputActionValueType::Boolean);
	ResetAction = MakeAction(TEXT("IA_Reset"), EInputActionValueType::Boolean);

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
}

void APeregonCar::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(Mapping, 0);
		}
	}

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
	// Отпустили клавишу — оси в ноль.
	EIC->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &APeregonCar::OnThrottleReleased);
	EIC->BindAction(BrakeAction, ETriggerEvent::Completed, this, &APeregonCar::OnBrakeReleased);
	EIC->BindAction(SteerAction, ETriggerEvent::Completed, this, &APeregonCar::OnSteerReleased);
	EIC->BindAction(ClutchAction, ETriggerEvent::Completed, this, &APeregonCar::OnClutchReleased);
	EIC->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &APeregonCar::OnHandbrakeReleased);

	EIC->BindAction(StarterAction, ETriggerEvent::Started, this, &APeregonCar::OnStarterPressed);
	EIC->BindAction(StarterAction, ETriggerEvent::Completed, this, &APeregonCar::OnStarterReleased);
	EIC->BindAction(ShiftUpAction, ETriggerEvent::Started, this, &APeregonCar::OnShiftUp);
	EIC->BindAction(ShiftDownAction, ETriggerEvent::Started, this, &APeregonCar::OnShiftDown);
	EIC->BindAction(IgnitionAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleIgnition);
	EIC->BindAction(GearboxModeAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleGearbox);
	EIC->BindAction(CameraAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleCamera);
	EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &APeregonCar::OnResetCar);
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

void APeregonCar::OnToggleCamera(const FInputActionValue&)
{
	bHoodCamera = !bHoodCamera;
	ChaseCamera->SetActive(!bHoodCamera);
	HoodCamera->SetActive(bHoodCamera);
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
