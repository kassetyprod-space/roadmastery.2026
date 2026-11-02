#include "Player/PeregonCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Game/PeregonHUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace PeregonWalker
{
constexpr float EyeHeight = 68.f; // от центра капсулы: глаза на ~1.6 м
const FLinearColor JacketColor(0.12f, 0.16f, 0.10f); // ватник цвета хаки
const FLinearColor SkinColor(0.55f, 0.40f, 0.30f);
} // namespace PeregonWalker

APeregonCharacter::APeregonCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(36.f, 90.f);
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = WalkSpeed;
	Move->JumpZVelocity = 360.f;
	Move->AirControl = 0.15f;
	Move->BrakingDecelerationWalking = 1400.f;
	Move->GroundFriction = 8.f;
	Move->bOrientRotationToMovement = false;
	Move->MaxStepHeight = 40.f;
	Move->SetWalkableFloorAngle(50.f);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, PeregonWalker::EyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(85.f);

	ThirdPersonArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ThirdPersonArm"));
	ThirdPersonArm->SetupAttachment(GetCapsuleComponent());
	ThirdPersonArm->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	ThirdPersonArm->TargetArmLength = 280.f;
	ThirdPersonArm->SocketOffset = FVector(0.f, 45.f, 15.f); // из-за плеча
	ThirdPersonArm->bUsePawnControlRotation = true;
	ThirdPersonArm->bEnableCameraLag = true;
	ThirdPersonArm->CameraLagSpeed = 14.f;

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(ThirdPersonArm, USpringArmComponent::SocketName);
	ThirdPersonCamera->SetFieldOfView(80.f);
	ThirdPersonCamera->SetAutoActivate(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	TorsoMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TorsoMesh"));
	TorsoMesh->SetupAttachment(GetCapsuleComponent());
	TorsoMesh->SetRelativeLocation(FVector(0.f, 0.f, -20.f));
	TorsoMesh->SetRelativeScale3D(FVector(0.45f, 0.45f, 1.45f));
	TorsoMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(GetCapsuleComponent());
	HeadMesh->SetRelativeLocation(FVector(0.f, 0.f, 66.f));
	HeadMesh->SetRelativeScale3D(FVector(0.24f, 0.22f, 0.27f));
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (CylinderFinder.Succeeded())
	{
		TorsoMesh->SetStaticMesh(CylinderFinder.Object);
	}
	if (SphereFinder.Succeeded())
	{
		HeadMesh->SetStaticMesh(SphereFinder.Object);
	}
}

void APeregonCharacter::BeginPlay()
{
	Super::BeginPlay();
	auto Tint = [](UStaticMeshComponent* Part, const FLinearColor& Color) {
		if (Part && Part->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* Mid = Part->CreateDynamicMaterialInstance(0))
			{
				Mid->SetVectorParameterValue(TEXT("Color"), Color);
			}
		}
	};
	Tint(TorsoMesh, PeregonWalker::JacketColor);
	Tint(HeadMesh, PeregonWalker::SkinColor);
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	SetFirstPerson(true);
}

void APeregonCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsPlayerControlled() || IsHidden())
	{
		Focus = FPeregonFocus();
		return;
	}

	// Покачивание головы при ходьбе — мягкое, чтобы не укачивало.
	const float Speed2D = GetVelocity().Size2D();
	const bool bGrounded = GetCharacterMovement()->IsMovingOnGround();
	const float BobAmp = bGrounded ? FMath::Clamp(Speed2D / SprintSpeed, 0.f, 1.f) * 1.6f : 0.f;
	const float BobFreq = Speed2D > WalkSpeed + 10.f ? 13.f : 9.f;
	const float Time = GetWorld()->GetTimeSeconds();
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, FMath::Cos(Time * BobFreq * 0.5f) * BobAmp * 0.5f,
		PeregonWalker::EyeHeight + FMath::Sin(Time * BobFreq) * BobAmp));

	const FVector Eye = FirstPersonCamera->GetComponentLocation();
	Focus = PeregonInteraction::Trace(GetWorld(), Eye, GetControlRotation().Vector(), InteractReach, this);
}

void APeregonCharacter::SetFirstPerson(bool bEnable)
{
	bFirstPerson = bEnable;
	FirstPersonCamera->SetActive(bEnable);
	ThirdPersonCamera->SetActive(!bEnable);
	TorsoMesh->SetOwnerNoSee(bEnable);
	HeadMesh->SetOwnerNoSee(bEnable);
}

void APeregonCharacter::HideForVehicle()
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	Focus = FPeregonFocus();
}

void APeregonCharacter::ShowAt(const FVector& Location, const FRotator& Rotation)
{
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
}

void APeregonCharacter::BuildInput()
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
	MoveAction = MakeAction(TEXT("IA_Walk_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(TEXT("IA_Walk_Look"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(TEXT("IA_Walk_Jump"), EInputActionValueType::Boolean);
	SprintAction = MakeAction(TEXT("IA_Walk_Sprint"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(TEXT("IA_Walk_Interact"), EInputActionValueType::Boolean);
	CameraAction = MakeAction(TEXT("IA_Walk_Camera"), EInputActionValueType::Boolean);
	DetailsAction = MakeAction(TEXT("IA_Walk_Details"), EInputActionValueType::Boolean);

	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Walk"));
	// Модификаторы: Swizzle — ось клавиши в Y (вперёд/назад), Negate — знак; NegateY — инверсия мыши по вертикали.
	auto Map = [this](UInputAction* Action, const FKey& Key, bool bSwizzle, bool bNegate, bool bNegateYOnly = false, bool bDeadZone = false) {
		FEnhancedActionKeyMapping& M = Mapping->MapKey(Action, Key);
		if (bSwizzle)
		{
			M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		}
		if (bNegate || bNegateYOnly)
		{
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(this);
			if (bNegateYOnly)
			{
				Negate->bX = false;
				Negate->bZ = false;
			}
			M.Modifiers.Add(Negate);
		}
		if (bDeadZone)
		{
			M.Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
		}
	};

	Map(MoveAction, EKeys::W, true, false);
	Map(MoveAction, EKeys::S, true, true);
	Map(MoveAction, EKeys::D, false, false);
	Map(MoveAction, EKeys::A, false, true);
	Map(MoveAction, EKeys::Gamepad_Left2D, false, false, false, true);
	Map(LookAction, EKeys::Mouse2D, false, false, true);
	Map(LookAction, EKeys::Gamepad_Right2D, false, false, true, true);
	Map(JumpAction, EKeys::SpaceBar, false, false);
	Map(JumpAction, EKeys::Gamepad_FaceButton_Bottom, false, false);
	Map(SprintAction, EKeys::LeftShift, false, false);
	Map(SprintAction, EKeys::Gamepad_LeftThumbstick, false, false);
	Map(InteractAction, EKeys::E, false, false);
	Map(InteractAction, EKeys::F, false, false);
	Map(InteractAction, EKeys::LeftMouseButton, false, false);
	Map(InteractAction, EKeys::Gamepad_FaceButton_Left, false, false);
	Map(CameraAction, EKeys::C, false, false);
	Map(CameraAction, EKeys::Gamepad_RightThumbstick, false, false);
	Map(DetailsAction, EKeys::F3, false, false);
}

void APeregonCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		return;
	}
	EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APeregonCharacter::OnMove);
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &APeregonCharacter::OnLook);
	EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &APeregonCharacter::OnJumpPressed);
	EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &APeregonCharacter::OnJumpReleased);
	EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &APeregonCharacter::OnSprintPressed);
	EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &APeregonCharacter::OnSprintReleased);
	EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &APeregonCharacter::OnInteractPressed);
	EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &APeregonCharacter::OnInteractReleased);
	EIC->BindAction(CameraAction, ETriggerEvent::Started, this, &APeregonCharacter::OnToggleCamera);
	EIC->BindAction(DetailsAction, ETriggerEvent::Started, this, &APeregonCharacter::OnToggleDetails);
}

void APeregonCharacter::PawnClientRestart()
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

void APeregonCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X);
}

void APeregonCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	AddControllerYawInput(V.X);
	AddControllerPitchInput(V.Y);
}

void APeregonCharacter::OnJumpPressed(const FInputActionValue&) { Jump(); }
void APeregonCharacter::OnJumpReleased(const FInputActionValue&) { StopJumping(); }
void APeregonCharacter::OnSprintPressed(const FInputActionValue&) { GetCharacterMovement()->MaxWalkSpeed = SprintSpeed; }
void APeregonCharacter::OnSprintReleased(const FInputActionValue&) { GetCharacterMovement()->MaxWalkSpeed = WalkSpeed; }
void APeregonCharacter::OnToggleCamera(const FInputActionValue&) { SetFirstPerson(!bFirstPerson); }

void APeregonCharacter::OnInteractPressed(const FInputActionValue&)
{
	if (IPeregonInteractable* Target = Focus.IsValid() ? Focus.Target() : nullptr)
	{
		InteractingActor = Focus.Actor;
		InteractingPart = Focus.Part;
		Target->InteractStart(Focus.Part.Get(), this);
	}
}

void APeregonCharacter::OnInteractReleased(const FInputActionValue&)
{
	if (IPeregonInteractable* Target = Cast<IPeregonInteractable>(InteractingActor.Get()))
	{
		Target->InteractStop(InteractingPart.Get(), this);
	}
	InteractingActor.Reset();
	InteractingPart.Reset();
}

void APeregonCharacter::OnToggleDetails(const FInputActionValue&)
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (APeregonHUD* Hud = Cast<APeregonHUD>(PC->GetHUD()))
		{
			Hud->ToggleDetails();
		}
	}
}
