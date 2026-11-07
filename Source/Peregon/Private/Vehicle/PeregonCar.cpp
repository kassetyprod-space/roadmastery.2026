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
#include "Game/PeregonGameInstance.h"
#include "Game/PeregonPlayerController.h"
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
#include "Audio/PeregonAudio.h"
#include "Audio/PeregonRadio.h"
#include "EngineUtils.h"
#include "Game/PeregonGameMode.h"
#include "World/PeregonSky.h"

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

const vmodel::Model& PeregonCarModelData() { return PeregonCarModel::Get(); }

namespace PeregonCarLook
{
const FName DoorFL(TEXT("DoorFL"));
const FName DoorFR(TEXT("DoorFR"));
const FName DoorRL(TEXT("DoorRL"));
const FName DoorRR(TEXT("DoorRR"));
const FName Hood(TEXT("Hood"));
const FName Trunk(TEXT("Trunk"));
const FName Glovebox(TEXT("Glovebox"));
const FName VisorL(TEXT("VisorL"));
const FName VisorR(TEXT("VisorR"));
const FName Key(TEXT("Key"));
const FName Lights(TEXT("Lights"));
const FName Handbrake(TEXT("Handbrake"));
const FName Fuel(TEXT("Fuel"));
const FName GloveInside(TEXT("GloveInside"));
const FName TrunkInside(TEXT("TrunkInside"));

const TCHAR* PartNoun(FName Tag)
{
	if (Tag == DoorFR) return TEXT("дверь");
	if (Tag == DoorRL || Tag == DoorRR) return TEXT("заднюю дверь");
	if (Tag == Hood) return TEXT("капот");
	if (Tag == Trunk) return TEXT("багажник");
	if (Tag == Glovebox) return TEXT("бардачок");
	if (Tag == VisorL || Tag == VisorR) return TEXT("козырёк");
	return TEXT("");
}

constexpr float MaxExitSpeed = 3.f; // м/с
} // namespace PeregonCarLook

namespace PeregonGlassSplit
{
// Треугольники без общих вершин (так проще дробить и раскладывать по секциям).
struct FSoup
{
	TArray<FVector> V;
	TArray<FVector> N;
	TArray<FVector2D> UV;
	int32 Num() const { return V.Num() / 3; }
	FVector Center(int32 T) const { return (V[T * 3] + V[T * 3 + 1] + V[T * 3 + 2]) / 3.0; }
};

void Split(FSoup& Out, const FVector (&P)[3], const FVector (&Nn)[3], const FVector2D (&U)[3], float MaxEdge, int32 Depth)
{
	const float L = FMath::Max3(FVector::Dist(P[0], P[1]), FVector::Dist(P[1], P[2]), FVector::Dist(P[2], P[0]));
	if (L <= MaxEdge || Depth >= 6)
	{
		for (int32 K = 0; K < 3; ++K)
		{
			Out.V.Add(P[K]);
			Out.N.Add(Nn[K]);
			Out.UV.Add(U[K]);
		}
		return;
	}
	// Середины рёбер: четыре треугольника вместо одного.
	const FVector M[3] = {(P[0] + P[1]) * 0.5, (P[1] + P[2]) * 0.5, (P[2] + P[0]) * 0.5};
	const FVector Mn[3] = {(Nn[0] + Nn[1]).GetSafeNormal(), (Nn[1] + Nn[2]).GetSafeNormal(), (Nn[2] + Nn[0]).GetSafeNormal()};
	const FVector2D Mu[3] = {(U[0] + U[1]) * 0.5, (U[1] + U[2]) * 0.5, (U[2] + U[0]) * 0.5};
	const FVector A[3] = {P[0], M[0], M[2]};
	const FVector An[3] = {Nn[0], Mn[0], Mn[2]};
	const FVector2D Au[3] = {U[0], Mu[0], Mu[2]};
	const FVector B[3] = {M[0], P[1], M[1]};
	const FVector Bn[3] = {Mn[0], Nn[1], Mn[1]};
	const FVector2D Bu[3] = {Mu[0], U[1], Mu[1]};
	const FVector C[3] = {M[2], M[1], P[2]};
	const FVector Cn[3] = {Mn[2], Mn[1], Nn[2]};
	const FVector2D Cu[3] = {Mu[2], Mu[1], U[2]};
	Split(Out, A, An, Au, MaxEdge, Depth + 1);
	Split(Out, B, Bn, Bu, MaxEdge, Depth + 1);
	Split(Out, C, Cn, Cu, MaxEdge, Depth + 1);
	Split(Out, M, Mn, Mu, MaxEdge, Depth + 1);
}

FSoup Subdivide(const TArray<FVector>& V, const TArray<FVector>& N, const TArray<FVector2D>& UV, const TArray<int32>& Tris, float MaxEdge)
{
	FSoup Out;
	for (int32 T = 0; T + 2 < Tris.Num(); T += 3)
	{
		const FVector P[3] = {V[Tris[T]], V[Tris[T + 1]], V[Tris[T + 2]]};
		const FVector Nn[3] = {N[Tris[T]], N[Tris[T + 1]], N[Tris[T + 2]]};
		const FVector2D U[3] = {UV[Tris[T]], UV[Tris[T + 1]], UV[Tris[T + 2]]};
		Split(Out, P, Nn, U, MaxEdge, 0);
	}
	return Out;
}

// Края стекла: рёбра, у которых только один треугольник (вершины сравниваются по положению).
TArray<TPair<FVector, FVector>> BoundaryEdges(const TArray<FVector>& V, const TArray<int32>& Tris)
{
	auto Key = [](const FVector& P) { return FIntVector(FMath::RoundToInt(P.X * 10.0), FMath::RoundToInt(P.Y * 10.0), FMath::RoundToInt(P.Z * 10.0)); };
	TMap<TPair<FIntVector, FIntVector>, int32> Count;
	TMap<TPair<FIntVector, FIntVector>, TPair<FVector, FVector>> Ends;
	for (int32 T = 0; T + 2 < Tris.Num(); T += 3)
	{
		for (int32 E = 0; E < 3; ++E)
		{
			const FVector& A = V[Tris[T + E]];
			const FVector& B = V[Tris[T + (E + 1) % 3]];
			FIntVector Ka = Key(A);
			FIntVector Kb = Key(B);
			if (Kb.X < Ka.X || (Kb.X == Ka.X && (Kb.Y < Ka.Y || (Kb.Y == Ka.Y && Kb.Z < Ka.Z))))
			{
				Swap(Ka, Kb);
			}
			const TPair<FIntVector, FIntVector> K(Ka, Kb);
			Count.FindOrAdd(K)++;
			Ends.Add(K, TPair<FVector, FVector>(A, B));
		}
	}
	TArray<TPair<FVector, FVector>> Out;
	for (const TPair<TPair<FIntVector, FIntVector>, int32>& It : Count)
	{
		if (It.Value == 1)
		{
			Out.Add(Ends[It.Key]);
		}
	}
	return Out;
}

// Разложить треугольники по полосам (Band(T) → 0..Bands-1) и создать по секции на полосу.
void MakeBandSections(UProceduralMeshComponent* Mesh, int32& SectionIndex, const FSoup& Soup, TFunctionRef<int32(int32)> Band, int32 Bands,
	TFunctionRef<UMaterialInterface*(int32)> MaterialFor)
{
	for (int32 B = 0; B < Bands; ++B)
	{
		TArray<FVector> V;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<int32> Tris;
		for (int32 T = 0; T < Soup.Num(); ++T)
		{
			if (Band(T) != B)
			{
				continue;
			}
			for (int32 K = 0; K < 3; ++K)
			{
				Tris.Add(V.Num());
				V.Add(Soup.V[T * 3 + K]);
				N.Add(Soup.N[T * 3 + K]);
				UV.Add(Soup.UV[T * 3 + K]);
			}
		}
		if (Tris.Num() == 0)
		{
			continue;
		}
		Mesh->CreateMeshSection(SectionIndex, V, Tris, N, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		if (UMaterialInterface* M = MaterialFor(B))
		{
			Mesh->SetMaterial(SectionIndex, M);
		}
		else
		{
			Mesh->SetMeshSectionVisible(SectionIndex, false);
		}
		++SectionIndex;
	}
}

// Полоса запотевания по расстоянию до края стекла: 0 — у рамки (потеет первым), 1 — середина края, 2 — центр.
int32 FogBand(const FVector& P, const TArray<TPair<FVector, FVector>>& Edges)
{
	float Best = 1e9f;
	for (const TPair<FVector, FVector>& E : Edges)
	{
		Best = FMath::Min(Best, static_cast<float>(FMath::PointDistToSegment(P, E.Key, E.Value)));
	}
	return Best < 7.f ? 0 : (Best < 16.f ? 1 : 2);
}
} // namespace PeregonGlassSplit

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
	// Козырьки: шарнир вдоль Y у верха лобового, лежат под потолком — откидываются вниз к стеклу.
	AddMovingPart(VisorL, MakePartPivot(vmodel::Part::VisorL, TEXT("VisorLPivot")), FRotator(80.f, 0.f, 0.f), 4.f);
	AddMovingPart(VisorR, MakePartPivot(vmodel::Part::VisorR, TEXT("VisorRPivot")), FRotator(80.f, 0.f, 0.f), 4.f);

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
	WiperL = MakePartPivot(vmodel::Part::WiperL, TEXT("WiperL"));
	WiperR = MakePartPivot(vmodel::Part::WiperR, TEXT("WiperR"));
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
	// Дополнительные мишени салона: плафон на потолке, магнитола в консоли, кнопка сигнала в центре руля.
	struct FExtraHit
	{
		const TCHAR* Name;
		USceneComponent* Parent;
		FVector Center;
		FVector Half;
	};
	const FExtraHit Extras[] = {{TEXT("Dome"), Body.Get(), FVector(-10.f, 0.f, 82.f), FVector(7.f, 5.f, 2.f)},
		{TEXT("Heater"), Body.Get(), FVector(72.4f, 2.f, 24.f), FVector(1.2f, 8.f, 2.4f)},
		// Сиденья: навести и нажать — сесть (снаружи — через открытую дверь этого места, изнутри — в своём ряду).
		{TEXT("Seat0"), Body.Get(), FVector(0.f, -35.f, -5.f), FVector(20.f, 20.f, 3.f)},
		{TEXT("Seat1"), Body.Get(), FVector(0.f, 35.f, -5.f), FVector(20.f, 20.f, 3.f)},
		{TEXT("Seat2"), Body.Get(), FVector(-74.f, -32.f, -6.f), FVector(17.f, 22.f, 3.f)},
		{TEXT("Seat3"), Body.Get(), FVector(-74.f, 32.f, -6.f), FVector(17.f, 22.f, 3.f)},
		{TEXT("RadioVol"), Body.Get(), FVector(72.f, -4.f, 29.6f), FVector(2.f, 1.8f, 1.8f)},
		{TEXT("RadioTune"), Body.Get(), FVector(72.f, 8.f, 29.6f), FVector(2.f, 1.8f, 1.8f)},
		{TEXT("Horn"), SteeringPivot.Get(), FVector::ZeroVector, FVector(4.f, 7.f, 7.f)}};
	for (const FExtraHit& E : Extras)
	{
		UBoxComponent* Hit = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("Hit_%s_Extra"), E.Name));
		Hit->SetupAttachment(E.Parent ? E.Parent : Body.Get());
		Hit->SetRelativeLocation(E.Center);
		Hit->SetBoxExtent(E.Half);
		Hit->SetHiddenInGame(true);
		Hit->SetCanEverAffectNavigation(false);
		PeregonInteraction::MakeInteractable(Hit, FName(E.Name));
	}

	HeadlightL = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightL"));
	HeadlightR = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightR"));
	for (USpotLightComponent* Lamp : {HeadlightL.Get(), HeadlightR.Get()})
	{
		Lamp->SetupAttachment(Body);
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->SetRelativeRotation(FRotator(-3.f, 0.f, 0.f));
		Lamp->SetIntensityUnits(ELightUnits::Candelas);
		// Ближний свет советской фары ~1 000 кд в пятне. Раньше было 6 000 — на ночной экспозиции дорога у капота «выгорала».
		Lamp->SetIntensity(950.f);
		Lamp->SetAttenuationRadius(6000.f);
		// Свет фар в тумане и дожде — лёгкая дымка, а не плотный «прожекторный» конус. Источник размером с отражатель:
		// мягкие блики на мокром асфальте и кузовах вместо точки.
		Lamp->SetVolumetricScatteringIntensity(0.12f);
		Lamp->SetSourceRadius(6.f);
		Lamp->SetSoftSourceRadius(4.f);
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
	HeadPivot = AddPivot(TEXT("HeadPivot"), Body, PeregonCarModel::ToVector(Model.DriverEye));
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

// ---------------------------------------------------------------------------------------------
// Звук
// ---------------------------------------------------------------------------------------------
void APeregonCar::BuildSounds()
{
	auto MakeVoice = [this](const TCHAR* Name, const FVector& Local) {
		UPeregonSynth* Voice = NewObject<UPeregonSynth>(this, Name);
		Voice->SetupAttachment(Body);
		Voice->SetRelativeLocation(Local);
		return Voice;
	};
	EngineSound = MakeVoice(TEXT("EngineSound"), FVector(140.f, 0.f, 0.f));
	EngineSound->SetupEngine();
	TireSound = MakeVoice(TEXT("TireSound"), FVector(0.f, 0.f, -35.f));
	TireSound->SetupLoop(TEXT("tires.roll"), EPeregonProcedural::TireRoll);
	WindSound = MakeVoice(TEXT("WindSound"), FVector(40.f, 0.f, 60.f));
	WindSound->SetupLoop(TEXT("wind"), EPeregonProcedural::Wind);
	WindSound->SetGroup(PeregonAudio::EGroup::Ambience);
	SkidSound = MakeVoice(TEXT("SkidSound"), FVector(0.f, 0.f, -40.f));
	SkidSound->SetupLoop(TEXT("tires.skid"), EPeregonProcedural::Skid);
	for (UPeregonSynth* Voice : {EngineSound.Get(), TireSound.Get(), WindSound.Get(), SkidSound.Get()})
	{
		Voice->SetLevel(0.f, 0.f);
		Voice->RegisterComponent();
		Voice->Start();
	}
}

void APeregonCar::UpdateSounds(float DeltaSeconds, const FPeregonTelemetry& T)
{
	if (!EngineSound)
	{
		return;
	}
	const float Speed = FMath::Abs(Movement->GetForwardSpeed());
	// В салоне с закрытыми дверями всё звучит глуше.
	bool bDoorsClosed = true;
	for (const FPeregonMovingPart& Part : MovingParts)
	{
		if (Part.Tag.ToString().StartsWith(TEXT("Door")) && Part.Alpha > 0.05f)
		{
			bDoorsClosed = false;
		}
	}
	// Открытое окно — улица слышна почти как снаружи.
	const float MaxWindow = FMath::Max(FMath::Max(WindowOpen[0], WindowOpen[1]), FMath::Max(WindowOpen[2], WindowOpen[3]));
	const bool bCabin = bFirstPerson && Controller && bDoorsClosed && MaxWindow < 0.3f;
	for (UPeregonSynth* Voice : {EngineSound.Get(), TireSound.Get(), WindSound.Get(), SkidSound.Get()})
	{
		Voice->SetMuffled(bCabin);
	}

	EngineSound->SetEngineState(T.Rpm, Movement->GetThrottleInput(), T.bEngineRunning, Movement->IsStarterOn());
	EngineSound->SetLevel(bCabin ? 0.8f : 1.f, 0.f);

	const bool bGrounded = Movement->GetWheelsInContact() > 0;
	const float Rough = Movement->GetSurfaceRoughness();
	TireSound->SetLevel(bGrounded ? FMath::Clamp(Speed / 20.f, 0.f, 1.f) * (0.35f + Rough * 1.6f) : 0.f, Speed);
	WindSound->SetLevel(FMath::Clamp(Speed / 45.f, 0.f, 1.f) * (bCabin ? 0.6f : 1.f), Speed);

	float Skid = 0.f;
	for (int32 I = 0; I < T.SlipRatio.Num(); ++I)
	{
		const float Long = FMath::Abs(T.SlipRatio[I]) - 0.12f;
		const float Lat = (FMath::Abs(T.SlipAngleDeg[I]) - 6.f) / 10.f;
		if (T.WheelLoadN.IsValidIndex(I) && T.WheelLoadN[I] > 300.f)
		{
			Skid = FMath::Max(Skid, FMath::Max(Long * 2.5f, Lat));
		}
	}
	// По гравию и траве колёса не визжат, а шуршат.
	Skid *= FMath::Clamp(1.f - Rough * 2.f, 0.f, 1.f) * FMath::Clamp(Speed / 3.f, 0.f, 1.f);
	SkidSound->SetLevel(bGrounded ? FMath::Clamp(Skid, 0.f, 1.f) * 0.8f : 0.f, FMath::Clamp(Skid, 0.f, 1.f));

	if (T.Gear != LastGear)
	{
		PeregonAudio::PlayEvent(this, TEXT("gear.shift"), GearLever ? GearLever->GetComponentLocation() : GetActorLocation(), 0.5f);
		LastGear = T.Gear;
	}
	if (T.bGearGrind && !bLastGrind)
	{
		PeregonAudio::PlayEvent(this, TEXT("gear.grind"), GetActorLocation(), 0.7f);
	}
	bLastGrind = T.bGearGrind;
}

void APeregonCar::PlayPartSound(const FPeregonMovingPart& Part, bool bOpen) const
{
	const FString Name = Part.Tag.ToString();
	const TCHAR* Kind = Name.StartsWith(TEXT("Door")) ? TEXT("door") : (Name == TEXT("Hood") ? TEXT("hood") : (Name == TEXT("Trunk") ? TEXT("trunk") : TEXT("glovebox")));
	const FVector Where = Part.Pivot ? Part.Pivot->GetComponentLocation() : GetActorLocation();
	PeregonAudio::PlayEvent(this, FString::Printf(TEXT("%s.%s"), Kind, bOpen ? TEXT("open") : TEXT("close")), Where, bOpen ? 0.6f : 0.9f);
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
	if (!bPartsAssigned)
	{
		const float Color[3] = {PaintColor.R, PaintColor.G, PaintColor.B};
		PartsState = vsim::parts::MakeFactoryNew(Color);
	}
	PartsEffects = vsim::parts::Evaluate(PartsState);
	BuildModelMeshes();
	BuildSounds();
	BuildCabinExtras();
	BuildGaugeFaces();
	BuildPartSlots();
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
		// Пыль и разводы на стекле (альфа текстуры): стекло видно и изнутри; запотевание и грязь — UpdateGlassAndDirt.
		// Три материала: у рамки, ближе к центру и в центре — запотевают по-разному (UpdateGlassAndDirt).
		GlassBands.Reset();
		for (int32 B = 0; B < 3; ++B)
		{
			UMaterialInstanceDynamic* Band = Art->MakeGlassMaterial(this, FLinearColor(0.42f, 0.5f, 0.52f), 0.3f);
			if (Band && Art->HasWeatherMaterials())
			{
				Band->SetTextureParameterValue(TEXT("Tex"), Art->GetTexture(EPeregonTex::GlassDirt));
			}
			if (Band)
			{
				GlassBands.Add(Band);
			}
		}
		GlassMaterial = GlassBands.Num() == 3 ? GlassBands[2].Get() : nullptr;
		return GlassMaterial;
	}
	if (M == vmodel::Mat::LampLens)
	{
		// Рассеиватели фар и ламп: почти прозрачное чистое стекло — за ним виден отражатель и лампа.
		return Art->MakeGlassMaterial(this, FLinearColor(0.93f, 0.95f, 0.97f), 0.1f);
	}
	if (M == vmodel::Mat::Water)
	{
		// Капли на лобовом: прозрачность растёт под дождём, дворники её сбрасывают. Без M_PSX3_Glass — нет капель.
		WaterMaterial = Art->MakeTranslucent(this, EPeregonTex::Raindrops, FLinearColor(0.85f, 0.9f, 0.95f), 0.f, 0.03f);
		WaterEdgeMaterial = Art->MakeTranslucent(this, EPeregonTex::Raindrops, FLinearColor(0.85f, 0.9f, 0.95f), 0.f, 0.03f);
		return WaterMaterial;
	}
	FPeregonLook Look;
	switch (M)
	{
	// Цвет каждой панели — в цвете вершин (ApplyDirt), материал белый. Без M_PSX3 вершинный цвет не читается — тогда общий цвет.
	case vmodel::Mat::Paint: Look = FPeregonLook(EPeregonTex::Paint, Art->HasWeatherMaterials() ? FLinearColor::White : PaintColor, 0.35f); break;
	case vmodel::Mat::Chrome: Look = FPeregonLook(EPeregonTex::Chrome, FLinearColor(0.9f, 0.9f, 0.88f), 0.18f, 1.f); break;
	case vmodel::Mat::BlackTrim: Look = FPeregonLook(EPeregonTex::Plastic, FLinearColor(0.035f, 0.035f, 0.04f), 0.6f); break;
	case vmodel::Mat::Rubber: Look = FPeregonLook(EPeregonTex::Rubber, FLinearColor(0.11f, 0.11f, 0.11f), 0.92f); break;
	// Отражатель фары — полированный металл за прозрачным рассеивателем; при включённых фарах светится весь.
	case vmodel::Mat::Headlamp: Look = FPeregonLook(EPeregonTex::Chrome, FLinearColor(0.88f, 0.87f, 0.82f), 0.12f, 1.f); break;
	case vmodel::Mat::Taillamp: Look = FPeregonLook(EPeregonTex::Glass, FLinearColor(0.6f, 0.03f, 0.02f), 0.15f); break;
	case vmodel::Mat::Indicator: Look = FPeregonLook(EPeregonTex::Glass, FLinearColor(0.9f, 0.42f, 0.04f), 0.15f); break;
	// Салон «шестёрки»: бежево-коричневый кожзам дверей и торпедо, рыжий велюр сидений, тёмный ковролин, светлый потолок.
	case vmodel::Mat::Interior: Look = FPeregonLook(EPeregonTex::Vinyl, FLinearColor(0.23f, 0.2f, 0.16f), 0.62f); break;
	case vmodel::Mat::InteriorDark: Look = FPeregonLook(EPeregonTex::Vinyl, FLinearColor(0.06f, 0.056f, 0.052f), 0.55f); break;
	case vmodel::Mat::Fabric: Look = FPeregonLook(EPeregonTex::CarFabric, FLinearColor(0.36f, 0.17f, 0.08f), 0.97f); break;
	case vmodel::Mat::Carpet: Look = FPeregonLook(EPeregonTex::Carpet, FLinearColor(0.085f, 0.075f, 0.065f), 1.f); break;
	case vmodel::Mat::Headliner: Look = FPeregonLook(EPeregonTex::Headliner, FLinearColor(0.62f, 0.6f, 0.54f), 0.9f); break;
	case vmodel::Mat::Dome: Look = FPeregonLook(EPeregonTex::Plastic, FLinearColor(1.f, 0.88f, 0.6f), 0.4f); break;
	case vmodel::Mat::GaugeFace: Look = FPeregonLook(EPeregonTex::White, FLinearColor(0.85f, 0.83f, 0.74f), 0.6f); break;
	case vmodel::Mat::Needle: Look = FPeregonLook(EPeregonTex::White, FLinearColor(0.95f, 0.3f, 0.05f), 0.5f); break;
	case vmodel::Mat::Engine: Look = FPeregonLook(EPeregonTex::Plastic, FLinearColor(0.16f, 0.16f, 0.17f), 0.5f, 0.7f); break;
	case vmodel::Mat::Metal: Look = FPeregonLook(EPeregonTex::Chrome, FLinearColor(0.32f, 0.32f, 0.33f), 0.55f, 0.8f); break;
	case vmodel::Mat::Plate: Look = FPeregonLook(EPeregonTex::White, FLinearColor(0.9f, 0.9f, 0.87f), 0.5f); break;
	default: Look = FPeregonLook(EPeregonTex::White, FLinearColor::White, 0.8f); break;
	}
	// Кузов мокнет под дождём (глянец), салон — нет. Снег на машине не копим: она едет.
	switch (M)
	{
	case vmodel::Mat::Paint: Look.WetMask = 1.f; break;
	case vmodel::Mat::Chrome: Look.WetMask = 0.5f; break;
	case vmodel::Mat::BlackTrim:
	case vmodel::Mat::Rubber: Look.WetMask = 0.7f; break;
	default: Look.WetMask = 0.f; break;
	}
	Look.SnowMask = 0.f;
	UMaterialInstanceDynamic* Mid = Art->MakeMaterial(this, Look);
	if (M == vmodel::Mat::Dome)
	{
		DomeMaterial = Mid;
	}
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
		// Краска мокнет (блестит в дождь) и собирает снег на крыше, капоте и багажнике, пока машина стоит.
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(SlotMaterials.Last()))
		{
			if (static_cast<vmodel::Mat>(Slot) == vmodel::Mat::Paint)
			{
				Mid->SetScalarParameterValue(TEXT("WetMask"), 0.7f);
				Mid->SetScalarParameterValue(TEXT("SnowMask"), 0.85f);
			}
		}
	}

	WindowGlass.SetNum(4);
	DirtSections.Reset();
	auto Spawn = [&](const vmodel::PartMesh& Part, USceneComponent* Parent, const FString& Name) {
		const int32 Door = Part.Id == vmodel::Part::DoorFL ? 0 : (Part.Id == vmodel::Part::DoorFR ? 1 : (Part.Id == vmodel::Part::DoorRL ? 2 : (Part.Id == vmodel::Part::DoorRR ? 3 : -1)));
		const bool bWheel = Part.Id == vmodel::Part::WheelLeft || Part.Id == vmodel::Part::WheelRight;
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
			if (S.Material == vmodel::Mat::Indicator)
			{
				// Поворотники: левые и правые фонари — разные секции, чтобы мигать по отдельности.
				TArray<int32> Sides[2];
				for (int32 K = 0; K + 2 < Triangles.Num(); K += 3)
				{
					const float Y = static_cast<float>(Vertices[Triangles[K]].Y + Vertices[Triangles[K + 1]].Y + Vertices[Triangles[K + 2]].Y);
					Sides[Y < 0.f ? 0 : 1].Append({Triangles[K], Triangles[K + 1], Triangles[K + 2]});
				}
				for (int32 Side = 0; Side < 2; ++Side)
				{
					UMaterialInstanceDynamic* Blink = Cast<UMaterialInstanceDynamic>(MakeSlotMaterial(static_cast<uint8>(vmodel::Mat::Indicator)));
					(Side == 0 ? BlinkerLeft : BlinkerRight) = Blink;
					Mesh->CreateMeshSection(SectionIndex, Vertices, Sides[Side], Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
					if (Blink)
					{
						Mesh->SetMaterial(SectionIndex, Blink);
					}
					++SectionIndex;
				}
				continue;
			}
			if (Door >= 0 && S.Material == vmodel::Mat::Glass)
			{
				// Стекло двери — отдельный узел: его опускает ручка стеклоподъёмника.
				UProceduralMeshComponent* Win = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("WindowGlass%d"), Door));
				Win->SetupAttachment(Parent);
				Win->SetMobility(EComponentMobility::Movable);
				Win->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Win->SetCanEverAffectNavigation(false);
				Win->RegisterComponent();
				{
					// Запотевает от рамки к центру: три полосы со своими материалами.
					using namespace PeregonGlassSplit;
					const TArray<TPair<FVector, FVector>> Edges = BoundaryEdges(Vertices, Triangles);
					const FSoup Soup = Subdivide(Vertices, Normals, UVs, Triangles, 8.f);
					int32 WinSection = 0;
					MakeBandSections(Win, WinSection, Soup, [&](int32 T) { return FogBand(Soup.Center(T), Edges); }, 3,
						[&](int32 B) -> UMaterialInterface* { return GlassBands.IsValidIndex(B) ? GlassBands[B].Get() : nullptr; });
				}
				WindowGlass[Door] = Win;
				continue;
			}
			if (S.Material == vmodel::Mat::Glass && GlassBands.Num() == 3)
			{
				using namespace PeregonGlassSplit;
				const TArray<TPair<FVector, FVector>> Edges = BoundaryEdges(Vertices, Triangles);
				const FSoup Soup = Subdivide(Vertices, Normals, UVs, Triangles, 10.f);
				MakeBandSections(Mesh, SectionIndex, Soup, [&](int32 T) { return FogBand(Soup.Center(T), Edges); }, 3,
					[&](int32 B) -> UMaterialInterface* { return GlassBands[B].Get(); });
				continue;
			}
			if (S.Material == vmodel::Mat::Water && WaterMaterial)
			{
				// Дворники вытирают только сектор под щёткой: остальное стекло остаётся в каплях.
				using namespace PeregonGlassSplit;
				const FSoup Soup = Subdivide(Vertices, Normals, UVs, Triangles, 6.f);
				const FVector Origin = PeregonCarModel::ToVector(Part.Origin);
				MakeBandSections(Mesh, SectionIndex, Soup, [&](int32 T) { return IsWiped(Soup.Center(T) + Origin) ? 0 : 1; }, 2,
					[&](int32 B) -> UMaterialInterface* { return B == 0 ? WaterMaterial.Get() : WaterEdgeMaterial.Get(); });
				continue;
			}
			Mesh->CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
			// Снаружи кузов пачкается: низ и колёса сильнее, пятнами.
			const bool bExterior = S.Material == vmodel::Mat::Paint || S.Material == vmodel::Mat::Chrome || S.Material == vmodel::Mat::BlackTrim ||
				S.Material == vmodel::Mat::Rubber || S.Material == vmodel::Mat::Plate || S.Material == vmodel::Mat::Headlamp || S.Material == vmodel::Mat::Taillamp;
			if (bExterior)
			{
				FDirtSection& Dirty = DirtSections.AddDefaulted_GetRef();
				Dirty.Mesh = Mesh;
				Dirty.Section = SectionIndex;
				Dirty.Vertices = Vertices;
				Dirty.Normals = Normals;
				Dirty.UVs = UVs;
				Dirty.Mask.SetNumUninitialized(N);
				const float OriginZ = bWheel ? 0.f : Part.Origin.Z;
				for (int32 K = 0; K < N; ++K)
				{
					const FVector& V = Vertices[K];
					const float Height = bWheel ? 0.f : static_cast<float>(V.Z) + OriginZ;
					const float Low = FMath::Clamp(1.f - (Height - 8.f) / 55.f, 0.12f, 1.f);
					const float Blot = 0.6f + 0.4f * (0.5f + 0.5f * FMath::PerlinNoise3D(FVector(V) * 0.08f + FVector(OriginZ)));
					Dirty.Mask[K] = Low * Blot;
				}
				// Панель кузова у каждой вершины: двери, капот и багажник — свои узлы, остальное делим по месту.
				Dirty.PanelIndex.Init(255, N);
				Dirty.Noise.SetNumUninitialized(N);
				for (int32 K = 0; K < N; ++K)
				{
					const FVector P = Vertices[K] + PeregonCarModel::ToVector(Part.Origin);
					Dirty.Noise[K] = 0.5f + 0.5f * FMath::PerlinNoise3D(P * 0.045f + FVector(3.1f, 7.7f, 1.3f));
				}
				if (S.Material == vmodel::Mat::Paint)
				{
					using vsim::parts::Panel;
					for (int32 K = 0; K < N; ++K)
					{
						const FVector P = Vertices[K] + PeregonCarModel::ToVector(Part.Origin);
						Panel Which = Panel::Roof;
						switch (Part.Id)
						{
						case vmodel::Part::Hood: Which = Panel::Hood; break;
						case vmodel::Part::Trunk: Which = Panel::Trunk; break;
						case vmodel::Part::DoorFL: Which = Panel::DoorFL; break;
						case vmodel::Part::DoorFR: Which = Panel::DoorFR; break;
						case vmodel::Part::DoorRL: Which = Panel::DoorRL; break;
						case vmodel::Part::DoorRR: Which = Panel::DoorRR; break;
						default:
						{
							const bool bLeft = P.Y < 0.0;
							if (P.Z > 58.0)
							{
								Which = Panel::Roof;
							}
							else if (P.X > 95.0)
							{
								Which = bLeft ? Panel::FenderFL : Panel::FenderFR;
							}
							else if (P.X < -95.0)
							{
								Which = bLeft ? Panel::QuarterRL : Panel::QuarterRR;
							}
							else
							{
								Which = bLeft ? Panel::SillL : Panel::SillR;
							}
							break;
						}
						}
						Dirty.PanelIndex[K] = static_cast<uint8>(Which);
					}
				}
			}
			UMaterialInterface* Material = SlotMaterials[static_cast<int32>(S.Material)];
			if (Material)
			{
				Mesh->SetMaterial(SectionIndex, Material);
			}
			else if (S.Material == vmodel::Mat::Glass || S.Material == vmodel::Mat::Water || S.Material == vmodel::Mat::LampLens)
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
	UpdateWipers(DeltaSeconds);
	UpdateCabin(DeltaSeconds);
	UpdateGlassAndDirt(DeltaSeconds);
	UpdateParts(DeltaSeconds, T);
	UpdateSounds(DeltaSeconds, T);
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
		const float PrevAlpha = Part.Alpha;
		if (PrevAlpha <= 0.f && Part.Target > 0.f)
		{
			PlayPartSound(Part, true);
		}
		Part.Alpha = FMath::FInterpConstantTo(Part.Alpha, Part.Target, DeltaSeconds, Part.Speed);
		if (PrevAlpha > 0.f && Part.Alpha <= 0.f)
		{
			PlayPartSound(Part, false);
		}
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
		// C — приблизить взгляд (посмотреть на знак, приборы, дорогу вдали).
		const float TargetFov = Settings->FieldOfView * (bZoom ? 0.45f : 1.f);
		ZoomFov = ZoomFov <= 0.f ? TargetFov : FMath::FInterpTo(ZoomFov, TargetFov, Dt, 9.f);
		FirstPersonCamera->SetFieldOfView(ZoomFov);
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
		// Вид сверху — узкий угол и далеко: машина в пейзаже, как на картине.
		const bool bHigh = CameraMode == 2;
		const float TargetFov = UPeregonGameUserSettings::Get()->FieldOfView * (bZoom ? 0.5f : 1.f) * (bHigh ? 0.5f : 1.f);
		ZoomFov = ZoomFov <= 0.f ? TargetFov : FMath::FInterpTo(ZoomFov, TargetFov, Dt, 9.f);
		ChaseCamera->SetFieldOfView(ZoomFov);
		ChaseArm->TargetArmLength = FMath::FInterpTo(ChaseArm->TargetArmLength, bHigh ? 2300.f : 620.f, Dt, 3.f);
		// Камера сама возвращается за машину, если игрок не крутит мышью.
		TimeSinceLook += Dt;
		if (Controller && TimeSinceLook > 2.f && FMath::Abs(Movement->GetForwardSpeed()) > 1.f)
		{
			const FRotator Desired(bHigh ? -38.f : -12.f, GetActorRotation().Yaw + (bHigh ? 25.f : 0.f), 0.f);
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
	CameraMode = bEnable ? 0 : FMath::Max(1, CameraMode);
	FirstPersonCamera->SetActive(bEnable);
	ChaseCamera->SetActive(!bEnable);
	TimeSinceLook = 10.f;
}

void APeregonCar::SetHeadlights(bool bOn)
{
	if (bOn != bHeadlights && HasActorBegunPlay())
	{
		PeregonAudio::PlayEvent(this, TEXT("lights.switch"), LightKnobPivot ? LightKnobPivot->GetComponentLocation() : GetActorLocation(), 0.5f);
	}
	bHeadlights = bOn;
	// Фары светят, только если они стоят и есть ток (аккумулятор или работающий генератор).
	const bool bPower = PartsState.BatteryCharge > 0.03 || (Movement && Movement->GetTelemetry().bEngineRunning && PartsEffects.ChargeRate > 0.1);
	const bool bLit = bOn && (!HasActorBegunPlay() || (PartsEffects.bHeadlights && bPower));
	HeadlightL->SetVisibility(bLit);
	HeadlightR->SetVisibility(bLit);
	for (UMaterialInstanceDynamic* Mid : LampMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Emissive"), bLit ? 6.f : 0.f);
		}
	}
	// Подсветка приборов.
	for (UMaterialInstanceDynamic* Mid : GaugeMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Emissive"), bLit && PartsEffects.bDashboard ? 0.6f : 0.f);
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
	{
		const FString Work = PartsPrompt(Part, User);
		if (!Work.IsEmpty())
		{
			return Work;
		}
	}

	{
		const int32 SeatHit = SeatHitIndex(Part);
		if (SeatHit >= 0)
		{
			static const TCHAR* Names[] = {TEXT("за руль"), TEXT("на переднее сиденье"), TEXT("назад слева"), TEXT("назад справа")};
			if (bOnFoot && !Driver)
			{
				const FPeregonMovingPart* SeatDoorPart = FindMovingPart(SeatDoor(SeatHit));
				return SeatDoorPart && SeatDoorPart->Target > 0.5f ? FString::Printf(TEXT("Сесть %s"), Names[SeatHit])
																	: FString(TEXT("Сначала откройте дверь этого места"));
			}
			if (bInside && SeatHit != Seat && SeatHit / 2 == Seat / 2)
			{
				return FString::Printf(TEXT("Пересесть %s"), Names[SeatHit]);
			}
			return FString();
		}
	}
	// Двери: снаружи — открыть; из открытой двери (стоя в проёме) — сесть на это место, сбоку — закрыть.
	// Изнутри — открыть/закрыть любую; через открытую — выйти. Сами двери не закрываются.
	for (const FName& Tag : {DoorFL, DoorFR, DoorRL, DoorRR, Hood, Trunk, Glovebox, VisorL, VisorR})
	{
		if (Part->ComponentHasTag(Tag))
		{
			const FPeregonMovingPart* Moving = FindMovingPart(Tag);
			const bool bOpen = Moving && Moving->Target > 0.5f;
			const int32 DoorSeat = DoorSeatIndex(Tag);
			if (bOnFoot && bOpen && !Driver && DoorSeat >= 0 && IsInDoorway(Tag, User))
			{
				static const TCHAR* Places[] = {TEXT("Сесть за руль"), TEXT("Сесть на переднее сиденье"), TEXT("Сесть назад слева"), TEXT("Сесть назад справа")};
				return Places[DoorSeat];
			}
			if (bInside && bOpen && DoorSeat >= 0)
			{
				return FMath::Abs(Movement->GetForwardSpeed()) < MaxExitSpeed ? FString(TEXT("Закрыть дверь  ·  F — выйти через неё"))
																			   : FString(TEXT("Закрыть дверь"));
			}
			if (Tag == VisorL || Tag == VisorR)
			{
				return bOpen ? FString(TEXT("Поднять козырёк")) : FString(TEXT("Опустить козырёк"));
			}
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
		if (Part->ComponentHasTag(TEXT("Dome")))
		{
			static const TCHAR* Modes[] = {TEXT("Плафон: по дверям"), TEXT("Плафон: горит"), TEXT("Плафон: выключен")};
			return FString::Printf(TEXT("%s · переключить (O)"), Modes[FMath::Clamp(DomeMode, 0, 2)]);
		}
		if (Part->ComponentHasTag(TEXT("RadioVol")))
		{
			return Radio && Radio->IsPowered() ? FString::Printf(TEXT("Громкость %d%% — колесо мыши · ЛКМ выключить"), FMath::RoundToInt(RadioVolume * 100.f))
											   : FString(TEXT("Включить радио (ЛКМ / N)"));
		}
		if (Part->ComponentHasTag(TEXT("RadioTune")))
		{
			return Radio && Radio->IsPowered()
				? FString::Printf(TEXT("%s · %s — колесо: другая песня%s"), *Radio->GetStationName(), *Radio->GetTrackName(),
					Radio->GetStationCount() > 1 ? TEXT(", ЛКМ: другая станция") : TEXT(""))
				: FString(TEXT("Включить радио (ЛКМ / N)"));
		}
		const int32 Win = WindowIndex(Part);
		if (Win >= 0)
		{
			return FString::Printf(TEXT("Стекло: колесо мыши (опущено на %d%%)"), FMath::RoundToInt(WindowOpen[Win] * 100.f));
		}
		if (Part->ComponentHasTag(TEXT("Horn")))
		{
			return TEXT("Сигнал (H)");
		}
		if (Part->ComponentHasTag(TEXT("Heater")))
		{
			static const TCHAR* Modes[] = {TEXT("выключена"), TEXT("слабо"), TEXT("сильно")};
			return FString::Printf(TEXT("Печка: %s · в салоне %+.0f °C — переключить (U)"), Modes[FMath::Clamp(HeaterMode, 0, 2)], CabinTempC);
		}
	}
	auto IsOpen = [this](FName Tag) {
		const FPeregonMovingPart* Moving = FindMovingPart(Tag);
		return Moving && Moving->Alpha > 0.7f;
	};
	if (bInside && Part->ComponentHasTag(GloveInside))
	{
		return IsOpen(Glovebox) ? FString(TEXT("Бардачок — посмотреть вещи")) : FString();
	}
	if (bOnFoot && Part->ComponentHasTag(TrunkInside))
	{
		return IsOpen(Trunk) ? FString(TEXT("Багажник — посмотреть вещи")) : FString();
	}
	if (bOnFoot && Part->ComponentHasTag(Fuel))
	{
		const FPeregonTelemetry T = Movement->GetTelemetry();
		const UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
		if (GI && GI->Pockets.Has(PeregonItems::CanisterFull))
		{
			return FString::Printf(TEXT("Залить из канистры (в баке %.0f из %.0f л)"), T.FuelL, T.FuelCapacityL);
		}
		return FString::Printf(TEXT("В баке %.0f из %.0f л · нужна канистра с бензином"), T.FuelL, T.FuelCapacityL);
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
	if (PartsInteract(Part, User))
	{
		return;
	}

	{
		const int32 SeatHit = SeatHitIndex(Part);
		if (SeatHit >= 0)
		{
			const FPeregonMovingPart* SeatDoorPart = FindMovingPart(SeatDoor(SeatHit));
			if (Walker && !Driver && SeatDoorPart && SeatDoorPart->Target > 0.5f)
			{
				EnterCar(Walker, SeatHit);
			}
			else if (bInside && SeatHit != Seat && SeatHit / 2 == Seat / 2)
			{
				SwitchSeat(SeatHit);
			}
			return;
		}
	}
	for (const FName& Tag : {DoorFL, DoorFR, DoorRL, DoorRR, Hood, Trunk, Glovebox, VisorL, VisorR})
	{
		if (Part->ComponentHasTag(Tag))
		{
			const FPeregonMovingPart* Moving = FindMovingPart(Tag);
			const bool bOpen = Moving && Moving->Target > 0.5f;
			const int32 DoorSeat = DoorSeatIndex(Tag);
			if (Walker && !Driver && bOpen && DoorSeat >= 0 && IsInDoorway(Tag, User))
			{
				EnterCar(Walker, DoorSeat);
				return;
			}
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
			PeregonAudio::PlayEvent(this, TEXT("key.turn"), KeyPivot->GetComponentLocation(), 0.5f);
		}
		else if (Part->ComponentHasTag(Lights))
		{
			SetHeadlights(!bHeadlights);
		}
		else if (Part->ComponentHasTag(Handbrake))
		{
			Movement->SetParkingBrake(!Movement->IsParkingBrake());
			PeregonAudio::PlayEvent(this, TEXT("handbrake"), HandbrakeLever->GetComponentLocation(), 0.6f);
		}
		else if (Part->ComponentHasTag(GloveInside))
		{
			if (APeregonPlayerController* DriverPC = Cast<APeregonPlayerController>(GetController()))
			{
				DriverPC->OpenInventory(EPeregonStorage::Glovebox, this);
			}
		}
		else if (Part->ComponentHasTag(TEXT("Dome")))
		{
			OnDome(FInputActionValue());
		}
		else if (Part->ComponentHasTag(TEXT("RadioTune")) && Radio && Radio->IsPowered() && Radio->GetStationCount() > 1)
		{
			Radio->NextStation();
			if (APeregonGameMode* StationGM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
			{
				StationGM->Toast(FString::Printf(TEXT("Станция: %s"), *Radio->GetStationName()), 2.f);
			}
		}
		else if (Part->ComponentHasTag(TEXT("RadioVol")) || Part->ComponentHasTag(TEXT("RadioTune")))
		{
			OnRadioPower(FInputActionValue());
		}
		else if (Part->ComponentHasTag(TEXT("Horn")))
		{
			bHorn = true;
		}
		else if (Part->ComponentHasTag(TEXT("Heater")))
		{
			SetHeater((HeaterMode + 1) % 3);
		}
		return;
	}
	APeregonPlayerController* PC = User ? Cast<APeregonPlayerController>(User->GetController()) : nullptr;
	if (Walker && Part->ComponentHasTag(TrunkInside) && PC)
	{
		PC->OpenInventory(EPeregonStorage::Trunk, this);
		return;
	}
	if (Walker && Part->ComponentHasTag(Fuel) && PC)
	{
		PC->UseItem(PeregonItems::CanisterFull, EPeregonStorage::None);
	}
}

void APeregonCar::InteractStop(UPrimitiveComponent* Part, APawn* /*User*/)
{
	PartsInteractStop(Part);
	if (Part && Part->ComponentHasTag(TEXT("Horn")))
	{
		bHorn = false;
	}
	if (bKeyCranking && Part && Part->ComponentHasTag(PeregonCarLook::Key))
	{
		Movement->SetStarter(false);
		bKeyCranking = false;
	}
}

void APeregonCar::EnterCar(APeregonCharacter* Character, int32 InSeat)
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
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		GM->SetActiveCar(this); // в колонне «своя» машина — та, в которую сели последней
	}
	Seat = FMath::Clamp(InSeat, 0, 3);
	HeadBaseLocation = SeatEye(Seat);
	HeadPivot->SetRelativeLocation(HeadBaseLocation);
	// Дверь остаётся, как была: закрывают её сами (изнутри — ЛКМ по двери).
	PC->Possess(this);
	PC->SetControlRotation(FRotator(-12.f, GetActorRotation().Yaw, 0.f));
	HeadYaw = 0.f;
	HeadPitch = 0.f;
	SetFirstPerson(true);
}

void APeregonCar::ExitCar(int32 ThroughSeat)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!Driver || !PC)
	{
		return;
	}
	APeregonCharacter* Walker = Driver;
	Driver = nullptr;
	// Выходим через выбранную дверь (по умолчанию — свою): она открывается и так и остаётся.
	const int32 ExitSeat = ThroughSeat >= 0 ? ThroughSeat : Seat;
	if (FPeregonMovingPart* Door = FindMovingPart(SeatDoor(ExitSeat)))
	{
		Door->Target = 1.f;
		Door->AutoCloseTimer = 0.f;
	}
	// Встаём сбоку у этой двери, лицом по ходу машины.
	const FVector Eye = SeatEye(ExitSeat);
	const FVector ExitLocation = GetActorTransform().TransformPosition(FVector(Eye.X - 3.f, Eye.Y < 0.f ? -150.f : 150.f, 60.f));
	Seat = 0;
	HeadBaseLocation = SeatEye(0);
	bHorn = false;
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
	ZoomAction = MakeAction(TEXT("IA_Car_Zoom"), EInputActionValueType::Boolean);
	WipersAction = MakeAction(TEXT("IA_Car_Wipers"), EInputActionValueType::Boolean);
	RadioAction = MakeAction(TEXT("IA_Car_Radio"), EInputActionValueType::Boolean);
	RadioNextAction = MakeAction(TEXT("IA_Car_RadioNext"), EInputActionValueType::Boolean);
	DomeAction = MakeAction(TEXT("IA_Car_Dome"), EInputActionValueType::Boolean);
	HeaterAction = MakeAction(TEXT("IA_Car_Heater"), EInputActionValueType::Boolean);
	HornAction = MakeAction(TEXT("IA_Car_Horn"), EInputActionValueType::Boolean);
	BlinkLeftAction = MakeAction(TEXT("IA_Car_BlinkLeft"), EInputActionValueType::Boolean);
	BlinkRightAction = MakeAction(TEXT("IA_Car_BlinkRight"), EInputActionValueType::Boolean);
	HazardAction = MakeAction(TEXT("IA_Car_Hazard"), EInputActionValueType::Boolean);
	SeatAction = MakeAction(TEXT("IA_Car_Seat"), EInputActionValueType::Boolean);
	WheelAction = MakeAction(TEXT("IA_Car_Wheel"), EInputActionValueType::Axis1D);

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
	Map(CameraAction, EKeys::V);
	Map(ZoomAction, EKeys::C);
	Map(ZoomAction, EKeys::RightMouseButton);
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
	Map(WipersAction, EKeys::K);
	Map(WipersAction, EKeys::Gamepad_DPad_Right);
	Map(RadioAction, EKeys::N);
	Map(RadioNextAction, EKeys::J);
	Map(DomeAction, EKeys::O);
	Map(HeaterAction, EKeys::U);
	Map(HornAction, EKeys::H);
	Map(HornAction, EKeys::Gamepad_LeftThumbstick);
	Map(BlinkLeftAction, EKeys::Z);
	Map(BlinkRightAction, EKeys::X);
	Map(HazardAction, EKeys::Y);
	Map(SeatAction, EKeys::T);
	Map(WheelAction, EKeys::MouseWheelAxis);
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
	EIC->BindAction(ZoomAction, ETriggerEvent::Started, this, &APeregonCar::OnZoomPressed);
	EIC->BindAction(ZoomAction, ETriggerEvent::Completed, this, &APeregonCar::OnZoomReleased);
	EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &APeregonCar::OnResetCar);
	EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &APeregonCar::OnInteractPressed);
	EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &APeregonCar::OnInteractReleased);
	EIC->BindAction(ExitAction, ETriggerEvent::Started, this, &APeregonCar::OnExit);
	EIC->BindAction(LightsAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleLights);
	EIC->BindAction(ParkingBrakeAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleParkingBrake);
	EIC->BindAction(DetailsAction, ETriggerEvent::Started, this, &APeregonCar::OnToggleDetails);
	EIC->BindAction(WipersAction, ETriggerEvent::Started, this, &APeregonCar::OnCycleWipers);
	EIC->BindAction(RadioAction, ETriggerEvent::Started, this, &APeregonCar::OnRadioPower);
	EIC->BindAction(RadioNextAction, ETriggerEvent::Started, this, &APeregonCar::OnRadioNext);
	EIC->BindAction(DomeAction, ETriggerEvent::Started, this, &APeregonCar::OnDome);
	EIC->BindAction(HeaterAction, ETriggerEvent::Started, this, &APeregonCar::OnHeater);
	EIC->BindAction(HornAction, ETriggerEvent::Started, this, &APeregonCar::OnHornPressed);
	EIC->BindAction(HornAction, ETriggerEvent::Completed, this, &APeregonCar::OnHornReleased);
	EIC->BindAction(BlinkLeftAction, ETriggerEvent::Started, this, &APeregonCar::OnBlinkLeft);
	EIC->BindAction(BlinkRightAction, ETriggerEvent::Started, this, &APeregonCar::OnBlinkRight);
	EIC->BindAction(HazardAction, ETriggerEvent::Started, this, &APeregonCar::OnHazard);
	EIC->BindAction(SeatAction, ETriggerEvent::Started, this, &APeregonCar::OnNextSeat);
	EIC->BindAction(WheelAction, ETriggerEvent::Triggered, this, &APeregonCar::OnWheel);
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

// С пассажирского места машиной не управляют.
void APeregonCar::OnThrottle(const FInputActionValue& Value) { ThrottleAxis = CanDrive() ? FMath::Clamp(Value.Get<float>(), 0.f, 1.f) : 0.f; }
void APeregonCar::OnBrake(const FInputActionValue& Value) { BrakeAxis = CanDrive() ? FMath::Clamp(Value.Get<float>(), 0.f, 1.f) : 0.f; }
void APeregonCar::OnSteer(const FInputActionValue& Value) { Movement->SetSteering(CanDrive() ? Value.Get<float>() : 0.f); }
void APeregonCar::OnClutch(const FInputActionValue& Value) { Movement->SetClutchPedal(CanDrive() ? Value.Get<float>() : 0.f); }
void APeregonCar::OnHandbrake(const FInputActionValue& Value) { Movement->SetHandbrake(CanDrive() ? Value.Get<float>() : 0.f); }
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
	if (!CanDrive())
	{
		return;
	}
	Movement->SetIgnition(true);
	Movement->SetStarter(true);
}

void APeregonCar::OnStarterReleased(const FInputActionValue&) { Movement->SetStarter(false); }
void APeregonCar::OnShiftUp(const FInputActionValue&)
{
	if (CanDrive())
	{
		Movement->ShiftUp();
	}
}
void APeregonCar::OnShiftDown(const FInputActionValue&)
{
	if (CanDrive())
	{
		Movement->ShiftDown();
	}
}
void APeregonCar::OnToggleIgnition(const FInputActionValue&)
{
	if (CanDrive())
	{
		Movement->SetIgnition(!Movement->GetIgnition());
	}
}
void APeregonCar::OnToggleGearbox(const FInputActionValue&) { Movement->SetAutoGearbox(!Movement->IsAutoGearbox()); }
// V: из салона → сзади → сверху издалека («изометрия», как в Заре) → снова из салона.
void APeregonCar::OnToggleCamera(const FInputActionValue&) { SetCameraMode((CameraMode + 1) % 3); }
void APeregonCar::OnZoomPressed(const FInputActionValue&) { bZoom = true; }
void APeregonCar::OnZoomReleased(const FInputActionValue&) { bZoom = false; }
void APeregonCar::OnToggleLights(const FInputActionValue&) { SetHeadlights(!bHeadlights); }

void APeregonCar::OnCycleWipers(const FInputActionValue&)
{
	WiperMode = (WiperMode + 1) % 3;
	PeregonAudio::PlayEvent(this, TEXT("lights.switch"), GetActorLocation(), 0.5f, 1.2f);
	if (APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>())
	{
		static const TCHAR* Names[] = {TEXT("Дворники выключены"), TEXT("Дворники: медленно"), TEXT("Дворники: быстро")};
		GM->Toast(Names[WiperMode], 1.5f);
	}
}

void APeregonCar::UpdateWipers(float DeltaSeconds)
{
	// Погода: сколько воды и снега летит на стекло (больше — на скорости).
	float Rain = 0.f;
	float Snowfall = 0.f;
	for (TActorIterator<APeregonSky> It(GetWorld()); It; ++It)
	{
		Rain = static_cast<float>(It->GetWeather().Rain);
		Snowfall = static_cast<float>(It->GetWeather().Snow);
		break;
	}
	const float Speed = FMath::Abs(Movement->GetForwardSpeed()); // м/с
	const float Hit = 1.f + FMath::Clamp(Speed / 20.f, 0.f, 1.5f);
	WaterLevel = FMath::Clamp(WaterLevel + DeltaSeconds * (Rain * 0.22f * Hit - (Rain < 0.02f ? 0.015f : 0.f)), 0.f, 1.f);
	SnowOnGlass = FMath::Clamp(SnowOnGlass + DeltaSeconds * (Snowfall * 0.12f * Hit - (Snowfall < 0.02f ? 0.01f : 0.f)), 0.f, 1.f);
	// Там, куда щётка не достаёт, вода и снег копятся и сходят только сами (ветер на скорости сдувает капли).
	WaterEdge = FMath::Clamp(WaterEdge + DeltaSeconds * (Rain * 0.22f * Hit - (Rain < 0.02f ? 0.008f : 0.f) - Speed * 0.0006f), 0.f, 1.f);
	SnowEdge = FMath::Clamp(SnowEdge + DeltaSeconds * (Snowfall * 0.14f * Hit - (Snowfall < 0.02f ? 0.004f : 0.f)), 0.f, 1.f);

	// Ход щётки: медленно ~40 взмахов в минуту, быстро ~65. Выключили — щётки доходят до упора и встают.
	const float Rate = WiperMode == 2 ? 1.1f : 0.68f;
	const bool bRunning = WiperMode > 0 && Movement->GetIgnition();
	if (bRunning || WiperPhase > 0.f)
	{
		const float Prev = WiperPhase;
		WiperPhase += DeltaSeconds * Rate;
		// Середина хода — щётка вверху: стекло на её пути вытерто.
		if (Prev < 0.5f && WiperPhase >= 0.5f)
		{
			WaterLevel *= 0.08f;
			SnowOnGlass *= 0.15f;
		}
		if (WiperPhase >= 1.f)
		{
			WaterLevel *= 0.3f;
			WiperPhase = bRunning ? FMath::Fmod(WiperPhase, 1.f) : 0.f;
			if (bRunning)
			{
				PeregonAudio::PlayEvent(this, TEXT("wiper.swipe"), GetActorLocation() + GetActorForwardVector() * 90.f, 0.35f);
			}
		}
		if (Prev == 0.f && bRunning)
		{
			PeregonAudio::PlayEvent(this, TEXT("wiper.swipe"), GetActorLocation() + GetActorForwardVector() * 90.f, 0.35f);
		}
	}
	const vmodel::Model& Model = PeregonCarModel::Get();
	const FVector Axis = PeregonCarModel::ToVector(Model.WiperAxis).GetSafeNormal();
	const float Angle = Model.WiperSweepDeg * 0.5f * (1.f - FMath::Cos(2.f * PI * WiperPhase));
	const FQuat Turn(Axis, FMath::DegreesToRadians(Angle));
	for (USceneComponent* Wiper : {WiperL.Get(), WiperR.Get()})
	{
		if (Wiper)
		{
			Wiper->SetRelativeRotation(Turn);
		}
	}
	if (WaterMaterial)
	{
		const float Amount = FMath::Max(WaterLevel, SnowOnGlass);
		WaterMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.9f * Amount);
		WaterMaterial->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FLinearColor(0.85f, 0.9f, 0.95f), FLinearColor(1.f, 1.f, 1.f),
			SnowOnGlass > WaterLevel ? 1.f : 0.f));
	}
	if (WaterEdgeMaterial)
	{
		WaterEdgeMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.95f * FMath::Max(WaterEdge, SnowEdge));
		WaterEdgeMaterial->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FLinearColor(0.85f, 0.9f, 0.95f), FLinearColor(1.f, 1.f, 1.f),
			SnowEdge > WaterEdge ? 1.f : 0.f));
	}
}
void APeregonCar::OnToggleParkingBrake(const FInputActionValue&)
{
	Movement->SetParkingBrake(!Movement->IsParkingBrake());
	PeregonAudio::PlayEvent(this, TEXT("handbrake"), HandbrakeLever->GetComponentLocation(), 0.6f);
}

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
		// Смотрим на открытую дверь — выходим через неё (хоть через соседнюю), иначе — через свою.
		int32 Through = -1;
		if (const UPrimitiveComponent* Looked = Focus.Part.Get())
		{
			for (const FName& Tag : Looked->ComponentTags)
			{
				const int32 DoorSeat = DoorSeatIndex(Tag);
				const FPeregonMovingPart* Moving = DoorSeat >= 0 ? FindMovingPart(Tag) : nullptr;
				if (Moving && Moving->Target > 0.5f)
				{
					Through = DoorSeat;
				}
			}
		}
		ExitCar(Through);
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

bool APeregonCar::IsWiped(const FVector& BodyPoint) const
{
	// Сектор, который проходит щётка: от положения покоя на угол хода, по радиусу — от ближнего до дальнего конца щётки.
	const vmodel::Model& Model = PeregonCarModel::Get();
	const FVector Axis = PeregonCarModel::ToVector(Model.WiperAxis).GetSafeNormal();
	const float Sweep = Model.WiperSweepDeg;
	for (vmodel::Part Id : {vmodel::Part::WiperL, vmodel::Part::WiperR})
	{
		const vmodel::PartMesh* Blade = Model.Find(Id);
		if (!Blade || Axis.IsNearlyZero())
		{
			continue;
		}
		float RMin = 1e9f;
		float RMax = 0.f;
		FVector Rest = FVector::ZeroVector;
		for (const vmodel::Section& Sec : Blade->Sections)
		{
			for (const auto& Pos : Sec.Positions)
			{
				const FVector L = FVector::VectorPlaneProject(PeregonCarModel::ToVector(Pos), Axis);
				const float R = static_cast<float>(L.Size());
				if (R > 4.f)
				{
					RMin = FMath::Min(RMin, R);
				}
				if (R > RMax)
				{
					RMax = R;
					Rest = L;
				}
			}
		}
		if (RMax < 5.f)
		{
			continue;
		}
		const FVector V = FVector::VectorPlaneProject(BodyPoint - PeregonCarModel::ToVector(Blade->Origin), Axis);
		const float R = static_cast<float>(V.Size());
		if (R < RMin * 0.8f || R > RMax + 2.f)
		{
			continue;
		}
		// Угол от положения покоя вокруг оси, со знаком хода.
		const FVector A = Rest.GetSafeNormal();
		const FVector B = V.GetSafeNormal();
		const float Deg = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(FVector::DotProduct(FVector::CrossProduct(A, B), Axis)),
			static_cast<float>(FVector::DotProduct(A, B))));
		const float Along = Sweep >= 0.f ? Deg : -Deg;
		if (Along > -4.f && Along < FMath::Abs(Sweep) + 4.f)
		{
			return true;
		}
	}
	return false;
}

int32 APeregonCar::DoorSeatIndex(FName Tag)
{
	return Tag == FName(TEXT("DoorFL")) ? 0 : Tag == FName(TEXT("DoorFR")) ? 1 : Tag == FName(TEXT("DoorRL")) ? 2 : Tag == FName(TEXT("DoorRR")) ? 3 : -1;
}

bool APeregonCar::IsInDoorway(FName Tag, const APawn* User) const
{
	// Стоит ли пешеход в проёме открытой двери: со стороны салона от плоскости двери (а не снаружи у её края).
	const FPeregonMovingPart* Moving = FindMovingPart(Tag);
	if (!Moving || !Moving->Pivot || !User)
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(User->GetActorLocation());
	const int32 DoorSeat = DoorSeatIndex(Tag);
	const FVector Eye = SeatEye(FMath::Max(0, DoorSeat));
	// Проём — полоса вдоль кузова у этой двери: по X — у сиденья, по Y — до ~1.4 м от борта.
	const bool bAlong = FMath::Abs(Local.X - Eye.X) < 75.f;
	const bool bSide = Local.Y * FMath::Sign(Eye.Y) > 60.f && Local.Y * FMath::Sign(Eye.Y) < 150.f;
	// За дверью (с её наружной стороны) — нет: проверяем, по какую сторону открытой створки стоим.
	const FVector Hinge = GetActorTransform().InverseTransformPosition(Moving->Pivot->GetComponentLocation());
	const FVector DoorDir = (GetActorTransform().InverseTransformVector(Moving->Pivot->GetForwardVector() * -1.f)).GetSafeNormal2D();
	const FVector ToUser = (Local - Hinge).GetSafeNormal2D();
	const float Cross = static_cast<float>(DoorDir.X * ToUser.Y - DoorDir.Y * ToUser.X) * FMath::Sign(Eye.Y);
	return bAlong && bSide && Cross < 0.35f;
}

int32 APeregonCar::SeatHitIndex(const UPrimitiveComponent* Part)
{
	static const FName SeatTags[] = {FName(TEXT("Seat0")), FName(TEXT("Seat1")), FName(TEXT("Seat2")), FName(TEXT("Seat3"))};
	for (int32 I = 0; I < 4; ++I)
	{
		if (Part && Part->ComponentHasTag(SeatTags[I]))
		{
			return I;
		}
	}
	return -1;
}
