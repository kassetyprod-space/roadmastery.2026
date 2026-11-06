#include "rgen/Landscape.h"

#include "rgen/Noise.h"

#include <algorithm>
#include <cmath>

namespace rgen
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double Clamp01(double V) { return std::min(1.0, std::max(0.0, V)); }
double Smooth(double E0, double E1, double X)
{
	const double T = Clamp01((X - E0) / (E1 - E0));
	return T * T * (3.0 - 2.0 * T);
}
// Колокол по разности азимутов (°).
double AzBell(double Az, double Center, double Width)
{
	double D = std::fmod(Az - Center + 540.0, 360.0) - 180.0;
	return std::exp(-(D * D) / (Width * Width));
}

// Насколько большой населённый пункт (по названию из атласа). 2 — областной центр.
int TownSize(const std::string& Name, PoiType Type)
{
	if (Name.find("Новосибирск") != std::string::npos || Name.find("Кемерово") != std::string::npos)
	{
		return 2;
	}
	if (Name.find("Юрга") != std::string::npos)
	{
		return 1;
	}
	return Type == PoiType::Town ? 0 : -1;
}
} // namespace

double Building::Height() const
{
	switch (Kind)
	{
	case BuildingKind::Chimney: return 70.0 + static_cast<double>(Floors) * 5.0;
	case BuildingKind::Mast: return 110.0 + static_cast<double>(Floors) * 4.0;
	case BuildingKind::Church: return 14.0;
	case BuildingKind::Industrial: return 9.0 + static_cast<double>(Floors) * 3.0;
	default: return static_cast<double>(Floors) * 2.8 + 1.2;
	}
}

void Landscape::Build(const Route& InRoute)
{
	R = &InRoute;
	Seed = Hash(InRoute.Params().Seed, 0x1A4Du);
	Line.clear();
	Towns.clear();
	const double Len = InRoute.Length();
	BMinX = BMinY = 1e30;
	BMaxX = BMaxY = -1e30;
	for (double S = 0.0;; S += 25.0)
	{
		const double Sc = std::min(S, Len);
		const RouteSample Smp = InRoute.Sample(Sc);
		Line.push_back({Smp.X, Smp.Y, Sc, Smp.Z});
		BMinX = std::min(BMinX, Smp.X);
		BMinY = std::min(BMinY, Smp.Y);
		BMaxX = std::max(BMaxX, Smp.X);
		BMaxY = std::max(BMaxY, Smp.Y);
		if (Sc >= Len)
		{
			break;
		}
	}

	// Корзины отрезков.
	BucketOx = BMinX - 1.0;
	BucketOy = BMinY - 1.0;
	BucketsX = static_cast<int>((BMaxX - BMinX) / BucketSize) + 3;
	BucketsY = static_cast<int>((BMaxY - BMinY) / BucketSize) + 3;
	Buckets.assign(static_cast<size_t>(BucketsX * BucketsY), {});
	for (size_t I = 0; I + 1 < Line.size(); ++I)
	{
		const int X0 = static_cast<int>((std::min(Line[I].X, Line[I + 1].X) - BucketOx) / BucketSize);
		const int X1 = static_cast<int>((std::max(Line[I].X, Line[I + 1].X) - BucketOx) / BucketSize);
		const int Y0 = static_cast<int>((std::min(Line[I].Y, Line[I + 1].Y) - BucketOy) / BucketSize);
		const int Y1 = static_cast<int>((std::max(Line[I].Y, Line[I + 1].Y) - BucketOy) / BucketSize);
		for (int Bx = X0; Bx <= X1; ++Bx)
		{
			for (int By = Y0; By <= Y1; ++By)
			{
				Buckets[static_cast<size_t>(By * BucketsX + Bx)].push_back(static_cast<uint32_t>(I));
			}
		}
	}

	// Города: центр немного в стороне от трассы (дорога идёт краем или насквозь).
	for (const Poi& P : InRoute.Pois())
	{
		if (P.Type != PoiType::Town && P.Type != PoiType::Finish)
		{
			continue;
		}
		const int Size = TownSize(P.Name, P.Type);
		if (Size < 0)
		{
			continue;
		}
		Settlement T;
		T.Name = P.Name;
		T.S = P.S;
		T.Size = Size;
		T.Radius = Size == 2 ? 1100.0 : (Size == 1 ? 600.0 : 300.0);
		const RouteSample Smp = InRoute.Sample(P.S);
		const double Fx = std::cos(Smp.Heading);
		const double Fy = std::sin(Smp.Heading);
		// Начало и конец маршрута: город лежит «за краем» дороги.
		double Along = 0.0;
		if (P.S < 1.0)
		{
			Along = -T.Radius * 0.55;
		}
		else if (P.S > Len - 50.0)
		{
			Along = T.Radius * 0.5;
		}
		const double Side = (Hash(Seed, static_cast<uint32_t>(P.S)) & 1u) ? 1.0 : -1.0;
		const double Lateral = Size == 0 ? Side * 120.0 : Side * T.Radius * 0.15;
		T.X = Smp.X + Fx * Along - Fy * Lateral;
		T.Y = Smp.Y + Fy * Along + Fx * Lateral;
		BuildCity(T);
		Towns.push_back(std::move(T));
	}
}

void Landscape::Nearest(double X, double Y, double& OutS, double& OutT, double& OutDist) const
{
	OutS = 0.0;
	OutT = 0.0;
	OutDist = 1e30;
	if (Line.size() < 2)
	{
		return;
	}
	size_t Best = 0;
	double BestD2 = 1e300;
	auto TestSegment = [&](size_t I) {
		const Pt& A = Line[I];
		const Pt& B = Line[I + 1];
		const double Dx = B.X - A.X;
		const double Dy = B.Y - A.Y;
		const double L2 = Dx * Dx + Dy * Dy;
		const double U = L2 > 1e-9 ? Clamp01(((X - A.X) * Dx + (Y - A.Y) * Dy) / L2) : 0.0;
		const double Px = A.X + Dx * U - X;
		const double Py = A.Y + Dy * U - Y;
		const double D2 = Px * Px + Py * Py;
		if (D2 < BestD2)
		{
			BestD2 = D2;
			Best = I;
			OutS = A.S + (B.S - A.S) * U;
		}
	};
	// Кольца корзин вокруг точки: как только найденное ближе, чем внутренний край кольца, — дальше не ищем.
	const int Cx = static_cast<int>(std::floor((X - BucketOx) / BucketSize));
	const int Cy = static_cast<int>(std::floor((Y - BucketOy) / BucketSize));
	bool bFound = false;
	for (int Ring = 0; Ring < 14 && !Buckets.empty(); ++Ring)
	{
		for (int Bx = Cx - Ring; Bx <= Cx + Ring; ++Bx)
		{
			for (int By = Cy - Ring; By <= Cy + Ring; ++By)
			{
				if (std::max(std::abs(Bx - Cx), std::abs(By - Cy)) != Ring || Bx < 0 || By < 0 || Bx >= BucketsX || By >= BucketsY)
				{
					continue;
				}
				for (uint32_t I : Buckets[static_cast<size_t>(By * BucketsX + Bx)])
				{
					TestSegment(I);
				}
			}
		}
		const double Inner = static_cast<double>(Ring) * BucketSize;
		if (BestD2 < 1e299 && BestD2 <= Inner * Inner)
		{
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		for (size_t I = 0; I + 1 < Line.size(); ++I)
		{
			TestSegment(I);
		}
	}
	const Pt& A = Line[Best];
	const Pt& B = Line[Best + 1];
	const double Dx = B.X - A.X;
	const double Dy = B.Y - A.Y;
	const double L = std::max(1e-6, std::sqrt(Dx * Dx + Dy * Dy));
	// Вправо по ходу: (−sin, cos) направления.
	OutT = ((X - A.X) * (-Dy / L) + (Y - A.Y) * (Dx / L));
	OutDist = std::sqrt(BestD2);
}

double Landscape::ForestDensity(double X, double Y) const
{
	if (!R)
	{
		return 0.0;
	}
	const double Forest = Noise2D(X / 380.0, Y / 380.0, Hash(R->Params().Seed, 0xF0E57u));
	return (Smooth(-0.25, 0.35, Forest) * 0.75 + 0.04) * (1.0 - UrbanMask(X, Y));
}

double Landscape::UrbanMask(double X, double Y) const
{
	double M = 0.0;
	for (const Settlement& T : Towns)
	{
		const double D = std::hypot(X - T.X, Y - T.Y);
		M = std::max(M, 1.0 - Smooth(T.Radius * 0.85, T.Radius * 1.25, D));
	}
	return M;
}

bool Landscape::InsideBuilding(double X, double Y, double Margin) const
{
	for (const Settlement& T : Towns)
	{
		if (std::hypot(X - T.X, Y - T.Y) > T.Radius * 1.3 + 150.0)
		{
			continue;
		}
		for (const Building& B : T.Buildings)
		{
			const double Dx = X - B.X;
			const double Dy = Y - B.Y;
			// В осях здания.
			const double U = Dx * std::cos(B.Yaw) + Dy * std::sin(B.Yaw);
			const double V = -Dx * std::sin(B.Yaw) + Dy * std::cos(B.Yaw);
			if (std::abs(U) < B.Length * 0.5 + Margin && std::abs(V) < B.Width * 0.5 + Margin)
			{
				return true;
			}
		}
	}
	return false;
}

double Landscape::CanopyHeight(double X, double Y) const
{
	// Дальние массивы — «шапкой» над рельефом: так лес виден до горизонта, а деревьев на это не нужно.
	return 11.0 * Smooth(0.42, 0.62, ForestDensity(X, Y));
}

double Landscape::OuterHeight(double X, double Y, double S, double D) const
{
	const double RoadZ = R->Sample(S).Z;
	// В городе рельеф спокойнее — но только вдали от дороги, у дороги земля та же, что у чанков.
	const double Urban = UrbanMask(X, Y) * Smooth(450.0, 800.0, D);
	const double Natural = RoadZ + R->Params().TerrainAmplitude * R->NaturalNoise(X, Y) * (1.0 - 0.7 * Urban);
	// Холмы растут с удалением от трассы: у дороги рельеф спокойный, к горизонту — увалы и гривы.
	const double Hills = 34.0 * Smooth(350.0, 3000.0, D) * (1.0 - Urban) *
		(0.65 * Noise2D(X / 2100.0, Y / 2100.0, Seed + 11u) + 0.35 * Noise2D(X / 760.0, Y / 760.0, Seed + 12u));
	return Natural + Hills;
}

double Landscape::GroundAt(double X, double Y) const
{
	if (!R)
	{
		return 0.0;
	}
	double S = 0.0;
	double T = 0.0;
	double D = 0.0;
	Nearest(X, Y, S, T, D);
	return D < 300.0 ? R->GroundHeight(S, T) : OuterHeight(X, Y, S, D);
}

double Landscape::FarHeight(double X, double Y) const
{
	if (!R)
	{
		return 0.0;
	}
	double S = 0.0;
	double T = 0.0;
	double D = 0.0;
	Nearest(X, Y, S, T, D);
	const double Outer = OuterHeight(X, Y, S, D);
	if (D >= 520.0)
	{
		return Outer;
	}
	// У дороги лежит настоящая земля чанков. Сетка дальнего рельефа редкая (140 м), и между её узлами
	// земля может проседать — поэтому узел берёт минимум земли во всём квадрате ±140 м (это все
	// треугольники, где он участвует) и ещё 1.5 м запаса: дальний рельеф гарантированно под землёй.
	double Low = 1e30;
	for (int I = -3; I <= 3; ++I)
	{
		for (int J = -3; J <= 3; ++J)
		{
			Low = std::min(Low, GroundAt(X + I * 46.67, Y + J * 46.67));
		}
	}
	Low -= 1.5;
	return D < 380.0 ? Low : Low + (Outer - Low) * Smooth(380.0, 520.0, D);
}

double Landscape::RidgeHeight(double AzimuthDeg, int Layer, double ViewerX, double ViewerY) const
{
	const double Az = AzimuthDeg * kPi / 180.0;
	const uint32_t S = Seed + static_cast<uint32_t>(Layer) * 101u;
	// Неровный край: несколько октав шума по окружности (бесшовно — шум по точке на окружности).
	auto Ridge = [&](double Scale) {
		return 0.6 * Noise2D(std::cos(Az) * Scale, std::sin(Az) * Scale, S) + 0.4 * Noise2D(std::cos(Az) * Scale * 3.1, std::sin(Az) * Scale * 3.1, S + 5u);
	};
	switch (Layer)
	{
	case 0: return 22.0 + 18.0 * Ridge(4.0);
	case 1: return 38.0 + 26.0 * Ridge(6.0);
	default:
	{
		// Салаирский кряж — на юге всего пути, Кузнецкий Алатау — на востоке, ближе к Кемерово.
		double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0;
		Bounds(MinX, MinY, MaxX, MaxY);
		const double East = Clamp01((ViewerY - MinY) / std::max(1.0, MaxY - MinY));
		(void)ViewerX;
		const double Salair = 110.0 * AzBell(AzimuthDeg, 185.0, 38.0);
		const double Alatau = 260.0 * AzBell(AzimuthDeg, 105.0, 30.0) * (0.35 + 0.65 * East);
		const double Peaks = std::max(0.0, Ridge(9.0)) * (Salair + Alatau) * 0.6;
		return 25.0 + Salair + Alatau + Peaks + 12.0 * Ridge(5.0);
	}
	}
}

void Landscape::Bounds(double& MinX, double& MinY, double& MaxX, double& MaxY) const
{
	MinX = BMinX;
	MinY = BMinY;
	MaxX = BMaxX;
	MaxY = BMaxY;
}

bool Landscape::Blocked(double X, double Y, double Radius, const std::vector<Building>& Placed) const
{
	double S = 0.0;
	double T = 0.0;
	double D = 0.0;
	Nearest(X, Y, S, T, D);
	if (D < 32.0 + Radius)
	{
		return true; // дорога, обочины и кюветы
	}
	for (const FlattenZone& F : R->Flatten())
	{
		if (S > F.S0 - 30.0 && S < F.S1 + 30.0 && T > F.TMin - 20.0 - Radius && T < F.TMax + 20.0 + Radius)
		{
			return true; // АЗС, кафе, улицы сёл
		}
	}
	for (const Building& B : Placed)
	{
		const double Rb = 0.5 * std::hypot(B.Length, B.Width);
		if (std::hypot(B.X - X, B.Y - Y) < Rb + Radius + 8.0)
		{
			return true;
		}
	}
	return false;
}

void Landscape::BuildCity(Settlement& Town)
{
	Rng Rand(Hash(Seed, static_cast<uint32_t>(Town.S * 3.0 + 17.0)));
	double S0 = 0.0, T0 = 0.0, D0 = 0.0;
	Nearest(Town.X, Town.Y, S0, T0, D0);
	const double Heading = R->Sample(S0).Heading;
	const double Cx = std::cos(Heading);
	const double Cy = std::sin(Heading);
	const double Cell = Town.Size == 0 ? 55.0 : 68.0;
	const int N = static_cast<int>(std::ceil(Town.Radius / Cell));
	// Промзона — в одном секторе города (у Кемерово — «Азот» и коксохим, у Юрги — машзавод).
	const double IndustrialAz = Rand.Range(0.0, 2.0 * kPi);
	std::vector<Building>& Out = Town.Buildings;
	auto Ground = [&](double X, double Y, double L, double W, double Yaw) {
		double Z = 1e30;
		const double Ux = std::cos(Yaw), Uy = std::sin(Yaw);
		for (double A : {-0.5, 0.5})
		{
			for (double B : {-0.5, 0.5})
			{
				Z = std::min(Z, GroundAt(X + Ux * L * A - Uy * W * B, Y + Uy * L * A + Ux * W * B));
			}
		}
		return Z;
	};
	auto Place = [&](Building B) {
		B.Z = Ground(B.X, B.Y, B.Length, B.Width, B.Yaw) - 0.3;
		B.Seed = Rand.Next();
		Out.push_back(B);
	};

	for (int I = -N; I <= N; ++I)
	{
		for (int J = -N; J <= N; ++J)
		{
			const double Lx = I * Cell + Rand.Range(-6.0, 6.0);
			const double Ly = J * Cell + Rand.Range(-6.0, 6.0);
			const double Dist = std::hypot(Lx, Ly);
			const double Rn = Dist / Town.Radius;
			if (Rn > 1.0)
			{
				continue;
			}
			const double X = Town.X + Cx * Lx - Cy * Ly;
			const double Y = Town.Y + Cy * Lx + Cx * Ly;
			const double Fill = Town.Size == 0 ? 0.55 * (1.0 - Rn) : 0.9 - 0.45 * Rn;
			if (!Rand.Chance(Fill))
			{
				continue;
			}
			Building B;
			B.X = X;
			B.Y = Y;
			B.Yaw = Heading + (Rand.Chance(0.5) ? 0.0 : kPi * 0.5);
			const double Angle = std::atan2(Ly, Lx);
			const double InIndustry = std::cos(Angle - IndustrialAz);
			if (Town.Size > 0 && Rn > 0.45 && InIndustry > 0.85)
			{
				B.Kind = BuildingKind::Industrial;
				B.Length = Rand.Range(40.0, 70.0);
				B.Width = Rand.Range(18.0, 30.0);
				B.Floors = static_cast<int>(Rand.Range(0.0, 3.0));
			}
			else if (Town.Size == 2 && Rn < 0.35 && Rand.Chance(0.45))
			{
				B.Kind = BuildingKind::Tower;
				B.Length = 22.0;
				B.Width = 20.0;
				B.Floors = 12 + static_cast<int>(Rand.Range(0.0, 6.0));
			}
			else if (Town.Size > 0 && Rn < 0.7 && Rand.Chance(0.6))
			{
				B.Kind = BuildingKind::Panel9;
				B.Length = Rand.Chance(0.5) ? 60.0 : 90.0;
				B.Width = 13.0;
				B.Floors = 9 + (Town.Size == 2 && Rand.Chance(0.3) ? 1 : 0);
			}
			else if (Rn < 0.25 && Rand.Chance(0.4))
			{
				B.Kind = BuildingKind::Brick;
				B.Length = Rand.Range(30.0, 50.0);
				B.Width = 15.0;
				B.Floors = 3 + (Rand.Chance(0.5) ? 1 : 0);
			}
			else
			{
				B.Kind = BuildingKind::Panel5;
				B.Length = Rand.Chance(0.6) ? 60.0 : 80.0;
				B.Width = 12.0;
				B.Floors = 5;
			}
			const double Radius = 0.5 * std::hypot(B.Length, B.Width);
			if (Blocked(B.X, B.Y, Radius, Out))
			{
				continue;
			}
			Place(B);
		}
	}

	// Ориентиры: трубы в промзоне, храм у центра, мачта связи на окраине.
	auto TryPlace = [&](BuildingKind Kind, double Dist, double Angle, double L, double W, int Floors) {
		for (int Attempt = 0; Attempt < 24; ++Attempt)
		{
			const double A = Angle + Rand.Range(-0.4, 0.4) * Attempt / 6.0;
			const double Dd = Dist * (1.0 + 0.04 * Attempt);
			Building B;
			B.Kind = Kind;
			const double Lx = std::cos(A) * Dd;
			const double Ly = std::sin(A) * Dd;
			B.X = Town.X + Cx * Lx - Cy * Ly;
			B.Y = Town.Y + Cy * Lx + Cx * Ly;
			B.Yaw = Heading;
			B.Length = L;
			B.Width = W;
			B.Floors = Floors;
			if (!Blocked(B.X, B.Y, 0.5 * std::hypot(L, W), Out))
			{
				Place(B);
				return;
			}
		}
	};
	if (Town.Size > 0)
	{
		const int Chimneys = Town.Size == 2 ? 4 : 2;
		for (int K = 0; K < Chimneys; ++K)
		{
			TryPlace(BuildingKind::Chimney, Town.Radius * (0.7 + 0.08 * K), IndustrialAz + 0.12 * K, 7.0, 7.0, K % 3);
		}
		TryPlace(BuildingKind::Mast, Town.Radius * 0.8, IndustrialAz + kPi * 0.6, 12.0, 12.0, Town.Size);
	}
	TryPlace(BuildingKind::Church, Town.Size == 0 ? 60.0 : Town.Radius * 0.2, IndustrialAz + kPi, 26.0, 18.0, 1);
}
} // namespace rgen
