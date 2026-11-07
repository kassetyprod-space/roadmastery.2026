// Машина игрока: процедурная модель «Сокол-1500» (цельный кузов, салон, моторный отсек) в PSX-духе.
// Двери, капот, багажник, бардачок открываются; ключ, фары, ручник работают мышкой из салона.
// Вид от 1-го лица (с инерцией головы на перегрузках) и от 3-го лица (орбитальная камера).
// Ввод (Enhanced Input) собирается в коде, чтобы проект запускался без бинарных .uasset.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Game/PeregonInventory.h"
#include "Interaction/PeregonInteractable.h"
#include "vsim/CarParts.h"
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
class UMaterialInterface;
class UProceduralMeshComponent;
class UPeregonSynth;
class UPeregonRadio;
class UPointLightComponent;
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
	// Дворники: 0 — выключены, 1 — медленно, 2 — быстро.
	int32 GetWiperMode() const { return WiperMode; }
	// Печка и климат в салоне.
	int32 GetHeaterMode() const { return HeaterMode; }
	float GetCabinTemp() const { return CabinTempC; }
	float GetOutsideTemp() const { return OutsideTempC; }
	float GetChill() const { return Chill; }
	float GetFogLevel() const { return FogLevel; }
	bool IsVented() const { return FMath::Max(FMath::Max(WindowOpen[0], WindowOpen[1]), FMath::Max(WindowOpen[2], WindowOpen[3])) > 0.08f; }
	bool HasDriver() const { return Driver != nullptr; }

	void EnterCar(APeregonCharacter* Character, int32 InSeat = 0);
	void ExitCar();
	APeregonCharacter* GetDriver() const { return Driver; }

	// Поставить машину на землю: физика выключается, по лучу ищется земля, машина ставится на колёса
	// и только потом снова падает в симуляцию. Так машина не «выстреливает» и не проваливается при появлении.
	void PlaceOnGround(const FTransform& Target);
	bool IsSettling() const { return bSettling; }

	// Цвет кузова (задаётся до BeginPlay, например при создании новой игры).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon")
	FLinearColor PaintColor = FLinearColor(0.32f, 0.04f, 0.04f);

	// --- Узлы, детали и кузов (PeregonCarParts.cpp) ---
	// Что стоит в машине и в каком состоянии. Задать до FinishSpawning — иначе машина «как с завода».
	void SetParts(const vsim::parts::CarState& State);
	const vsim::parts::CarState& GetParts() const { return PartsState; }
	const vsim::parts::Effects& GetPartEffects() const { return PartsEffects; }
	bool HasDashboard() const { return PartsEffects.bDashboard; }
	// Бардачок и багажник у каждой машины свои.
	FPeregonContainer* GetStorage(EPeregonStorage Storage);
	// Идёт ли работа (снять/поставить/кузов) и насколько она сделана 0..1.
	bool IsWorking() const { return Job.bActive; }
	float GetWorkProgress() const { return Job.Duration > 0.f ? Job.Elapsed / Job.Duration : 0.f; }

	UPROPERTY(SaveGame) FPeregonContainer TrunkStore;
	UPROPERTY(SaveGame) FPeregonContainer GloveStore;

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
	void OnZoomPressed(const FInputActionValue& Value);
	void OnZoomReleased(const FInputActionValue& Value);
	void OnCycleWipers(const FInputActionValue& Value);
	void UpdateWipers(float DeltaSeconds);

	// Салон (PeregonCarCabin.cpp): радио, плафон, сигнал, поворотники, места, виды камеры.
	void BuildCabinExtras();
	void UpdateCabin(float DeltaSeconds);
	void SetDomeLight(bool bOn);
	void SetBlinkers(int32 Mode);
	void SwitchSeat(int32 NewSeat);
	FVector SeatEye(int32 InSeat) const;
	FName SeatDoor(int32 InSeat) const;
	void SetCameraMode(int32 Mode);
	bool CanDrive() const { return Seat == 0; }
	void OnRadioPower(const FInputActionValue& Value);
	void OnRadioNext(const FInputActionValue& Value);
	void OnDome(const FInputActionValue& Value);
	void OnHornPressed(const FInputActionValue& Value);
	void OnHornReleased(const FInputActionValue& Value);
	void OnBlinkLeft(const FInputActionValue& Value);
	void OnBlinkRight(const FInputActionValue& Value);
	void OnHazard(const FInputActionValue& Value);
	void OnNextSeat(const FInputActionValue& Value);
	void OnWheel(const FInputActionValue& Value);
	void OnHeater(const FInputActionValue& Value);
	void SetHeater(int32 Mode);
	int32 HeaterMode = 0;      // 0 — выкл, 1 — слабо, 2 — сильно
	float CabinTempC = 12.f;
	float OutsideTempC = 12.f;
	float Chill = 0.f;         // 0..1 насколько замёрзли люди в салоне
	bool bClimateInit = false;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HeaterAction;
	// Стёкла (опускание, запотевание) и грязь на кузове.
	void UpdateGlassAndDirt(float DeltaSeconds);
	void ApplyDirt();
	int32 WindowIndex(const UPrimitiveComponent* Part) const;

	// Узлы и кузов.
	void BuildPartSlots();
	void UpdateParts(float DeltaSeconds, const FPeregonTelemetry& T);
	void RebuildPartsMesh();
	FString PartsPrompt(const UPrimitiveComponent* Part, const APawn* User) const;
	bool PartsInteract(UPrimitiveComponent* Part, APawn* User);
	void PartsInteractStop(UPrimitiveComponent* Part);
	// Панель кузова под прицелом (−1 — не панель).
	int32 PanelOf(const UPrimitiveComponent* Part) const;
	bool IsHoodOpen() const;
	void StartJob(const FString& Label, float Seconds, UPrimitiveComponent* Part, APawn* User, TFunction<void()> Finish);
	struct FPartsJob
	{
		bool bActive = false;
		FString Label;
		float Elapsed = 0.f;
		float Duration = 1.f;
		TWeakObjectPtr<UPrimitiveComponent> Part;
		TWeakObjectPtr<APawn> User;
		TFunction<void()> Finish;
	};
	FPartsJob Job;
	vsim::parts::CarState PartsState;
	vsim::parts::Effects PartsEffects;
	bool bPartsAssigned = false;
	bool bPartsDirty = true;
	uint32 TrunkHash = 0;
	bool bBodyDirty = true;
	double LastOdometerKm = -1.0;
	float PartsToastCooldown = 0.f;
	UPROPERTY(Transient) TArray<TObjectPtr<UBoxComponent>> SlotBoxes;   // по vsim::parts::Slot
	UPROPERTY(Transient) TArray<TObjectPtr<UBoxComponent>> PanelBoxes;  // крылья, четверти, крыша, пороги
	UPROPERTY(Transient) TArray<TObjectPtr<UBoxComponent>> FillBoxes;   // 0 — масло, 1 — антифриз
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> PartsMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PartsMaterial;

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
	UPROPERTY(Transient) TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> WipersAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RadioAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RadioNextAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DomeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HornAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BlinkLeftAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BlinkRightAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HazardAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SeatAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> WheelAction;
	// Опускные стёкла дверей (FL, FR, RL, RR) и общий материал стёкол.
	UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> WindowGlass;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlassMaterial;
	float WindowOpen[4] = {0.f, 0.f, 0.f, 0.f};
	float FogLevel = 0.f;     // 0..1 запотевание изнутри
	float DirtLevel = 0.f;    // 0..1 грязь на кузове
	float AppliedDirt = -1.f;
	float EngineWarm = 0.f;   // 0..1 печка прогрелась
	float RadioVolume = 0.75f;
	int32 DomeMode = 0;       // 0 — по дверям (авто), 1 — горит, 2 — выключен
	// Секции кузова, которые пачкаются: копия геометрии и маска грязи по вершинам.
	struct FDirtSection
	{
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;
		int32 Section = 0;
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<float> Mask;
		// Краска кузова: панель каждой вершины (255 — не краска) и шум для пятен ржавчины.
		TArray<uint8> PanelIndex;
		TArray<float> Noise;
	};
	TArray<FDirtSection> DirtSections;

	UPROPERTY(Transient) TObjectPtr<UPeregonRadio> Radio;
	UPROPERTY(Transient) TObjectPtr<UPeregonSynth> HornSound;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> DomeLight;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DomeMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RadioDialMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BlinkerLeft;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BlinkerRight;
	TWeakObjectPtr<AActor> RoadWorldRef;
	int32 Seat = 0;          // 0 — водитель, 1 — переднее пассажирское, 2 — заднее левое, 3 — заднее правое
	int32 CameraMode = 0;    // 0 — из салона, 1 — сзади, 2 — сверху издалека
	int32 BlinkMode = 0;     // 0 — нет, 1 — левый, 2 — правый, 3 — аварийка
	float BlinkTime = 0.f;
	bool bBlinkLit = false;
	bool bDome = false;
	bool bHorn = false;

	// Дворники и вода на лобовом стекле.
	UPROPERTY() TObjectPtr<USceneComponent> WiperL;
	UPROPERTY() TObjectPtr<USceneComponent> WiperR;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WaterMaterial;
	// Капли там, куда щётки не достают; полосы запотевания стёкол (0 — у рамки, 2 — центр).
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WaterEdgeMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> GlassBands;
	float WaterEdge = 0.f;
	float SnowEdge = 0.f;
	bool IsWiped(const FVector& BodyPoint) const;
	int32 WiperMode = 0;
	float WiperPhase = 0.f;   // 0..1 — один ход туда и обратно
	float WaterLevel = 0.f;   // 0..1 — сколько воды на стекле
	float SnowOnGlass = 0.f;  // 0..1 — снежинки
	bool bZoom = false;
	float ZoomFov = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<APeregonCharacter> Driver;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> LampMaterials;

	// Цвета деталей (красятся в BeginPlay через динамические материалы).
	// Узлы модели по vmodel::Part (Body — корень).
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> PartPivots;
	UPROPERTY() TObjectPtr<USceneComponent> LightKnobPivot;
	UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> ModelMeshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> GaugeMaterials;
	void BuildModelMeshes();
	void BuildSounds();
	void UpdateSounds(float DeltaSeconds, const FPeregonTelemetry& T);
	void PlayPartSound(const FPeregonMovingPart& Part, bool bOpen) const;

	UPROPERTY(Transient) TObjectPtr<UPeregonSynth> EngineSound;
	UPROPERTY(Transient) TObjectPtr<UPeregonSynth> TireSound;
	UPROPERTY(Transient) TObjectPtr<UPeregonSynth> WindSound;
	UPROPERTY(Transient) TObjectPtr<UPeregonSynth> SkidSound;
	int32 LastGear = 0;
	bool bLastGrind = false;
	UMaterialInterface* MakeSlotMaterial(uint8 Slot);

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
