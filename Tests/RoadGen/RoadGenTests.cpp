// Тесты генератора трассы: геометрия должна быть «проектной» — плавной, проезжей и повторяемой.
#include "rgen/Noise.h"
#include "rgen/Atlas.h"
#include "rgen/Route.h"
#include "rgen/Climate.h"
#include "rgen/Landscape.h"

#include <chrono>

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

	// --- Климат: солнце, времена года, погода ---
	{
		std::printf("\nКлимат\n");
		double Elev = 0.0, Az = 0.0;
		Climate::SunPosition(172.0, 13.4, Elev, Az);
		Check(std::abs(Elev - 58.4) < 1.0, "летнее солнцестояние: солнце в полдень ~58° (55° с.ш.)", Elev);
		Check(std::abs(Az - 180.0) < 3.0, "в солнечный полдень солнце на юге", Az);
		Climate::SunPosition(355.0, 13.4, Elev, Az);
		Check(std::abs(Elev - 11.6) < 1.0, "зимой в полдень солнце низко (~11.5°)", Elev);
		Climate::SunPosition(355.0, 1.0, Elev, Az);
		Check(Elev < -30.0, "зимней ночью солнце глубоко под горизонтом", Elev);
		Climate::SunPosition(258.0, 8.0, Elev, Az);
		Check(Az > 80.0 && Az < 140.0, "утром солнце на востоке-юго-востоке", Az);
		Climate::SunPosition(172.0, 0.5, Elev, Az);
		Check(Elev > -14.0 && Elev < -5.0, "июньская полночь: светлые сумерки (солнце неглубоко)", Elev);
		double MoonElev = 0.0, MoonAz = 0.0, Phase = 0.0;
		bool bMoonUp = false;
		for (double D = 1.0; D < 30.0; D += 1.0)
		{
			Climate::MoonPosition(D, 1.0, MoonElev, MoonAz, Phase);
			bMoonUp |= MoonElev > 10.0 && Climate::MoonBrightness(Phase) > 0.5;
		}
		Check(bMoonUp, "за месяц бывают ночи с высокой яркой луной", 1.0);
		Check(Climate::TemperatureC(15.0, 14.0, 0.2) < -10.0, "январь — мороз", Climate::TemperatureC(15.0, 14.0, 0.2));
		Check(Climate::TemperatureC(196.0, 15.0, 0.2) > 20.0, "июльский день — тепло", Climate::TemperatureC(196.0, 15.0, 0.2));
		const SeasonLook Summer = Climate::Season(190.0);
		const SeasonLook Autumn = Climate::Season(268.0);
		const SeasonLook Winter = Climate::Season(20.0);
		Check(Summer.Leaves > 0.99 && Summer.Yellow < 0.01 && Summer.GroundSnow < 0.01, "лето: зелёная листва, без снега", Summer.Leaves);
		Check(Autumn.Yellow > 0.8 && Autumn.LeafFall > 0.9 && Autumn.Leaves > 0.3, "конец сентября: золотая осень, листопад", Autumn.Yellow);
		Check(Winter.Leaves < 0.01 && Winter.GroundSnow > 0.99, "зима: голые берёзы, снег", Winter.GroundSnow);
		Check(Climate::Season(330.0).GroundSnow > 0.9 && Climate::Season(100.0).GroundSnow > 0.2, "снег с ноября по апрель", Climate::Season(100.0).GroundSnow);

		// Долгий прогон: погода меняется, все виды встречаются, дороги мокнут и сохнут.
		ClimateState St;
		St.DayOfYear = 250.0;
		int Seen[static_cast<int>(Weather::Count)] = {};
		double MaxWet = 0.0, MinWet = 1.0;
		int Changes = 0;
		Weather Prev = St.Current;
		double Hours = 10.0;
		for (int K = 0; K < 30 * 24 * 12; ++K)
		{
			Climate::Step(St, Hours, 1.0 / 12.0);
			Hours += 1.0 / 12.0;
			if (Hours >= 24.0)
			{
				Hours -= 24.0;
			}
			Seen[static_cast<int>(St.Current)]++;
			Changes += St.Current != Prev ? 1 : 0;
			Prev = St.Current;
			MaxWet = std::max(MaxWet, St.Wetness);
			MinWet = std::min(MinWet, St.Wetness);
			const WeatherMix Mx = Climate::Mix(St, Hours);
			if (Mx.Cloud < 0.0 || Mx.Cloud > 1.0 || Mx.Rain < 0.0 || Mx.Snow < 0.0 || Mx.Sun < 0.0 || Mx.Sun > 1.0)
			{
				Check(false, "смесь погоды в пределах 0..1", Mx.Cloud);
				break;
			}
		}
		int Kinds = 0;
		for (int V : Seen)
		{
			Kinds += V > 0 ? 1 : 0;
		}
		Check(Kinds == static_cast<int>(Weather::Count), "за месяц встречается вся погода", Kinds);
		Check(Changes > 60 && Changes < 600, "погода меняется раз в несколько часов", Changes);
		Check(MaxWet > 0.9 && MinWet < 0.05, "дороги мокнут под дождём и высыхают", MaxWet);
		ClimateState Winter2;
		Winter2.DayOfYear = 20.0;
		Climate::Force(Winter2, Weather::Storm);
		for (int K = 0; K < 24; ++K)
		{
			Climate::Step(Winter2, 12.0, 0.1);
			Winter2.HoursLeft = 5.0;
		}
		const WeatherMix Blizzard = Climate::Mix(Winter2, 12.0);
		Check(Blizzard.Snow > 0.9 && Blizzard.Rain < 0.01, "зимой осадки — снегом (метель)", Blizzard.Snow);
		Check(Winter2.SnowCover > 0.5, "в метель ложится свежий снег", Winter2.SnowCover);
	}

	// --- Дальний рельеф и города ---
	{
		std::printf("\nЛандшафт\n");
		Route R;
		R.Build(NovosibirskKemerovo(2026));
		const auto T0 = std::chrono::steady_clock::now();
		Landscape L;
		L.Build(R);
		const double BuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
		std::printf("  построен за %.0f мс\n", BuildMs);
		Check(L.Settlements().size() >= 5, "города: Новосибирск, Мошково, Болотное, Юрга, Топки, Кемерово", static_cast<double>(L.Settlements().size()));
		const Settlement* Kemerovo = nullptr;
		size_t MaxBuildings = 0;
		bool bOffRoad = true;
		bool bNoOverlap = true;
		for (const Settlement& T : L.Settlements())
		{
			std::printf("  %-26s размер %d, зданий %zu\n", T.Name.c_str(), T.Size, T.Buildings.size());
			if (T.Name.find("Кемерово") != std::string::npos)
			{
				Kemerovo = &T;
			}
			MaxBuildings = std::max(MaxBuildings, T.Buildings.size());
			for (size_t I = 0; I < T.Buildings.size(); ++I)
			{
				const Building& B = T.Buildings[I];
				double S = 0.0, Tt = 0.0, D = 0.0;
				L.Nearest(B.X, B.Y, S, Tt, D);
				bOffRoad &= D > 30.0;
				for (size_t J = I + 1; J < T.Buildings.size(); ++J)
				{
					const Building& C = T.Buildings[J];
					bNoOverlap &= std::hypot(B.X - C.X, B.Y - C.Y) > 0.5 * (std::hypot(B.Length, B.Width) + std::hypot(C.Length, C.Width)) + 1.0;
				}
			}
		}
		Check(Kemerovo != nullptr, "Кемерово застроено", 1.0);
		if (Kemerovo)
		{
			int High = 0, Chimneys = 0, Churches = 0;
			for (const Building& B : Kemerovo->Buildings)
			{
				High += B.Floors >= 9 && (B.Kind == BuildingKind::Panel9 || B.Kind == BuildingKind::Tower) ? 1 : 0;
				Chimneys += B.Kind == BuildingKind::Chimney ? 1 : 0;
				Churches += B.Kind == BuildingKind::Church ? 1 : 0;
			}
			Check(Kemerovo->Buildings.size() > 150, "Кемерово — сотни домов", static_cast<double>(Kemerovo->Buildings.size()));
			Check(High > 40, "многоэтажки в Кемерово (силуэт города)", High);
			Check(Chimneys >= 2 && Churches == 1, "заводские трубы и храм", Chimneys);
		}
		Check(bOffRoad, "дома не стоят на дороге", 1.0);
		Check(bNoOverlap, "дома не пересекаются", 1.0);

		// Дальний рельеф у дороги ниже настоящей земли, вдали — холмы.
		bool bUnder = true;
		double MaxRelief = 0.0;
		for (double S = 500.0; S < R.Length(); S += 997.0)
		{
			for (double T : {-250.0, -60.0, 0.0, 60.0, 250.0})
			{
				double X = 0.0, Y = 0.0;
				R.ToWorld(S, T, X, Y);
				bUnder &= L.FarHeight(X, Y) <= R.GroundHeight(S, T) - 1.0;
			}
			double X = 0.0, Y = 0.0;
			R.ToWorld(S, 2500.0, X, Y);
			MaxRelief = std::max(MaxRelief, std::abs(L.FarHeight(X, Y) - R.Sample(S).Z));
		}
		Check(bUnder, "дальний рельеф спрятан под придорожной землёй", 1.0);
		Check(MaxRelief > 15.0 && MaxRelief < 120.0, "вдали холмы (перепад 15–120 м)", MaxRelief);
		const auto T1 = std::chrono::steady_clock::now();
		double Sum = 0.0;
		for (int K = 0; K < 20000; ++K)
		{
			Sum += L.FarHeight(static_cast<double>(K % 200) * 150.0, static_cast<double>(K / 200) * 150.0);
		}
		const double QueryUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - T1).count() / 20000.0;
		std::printf("  высота дальнего рельефа: %.1f мкс на точку (%f)\n", QueryUs, Sum * 0.0);
		Check(QueryUs < 60.0, "дальний рельеф считается быстро", QueryUs);
		double SouthMax = 0.0, NorthMax = 0.0;
		for (double Az = 0.0; Az < 360.0; Az += 2.0)
		{
			const double H = L.RidgeHeight(Az, 2, 0.0, 20000.0);
			if (Az > 150.0 && Az < 220.0)
			{
				SouthMax = std::max(SouthMax, H);
			}
			else if (Az < 30.0 || Az > 330.0)
			{
				NorthMax = std::max(NorthMax, H);
			}
		}
		Check(SouthMax > NorthMax + 40.0, "горы на юге (Салаир) выше, чем на севере", SouthMax - NorthMax);
	}

	std::printf("\n%d проверок, %d провалено\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
