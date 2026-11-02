// Машина игрока. Пока — «серая коробка» в PSX-духе из примитивов движка: кузов, кабина, колёса.
// Ввод (Enhanced Input) собирается в коде, чтобы проект запускался без бинарных .uasset.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PeregonCar.generated.h"

class UBoxComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
class UPeregonVehicleMovementComponent;
struct FInputActionValue;

UCLASS()
class PEREGON_API APeregonCar : public APawn
{
	GENERATED_BODY()

public:
	APeregonCar();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPeregonVehicleMovementComponent* GetVehicleMovement() const { return Movement; }

	// Подсказка для HUD.
	bool IsHoodCamera() const { return bHoodCamera; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UStaticMeshComponent> CabinMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TArray<TObjectPtr<USceneComponent>> WheelPivots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<USpringArmComponent> ChaseArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UCameraComponent> HoodCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UPeregonVehicleMovementComponent> Movement;

private:
	void BuildInput();

	void OnThrottle(const FInputActionValue& Value);
	void OnBrake(const FInputActionValue& Value);
	void OnSteer(const FInputActionValue& Value);
	void OnClutch(const FInputActionValue& Value);
	void OnHandbrake(const FInputActionValue& Value);
	void OnThrottleReleased(const FInputActionValue& Value);
	void OnBrakeReleased(const FInputActionValue& Value);
	void OnSteerReleased(const FInputActionValue& Value);
	void OnClutchReleased(const FInputActionValue& Value);
	void OnHandbrakeReleased(const FInputActionValue& Value);
	void OnStarterPressed(const FInputActionValue& Value);
	void OnStarterReleased(const FInputActionValue& Value);
	void OnShiftUp(const FInputActionValue& Value);
	void OnShiftDown(const FInputActionValue& Value);
	void OnToggleIgnition(const FInputActionValue& Value);
	void OnToggleGearbox(const FInputActionValue& Value);
	void OnToggleCamera(const FInputActionValue& Value);
	void OnResetCar(const FInputActionValue& Value);

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SteerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ClutchAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HandbrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> StarterAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ShiftUpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ShiftDownAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> IgnitionAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> GearboxModeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CameraAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;

	float ThrottleAxis = 0.f;
	float BrakeAxis = 0.f;
	float ReverseHoldTime = 0.f;
	bool bHoodCamera = false;
};
