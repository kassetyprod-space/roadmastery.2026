// Тесты генератора трассы: геометрия должна быть «проектной» — плавной, проезжей и повторяемой.
#include "rgen/Noise.h"
#include "rgen/Atlas.h"
#include "rgen/Route.h"
#include "rgen/Climate.h"
#include "rgen/City.h"
#include "rgen/Landscape.h"

#include <chrono>
#include <fstream>
#include <sstream>

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
		Check(R.Length() > 255000.0 * kGameScale * 0.9 && R.Length() < 255000.0 * kGameScale * 1.25, "длина ~128 км (1:2 от 255 км), м", R.Length());
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
			int Garages = 0, Shops = 0, Playgrounds = 0;
			for (const Building& B : Kemerovo->Buildings)
			{
				Garages += B.Kind == BuildingKind::Garages ? 1 : 0;
				Shops += B.Kind == BuildingKind::Shop ? 1 : 0;
				Playgrounds += B.Kind == BuildingKind::Playground ? 1 : 0;
			}
			std::printf("  Кемерово: гаражей %d, магазинов %d, площадок %d, проездов %zu\n", Garages, Shops, Playgrounds, Kemerovo->Streets.size());
			Check(Garages > 5 && Shops > 10 && Playgrounds > 5, "во дворах гаражи, магазины, площадки", Garages + Shops + Playgrounds);
			Check(Kemerovo->Streets.size() > 150, "дворовые проезды между кварталами", static_cast<double>(Kemerovo->Streets.size()));

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
		// Между узлами сетки 140 м (как в игре) дальний рельеф тоже не выше земли чанков.
		{
			double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0;
			L.Bounds(MinX, MinY, MaxX, MaxY);
			const double Ox = MinX - 6740.0;
			const double Oy = MinY - 6740.0;
			constexpr double Step = 140.0;
			int Above = 0, Tested = 0;
			double Worst = 0.0;
			Rng Rand(77);
			for (int K = 0; K < 4000; ++K)
			{
				const double S = Rand.Range(50.0, R.Length() - 50.0);
				const double T = Rand.Range(-285.0, 285.0);
				double X = 0.0, Y = 0.0;
				R.ToWorld(S, T, X, Y);
				const double Gx = std::floor((X - Ox) / Step), Gy = std::floor((Y - Oy) / Step);
				const double Fx = (X - Ox) / Step - Gx, Fy = (Y - Oy) / Step - Gy;
				const double X0 = Ox + Gx * Step, Y0 = Oy + Gy * Step;
				const double H00 = L.FarHeight(X0, Y0), H10 = L.FarHeight(X0 + Step, Y0), H01 = L.FarHeight(X0, Y0 + Step), H11 = L.FarHeight(X0 + Step, Y0 + Step);
				// Треугольники сетки делят квадрат по диагонали (1,0)–(0,1).
				const double H = Fx + Fy <= 1.0 ? H00 + (H10 - H00) * Fx + (H01 - H00) * Fy : H11 + (H01 - H11) * (1.0 - Fx) + (H10 - H11) * (1.0 - Fy);
				const double Ground = R.GroundHeight(S, T);
				++Tested;
				if (H > Ground - 0.05)
				{
					++Above;
					Worst = std::max(Worst, H - Ground);
				}
			}
			std::printf("  дальний рельеф выше земли: %d из %d (до %.2f м)\n", Above, Tested, Worst);
			Check(Above <= Tested / 500, "сетка дальнего рельефа не протыкает землю у дороги", Above);
		}
		// Деревья и трава не растут в домах.
		if (Kemerovo && !Kemerovo->Buildings.empty())
		{
			const Building& B = Kemerovo->Buildings.front();
			Check(L.InsideBuilding(B.X, B.Y, 0.0), "центр дома — внутри дома", 1.0);
		}
		Check(MaxRelief > 15.0 && MaxRelief < 120.0, "вдали холмы (перепад 15–120 м)", MaxRelief);
		// Двор у старта свободен от домов города и ровный.
		{
			int Inside = 0;
			double Spread = 0.0;
			const double Z0 = R.GroundHeight(0.5 * (StartYardS0 + StartYardS1), 0.5 * (StartYardTMin + StartYardTMax));
			for (double S = StartYardS0 + 5.0; S < StartYardS1 - 5.0; S += 5.0)
			{
				for (double T = StartYardTMin + 3.0; T < StartYardTMax - 3.0; T += 5.0)
				{
					double X = 0.0, Y = 0.0;
					R.ToWorld(S, T, X, Y);
					Inside += L.InsideBuilding(X, Y, 1.0) ? 1 : 0;
					Spread = std::max(Spread, std::abs(R.GroundHeight(S, T) - Z0));
				}
			}
			Check(Inside == 0, "во дворе у старта нет домов города", Inside);
			Check(Spread < 3.0, "двор у старта ровный (перепад < 3 м)", Spread);
		}
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

	// --- Настоящий город вместо процедурного ---
	{
		rgen::RouteParams P = rgen::NovosibirskKemerovo(7u);
		rgen::Route Rt;
		Rt.Build(P);
		rgen::Landscape L;
		const double Z0 = Rt.Sample(0.0).Z;
		L.SetRealCity("Новосибирск", -6000.0, -3000.0, 0.0, 3000.0, Z0);
		L.Build(Rt);
		bool bReal = false;
		size_t Houses = 1;
		for (const rgen::Settlement& T : L.Settlements())
		{
			if (T.Name == "Новосибирск")
			{
				bReal = T.Real;
				Houses = T.Buildings.size();
			}
		}
		Check(bReal && Houses == 0, "Новосибирск из OSM: процедурный город не строится", static_cast<double>(Houses));
		const double Far = L.FarHeight(-3000.0, 0.0);
		Check(Far <= Z0 - 3.9, "дальний рельеф под городом ниже его земли, м", Far - Z0);
	}

	// --- Город из OSM (синтетическая выгрузка Tools/MapPipeline/make_test_city.py) ---
	{
		std::printf("\nГород из .city (тестовые данные)\n");
		std::ifstream F(std::string(PEREGON_TEST_DATA) + "/test.city", std::ios::binary);
		std::stringstream Buf;
		Buf << F.rdbuf();
		rgen::CityData City;
		std::string Err;
		const bool bOk = City.Parse(Buf.str(), &Err);
		Check(bOk, "файл .city читается", bOk ? 1.0 : 0.0);
		if (!bOk)
		{
			std::printf("  ошибка: %s\n", Err.c_str());
		}
		Check(City.Ways.size() > 20 && City.Buildings.size() > 100 && !City.Water.empty() && !City.Landmarks.empty(), "дороги, дома, вода, достопримечательности",
			static_cast<double>(City.Ways.size()));
		rgen::CityBuildOptions Opt;
		Opt.Keepout.push_back(rgen::CityPt{300.0, 300.0});
		const auto T0 = std::chrono::steady_clock::now();
		const rgen::CityResult Res = rgen::BuildCity(City, Opt);
		const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
		std::printf("  построен за %.0f мс: рёбер %d, перекрёстков %d, мостов %d, домов %d, тайлов %zu, треугольников %zu, реквизита %zu\n", Ms, Res.Edges,
			Res.Junctions, Res.Bridges, Res.BuildingsBuilt, Res.Tiles.size(), Res.TriangleCount(), Res.Props.size());
		Check(Res.Junctions >= 10, "перекрёстки (шт.)", Res.Junctions);
		Check(Res.Bridges >= 2, "мост через реку и путепровод", Res.Bridges);
		double MaxH = 0.0;
		for (double Hn : Res.NodeHeight)
		{
			MaxH = std::max(MaxH, Hn);
		}
		Check(MaxH > 6.0 && MaxH < 9.5, "путепровод и мост подняты (6.5 и 9 м)", MaxH);
		bool bFinite = true;
		size_t Indices = 0;
		bool bInRange = true;
		for (const rgen::CityTile& T : Res.Tiles)
		{
			for (const rgen::CitySection& S : T.Sections)
			{
				for (float V : S.P)
				{
					bFinite &= std::isfinite(V);
				}
				for (float V : S.N)
				{
					bFinite &= std::isfinite(V);
				}
				for (int32_t I : S.I)
				{
					bInRange &= I >= 0 && static_cast<size_t>(I) < S.VertexCount();
				}
				Indices += S.I.size();
			}
		}
		Check(bFinite && bInRange && Indices % 3 == 0, "геометрия без NaN и битых индексов", 1.0);
		int Lights = 0, Lamps = 0, Landmarks = 0, Signs = 0;
		for (const rgen::CityProp& P : Res.Props)
		{
			Lights += P.Kind == rgen::CityPropKind::TrafficLight;
			Lamps += P.Kind == rgen::CityPropKind::StreetLamp;
			Landmarks += P.Kind == rgen::CityPropKind::Landmark;
			Signs += P.Kind == rgen::CityPropKind::SignYield || P.Kind == rgen::CityPropKind::SignRoundabout || P.Kind == rgen::CityPropKind::SignCrossing;
		}
		std::printf("  светофоров %d, фонарей %d, знаков %d, достопримечательностей %d\n", Lights, Lamps, Signs, Landmarks);
		Check(Lights >= 8 && Lamps > 50 && Signs >= 4 && Landmarks >= 1, "светофоры, фонари, знаки у кольца и переходов", Lights);
		Check(Ms < 3000.0, "город строится быстро, мс", Ms);
	}

	// --- Настоящий Новосибирск (Content/Data/Cities/novosibirsk.city, если он есть в репозитории) ---
	{
		std::ifstream F(std::string(PEREGON_TEST_DATA) + "/../../../Content/Data/Cities/novosibirsk.city", std::ios::binary);
		if (F)
		{
			std::printf("\nНовосибирск из OSM\n");
			std::stringstream Buf;
			Buf << F.rdbuf();
			rgen::CityData City;
			std::string Err;
			const bool bOk = City.Parse(Buf.str(), &Err);
			Check(bOk && City.Buildings.size() > 10000 && City.Ways.size() > 1000, "файл читается: домов и улиц тысячи", static_cast<double>(City.Buildings.size()));
			bool bOktyabrsky = false;
			for (const rgen::CityWay& W : City.Ways)
			{
				bOktyabrsky |= W.Style == "oktyabrsky" && W.Bridge;
			}
			Check(bOktyabrsky, "Октябрьский мост найден в данных", bOktyabrsky ? 1.0 : 0.0);
			rgen::Route Rt;
			Rt.Build(rgen::NovosibirskKemerovo(1));
			rgen::CityBuildOptions Opt;
			for (double S = 30.0; S < 4000.0; S += 25.0)
			{
				const rgen::RouteSample Smp = Rt.Sample(S);
				Opt.Keepout.push_back(rgen::CityPt{Smp.X, Smp.Y});
			}
			const auto T0 = std::chrono::steady_clock::now();
			const rgen::CityResult Res = rgen::BuildCity(City, Opt);
			const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
			std::printf("  построен за %.0f мс: перекрёстков %d, мостов %d, домов %d (убрано с трассы %d), проездов %d, треугольников %zu\n", Ms,
				Res.Junctions, Res.Bridges, Res.BuildingsBuilt, Res.BuildingsCleared, Res.Driveways, Res.TriangleCount());
			Check(Res.Junctions > 500 && Res.Bridges > 10, "развязки и мосты настоящего города", Res.Junctions);
			Check(Res.Driveways > 500, "дворовые проезды у многоэтажек", Res.Driveways);
			Check(Res.TriangleCount() < 4000000, "треугольников меньше 4 млн", static_cast<double>(Res.TriangleCount()));
			Check(Ms < 20000.0, "строится меньше 20 с, мс", Ms);
		}
	}

	std::printf("\n%d проверок, %d провалено\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
