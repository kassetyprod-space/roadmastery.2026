// Peregon — VehicleSimCore
// Модель шины: «магическая формула» Пасейки + длина релаксации.
//
// Почему релаксация: классический slip ratio (wr - v) / |v| делится на ноль у стоящей машины
// и даёт очень «жёсткую» систему, которая взрывается при шаге 1/60 с. Вместо этого храним
// деформацию пятна контакта как состояние и интегрируем её (как в реальной шине):
//     dS/dt = (Vslip - |Vx| * S) / L
// На скорости это сходится к обычному slip ratio, а на месте работает как пружина —
// машина стоит на уклоне на ручнике, не «ползёт» и не дрожит.
#pragma once

#include "vsim/Math.h"

namespace vsim
{
// Нормированная кривая Пасейки, D = 1. Аргумент — проскальзывание (безразмерное или радианы).
struct MagicFormula
{
	double B = 10.0;
	double C = 1.65;
	double E = 0.5;

	double Eval(double Slip) const
	{
		const double BS = B * Slip;
		return std::sin(C * std::atan(BS - E * (BS - std::atan(BS))));
	}

	// Проскальзывание, при котором сила максимальна (численный поиск, вызывается при инициализации).
	double FindPeakSlip() const;
};

// Покрытие под колесом. Задаётся физическим материалом в Unreal.
struct SurfaceInfo
{
	double Grip = 1.0;            // множитель сцепления (асфальт 1.0, лёд ~0.1)
	double RollingResistance = 1.0; // множитель сопротивления качению (песок/грязь > 1)
	double TreadSensitivity = 0.0; // насколько важен протектор (0 — сухой асфальт, 1 — снег/грязь)
	double Roughness = 0.0;       // 0..1, неровность (износ амортизаторов, тряска)
};

struct TireParams
{
	double Radius = 0.285;            // м
	double Width = 0.175;             // м
	double MuLong = 0.95;             // пиковый коэффициент сцепления, продольный
	double MuLat = 0.90;              // пиковый коэффициент сцепления, боковой
	double NominalLoad = 2700.0;      // Н, нагрузка, для которой заданы Mu
	double LoadSensitivity = 0.08;    // падение Mu при росте нагрузки
	MagicFormula Long{14.0, 1.65, 0.5};
	MagicFormula Lat{12.5, 1.35, -0.4};
	double RelaxationLong = 0.12;     // м
	double RelaxationLat = 0.35;      // м
	double RollingResistanceCoeff = 0.013;
	double TreadDepthNewMm = 8.0;
	double WearMmPerJoule = 5.0e-7;   // ускорено относительно реальности, чтобы износ был заметен за перегон
	double LowSpeedDamping = 0.5;     // доля критического демпфирования пятна на малой скорости
};

struct TireState
{
	double SlipLong = 0.0;  // состояние деформации, ~ slip ratio
	double SlipLatTan = 0.0; // ~ tan(угол увода)
	double TreadMm = 8.0;
	double Fx = 0.0;
	double Fy = 0.0;
	bool bSliding = false;
};

class Tire
{
public:
	void Init(const TireParams& InParams);

	// Интегрировать деформацию за шаг Dt.
	// Vx, Vy — скорость пятна контакта в системе колеса (x — по направлению колеса, y — вправо).
	// SurfaceSpeed = omega * R — окружная скорость колеса.
	void IntegrateSlip(TireState& State, double Dt, double Vx, double Vy, double SurfaceSpeed) const;

	// Посчитать силы в пятне (результат пишется в State.Fx/Fy). Fz — нормальная нагрузка, Н.
	void ComputeForces(TireState& State, double Fz, double Vx, double Vy, double SurfaceSpeed, const SurfaceInfo& Surface) const;

	// Износ протектора по мощности проскальзывания.
	void ApplyWear(TireState& State, double Dt, double Vx, double Vy, double SurfaceSpeed) const;

	double GripMultiplier(const TireState& State, const SurfaceInfo& Surface) const;
	double RollingResistanceTorque(double Fz, const SurfaceInfo& Surface) const;

	const TireParams& Params() const { return P; }
	double PeakSlipLong() const { return PeakLong; }
	double PeakSlipAngle() const { return PeakLat; }

private:
	TireParams P;
	double PeakLong = 0.1;
	double PeakLat = 0.12;
};
} // namespace vsim
