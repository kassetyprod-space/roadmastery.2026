// Peregon — RoadGenCore
// Трасса: осевая линия дороги, профиль высот, рельеф вдоль дороги, точки интереса.
//
// Сейчас осевая линия процедурная: прямые → переходные кривые → дуги, как проектируют настоящие
// трассы (кривизна растёт плавно, без «изломов» руля). Позже сюда же придёт полилиния из
// OpenStreetMap (Tools/MapPipeline) — всё остальное (рельеф, обочины, сёла, стриминг) не изменится.
//
// Координаты в метрах. X, Y — план (как в Unreal: Y вправо от X), Z — вверх.
// S — расстояние вдоль дороги от начала, T — смещение поперёк (> 0 — вправо по ходу).
#pragma once

#include "rgen/Export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rgen
{
struct RouteParams
{
	uint32_t Seed = 1;
	double Length = 15000.0;      // м (15 км в игре ≈ 75 км в реальности при масштабе 1:5)
	double Step = 2.0;            // м, шаг таблицы осевой линии
	double MinRadius = 320.0;     // м, самый крутой поворот
	double MaxRadius = 1800.0;
	double MaxGrade = 0.06;       // 6% — предел для трассы такого класса
	double HillAmplitude = 24.0;  // м, высота холмов профиля
	double TerrainAmplitude = 26.0; // м, рельеф вдали от дороги
	double LaneWidth = 3.5;       // м, две полосы
	double ShoulderWidth = 2.0;   // м, гравийная обочина
};

struct RouteSample
{
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	double Heading = 0.0;   // рад, 0 — вдоль +X, растёт к +Y
	double Curvature = 0.0; // 1/м, > 0 — поворот вправо
	double Grade = 0.0;     // dZ/dS
};

enum class PoiType
{
	Town,    // начало маршрута
	Village,
	Fuel,
	Cafe,
	Finish
};

struct Poi
{
	PoiType Type = PoiType::Village;
	double S = 0.0;
	int Side = 1;          // сторона дороги для АЗС и кафе
	std::string Name;      // UTF-8
};

// Участок, где рельеф выравнивается до уровня дороги (площадка АЗС, улица села).
struct FlattenZone
{
	double S0 = 0.0;
	double S1 = 0.0;
	double TMin = 0.0; // со знаком: участок поперёк дороги [TMin, TMax]
	double TMax = 0.0;
	double Strength = 1.0;
};

class RGEN_API Route
{
public:
	void Build(const RouteParams& InParams);

	const RouteParams& Params() const { return P; }
	double Length() const { return TotalLength; }
	double RoadHalfWidth() const { return P.LaneWidth; }
	double ShoulderOuter() const { return P.LaneWidth + P.ShoulderWidth; }

	RouteSample Sample(double S) const;
	// Точка в плане по (S, T).
	void ToWorld(double S, double T, double& OutX, double& OutY) const;
	// Высота земли в точке (S, T): полотно, обочина, кювет, затем естественный рельеф.
	double GroundHeight(double S, double T) const;
	// Ближайшее S к точке плана. Hint < 0 — глобальный поиск.
	double FindNearestS(double X, double Y, double Hint = -1.0, double Window = 300.0) const;
	// Смещение T точки относительно дороги в найденном S.
	double LateralOffset(double X, double Y, double S) const;

	const std::vector<Poi>& Pois() const { return PoiList; }
	const std::vector<FlattenZone>& Flatten() const { return FlattenList; }
	// Первая точка интереса дальше S (или nullptr).
	const Poi* NextPoi(double S) const;

	double MinZ() const { return ZMin; }
	double MaxZ() const { return ZMax; }

private:
	void BuildPlan();
	void BuildProfile();
	void BuildPois();
	double NaturalNoise(double X, double Y) const;

	RouteParams P;
	std::vector<RouteSample> Samples;
	std::vector<Poi> PoiList;
	std::vector<FlattenZone> FlattenList;
	double TotalLength = 0.0;
	double ZMin = 0.0;
	double ZMax = 0.0;
};
} // namespace rgen
