// Peregon — VehicleSimCore
// Силовой агрегат: двигатель, сцепление, коробка, главная передача, дифференциал, топливо, тепло.
#pragma once

#include "vsim/Math.h"

#include <string>
#include <vector>

namespace vsim
{
struct EngineParams
{
	// Внешняя скоростная характеристика: момент на валу при полном газе (Н·м) от оборотов.
	Curve FullLoadTorque;
	// Момент механических потерь: F0 + F1 * k + F2 * k^2, где k = rpm / 1000. Тормозит двигатель при сброшенном газе.
	double Friction0 = 8.0;
	double Friction1 = 2.2;
	double Friction2 = 0.35;

	double Inertia = 0.18;       // кг·м², коленвал + маховик
	double IdleRpm = 850.0;
	double StallRpm = 350.0;
	double RedlineRpm = 6000.0;  // выше — ускоренный износ
	double RevLimitRpm = 6400.0; // отсечка
	double StarterTorque = 45.0;
	double StarterMaxRpm = 320.0;
	double StartRpm = 250.0;      // при каких оборотах «схватывает»

	// Удельный индикаторный расход топлива, г/кВт·ч. Расход считается по индикаторной мощности,
	// поэтому на холостом и при малой нагрузке удельный расход на «полезный» кВт растёт сам.
	double IndicatedSfc = 255.0;

	// Тепло.
	double ThermalMass = 45000.0;      // Дж/К, блок + антифриз
	double HeatToCoolantFraction = 0.30;
	double RadiatorConductance = 800.0; // Вт/К при «единичном» обдуве
	double ThermostatOpenC = 82.0;
	double ThermostatFullC = 95.0;
	double FanOnC = 100.0;
	double FanOffC = 94.0;
	double OverheatC = 112.0;          // выше — быстрый износ

	// Износ (ускорен ради игрового процесса).
	double WearPerRev = 1.2e-8;
	double OverRevWearPerSec = 4.0e-3;
	double OverheatWearPerSec = 2.5e-3;
};

struct ClutchParams
{
	double MaxTorque = 200.0;       // Н·м, ёмкость нового сцепления
	double WearPerJoule = 1.0e-7;   // износ диска от энергии проскальзывания
};

struct GearboxParams
{
	std::vector<double> Ratios{3.667, 2.100, 1.361, 1.000, 0.820};
	double ReverseRatio = 3.530;
	double FinalDrive = 4.1;
	double Efficiency = 0.92;  // КПП + главная пара
	double ShiftTime = 0.35;   // с, время переключения с автосцеплением
};

enum class DiffType
{
	Open,
	Locked,
	LimitedSlip
};

struct DiffParams
{
	DiffType Type = DiffType::Open;
	double LsdPreload = 40.0;    // Н·м
	double LsdLockRatio = 0.3;   // доля входного момента, которую можно перебросить на одно колесо
};

struct EngineState
{
	double Omega = 0.0;  // рад/с
	bool bRunning = false;
	bool bRevLimiterCut = false;
	bool bFanOn = false;
	double TemperatureC = 20.0;
	double Torque = 0.0;          // нетто-момент на маховике последнего шага
	double FuelFlowLPerH = 0.0;
	double Condition = 1.0;       // 0..1
	double EffectiveThrottle = 0.0;

	double Rpm() const { return RadSToRpm(Omega); }
};

// Функции двигателя без собственного состояния — удобно тестировать.
namespace EngineModel
{
double FrictionTorque(const EngineParams& P, double Rpm, double Condition);
// Нетто-момент на маховике при данной дроссельной заслонке (0..1).
double NetTorque(const EngineParams& P, double Rpm, double Throttle, double Condition);
// Индикаторная мощность (Вт) — для расчёта расхода.
double IndicatedPower(const EngineParams& P, double Rpm, double Throttle, double Condition);
// Заслонка, нужная, чтобы на холостых удержать IdleRpm (подаётся к P-регулятору как смещение).
double IdleThrottleBias(const EngineParams& P);
} // namespace EngineModel
} // namespace vsim
