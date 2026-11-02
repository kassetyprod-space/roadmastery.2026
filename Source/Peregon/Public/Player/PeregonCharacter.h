// Пешеход: ходит, бегает, прыгает, смотрит от 1-го или 3-го лица и взаимодействует с миром
// (сесть в машину, открыть капот, залить бензин…).
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

	UPROPERTY(EditAnywhere, Category = "Peregon|Movement")
	float WalkSpeed = 170.f;

	UPROPERTY(EditAnywhere, Category = "Peregon|Movement")
	float SprintSpeed = 480.f;

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

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CameraAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DetailsAction;

	FPeregonFocus Focus;
	TWeakObjectPtr<AActor> InteractingActor;
	TWeakObjectPtr<UPrimitiveComponent> InteractingPart;
	bool bFirstPerson = true;
};
