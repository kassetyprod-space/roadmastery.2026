// Peregon — VehicleSimCore
// Небольшая математическая база. Всё в СИ: метры, секунды, килограммы, ньютоны, радианы.
// Ядро не зависит от Unreal: его можно собрать и протестировать отдельно (см. Tests/).
#pragma once

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

namespace vsim
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 9.81;
constexpr double kFuelDensityKgPerL = 0.745; // бензин АИ-92

inline double Clamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
inline double Clamp01(double V) { return Clamp(V, 0.0, 1.0); }
inline double Lerp(double A, double B, double T) { return A + (B - A) * T; }
inline double SignOf(double V) { return V > 0.0 ? 1.0 : (V < 0.0 ? -1.0 : 0.0); }
inline double Sq(double V) { return V * V; }

inline double SmoothStep(double Edge0, double Edge1, double X)
{
	const double T = Clamp01((X - Edge0) / (Edge1 - Edge0));
	return T * T * (3.0 - 2.0 * T);
}

inline double RpmToRadS(double Rpm) { return Rpm * 2.0 * kPi / 60.0; }
inline double RadSToRpm(double W) { return W * 60.0 / (2.0 * kPi); }

inline double MoveTowards(double Current, double Target, double MaxDelta)
{
	if (std::abs(Target - Current) <= MaxDelta)
	{
		return Target;
	}
	return Current + SignOf(Target - Current) * MaxDelta;
}

// Кусочно-линейная кривая: точки (x, y), x по возрастанию. За краями — значение крайней точки.
struct Curve
{
	std::vector<std::pair<double, double>> Points;

	Curve() = default;
	Curve(std::initializer_list<std::pair<double, double>> InPoints) : Points(InPoints) {}

	double Eval(double X) const
	{
		if (Points.empty())
		{
			return 0.0;
		}
		if (X <= Points.front().first)
		{
			return Points.front().second;
		}
		if (X >= Points.back().first)
		{
			return Points.back().second;
		}
		for (size_t I = 1; I < Points.size(); ++I)
		{
			if (X <= Points[I].first)
			{
				const auto& P0 = Points[I - 1];
				const auto& P1 = Points[I];
				const double T = (X - P0.first) / (P1.first - P0.first);
				return Lerp(P0.second, P1.second, T);
			}
		}
		return Points.back().second;
	}

	double MaxY() const
	{
		double M = 0.0;
		for (const auto& P : Points)
		{
			M = P.second > M ? P.second : M;
		}
		return M;
	}
};
} // namespace vsim
