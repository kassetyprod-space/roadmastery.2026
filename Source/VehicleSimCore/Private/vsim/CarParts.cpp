#include "vsim/CarParts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vsim
{
namespace parts
{
namespace
{
double Clamp01(double V) { return std::min(1.0, std::max(0.0, V)); }
double Smooth(double E0, double E1, double X)
{
	const double T = Clamp01((X - E0) / (E1 - E0));
	return T * T * (3.0 - 2.0 * T);
}
// Детерминированный «случай» для пресетов.
double Rand01(uint32_t& State)
{
	State = State * 1664525u + 1013904223u;
	return static_cast<double>(State >> 8) / 16777216.0;
}

// id, название, инструмент, секунд, под капотом, необязательная, износ/км
const SlotInfo kSlots[SlotCount] = {
	{"battery", "Аккумулятор", Tool::Wrenches, 20.0, true, false, 1.0 / 60000.0},
	{"plugs", "Свечи", Tool::PlugSocket, 40.0, true, false, 1.0 / 25000.0},
	{"distributor", "Трамблёр", Tool::Screwdriver, 45.0, true, false, 1.0 / 80000.0},
	{"carburetor", "Карбюратор", Tool::Wrenches, 60.0, true, false, 1.0 / 70000.0},
	{"airfilter", "Воздушный фильтр", Tool::Screwdriver, 10.0, true, false, 1.0 / 12000.0},
	{"fuelfilter", "Топливный фильтр", Tool::Pliers, 15.0, true, false, 1.0 / 20000.0},
	{"oilfilter", "Масляный фильтр", Tool::Wrenches, 20.0, true, false, 1.0 / 10000.0},
	{"radiator", "Радиатор", Tool::Wrenches, 90.0, true, false, 1.0 / 90000.0},
	{"waterpump", "Помпа", Tool::Wrenches, 80.0, true, false, 1.0 / 60000.0},
	{"hoses", "Патрубки", Tool::Pliers, 40.0, true, false, 1.0 / 40000.0},
	{"alternator", "Генератор", Tool::Wrenches, 60.0, true, false, 1.0 / 80000.0},
	{"starter", "Стартер", Tool::Wrenches, 70.0, true, false, 1.0 / 90000.0},
	{"turbo", "Турбина", Tool::Wrenches, 120.0, true, true, 1.0 / 50000.0},
	{"exhaust", "Глушитель", Tool::Wrenches, 60.0, false, false, 1.0 / 40000.0},
	{"susp_front", "Передняя подвеска", Tool::Jack, 150.0, false, false, 1.0 / 50000.0},
	{"susp_rear", "Задняя подвеска", Tool::Jack, 120.0, false, false, 1.0 / 60000.0},
	{"brakepads", "Тормозные колодки", Tool::Jack, 60.0, false, false, 1.0 / 30000.0},
	{"headlights", "Фары", Tool::Screwdriver, 30.0, false, false, 1.0 / 150000.0},
	{"taillights", "Задние фонари", Tool::Screwdriver, 25.0, false, false, 1.0 / 150000.0},
	{"dashboard", "Приборная панель", Tool::Screwdriver, 60.0, false, false, 1.0 / 200000.0},
	{"radio", "Магнитола", Tool::Screwdriver, 20.0, false, true, 1.0 / 200000.0},
};

double Cond(const CarState& Car, Slot S)
{
	const PartItem* P = Car.Get(S);
	return P ? Clamp01(P->Condition) : 0.0;
}
double TierBonus(const CarState& Car, Slot S)
{
	const PartItem* P = Car.Get(S);
	return P ? 0.04 * P->Tier : 0.0;
}
} // namespace

const SlotInfo& Info(Slot S) { return kSlots[std::min(SlotCount - 1, static_cast<int>(S))]; }

const char* PanelName(Panel P)
{
	static const char* Names[PanelCount] = {"Капот", "Крышка багажника", "Левая передняя дверь", "Правая передняя дверь", "Левая задняя дверь",
		"Правая задняя дверь", "Левое переднее крыло", "Правое переднее крыло", "Левое заднее крыло", "Правое заднее крыло", "Крыша", "Левый порог",
		"Правый порог"};
	return Names[std::min(PanelCount - 1, static_cast<int>(P))];
}

const char* ToolName(Tool T)
{
	switch (T)
	{
	case Tool::Wrenches: return "набор ключей";
	case Tool::PlugSocket: return "свечной ключ";
	case Tool::Screwdriver: return "отвёртки";
	case Tool::Pliers: return "пассатижи";
	case Tool::Jack: return "домкрат";
	case Tool::Grinder: return "болгарка";
	default: return "";
	}
}

const char* ConsumableName(Consumable C)
{
	switch (C)
	{
	case Consumable::CutDisc: return "отрезной диск";
	case Consumable::FlapDisc: return "лепестковый диск";
	case Consumable::Putty: return "шпаклёвка";
	case Consumable::Primer: return "грунт";
	case Consumable::Paint: return "краска";
	default: return "";
	}
}

Effects Evaluate(const CarState& Car)
{
	Effects E;
	const double Plugs = Cond(Car, Slot::SparkPlugs);
	const double Dist = Cond(Car, Slot::Distributor);
	const double Carb = Cond(Car, Slot::Carburetor);
	const double Air = Car.Has(Slot::AirFilter) ? Cond(Car, Slot::AirFilter) : 0.6; // без фильтра едет, но пылит мотор
	const double Fuel = Cond(Car, Slot::FuelFilter);
	E.bSpark = Car.Has(Slot::SparkPlugs) && Car.Has(Slot::Distributor) && Plugs > 0.05 && Dist > 0.05;
	E.bFuel = Car.Has(Slot::Carburetor) && Car.Has(Slot::FuelFilter) && Carb > 0.05 && Fuel > 0.02;
	// Свечи и трамблёр — троение; карбюратор и фильтры — недобор мощности.
	E.Misfire = Clamp01((1.0 - Plugs) * 0.7 + (1.0 - Dist) * 0.4 - 0.15);
	double Power = (0.55 + 0.45 * Carb) * (0.8 + 0.2 * Air) * (0.85 + 0.15 * Fuel) * (1.0 - 0.45 * E.Misfire);
	Power *= Car.Has(Slot::Exhaust) ? (0.92 + 0.08 * Cond(Car, Slot::Exhaust)) : 0.9;
	Power += TierBonus(Car, Slot::Carburetor) + TierBonus(Car, Slot::AirFilter) + TierBonus(Car, Slot::Exhaust);
	if (Car.Has(Slot::Turbo))
	{
		Power *= 1.0 + 0.3 * Cond(Car, Slot::Turbo) * (1.0 + TierBonus(Car, Slot::Turbo) * 2.0);
	}
	E.PowerFactor = std::max(0.0, Power);
	// Стартер крутит, если есть аккумулятор с зарядом и сам стартер.
	const double Battery = Cond(Car, Slot::Battery) * Clamp01(Car.BatteryCharge);
	E.StarterFactor = Car.Has(Slot::Battery) && Car.Has(Slot::Starter) ? Smooth(0.08, 0.5, Battery) * (0.4 + 0.6 * Cond(Car, Slot::Starter)) : 0.0;
	E.ChargeRate = Car.Has(Slot::Alternator) ? Cond(Car, Slot::Alternator) : 0.0;
	// Охлаждение: всё по цепочке; без антифриза — почти ноль.
	const double Coolant = Smooth(1.0, 6.0, Car.CoolantL);
	E.CoolingFactor = Cond(Car, Slot::Radiator) * (0.25 + 0.75 * Cond(Car, Slot::WaterPump)) * (0.3 + 0.7 * Cond(Car, Slot::Hoses)) * Coolant;
	E.BrakeFactor = Car.Has(Slot::BrakePads) ? 0.35 + 0.65 * Cond(Car, Slot::BrakePads) : 0.25;
	E.ShockFactor = 0.5 * (Cond(Car, Slot::SuspensionFront) + Cond(Car, Slot::SuspensionRear));
	E.FuelFactor = 1.0 + 0.3 * (1.0 - Carb) + 0.15 * E.Misfire + (Car.Has(Slot::Turbo) ? 0.12 : 0.0);
	// Электрика: без заряда ничего не светит.
	const bool bPower = Car.Has(Slot::Battery) && Battery > 0.05;
	E.bHeadlights = bPower && Cond(Car, Slot::Headlights) > 0.1;
	E.bTaillights = bPower && Cond(Car, Slot::Taillights) > 0.1;
	E.bDashboard = bPower && Cond(Car, Slot::Dashboard) > 0.1;
	E.bRadio = bPower && Cond(Car, Slot::Radio) > 0.1;
	// Масло: мало масла или грязный фильтр — мотор изнашивается быстрее.
	E.EngineWearFactor = (1.0 + 4.0 * (1.0 - Smooth(1.5, 3.2, Car.OilL))) * (1.0 + 1.5 * (1.0 - Cond(Car, Slot::OilFilter)));
	return E;
}

void Wear(CarState& Car, double Km, double Hours, double Wetness, bool bEngineRunning, double EngineTempC)
{
	for (int I = 0; I < SlotCount; ++I)
	{
		SlotRecord& R = Car.Slots[static_cast<size_t>(I)];
		if (!R.bInstalled)
		{
			continue;
		}
		double Rate = kSlots[I].WearPerKm * Km * (1.0 - 0.25 * R.Part.Tier);
		if (static_cast<Slot>(I) == Slot::Hoses || static_cast<Slot>(I) == Slot::WaterPump || static_cast<Slot>(I) == Slot::Radiator)
		{
			Rate *= 1.0 + 3.0 * Smooth(105.0, 125.0, EngineTempC); // перегрев убивает охлаждение
		}
		R.Part.Condition = Clamp01(R.Part.Condition - Rate);
	}
	// Аккумулятор: генератор заряжает на ходу, без него — садится.
	const Effects E = Evaluate(Car);
	if (bEngineRunning)
	{
		Car.BatteryCharge = Clamp01(Car.BatteryCharge + Hours * (0.6 * E.ChargeRate - 0.15));
	}
	// Масло понемногу «уходит», антифриз — через худые патрубки.
	Car.OilL = std::max(0.0, Car.OilL - Km * 0.0001);
	Car.CoolantL = std::max(0.0, Car.CoolantL - Km * 0.0004 * (1.0 - Cond(Car, Slot::Hoses)) - Hours * 0.02 * (1.0 - Cond(Car, Slot::Radiator)));
	// Кузов: краска выцветает, где её нет — ржавеет, ржавчина переходит в дыры (пороги и арки — быстрее).
	for (int I = 0; I < PanelCount; ++I)
	{
		PanelState& P = Car.Panels[static_cast<size_t>(I)];
		const bool bLow = static_cast<Panel>(I) == Panel::SillL || static_cast<Panel>(I) == Panel::SillR || static_cast<Panel>(I) == Panel::QuarterRL ||
			static_cast<Panel>(I) == Panel::QuarterRR;
		P.Paint = Clamp01(P.Paint - Km * 1.0e-6);
		const double Exposed = (1.0 - P.Paint) * (P.bPrimed ? 0.3 : 1.0) * (0.25 + 0.75 * Clamp01(Wetness)) * (bLow ? 2.0 : 1.0);
		P.Rust = Clamp01(P.Rust + Hours * 0.002 * Exposed);
		P.Holes = Clamp01(P.Holes + Hours * 0.0004 * Smooth(0.6, 1.0, P.Rust));
	}
}

Verdict CanRemove(const CarState& Car, Slot S, bool bHasTool, bool bHoodOpen)
{
	const SlotInfo& I = Info(S);
	if (!Car.Has(S))
	{
		return {false, "Пусто"};
	}
	if (I.bUnderHood && !bHoodOpen)
	{
		return {false, "Откройте капот"};
	}
	if (!bHasTool)
	{
		return {false, "Нужен инструмент"};
	}
	return {true, ""};
}

Verdict CanInstall(const CarState& Car, const PartItem& Part, bool bHasTool, bool bHoodOpen)
{
	const SlotInfo& I = Info(Part.Kind);
	if (Car.Has(Part.Kind))
	{
		return {false, "Место занято — сначала снимите старую"};
	}
	if (I.bUnderHood && !bHoodOpen)
	{
		return {false, "Откройте капот"};
	}
	if (!bHasTool)
	{
		return {false, "Нужен инструмент"};
	}
	return {true, ""};
}

PartItem Remove(CarState& Car, Slot S)
{
	SlotRecord& R = Car.Slots[static_cast<size_t>(S)];
	PartItem Out = R.Part;
	R.bInstalled = false;
	if (S == Slot::Battery)
	{
		Out.Charge = Car.BatteryCharge; // заряд уходит вместе с аккумулятором
		Car.BatteryCharge = 0.0;
	}
	if (S == Slot::Radiator || S == Slot::Hoses || S == Slot::WaterPump)
	{
		Car.CoolantL = std::min(Car.CoolantL, 1.0); // антифриз вытек
	}
	return Out;
}

void Install(CarState& Car, const PartItem& Part)
{
	SlotRecord& R = Car.Slots[static_cast<size_t>(Part.Kind)];
	R.bInstalled = true;
	R.Part = Part;
	if (Part.Kind == Slot::Battery)
	{
		Car.BatteryCharge = Clamp01(Part.Charge >= 0.0 ? Part.Charge : Part.Condition);
	}
}

const char* OpName(BodyOp Op)
{
	switch (Op)
	{
	case BodyOp::CutRot: return "Вырезать гниль";
	case BodyOp::Grind: return "Зачистить";
	case BodyOp::Putty: return "Зашпаклевать";
	case BodyOp::Sand: return "Ошкурить";
	case BodyOp::Prime: return "Загрунтовать";
	case BodyOp::PaintIt: return "Покрасить";
	default: return "";
	}
}

OpNeeds Needs(BodyOp Op)
{
	switch (Op)
	{
	case BodyOp::CutRot: return {Tool::Grinder, Consumable::CutDisc, 0.25, 40.0};
	case BodyOp::Grind: return {Tool::Grinder, Consumable::FlapDisc, 0.2, 45.0};
	case BodyOp::Putty: return {Tool::None, Consumable::Putty, 0.25, 50.0};
	case BodyOp::Sand: return {Tool::Grinder, Consumable::FlapDisc, 0.15, 35.0};
	case BodyOp::Prime: return {Tool::None, Consumable::Primer, 0.25, 25.0};
	case BodyOp::PaintIt: return {Tool::None, Consumable::Paint, 0.25, 40.0};
	default: return {};
	}
}

Verdict CanApply(const PanelState& P, BodyOp Op)
{
	switch (Op)
	{
	case BodyOp::CutRot:
		return P.Holes > 0.6 || P.Rust > 0.85 ? Verdict{true, ""} : Verdict{false, "Гнили нет — хватит зачистки"};
	case BodyOp::Grind:
		if (P.Rust < 0.02 && P.Paint < 0.02 && !P.bPrimed)
		{
			return {false, "Уже голый металл"};
		}
		return {true, ""};
	case BodyOp::Putty:
		if (P.Paint > 0.05 || P.Rust > 0.1)
		{
			return {false, "Сначала зачистите до металла"};
		}
		if (P.Holes > 0.6)
		{
			return {false, "Дыра большая — вырежьте гниль"};
		}
		return P.Holes > 0.01 || P.Putty < 0.01 ? Verdict{true, ""} : Verdict{false, "Ровно, шпаклевать нечего"};
	case BodyOp::Sand:
		return P.Putty > 0.01 || (!P.bSanded && P.Paint < 0.05) ? Verdict{true, ""} : Verdict{false, "Шлифовать нечего"};
	case BodyOp::Prime:
		if (P.Holes > 0.05)
		{
			return {false, "Есть дыры — зашпаклюйте"};
		}
		if (P.Putty > 0.01 || !P.bSanded)
		{
			return {false, "Сначала ошкурьте"};
		}
		if (P.Rust > 0.1)
		{
			return {false, "Ржавчина — зачистите"};
		}
		return P.bPrimed ? Verdict{false, "Уже в грунте"} : Verdict{true, ""};
	case BodyOp::PaintIt:
		// Красить можно по грунту или поверх целой старой краски (подкрасить другим цветом).
		return P.bPrimed || (P.Paint > 0.6 && P.Rust < 0.1) ? Verdict{true, ""} : Verdict{false, "Сначала загрунтуйте"};
	default:
		return {false, ""};
	}
}

void Apply(PanelState& P, BodyOp Op, const float* Color)
{
	switch (Op)
	{
	case BodyOp::CutRot:
		// Гниль вырезана — дыра чистая и меньше (её края можно шпаклевать, большую часть заварили бы).
		P.Holes = std::min(P.Holes, 0.45);
		P.Rust = std::min(P.Rust, 0.3);
		P.Paint = 0.0;
		P.bPrimed = false;
		P.bSanded = false;
		break;
	case BodyOp::Grind:
		// Зачистка вскрывает скрытую гниль.
		P.Holes = Clamp01(P.Holes + 0.25 * Smooth(0.5, 1.0, P.Rust));
		P.Rust = 0.0;
		P.Paint = 0.0;
		P.bPrimed = false;
		P.bSanded = true;
		break;
	case BodyOp::Putty:
		P.Holes = std::max(0.0, P.Holes - 0.35);
		P.Putty = 1.0;
		P.bSanded = false;
		break;
	case BodyOp::Sand:
		P.Putty = 0.0;
		P.bSanded = true;
		break;
	case BodyOp::Prime:
		P.bPrimed = true;
		break;
	case BodyOp::PaintIt:
		P.Paint = 1.0;
		P.bPrimed = false;
		P.bSanded = false;
		P.Putty = 0.0;
		if (Color)
		{
			P.Color[0] = Color[0];
			P.Color[1] = Color[1];
			P.Color[2] = Color[2];
		}
		break;
	default:
		break;
	}
}

BodyOp NextStep(const PanelState& P)
{
	if (P.Holes > 0.6 || P.Rust > 0.85)
	{
		return BodyOp::CutRot;
	}
	if (P.Rust > 0.1 || (P.Paint > 0.05 && P.Paint < 0.6))
	{
		return BodyOp::Grind;
	}
	if (P.Paint >= 0.6)
	{
		return BodyOp::PaintIt;
	}
	if (P.Holes > 0.01)
	{
		return BodyOp::Putty;
	}
	if (P.Putty > 0.01 || !P.bSanded)
	{
		return P.bPrimed ? BodyOp::PaintIt : BodyOp::Sand;
	}
	return P.bPrimed ? BodyOp::PaintIt : BodyOp::Prime;
}

double PanelScore(const PanelState& P)
{
	return Clamp01(P.Paint * (1.0 - P.Rust) * (1.0 - P.Holes));
}

CarState MakeAbandoned(uint32_t Seed, double Quality)
{
	CarState Car;
	uint32_t R = Seed * 2654435761u + 7u;
	for (int I = 0; I < SlotCount; ++I)
	{
		SlotRecord& S = Car.Slots[static_cast<size_t>(I)];
		S.bInstalled = !kSlots[I].bOptional || static_cast<Slot>(I) == Slot::Radio;
		S.Part.Kind = static_cast<Slot>(I);
		S.Part.Condition = Clamp01(Quality * (0.6 + 0.4 * Rand01(R)) + 0.1 * (Rand01(R) - 0.5));
		S.Part.Tier = 0;
	}
	for (int I = 0; I < PanelCount; ++I)
	{
		PanelState& P = Car.Panels[static_cast<size_t>(I)];
		const double Bad = 1.0 - Quality;
		P.Paint = Clamp01(Quality + 0.2 * (Rand01(R) - 0.5));
		P.Rust = Clamp01(Bad * (0.4 + 0.8 * Rand01(R)));
		P.Holes = Clamp01(Smooth(0.6, 1.0, P.Rust) * Rand01(R));
	}
	Car.OilL = 1.5 + 2.2 * Quality;
	Car.CoolantL = 2.0 + 5.8 * Quality;
	Car.BatteryCharge = Quality;
	return Car;
}

CarState MakeGarageWreck(uint32_t Seed)
{
	CarState Car = MakeAbandoned(Seed, 0.35);
	// Что растащили за годы в гараже: аккумулятор, свечи, карбюратор, воздушный фильтр.
	for (Slot S : {Slot::Battery, Slot::SparkPlugs, Slot::Carburetor, Slot::AirFilter, Slot::Radio})
	{
		Car.Slots[static_cast<size_t>(S)].bInstalled = false;
	}
	Car.Slots[static_cast<size_t>(Slot::Radiator)].Part.Condition = 0.2; // течёт
	Car.Slots[static_cast<size_t>(Slot::Hoses)].Part.Condition = 0.25;
	Car.CoolantL = 0.5;
	Car.OilL = 2.0;
	Car.BatteryCharge = 0.0;
	// Пороги и низ крыльев сгнили насквозь.
	for (Panel P : {Panel::SillL, Panel::SillR, Panel::QuarterRL, Panel::FenderFL})
	{
		Car.PanelAt(P).Rust = 0.9;
		Car.PanelAt(P).Holes = 0.7;
		Car.PanelAt(P).Paint = 0.1;
	}
	return Car;
}

CarState MakeFactoryNew(const float* Color)
{
	CarState Car = MakeAbandoned(1u, 1.0);
	for (int I = 0; I < SlotCount; ++I)
	{
		Car.Slots[static_cast<size_t>(I)].Part.Condition = 1.0;
	}
	for (PanelState& P : Car.Panels)
	{
		P = PanelState();
		if (Color)
		{
			P.Color[0] = Color[0];
			P.Color[1] = Color[1];
			P.Color[2] = Color[2];
		}
	}
	Car.OilL = 3.75;
	Car.CoolantL = 7.8;
	Car.BatteryCharge = 1.0;
	return Car;
}

namespace
{
constexpr double kPackVersion = 1.0;
}

std::vector<double> Pack(const CarState& Car)
{
	std::vector<double> D;
	D.reserve(4 + SlotCount * 4 + PanelCount * 9);
	D.push_back(kPackVersion);
	D.push_back(static_cast<double>(SlotCount));
	D.push_back(static_cast<double>(PanelCount));
	for (const SlotRecord& R : Car.Slots)
	{
		D.push_back(R.bInstalled ? 1.0 : 0.0);
		D.push_back(R.Part.Condition);
		D.push_back(static_cast<double>(R.Part.Tier));
		D.push_back(R.Part.Charge);
	}
	for (const PanelState& P : Car.Panels)
	{
		D.push_back(P.Rust);
		D.push_back(P.Holes);
		D.push_back(P.Paint);
		D.push_back(P.Putty);
		D.push_back((P.bSanded ? 1.0 : 0.0) + (P.bPrimed ? 2.0 : 0.0));
		D.push_back(P.Color[0]);
		D.push_back(P.Color[1]);
		D.push_back(P.Color[2]);
	}
	D.push_back(Car.OilL);
	D.push_back(Car.CoolantL);
	D.push_back(Car.BatteryCharge);
	return D;
}

bool Unpack(const std::vector<double>& Data, CarState& Car)
{
	const size_t Need = 3 + static_cast<size_t>(SlotCount) * 4 + static_cast<size_t>(PanelCount) * 8 + 3;
	if (Data.size() != Need || Data[0] != kPackVersion || static_cast<int>(Data[1]) != SlotCount || static_cast<int>(Data[2]) != PanelCount)
	{
		return false;
	}
	size_t I = 3;
	CarState Out;
	for (int S = 0; S < SlotCount; ++S)
	{
		SlotRecord& R = Out.Slots[static_cast<size_t>(S)];
		R.bInstalled = Data[I++] > 0.5;
		R.Part.Kind = static_cast<Slot>(S);
		R.Part.Condition = Clamp01(Data[I++]);
		R.Part.Tier = static_cast<int>(Data[I++]);
		R.Part.Charge = Data[I++];
	}
	for (PanelState& P : Out.Panels)
	{
		P.Rust = Clamp01(Data[I++]);
		P.Holes = Clamp01(Data[I++]);
		P.Paint = Clamp01(Data[I++]);
		P.Putty = Clamp01(Data[I++]);
		const int Flags = static_cast<int>(Data[I++]);
		P.bSanded = (Flags & 1) != 0;
		P.bPrimed = (Flags & 2) != 0;
		for (float& C : P.Color)
		{
			C = static_cast<float>(Data[I++]);
		}
	}
	Out.OilL = Data[I++];
	Out.CoolantL = Data[I++];
	Out.BatteryCharge = Clamp01(Data[I++]);
	Car = Out;
	return true;
}

double Overall(const CarState& Car)
{
	double Sum = 0.0;
	int N = 0;
	for (int S = 0; S < SlotCount; ++S)
	{
		if (Info(static_cast<Slot>(S)).bOptional)
		{
			continue;
		}
		const SlotRecord& R = Car.Slots[static_cast<size_t>(S)];
		Sum += R.bInstalled ? R.Part.Condition : 0.0;
		++N;
	}
	double Body = 0.0;
	for (const PanelState& P : Car.Panels)
	{
		Body += PanelScore(P);
	}
	return 0.7 * Sum / std::max(1, N) + 0.3 * Body / PanelCount;
}


std::vector<Finding> Diagnose(const CarState& Car, double FuelL)
{
	std::vector<Finding> Out;
	auto Add = [&Out](int Severity, const std::string& Text) { Out.push_back({Severity, Text}); };
	auto Where = [](Slot S) {
		const SlotInfo& I = Info(S);
		std::string W = I.bUnderHood ? " (под капотом" : " (";
		W += I.bUnderHood ? ", нужен " : "нужен ";
		W += ToolName(I.NeedTool);
		W += ")";
		return W;
	};
	auto Missing = [&](Slot S, int Severity, const char* Why) {
		if (!Car.Has(S))
		{
			Add(Severity, std::string("Нет: ") + Info(S).Name + " — " + Why + Where(S));
			return true;
		}
		return false;
	};
	auto Worn = [&](Slot S, double Below, int Severity, const char* Why) {
		const PartItem* P = Car.Get(S);
		if (P && P->Condition < Below)
		{
			Add(Severity, std::string(Info(S).Name) + " изношен(а) до " + std::to_string(static_cast<int>(P->Condition * 100.0 + 0.5)) + "% — " + Why + Where(S));
		}
	};
	// Запуск.
	if (!Missing(Slot::Battery, 2, "стартер не крутит"))
	{
		const double Charge = Clamp01(Car.BatteryCharge) * Cond(Car, Slot::Battery);
		if (Charge < 0.12)
		{
			Add(2, "Аккумулятор сел — нужен другой (или заряженный) аккумулятор");
		}
		else if (Charge < 0.3)
		{
			Add(1, "Аккумулятор слабый — стартер крутит вяло, заводите с первой попытки");
		}
	}
	Missing(Slot::Starter, 2, "нечем крутить мотор");
	Missing(Slot::SparkPlugs, 2, "нет искры");
	Worn(Slot::SparkPlugs, 0.2, 1, "троит, может не схватывать");
	Missing(Slot::Distributor, 2, "нет искры");
	Missing(Slot::Carburetor, 2, "бензин не попадает в мотор");
	Worn(Slot::Carburetor, 0.25, 1, "мотор глохнет и плохо тянет");
	Missing(Slot::FuelFilter, 2, "бензин не идёт к карбюратору");
	Missing(Slot::AirFilter, 1, "мотор работает, но глотает пыль");
	if (FuelL < 0.5)
	{
		Add(2, "Бак пуст — залейте из канистры или на АЗС");
	}
	else if (FuelL < 4.0)
	{
		Add(1, "Бензина мало — " + std::to_string(static_cast<int>(FuelL + 0.5)) + " л, хватит ненадолго");
	}
	// Охлаждение и смазка.
	Missing(Slot::Radiator, 1, "мотор перегреется за пару минут");
	Worn(Slot::Radiator, 0.35, 1, "течёт, антифриз уходит");
	Missing(Slot::WaterPump, 1, "антифриз не гоняется — перегрев");
	Missing(Slot::Hoses, 1, "антифриз вытечет");
	Worn(Slot::Hoses, 0.3, 1, "подтекают");
	if (Car.CoolantL < 5.0)
	{
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), "%.1f", Car.CoolantL);
		Add(1, std::string("Мало антифриза: ") + Buf + " из 7,8 л — долейте в бачок под капотом");
	}
	if (Car.OilL < 2.8)
	{
		Add(Car.OilL < 1.5 ? 2 : 1, "Мало масла — долейте в горловину на крышке клапанов (норма 3,75 л)");
	}
	Missing(Slot::OilFilter, 1, "мотор быстро износится");
	// Дорога.
	Missing(Slot::Alternator, 1, "аккумулятор сядет в дороге");
	Missing(Slot::BrakePads, 1, "почти нет тормозов");
	Worn(Slot::BrakePads, 0.2, 1, "тормоза слабые");
	Missing(Slot::SuspensionFront, 1, "машину раскачивает");
	Missing(Slot::SuspensionRear, 1, "машину раскачивает");
	Missing(Slot::Exhaust, 1, "громко и жрёт бензин");
	Missing(Slot::Headlights, 1, "ночью ехать нельзя");
	Missing(Slot::Taillights, 1, "сзади не видно");
	Missing(Slot::Dashboard, 1, "не видно скорости и топлива");
	return Out;
}

} // namespace parts
} // namespace vsim
