// Peregon — RoadGenCore
// Климат юга Западной Сибири (55° с. ш.): положение солнца и луны по дате, температура, времена года,
// смена погоды. Чистый C++ — проверяется тестами, в Unreal только применяется к свету, туману и материалам.
#pragma once

#include "rgen/Export.h"

#include <cstdint>

namespace rgen
{
enum class Weather : uint8_t
{
	Clear,    // ясно
	Cloudy,   // переменная облачность
	Overcast, // пасмурно
	Rain,     // дождь (зимой — снег)
	Storm,    // ливень с ветром (зимой — метель)
	Fog,      // туман
	Count
};

RGEN_API const char* WeatherName(Weather W, bool bFrozen);

// Что видит игрок: смешанные параметры текущей погоды.
struct WeatherMix
{
	double Cloud = 0.2;   // 0..1 покрытие неба
	double Rain = 0.0;    // 0..1 интенсивность дождя
	double Snow = 0.0;    // 0..1 интенсивность снегопада
	double Fog = 0.0;     // 0..1 плотность тумана сверх обычной дымки
	double Wind = 0.3;    // 0..1 сила ветра
	double Sun = 1.0;     // 0..1 доля прямого солнца, доходящая до земли
};

// Вид природы в этот день года.
struct SeasonLook
{
	double Leaves = 1.0;       // 0..1 листва на берёзах (0 — голые ветки)
	double Yellow = 0.0;       // 0..1 желтизна листвы
	double LeafFall = 0.0;     // 0..1 насколько активно опадают листья
	double DryGrass = 0.0;     // 0..1 сухая (жёлто-бурая) трава
	double GroundSnow = 0.0;   // 0..1 снежный покров «по сезону»
};

struct ClimateState
{
	double DayOfYear = 258.0;   // 1..365, дробная часть — время суток не учитывает
	Weather Current = Weather::Clear;
	Weather Next = Weather::Clear;
	double Blend = 1.0;         // 0..1 переход Current → Next
	double HoursLeft = 3.0;     // до следующей смены погоды
	double Wetness = 0.0;       // 0..1 мокрые дороги и крыши
	double SnowCover = 0.0;     // 0..1 свежий снег поверх сезонного
	double WindDirDeg = 240.0;  // откуда дует (0 — с севера, 90 — с востока)
	uint32_t Seed = 0x2026u;
};

class RGEN_API Climate
{
public:
	static constexpr double LatitudeDeg = 55.0;
	// Местное время Новосибирска и Кемерово (UTC+7) опережает солнечное примерно на 1 ч 25 мин.
	static constexpr double SolarOffsetHours = 1.4;

	// Высота солнца над горизонтом (°) и азимут (° от севера по часовой).
	static void SunPosition(double DayOfYear, double Hours, double& OutElevationDeg, double& OutAzimuthDeg);
	// Луна: упрощённо — напротив солнца со сдвигом по фазе. Phase 0 — новолуние, 0.5 — полнолуние.
	static void MoonPosition(double DayOfYear, double Hours, double& OutElevationDeg, double& OutAzimuthDeg, double& OutPhase);
	// Освещённость от луны относительно полнолуния (0..1).
	static double MoonBrightness(double Phase);

	// Температура воздуха, °C (средняя по сезону + суточный ход, облака сглаживают).
	static double TemperatureC(double DayOfYear, double Hours, double Cloud);
	static SeasonLook Season(double DayOfYear);
	// Название времени года.
	static const char* SeasonName(double DayOfYear);

	// Параметры одной погоды в чистом виде.
	static WeatherMix Pure(Weather W);
	// Текущая смесь (с переходом), осадки — дождём или снегом по температуре.
	static WeatherMix Mix(const ClimateState& State, double Hours);

	// Шаг модели: время идёт, погода меняется, дороги мокнут и сохнут, снег ложится и тает.
	static void Step(ClimateState& State, double Hours, double DtHours);
	// Принудительно сменить погоду (переход за ~20 игровых минут).
	static void Force(ClimateState& State, Weather W);
	// Случайная следующая погода с учётом сезона.
	static Weather PickNext(Weather From, double DayOfYear, uint32_t Random);
};
} // namespace rgen
