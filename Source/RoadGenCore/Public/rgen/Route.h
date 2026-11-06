// Peregon — RoadGenCore
// Трасса: осевая линия дороги, профиль высот, рельеф вдоль дороги, точки интереса.
//
// Два режима осевой линии:
//  • процедурный: прямые → переходные кривые → дуги, как проектируют настоящие трассы;
//  • по опорным точкам (Anchors): реальные населённые пункты в масштабе 1:5 (см. rgen/Atlas.h),
//    дорога идёт от точки к точке с плавными переходными кривыми и естественными «гуляниями».
//    Позже сюда же можно подать полилинию из OpenStreetMap (Tools/MapPipeline) — как частые опорные точки.
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
enum class PoiType
{
	Town,    // город/райцентр (S = 0 — начало маршрута)
	Village,
	Fuel,
	Cafe,
	Finish,
	Shop,    // магазин у дороги (карты, мелочи)
	Border,  // граница области
	Waypoint, // точка формы дороги без названия
	Parts     // фирменный магазин автозапчастей (в городах)
};

// Опорная точка маршрута (в метрах игрового мира).
struct Anchor
{
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;     // высота, м (уже в масштабе игры)
	double Km = 0.0;    // реальный километраж трассы
	PoiType Type = PoiType::Waypoint;
	int Side = 1;
	std::string Name;   // UTF-8
	std::string Region; // область, к которой относится участок ПОСЛЕ этой точки (пусто — та же)
};

// Сервис у дороги, привязанный к реальному километражу.
struct ServiceSpec
{
	double Km = 0.0;
	PoiType Type = PoiType::Fuel;
	int Side = 1;
	std::string Name;
};

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
	double WanderAmplitude = 0.2; // рад, насколько дорога «гуляет» между опорными точками
	double KmPerMeter = 0.005;    // реальные км на метр игры (масштаб 1:5)
	std::string Name;             // название маршрута (UTF-8)
	std::vector<Anchor> Anchors;  // не пусто — режим по опорным точкам
	std::vector<ServiceSpec> Services;
	std::string StartRegion = "Новосибирская область";
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

struct Poi
{
	PoiType Type = PoiType::Village;
	double S = 0.0;
	int Side = 1;          // сторона дороги для АЗС и кафе
	std::string Name;      // UTF-8
	double RealKm = 0.0;   // километраж настоящей трассы
	int Region = 0;        // индекс в Regions()
};

// Участок маршрута в пределах одной области (для бумажных карт).
struct Region
{
	std::string Name;
	double S0 = 0.0;
	double S1 = 0.0;
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

// Двор у начала маршрута (справа от дороги): пятиэтажка, гаражный кооператив, помойка, разборка.
// Рельеф там ровный, города и леса там не строятся.
constexpr double StartYardS0 = 10.0;
constexpr double StartYardS1 = 140.0;
constexpr double StartYardTMin = 9.0;
constexpr double StartYardTMax = 82.0;

class RGEN_API Route
{
public:
	static FlattenZone StartYard();
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
	// Первая точка интереса дальше S (или nullptr). Точки формы и границы пропускаются.
	const Poi* NextPoi(double S) const;

	const std::vector<Region>& Regions() const { return RegionList; }
	int RegionAt(double S) const;
	// Километраж настоящей трассы в точке S (кусочно-линейно по опорным точкам).
	double RealKmAt(double S) const;
	double SAtRealKm(double Km) const;
	bool IsGuided() const { return !P.Anchors.empty(); }

	double MinZ() const { return ZMin; }
	double MaxZ() const { return ZMax; }

	// Естественный рельеф (−1..1) в точке плана — им же строится дальний рельеф (rgen/Landscape).
	double NaturalNoise(double X, double Y) const;

private:
	void BuildPlan();
	void BuildGuidedPlan();
	void BuildProfile();
	void BuildPois();
	void BuildGuidedPois();
	void AddFlatten(const Poi& Item);

	RouteParams P;
	std::vector<RouteSample> Samples;
	std::vector<Poi> PoiList;
	std::vector<FlattenZone> FlattenList;
	std::vector<Region> RegionList;
	std::vector<double> AnchorS; // S, в которой дорога прошла каждую опорную точку
	double TotalLength = 0.0;
	double ZMin = 0.0;
	double ZMax = 0.0;
};
} // namespace rgen
