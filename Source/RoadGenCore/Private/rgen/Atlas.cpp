// Peregon — RoadGenCore
#include "rgen/Atlas.h"

#include <cmath>

namespace rgen
{
namespace
{
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kMetersPerDegLat = 111320.0;

struct Place
{
	double Lat;
	double Lon;
	double Km;        // километраж Р-255 от Новосибирска (по съездам; для сёл — оценка)
	double ElevM;     // высота над морем, м (реальная)
	PoiType Type;
	const char* Name;
	const char* Region; // новая область начинается с этой точки
	int Side;
};

// Порядок — как на трассе: Новосибирск → Мочище → Сокур → Мошково → Станционно-Ояшинский → Болотное →
// граница → Юрга → Опарино → Симаново → Рассвет → Терехино → Октябрьский → Топки → Кемерово.
const Place kR255[] = {
	{55.105, 82.955, 0.0, 150.0, PoiType::Town, "Новосибирск", nullptr, 1},
	{55.140, 82.995, 8.0, 135.0, PoiType::Waypoint, "", nullptr, 1},
	{55.170, 83.045, 16.0, 140.0, PoiType::Village, "Мочище", nullptr, 1},
	{55.192, 83.190, 27.0, 165.0, PoiType::Waypoint, "", nullptr, 1},
	{55.205, 83.330, 36.0, 170.0, PoiType::Village, "Сокур", nullptr, 1},
	{55.262, 83.480, 48.0, 160.0, PoiType::Waypoint, "", nullptr, 1},
	{55.307, 83.611, 59.0, 160.0, PoiType::Town, "Мошково", nullptr, 1},
	{55.400, 83.730, 72.0, 150.0, PoiType::Waypoint, "", nullptr, 1},
	{55.482, 83.840, 84.0, 150.0, PoiType::Village, "Станционно-Ояшинский", nullptr, 1},
	{55.560, 84.060, 101.0, 155.0, PoiType::Waypoint, "", nullptr, 1},
	{55.673, 84.392, 126.0, 160.0, PoiType::Town, "Болотное", nullptr, 1},
	{55.695, 84.590, 140.0, 150.0, PoiType::Border, "Кемеровская область — Кузбасс", "Кемеровская область — Кузбасс", 1},
	{55.705, 84.720, 149.0, 140.0, PoiType::Waypoint, "", nullptr, 1},
	{55.713, 84.933, 161.0, 120.0, PoiType::Town, "Юрга", nullptr, 1},
	{55.640, 85.080, 175.0, 160.0, PoiType::Village, "Опарино", nullptr, 1},
	{55.575, 85.190, 186.0, 180.0, PoiType::Village, "Симаново", nullptr, 1},
	{55.505, 85.300, 197.0, 190.0, PoiType::Village, "Рассвет", nullptr, 1},
	{55.440, 85.395, 207.0, 200.0, PoiType::Village, "Терехино", nullptr, 1},
	{55.365, 85.500, 217.0, 210.0, PoiType::Village, "Октябрьский", nullptr, 1},
	{55.276, 85.613, 227.0, 220.0, PoiType::Town, "Топки", nullptr, 1},
	{55.300, 85.800, 238.0, 200.0, PoiType::Waypoint, "", nullptr, 1},
	{55.355, 86.087, 255.0, 140.0, PoiType::Finish, "Кемерово", nullptr, 1},
};

// Сервисы — названия вымышленные, места — примерно там, где на трассе есть АЗС и кафе.
const ServiceSpec kR255Services[] = {
	{6.0, PoiType::Fuel, 1, "АЗС «Сибирь»"},
	{31.0, PoiType::Cafe, 1, "Кафе «У трассы»"},
	{52.0, PoiType::Fuel, -1, "АЗС «Обь»"},
	{78.0, PoiType::Cafe, -1, "Кафе «Привал»"},
	{110.0, PoiType::Fuel, 1, "АЗС «Ояш-Ойл»"},
	{149.0, PoiType::Fuel, -1, "АЗС «Кузбасс»"},
	{158.0, PoiType::Cafe, 1, "Кафе «Шалай»"},
	{192.0, PoiType::Fuel, 1, "АЗС «Таёжная»"},
	{222.0, PoiType::Cafe, -1, "Кафе «Топки»"},
	{243.0, PoiType::Fuel, 1, "АЗС «Кемерово-Нефть»"},
};
} // namespace

GeoPoint NovosibirskKemerovoOrigin() { return GeoPoint{55.105, 82.955}; }

void GeoToGame(const GeoPoint& Origin, double Lat, double Lon, double& OutX, double& OutY)
{
	const double North = (Lat - Origin.Lat) * kMetersPerDegLat;
	const double East = (Lon - Origin.Lon) * kMetersPerDegLat * std::cos(Origin.Lat * kDegToRad);
	OutX = North * kGameScale;
	OutY = East * kGameScale;
}

void GameToGeo(const GeoPoint& Origin, double X, double Y, double& OutLat, double& OutLon)
{
	OutLat = Origin.Lat + X / kGameScale / kMetersPerDegLat;
	OutLon = Origin.Lon + Y / kGameScale / (kMetersPerDegLat * std::cos(Origin.Lat * kDegToRad));
}

RouteParams NovosibirskKemerovo(uint32_t Seed)
{
	RouteParams P;
	P.Seed = Seed;
	P.Name = "Р-255 «Сибирь»: Новосибирск — Кемерово";
	P.StartRegion = "Новосибирская область";
	P.KmPerMeter = 1.0 / (1000.0 * kGameScale);
	P.MinRadius = 300.0;
	P.WanderAmplitude = 0.16;
	P.HillAmplitude = 14.0;
	const GeoPoint Origin = NovosibirskKemerovoOrigin();
	const double Base = kR255[0].ElevM;
	for (const Place& Pl : kR255)
	{
		Anchor A;
		GeoToGame(Origin, Pl.Lat, Pl.Lon, A.X, A.Y);
		A.Z = (Pl.ElevM - Base) * kGameScale;
		A.Km = Pl.Km;
		A.Type = Pl.Type;
		A.Side = Pl.Side;
		A.Name = Pl.Name;
		A.Region = Pl.Region ? Pl.Region : "";
		P.Anchors.push_back(A);
	}
	for (const ServiceSpec& Spec : kR255Services)
	{
		P.Services.push_back(Spec);
	}
	return P;
}
} // namespace rgen
