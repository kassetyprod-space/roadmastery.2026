// Мост между ядром физики (VehicleSimCore) и Unreal.
// Каждый кадр: трассирует лучи колёс, собирает контакт и скорость пятна, шагает ядро,
// прикладывает силы подвески/шин к кузову и двигает визуальные колёса.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/EngineTypes.h"
#include "vsim/Vehicle.h"
#include "PeregonVehicleMovementComponent.generated.h"

class UPrimitiveComponent;
class UPhysicalMaterial;
class USceneComponent;

// Свойства покрытия (ставятся по типу физического материала, см. Config/DefaultEngine.ini).
USTRUCT(BlueprintType)
struct FPeregonSurface
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Grip = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float RollingResistance = 1.0f;

	// 0 — протектор не важен (сухой асфальт), 1 — критичен (снег, грязь).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float TreadSensitivity = 0.0f;

	// 0..1, неровность: ускоряет износ амортизаторов.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Roughness = 0.0f;
};

// Телеметрия для HUD и Blueprints.
USTRUCT(BlueprintType)
struct FPeregonTelemetry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float SpeedKmh = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float Rpm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") int32 Gear = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float ClutchEngagement = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float FuelL = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float FuelCapacityL = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float InstantLPer100Km = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float EngineTempC = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float OdometerKm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") bool bEngineRunning = false;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") bool bFanOn = false;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") bool bGearGrind = false;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float EngineCondition = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float ClutchCondition = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") float RadiatorCondition = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") TArray<float> TreadMm;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") TArray<float> PadMm;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") TArray<float> ShockCondition;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") TArray<float> SlipRatio;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") TArray<float> SlipAngleDeg;
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry") TArray<float> WheelLoadN;
};

UENUM(BlueprintType)
enum class EPeregonVehiclePreset : uint8
{
	Sokol1500 UMETA(DisplayName = "Сокол-1500 (седан, задний привод)")
};

UCLASS(ClassGroup = (Peregon), meta = (BlueprintSpawnableComponent))
class PEREGON_API UPeregonVehicleMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPeregonVehicleMovementComponent();
	virtual ~UPeregonVehicleMovementComponent() override;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Управление (вызывает пешка) ---
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") void SetThrottle(float Value) { Throttle = FMath::Clamp(Value, 0.f, 1.f); }
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") void SetBrake(float Value) { Brake = FMath::Clamp(Value, 0.f, 1.f); }
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") void SetSteering(float Value) { Steering = FMath::Clamp(Value, -1.f, 1.f); }
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") void SetClutchPedal(float Value) { ClutchPedal = FMath::Clamp(Value, 0.f, 1.f); }
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") void SetHandbrake(float Value) { Handbrake = FMath::Clamp(Value, 0.f, 1.f); }
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") void SetStarter(bool bOn) { bStarter = bOn; }
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") void SetIgnition(bool bOn) { bIgnition = bOn; }
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") bool GetIgnition() const { return bIgnition; }
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") bool ShiftUp();
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") bool ShiftDown();
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") bool RequestGear(int32 Gear);
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") int32 GetGear() const;
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") void SetAutoGearbox(bool bAuto);
	UFUNCTION(BlueprintCallable, Category = "Peregon|Input") bool IsAutoGearbox() const { return bAutoGearbox; }

	// Продольная скорость кузова, м/с (знак — вперёд/назад).
	UFUNCTION(BlueprintCallable, Category = "Peregon") float GetForwardSpeed() const { return ForwardSpeedMs; }
	UFUNCTION(BlueprintCallable, Category = "Peregon") FPeregonTelemetry GetTelemetry() const;

	// --- Ремонт / заправка ---
	UFUNCTION(BlueprintCallable, Category = "Peregon|Service") void Refuel(float Liters);
	UFUNCTION(BlueprintCallable, Category = "Peregon|Service") void RepairAll();

	// Позиция визуального колеса относительно корня (см) — для пешки, которая строит меши.
	FVector GetWheelAttachLocal(int32 Index) const;
	float GetWheelRadiusCm(int32 Index) const;
	float GetWheelWidthCm(int32 Index) const;
	int32 GetNumWheels() const;
	float GetVehicleMassKg() const;

	// --- Настройки ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Peregon|Vehicle")
	EPeregonVehiclePreset Preset = EPeregonVehiclePreset::Sokol1500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|Assists")
	bool bAutoGearbox = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|Assists")
	bool bAutoClutch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|Assists")
	bool bSpeedSensitiveSteering = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|Assists")
	bool bAbs = false;

	// Высота точки приложения сил шины над пятном контакта (см). 0 — честно, но машину легко опрокинуть;
	// поднятие точки имитирует центр крена подвески.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|Vehicle", meta = (ClampMin = "0"))
	float TireForceHeightCm = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|Vehicle")
	TEnumAsByte<ECollisionChannel> WheelTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|World")
	float AmbientTemperatureC = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|World")
	TMap<TEnumAsByte<EPhysicalSurface>, FPeregonSurface> Surfaces;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|World")
	FPeregonSurface DefaultSurface;

	// Визуальные колёса в порядке FL, FR, RL, RR. Позиция и поворот выставляются каждый кадр.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|Visual")
	TArray<TObjectPtr<USceneComponent>> WheelVisuals;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon|Debug")
	bool bDrawDebug = false;

private:
	void EnsureVehicle();
	void StepVehicle(float DeltaTime, UPrimitiveComponent* Body);
	void UpdateWheelVisuals();
	FPeregonSurface LookupSurface(const UPhysicalMaterial* Material) const;

	TUniquePtr<vsim::Vehicle> Sim;

	float Throttle = 0.f;
	float Brake = 0.f;
	float Steering = 0.f;
	float ClutchPedal = 0.f;
	float Handbrake = 0.f;
	bool bStarter = false;
	bool bIgnition = true;
	float ForwardSpeedMs = 0.f;
	TArray<float> WheelCompressionM;
	TArray<bool> WheelInContact;
};
