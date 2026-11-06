#include "rgen/Climate.h"

#include "rgen/Noise.h"

#include <algorithm>
#include <cmath>

namespace rgen
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

double Clamp01(double V) { return std::min(1.0, std::max(0.0, V)); }
double Smooth(double E0, double E1, double X)
{
	const double T = Clamp01((X - E0) / (E1 - E0));
	return T * T * (3.0 - 2.0 * T);
}
double Lerpd(double A, double B, double T) { return A + (B - A) * T; }
double WrapDay(double D)
{
	double R = std::fmod(D - 1.0, 365.0);
	if (R < 0.0)
	{
		R += 365.0;
	}
	return R + 1.0;
}
// «Окно» внутри года: 0 вне [A, D], 1 в [B, C], плавные края. Дни можно задавать через новый год (A > D).
double Window(double Day, double A, double B, double C, double D)
{
	if (A > D)
	{
		const double Shifted = Day < A ? Day + 365.0 : Day;
		return Window(Shifted, A, B < A ? B + 365.0 : B, C < A ? C + 365.0 : C, D + 365.0);
	}
	if (Day <= A || Day >= D)
	{
		return 0.0;
	}
	if (Day < B)
	{
		return Smooth(A, B, Day);
	}
	if (Day > C)
	{
		return 1.0 - Smooth(C, D, Day);
	}
	return 1.0;
}
} // namespace

const char* WeatherName(Weather W, bool bFrozen)
{
	switch (W)
	{
	case Weather::Clear: return "Ясно";
	case Weather::Cloudy: return "Переменная облачность";
	case Weather::Overcast: return "Пасмурно";
	case Weather::Rain: return bFrozen ? "Снег" : "Дождь";
	case Weather::Storm: return bFrozen ? "Метель" : "Ливень";
	case Weather::Fog: return "Туман";
	default: return "?";
	}
}

void Climate::SunPosition(double DayOfYear, double Hours, double& OutElevationDeg, double& OutAzimuthDeg)
{
	// Склонение солнца (формула Купера) и часовой угол по солнечному времени.
	const double Decl = -23.44 * kDeg * std::cos(2.0 * kPi * (WrapDay(DayOfYear) + 10.0) / 365.0);
	const double Lat = LatitudeDeg * kDeg;
	const double HourAngle = (Hours - SolarOffsetHours - 12.0) * 15.0 * kDeg;
	const double SinAlt = std::sin(Lat) * std::sin(Decl) + std::cos(Lat) * std::cos(Decl) * std::cos(HourAngle);
	const double Alt = std::asin(std::max(-1.0, std::min(1.0, SinAlt)));
	// Азимут от севера по часовой: утром солнце на востоке (90°), в полдень на юге (180°).
	const double Az = std::atan2(std::sin(HourAngle), std::cos(HourAngle) * std::sin(Lat) - std::tan(Decl) * std::cos(Lat)) + kPi;
	OutElevationDeg = Alt / kDeg;
	OutAzimuthDeg = std::fmod(Az / kDeg + 360.0, 360.0);
}

void Climate::MoonPosition(double DayOfYear, double Hours, double& OutElevationDeg, double& OutAzimuthDeg, double& OutPhase)
{
	// Синодический месяц 29.53 сут. Фаза — доля месяца; луна «отстаёт» от солнца на фазу × 24 ч.
	const double Days = DayOfYear + Hours / 24.0;
	OutPhase = std::fmod(Days / 29.53 + 0.37, 1.0);
	// Полная луна стоит там, где солнце было бы через полгода (высоко зимой, низко летом).
	const double LagHours = OutPhase * 24.0;
	double SunAlt = 0.0;
	double SunAz = 0.0;
	SunPosition(DayOfYear + 182.5 * (1.0 - 2.0 * std::abs(OutPhase - 0.5)), Hours - LagHours, SunAlt, SunAz);
	OutElevationDeg = SunAlt;
	OutAzimuthDeg = SunAz;
}

double Climate::MoonBrightness(double Phase)
{
	// 0 в новолуние, 1 в полнолуние; освещённость растёт быстрее к полнолунию (эффект оппозиции).
	const double Lit = 0.5 * (1.0 - std::cos(2.0 * kPi * Phase));
	return std::pow(Lit, 1.6);
}

double Climate::TemperatureC(double DayOfYear, double Hours, double Cloud)
{
	// Новосибирск: январь ≈ −17 °C, июль ≈ +19 °C. Самая холодная пора — середина января.
	const double Annual = 1.0 - 18.0 * std::cos(2.0 * kPi * (WrapDay(DayOfYear) - 15.0) / 365.0);
	// Суточный ход: минимум перед рассветом, максимум в 15 ч; облака его гасят.
	const double Amplitude = Lerpd(6.0, 2.0, Clamp01(Cloud));
	const double Diurnal = Amplitude * std::cos(2.0 * kPi * (Hours - 15.0) / 24.0);
	return Annual + Diurnal;
}

SeasonLook Climate::Season(double DayOfYear)
{
	const double D = WrapDay(DayOfYear);
	SeasonLook L;
	// Берёзы распускаются в конце апреля — середине мая, желтеют с конца августа, голые к 20 октября.
	L.Leaves = Window(D, 112.0, 140.0, 262.0, 293.0);
	L.Yellow = Smooth(236.0, 272.0, D) * (D < 300.0 ? 1.0 : 0.0);
	L.LeafFall = Window(D, 240.0, 262.0, 280.0, 298.0);
	// Трава: зелёная с середины мая, сухая с конца августа до схода снега.
	L.DryGrass = 1.0 - Window(D, 118.0, 142.0, 232.0, 282.0);
	// Снег лежит с начала ноября до середины апреля.
	L.GroundSnow = Window(D, 296.0, 318.0, 88.0, 112.0);
	return L;
}

const char* Climate::SeasonName(double DayOfYear)
{
	const double D = WrapDay(DayOfYear);
	if (D < 60.0 || D >= 335.0)
	{
		return "Зима";
	}
	if (D < 152.0)
	{
		return "Весна";
	}
	if (D < 244.0)
	{
		return "Лето";
	}
	return "Осень";
}

WeatherMix Climate::Pure(Weather W)
{
	WeatherMix M;
	switch (W)
	{
	case Weather::Clear: M = {0.12, 0.0, 0.0, 0.0, 0.25, 1.0}; break;
	case Weather::Cloudy: M = {0.5, 0.0, 0.0, 0.05, 0.4, 0.8}; break;
	case Weather::Overcast: M = {0.92, 0.0, 0.0, 0.15, 0.4, 0.3}; break;
	case Weather::Rain: M = {0.97, 0.55, 0.0, 0.3, 0.5, 0.2}; break;
	case Weather::Storm: M = {1.0, 1.0, 0.0, 0.4, 0.95, 0.1}; break;
	case Weather::Fog: M = {0.65, 0.0, 0.0, 1.0, 0.05, 0.45}; break;
	default: break;
	}
	return M;
}

WeatherMix Climate::Mix(const ClimateState& State, double Hours)
{
	const WeatherMix A = Pure(State.Current);
	const WeatherMix B = Pure(State.Next);
	const double T = Smooth(0.0, 1.0, State.Blend);
	WeatherMix M;
	M.Cloud = Lerpd(A.Cloud, B.Cloud, T);
	M.Rain = Lerpd(A.Rain, B.Rain, T);
	M.Fog = Lerpd(A.Fog, B.Fog, T);
	M.Wind = Lerpd(A.Wind, B.Wind, T);
	M.Sun = Lerpd(A.Sun, B.Sun, T);
	// Осадки при морозе — снегом (с переходной полосой мокрого снега около нуля).
	const double Temp = TemperatureC(State.DayOfYear, Hours, M.Cloud);
	const double Frozen = 1.0 - Smooth(-1.0, 1.5, Temp);
	M.Snow = M.Rain * Frozen;
	M.Rain *= 1.0 - Frozen;
	return M;
}

Weather Climate::PickNext(Weather From, double DayOfYear, uint32_t Random)
{
	const double D = WrapDay(DayOfYear);
	// Весы переходов: летом чаще ясно и грозы, осенью — пасмурно и дожди, туманы — по утрам весной и осенью.
	const double Summer = Window(D, 130.0, 160.0, 230.0, 260.0);
	const double Autumn = Window(D, 235.0, 260.0, 300.0, 330.0);
	double W[static_cast<int>(Weather::Count)] = {
		3.0 + 2.0 * Summer,             // Clear
		3.0,                            // Cloudy
		2.0 + 2.0 * Autumn,             // Overcast
		1.4 + 1.2 * Autumn,             // Rain
		0.3 + 0.8 * Summer,             // Storm
		0.5 + 0.8 * Autumn};            // Fog
	// Погода меняется постепенно: из ясной не бывает сразу ливня.
	switch (From)
	{
	case Weather::Clear: W[static_cast<int>(Weather::Rain)] *= 0.2; W[static_cast<int>(Weather::Storm)] *= 0.1; break;
	case Weather::Rain: W[static_cast<int>(Weather::Clear)] *= 0.3; break;
	case Weather::Storm: W[static_cast<int>(Weather::Clear)] *= 0.2; W[static_cast<int>(Weather::Fog)] *= 0.3; break;
	case Weather::Fog: W[static_cast<int>(Weather::Storm)] *= 0.1; break;
	default: break;
	}
	W[static_cast<int>(From)] *= 0.35; // скорее сменится, чем повторится
	double Total = 0.0;
	for (double V : W)
	{
		Total += V;
	}
	double Pick = HashToUnit(Random) * Total;
	for (int I = 0; I < static_cast<int>(Weather::Count); ++I)
	{
		Pick -= W[I];
		if (Pick <= 0.0)
		{
			return static_cast<Weather>(I);
		}
	}
	return Weather::Cloudy;
}

void Climate::Force(ClimateState& State, Weather W)
{
	State.Current = State.Blend >= 0.5 ? State.Next : State.Current;
	State.Next = W;
	State.Blend = 0.0;
	State.HoursLeft = 2.0 + HashToUnit(Hash(State.Seed++, 0x51u)) * 3.0;
}

void Climate::Step(ClimateState& State, double Hours, double DtHours)
{
	if (DtHours <= 0.0)
	{
		return;
	}
	// Переход длится ~20 игровых минут.
	if (State.Blend < 1.0)
	{
		State.Blend = std::min(1.0, State.Blend + DtHours / 0.35);
		if (State.Blend >= 1.0)
		{
			State.Current = State.Next;
		}
	}
	State.HoursLeft -= DtHours;
	if (State.HoursLeft <= 0.0 && State.Blend >= 1.0)
	{
		const uint32_t R = Hash(State.Seed++, 0xC11Au);
		State.Next = PickNext(State.Current, State.DayOfYear, R);
		State.Blend = State.Next == State.Current ? 1.0 : 0.0;
		State.HoursLeft = 1.5 + HashToUnit(Hash(R, 7u)) * 4.5;
	}
	// Ветер медленно поворачивает.
	State.WindDirDeg = std::fmod(State.WindDirDeg + DtHours * 6.0 * Noise1D(State.DayOfYear * 3.0 + Hours * 0.2, State.Seed ^ 0xA17Du) + 360.0, 360.0);

	const WeatherMix M = Mix(State, Hours);
	const double Temp = TemperatureC(State.DayOfYear, Hours, M.Cloud);
	double SunElev = 0.0;
	double SunAz = 0.0;
	SunPosition(State.DayOfYear, Hours, SunElev, SunAz);
	// Мокнет за ~20 минут дождя, сохнет за 2–6 часов (быстрее на солнце, медленнее в мороз и ночью).
	const double DryRate = (0.15 + 0.35 * M.Sun * Smooth(-2.0, 20.0, SunElev)) * Smooth(-5.0, 8.0, Temp);
	State.Wetness = Clamp01(State.Wetness + DtHours * (M.Rain * 3.0 - DryRate * (1.0 - M.Rain)));
	// Свежий снег ложится за час метели, тает в оттепель.
	const double Melt = std::max(0.0, Temp - 1.0) * 0.08;
	State.SnowCover = Clamp01(State.SnowCover + DtHours * (M.Snow * 0.9 - Melt));
}
} // namespace rgen
