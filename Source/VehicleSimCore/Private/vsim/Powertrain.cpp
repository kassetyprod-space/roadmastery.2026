#include "vsim/Powertrain.h"

namespace vsim
{
namespace EngineModel
{
double FrictionTorque(const EngineParams& P, double Rpm, double Condition)
{
	const double K = std::max(0.0, Rpm) / 1000.0;
	const double Base = P.Friction0 + P.Friction1 * K + P.Friction2 * K * K;
	// Изношенный мотор: больше трения (задиры, плохое масло).
	return Base * (1.0 + 0.5 * (1.0 - Clamp01(Condition)));
}

static double BrakeTorqueAtFullLoad(const EngineParams& P, double Rpm, double Condition)
{
	// Изношенный мотор: падает компрессия, теряется до 45% момента.
	return P.FullLoadTorque.Eval(Rpm) * (0.55 + 0.45 * Clamp01(Condition));
}

double NetTorque(const EngineParams& P, double Rpm, double Throttle, double Condition)
{
	const double Tf = FrictionTorque(P, Rpm, Condition);
	const double Tb = BrakeTorqueAtFullLoad(P, Rpm, Condition);
	return Clamp01(Throttle) * (Tb + Tf) - Tf;
}

double IndicatedPower(const EngineParams& P, double Rpm, double Throttle, double Condition)
{
	const double Tf = FrictionTorque(P, Rpm, Condition);
	const double Tb = BrakeTorqueAtFullLoad(P, Rpm, Condition);
	return Clamp01(Throttle) * (Tb + Tf) * RpmToRadS(std::max(0.0, Rpm));
}

double IdleThrottleBias(const EngineParams& P)
{
	const double Tf = FrictionTorque(P, P.IdleRpm, 1.0);
	const double Tb = BrakeTorqueAtFullLoad(P, P.IdleRpm, 1.0);
	return Tf / (Tb + Tf);
}
} // namespace EngineModel
} // namespace vsim
