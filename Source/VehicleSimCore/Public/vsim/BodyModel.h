// Peregon — VehicleSimCore
// Процедурная модель кузова «Сокол-1500»: цельные панели, остекление, салон, моторный отсек, багажник, колёса.
// Чистый C++ без Unreal: геометрия проверяется тестами и рендерится в превью вне движка.
//
// Оси и единицы как у машины в Unreal: X — вперёд, Y — вправо, Z — вверх, сантиметры.
// Начало координат — центр кузова (≈ центр масс, 52 см над землёй в покое).
// Лицевая сторона треугольника (A, B, C) — та, куда смотрит (C − A) × (B − A) (правило Unreal).
#pragma once

#include "vsim/Export.h"

#include <cstdint>
#include <vector>

namespace vmodel
{
struct V3
{
	float X = 0.f;
	float Y = 0.f;
	float Z = 0.f;
	V3() = default;
	V3(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}
};

// Слоты материалов. Порядок — часть формата (по нему Unreal выбирает текстуру и цвет).
enum class Mat : uint8_t
{
	Paint,        // краска кузова (цвет выбирает игрок)
	Chrome,       // бамперы, молдинги, ручки
	BlackTrim,    // уплотнители, решётки, матовый пластик снаружи
	Rubber,       // шины, коврики
	Glass,        // стёкла
	Headlamp,     // рассеиватели фар (светятся)
	Taillamp,     // красные фонари (светятся)
	Indicator,    // оранжевые поворотники
	Interior,     // пластик салона
	InteriorDark, // торпедо, рулевое колесо
	Fabric,       // обивка сидений
	Carpet,       // ковролин пола и багажника
	Headliner,    // потолок
	GaugeFace,    // циферблаты
	Needle,       // стрелки приборов
	Engine,       // блок, головка, железо под капотом
	Metal,        // некрашеный металл: днище, выхлоп, диски
	Plate,        // номерной знак
	Water,        // капли на лобовом стекле (полупрозрачные, их стирают дворники)
	Dome,         // плафон освещения салона
	Count
};

// Подвижные части: каждая — отдельный узел со своим началом координат.
enum class Part : uint8_t
{
	Body,
	Hood,
	Trunk,
	DoorFL,
	DoorFR,
	DoorRL,
	DoorRR,
	Glovebox,
	Steering,     // своя система координат: X — по оси колонки к водителю, обод в плоскости YZ
	Key,
	LightKnob,
	GearLever,
	Handbrake,
	ClutchPedal,
	BrakePedal,
	GasPedal,
	SpeedoNeedle, // стрелки: X — от водителя к прибору, стрелка смотрит в +Z
	TachoNeedle,
	FuelNeedle,
	TempNeedle,
	WheelLeft,    // колесо в своей системе: центр в нуле, ось — Y, внешняя сторона −Y
	WheelRight,   // то же, внешняя сторона +Y
	WiperL,       // дворники: узел на оси щётки, поворот вокруг WiperAxis
	WiperR,
	Count
};

struct Section
{
	Mat Material = Mat::Paint;
	std::vector<V3> Positions;
	std::vector<V3> Normals;
	std::vector<float> U;
	std::vector<float> V;
	std::vector<int32_t> Indices;
};

struct PartMesh
{
	Part Id = Part::Body;
	V3 Origin;                    // положение узла в системе кузова (для колёс — ноль)
	std::vector<Section> Sections; // геометрия в системе узла, по одной секции на материал
};

// Прямоугольник для невидимых «мишеней» взаимодействия (двери, капот, ключ...), в системе узла.
struct HitBox
{
	Part Owner = Part::Body;
	const char* Tag = "";
	V3 Center;
	V3 HalfSize;
};

struct VSIM_API Model
{
	std::vector<PartMesh> Parts;
	std::vector<HitBox> HitBoxes;
	V3 SteeringColumnDir;  // ось колонки (X узла руля) в системе кузова
	V3 HeadlightL;         // точки фар для прожекторов
	V3 HeadlightR;
	V3 WiperAxis;          // ось поворота дворников (перпендикуляр к стеклу), в системе кузова
	float WiperSweepDeg = 0.f; // угол хода щётки: со знаком, поворот на него поднимает щётку по стеклу
	V3 DriverEye;          // глаза водителя
	V3 ExitPoint;          // куда выходит водитель (слева от машины)

	const PartMesh* Find(Part Id) const;
	size_t TriangleCount() const;
};

// Собрать модель. Детерминированно; занимает несколько миллисекунд.
VSIM_API Model BuildSokol();

VSIM_API const char* PartName(Part Id);
VSIM_API const char* MatName(Mat Id);
} // namespace vmodel
