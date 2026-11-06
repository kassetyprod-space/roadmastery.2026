#include "Vehicle/PeregonVehicleMovementComponent.h"

#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "vsim/Presets.h"

#include <vector>

namespace PeregonVehicle
{
constexpr double MToCm = 100.0;
constexpr double CmToM = 0.01;

vsim::VehicleConfig MakeConfig(EPeregonVehiclePreset Preset)
{
	switch (Preset)
	{
	case EPeregonVehiclePreset::Sokol1500:
	default:
		return vsim::Presets::Sokol1500();
	}
}

FPeregonSurface MakeSurface(float Grip, float Rolling, float Tread, float Rough)
{
	FPeregonSurface S;
	S.Grip = Grip;
	S.RollingResistance = Rolling;
	S.TreadSensitivity = Tread;
	S.Roughness = Rough;
	return S;
}

struct FWheelFrame
{
	FVector Attach = FVector::ZeroVector;
	FVector Contact = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	FVector Right = FVector::RightVector;
	bool bHit = false;
};
} // namespace PeregonVehicle

UPeregonVehicleMovementComponent::UPeregonVehicleMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	using PeregonVehicle::MakeSurface;
	// Порядок типов совпадает с Config/DefaultEngine.ini → PhysicalSurfaces.
	Surfaces.Add(SurfaceType1, MakeSurface(1.00f, 1.0f, 0.0f, 0.05f)); // Асфальт
	Surfaces.Add(SurfaceType2, MakeSurface(0.70f, 1.6f, 0.4f, 0.40f)); // Гравий
	Surfaces.Add(SurfaceType3, MakeSurface(0.65f, 1.8f, 0.5f, 0.50f)); // Грунтовка
	Surfaces.Add(SurfaceType4, MakeSurface(0.40f, 4.0f, 0.9f, 0.60f)); // Грязь
	Surfaces.Add(SurfaceType5, MakeSurface(0.32f, 2.0f, 1.0f, 0.20f)); // Снег
	Surfaces.Add(SurfaceType6, MakeSurface(0.10f, 1.0f, 0.3f, 0.05f)); // Лёд
	Surfaces.Add(SurfaceType7, MakeSurface(0.55f, 2.2f, 0.6f, 0.30f)); // Трава
	Surfaces.Add(SurfaceType8, MakeSurface(0.60f, 5.0f, 0.7f, 0.30f)); // Песок
	DefaultSurface = MakeSurface(0.90f, 1.1f, 0.2f, 0.10f);

	EnsureVehicle();
}

UPeregonVehicleMovementComponent::~UPeregonVehicleMovementComponent() = default;

void UPeregonVehicleMovementComponent::EnsureVehicle()
{
	if (!Sim)
	{
		Sim = MakeUnique<vsim::Vehicle>(PeregonVehicle::MakeConfig(Preset));
	}
	const int32 N = static_cast<int32>(Sim->NumWheels());
	WheelCompressionM.Init(0.f, N);
	WheelInContact.Init(false, N);
}

void UPeregonVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// Пересобираем симуляцию: пресет мог поменяться в редакторе.
	Sim.Reset();
	EnsureVehicle();

	if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
	{
		Body->SetMassOverrideInKg(NAME_None, GetVehicleMassKg(), true);
	}
}

int32 UPeregonVehicleMovementComponent::GetNumWheels() const
{
	return Sim ? static_cast<int32>(Sim->NumWheels()) : 0;
}

float UPeregonVehicleMovementComponent::GetVehicleMassKg() const
{
	return Sim ? static_cast<float>(Sim->Config().Mass) : 1000.f;
}

FVector UPeregonVehicleMovementComponent::GetWheelAttachLocal(int32 Index) const
{
	if (!Sim || Index < 0 || Index >= GetNumWheels())
	{
		return FVector::ZeroVector;
	}
	const vsim::WheelConfig& W = Sim->GetWheelConfig(static_cast<size_t>(Index));
	// Точка крепления = центр колеса при полном сжатии (полный отбой + ход).
	return FVector(W.X, W.Y, W.Z + W.Suspension.Travel) * PeregonVehicle::MToCm;
}

float UPeregonVehicleMovementComponent::GetWheelRadiusCm(int32 Index) const
{
	return Sim && Index >= 0 && Index < GetNumWheels()
		? static_cast<float>(Sim->GetWheelConfig(static_cast<size_t>(Index)).Tire.Radius * PeregonVehicle::MToCm)
		: 30.f;
}

float UPeregonVehicleMovementComponent::GetWheelWidthCm(int32 Index) const
{
	return Sim && Index >= 0 && Index < GetNumWheels()
		? static_cast<float>(Sim->GetWheelConfig(static_cast<size_t>(Index)).Tire.Width * PeregonVehicle::MToCm)
		: 18.f;
}

FPeregonSurface UPeregonVehicleMovementComponent::LookupSurface(const UPhysicalMaterial* Material) const
{
	if (Material)
	{
		if (const FPeregonSurface* Found = Surfaces.Find(Material->SurfaceType))
		{
			return *Found;
		}
	}
	return DefaultSurface;
}

void UPeregonVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UPrimitiveComponent* Body = GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
	if (!Sim || !Body || !Body->IsSimulatingPhysics() || DeltaTime <= 0.f)
	{
		return;
	}
	// Ограничиваем шаг при подвисаниях кадра: ядро сабстепит само, но силы к кузову прикладываются раз в кадр.
	StepVehicle(FMath::Min(DeltaTime, 1.f / 20.f), Body);
	UpdateWheelVisuals();
}

void UPeregonVehicleMovementComponent::StepVehicle(float DeltaTime, UPrimitiveComponent* Body)
{
	using namespace PeregonVehicle;

	UWorld* World = GetWorld();
	const FTransform T = Body->GetComponentTransform();
	const FVector Up = T.GetUnitAxis(EAxis::Z);
	const FVector Fwd = T.GetUnitAxis(EAxis::X);
	const FVector BodyVel = Body->GetPhysicsLinearVelocity();
	ForwardSpeedMs = static_cast<float>(FVector::DotProduct(BodyVel, Fwd) * CmToM);

	const int32 N = GetNumWheels();
	std::vector<vsim::WheelContact> Contacts(static_cast<size_t>(N));
	TArray<FWheelFrame> Frames;
	Frames.SetNum(N);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PeregonWheelTrace), false, GetOwner());
	Params.bReturnPhysicalMaterial = true;

	for (int32 I = 0; I < N; ++I)
	{
		const vsim::WheelConfig& W = Sim->GetWheelConfig(static_cast<size_t>(I));
		FWheelFrame& F = Frames[I];
		vsim::WheelContact& C = Contacts[static_cast<size_t>(I)];

		F.Attach = T.TransformPosition(GetWheelAttachLocal(I));

		// Колесо — не луч, а сфера (половина радиуса шины), опущенная так, что её низ совпадает
		// с низом колеса. Так кочки, бордюры и «стиральная доска» ощущаются плавно, без ступенек.
		const double WheelR = W.Tire.Radius * MToCm;
		const double SphereR = WheelR * 0.5;
		const double TravelCm = W.Suspension.Travel * MToCm;
		const FVector SweepStart = F.Attach - Up * (WheelR - SphereR);
		const FVector SweepEnd = SweepStart - Up * TravelCm;
		FHitResult Hit;
		const bool bSweepHit = World->SweepSingleByChannel(Hit, SweepStart, SweepEnd, FQuat::Identity, WheelTraceChannel,
			FCollisionShape::MakeSphere(static_cast<float>(SphereR)), Params);

		double CompressionCm = 0.0;
		FVector Normal = Up;
		if (bSweepHit && Hit.bStartPenetrating)
		{
			// Сфера уже внутри препятствия (кочка выше полного сжатия): держим на отбойнике,
			// без «выстрела» — иначе машина подпрыгивает и проваливается.
			CompressionCm = TravelCm;
			F.Contact = SweepStart - Up * SphereR;
			F.bHit = true;
		}
		else if (bSweepHit && FVector::DotProduct(Hit.ImpactNormal, Up) > 0.35)
		{
			CompressionCm = TravelCm - Hit.Distance;
			F.Contact = Hit.ImpactPoint;
			Normal = Hit.ImpactNormal;
			F.bHit = true;
		}
		else
		{
			F.bHit = false; // в воздухе или зацепили стену боком
		}
		if (!F.bHit)
		{
			C.bInContact = false;
			continue;
		}

		const double Steer = Sim->SteerAngle(static_cast<size_t>(I));
		const FVector WheelFwd = T.TransformVectorNoScale(FVector(FMath::Cos(Steer), FMath::Sin(Steer), 0.0));
		F.Forward = (WheelFwd - Normal * FVector::DotProduct(WheelFwd, Normal)).GetSafeNormal();
		F.Right = FVector::CrossProduct(Normal, F.Forward);

		const FVector ContactVel = Body->GetPhysicsLinearVelocityAtPoint(F.Contact);
		const FVector AttachVel = Body->GetPhysicsLinearVelocityAtPoint(F.Attach);
		const FPeregonSurface Surface = LookupSurface(Hit.PhysMaterial.Get());

		C.bInContact = true;
		C.Compression = CompressionCm * CmToM;
		C.CompressionVelocity = -FVector::DotProduct(AttachVel, Up) * CmToM;
		C.Vx = FVector::DotProduct(ContactVel, F.Forward) * CmToM;
		C.Vy = FVector::DotProduct(ContactVel, F.Right) * CmToM;
		C.Surface.Grip = Surface.Grip;
		C.Surface.RollingResistance = Surface.RollingResistance;
		C.Surface.TreadSensitivity = Surface.TreadSensitivity;
		C.Surface.Roughness = Surface.Roughness;
	}

	vsim::Assists A;
	A.bAutoGearbox = bAutoGearbox;
	A.bAutoClutch = bAutoClutch || bAutoGearbox;
	A.bSpeedSensitiveSteering = bSpeedSensitiveSteering;
	A.bAbs = bAbs;
	Sim->SetAssists(A);

	vsim::DriverInput In;
	In.Throttle = Throttle;
	In.Brake = Brake;
	In.Steer = Steering;
	In.Clutch = ClutchPedal;
	In.Handbrake = FMath::Max(Handbrake, bParkingBrake ? 1.f : 0.f);
	In.bStarter = bStarter;
	In.bIgnition = bIgnition;
	Sim->SetInput(In);
	Sim->Step(DeltaTime, Contacts, ForwardSpeedMs, AmbientTemperatureC);

	for (int32 I = 0; I < N; ++I)
	{
		const FWheelFrame& F = Frames[I];
		const vsim::WheelOutput& O = Sim->GetWheel(static_cast<size_t>(I));
		WheelInContact[I] = F.bHit;
		WheelCompressionM[I] = F.bHit ? static_cast<float>(Contacts[static_cast<size_t>(I)].Compression) : 0.f;
		if (!F.bHit)
		{
			continue;
		}
		// Ньютоны → кг·см/с² (единицы Unreal).
		const FVector SuspensionForce = Up * O.SuspensionForce * MToCm;
		const FVector TireForce = (F.Forward * O.Fx + F.Right * O.Fy) * MToCm;
		Body->AddForceAtLocation(SuspensionForce, F.Attach);
		Body->AddForceAtLocation(TireForce, F.Contact + Up * TireForceHeightCm);

		if (bDrawDebug)
		{
			DrawDebugLine(World, F.Attach, F.Contact, FColor::Yellow, false, -1.f, 0, 1.f);
			DrawDebugLine(World, F.Contact, F.Contact + TireForce * 0.0005, O.bSliding ? FColor::Red : FColor::Green, false, -1.f, 0, 2.f);
		}
	}

	// Реакция мотора: момент на маховике качает кузов вокруг продольной оси (особенно на газу в первой).
	const double ReactionNm = Sim->GetTelemetry().ClutchTorque * EngineReactionScale;
	Body->AddTorqueInRadians(Fwd * ReactionNm * 1.0e4); // Н·м → кг·см²/с²

	const double SpeedMs = BodyVel.Size() * CmToM;
	if (SpeedMs > 0.1)
	{
		Body->AddForce(-BodyVel.GetSafeNormal() * Sim->AeroDragForce(SpeedMs) * MToCm);
	}
}

void UPeregonVehicleMovementComponent::UpdateWheelVisuals()
{
	const int32 N = FMath::Min(GetNumWheels(), WheelVisuals.Num());
	for (int32 I = 0; I < N; ++I)
	{
		USceneComponent* Visual = WheelVisuals[I];
		if (!Visual)
		{
			continue;
		}
		const vsim::WheelConfig& W = Sim->GetWheelConfig(static_cast<size_t>(I));
		const vsim::WheelOutput& O = Sim->GetWheel(static_cast<size_t>(I));
		const double Compression = WheelInContact[I] ? FMath::Min<double>(WheelCompressionM[I], W.Suspension.Travel) : 0.0;
		const FVector Local = GetWheelAttachLocal(I) - FVector(0.0, 0.0, (W.Suspension.Travel - Compression) * PeregonVehicle::MToCm);
		const FQuat Rotation = FQuat(FVector::UpVector, O.SteerAngle) * FQuat(FVector::RightVector, -O.SpinAngle);
		Visual->SetRelativeLocationAndRotation(Local, Rotation);
	}
}

bool UPeregonVehicleMovementComponent::ShiftUp() { return Sim && Sim->ShiftUp(); }
bool UPeregonVehicleMovementComponent::ShiftDown() { return Sim && Sim->ShiftDown(); }
bool UPeregonVehicleMovementComponent::RequestGear(int32 Gear) { return Sim && Sim->RequestGear(Gear); }
int32 UPeregonVehicleMovementComponent::GetGear() const { return Sim ? Sim->GetGear() : 0; }

void UPeregonVehicleMovementComponent::SetAutoGearbox(bool bAuto)
{
	bAutoGearbox = bAuto;
}

void UPeregonVehicleMovementComponent::Refuel(float Liters)
{
	if (Sim)
	{
		vsim::VehicleCondition& Cond = Sim->Condition();
		Cond.FuelL = FMath::Min(Sim->Config().FuelCapacityL, Cond.FuelL + FMath::Max(0.f, Liters));
	}
}

void UPeregonVehicleMovementComponent::RepairAll()
{
	if (!Sim)
	{
		return;
	}
	vsim::VehicleCondition& Cond = Sim->Condition();
	Cond.Engine = Cond.Clutch = Cond.Gearbox = Cond.Radiator = 1.0;
	for (size_t I = 0; I < Cond.Wheels.size(); ++I)
	{
		const vsim::WheelConfig& W = Sim->GetWheelConfig(I);
		Cond.Wheels[I].TreadMm = W.Tire.TreadDepthNewMm;
		Cond.Wheels[I].PadMm = W.Brake.PadNewMm;
		Cond.Wheels[I].Shock = 1.0;
	}
}

FPeregonTelemetry UPeregonVehicleMovementComponent::GetTelemetry() const
{
	FPeregonTelemetry Out;
	if (!Sim)
	{
		return Out;
	}
	const vsim::Telemetry T = Sim->GetTelemetry();
	const vsim::VehicleCondition& C = Sim->Condition();
	Out.SpeedKmh = static_cast<float>(T.SpeedKmh);
	Out.Rpm = static_cast<float>(T.Rpm);
	Out.Gear = T.Gear;
	Out.ClutchEngagement = static_cast<float>(T.ClutchEngagement);
	Out.FuelL = static_cast<float>(T.FuelL);
	Out.FuelCapacityL = static_cast<float>(Sim->Config().FuelCapacityL);
	Out.InstantLPer100Km = static_cast<float>(T.InstantLPer100Km);
	Out.EngineTempC = static_cast<float>(T.EngineTempC);
	Out.OdometerKm = static_cast<float>(T.OdometerKm);
	Out.bEngineRunning = T.bEngineRunning;
	Out.bFanOn = T.bFanOn;
	Out.bGearGrind = T.bGearGrind;
	Out.EngineCondition = static_cast<float>(C.Engine);
	Out.ClutchCondition = static_cast<float>(C.Clutch);
	Out.RadiatorCondition = static_cast<float>(C.Radiator);
	Out.SteeringWheelDeg = static_cast<float>(T.SteeringWheelDeg);
	for (size_t I = 0; I < Sim->NumWheels(); ++I)
	{
		const vsim::WheelOutput& O = Sim->GetWheel(I);
		Out.TreadMm.Add(static_cast<float>(C.Wheels[I].TreadMm));
		Out.PadMm.Add(static_cast<float>(C.Wheels[I].PadMm));
		Out.ShockCondition.Add(static_cast<float>(C.Wheels[I].Shock));
		Out.SlipRatio.Add(static_cast<float>(O.SlipRatio));
		Out.SlipAngleDeg.Add(static_cast<float>(FMath::RadiansToDegrees(O.SlipAngle)));
		Out.WheelLoadN.Add(static_cast<float>(O.Load));
		Out.BrakeTempC.Add(static_cast<float>(O.BrakeTempC));
	}
	return Out;
}

float UPeregonVehicleMovementComponent::GetFuelCapacityL() const
{
	return Sim ? static_cast<float>(Sim->Config().FuelCapacityL) : 0.f;
}

FPeregonCarState UPeregonVehicleMovementComponent::GetCarState() const
{
	FPeregonCarState Out;
	if (!Sim)
	{
		return Out;
	}
	const vsim::VehicleCondition& C = Sim->Condition();
	Out.FuelL = static_cast<float>(C.FuelL);
	Out.Engine = static_cast<float>(C.Engine);
	Out.Clutch = static_cast<float>(C.Clutch);
	Out.Gearbox = static_cast<float>(C.Gearbox);
	Out.Radiator = static_cast<float>(C.Radiator);
	Out.OdometerKm = static_cast<float>(Sim->GetTelemetry().OdometerKm);
	for (const vsim::WheelCondition& W : C.Wheels)
	{
		Out.TreadMm.Add(static_cast<float>(W.TreadMm));
		Out.PadMm.Add(static_cast<float>(W.PadMm));
		Out.Shock.Add(static_cast<float>(W.Shock));
	}
	return Out;
}

void UPeregonVehicleMovementComponent::ApplyCarState(const FPeregonCarState& State)
{
	EnsureVehicle();
	vsim::VehicleCondition& C = Sim->Condition();
	C.FuelL = FMath::Clamp<double>(State.FuelL, 0.0, Sim->Config().FuelCapacityL);
	C.Engine = State.Engine;
	C.Clutch = State.Clutch;
	C.Gearbox = State.Gearbox;
	C.Radiator = State.Radiator;
	Sim->SetOdometer(State.OdometerKm * 1000.0);
	for (int32 I = 0; I < static_cast<int32>(C.Wheels.size()); ++I)
	{
		if (State.TreadMm.IsValidIndex(I)) { C.Wheels[static_cast<size_t>(I)].TreadMm = State.TreadMm[I]; }
		if (State.PadMm.IsValidIndex(I)) { C.Wheels[static_cast<size_t>(I)].PadMm = State.PadMm[I]; }
		if (State.Shock.IsValidIndex(I)) { C.Wheels[static_cast<size_t>(I)].Shock = State.Shock[I]; }
	}
}
