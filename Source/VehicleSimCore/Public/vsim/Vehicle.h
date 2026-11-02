// Peregon — VehicleSimCore
// Машина целиком: колёса, подвеска, тормоза, рулевое, трансмиссия, износ.
//
// Разделение труда с движком (Unreal):
//   Движок: твёрдое тело кузова, рейкасты колёс, приложение сил, визуал.
//   Ядро:   всё, что «внутри» машины. На вход — контакт каждого колеса и скорость пятна,
//           на выход — сила подвески и силы шины. Вращение колёс и мотора считается здесь
//           с внутренним шагом 1/600 с, поэтому физика не зависит от FPS.
//
// Система координат шасси: X — вперёд, Y — вправо, Z — вверх (как в Unreal). Положительный руль — вправо.
#pragma once

#include "vsim/Powertrain.h"
#include "vsim/Tire.h"

#include <string>
#include <vector>

namespace vsim
{
struct SuspensionParams
{
	double Travel = 0.18;            // м, полный ход
	double SpringRate = 19000.0;     // Н/м
	double Preload = 900.0;          // Н, усилие пружины при полном отбое
	double BumpDamping = 1400.0;     // Н·с/м, сжатие
	double ReboundDamping = 2500.0;  // Н·с/м, отбой
	double BumpStopRange = 0.025;    // м до конца хода, где включается отбойник
	double BumpStopRate = 2.4e7;     // Н/м² (квадратичный отбойник: ~15 кН на последних 2.5 см)
	double ShockWearPerMeter = 2.0e-5; // износ амортизатора на метр хода штока
};

struct BrakeParams
{
	double MaxTorque = 1300.0;       // Н·м при полном нажатии педали
	double HandbrakeTorque = 0.0;    // Н·м
	double PadNewMm = 10.0;
	double PadWearMmPerJoule = 1.0e-7;
	// Нагрев и «ватность» тормозов: после серии торможений с горы педаль слабеет.
	double ThermalMass = 2500.0;     // Дж/К (диск/барабан + суппорт)
	double CoolingBase = 3.0;        // Вт/К на месте
	double CoolingPerSpeed = 1.0;    // Вт/К на каждый м/с
	double FadeStartC = 300.0;
	double FadeEndC = 550.0;
	double FadeMinFactor = 0.4;      // доля эффективности на полностью перегретых тормозах
};

struct WheelConfig
{
	std::string Name;
	// Центр колеса при полном отбое, м, относительно начала координат шасси (центра масс).
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	int Axle = 0;   // 0 — передняя ось
	int Side = 0;   // -1 левое, +1 правое
	bool bSteered = false;
	bool bDriven = false;
	bool bHandbrake = false;
	double Inertia = 1.0; // кг·м², колесо + ступица + тормоз
	TireParams Tire;
	SuspensionParams Suspension;
	BrakeParams Brake;
};

struct SteeringParams
{
	double MaxAngleDeg = 34.0;
	double InputRate = 4.0;          // 1/с — скорость поворота руля к цели (клавиатура)
	double ReturnRate = 6.0;         // 1/с — возврат к центру
	double HighSpeedFraction = 0.35; // доля угла на высокой скорости (если включён ассист)
	double SteeringRatio = 16.0;     // поворот руля / поворот колёс (~1.5 оборота от центра до упора)
};

struct AeroParams
{
	double DragCoefficient = 0.45;
	double FrontalArea = 1.9;
	double AirDensity = 1.225;
};

struct VehicleConfig
{
	std::string Name;
	double Mass = 1100.0;     // кг, с водителем
	double CgHeight = 0.52;   // м над землёй
	double YawInertia = 1650.0;
	std::vector<WheelConfig> Wheels;
	std::vector<double> AntiRollRate; // Н/м по осям (разность ходов левого и правого колеса)
	EngineParams Engine;
	ClutchParams Clutch;
	GearboxParams Gearbox;
	DiffParams Diff;
	SteeringParams Steering;
	AeroParams Aero;
	double FuelCapacityL = 39.0;
};

struct DriverInput
{
	double Throttle = 0.0;  // 0..1
	double Brake = 0.0;     // 0..1
	double Clutch = 0.0;    // 0..1, 1 — педаль выжата (сцепление разомкнуто)
	double Steer = 0.0;     // -1..1
	double Handbrake = 0.0; // 0..1
	bool bIgnition = true;
	bool bStarter = false;
};

struct Assists
{
	bool bAutoClutch = true;
	bool bAutoGearbox = false;
	bool bSpeedSensitiveSteering = true;
	bool bAbs = false;
};

// Контакт колеса за кадр (заполняет движок по результату трассировки).
struct WheelContact
{
	bool bInContact = false;
	double Compression = 0.0;          // м от полного отбоя (может быть > Travel — пробой)
	double CompressionVelocity = 0.0;  // м/с, > 0 — сжатие
	double Vx = 0.0;                   // м/с, скорость пятна вдоль колеса
	double Vy = 0.0;                   // м/с, поперёк (вправо)
	SurfaceInfo Surface;
};

struct WheelOutput
{
	double SuspensionForce = 0.0; // Н вдоль оси Z шасси (приложить в точке крепления)
	double Fx = 0.0;              // Н, вдоль колеса (в плоскости дороги)
	double Fy = 0.0;              // Н, поперёк колеса
	double Load = 0.0;            // Н, нормальная нагрузка на шину
	double SteerAngle = 0.0;      // рад
	double Omega = 0.0;           // рад/с
	double SpinAngle = 0.0;       // рад, для визуала
	double Compression = 0.0;     // м
	double SlipRatio = 0.0;
	double SlipAngle = 0.0;       // рад
	bool bInContact = false;
	bool bSliding = false;
	double BrakeTempC = 20.0;
};

// Всё, что изнашивается и чинится. Сохраняется между сессиями.
struct WheelCondition
{
	double TreadMm = 8.0;
	double PadMm = 10.0;
	double Shock = 1.0; // 0..1
};

struct VehicleCondition
{
	double Engine = 1.0;
	double Clutch = 1.0;
	double Gearbox = 1.0;
	double Radiator = 1.0;
	double FuelL = 30.0;
	std::vector<WheelCondition> Wheels;
};

struct Telemetry
{
	double Rpm = 0.0;
	int Gear = 0;
	double SpeedKmh = 0.0;
	double ClutchEngagement = 0.0;
	double EffectiveThrottle = 0.0;
	double FuelL = 0.0;
	double FuelFlowLPerH = 0.0;
	double InstantLPer100Km = 0.0;
	double EngineTempC = 0.0;
	double EngineTorque = 0.0;
	double OdometerKm = 0.0;
	bool bEngineRunning = false;
	bool bFanOn = false;
	bool bShifting = false;
	bool bGearGrind = false; // последняя попытка переключения не удалась (не выжато сцепление)
	double ClutchTorque = 0.0;        // Н·м через сцепление — реакция мотора качает кузов
	double SteeringWheelDeg = 0.0;    // угол рулевого колеса для визуала
};

class VSIM_API Vehicle
{
public:
	static constexpr double MaxSubstep = 1.0 / 600.0;

	explicit Vehicle(const VehicleConfig& InConfig);

	void SetInput(const DriverInput& In) { Input = In; }
	void SetAssists(const Assists& In) { Assist = In; }
	const Assists& GetAssists() const { return Assist; }

	bool ShiftUp();
	bool ShiftDown();
	bool RequestGear(int NewGear); // -1 — задняя, 0 — нейтраль, 1..N
	int GetGear() const { return Gear; }
	int NumForwardGears() const { return static_cast<int>(Cfg.Gearbox.Ratios.size()); }

	// Основной шаг. Contacts.size() == NumWheels(). ForwardSpeed — продольная скорость кузова, м/с.
	void Step(double Dt, const std::vector<WheelContact>& Contacts, double ForwardSpeed, double AmbientC = 20.0);

	// Аэродинамическое сопротивление (модуль, Н) — движок прикладывает против скорости к центру масс.
	double AeroDragForce(double Speed) const;

	size_t NumWheels() const { return Wheels.size(); }
	const WheelOutput& GetWheel(size_t I) const { return Wheels[I].Out; }
	const WheelConfig& GetWheelConfig(size_t I) const { return Cfg.Wheels[I]; }
	double SteerAngle(size_t I) const { return Wheels[I].SteerAngle; }

	const VehicleConfig& Config() const { return Cfg; }
	VehicleCondition& Condition() { return Cond; }
	const VehicleCondition& Condition() const { return Cond; }
	Telemetry GetTelemetry() const;
	const EngineState& Engine() const { return EngineSt; }

	// Отладка/тесты: принудительно запустить мотор.
	void ForceEngineRunning(double Rpm);

private:
	struct WheelRuntime
	{
		Tire TireModel;
		TireState TireSt;
		double Omega = 0.0;
		double SpinAngle = 0.0;
		double SteerAngle = 0.0;
		double Load = 0.0;
		double SuspensionForce = 0.0;
		double FxAccum = 0.0;
		double FyAccum = 0.0;
		double DriveTorque = 0.0;
		double BrakeTempC = 20.0;
		WheelOutput Out;
	};

	void UpdateSteering(double Dt, double ForwardSpeed);
	void UpdateAutoGearbox(double Dt);
	void UpdateClutchEngagement(double Dt);
	void UpdateSuspension(const std::vector<WheelContact>& Contacts);
	void Substep(double H, const std::vector<WheelContact>& Contacts);
	void UpdateThermalAndWear(double Dt, double ForwardSpeed, double AmbientC, const std::vector<WheelContact>& Contacts);
	double TotalRatio() const;
	double DrivenWheelsOmega() const;

	VehicleConfig Cfg;
	Assists Assist;
	DriverInput Input;
	VehicleCondition Cond;
	EngineState EngineSt;
	std::vector<WheelRuntime> Wheels;
	std::vector<int> DrivenAxles;

	int Gear = 0;
	double SteerState = 0.0;
	double ClutchEngagement = 0.0;
	double ShiftTimer = 0.0;
	double ShiftCooldown = 0.0;
	double GrindTimer = 0.0;
	double IdleBias = 0.1;
	double RearAxleX = 0.0;
	double OdometerM = 0.0;
	double LastSpeed = 0.0;
	double ThrottleState = 0.0;   // заслонка с инерцией впуска
	double ClutchTorqueLast = 0.0;
	double AmbientLast = 20.0;
	double FuelFlowKgPerS = 0.0;
	double AxleTorqueLast = 0.0;
};
} // namespace vsim
