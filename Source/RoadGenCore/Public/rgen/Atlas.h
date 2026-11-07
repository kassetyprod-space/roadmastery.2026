// Peregon — RoadGenCore
// Реальные маршруты. Координаты пунктов — широта/долгота (WGS-84), проецируются в мир игры в масштабе 1:2
// (дороги между городами вдвое короче настоящих, как в Euro Truck Simulator): X — на север, Y — на восток (в Unreal сверху: север вверх, восток
// вправо). Высоты тоже делятся на 2, иначе уклоны вышли бы круче настоящих. Дома, дороги и машины при этом
// в натуральную величину; города — из OpenStreetMap, с ядром 1:1 (см. rgen/City.h).
//
// Новосибирск → Кемерово по трассе Р-255 «Сибирь» (~255 км → ~128 км в игре). Порядок пунктов и
// километраж съездов — по открытым данным (Мошково — 59 км, Болотное — 126, Топки — 227, Кемерово — 249+).
// Точки сёл между Юргой и Топками приблизительные; точную геометрию дороги можно выгрузить из
// OpenStreetMap скриптом Tools/MapPipeline/fetch_route.py и подставить как частые опорные точки.
#pragma once

#include "rgen/Export.h"
#include "rgen/Route.h"

namespace rgen
{
constexpr double kGameScale = 1.0 / 2.0;

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
