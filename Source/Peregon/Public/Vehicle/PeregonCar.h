// Машина игрока: кузов «коробками» в PSX-духе с полноценным салоном.
// Двери, капот, багажник, бардачок открываются; ключ, фары, ручник работают мышкой из салона.
// Вид от 1-го лица (с инерцией головы на перегрузках) и от 3-го лица (орбитальная камера).
// Ввод (Enhanced Input) собирается в коде, чтобы проект запускался без бинарных .uasset.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Interaction/PeregonInteractable.h"
#include "PeregonCar.generated.h"

class APeregonCharacter;
class UBoxComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UPeregonVehicleMovementComponent;
class USceneComponent;
class USpotLightComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FInputActionValue;
struct FPeregonTelemetry;

// Подвижная деталь: плавно ходит между двумя положениями.
USTRUCT()
struct FPeregonMovingPart
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USceneComponent> Pivot = nullptr;

	FName Tag;
	FRotator Closed = FRotator::ZeroRotator;
	FRotator Open = FRotator::ZeroRotator;
	float Alpha = 0.f;
	float Target = 0.f;
	float Speed = 2.5f;
	float AutoCloseTimer = 0.f;
};

UCLASS()
class PEREGON_API APeregonCar : public APawn, public IPeregonInteractable
{
	GENERATED_BODY()

public:
	APeregonCar();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;

	// IPeregonInteractable
	virtual FString GetInteractPrompt(const UPrimitiveComponent* Part, const APawn* User) const override;
	virtual void InteractStart(UPrimitiveComponent* Part, APawn* User) override;
	virtual void InteractStop(UPrimitiveComponent* Part, APawn* User) override;

	UPeregonVehicleMovementComponent* GetVehicleMovement() const { return Movement; }
	const FPeregonFocus& GetFocus() const { return Focus; }
	bool IsFirstPerson() const { return bFirstPerson; }
	bool AreHeadlightsOn() const { return bHeadlights; }
	bool HasDriver() const { return Driver != nullptr; }

	void EnterCar(APeregonCharacter* Character);
	void ExitCar();
	APeregonCharacter* GetDriver() const { return Driver; }

	// Поставить машину на землю: физика выключается, по лучу ищется земля, машина ставится на колёса
	// и только потом снова падает в симуляцию. Так машина не «выстреливает» и не проваливается при появлении.
	void PlaceOnGround(const FTransform& Target);
	bool IsSettling() const { return bSettling; }

	// Цвет кузова (задаётся до BeginPlay, например при создании новой игры).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon")
	FLinearColor PaintColor = FLinearColor(0.32f, 0.04f, 0.04f);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UBoxComponent> Body;

	// Невидимая коробка салона: не даёт пешеходу пройти сквозь крышу. В физике кузова не участвует.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UBoxComponent> CabinBlocker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon")
	TObjectPtr<UPeregonVehicleMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon|Camera")
	TObjectPtr<USceneComponent> HeadPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon|Camera")
	TObjectPtr<USpringArmComponent> ChaseArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Peregon|Camera")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, Category = "Peregon|Lights")
	TObjectPtr<USpotLightComponent> HeadlightL;

	UPROPERTY(VisibleAnywhere, Category = "Peregon|Lights")
	TObjectPtr<USpotLightComponent> HeadlightR;

	// Подвижные элементы салона.
	UPROPERTY() TObjectPtr<USceneComponent> SteeringPivot;
	UPROPERTY() TObjectPtr<USceneComponent> SpeedoNeedle;
	UPROPERTY() TObjectPtr<USceneComponent> TachoNeedle;
	UPROPERTY() TObjectPtr<USceneComponent> FuelNeedle;
	UPROPERTY() TObjectPtr<USceneComponent> TempNeedle;
	UPROPERTY() TObjectPtr<USceneComponent> GearLever;
	UPROPERTY() TObjectPtr<USceneComponent> HandbrakeLever;
	UPROPERTY() TObjectPtr<USceneComponent> KeyPivot;
	UPROPERTY() TObjectPtr<USceneComponent> ClutchPedal;
	UPROPERTY() TObjectPtr<USceneComponent> BrakePedal;
	UPROPERTY() TObjectPtr<USceneComponent> GasPedal;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> WheelPivots;

	// Заполняется в конструкторе каждого экземпляра; компоненты — подобъекты актёра, GC их держит.
	TArray<FPeregonMovingPart> MovingParts;

private:
	// Построение «модели» из примитивов.
	UStaticMeshComponent* AddPart(const TCHAR* Name, USceneComponent* Parent, UStaticMesh* InMesh, const FVector& Location,
		const FVector& SizeCm, const FRotator& Rotation, const FLinearColor& Color);
	UStaticMeshComponent* AddBeam(const TCHAR* Name, USceneComponent* Parent, UStaticMesh* InMesh, const FVector& From, const FVector& To,
		float Thickness, const FLinearColor& Color);
	USceneComponent* AddPivot(const TCHAR* Name, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);
	void AddMovingPart(FName Tag, USceneComponent* Pivot, const FRotator& Open, float Speed = 2.5f);
	FPeregonMovingPart* FindMovingPart(FName Tag);
	const FPeregonMovingPart* FindMovingPart(FName Tag) const;
	void ToggleMovingPart(FName Tag);

	void UpdateMovingParts(float DeltaSeconds);
	void UpdateSettling(float DeltaSeconds);
	void UpdateCockpit(float DeltaSeconds, const FPeregonTelemetry& T);
	void UpdateCameras(float DeltaSeconds, const FPeregonTelemetry& T);
	void UpdateFocus();
	void SetFirstPerson(bool bEnable);
	void SetHeadlights(bool bOn);

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
	void OnLook(const FInputActionValue& Value);
	void OnStarterPressed(const FInputActionValue& Value);
	void OnStarterReleased(const FInputActionValue& Value);
	void OnShiftUp(const FInputActionValue& Value);
	void OnShiftDown(const FInputActionValue& Value);
	void OnToggleIgnition(const FInputActionValue& Value);
	void OnToggleGearbox(const FInputActionValue& Value);
	void OnToggleCamera(const FInputActionValue& Value);
	void OnResetCar(const FInputActionValue& Value);
	void OnInteractPressed(const FInputActionValue& Value);
	void OnInteractReleased(const FInputActionValue& Value);
	void OnExit(const FInputActionValue& Value);
	void OnToggleLights(const FInputActionValue& Value);
	void OnToggleParkingBrake(const FInputActionValue& Value);
	void OnToggleDetails(const FInputActionValue& Value);

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SteerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ClutchAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HandbrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> StarterAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ShiftUpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ShiftDownAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> IgnitionAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> GearboxModeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CameraAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ExitAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LightsAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ParkingBrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DetailsAction;

	UPROPERTY(Transient)
	TObjectPtr<APeregonCharacter> Driver;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> LampMaterials;

	// Цвета деталей (красятся в BeginPlay через динамические материалы).
	TArray<TPair<UStaticMeshComponent*, FLinearColor>> PartColors;

	FPeregonFocus Focus;
	TWeakObjectPtr<AActor> InteractingActor;
	TWeakObjectPtr<UPrimitiveComponent> InteractingPart;

	FRotator SteeringBaseRotation = FRotator::ZeroRotator;
	FVector HeadBaseLocation = FVector::ZeroVector;
	FVector HeadOffset = FVector::ZeroVector;
	FVector LastVelocity = FVector::ZeroVector;
	FVector SmoothedAccel = FVector::ZeroVector;
	float HeadYaw = 0.f;
	float HeadPitch = 0.f;
	float TimeSinceLook = 10.f;
	float ThrottleAxis = 0.f;
	float BrakeAxis = 0.f;
	float ThrottleSmoothed = 0.f;
	float BrakeSmoothed = 0.f;
	float ReverseHoldTime = 0.f;
	float CockpitTime = 0.f;
	bool bFirstPerson = true;
	bool bHeadlights = false;
	bool bKeyCranking = false;
	bool bSettling = true;
	float SettleTime = 0.f;
};
