#include "Vehicle/PeregonCar.h"

#include "Art/PeregonArtSubsystem.h"
#include "Game/PeregonGameUserSettings.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Game/PeregonHUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Player/PeregonCharacter.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "vsim/BodyModel.h"

namespace PeregonCarModel
{
// Модель строится один раз за запуск (≈0.1 с) и дальше переиспользуется всеми машинами.
const vmodel::Model& Get()
{
	static const vmodel::Model Model = vmodel::BuildSokol();
	return Model;
}

FVector ToVector(const vmodel::V3& V) { return FVector(V.X, V.Y, V.Z); }
} // namespace PeregonCarModel

namespace PeregonCarLook
{
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
// Конструктор: физика, узлы подвижных частей, камеры. Начало координат — центр кузова (≈ центр масс, 52 см над землёй).
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

	// --- Физика: один бокс нижней части кузова. Колёса — сферы-трассировки в компоненте движения.
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(205.f, 81.f, 30.f));
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Body->SetSimulatePhysics(true);
	Body->SetLinearDamping(0.01f);
	Body->SetAngularDamping(0.05f);
	// У плоского бокса крен «лёгкий»; у настоящего седана моменты инерции по крену и тангажу больше.
	Body->BodyInstance.InertiaTensorScale = FVector(1.7f, 1.15f, 1.0f);
	// Непрерывные столкновения: тонкая процедурная земля не «пропускает» кузов на скорости.
	Body->BodyInstance.bUseCCD = true;
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

	// --- Геометрия — процедурная модель «Сокол-1500» (VehicleSimCore/vsim/BodyModel). Здесь только узлы
	// подвижных частей и невидимые «мишени» взаимодействия; сами меши создаются в BeginPlay.
	const vmodel::Model& Model = PeregonCarModel::Get();
	PartPivots.SetNum(static_cast<int32>(vmodel::Part::Count));
	auto MakePartPivot = [&](vmodel::Part Id, const TCHAR* Name, const FRotator& Rotation = FRotator::ZeroRotator) {
		const vmodel::PartMesh* Mesh = Model.Find(Id);
		USceneComponent* Pivot = AddPivot(Name, Body, Mesh ? PeregonCarModel::ToVector(Mesh->Origin) : FVector::ZeroVector, Rotation);
		PartPivots[static_cast<int32>(Id)] = Pivot;
		return Pivot;
	};

	AddMovingPart(Hood, MakePartPivot(vmodel::Part::Hood, TEXT("HoodPivot")), FRotator(55.f, 0.f, 0.f), 1.6f);
	AddMovingPart(Trunk, MakePartPivot(vmodel::Part::Trunk, TEXT("TrunkPivot")), FRotator(-65.f, 0.f, 0.f), 1.6f);
	// Двери: петли спереди, открываются наружу.
	AddMovingPart(DoorFL, MakePartPivot(vmodel::Part::DoorFL, TEXT("DoorFLPivot")), FRotator(0.f, 65.f, 0.f), 3.f);
	AddMovingPart(DoorFR, MakePartPivot(vmodel::Part::DoorFR, TEXT("DoorFRPivot")), FRotator(0.f, -65.f, 0.f), 3.f);
	AddMovingPart(DoorRL, MakePartPivot(vmodel::Part::DoorRL, TEXT("DoorRLPivot")), FRotator(0.f, 65.f, 0.f), 3.f);
	AddMovingPart(DoorRR, MakePartPivot(vmodel::Part::DoorRR, TEXT("DoorRRPivot")), FRotator(0.f, -65.f, 0.f), 3.f);
	AddMovingPart(Glovebox, MakePartPivot(vmodel::Part::Glovebox, TEXT("GlovePivot")), FRotator(70.f, 0.f, 0.f), 3.f);

	// Руль: X узла — по оси колонки к водителю, обод в плоскости YZ.
	SteeringBaseRotation = FRotationMatrix::MakeFromX(PeregonCarModel::ToVector(Model.SteeringColumnDir)).Rotator();
	SteeringPivot = MakePartPivot(vmodel::Part::Steering, TEXT("SteeringPivot"), SteeringBaseRotation);
	SpeedoNeedle = MakePartPivot(vmodel::Part::SpeedoNeedle, TEXT("SpeedoNeedle"));
	TachoNeedle = MakePartPivot(vmodel::Part::TachoNeedle, TEXT("TachoNeedle"));
	FuelNeedle = MakePartPivot(vmodel::Part::FuelNeedle, TEXT("FuelNeedle"));
	TempNeedle = MakePartPivot(vmodel::Part::TempNeedle, TEXT("TempNeedle"));
	KeyPivot = MakePartPivot(vmodel::Part::Key, TEXT("KeyPivot"));
	LightKnobPivot = MakePartPivot(vmodel::Part::LightKnob, TEXT("LightKnobPivot"));
	GearLever = MakePartPivot(vmodel::Part::GearLever, TEXT("GearLever"));
	HandbrakeLever = MakePartPivot(vmodel::Part::Handbrake, TEXT("HandbrakeLever"));
	ClutchPedal = MakePartPivot(vmodel::Part::ClutchPedal, TEXT("ClutchPedal"));
	BrakePedal = MakePartPivot(vmodel::Part::BrakePedal, TEXT("BrakePedal"));
	GasPedal = MakePartPivot(vmodel::Part::GasPedal, TEXT("GasPedal"));
	PartPivots[static_cast<int32>(vmodel::Part::Body)] = Body;

	// Мишени взаимодействия: невидимые боксы, ловят только луч Interact.
	for (int32 I = 0; I < static_cast<int32>(Model.HitBoxes.size()); ++I)
	{
		const vmodel::HitBox& H = Model.HitBoxes[static_cast<size_t>(I)];
		USceneComponent* Parent = PartPivots[static_cast<int32>(H.Owner)];
		UBoxComponent* Hit = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("Hit_%s_%d"), ANSI_TO_TCHAR(H.Tag), I));
		Hit->SetupAttachment(Parent ? Parent : Body.Get());
		Hit->SetRelativeLocation(PeregonCarModel::ToVector(H.Center));
		Hit->SetBoxExtent(PeregonCarModel::ToVector(H.HalfSize));
		Hit->SetHiddenInGame(true);
		Hit->SetCanEverAffectNavigation(false);
		PeregonInteraction::MakeInteractable(Hit, FName(ANSI_TO_TCHAR(H.Tag)));
	}

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
	HeadlightL->SetRelativeLocation(PeregonCarModel::ToVector(Model.HeadlightL));
	HeadlightR->SetRelativeLocation(PeregonCarModel::ToVector(Model.HeadlightR));

	// --- Колёса (позиции берём у физики, меш — в BeginPlay)
	for (int32 I = 0; I < Movement->GetNumWheels(); ++I)
	{
		USceneComponent* Pivot = AddPivot(*FString::Printf(TEXT("WheelPivot%d"), I), Body, Movement->GetWheelAttachLocal(I));
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
	BuildModelMeshes();
	HeadBaseLocation = HeadPivot->GetRelativeLocation();
	SetFirstPerson(UPeregonGameUserSettings::Get()->bStartFirstPerson);
	PlaceOnGround(GetActorTransform());
	SetHeadlights(false);
}

// ---------------------------------------------------------------------------------------------
// Меши из процедурной модели: по компоненту на узел, по секции на материал.
// ---------------------------------------------------------------------------------------------
UMaterialInterface* APeregonCar::MakeSlotMaterial(uint8 Slot)
{
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	if (!Art)
	{
		return nullptr;
	}
	const vmodel::Mat M = static_cast<vmodel::Mat>(Slot);
	if (M == vmodel::Mat::Glass)
	{
		return Art->MakeGlassMaterial(this, FLinearColor(0.42f, 0.5f, 0.52f), 0.22f);
	}
	FPeregonLook Look;
	switch (M)
	{
	case vmodel::Mat::Paint: Look = FPeregonLook(EPeregonTex::Paint, PaintColor, 0.35f); break;
	case vmodel::Mat::Chrome: Look = FPeregonLook(EPeregonTex::Chrome, FLinearColor(0.9f, 0.9f, 0.88f), 0.18f, 1.f); break;
	case vmodel::Mat::BlackTrim: Look = FPeregonLook(EPeregonTex::Plastic, FLinearColor(0.035f, 0.035f, 0.04f), 0.6f); break;
	case vmodel::Mat::Rubber: Look = FPeregonLook(EPeregonTex::Rubber, FLinearColor(0.11f, 0.11f, 0.11f), 0.92f); break;
	case vmodel::Mat::Headlamp: Look = FPeregonLook(EPeregonTex::Glass, FLinearColor(0.9f, 0.88f, 0.78f), 0.12f); break;
	case vmodel::Mat::Taillamp: Look = FPeregonLook(EPeregonTex::Glass, FLinearColor(0.6f, 0.03f, 0.02f), 0.15f); break;
	case vmodel::Mat::Indicator: Look = FPeregonLook(EPeregonTex::Glass, FLinearColor(0.9f, 0.42f, 0.04f), 0.15f); break;
	case vmodel::Mat::Interior: Look = FPeregonLook(EPeregonTex::Plastic, FLinearColor(0.2f, 0.19f, 0.17f), 0.8f); break;
	case vmodel::Mat::InteriorDark: Look = FPeregonLook(EPeregonTex::Plastic, FLinearColor(0.055f, 0.052f, 0.05f), 0.7f); break;
	case vmodel::Mat::Fabric: Look = FPeregonLook(EPeregonTex::Fabric, FLinearColor(0.34f, 0.16f, 0.08f), 0.95f); break;
	case vmodel::Mat::Carpet: Look = FPeregonLook(EPeregonTex::Fabric, FLinearColor(0.09f, 0.08f, 0.07f), 1.f); break;
	case vmodel::Mat::Headliner: Look = FPeregonLook(EPeregonTex::Fabric, FLinearColor(0.6f, 0.58f, 0.52f), 0.95f); break;
	case vmodel::Mat::GaugeFace: Look = FPeregonLook(EPeregonTex::White, FLinearColor(0.85f, 0.83f, 0.74f), 0.6f); break;
	case vmodel::Mat::Needle: Look = FPeregonLook(EPeregonTex::White, FLinearColor(0.95f, 0.3f, 0.05f), 0.5f); break;
	case vmodel::Mat::Engine: Look = FPeregonLook(EPeregonTex::Plastic, FLinearColor(0.16f, 0.16f, 0.17f), 0.5f, 0.7f); break;
	case vmodel::Mat::Metal: Look = FPeregonLook(EPeregonTex::Chrome, FLinearColor(0.32f, 0.32f, 0.33f), 0.55f, 0.8f); break;
	case vmodel::Mat::Plate: Look = FPeregonLook(EPeregonTex::White, FLinearColor(0.9f, 0.9f, 0.87f), 0.5f); break;
	default: Look = FPeregonLook(EPeregonTex::White, FLinearColor::White, 0.8f); break;
	}
	UMaterialInstanceDynamic* Mid = Art->MakeMaterial(this, Look);
	if (Mid && (M == vmodel::Mat::Headlamp || M == vmodel::Mat::Taillamp))
	{
		LampMaterials.Add(Mid);
	}
	if (Mid && (M == vmodel::Mat::GaugeFace || M == vmodel::Mat::Needle))
	{
		GaugeMaterials.Add(Mid);
	}
	return Mid;
}

void APeregonCar::BuildModelMeshes()
{
	const vmodel::Model& Model = PeregonCarModel::Get();
	LampMaterials.Reset();
	GaugeMaterials.Reset();
	TArray<UMaterialInterface*> SlotMaterials;
	for (int32 Slot = 0; Slot < static_cast<int32>(vmodel::Mat::Count); ++Slot)
	{
		SlotMaterials.Add(MakeSlotMaterial(static_cast<uint8>(Slot)));
	}

	auto Spawn = [&](const vmodel::PartMesh& Part, USceneComponent* Parent, const FString& Name) {
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, *Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->RegisterComponent();
		int32 SectionIndex = 0;
		for (const vmodel::Section& S : Part.Sections)
		{
			const int32 N = static_cast<int32>(S.Positions.size());
			TArray<FVector> Vertices;
			TArray<FVector> Normals;
			TArray<FVector2D> UVs;
			Vertices.Reserve(N);
			Normals.Reserve(N);
			UVs.Reserve(N);
			for (int32 K = 0; K < N; ++K)
			{
				Vertices.Add(PeregonCarModel::ToVector(S.Positions[static_cast<size_t>(K)]));
				Normals.Add(PeregonCarModel::ToVector(S.Normals[static_cast<size_t>(K)]));
				UVs.Add(FVector2D(S.U[static_cast<size_t>(K)], S.V[static_cast<size_t>(K)]));
			}
			TArray<int32> Triangles;
			Triangles.Append(S.Indices.data(), static_cast<int32>(S.Indices.size()));
			Mesh->CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
			UMaterialInterface* Material = SlotMaterials[static_cast<int32>(S.Material)];
			if (Material)
			{
				Mesh->SetMaterial(SectionIndex, Material);
			}
			else if (S.Material == vmodel::Mat::Glass)
			{
				Mesh->SetMeshSectionVisible(SectionIndex, false); // нет стеклянного материала — лучше без стёкол, чем непрозрачные
			}
			++SectionIndex;
		}
		ModelMeshes.Add(Mesh);
	};

	for (const vmodel::PartMesh& Part : Model.Parts)
	{
		if (Part.Id == vmodel::Part::WheelLeft || Part.Id == vmodel::Part::WheelRight)
		{
			continue;
		}
		USceneComponent* Parent = PartPivots.IsValidIndex(static_cast<int32>(Part.Id)) ? PartPivots[static_cast<int32>(Part.Id)].Get() : nullptr;
		Spawn(Part, Parent ? Parent : Body.Get(), FString::Printf(TEXT("Model_%s"), ANSI_TO_TCHAR(vmodel::PartName(Part.Id))));
	}
	const vmodel::PartMesh* WheelL = Model.Find(vmodel::Part::WheelLeft);
	const vmodel::PartMesh* WheelR = Model.Find(vmodel::Part::WheelRight);
	for (int32 I = 0; I < WheelPivots.Num(); ++I)
	{
		const bool bLeft = Movement->GetWheelAttachLocal(I).Y < 0.f;
		if (const vmodel::PartMesh* Wheel = bLeft ? WheelL : WheelR)
		{
			Spawn(*Wheel, WheelPivots[I], FString::Printf(TEXT("Model_Wheel%d"), I));
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[Peregon] Модель машины: %d узлов, %d треугольников"), ModelMeshes.Num(), static_cast<int32>(Model.TriangleCount()));
}

void APeregonCar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Movement)
	{
		return;
	}
	CockpitTime += DeltaSeconds;
	if (bSettling)
	{
		UpdateSettling(DeltaSeconds);
	}

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

void APeregonCar::PlaceOnGround(const FTransform& Target)
{
	Body->SetSimulatePhysics(false);
	FRotator Rotation = Target.Rotator();
	Rotation.Roll = 0.f;
	SetActorLocationAndRotation(Target.GetLocation(), Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	bSettling = true;
	SettleTime = 0.f;
}

void APeregonCar::UpdateSettling(float DeltaSeconds)
{
	SettleTime += DeltaSeconds;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PeregonCarSettle), false, this);
	FHitResult Hit;
	const FVector Start = GetActorLocation() + FVector(0.f, 0.f, 300.f);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, 5000.f), ECC_Visibility, Params);
	// Ждём пару кадров, чтобы у сгенерированной земли точно появилась коллизия.
	if (!(bHit && SettleTime > 0.15f) && SettleTime < 5.f)
	{
		return;
	}
	if (bHit)
	{
		// 64 см над землёй: колёса ровно касаются её на полном отбое, машина мягко садится на пружины.
		SetActorLocation(Hit.ImpactPoint + FVector(0.f, 0.f, 64.f), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Log, TEXT("[Peregon] Машина поставлена на %s"), *GetNameSafe(Hit.GetComponent()));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Peregon] Под машиной нет земли с коллизией (канал Visibility) — проверьте генерацию мира"));
	}
	bSettling = false;
	Body->SetSimulatePhysics(true);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
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
	SteeringPivot->SetRelativeRotation(SteeringBaseRotation.Quaternion() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(T.SteeringWheelDeg)));

	auto SetNeedle = [](USceneComponent* Needle, float Fraction, float Sweep) {
		const float Angle = FMath::Lerp(-Sweep, Sweep, FMath::Clamp(Fraction, 0.f, 1.f));
		// Положительный угол по оси X поворачивает стрелку против часовой (с места водителя), поэтому минус.
		Needle->SetRelativeRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-Angle)));
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
		const UPeregonGameUserSettings* Settings = UPeregonGameUserSettings::Get();
		FirstPersonCamera->SetFieldOfView(Settings->FieldOfView);
		const FVector A = GetActorTransform().InverseTransformVectorNoScale(SmoothedAccel);
		FVector Target = FVector(-A.X * 0.6f, -A.Y * 0.5f, -A.Z * 0.25f) * Settings->HeadMotion;
		Target = Target.BoundToBox(FVector(-6.f, -6.f, -4.f), FVector(6.f, 6.f, 4.f));
		// Мелкая дрожь мотора: на холостых заметнее, на оборотах — частая и мелкая.
		if (T.bEngineRunning)
		{
			const float Amp = (0.06f + 0.04f * FMath::Clamp(T.Rpm / 6000.f, 0.f, 1.f)) * Settings->HeadMotion;
			const float Freq = 15.f + T.Rpm / 200.f;
			Target += FVector(0.f, FMath::PerlinNoise1D(CockpitTime * Freq), FMath::PerlinNoise1D(CockpitTime * Freq + 17.f)) * Amp;
		}
		HeadOffset = FMath::VInterpTo(HeadOffset, Target, Dt, 12.f);
		HeadPivot->SetRelativeLocation(HeadBaseLocation + HeadOffset);
		FirstPersonCamera->SetRelativeRotation(FRotator(HeadPitch, HeadYaw, 0.f));
	}
	else
	{
		ChaseCamera->SetFieldOfView(UPeregonGameUserSettings::Get()->FieldOfView);
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
	for (UMaterialInstanceDynamic* Mid : LampMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Emissive"), bOn ? 6.f : 0.f);
		}
	}
	// Подсветка приборов.
	for (UMaterialInstanceDynamic* Mid : GaugeMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Emissive"), bOn ? 0.6f : 0.f);
		}
	}
	// Ручка света «вытянута», когда фары горят.
	if (LightKnobPivot)
	{
		const vmodel::PartMesh* Knob = PeregonCarModel::Get().Find(vmodel::Part::LightKnob);
		const FVector Base = Knob ? PeregonCarModel::ToVector(Knob->Origin) : LightKnobPivot->GetRelativeLocation();
		LightKnobPivot->SetRelativeLocation(Base + FVector(bOn ? -1.2f : 0.f, 0.f, 0.f));
	}
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
	const UPeregonGameUserSettings* Settings = UPeregonGameUserSettings::Get();
	const FVector2D V(Value.Get<FVector2D>().X * Settings->MouseSensitivity,
		Value.Get<FVector2D>().Y * Settings->MouseSensitivity * (Settings->bInvertY ? -1.f : 1.f));
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
	PlaceOnGround(FTransform(Rotation, GetActorLocation() + FVector(0.f, 0.f, 150.f)));
}
