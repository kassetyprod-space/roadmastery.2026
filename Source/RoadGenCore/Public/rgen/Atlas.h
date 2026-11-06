// Peregon — RoadGenCore
// Реальные маршруты. Координаты пунктов — широта/долгота (WGS-84), проецируются в мир игры в масштабе 1:5:
// X — на север, Y — на восток (в Unreal сверху: север вверх, восток вправо). Высоты тоже делятся на 5,
// иначе уклоны вышли бы впятеро круче настоящих.
//
// Новосибирск → Кемерово по трассе Р-255 «Сибирь» (~255 км → ~51 км в игре). Порядок пунктов и
// километраж съездов — по открытым данным (Мошково — 59 км, Болотное — 126, Топки — 227, Кемерово — 249+).
// Точки сёл между Юргой и Топками приблизительные; точную геометрию дороги можно выгрузить из
// OpenStreetMap скриптом Tools/MapPipeline/fetch_route.py и подставить как частые опорные точки.
#pragma once

#include "rgen/Export.h"
#include "rgen/Route.h"

namespace rgen
{
constexpr double kGameScale = 1.0 / 5.0;

struct GeoPoint
{
	double Lat = 0.0;
	double Lon = 0.0;
};

// Проекция (равнопромежуточная вокруг центра маршрута) → метры игры.
RGEN_API void GeoToGame(const GeoPoint& Origin, double Lat, double Lon, double& OutX, double& OutY);
RGEN_API void GameToGeo(const GeoPoint& Origin, double X, double Y, double& OutLat, double& OutLon);

// Параметры маршрута Новосибирск — Кемерово (Р-255). Seed меняет только мелочи: изгибы, холмы, сёла.
RGEN_API RouteParams NovosibirskKemerovo(uint32_t Seed);
RGEN_API GeoPoint NovosibirskKemerovoOrigin();
} // namespace rgen
