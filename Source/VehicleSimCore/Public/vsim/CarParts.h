// Peregon — VehicleSimCore
// Узлы и кузов машины, которые игрок снимает, ставит, чинит и красит. Чистая логика без Unreal:
// что стоит в машине и в каком состоянии, как это влияет на езду, как изнашивается, какие инструменты
// и расходники нужны для каждой работы. Игра хранит CarState у каждой машины и раз в кадр передаёт
// Effects в физику (VehicleCondition).
#pragma once

#include "vsim/Export.h"

#include <array>
#include <cstdint>
#include <vector>

namespace vsim
{
namespace parts
{
// Места под детали.
enum class Slot : uint8_t
{
	Battery,         // аккумулятор
	SparkPlugs,      // свечи (комплект)
	Distributor,     // трамблёр
	Carburetor,      // карбюратор
	AirFilter,       // воздушный фильтр
	FuelFilter,      // топливный фильтр
	OilFilter,       // масляный фильтр
	Radiator,        // радиатор
	WaterPump,       // помпа (крыльчатка)
	Hoses,           // патрубки
	Alternator,      // генератор
	Starter,         // стартер
	Turbo,           // турбина (тюнинг, необязательна)
	Exhaust,         // глушитель
	SuspensionFront, // передняя подвеска (амортизаторы, шаровые)
	SuspensionRear,  // задняя подвеска
	BrakePads,       // колодки (комплект)
	Headlights,      // фары
	Taillights,      // задние фонари
	Dashboard,       // приборная панель
	Radio,           // магнитола
	Count
};
constexpr int SlotCount = static_cast<int>(Slot::Count);

// Панели кузова (каждая гниёт, красится и шпаклюется отдельно).
enum class Panel : uint8_t
{
	Hood,
	Trunk,
	DoorFL,
	DoorFR,
	DoorRL,
	DoorRR,
	FenderFL,
	FenderFR,
	QuarterRL,
	QuarterRR,
	Roof,
	SillL,
	SillR,
	Count
};
constexpr int PanelCount = static_cast<int>(Panel::Count);

// Инструменты (лежат в карманах или багажнике — их наличие проверяет игра).
enum class Tool : uint8_t
{
	None,
	Wrenches,     // набор ключей
	PlugSocket,   // свечной ключ
	Screwdriver,  // отвёртки
	Pliers,       // пассатижи (хомуты патрубков)
	Jack,         // домкрат и баллонник (подвеска, колодки)
	Grinder,      // болгарка
	Count
};

// Расходники кузовных работ.
enum class Consumable : uint8_t
{
	None,
	CutDisc,      // отрезной диск — срезать гниль
	FlapDisc,     // лепестковый — зачистить ржавчину, ошкурить
	Putty,        // шпаклёвка
	Primer,       // грунт
	Paint,        // краска (цвет — у баллона)
	Count
};

struct PartItem
{
	Slot Kind = Slot::Battery;
	double Condition = 1.0; // 0..1
	int Tier = 0;           // 0 — стоковая, 1 — улучшенная, 2 — спортивная (фирменный магазин)
	double Charge = -1.0;   // заряд аккумулятора 0..1 (−1 — не задан: берётся Condition)
};

struct PanelState
{
	double Rust = 0.0;   // 0..1 ржавчина на поверхности
	double Holes = 0.0;  // 0..1 сгнило насквозь
	double Paint = 1.0;  // 0..1 состояние ЛКП (0 — голый металл или грунт)
	double Putty = 0.0;  // 0..1 нешлифованная шпаклёвка
	bool bSanded = false;
	bool bPrimed = false;
	float Color[3] = {0.55f, 0.07f, 0.05f};
};

struct SlotRecord
{
	bool bInstalled = false;
	PartItem Part;
};

struct CarState
{
	std::array<SlotRecord, SlotCount> Slots;
	std::array<PanelState, PanelCount> Panels;
	double OilL = 3.5;      // норма 3.75
	double CoolantL = 7.5;  // норма 7.8
	double BatteryCharge = 1.0; // 0..1 заряд (не путать с состоянием аккумулятора)

	bool Has(Slot S) const { return Slots[static_cast<int>(S)].bInstalled; }
	const PartItem* Get(Slot S) const { return Has(S) ? &Slots[static_cast<int>(S)].Part : nullptr; }
	PanelState& PanelAt(Panel P) { return Panels[static_cast<int>(P)]; }
	const PanelState& PanelAt(Panel P) const { return Panels[static_cast<int>(P)]; }
};

// Что о месте знает игра: название, какой инструмент нужен, сколько секунд работы, под капотом ли.
struct SlotInfo
{
	const char* Id;        // для предметов: "part." + Id
	const char* Name;      // по-русски
	Tool NeedTool;
	double Seconds;
	bool bUnderHood;
	bool bOptional;        // без него машина ездит (турбина, магнитола)
	double WearPerKm;      // потеря состояния за км при среднем качестве
};
VSIM_API const SlotInfo& Info(Slot S);
VSIM_API const char* PanelName(Panel P);
VSIM_API const char* ToolName(Tool T);
VSIM_API const char* ConsumableName(Consumable C);

// Влияние на езду — передаётся в VehicleCondition.
struct Effects
{
	double PowerFactor = 1.0;   // множитель момента (свечи, карбюратор, фильтр, выхлоп, турбина)
	double StarterFactor = 1.0; // аккумулятор × стартер × заряд
	bool bSpark = true;         // искра есть (свечи и трамблёр)
	bool bFuel = true;          // топливо подаётся (карбюратор, фильтр)
	double Misfire = 0.0;       // 0..1 провалы и троение
	double CoolingFactor = 1.0; // радиатор × помпа × патрубки × антифриз
	double BrakeFactor = 1.0;
	double ShockFactor = 1.0;   // подвеска
	double ChargeRate = 1.0;    // генератор (0 — аккумулятор садится)
	double FuelFactor = 1.0;    // расход
	bool bHeadlights = true;
	bool bTaillights = true;
	bool bDashboard = true;
	bool bRadio = true;
	double EngineWearFactor = 1.0; // масло и масляный фильтр
};
VSIM_API Effects Evaluate(const CarState& Car);

// Износ за пробег (км) и простой (часы). Мокро — ржавчина растёт там, где нет краски.
VSIM_API void Wear(CarState& Car, double Km, double Hours, double Wetness, bool bEngineRunning, double EngineTempC);

// --- Детали ---
struct Verdict
{
	bool bOk = false;
	const char* Reason = "";
};
// Можно ли снять/поставить деталь (с учётом инструмента в наличии и открытого капота).
VSIM_API Verdict CanRemove(const CarState& Car, Slot S, bool bHasTool, bool bHoodOpen);
VSIM_API Verdict CanInstall(const CarState& Car, const PartItem& Part, bool bHasTool, bool bHoodOpen);
VSIM_API PartItem Remove(CarState& Car, Slot S);
VSIM_API void Install(CarState& Car, const PartItem& Part);

// --- Кузов: болгарка → шпаклёвка → шлифовка → грунт → краска ---
enum class BodyOp : uint8_t
{
	CutRot,  // срезать гниль отрезным диском (дыры остаются, но края чистые — можно шпаклевать)
	Grind,   // зачистить лепестковым диском: ржавчина и старая краска — долой
	Putty,   // зашпаклевать дыры и неровности
	Sand,    // ошкурить шпаклёвку
	Prime,   // загрунтовать
	PaintIt, // покрасить
	Count
};
struct OpNeeds
{
	Tool NeedTool = Tool::None;
	Consumable Use = Consumable::None;
	double Amount = 0.0;   // сколько расходника (1 — целый баллон / банка / диск)
	double Seconds = 0.0;
};
VSIM_API const char* OpName(BodyOp Op);
VSIM_API OpNeeds Needs(BodyOp Op);
VSIM_API Verdict CanApply(const PanelState& P, BodyOp Op);
VSIM_API void Apply(PanelState& P, BodyOp Op, const float* Color = nullptr);
// Какую работу логично делать следующей (подсказка игроку).
VSIM_API BodyOp NextStep(const PanelState& P);
// Насколько панель «в порядке» (0..1) — для оценки машины.
VSIM_API double PanelScore(const PanelState& P);

// --- Состояния для старта и находок ---
// Гаражная находка: без аккумулятора, свечей, карбюратора и фильтра; радиатор течёт; кузов гнилой.
VSIM_API CarState MakeGarageWreck(uint32_t Seed);
// Брошенная машина: всё на месте, но в случайном состоянии Quality 0..1 (1 — как с завода).
VSIM_API CarState MakeAbandoned(uint32_t Seed, double Quality);
VSIM_API CarState MakeFactoryNew(const float* Color);

// Сохранение: всё состояние одним плоским списком чисел (порядок фиксирован, в начале — версия).
VSIM_API std::vector<double> Pack(const CarState& Car);
// false — список пуст или не той версии (тогда Car не меняется).
VSIM_API bool Unpack(const std::vector<double>& Data, CarState& Car);
// Общая оценка машины 0..1 (детали + кузов) — для подсказок и цен.
VSIM_API double Overall(const CarState& Car);
} // namespace parts
} // namespace vsim
