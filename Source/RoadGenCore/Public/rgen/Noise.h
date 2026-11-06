// Peregon — RoadGenCore
// Детерминированный ГПСЧ и гладкий шум. Один и тот же сид даёт один и тот же мир на любой машине.
#pragma once

#include "rgen/Export.h"

#include <cmath>
#include <cstdint>

namespace rgen
{
inline uint32_t Hash(uint32_t A, uint32_t B = 0x9E3779B9u, uint32_t C = 0x85EBCA6Bu)
{
	uint32_t H = A * 0xCC9E2D51u;
	H ^= (B + 0x6B43A9B5u) * 0x1B873593u;
	H = (H << 13) | (H >> 19);
	H ^= C * 0xE6546B64u;
	H ^= H >> 16;
	H *= 0x85EBCA6Bu;
	H ^= H >> 13;
	H *= 0xC2B2AE35u;
	H ^= H >> 16;
	return H;
}

// [0, 1)
inline double HashToUnit(uint32_t H) { return static_cast<double>(H >> 8) / 16777216.0; }

class Rng
{
public:
	explicit Rng(uint64_t Seed) : State(Seed * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull) {}

	uint32_t Next()
	{
		// splitmix64
		uint64_t Z = (State += 0x9E3779B97F4A7C15ull);
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		return static_cast<uint32_t>((Z ^ (Z >> 31)) >> 32);
	}
	double Unit() { return HashToUnit(Next()); }
	double Range(double Lo, double Hi) { return Lo + (Hi - Lo) * Unit(); }
	bool Chance(double P) { return Unit() < P; }

private:
	uint64_t State;
};

inline double Fade(double T) { return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); }

// Гладкий 1D-шум, диапазон примерно [-1, 1].
inline double Noise1D(double X, uint32_t Seed)
{
	const double F = std::floor(X);
	const int32_t I = static_cast<int32_t>(F);
	const double A = HashToUnit(Hash(static_cast<uint32_t>(I), Seed)) * 2.0 - 1.0;
	const double B = HashToUnit(Hash(static_cast<uint32_t>(I + 1), Seed)) * 2.0 - 1.0;
	const double T = Fade(X - F);
	return A + (B - A) * T;
}

// Гладкий 2D-шум (value noise), диапазон примерно [-1, 1].
inline double Noise2D(double X, double Y, uint32_t Seed)
{
	const double Fx = std::floor(X);
	const double Fy = std::floor(Y);
	const uint32_t Ix = static_cast<uint32_t>(static_cast<int32_t>(Fx));
	const uint32_t Iy = static_cast<uint32_t>(static_cast<int32_t>(Fy));
	auto V = [Seed](uint32_t Px, uint32_t Py) { return HashToUnit(Hash(Px, Py, Seed)) * 2.0 - 1.0; };
	const double Tx = Fade(X - Fx);
	const double Ty = Fade(Y - Fy);
	const double A = V(Ix, Iy) + (V(Ix + 1, Iy) - V(Ix, Iy)) * Tx;
	const double B = V(Ix, Iy + 1) + (V(Ix + 1, Iy + 1) - V(Ix, Iy + 1)) * Tx;
	return A + (B - A) * Ty;
}
} // namespace rgen
