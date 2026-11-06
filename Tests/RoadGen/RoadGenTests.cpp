// Тесты генератора трассы: геометрия должна быть «проектной» — плавной, проезжей и повторяемой.
#include "rgen/Noise.h"
#include "rgen/Atlas.h"
#include "rgen/Route.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace rgen;

namespace
{
int GFailures = 0;
int GChecks = 0;

void Check(bool bCond, const std::string& What, double Value)
{
	++GChecks;
	std::printf("  [%s] %-60s %12.4f\n", bCond ? " OK " : "FAIL", What.c_str(), Value);
	if (!bCond)
	{
		++GFailures;
	}
}
} // namespace

int main()
{
	std::printf("Peregon RoadGenCore tests\n\n");

	for (uint32_t Seed : {1u, 7u, 2026u, 123456u})
	{
		std::printf("Сид %u\n", Seed);
		RouteParams Params;
		Params.Seed = Seed;
		Route R;
		R.Build(Params);

		Check(std::abs(R.Length() - Params.Length) < Params.Step * 1.5, "длина трассы, м", R.Length());

		double MaxGrade = 0.0;
		double MaxK = 0.0;
		double MaxDk = 0.0;
		double MaxJump = 0.0;
		double PrevK = R.Sample(0.0).Curvature;
		double PrevX = R.Sample(0.0).X;
		double PrevY = R.Sample(0.0).Y;
		for (double S = 1.0; S <= R.Length(); S += 1.0)
		{
			const RouteSample Smp = R.Sample(S);
			MaxGrade = std::max(MaxGrade, std::abs(Smp.Grade));
			MaxK = std::max(MaxK, std::abs(Smp.Curvature));
			MaxDk = std::max(MaxDk, std::abs(Smp.Curvature - PrevK));
			MaxJump = std::max(MaxJump, std::hypot(Smp.X - PrevX, Smp.Y - PrevY));
			PrevK = Smp.Curvature;
			PrevX = Smp.X;
			PrevY = Smp.Y;
		}
		Check(MaxGrade <= Params.MaxGrade + 1e-6, "макс. уклон не больше 6%", MaxGrade);
		Check(MaxK <= 1.0 / Params.MinRadius + 1e-9, "минимальный радиус >= 320 м (макс. кривизна)", MaxK);
		Check(MaxDk < 2e-4, "кривизна меняется плавно (переходные кривые), 1/м на м", MaxDk);
		Check(std::abs(MaxJump - 1.0) < 0.01, "осевая без разрывов (шаг 1 м)", MaxJump);

		const RouteSample End = R.Sample(R.Length());
		Check(End.X > 0.6 * R.Length(), "общее направление — на восток (X конца / длина)", End.X / R.Length());

		// Поиск ближайшей точки: случайные точки в коридоре ±40 м.
		Rng Rand(Seed);
		double MaxErr = 0.0;
		for (int I = 0; I < 300; ++I)
		{
			const double S = Rand.Range(50.0, R.Length() - 50.0);
			const double T = Rand.Range(-40.0, 40.0);
			double X = 0.0, Y = 0.0;
			R.ToWorld(S, T, X, Y);
			const double Found = R.FindNearestS(X, Y, S + Rand.Range(-150.0, 150.0));
			MaxErr = std::max(MaxErr, std::abs(Found - S));
		}
		Check(MaxErr < 2.5, "FindNearestS: ошибка по S, м", MaxErr);

		// Рельеф: без ступенек и полотно ровно на уровне дороги.
		double MaxStep = 0.0;
		double MaxRoadDev = 0.0;
		for (int I = 0; I < 200; ++I)
		{
			const double S = Rand.Range(0.0, R.Length());
			MaxRoadDev = std::max(MaxRoadDev, std::abs(R.GroundHeight(S, Rand.Range(-3.4, 3.4)) - R.Sample(S).Z));
			for (double T = -260.0; T < 260.0; T += 0.5)
			{
				MaxStep = std::max(MaxStep, std::abs(R.GroundHeight(S, T + 0.5) - R.GroundHeight(S, T)));
			}
		}
		Check(MaxRoadDev < 1e-6, "полотно дороги совпадает с профилем, м", MaxRoadDev);
		Check(MaxStep < 0.35, "рельеф без ступенек (макс. перепад на 0.5 м)", MaxStep);

		bool bSorted = true;
		bool bInside = true;
		for (size_t I = 0; I < R.Pois().size(); ++I)
		{
			bSorted = bSorted && (I == 0 || R.Pois()[I - 1].S <= R.Pois()[I].S);
			bInside = bInside && R.Pois()[I].S >= 0.0 && R.Pois()[I].S <= R.Length();
		}
		Check(bSorted && bInside && R.Pois().size() >= 6, "точки интереса на трассе и по порядку (шт.)", static_cast<double>(R.Pois().size()));

		Route Again;
		Again.Build(Params);
		const RouteSample A = R.Sample(R.Length() * 0.77);
		const RouteSample B = Again.Sample(R.Length() * 0.77);
		Check(A.X == B.X && A.Y == B.Y && A.Z == B.Z, "детерминизм: тот же сид — та же трасса", 1.0);
		std::printf("\n");
	}

	Route R1, R2;
	RouteParams P1, P2;
	P1.Seed = 1;
	P2.Seed = 2;
	R1.Build(P1);
	R2.Build(P2);
	Check(std::abs(R1.Sample(10000.0).Y - R2.Sample(10000.0).Y) > 1.0, "разные сиды — разные трассы (разница Y, м)",
		std::abs(R1.Sample(10000.0).Y - R2.Sample(10000.0).Y));

	// Реальный маршрут Новосибирск — Кемерово (Р-255).
	{
		const RouteParams Real = NovosibirskKemerovo(7);
		Route R;
		R.Build(Real);
		std::printf("\nР-255: длина %.1f км в игре, %zu точек интереса, %zu областей\n", R.Length() / 1000.0, R.Pois().size(), R.Regions().size());
		Check(R.IsGuided(), "маршрут по опорным точкам", 1.0);
		Check(R.Length() > 255000.0 * kGameScale * 0.9 && R.Length() < 255000.0 * kGameScale * 1.25, "длина ~51 км (1:5 от 255 км), м", R.Length());
		double WorstMiss = 0.0;
		for (const Anchor& A : Real.Anchors)
		{
			const double S = R.FindNearestS(A.X, A.Y);
			const RouteSample C = R.Sample(S);
			WorstMiss = std::max(WorstMiss, std::hypot(C.X - A.X, C.Y - A.Y));
		}
		Check(WorstMiss < 60.0, "дорога проходит через все пункты (худший промах, м)", WorstMiss);
		double MaxK = 0.0;
		double MaxGrade = 0.0;
		for (double S = 0.0; S < R.Length(); S += 2.0)
		{
			MaxK = std::max(MaxK, std::abs(R.Sample(S).Curvature));
			MaxGrade = std::max(MaxGrade, std::abs(R.Sample(S).Grade));
		}
		Check(MaxK <= 1.0 / Real.MinRadius + 1e-9, "радиусы не круче 300 м", MaxK);
		Check(MaxGrade <= Real.MaxGrade + 1e-6, "уклоны не круче 6%", MaxGrade);
		Check(R.Regions().size() == 2, "две области: Новосибирская и Кемеровская", static_cast<double>(R.Regions().size()));
		Check(std::abs(R.RealKmAt(R.Length()) - 255.0) < 0.5, "километраж в конце — 255 км", R.RealKmAt(R.Length()));
		double PrevKm = -1.0;
		bool bMonotone = true;
		for (double S = 0.0; S <= R.Length(); S += 50.0)
		{
			const double Km = R.RealKmAt(S);
			bMonotone &= Km >= PrevKm;
			PrevKm = Km;
		}
		Check(bMonotone, "километраж растёт вдоль дороги", 1.0);
		int Fuel = 0;
		int Shops = 0;
		bool bSorted = true;
		for (size_t I = 0; I < R.Pois().size(); ++I)
		{
			const Poi& Item = R.Pois()[I];
			Fuel += Item.Type == PoiType::Fuel ? 1 : 0;
			Shops += Item.Type == PoiType::Shop ? 1 : 0;
			bSorted &= I == 0 || R.Pois()[I - 1].S <= Item.S;
			std::printf("  %6.0f м  %5.1f км  [%d] %s\n", Item.S, Item.RealKm, Item.Region, Item.Name.c_str());
		}
		Check(bSorted, "точки интереса по порядку", 1.0);
		Check(Fuel >= 5, "заправок хватает (не реже ~50 км)", Fuel);
		Check(Shops >= 3, "магазины в райцентрах (карты)", Shops);
		Check(R.Pois().back().Type == PoiType::Finish, "финиш — Кемерово", 1.0);
		double LatEnd = 0.0;
		double LonEnd = 0.0;
		const RouteSample End = R.Sample(R.Length());
		GameToGeo(NovosibirskKemerovoOrigin(), End.X, End.Y, LatEnd, LonEnd);
		Check(std::abs(LatEnd - 55.355) < 0.01 && std::abs(LonEnd - 86.087) < 0.01, "конец дороги в Кемерове (широта)", LatEnd);
	}

	std::printf("\n%d проверок, %d провалено\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
