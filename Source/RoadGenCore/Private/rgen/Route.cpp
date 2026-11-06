#include "rgen/Route.h"

#include "rgen/Noise.h"

#include <algorithm>
#include <cmath>

namespace rgen
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double Clamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
double Lerp(double A, double B, double T) { return A + (B - A) * T; }
double Dbl(size_t V) { return static_cast<double>(V); }
double SmoothStep(double E0, double E1, double X)
{
	const double T = Clamp((X - E0) / (E1 - E0), 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}
} // namespace

void Route::Build(const RouteParams& InParams)
{
	P = InParams;
	P.Step = std::max(0.5, P.Step);
	P.MinRadius = std::max(50.0, P.MinRadius);
	P.MaxRadius = std::max(P.MinRadius, P.MaxRadius);
	BuildPlan();
	BuildProfile();
	BuildPois();
}

void Route::BuildPlan()
{
	Rng R(P.Seed);
	Samples.clear();
	const size_t Count = static_cast<size_t>(std::ceil(P.Length / P.Step)) + 1;
	Samples.reserve(Count);

	// Кривизна как функция S: прямая → переходная (клотоида) → дуга → переходная → прямая.
	std::vector<double> K(Count, 0.0);
	double S = 0.0;
	double Heading = 0.0;
	bool bFirst = true;
	while (S < P.Length)
	{
		const double Straight = bFirst ? 420.0 : R.Range(220.0, 900.0);
		bFirst = false;
		S += Straight;
		if (S >= P.Length)
		{
			break;
		}

		const double Radius = R.Range(P.MinRadius, P.MaxRadius);
		const double Angle = R.Range(10.0, 55.0) * kPi / 180.0;
		// Общее направление — на восток: если сильно ушли в сторону, поворачиваем обратно.
		double Sign = R.Chance(0.5) ? 1.0 : -1.0;
		if (Heading > 0.55)
		{
			Sign = -1.0;
		}
		else if (Heading < -0.55)
		{
			Sign = 1.0;
		}
		const double Arc = Radius * Angle;
		const double Transition = std::min(90.0, Arc * 0.35);
		const double Kmax = Sign / Radius;
		const double Hold = std::max(0.0, Arc - Transition); // поворот = Kmax * (Hold + Transition)
		const double Total = Hold + 2.0 * Transition;
		for (size_t I = static_cast<size_t>(S / P.Step); I < Count && Dbl(I) * P.Step < S + Total; ++I)
		{
			const double U = Dbl(I) * P.Step - S;
			double F = 1.0;
			if (U < Transition)
			{
				F = U / Transition;
			}
			else if (U > Transition + Hold)
			{
				F = (Total - U) / Transition;
			}
			K[I] = Kmax * Clamp(F, 0.0, 1.0);
		}
		Heading += Kmax * (Hold + Transition);
		S += Total;
	}

	// Интегрируем план (метод средней точки).
	double X = 0.0;
	double Y = 0.0;
	double H = 0.0;
	for (size_t I = 0; I < Count; ++I)
	{
		RouteSample Smp;
		Smp.X = X;
		Smp.Y = Y;
		Smp.Heading = H;
		Smp.Curvature = K[I];
		Samples.push_back(Smp);
		const double KNext = I + 1 < Count ? K[I + 1] : K[I];
		const double HMid = H + 0.5 * (K[I] + KNext) * 0.5 * P.Step;
		X += std::cos(HMid) * P.Step;
		Y += std::sin(HMid) * P.Step;
		H += 0.5 * (K[I] + KNext) * P.Step;
	}
	TotalLength = Dbl(Samples.size() - 1) * P.Step;
}

void Route::BuildProfile()
{
	const size_t N = Samples.size();
	std::vector<double> Z(N);
	const uint32_t SeedZ = Hash(P.Seed, 0x5EEDu);
	for (size_t I = 0; I < N; ++I)
	{
		const double S = Dbl(I) * P.Step;
		Z[I] = P.HillAmplitude * (0.7 * Noise1D(S / 1400.0, SeedZ) + 0.3 * Noise1D(S / 480.0, SeedZ + 1));
	}
	// Ограничение уклона: проход вперёд и назад (липшицева огибающая), затем сглаживание.
	const double MaxDz = P.MaxGrade * 0.92 * P.Step;
	for (size_t I = 1; I < N; ++I)
	{
		Z[I] = Clamp(Z[I], Z[I - 1] - MaxDz, Z[I - 1] + MaxDz);
	}
	for (size_t I = N - 1; I-- > 0;)
	{
		Z[I] = Clamp(Z[I], Z[I + 1] - MaxDz, Z[I + 1] + MaxDz);
	}
	// Скользящее среднее не увеличивает максимальный уклон, но убирает «изломы» профиля.
	const int Half = std::max(1, static_cast<int>(30.0 / P.Step));
	for (int Pass = 0; Pass < 3; ++Pass)
	{
		std::vector<double> Out(N);
		for (size_t I = 0; I < N; ++I)
		{
			double Sum = 0.0;
			int Cnt = 0;
			for (int D = -Half; D <= Half; ++D)
			{
				const long J = static_cast<long>(I) + D;
				const size_t Jc = static_cast<size_t>(Clamp(static_cast<double>(J), 0.0, static_cast<double>(N - 1)));
				Sum += Z[Jc];
				++Cnt;
			}
			Out[I] = Sum / Cnt;
		}
		Z.swap(Out);
	}
	// Старт на нулевой высоте — удобнее отлаживать.
	const double Z0 = Z[0];
	ZMin = 1e9;
	ZMax = -1e9;
	for (size_t I = 0; I < N; ++I)
	{
		Samples[I].Z = Z[I] - Z0;
		ZMin = std::min(ZMin, Samples[I].Z);
		ZMax = std::max(ZMax, Samples[I].Z);
	}
	for (size_t I = 0; I < N; ++I)
	{
		const size_t A = I > 0 ? I - 1 : I;
		const size_t B = I + 1 < N ? I + 1 : I;
		Samples[I].Grade = (Samples[B].Z - Samples[A].Z) / (Dbl(B - A) * P.Step);
	}
	ZMin -= P.TerrainAmplitude;
	ZMax += P.TerrainAmplitude;
}

void Route::BuildPois()
{
	PoiList.clear();
	FlattenList.clear();
	const double L = TotalLength;
	auto Add = [this](PoiType Type, double S, int Side, const char* Name) {
		Poi Item;
		Item.Type = Type;
		Item.S = S;
		Item.Side = Side;
		Item.Name = Name;
		PoiList.push_back(Item);
	};
	// Тестовый участок. Названия вымышленные (типичные для Сибири), пока трасса процедурная.
	Add(PoiType::Town, 0.0, 1, "Новосибирск");
	Add(PoiType::Fuel, std::min(900.0, L * 0.1), 1, "АЗС «Сибирь»");
	Add(PoiType::Village, L * 0.27, 1, "Берёзовка");
	Add(PoiType::Fuel, L * 0.53, 1, "АЗС «Обь-Нефть»");
	Add(PoiType::Village, L * 0.66, 1, "Сосновка");
	Add(PoiType::Cafe, L * 0.8, 1, "Кафе «Привал»");
	Add(PoiType::Finish, L - 450.0, 1, "Заречный");
	std::sort(PoiList.begin(), PoiList.end(), [](const Poi& A, const Poi& B) { return A.S < B.S; });

	for (const Poi& Item : PoiList)
	{
		FlattenZone Z;
		switch (Item.Type)
		{
		case PoiType::Fuel:
		case PoiType::Cafe:
			Z.S0 = Item.S - 45.0;
			Z.S1 = Item.S + 45.0;
			Z.TMin = Item.Side > 0 ? 5.0 : -48.0;
			Z.TMax = Item.Side > 0 ? 48.0 : -5.0;
			Z.Strength = 1.0;
			FlattenList.push_back(Z);
			break;
		case PoiType::Village:
		case PoiType::Finish:
			Z.S0 = Item.S - 220.0;
			Z.S1 = Item.S + 220.0;
			Z.TMin = -50.0;
			Z.TMax = 50.0;
			Z.Strength = 0.85;
			FlattenList.push_back(Z);
			break;
		default:
			break;
		}
	}
}

RouteSample Route::Sample(double S) const
{
	if (Samples.empty())
	{
		return RouteSample();
	}
	const double F = Clamp(S, 0.0, TotalLength) / P.Step;
	const size_t I = std::min(static_cast<size_t>(F), Samples.size() - 2);
	const double T = Clamp(F - Dbl(I), 0.0, 1.0);
	const RouteSample& A = Samples[I];
	const RouteSample& B = Samples[I + 1];
	RouteSample Out;
	Out.X = Lerp(A.X, B.X, T);
	Out.Y = Lerp(A.Y, B.Y, T);
	Out.Z = Lerp(A.Z, B.Z, T);
	Out.Heading = Lerp(A.Heading, B.Heading, T);
	Out.Curvature = Lerp(A.Curvature, B.Curvature, T);
	Out.Grade = Lerp(A.Grade, B.Grade, T);
	// За пределами трассы продолжаем по прямой (нужно для «хвостов» рельефа у начала и конца).
	if (S < 0.0 || S > TotalLength)
	{
		const double Over = S < 0.0 ? S : S - TotalLength;
		Out.X += std::cos(Out.Heading) * Over;
		Out.Y += std::sin(Out.Heading) * Over;
	}
	return Out;
}

void Route::ToWorld(double S, double T, double& OutX, double& OutY) const
{
	const RouteSample C = Sample(S);
	// Вправо от направления (cos h, sin h) — это (-sin h, cos h): Y в Unreal смотрит вправо.
	OutX = C.X - std::sin(C.Heading) * T;
	OutY = C.Y + std::cos(C.Heading) * T;
}

double Route::NaturalNoise(double X, double Y) const
{
	const uint32_t Seed = Hash(P.Seed, 0x7E44u);
	return 0.7 * Noise2D(X / 420.0, Y / 420.0, Seed) + 0.3 * Noise2D(X / 110.0, Y / 110.0, Seed + 7);
}

double Route::GroundHeight(double S, double T) const
{
	const double RoadZ = Sample(S).Z;
	const double A = std::abs(T);
	const double Edge = P.LaneWidth;
	const double Shoulder = ShoulderOuter();
	double Z;
	if (A <= Edge)
	{
		Z = RoadZ;
	}
	else if (A <= Shoulder)
	{
		Z = RoadZ - (A - Edge) * 0.03; // обочина чуть ниже полотна
	}
	else if (A <= 9.0)
	{
		Z = RoadZ - (Shoulder - Edge) * 0.03 - 0.84 * SmoothStep(Shoulder, 9.0, A); // откос в кювет
	}
	else
	{
		double X = 0.0;
		double Y = 0.0;
		ToWorld(S, T, X, Y);
		const double Ditch = RoadZ - 0.9 + 0.75 * SmoothStep(9.0, 14.0, A);
		const double Natural = RoadZ + P.TerrainAmplitude * NaturalNoise(X, Y) * SmoothStep(14.0, 140.0, A);
		Z = Lerp(Ditch, Natural, SmoothStep(14.0, 40.0, A));
	}

	// Площадки АЗС и улицы сёл — почти вровень с дорогой.
	for (const FlattenZone& F : FlattenList)
	{
		if (S < F.S0 - 40.0 || S > F.S1 + 40.0 || T < F.TMin - 25.0 || T > F.TMax + 25.0 || A <= Shoulder)
		{
			continue;
		}
		const double Ws = SmoothStep(F.S0 - 40.0, F.S0, S) * (1.0 - SmoothStep(F.S1, F.S1 + 40.0, S));
		const double Wt = SmoothStep(F.TMin - 25.0, F.TMin, T) * (1.0 - SmoothStep(F.TMax, F.TMax + 25.0, T));
		Z = Lerp(Z, RoadZ - 0.08, F.Strength * Ws * Wt);
	}
	return Z;
}

double Route::FindNearestS(double X, double Y, double Hint, double Window) const
{
	if (Samples.empty())
	{
		return 0.0;
	}
	size_t Lo = 0;
	size_t Hi = Samples.size() - 1;
	if (Hint >= 0.0)
	{
		Lo = static_cast<size_t>(Clamp((Hint - Window) / P.Step, 0.0, static_cast<double>(Samples.size() - 1)));
		Hi = static_cast<size_t>(Clamp((Hint + Window) / P.Step, 0.0, static_cast<double>(Samples.size() - 1)));
	}
	const size_t Coarse = std::max<size_t>(1, static_cast<size_t>(6.0 / P.Step));
	size_t Best = Lo;
	double BestD = 1e300;
	for (size_t I = Lo; I <= Hi; I += Coarse)
	{
		const double D = (Samples[I].X - X) * (Samples[I].X - X) + (Samples[I].Y - Y) * (Samples[I].Y - Y);
		if (D < BestD)
		{
			BestD = D;
			Best = I;
		}
	}
	const size_t A = Best > Coarse ? Best - Coarse : 0;
	const size_t B = std::min(Samples.size() - 1, Best + Coarse);
	for (size_t I = A; I <= B; ++I)
	{
		const double D = (Samples[I].X - X) * (Samples[I].X - X) + (Samples[I].Y - Y) * (Samples[I].Y - Y);
		if (D < BestD)
		{
			BestD = D;
			Best = I;
		}
	}
	// Уточняем проекцией на касательную.
	const RouteSample& C = Samples[Best];
	const double Along = (X - C.X) * std::cos(C.Heading) + (Y - C.Y) * std::sin(C.Heading);
	return Clamp(Dbl(Best) * P.Step + Clamp(Along, -P.Step, P.Step), 0.0, TotalLength);
}

double Route::LateralOffset(double X, double Y, double S) const
{
	const RouteSample C = Sample(S);
	return -(X - C.X) * std::sin(C.Heading) + (Y - C.Y) * std::cos(C.Heading);
}

const Poi* Route::NextPoi(double S) const
{
	for (const Poi& Item : PoiList)
	{
		if (Item.S > S + 15.0)
		{
			return &Item;
		}
	}
	return nullptr;
}
} // namespace rgen
