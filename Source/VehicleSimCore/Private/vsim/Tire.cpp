#include "vsim/Tire.h"

namespace vsim
{
double MagicFormula::FindPeakSlip() const
{
	// Грубый проход + уточнение. Достаточно точно для нормировки комбинированного скольжения.
	double BestX = 0.01;
	double BestY = -1.0;
	for (int I = 1; I <= 400; ++I)
	{
		const double X = I * 0.0025; // 0..1
		const double Y = Eval(X);
		if (Y > BestY)
		{
			BestY = Y;
			BestX = X;
		}
	}
	double Lo = BestX - 0.0025;
	double Hi = BestX + 0.0025;
	for (int I = 0; I < 40; ++I)
	{
		const double M1 = Lo + (Hi - Lo) / 3.0;
		const double M2 = Hi - (Hi - Lo) / 3.0;
		if (Eval(M1) < Eval(M2))
		{
			Lo = M1;
		}
		else
		{
			Hi = M2;
		}
	}
	return 0.5 * (Lo + Hi);
}

void Tire::Init(const TireParams& InParams)
{
	P = InParams;
	PeakLong = P.Long.FindPeakSlip();
	PeakLat = P.Lat.FindPeakSlip();
}

void Tire::IntegrateSlip(TireState& State, double Dt, double Vx, double Vy, double SurfaceSpeed) const
{
	const double VAbs = std::abs(Vx);

	// Точное решение линейного ОДУ на шаге: безусловно устойчиво на любой скорости.
	auto Relax = [Dt, VAbs](double S, double Drive, double L) {
		const double A = VAbs * Dt / L;
		const double Decay = std::exp(-A);
		const double Gain = A > 1e-9 ? (1.0 - Decay) / VAbs : Dt / L;
		return S * Decay + Drive * Gain;
	};

	State.SlipLong = Clamp(Relax(State.SlipLong, SurfaceSpeed - Vx, P.RelaxationLong), -20.0, 20.0);
	State.SlipLatTan = Clamp(Relax(State.SlipLatTan, -Vy, P.RelaxationLat), -10.0, 10.0);
}

double Tire::GripMultiplier(const TireState& State, const SurfaceInfo& Surface) const
{
	const double TreadFrac = Clamp01(State.TreadMm / P.TreadDepthNewMm);
	const double DryFactor = 0.92 + 0.08 * TreadFrac;  // лысая шина на сухом асфальте почти не хуже
	const double LooseFactor = 0.45 + 0.55 * TreadFrac; // а на снегу и в грязи — сильно хуже
	return Surface.Grip * Lerp(DryFactor, LooseFactor, Clamp01(Surface.TreadSensitivity));
}

double Tire::RollingResistanceTorque(double Fz, const SurfaceInfo& Surface) const
{
	return P.RollingResistanceCoeff * Surface.RollingResistance * Fz * P.Radius;
}

void Tire::ComputeForces(TireState& State, double Fz, double Vx, double Vy, double SurfaceSpeed, const SurfaceInfo& Surface) const
{
	State.Fx = 0.0;
	State.Fy = 0.0;
	State.bSliding = false;
	if (Fz <= 0.0)
	{
		return;
	}

	const double Grip = GripMultiplier(State, Surface);
	const double LoadFactor = std::max(0.3, 1.0 - P.LoadSensitivity * (Fz / P.NominalLoad - 1.0));
	const double MaxFx = P.MuLong * Grip * LoadFactor * Fz;
	const double MaxFy = P.MuLat * Grip * LoadFactor * Fz;

	// Комбинированное скольжение: нормируем оба скольжения на их пики и раскладываем общую силу
	// по направлению суммарного скольжения. Даёт честный «эллипс трения»: газ в повороте режет боковую силу.
	const double Sx = State.SlipLong / PeakLong;
	const double Sy = std::atan(State.SlipLatTan) / PeakLat;
	const double Rho = std::sqrt(Sx * Sx + Sy * Sy);
	double Fx = 0.0;
	double Fy = 0.0;
	if (Rho > 1e-9)
	{
		Fx = MaxFx * P.Long.Eval(Rho * PeakLong) * (Sx / Rho);
		Fy = MaxFy * P.Lat.Eval(Rho * PeakLat) * (Sy / Rho);
	}
	State.bSliding = Rho > 1.15;

	// На малой скорости пятно — пружина без демпфирования. Добавляем вязкое трение, иначе машина
	// на месте будет «качаться». Коэффициент — доля критического для массы, которая давит на колесо.
	const double VAbs = std::abs(Vx);
	const double W = 1.0 - SmoothStep(0.5, 3.0, VAbs);
	if (W > 0.0)
	{
		const double CornerMass = Fz / kGravity;
		const double Kx = MaxFx * P.Long.B * P.Long.C / P.RelaxationLong;
		const double Ky = MaxFy * P.Lat.B * P.Lat.C / P.RelaxationLat;
		const double Cx = P.LowSpeedDamping * 2.0 * std::sqrt(Kx * CornerMass);
		const double Cy = P.LowSpeedDamping * 2.0 * std::sqrt(Ky * CornerMass);
		Fx += W * Cx * (SurfaceSpeed - Vx);
		Fy += W * Cy * (-Vy);
	}

	// Эллипс трения — жёсткий предел.
	const double E = Sq(Fx / MaxFx) + Sq(Fy / MaxFy);
	if (E > 1.0)
	{
		const double K = 1.0 / std::sqrt(E);
		Fx *= K;
		Fy *= K;
	}

	State.Fx = Fx;
	State.Fy = Fy;
}

void Tire::ApplyWear(TireState& State, double Dt, double Vx, double Vy, double SurfaceSpeed) const
{
	const double SlipPower = std::abs(State.Fx * (SurfaceSpeed - Vx)) + std::abs(State.Fy * Vy);
	constexpr double BaseWearMmPerMeter = 8.0e-7;
	const double Wear = SlipPower * Dt * P.WearMmPerJoule + std::abs(Vx) * Dt * BaseWearMmPerMeter;
	State.TreadMm = std::max(0.0, State.TreadMm - Wear);
}
} // namespace vsim
