#include "vsim/Vehicle.h"

namespace vsim
{
namespace
{
constexpr double kFuelLowerHeatingValue = 44.0e6; // Дж/кг
constexpr double kIdleMinFuelKgPerS = 0.5 * kFuelDensityKgPerL / 3600.0; // ~0.5 л/ч минимум у работающего мотора
constexpr double kBearingTorque = 0.5;
constexpr double kInfiniteTorque = 1.0e9;

// Момент, который нужно передать между двумя вращающимися телами, чтобы за шаг H их скорости сравнялись.
// Неявная (устойчивая) модель «сцепки»: сцепление, блокировка дифференциала, межосевая муфта.
double LockTorque(double OmegaA, double OmegaB, double InertiaA, double InertiaB, double H)
{
	return (OmegaA - OmegaB) / (H * (1.0 / InertiaA + 1.0 / InertiaB));
}
} // namespace

Vehicle::Vehicle(const VehicleConfig& InConfig)
	: Cfg(InConfig)
{
	Wheels.resize(Cfg.Wheels.size());
	Cond.Wheels.resize(Cfg.Wheels.size());
	int MaxAxle = 0;
	double RearSum = 0.0;
	int RearCount = 0;
	for (size_t I = 0; I < Cfg.Wheels.size(); ++I)
	{
		const WheelConfig& W = Cfg.Wheels[I];
		Wheels[I].TireModel.Init(W.Tire);
		Wheels[I].TireSt.TreadMm = W.Tire.TreadDepthNewMm;
		Cond.Wheels[I].TreadMm = W.Tire.TreadDepthNewMm;
		Cond.Wheels[I].PadMm = W.Brake.PadNewMm;
		MaxAxle = std::max(MaxAxle, W.Axle);
		if (!W.bSteered)
		{
			RearSum += W.X;
			++RearCount;
		}
		if (W.bDriven && std::find(DrivenAxles.begin(), DrivenAxles.end(), W.Axle) == DrivenAxles.end())
		{
			DrivenAxles.push_back(W.Axle);
		}
	}
	RearAxleX = RearCount > 0 ? RearSum / RearCount : 0.0;
	Cfg.AntiRollRate.resize(static_cast<size_t>(MaxAxle + 1), 0.0);
	Cond.FuelL = std::min(30.0, Cfg.FuelCapacityL);
	IdleBias = EngineModel::IdleThrottleBias(Cfg.Engine);
}

double Vehicle::TotalRatio() const
{
	const GearboxParams& G = Cfg.Gearbox;
	if (Gear > 0)
	{
		return G.Ratios[static_cast<size_t>(Gear - 1)] * G.FinalDrive;
	}
	if (Gear < 0)
	{
		return -G.ReverseRatio * G.FinalDrive;
	}
	return 0.0;
}

double Vehicle::DrivenWheelsOmega() const
{
	double Sum = 0.0;
	int Count = 0;
	for (size_t I = 0; I < Wheels.size(); ++I)
	{
		if (Cfg.Wheels[I].bDriven)
		{
			Sum += Wheels[I].Omega;
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : 0.0;
}

bool Vehicle::ShiftUp() { return RequestGear(Gear + 1); }
bool Vehicle::ShiftDown() { return RequestGear(Gear - 1); }

bool Vehicle::RequestGear(int NewGear)
{
	NewGear = static_cast<int>(Clamp(NewGear, -1, NumForwardGears()));
	if (NewGear == Gear)
	{
		return true;
	}
	const bool bAutoClutch = Assist.bAutoClutch || Assist.bAutoGearbox;
	// Задняя на ходу вперёд (и наоборот) — хруст, передача не включается.
	const bool bWrongDirection = (NewGear == -1 && LastSpeed > 1.5) || (NewGear >= 1 && Gear == -1 && LastSpeed < -1.5);
	// Без автосцепления передача включается только с выжатой педалью (в нейтраль — всегда).
	const bool bClutchNotPressed = !bAutoClutch && NewGear != 0 && Input.Clutch < 0.8;
	if (bWrongDirection || bClutchNotPressed)
	{
		GrindTimer = 0.6;
		Cond.Gearbox = std::max(0.0, Cond.Gearbox - 0.002);
		return false;
	}
	Gear = NewGear;
	if (bAutoClutch)
	{
		ShiftTimer = Cfg.Gearbox.ShiftTime;
		ShiftCooldown = Cfg.Gearbox.ShiftTime + 0.6;
	}
	return true;
}

void Vehicle::ForceEngineRunning(double Rpm)
{
	EngineSt.bRunning = true;
	EngineSt.Omega = RpmToRadS(Rpm);
}

double Vehicle::AeroDragForce(double Speed) const
{
	return 0.5 * Cfg.Aero.AirDensity * Cfg.Aero.DragCoefficient * Cfg.Aero.FrontalArea * Speed * Speed;
}

void Vehicle::UpdateSteering(double Dt, double ForwardSpeed)
{
	const SteeringParams& S = Cfg.Steering;
	double Target = Clamp(Input.Steer, -1.0, 1.0);
	if (Assist.bSpeedSensitiveSteering)
	{
		Target *= Lerp(1.0, S.HighSpeedFraction, SmoothStep(5.0, 35.0, std::abs(ForwardSpeed)));
	}
	const bool bReturning = std::abs(Target) < std::abs(SteerState) || Target * SteerState < 0.0;
	SteerState = MoveTowards(SteerState, Target, (bReturning ? S.ReturnRate : S.InputRate) * Dt);

	const double Delta = SteerState * S.MaxAngleDeg * kPi / 180.0;
	double SteeredXSum = 0.0;
	int SteeredCount = 0;
	for (const WheelConfig& W : Cfg.Wheels)
	{
		if (W.bSteered)
		{
			SteeredXSum += W.X;
			++SteeredCount;
		}
	}
	const double CenterBase = SteeredCount > 0 ? SteeredXSum / SteeredCount - RearAxleX : 2.4;

	for (size_t I = 0; I < Wheels.size(); ++I)
	{
		const WheelConfig& W = Cfg.Wheels[I];
		if (!W.bSteered || std::abs(Delta) < 1e-5)
		{
			Wheels[I].SteerAngle = 0.0;
			continue;
		}
		// Аккерман: внутреннее колесо поворачивается сильнее внешнего.
		const double Sgn = SignOf(Delta);
		const double Radius = CenterBase / std::tan(std::abs(Delta));
		const double Base = W.X - RearAxleX;
		Wheels[I].SteerAngle = Sgn * std::atan2(Base, Radius - W.Y * Sgn);
	}
}

void Vehicle::UpdateAutoGearbox(double Dt)
{
	ShiftCooldown = std::max(0.0, ShiftCooldown - Dt);
	if (!Assist.bAutoGearbox || Gear < 1 || ShiftTimer > 0.0 || ShiftCooldown > 0.0)
	{
		return;
	}
	const EngineParams& E = Cfg.Engine;
	const double WheelRpm = RadSToRpm(std::abs(DrivenWheelsOmega() * TotalRatio()));
	// Повышаем рано при спокойной езде (экономия), поздно — при полном газе.
	const double UpRpm = Lerp(2300.0, E.RedlineRpm - 300.0, Sq(Clamp01(Input.Throttle)));
	const double DownRpm = Lerp(1300.0, 3000.0, Clamp01(Input.Throttle));
	if (WheelRpm > UpRpm && Gear < NumForwardGears() && ClutchEngagement > 0.9)
	{
		ShiftUp();
	}
	else if (Gear > 1 && WheelRpm < DownRpm)
	{
		const auto& R = Cfg.Gearbox.Ratios;
		const double LowerRpm = WheelRpm * R[static_cast<size_t>(Gear - 2)] / R[static_cast<size_t>(Gear - 1)];
		// Гистерезис: после понижения обороты не должны сразу звать повышение — иначе «охота» передач.
		if (LowerRpm < E.RedlineRpm - 400.0 && LowerRpm < UpRpm * 0.92)
		{
			ShiftDown();
		}
	}
}

void Vehicle::UpdateClutchEngagement(double Dt)
{
	if (Assist.bAutoClutch || Assist.bAutoGearbox)
	{
		double Target = 0.0;
		if (Gear != 0 && ShiftTimer <= 0.0 && EngineSt.bRunning)
		{
			// «Центробежное» сцепление: схватывает по мере роста оборотов, не даёт заглохнуть.
			const double Idle = Cfg.Engine.IdleRpm;
			Target = SmoothStep(Idle + 150.0, Idle + 1100.0, EngineSt.Rpm());
		}
		ShiftTimer = std::max(0.0, ShiftTimer - Dt);
		const double Rate = Target > ClutchEngagement ? 4.0 : 12.0;
		ClutchEngagement = MoveTowards(ClutchEngagement, Target, Rate * Dt);
	}
	else
	{
		ClutchEngagement = 1.0 - Clamp01(Input.Clutch);
	}
}

void Vehicle::UpdateSuspension(const std::vector<WheelContact>& Contacts)
{
	for (size_t I = 0; I < Wheels.size(); ++I)
	{
		const WheelContact& C = Contacts[I];
		const SuspensionParams& S = Cfg.Wheels[I].Suspension;
		double F = 0.0;
		if (C.bInContact)
		{
			const double X = std::max(0.0, C.Compression);
			const double ShockMul = 0.3 + 0.7 * Clamp01(Cond.Wheels[I].Shock);
			const double Vel = Clamp(C.CompressionVelocity, -S.MaxCompressionSpeed, S.MaxCompressionSpeed);
			const double Damping = (Vel > 0.0 ? S.BumpDamping : S.ReboundDamping) * ShockMul;
			F = S.Preload + S.SpringRate * X + Damping * Vel;
			const double BumpStart = S.Travel - S.BumpStopRange;
			if (X > BumpStart)
			{
				F += S.BumpStopRate * Sq(std::min(X, S.Travel) - BumpStart);
			}
			F = std::min(F, S.MaxForce);
		}
		Wheels[I].SuspensionForce = F;
	}

	// Стабилизаторы поперечной устойчивости.
	for (size_t Axle = 0; Axle < Cfg.AntiRollRate.size(); ++Axle)
	{
		const double Rate = Cfg.AntiRollRate[Axle];
		if (Rate <= 0.0)
		{
			continue;
		}
		int Left = -1;
		int Right = -1;
		for (size_t I = 0; I < Wheels.size(); ++I)
		{
			if (Cfg.Wheels[I].Axle != static_cast<int>(Axle))
			{
				continue;
			}
			if (Cfg.Wheels[I].Side < 0)
			{
				Left = static_cast<int>(I);
			}
			else if (Cfg.Wheels[I].Side > 0)
			{
				Right = static_cast<int>(I);
			}
		}
		if (Left < 0 || Right < 0 || !Contacts[Left].bInContact || !Contacts[Right].bInContact)
		{
			continue;
		}
		const double DF = Rate * (Contacts[Left].Compression - Contacts[Right].Compression);
		Wheels[Left].SuspensionForce += DF;
		Wheels[Right].SuspensionForce -= DF;
	}

	for (WheelRuntime& W : Wheels)
	{
		W.SuspensionForce = std::max(0.0, W.SuspensionForce);
		W.Load = W.SuspensionForce;
	}
}

void Vehicle::Substep(double H, const std::vector<WheelContact>& Contacts)
{
	const EngineParams& EP = Cfg.Engine;
	const double Rpm = EngineSt.Rpm();
	const double EngCond = Cond.Engine;

	// --- Двигатель: момент сгорания и трения ---
	double Throttle = 0.0;
	double CombustionTorque = 0.0;
	if (EngineSt.bRunning)
	{
		const double IdleThrottle = Clamp(IdleBias + 0.0012 * (EP.IdleRpm - Rpm), 0.0, 0.4);
		// Впуск не мгновенный: мотор подхватывает газ за ~60 мс, а не за один кадр.
		ThrottleState += (Clamp01(Input.Throttle) - ThrottleState) * std::min(1.0, H / 0.06);
		Throttle = std::max(ThrottleState, IdleThrottle);
		// Автосцепление сбрасывает газ на время переключения, чтобы мотор не улетал на отсечку.
		if (ShiftTimer > 0.0 && (Assist.bAutoClutch || Assist.bAutoGearbox))
		{
			Throttle = IdleThrottle;
		}
		if (Rpm > EP.RevLimitRpm)
		{
			EngineSt.bRevLimiterCut = true;
		}
		else if (Rpm < EP.RevLimitRpm - 250.0)
		{
			EngineSt.bRevLimiterCut = false;
		}
		if (EngineSt.bRevLimiterCut)
		{
			Throttle = 0.0;
		}
		CombustionTorque = EngineModel::NetTorque(EP, Rpm, Throttle, EngCond) + EngineModel::FrictionTorque(EP, Rpm, EngCond);
	}
	if (!EngineSt.bRunning && Input.bStarter && Input.bIgnition)
	{
		CombustionTorque += EP.StarterTorque * std::max(0.0, 1.0 - Rpm / EP.StarterMaxRpm);
	}
	// Трение как диссипативный момент: у стоящего вала не крутит его назад.
	const double FrictionTorque = EngineModel::FrictionTorque(EP, Rpm, EngCond) * Clamp01(EngineSt.Omega / 5.0);
	const double EngineFreeTorque = CombustionTorque - FrictionTorque;
	EngineSt.EffectiveThrottle = Throttle;

	// --- Шины ---
	std::vector<double> TireTorque(Wheels.size(), 0.0);
	for (size_t I = 0; I < Wheels.size(); ++I)
	{
		WheelRuntime& W = Wheels[I];
		const WheelContact& C = Contacts[I];
		const double R = Cfg.Wheels[I].Tire.Radius;
		if (C.bInContact && W.Load > 0.0)
		{
			W.TireModel.IntegrateSlip(W.TireSt, H, C.Vx, C.Vy, W.Omega * R);
			W.TireModel.ComputeForces(W.TireSt, W.Load, C.Vx, C.Vy, W.Omega * R, C.Surface);
		}
		else
		{
			const double Decay = std::exp(-H * 30.0);
			W.TireSt.SlipLong *= Decay;
			W.TireSt.SlipLatTan *= Decay;
			W.TireSt.Fx = 0.0;
			W.TireSt.Fy = 0.0;
		}
		TireTorque[I] = -W.TireSt.Fx * R;
		W.DriveTorque = 0.0;
	}

	// --- Сцепление и трансмиссия ---
	const double G = TotalRatio();
	double ClutchTorque = 0.0;
	const double Ie = EP.Inertia;
	if (G != 0.0 && ClutchEngagement > 0.0)
	{
		double DrivenInertia = 0.0;
		double DrivenTireTorque = 0.0;
		for (size_t I = 0; I < Wheels.size(); ++I)
		{
			if (Cfg.Wheels[I].bDriven)
			{
				DrivenInertia += Cfg.Wheels[I].Inertia;
				DrivenTireTorque += TireTorque[I];
			}
		}
		const double Id = DrivenInertia / (G * G);
		const double OmegaIn = G * DrivenWheelsOmega();
		const double OmegaEPred = EngineSt.Omega + EngineFreeTorque / Ie * H;
		const double OmegaInPred = OmegaIn + (DrivenTireTorque / G) / Id * H;
		const double Capacity = Cfg.Clutch.MaxTorque * ClutchEngagement * (0.35 + 0.65 * Clamp01(Cond.Clutch));
		ClutchTorque = Clamp(LockTorque(OmegaEPred, OmegaInPred, Ie, Id, H), -Capacity, Capacity);
		const double SlipEnergy = std::abs(ClutchTorque * (EngineSt.Omega - OmegaIn)) * H;
		Cond.Clutch = std::max(0.0, Cond.Clutch - SlipEnergy * Cfg.Clutch.WearPerJoule);
	}
	EngineSt.Omega = std::max(0.0, EngineSt.Omega + (EngineFreeTorque - ClutchTorque) / Ie * H);
	EngineSt.Torque = EngineFreeTorque;
	ClutchTorqueLast = ClutchTorque;

	const double TotalAxleTorque = ClutchTorque * G * Cfg.Gearbox.Efficiency;
	AxleTorqueLast = TotalAxleTorque;
	if (!DrivenAxles.empty())
	{
		const double PerAxle = TotalAxleTorque / static_cast<double>(DrivenAxles.size());
		for (int Axle : DrivenAxles)
		{
			int Count = 0;
			for (const WheelConfig& WC : Cfg.Wheels)
			{
				Count += (WC.bDriven && WC.Axle == Axle) ? 1 : 0;
			}
			for (size_t I = 0; I < Wheels.size(); ++I)
			{
				if (Cfg.Wheels[I].bDriven && Cfg.Wheels[I].Axle == Axle)
				{
					Wheels[I].DriveTorque += PerAxle / Count;
				}
			}
		}
	}

	auto PredictOmega = [&](size_t I) {
		return Wheels[I].Omega + (Wheels[I].DriveTorque + TireTorque[I]) / Cfg.Wheels[I].Inertia * H;
	};

	// Межосевая связь (полный привод): жёстко заблокирована, как у раздатки «по-уазовски».
	if (DrivenAxles.size() > 1)
	{
		for (size_t K = 1; K < DrivenAxles.size(); ++K)
		{
			const int A0 = DrivenAxles[0];
			const int A1 = DrivenAxles[K];
			double Sum0 = 0.0, Sum1 = 0.0, I0 = 0.0, I1 = 0.0;
			int N0 = 0, N1 = 0;
			for (size_t I = 0; I < Wheels.size(); ++I)
			{
				if (!Cfg.Wheels[I].bDriven)
				{
					continue;
				}
				if (Cfg.Wheels[I].Axle == A0)
				{
					Sum0 += PredictOmega(I);
					I0 += Cfg.Wheels[I].Inertia;
					++N0;
				}
				else if (Cfg.Wheels[I].Axle == A1)
				{
					Sum1 += PredictOmega(I);
					I1 += Cfg.Wheels[I].Inertia;
					++N1;
				}
			}
			if (N0 == 0 || N1 == 0)
			{
				continue;
			}
			const double T = LockTorque(Sum0 / N0, Sum1 / N1, I0, I1, H);
			for (size_t I = 0; I < Wheels.size(); ++I)
			{
				if (Cfg.Wheels[I].bDriven && Cfg.Wheels[I].Axle == A0)
				{
					Wheels[I].DriveTorque -= T / N0;
				}
				else if (Cfg.Wheels[I].bDriven && Cfg.Wheels[I].Axle == A1)
				{
					Wheels[I].DriveTorque += T / N1;
				}
			}
		}
	}

	// Межколёсный дифференциал на каждой ведущей оси.
	for (int Axle : DrivenAxles)
	{
		int Left = -1;
		int Right = -1;
		for (size_t I = 0; I < Wheels.size(); ++I)
		{
			if (Cfg.Wheels[I].bDriven && Cfg.Wheels[I].Axle == Axle)
			{
				(Cfg.Wheels[I].Side < 0 ? Left : Right) = static_cast<int>(I);
			}
		}
		if (Left < 0 || Right < 0 || Cfg.Diff.Type == DiffType::Open)
		{
			continue;
		}
		const double Cap = Cfg.Diff.Type == DiffType::Locked
			? kInfiniteTorque
			: Cfg.Diff.LsdPreload + Cfg.Diff.LsdLockRatio * std::abs(TotalAxleTorque);
		const double T = Clamp(LockTorque(PredictOmega(static_cast<size_t>(Left)), PredictOmega(static_cast<size_t>(Right)),
			Cfg.Wheels[static_cast<size_t>(Left)].Inertia, Cfg.Wheels[static_cast<size_t>(Right)].Inertia, H), -Cap, Cap);
		Wheels[static_cast<size_t>(Left)].DriveTorque -= T;
		Wheels[static_cast<size_t>(Right)].DriveTorque += T;
	}

	// --- Интегрирование колёс, тормоза, сопротивление качению ---
	for (size_t I = 0; I < Wheels.size(); ++I)
	{
		WheelRuntime& W = Wheels[I];
		const WheelConfig& WC = Cfg.Wheels[I];
		const WheelContact& C = Contacts[I];
		W.Omega += (W.DriveTorque + TireTorque[I]) / WC.Inertia * H;

		const double PadFactor = Cond.Wheels[I].PadMm > 0.5 ? 1.0 : 0.35; // колодки стёрты — железо по железу
		const double Fade = 1.0 - (1.0 - WC.Brake.FadeMinFactor) * SmoothStep(WC.Brake.FadeStartC, WC.Brake.FadeEndC, W.BrakeTempC);
		double BrakeTorque = Clamp01(Input.Brake) * WC.Brake.MaxTorque * PadFactor * Fade;
		if (Assist.bAbs && BrakeTorque > 0.0 && std::abs(C.Vx) > 3.0
			&& W.TireSt.SlipLong * SignOf(C.Vx) < -1.2 * W.TireModel.PeakSlipLong())
		{
			BrakeTorque *= 0.25;
		}
		if (WC.bHandbrake)
		{
			BrakeTorque += Clamp01(Input.Handbrake) * WC.Brake.HandbrakeTorque;
		}
		const double Rolling = (C.bInContact && W.Load > 0.0) ? W.TireModel.RollingResistanceTorque(W.Load, C.Surface) : 0.0;
		const double Dissipative = BrakeTorque + Rolling + kBearingTorque;
		const double DOmega = Dissipative / WC.Inertia * H;

		const double PadEnergy = BrakeTorque * std::abs(W.Omega) * H;
		Cond.Wheels[I].PadMm = std::max(0.0, Cond.Wheels[I].PadMm - PadEnergy * WC.Brake.PadWearMmPerJoule);
		const double Cooling = (WC.Brake.CoolingBase + WC.Brake.CoolingPerSpeed * std::abs(C.Vx)) * (W.BrakeTempC - AmbientLast);
		W.BrakeTempC += (PadEnergy - Cooling * H) / WC.Brake.ThermalMass;

		// Тормоз и трение не могут развернуть колесо — только остановить (так колесо честно блокируется).
		W.Omega = std::abs(W.Omega) <= DOmega ? 0.0 : W.Omega - SignOf(W.Omega) * DOmega;
		W.SpinAngle = std::fmod(W.SpinAngle + W.Omega * H, 2.0 * kPi);

		if (C.bInContact)
		{
			W.TireModel.ApplyWear(W.TireSt, H, C.Vx, C.Vy, W.Omega * WC.Tire.Radius);
		}
		W.FxAccum += W.TireSt.Fx * H;
		W.FyAccum += W.TireSt.Fy * H;
	}

	// --- Топливо ---
	double FuelKgPerS = 0.0;
	if (EngineSt.bRunning)
	{
		const double Pind = EngineModel::IndicatedPower(EP, Rpm, Throttle, EngCond);
		FuelKgPerS = std::max(Pind * EP.IndicatedSfc / 3.6e9, kIdleMinFuelKgPerS);
		Cond.FuelL -= FuelKgPerS / kFuelDensityKgPerL * H;
		if (Cond.FuelL <= 0.0)
		{
			Cond.FuelL = 0.0;
			EngineSt.bRunning = false; // кончился бензин
		}
	}
	FuelFlowKgPerS += FuelKgPerS * H; // нормируется в Step

	// --- Пуск и остановка ---
	if (EngineSt.bRunning && (!Input.bIgnition || EngineSt.Rpm() < EP.StallRpm))
	{
		EngineSt.bRunning = false;
	}
	if (!EngineSt.bRunning && Input.bStarter && Input.bIgnition && Cond.FuelL > 0.0 && Cond.Engine > 0.02
		&& EngineSt.Rpm() > EP.StartRpm)
	{
		EngineSt.bRunning = true;
	}
}

void Vehicle::UpdateThermalAndWear(double Dt, double ForwardSpeed, double AmbientC, const std::vector<WheelContact>& Contacts)
{
	const EngineParams& EP = Cfg.Engine;
	double& T = EngineSt.TemperatureC;

	if (Input.bIgnition && T > EP.FanOnC)
	{
		EngineSt.bFanOn = true;
	}
	else if (!Input.bIgnition || T < EP.FanOffC)
	{
		EngineSt.bFanOn = false;
	}

	const double HeatIn = EngineSt.bRunning ? EP.HeatToCoolantFraction * FuelFlowKgPerS * kFuelLowerHeatingValue : 0.0;
	const double Airflow = 0.25 + std::abs(ForwardSpeed) / 15.0 + (EngineSt.bFanOn ? 1.0 : 0.0);
	const double Thermostat = SmoothStep(EP.ThermostatOpenC, EP.ThermostatFullC, T);
	const double Conductance = EP.RadiatorConductance * Clamp01(Cond.Radiator) * Airflow * (0.05 + 0.95 * Thermostat) + 25.0;
	T += (HeatIn - Conductance * (T - AmbientC)) / EP.ThermalMass * Dt;

	if (EngineSt.bRunning)
	{
		const double Rpm = EngineSt.Rpm();
		double Wear = EngineSt.Omega * Dt / (2.0 * kPi) * EP.WearPerRev * (1.0 + 1.5 * EngineSt.EffectiveThrottle);
		if (Rpm > EP.RedlineRpm)
		{
			Wear += EP.OverRevWearPerSec * std::min(3.0, (Rpm - EP.RedlineRpm) / (EP.RevLimitRpm - EP.RedlineRpm)) * Dt;
		}
		if (T > EP.OverheatC)
		{
			Wear += EP.OverheatWearPerSec * (T - EP.OverheatC) / 10.0 * Dt;
		}
		Cond.Engine = std::max(0.0, Cond.Engine - Wear);
		if (Cond.Engine <= 0.02)
		{
			EngineSt.bRunning = false; // мотор заклинило
		}
	}

	for (size_t I = 0; I < Wheels.size(); ++I)
	{
		const WheelContact& C = Contacts[I];
		if (!C.bInContact)
		{
			continue;
		}
		const double Travel = std::abs(C.CompressionVelocity) * Dt;
		Cond.Wheels[I].Shock = std::max(0.0,
			Cond.Wheels[I].Shock - Travel * Cfg.Wheels[I].Suspension.ShockWearPerMeter * (1.0 + 2.0 * C.Surface.Roughness));
	}
}

void Vehicle::Step(double Dt, const std::vector<WheelContact>& Contacts, double ForwardSpeed, double AmbientC)
{
	if (Dt <= 0.0 || Contacts.size() != Wheels.size())
	{
		return;
	}
	LastSpeed = ForwardSpeed;
	AmbientLast = AmbientC;
	for (size_t I = 0; I < Wheels.size(); ++I)
	{
		Wheels[I].TireSt.TreadMm = Cond.Wheels[I].TreadMm;
		Wheels[I].FxAccum = 0.0;
		Wheels[I].FyAccum = 0.0;
	}
	FuelFlowKgPerS = 0.0;

	UpdateSteering(Dt, ForwardSpeed);
	UpdateAutoGearbox(Dt);
	UpdateClutchEngagement(Dt);
	UpdateSuspension(Contacts);

	const int Steps = std::max(1, static_cast<int>(std::ceil(Dt / MaxSubstep - 1e-9)));
	const double H = Dt / Steps;
	for (int S = 0; S < Steps; ++S)
	{
		Substep(H, Contacts);
	}
	FuelFlowKgPerS /= Dt;
	EngineSt.FuelFlowLPerH = FuelFlowKgPerS / kFuelDensityKgPerL * 3600.0;

	UpdateThermalAndWear(Dt, ForwardSpeed, AmbientC, Contacts);
	GrindTimer = std::max(0.0, GrindTimer - Dt);
	OdometerM += std::abs(ForwardSpeed) * Dt;

	for (size_t I = 0; I < Wheels.size(); ++I)
	{
		WheelRuntime& W = Wheels[I];
		const WheelContact& C = Contacts[I];
		Cond.Wheels[I].TreadMm = W.TireSt.TreadMm;
		WheelOutput& O = W.Out;
		O.bInContact = C.bInContact && W.Load > 0.0;
		O.SuspensionForce = W.SuspensionForce;
		O.Load = W.Load;
		O.Fx = W.FxAccum / Dt;
		O.Fy = W.FyAccum / Dt;
		O.SteerAngle = W.SteerAngle;
		O.Omega = W.Omega;
		O.SpinAngle = W.SpinAngle;
		O.Compression = C.bInContact ? C.Compression : 0.0;
		O.SlipRatio = W.TireSt.SlipLong;
		O.SlipAngle = std::atan(W.TireSt.SlipLatTan);
		O.bSliding = W.TireSt.bSliding;
		O.BrakeTempC = W.BrakeTempC;
	}
}

Telemetry Vehicle::GetTelemetry() const
{
	Telemetry T;
	T.Rpm = EngineSt.Rpm();
	T.Gear = Gear;
	T.SpeedKmh = LastSpeed * 3.6;
	T.ClutchEngagement = ClutchEngagement;
	T.EffectiveThrottle = EngineSt.EffectiveThrottle;
	T.FuelL = Cond.FuelL;
	T.FuelFlowLPerH = EngineSt.FuelFlowLPerH;
	T.InstantLPer100Km = std::abs(T.SpeedKmh) > 3.0 ? EngineSt.FuelFlowLPerH / std::abs(T.SpeedKmh) * 100.0 : 0.0;
	T.EngineTempC = EngineSt.TemperatureC;
	T.EngineTorque = EngineSt.Torque;
	T.OdometerKm = OdometerM / 1000.0;
	T.bEngineRunning = EngineSt.bRunning;
	T.bFanOn = EngineSt.bFanOn;
	T.bShifting = ShiftTimer > 0.0;
	T.bGearGrind = GrindTimer > 0.0;
	T.ClutchTorque = ClutchTorqueLast;
	T.SteeringWheelDeg = SteerState * Cfg.Steering.MaxAngleDeg * Cfg.Steering.SteeringRatio;
	return T;
}
} // namespace vsim
