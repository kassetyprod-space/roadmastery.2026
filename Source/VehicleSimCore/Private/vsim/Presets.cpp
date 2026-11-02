#include "vsim/Presets.h"

namespace vsim
{
namespace Presets
{
VehicleConfig Sokol1500()
{
	VehicleConfig C;
	C.Name = "Sokol-1500";
	C.Mass = 1100.0;
	C.CgHeight = 0.52;
	C.YawInertia = 1650.0;
	C.FuelCapacityL = 39.0;

	EngineParams& E = C.Engine;
	E.FullLoadTorque = Curve{
		{0.0, 55.0}, {800.0, 70.0}, {1500.0, 85.0}, {2000.0, 93.0}, {2500.0, 99.0}, {3000.0, 103.0},
		{3400.0, 104.0}, {4000.0, 102.0}, {4500.0, 99.0}, {5000.0, 94.0}, {5600.0, 89.0}, {6000.0, 84.0},
		{6500.0, 74.0}, {7000.0, 60.0}};
	E.IdleRpm = 850.0;
	E.RedlineRpm = 6000.0;
	E.RevLimitRpm = 6400.0;
	E.Inertia = 0.18;

	C.Clutch.MaxTorque = 200.0;
	C.Gearbox.Ratios = {3.667, 2.100, 1.361, 1.000, 0.820};
	C.Gearbox.ReverseRatio = 3.530;
	C.Gearbox.FinalDrive = 4.1;
	C.Diff.Type = DiffType::Open;

	C.Aero.DragCoefficient = 0.45;
	C.Aero.FrontalArea = 1.9;

	TireParams Tire; // 175/70 R13
	Tire.Radius = 0.285;
	Tire.Width = 0.175;

	// Геометрия: развесовка 52/48, база 2.424 м.
	const double FrontX = 1.164;
	const double RearX = -1.260;
	const double FrontHalfTrack = 0.6825;
	const double RearHalfTrack = 0.6605;
	const double DroopZ = -0.335; // центр колеса при полном отбое; в покое ~ -0.235 (ЦМ на 0.52 м над землёй)

	SuspensionParams Front;
	Front.Travel = 0.18;
	Front.SpringRate = 19100.0;
	Front.Preload = 895.0; // ~10 см статического хода
	Front.BumpDamping = 1400.0;
	Front.ReboundDamping = 2500.0;

	SuspensionParams Rear = Front; // неразрезной мост на пружинах
	Rear.SpringRate = 20400.0;
	Rear.Preload = 550.0;
	Rear.BumpDamping = 1500.0;
	Rear.ReboundDamping = 2700.0;

	BrakeParams FrontBrake; // диски
	FrontBrake.MaxTorque = 1300.0;
	BrakeParams RearBrake;  // барабаны + ручник
	RearBrake.MaxTorque = 600.0;
	RearBrake.HandbrakeTorque = 1000.0;
	RearBrake.PadNewMm = 6.0;

	auto MakeWheel = [&](const char* Name, double X, double Y, int Axle, int Side, bool bFront) {
		WheelConfig W;
		W.Name = Name;
		W.X = X;
		W.Y = Y;
		W.Z = DroopZ;
		W.Axle = Axle;
		W.Side = Side;
		W.bSteered = bFront;
		W.bDriven = !bFront;
		W.bHandbrake = !bFront;
		W.Inertia = bFront ? 0.9 : 1.1;
		W.Tire = Tire;
		W.Suspension = bFront ? Front : Rear;
		W.Brake = bFront ? FrontBrake : RearBrake;
		return W;
	};
	C.Wheels = {
		MakeWheel("FL", FrontX, -FrontHalfTrack, 0, -1, true),
		MakeWheel("FR", FrontX, FrontHalfTrack, 0, 1, true),
		MakeWheel("RL", RearX, -RearHalfTrack, 1, -1, false),
		MakeWheel("RR", RearX, RearHalfTrack, 1, 1, false),
	};
	C.AntiRollRate = {12000.0, 3000.0};
	return C;
}
} // namespace Presets
} // namespace vsim
