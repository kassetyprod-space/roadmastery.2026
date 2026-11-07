// Peregon — RoadGenCore
// Настоящий город из OpenStreetMap (файл .city — Tools/MapPipeline/fetch_city.py): чтение и построение
// геометрии без Unreal. Дома, улицы, развязки — в натуральную величину; масштаб ETS (ядро 1:1, окраины
// сжаты к 1:2, как дороги между городами) уже применён конвейером.
//
// Что строится:
//   • улицы по классам (магистраль, проспект, улица, двор, съезд) с шириной по числу полос;
//   • перекрёстки — многоугольники, в которые подрезаны улицы; кольца с островом и бордюром;
//   • разметка по месту: осевая (двойная сплошная у многополосных, прерывистая у двухполосных, сплошная перед
//     перекрёстком), разделители полос, края магистралей, стоп-линии, «зебры» у светофоров и переходов;
//   • тротуары с бордюром, у эстакад и путепроводов — подпорные стенки, пролётные строения, перила, опоры;
//   • мосты через реку: настил над водой, опоры в воде; «арочно-балочный» (стиль oktyabrsky) — арки под настилом;
//   • дома по контурам с этажностью и видом фасада, окна, которые горят вечером, плоские крыши;
//   • земля города с руслом реки (вода ниже набережных), парки, железная дорога;
//   • «реквизит» для игры: светофоры, фонари, знаки, таблички улиц, точки достопримечательностей.
// Координаты: X — север, Y — восток, Z — вверх, метры; Z = 0 — уровень города (игра поднимает его на высоту
// начала трассы). Лицевая сторона треугольника (A, B, C) — куда смотрит (C − A) × (B − A), как в Unreal.
#pragma once

#include "rgen/Export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rgen
{
struct CityPt
{
	double X = 0.0;
	double Y = 0.0;
};

enum class CityRoadClass : uint8_t
{
	Motorway,
	Trunk,
	Primary,
	Secondary,
	Tertiary,
	Residential,
	Link,
	Count
};

namespace CityFlag
{
constexpr int Signals = 1;
constexpr int Crossing = 2;
constexpr int Stop = 4;
constexpr int GiveWay = 8;
} // namespace CityFlag

struct CityNode
{
	double X = 0.0;
	double Y = 0.0;
	int Flags = 0;
};

struct CityWay
{
	CityRoadClass Class = CityRoadClass::Residential;
	int Lanes = 2;
	bool OneWay = false;
	bool Ring = false;
	bool Bridge = false;
	bool Tunnel = false;
	int Layer = 0;
	int Speed = 0;
	std::string Style; // «oktyabrsky» — арочно-балочный мост, «exit» — выезд на трассу
	std::string Name;
	std::vector<int> Nodes;
};

struct CityBuilding
{
	std::string Kind; // panel, brick, stucco, office, shop, industrial, house, garage, church
	int Levels = 4;
	std::string Name;
	std::vector<CityPt> Ring;
};

struct CityLandmark
{
	std::string Key;
	double X = 0.0;
	double Y = 0.0;
	double Yaw = 0.0; // рад, направление длинной стороны (0 — на север, растёт к востоку)
	double Radius = 0.0;
};

struct RGEN_API CityData
{
	std::string Name;
	double CenterX = 0.0;
	double CenterY = 0.0;
	double Core = 0.0;
	std::vector<CityNode> Nodes;
	std::vector<CityWay> Ways;
	std::vector<CityBuilding> Buildings;
	std::vector<CityLandmark> Landmarks;
	std::vector<std::vector<CityPt>> Water;
	std::vector<std::vector<CityPt>> Islands;
	std::vector<std::vector<CityPt>> Parks;
	std::vector<std::vector<CityPt>> Rails;

	// Текст файла .city (UTF-8). false — не тот формат или битые строки (Error — что именно).
	bool Parse(const std::string& Text, std::string* Error = nullptr);
};

// Материалы геометрии города. Порядок — часть формата (по нему игра выбирает материал).
enum class CityMat : uint8_t
{
	Asphalt,
	Marking,
	Sidewalk,
	Curb,
	Ground,
	Grass,
	Water,
	Concrete,   // опоры, подпорные стенки, пролёты
	Steel,      // перила, арки моста, рельсы
	Ballast,    // щебень под путями
	WallPanel,
	WallBrick,
	WallStucco,
	WallOffice,
	WallIndustrial,
	WallHouse,
	Roof,
	LitWindow,  // окна, в которых вечером горит свет
	Count
};

struct RGEN_API CitySection
{
	std::vector<float> P;   // x, y, z
	std::vector<float> N;   // нормали
	std::vector<float> UV;  // u, v
	std::vector<uint8_t> C; // r, g, b, a (оттенок для материалов с цветом вершин)
	std::vector<int32_t> I;

	size_t VertexCount() const { return P.size() / 3; }
	int32_t Vertex(double X, double Y, double Z, double Nx, double Ny, double Nz, double U, double V, uint32_t Rgba = 0xFFFFFFFFu);
	// Треугольник лицом к Hint (порядок вершин выбирается сам).
	void Tri(int32_t A, int32_t B, int32_t C, double Hx, double Hy, double Hz);
};

struct CityTile
{
	int TX = 0;
	int TY = 0;
	CitySection Sections[static_cast<int>(CityMat::Count)];
};

enum class CityPropKind : uint8_t
{
	TrafficLight, // Yaw — куда смотрит лицо (навстречу водителю)
	StreetLamp,   // Yaw — куда вылет консоли
	SignYield,    // 2.4 «Уступите дорогу»
	SignStop,     // 2.5
	SignCrossing, // 5.19 «Пешеходный переход»
	SignRoundabout, // 4.3 «Круговое движение»
	SignSpeed,    // 3.24, Value — км/ч
	StreetName,   // табличка с названием улицы, Text
	Landmark      // точка модели достопримечательности: Text — ключ, Value — радиус, м
};

struct CityProp
{
	CityPropKind Kind = CityPropKind::StreetLamp;
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	double Yaw = 0.0; // градусы, как в Unreal: 0 — на север (+X), 90 — на восток (+Y)
	int Value = 0;
	std::string Text;
};

struct CityBuildOptions
{
	double TileSize = 400.0;
	double WaterDepth = 9.0;     // дно русла ниже уровня города, м
	double WaterLevel = -4.0;    // зеркало воды (набережные выше на 4 м)
	double RiverBridgeLift = 9.0; // настил моста через реку над уровнем города (к нему — пандусы 5 %)
	double GroundStep = 16.0;    // шаг сетки земли там, где есть вода
	// Землю города не строить рядом с этими точками (там своя земля трассы).
	std::vector<CityPt> Keepout;
	double KeepoutRadius = 280.0;
	double KeepoutClear = 35.0;  // дома ближе этого к точкам Keepout не строятся (там идёт трасса)
	double ApronWidth = 1.4;     // отмостка и тротуар вокруг многоэтажек, м
};

struct RGEN_API CityResult
{
	std::vector<CityTile> Tiles;
	std::vector<CityProp> Props;
	double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0;
	int Edges = 0;
	int Junctions = 0;
	int Bridges = 0;
	int BuildingsBuilt = 0;
	int BuildingsCleared = 0; // убрано с коридора трассы
	int Driveways = 0;        // асфальтовые проезды у подъездов

	size_t TriangleCount() const;
	// Высота дорожного полотна в точке (для машин и реквизита): 0 — земля, > 0 — эстакада, NaN — не дорога.
	std::vector<double> NodeHeight;
};

RGEN_API CityResult BuildCity(const CityData& Data, const CityBuildOptions& Options);

// Ширина полосы и тротуара по классу улицы.
RGEN_API double CityLaneWidth(CityRoadClass Class);
RGEN_API double CitySidewalkWidth(CityRoadClass Class);
RGEN_API CityRoadClass CityClassFromOsm(const std::string& Highway);
} // namespace rgen
