#include "Player/PeregonCharacter.h"

#include "Art/PeregonArtSubsystem.h"
#include "Game/PeregonGameUserSettings.h"

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
#include "Audio/PeregonAudio.h"

namespace PeregonWalker
{
constexpr float EyeHeight = 68.f;        // от центра капсулы: глаза на ~1.6 м
constexpr float CrouchEyeHeight = 48.f;  // от центра присевшей капсулы: глаза на ~1.05 м
constexpr float StepLength = 72.f;       // см на шаг шагом; бегом шаг длиннее
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
	Move->NavAgentProps.bCanCrouch = true;
	Move->SetCrouchedHalfHeight(55.f);
	Move->MaxWalkSpeedCrouched = CrouchSpeed;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, PeregonWalker::EyeHeight));
	// Поворот камеры собираем сами (крен и «кивки» поверх взгляда игрока).
	FirstPersonCamera->bUsePawnControlRotation = false;
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
	UPeregonArtSubsystem::ApplyLook(this, TorsoMesh, FPeregonLook(EPeregonTex::Fabric, PeregonWalker::JacketColor * 3.f, 0.95f));
	UPeregonArtSubsystem::ApplyLook(this, HeadMesh, FPeregonLook(EPeregonTex::White, PeregonWalker::SkinColor, 0.7f));
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	SetFirstPerson(UPeregonGameUserSettings::Get()->bStartFirstPerson);
}

void APeregonCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsPlayerControlled() || IsHidden())
	{
		Focus = FPeregonFocus();
		return;
	}

	UpdateCamera(DeltaSeconds);

	const FVector Eye = FirstPersonCamera->GetComponentLocation();
	Focus = PeregonInteraction::Trace(GetWorld(), Eye, GetControlRotation().Vector(), InteractReach, this);
}

void APeregonCharacter::UpdateCamera(float DeltaSeconds)
{
	using namespace PeregonWalker;
	const UPeregonGameUserSettings* Settings = UPeregonGameUserSettings::Get();
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Dt = FMath::Max(DeltaSeconds, 1e-3f);
	const bool bGrounded = Move->IsMovingOnGround();
	const FVector Velocity = GetVelocity();
	const float Speed2D = Velocity.Size2D();
	const float Motion = Settings->HeadMotion;

	// Шаги: фаза растёт с пройденным путём, а не со временем — покачивание совпадает со звуком шагов.
	const float Stride = StepLength * (bIsCrouched ? 0.7f : (Speed2D > WalkSpeed + 30.f ? 1.45f : 1.f));
	if (bGrounded && Speed2D > 5.f)
	{
		const float PrevPhase = StepPhase;
		StepPhase += Speed2D * Dt / Stride * PI;
		// Каждые π — шаг (левая/правая нога).
		if (FMath::FloorToInt(StepPhase / PI) != FMath::FloorToInt(PrevPhase / PI))
		{
			Footstep(FMath::Clamp(Speed2D / SprintSpeed, 0.25f, 1.f) * (bIsCrouched ? 0.5f : 1.f));
		}
	}
	BobWeight = FMath::FInterpTo(BobWeight, bGrounded ? FMath::Clamp(Speed2D / WalkSpeed, 0.f, 1.6f) : 0.f, Dt, 8.f);

	const float Amp = (bIsCrouched ? 0.6f : 1.f) * Motion;
	const float BobZ = -FMath::Abs(FMath::Sin(StepPhase)) * 1.6f * BobWeight * Amp + 0.8f * BobWeight * Amp; // «пружинит» на каждом шаге
	const float BobY = FMath::Sin(StepPhase * 0.5f) * 1.1f * BobWeight * Amp;                               // перенос веса с ноги на ногу
	Breath += Dt * (Speed2D > WalkSpeed + 30.f ? 1.6f : 0.9f);
	const float BreathZ = FMath::Sin(Breath * 1.6f) * 0.35f * Motion;

	// Приземление: пружина «вниз и обратно».
	LandVelocity += (-LandOffset * 120.f - LandVelocity * 16.f) * Dt;
	LandOffset += LandVelocity * Dt;
	CrouchCamOffset = FMath::FInterpTo(CrouchCamOffset, 0.f, Dt, 9.f);

	const float Eye = bIsCrouched ? CrouchEyeHeight : EyeHeight;
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, BobY, Eye + BobZ + BreathZ + LandOffset + CrouchCamOffset));

	// Крен при движении вбок и лёгкий кивок на шагах.
	const FVector LocalVel = GetActorTransform().InverseTransformVectorNoScale(Velocity);
	Roll = FMath::FInterpTo(Roll, FMath::Clamp(LocalVel.Y / SprintSpeed, -1.f, 1.f) * 1.8f * Motion, Dt, 6.f);
	FRotator View = GetControlRotation();
	View.Roll = Roll + FMath::Sin(StepPhase * 0.5f) * 0.35f * BobWeight * Amp;
	View.Pitch += FMath::Sin(StepPhase) * 0.25f * BobWeight * Amp + LandOffset * 0.15f;
	FirstPersonCamera->SetWorldRotation(View);

	// Поле зрения: шире на бегу, уже при приближении (C).
	const float BaseFov = Settings->FieldOfView;
	float TargetFov = BaseFov + (Speed2D > WalkSpeed + 30.f && bGrounded ? 5.f : 0.f);
	if (bZoom)
	{
		TargetFov = BaseFov * 0.42f;
	}
	FovCurrent = FovCurrent <= 0.f ? TargetFov : FMath::FInterpTo(FovCurrent, TargetFov, Dt, bZoom ? 10.f : 7.f);
	FirstPersonCamera->SetFieldOfView(FovCurrent);
	ThirdPersonCamera->SetFieldOfView(FovCurrent);
	ThirdPersonArm->TargetArmLength = FMath::FInterpTo(ThirdPersonArm->TargetArmLength, bIsCrouched ? 230.f : 280.f, Dt, 5.f);

	// Тело от 3-го лица: приседает и наклоняется вперёд на бегу.
	const float Lean = FMath::Clamp(LocalVel.X / SprintSpeed, -0.5f, 1.f) * 10.f;
	TorsoMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, bIsCrouched ? -8.f : -20.f), FRotator(-Lean, 0.f, 0.f));
	TorsoMesh->SetRelativeScale3D(FVector(0.45f, 0.45f, bIsCrouched ? 0.95f : 1.45f));
	HeadMesh->SetRelativeLocation(FVector(Lean * 0.6f, 0.f, bIsCrouched ? 44.f : 66.f));
}

void APeregonCharacter::Footstep(float Loudness)
{
	PeregonAudio::PlayFootstep(this, GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), Loudness);
}

void APeregonCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	const float Impact = FMath::Abs(GetCharacterMovement()->Velocity.Z);
	const float FallSpeed = FMath::Max(Impact, FMath::Abs(GetVelocity().Z));
	LandVelocity -= FMath::Clamp(FallSpeed / 400.f, 0.3f, 2.5f) * 60.f * UPeregonGameUserSettings::Get()->HeadMotion;
	PeregonAudio::PlayEvent(this, TEXT("jump.land"), GetActorLocation(), FMath::Clamp(FallSpeed / 600.f, 0.4f, 1.f));
}

void APeregonCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	// Капсула опустилась скачком — камеру компенсируем и плавно доводим.
	CrouchCamOffset += ScaledHalfHeightAdjust + (PeregonWalker::EyeHeight - PeregonWalker::CrouchEyeHeight);
}

void APeregonCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchCamOffset -= ScaledHalfHeightAdjust + (PeregonWalker::EyeHeight - PeregonWalker::CrouchEyeHeight);
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
	CrouchAction = MakeAction(TEXT("IA_Walk_Crouch"), EInputActionValueType::Boolean);
	ZoomAction = MakeAction(TEXT("IA_Walk_Zoom"), EInputActionValueType::Boolean);

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
	Map(CameraAction, EKeys::V, false, false);
	Map(CrouchAction, EKeys::LeftControl, false, false);
	Map(CrouchAction, EKeys::Gamepad_FaceButton_Right, false, false);
	Map(ZoomAction, EKeys::C, false, false);
	Map(ZoomAction, EKeys::RightMouseButton, false, false);
	Map(ZoomAction, EKeys::Gamepad_LeftTrigger, false, false);
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
	EIC->BindAction(CrouchAction, ETriggerEvent::Started, this, &APeregonCharacter::OnCrouchPressed);
	EIC->BindAction(CrouchAction, ETriggerEvent::Completed, this, &APeregonCharacter::OnCrouchReleased);
	EIC->BindAction(ZoomAction, ETriggerEvent::Started, this, &APeregonCharacter::OnZoomPressed);
	EIC->BindAction(ZoomAction, ETriggerEvent::Completed, this, &APeregonCharacter::OnZoomReleased);
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
	const UPeregonGameUserSettings* Settings = UPeregonGameUserSettings::Get();
	// При приближении (C) мышь чуть «тяжелее» — проще прицелиться взглядом.
	const FVector2D V = Value.Get<FVector2D>() * Settings->MouseSensitivity * (bZoom ? 0.45f : 1.f);
	AddControllerYawInput(V.X);
	AddControllerPitchInput(Settings->bInvertY ? -V.Y : V.Y);
}

void APeregonCharacter::OnJumpPressed(const FInputActionValue&) { Jump(); }
void APeregonCharacter::OnJumpReleased(const FInputActionValue&) { StopJumping(); }
void APeregonCharacter::OnSprintPressed(const FInputActionValue&)
{
	bSprint = true;
	GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
}
void APeregonCharacter::OnSprintReleased(const FInputActionValue&)
{
	bSprint = false;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}
void APeregonCharacter::OnCrouchPressed(const FInputActionValue&) { Crouch(); }
void APeregonCharacter::OnCrouchReleased(const FInputActionValue&) { UnCrouch(); }
void APeregonCharacter::OnZoomPressed(const FInputActionValue&) { bZoom = true; }
void APeregonCharacter::OnZoomReleased(const FInputActionValue&) { bZoom = false; }
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
