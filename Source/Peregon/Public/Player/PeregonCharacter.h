// Пешеход: ходит, бегает, приседает (Ctrl), прыгает, смотрит от 1-го или 3-го лица (V), приближает взгляд (C)
// и взаимодействует с миром (сесть в машину, открыть капот, залить бензин…).
// Камера «живая»: покачивание в такт шагам, наклон при движении вбок, проседание при приземлении, дыхание.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interaction/PeregonInteractable.h"
#include "PeregonCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

UCLASS()
class PEREGON_API APeregonCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	APeregonCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// Сел в машину: прячемся и выключаем коллизию.
	void HideForVehicle();
	// Вышел из машины.
	void ShowAt(const FVector& Location, const FRotator& Rotation);

	const FPeregonFocus& GetFocus() const { return Focus; }
	bool IsFirstPerson() const { return bFirstPerson; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon|Camera")
	TObjectPtr<USpringArmComponent> ThirdPersonArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon|Camera")
	TObjectPtr<UCameraComponent> ThirdPersonCamera;

	// Временное тело из примитивов (видно от 3-го лица).
	UPROPERTY(VisibleAnywhere, Category = "Peregon|Visual")
	TObjectPtr<UStaticMeshComponent> TorsoMesh;

	UPROPERTY(VisibleAnywhere, Category = "Peregon|Visual")
	TObjectPtr<UStaticMeshComponent> HeadMesh;

	// Вещь в руках (ячейка карманов, выбранная клавишами 1–0).
	UPROPERTY(VisibleAnywhere, Category = "Peregon|Visual")
	TObjectPtr<UStaticMeshComponent> HeldMesh;
	FName HeldShown;
	void UpdateHeld();

	UPROPERTY(EditAnywhere, Category = "Peregon|Movement")
	float WalkSpeed = 170.f;

	UPROPERTY(EditAnywhere, Category = "Peregon|Movement")
	float SprintSpeed = 480.f;

	UPROPERTY(EditAnywhere, Category = "Peregon|Movement")
	float CrouchSpeed = 95.f;

	UPROPERTY(EditAnywhere, Category = "Peregon|Interaction")
	float InteractReach = 220.f;

private:
	void BuildInput();
	void SetFirstPerson(bool bEnable);

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJumpPressed(const FInputActionValue& Value);
	void OnJumpReleased(const FInputActionValue& Value);
	void OnSprintPressed(const FInputActionValue& Value);
	void OnSprintReleased(const FInputActionValue& Value);
	void OnInteractPressed(const FInputActionValue& Value);
	void OnInteractReleased(const FInputActionValue& Value);
	void OnToggleCamera(const FInputActionValue& Value);
	void OnToggleDetails(const FInputActionValue& Value);
	void OnCrouchPressed(const FInputActionValue& Value);
	void OnCrouchReleased(const FInputActionValue& Value);
	void OnZoomPressed(const FInputActionValue& Value);
	void OnZoomReleased(const FInputActionValue& Value);
	void UpdateCamera(float DeltaSeconds);
	void Footstep(float Loudness);

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CameraAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DetailsAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ZoomAction;

	FPeregonFocus Focus;
	TWeakObjectPtr<AActor> InteractingActor;
	TWeakObjectPtr<UPrimitiveComponent> InteractingPart;
	bool bFirstPerson = true;
	bool bSprint = false;
	bool bZoom = false;

	// Камера.
	float StepPhase = 0.f;        // фаза шага, рад (полный цикл — два шага)
	float BobWeight = 0.f;        // 0 — стоим, 1 — идём
	float CrouchCamOffset = 0.f;  // компенсация скачка капсулы при приседании (см)
	float LandOffset = 0.f;       // проседание после прыжка (см)
	float LandVelocity = 0.f;
	float Roll = 0.f;             // наклон при движении вбок, °
	float FovCurrent = 0.f;
	float Breath = 0.f;
};
