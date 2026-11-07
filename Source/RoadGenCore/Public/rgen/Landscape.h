// Peregon — RoadGenCore
// Всё, что дальше придорожной полосы: дальний рельеф до горизонта (холмы, лесные массивы, поля),
// силуэты гор на горизонте и застройка городов (панельные дома, высотки, заводские трубы, храмы).
// Строится один раз по готовому маршруту; детерминированно по сиду.
#pragma once

#include "rgen/Export.h"
#include "rgen/Route.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rgen
{
enum class BuildingKind : uint8_t
{
	Panel5,     // пятиэтажка («хрущёвка»)
	Panel9,     // девятиэтажка
	Tower,      // точечная высотка 12–17 этажей
	Brick,      // кирпичный дом 3–4 этажа, «сталинка»
	Industrial, // цех
	Chimney,    // заводская труба
	Church,     // храм с куполами
	Mast,       // телевышка / мачта связи
	Garages,    // ряд гаражей (гаражный кооператив)
	Shop,       // магазин с вывеской: десятки видов и вывесок (номер вывески — Floors)
	Playground, // детская площадка во дворе
	School,       // школа: корпус с крыльями, стадион
	Kindergarten, // детский сад: два этажа, веранды на участке
	CultureHouse, // дом культуры с колоннами
	Admin,        // администрация с флагом
	Clinic,       // поликлиника
	Monument,     // мемориал: площадь, обелиск со звездой, вечный огонь
	WaterTower,   // водонапорная башня
	Kiosk,        // киоск или бочка кваса (вид — Floors)
	Market,       // рынок: ряды прилавков под навесами
	Count
};

struct RGEN_API Building
{
	BuildingKind Kind = BuildingKind::Panel5;
	double X = 0.0;      // центр основания, м
	double Y = 0.0;
	double Z = 0.0;      // земля под зданием (минимум по углам)
	double Yaw = 0.0;    // рад, направление длинной стороны
	double Length = 60.0;
	double Width = 12.0;
	int Floors = 5;
	uint32_t Seed = 0;

	double Height() const;
};

// Улица (дворовый проезд) — отрезок по сетке кварталов, концы на земле.
struct Street
{
	double X0 = 0.0, Y0 = 0.0, Z0 = 0.0;
	double X1 = 0.0, Y1 = 0.0, Z1 = 0.0;
};

struct Settlement
{
	std::string Name;
	double S = 0.0;      // где дорога ближе всего к центру
	double X = 0.0;
	double Y = 0.0;
	double Radius = 300.0;
	int Size = 0;        // 0 — посёлок с парой пятиэтажек, 1 — город, 2 — областной центр
	bool Real = false;   // настоящий город из OSM (rgen/City.h): своих домов не строим
	std::vector<Building> Buildings;
	std::vector<Street> Streets;
};

class RGEN_API Landscape
{
public:
	// Настоящий город из OpenStreetMap (вызывать до Build): процедурного города с этим именем не будет,
	// а дальний рельеф в его границах (с запасом) опускается ниже уровня города BaseZ.
	void SetRealCity(const std::string& Name, double MinX, double MinY, double MaxX, double MaxY, double BaseZ);
	void Build(const Route& InRoute);
	// Точка в границах настоящего города (с запасом Margin, м).
	bool InRealCity(double X, double Y, double Margin) const;

	// Ближайшая точка дороги (по разреженной копии осевой линии, шаг 25 м): S, смещение T и расстояние.
	void Nearest(double X, double Y, double& OutS, double& OutT, double& OutDist) const;
	// Высота дальнего рельефа. Вдоль дороги (до 300 м) — чуть ниже настоящей земли, чтобы не торчать сквозь неё.
	double FarHeight(double X, double Y) const;
	// Настоящая земля: у дороги — та, что строят чанки, дальше — дальний рельеф.
	double GroundAt(double X, double Y) const;
	// Полог леса над рельефом (для дальних массивов), м.
	double CanopyHeight(double X, double Y) const;
	// Густота леса 0..1 (та же, по которой расставлены деревья у дороги).
	double ForestDensity(double X, double Y) const;
	// Точка внутри здания (с запасом Margin, м) — чтобы деревья, трава и столбы не росли сквозь дома.
	bool InsideBuilding(double X, double Y, double Margin) const;
	// 0..1 — насколько точка внутри города.
	double UrbanMask(double X, double Y) const;
	// Силуэт гор на горизонте: высота (м) над уровнем дороги для азимута (° от севера по часовой).
	// Layer 0 — ближняя гряда холмов, 1 — дальняя, 2 — горы на юге и востоке (Салаир, Кузнецкий Алатау).
	double RidgeHeight(double AzimuthDeg, int Layer, double ViewerX, double ViewerY) const;

	const std::vector<Settlement>& Settlements() const { return Towns; }
	void Bounds(double& MinX, double& MinY, double& MaxX, double& MaxY) const;

private:
	void BuildCity(Settlement& Town);
	double FarHeightRaw(double X, double Y) const;
	double OuterHeight(double X, double Y, double S, double D) const;
	bool Blocked(double X, double Y, double Radius, const std::vector<Building>& Placed) const;

	const Route* R = nullptr;
	struct Pt
	{
		double X, Y, S, Z;
	};
	std::vector<Pt> Line;
	// Ускорение поиска ближайшего отрезка: корзины 400 × 400 м.
	std::vector<std::vector<uint32_t>> Buckets;
	int BucketsX = 0, BucketsY = 0;
	double BucketOx = 0.0, BucketOy = 0.0;
	static constexpr double BucketSize = 400.0;
	std::vector<Settlement> Towns;
	struct RealCityArea
	{
		std::string Name;
		double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0, Z = 0.0;
	};
	std::vector<RealCityArea> RealCities;
	double BMinX = 0.0, BMinY = 0.0, BMaxX = 0.0, BMaxY = 0.0;
	uint32_t Seed = 0;
};
} // namespace rgen
