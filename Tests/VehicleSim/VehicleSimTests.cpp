// Headless-тесты физики. Каждая проверка — «паспортная» характеристика машины:
// если после правки физики разгон до сотни стал 9 секунд, тест это поймает.
#include "PlanarRig.h"
#include "vsim/Presets.h"
#include "vsim/CarParts.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <string>

using namespace vsim;

namespace
{
int GFailures = 0;
int GChecks = 0;

void Check(bool bCond, const std::string& What, double Value)
{
	++GChecks;
	std::printf("  [%s] %-58s %10.3f\n", bCond ? " OK " : "FAIL", What.c_str(), Value);
	if (!bCond)
	{
		++GFailures;
	}
}

void CheckRange(const std::string& What, double Value, double Lo, double Hi)
{
	Check(Value >= Lo && Value <= Hi, What + " [" + std::to_string(Lo).substr(0, 6) + ".." + std::to_string(Hi).substr(0, 6) + "]", Value);
}

constexpr double kDt = 1.0 / 60.0;

// Машина с заведённым мотором на ровном асфальте.
struct Setup
{
	Vehicle Car;
	PlanarRig Rig;
	DriverInput In;

	explicit Setup(bool bAutoGearbox = true, bool bAutoClutch = true)
		: Car(Presets::Sokol1500())
		, Rig(Car)
	{
		Assists A;
		A.bAutoGearbox = bAutoGearbox;
		A.bAutoClutch = bAutoClutch;
		A.bSpeedSensitiveSteering = false;
		Car.SetAssists(A);
		Car.ForceEngineRunning(850.0);
		Car.Condition().FuelL = 39.0;
		// Даём подвеске и мотору «устояться».
		for (int I = 0; I < 60; ++I)
		{
			Step();
		}
	}

	void Step(double Dt = kDt)
	{
		Car.SetInput(In);
		Rig.Step(Dt);
	}

	// Держать скорость простым P-регулятором газа (для тестов расхода и поворота).
	void HoldSpeed(double TargetMs)
	{
		const double Err = TargetMs - Rig.U;
		In.Throttle = Clamp01(0.15 + Err * 0.4);
		In.Brake = 0.0;
	}
};

double Accelerate0To100(double Dt, const char* CsvPath)
{
	Setup S;
	S.Car.RequestGear(1);
	S.In.Throttle = 1.0;
	std::ofstream Csv;
	if (CsvPath)
	{
		Csv.open(CsvPath);
		Csv << "t,speed_kmh,rpm,gear,clutch\n";
	}
	double T = 0.0;
	while (T < 40.0)
	{
		S.Step(Dt);
		T += Dt;
		const Telemetry Tel = S.Car.GetTelemetry();
		if (Csv.is_open())
		{
			Csv << T << "," << S.Rig.SpeedKmh() << "," << Tel.Rpm << "," << Tel.Gear << "," << Tel.ClutchEngagement << "\n";
		}
		if (S.Rig.SpeedKmh() >= 100.0)
		{
			return T;
		}
	}
	return 999.0;
}
} // namespace

int main()
{
	std::printf("Peregon VehicleSimCore tests — %s\n\n", Presets::Sokol1500().Name.c_str());

	std::printf("Шина\n");
	{
		Tire T;
		T.Init(TireParams{});
		CheckRange("пик продольного скольжения", T.PeakSlipLong(), 0.06, 0.18);
		CheckRange("пик угла увода, град", T.PeakSlipAngle() * 180.0 / kPi, 5.0, 11.0);

		// Комбинированное скольжение: при боковом уводе продольная сила должна падать.
		TireState Pure, Combined;
		Pure.SlipLong = Combined.SlipLong = T.PeakSlipLong();
		Combined.SlipLatTan = std::tan(T.PeakSlipAngle());
		SurfaceInfo Asphalt;
		T.ComputeForces(Pure, 3000.0, 20.0, 0.0, 20.0, Asphalt);
		T.ComputeForces(Combined, 3000.0, 20.0, 0.0, 20.0, Asphalt);
		Check(Combined.Fx < 0.8 * Pure.Fx, "эллипс трения: увод режет тягу (Fx комб./чист.)", Combined.Fx / Pure.Fx);
	}

	std::printf("\nДвигатель\n");
	{
		const EngineParams E = Presets::Sokol1500().Engine;
		CheckRange("момент при полном газе на 3400 об/мин, Н·м", EngineModel::NetTorque(E, 3400.0, 1.0, 1.0), 103.9, 104.1);
		Check(EngineModel::NetTorque(E, 4000.0, 0.0, 1.0) < -10.0, "торможение двигателем на 4000 об/мин, Н·м", EngineModel::NetTorque(E, 4000.0, 0.0, 1.0));
		Check(EngineModel::NetTorque(E, 3400.0, 1.0, 0.3) < 75.0, "изношенный мотор (30%) теряет момент, Н·м", EngineModel::NetTorque(E, 3400.0, 1.0, 0.3));

		Setup S;
		for (int I = 0; I < 300; ++I)
		{
			S.Step();
		}
		CheckRange("холостые обороты стабильны", S.Car.GetTelemetry().Rpm, 780.0, 920.0);
	}

	std::printf("\nПуск и глохнущий мотор\n");
	{
		Vehicle Car(Presets::Sokol1500());
		PlanarRig Rig(Car);
		DriverInput In;
		In.bStarter = true;
		for (int I = 0; I < 120; ++I)
		{
			Car.SetInput(In);
			Rig.Step(kDt);
		}
		Check(Car.GetTelemetry().bEngineRunning, "стартер заводит мотор за 2 с", Car.GetTelemetry().Rpm);
		In.bStarter = false;
		for (int I = 0; I < 180; ++I)
		{
			Car.SetInput(In);
			Rig.Step(kDt);
		}
		CheckRange("после пуска выходит на холостые", Car.GetTelemetry().Rpm, 780.0, 1100.0);

		// Ручное сцепление: бросить педаль на первой без газа — заглохнуть.
		Setup M(false, false);
		M.In.Clutch = 1.0;
		M.Step();
		Check(M.Car.RequestGear(1), "первая включается с выжатым сцеплением", 1.0);
		M.In.Clutch = 0.0;
		for (int I = 0; I < 120; ++I)
		{
			M.Step();
		}
		Check(!M.Car.GetTelemetry().bEngineRunning, "бросил сцепление без газа — заглох", M.Car.GetTelemetry().Rpm);

		Setup G(false, false);
		G.In.Clutch = 0.0;
		G.Step();
		Check(!G.Car.RequestGear(1) && G.Car.GetTelemetry().bGearGrind, "без выжатого сцепления передача не входит", 0.0);
	}

	std::printf("\nРазгон и максималка\n");
	{
		const double T100 = Accelerate0To100(kDt, "accel_0_100.csv");
		CheckRange("0-100 км/ч, с (автомат, полный газ)", T100, 13.0, 20.0);
		const double T100Slow = Accelerate0To100(1.0 / 30.0, nullptr);
		const double T100Fast = Accelerate0To100(1.0 / 144.0, nullptr);
		CheckRange("не зависит от FPS: 30 vs 144 Гц, разница %", std::abs(T100Slow - T100Fast) / T100 * 100.0, 0.0, 5.0);

		Setup S;
		S.Car.RequestGear(1);
		S.In.Throttle = 1.0;
		for (int I = 0; I < 60 * 150; ++I)
		{
			S.Step();
		}
		CheckRange("максимальная скорость, км/ч", S.Rig.SpeedKmh(), 135.0, 165.0);
	}

	std::printf("\nТорможение со 100 км/ч\n");
	{
		auto BrakeFrom100 = [](double Grip, bool bAbs) {
			Setup S;
			S.Rig.Surface.Grip = Grip;
			Assists A = S.Car.GetAssists();
			A.bAbs = bAbs;
			S.Car.SetAssists(A);
			S.Rig.U = 100.0 / 3.6;
			S.Car.RequestGear(0);
			// Колёса уже вращаются со скоростью машины.
			for (int I = 0; I < 30; ++I)
			{
				S.Step();
			}
			const double X0 = S.Rig.PosX;
			S.In.Brake = 1.0;
			for (int I = 0; I < 60 * 60 && S.Rig.U > 0.05; ++I)
			{
				S.Step();
			}
			return S.Rig.PosX - X0;
		};
		CheckRange("тормозной путь, асфальт, м", BrakeFrom100(1.0, false), 38.0, 62.0);
		CheckRange("тормозной путь, асфальт + ABS, м", BrakeFrom100(1.0, true), 34.0, 55.0);
		CheckRange("тормозной путь, укатанный снег (0.3), м", BrakeFrom100(0.3, false), 110.0, 220.0);
	}

	std::printf("\nПерегрев тормозов\n");
	{
		// 10 торможений со 100 км/ч подряд (спуск с перевала «в пол»). С ABS: колёса не блокируются,
		// и вся энергия уходит в тормоза (с блокировкой она уходит в шину).
		Setup S;
		Assists A = S.Car.GetAssists();
		A.bAbs = true;
		S.Car.SetAssists(A);
		S.Car.RequestGear(0);
		double First = 0.0, Last = 0.0;
		for (int Stop = 0; Stop < 10; ++Stop)
		{
			S.Rig.U = 100.0 / 3.6;
			S.In.Brake = 0.0;
			for (int I = 0; I < 60; ++I)
			{
				S.Step();
			}
			const double X0 = S.Rig.PosX;
			S.In.Brake = 1.0;
			for (int I = 0; I < 60 * 60 && S.Rig.U > 0.05; ++I)
			{
				S.Step();
			}
			(Stop == 0 ? First : Last) = S.Rig.PosX - X0;
		}
		CheckRange("температура передних тормозов после 10 стопов, °C", S.Car.GetWheel(0).BrakeTempC, 300.0, 800.0);
		Check(Last > First * 1.1, "тормоза «поплыли»: путь вырос (последний/первый)", Last / First);
	}

	std::printf("\nПоворот\n");
	{
		Setup S;
		S.Car.RequestGear(1);
		double MaxLatG = 0.0;
		bool bFinite = true;
		for (int I = 0; I < 60 * 40; ++I)
		{
			const double T = I * kDt;
			S.HoldSpeed(20.0);
			S.In.Steer = T < 10.0 ? 0.0 : std::min(1.0, (T - 10.0) / 25.0);
			S.Step();
			if (T > 10.0)
			{
				MaxLatG = std::max(MaxLatG, std::abs(S.Rig.Ay) / kGravity);
			}
			bFinite = bFinite && std::isfinite(S.Rig.U) && std::isfinite(S.Rig.R);
		}
		Check(bFinite, "численно устойчиво (нет NaN)", 1.0);
		CheckRange("предельное боковое ускорение, g", MaxLatG, 0.65, 1.0);
	}

	std::printf("\nУклон 15%%\n");
	{
		auto ParkOnSlope = [](double Handbrake) {
			Setup S;
			S.Rig.SlopeRad = std::atan(0.15);
			S.In.Handbrake = Handbrake;
			S.Car.RequestGear(0);
			for (int I = 0; I < 600; ++I)
			{
				S.Step();
			}
			return S.Rig.PosX;
		};
		CheckRange("на ручнике стоит, смещение за 10 с, м", std::abs(ParkOnSlope(1.0)), 0.0, 0.05);
		Check(ParkOnSlope(0.0) < -1.0, "без ручника катится назад, м", ParkOnSlope(0.0));
	}

	std::printf("\nРасход топлива\n");
	{
		Setup S;
		S.Car.RequestGear(1);
		for (int I = 0; I < 60 * 60; ++I)
		{
			S.HoldSpeed(25.0);
			S.Step();
		}
		const double Fuel0 = S.Car.Condition().FuelL;
		const double X0 = S.Rig.PosX;
		for (int I = 0; I < 60 * 120; ++I)
		{
			S.HoldSpeed(25.0);
			S.Step();
		}
		const double LPer100 = (Fuel0 - S.Car.Condition().FuelL) / ((S.Rig.PosX - X0) / 100000.0);
		CheckRange("расход на 90 км/ч, л/100 км", LPer100, 5.5, 9.0);
		CheckRange("передача на 90 км/ч (автомат)", S.Car.GetGear(), 4.0, 5.0);
		CheckRange("температура двигателя в движении, °C", S.Car.GetTelemetry().EngineTempC, 75.0, 98.0);
	}

	std::printf("\nИзнос\n");
	{
		auto RearTreadLoss = [](double Throttle) {
			Setup S;
			S.Car.RequestGear(1);
			S.In.Throttle = Throttle;
			const double T0 = S.Car.Condition().Wheels[2].TreadMm;
			for (int I = 0; I < 60 * 15; ++I)
			{
				S.Step();
			}
			return (T0 - S.Car.Condition().Wheels[2].TreadMm) * 1000.0; // мкм
		};
		const double Gentle = RearTreadLoss(0.3);
		const double Hard = RearTreadLoss(1.0);
		Check(Hard > 2.0 * Gentle, "резкий старт стирает заднюю шину сильнее (мкм)", Hard);

		// Затяжной подъём 12% на 30 км/ч с пробитым радиатором.
		Setup S;
		S.Car.Condition().Radiator = 0.05;
		S.Rig.SlopeRad = std::atan(0.12);
		S.Car.RequestGear(1);
		const double Engine0 = S.Car.Condition().Engine;
		double MaxTemp = 0.0;
		for (int I = 0; I < 60 * 600; ++I)
		{
			S.HoldSpeed(8.5);
			S.Step();
			MaxTemp = std::max(MaxTemp, S.Car.GetTelemetry().EngineTempC);
		}
		Check(MaxTemp > 112.0, "пробитый радиатор на подъёме = перегрев, °C", MaxTemp);
		Check(S.Car.Condition().Engine < Engine0 - 0.05, "перегрев убивает мотор (потеря состояния)", Engine0 - S.Car.Condition().Engine);

		// Отсечка в нейтрали долго — перекрут.
		Setup R;
		R.In.Throttle = 1.0;
		for (int I = 0; I < 60 * 60; ++I)
		{
			R.Step();
		}
		Check(R.Car.Condition().Engine < 0.95, "минута на отсечке изнашивает мотор (состояние)", R.Car.Condition().Engine);
	}

	// --- Детали и кузов ---
	{
		std::printf("\nДетали и кузов\n");
		using namespace vsim::parts;
		CarState Wreck = MakeGarageWreck(7u);
		Effects E = Evaluate(Wreck);
		{
			CarState Copy;
			const bool bOk = Unpack(Pack(Wreck), Copy);
			Check(bOk && Copy.Has(Slot::Radiator) == Wreck.Has(Slot::Radiator) && !Copy.Has(Slot::Battery)
					  && std::abs(Copy.PanelAt(Panel::SillL).Holes - Wreck.PanelAt(Panel::SillL).Holes) < 1e-9,
				"сохранение деталей и кузова: Pack → Unpack без потерь", Overall(Copy));
			CarState Bat = MakeFactoryNew(nullptr);
			const PartItem Old = Remove(Bat, Slot::Battery);
			Install(Bat, Old);
			Check(Bat.BatteryCharge > 0.99 && Overall(MakeFactoryNew(nullptr)) > Overall(Wreck) + 0.3, "аккумулятор уносит заряд с собой", Bat.BatteryCharge);
		}
		Check(!E.bSpark && !E.bFuel && E.StarterFactor < 0.01, "гаражная находка: нет искры, топлива, аккумулятора", E.StarterFactor);
		// Собираем: аккумулятор, свечи, карбюратор, фильтр, радиатор, антифриз.
		Check(!CanInstall(Wreck, {Slot::SparkPlugs, 0.6, 0}, true, false).bOk, "под капотом — только с открытым капотом", 0.0);
		Check(!CanInstall(Wreck, {Slot::SparkPlugs, 0.6, 0}, false, true).bOk, "без свечного ключа свечи не поставить", 0.0);
		for (const PartItem& P : {PartItem{Slot::Battery, 0.4, 0}, PartItem{Slot::SparkPlugs, 0.5, 0}, PartItem{Slot::Carburetor, 0.55, 0}, PartItem{Slot::AirFilter, 0.5, 0}})
		{
			Check(CanInstall(Wreck, P, true, true).bOk, std::string("ставится: ") + Info(P.Kind).Name, P.Condition);
			Install(Wreck, P);
		}
		Wreck.BatteryCharge = 0.8;
		E = Evaluate(Wreck);
		Check(E.bSpark && E.bFuel && E.StarterFactor > 0.2, "после сборки заводится", E.StarterFactor);
		Check(E.PowerFactor > 0.4 && E.PowerFactor < 0.95, "изношенные детали — мощность меньше заводской", E.PowerFactor);
		Check(E.CoolingFactor < 0.1, "течёт радиатор и нет антифриза — перегреется", E.CoolingFactor);
		const PartItem Old = Remove(Wreck, Slot::Radiator);
		Check(!Wreck.Has(Slot::Radiator) && Old.Condition < 0.3, "старый радиатор снят", Old.Condition);
		Install(Wreck, {Slot::Radiator, 0.9, 0});
		Wreck.CoolantL = 7.8;
		Check(Evaluate(Wreck).CoolingFactor > 0.15, "новый радиатор и антифриз — охлаждение есть", Evaluate(Wreck).CoolingFactor);
		CarState New = MakeFactoryNew(nullptr);
		const Effects En = Evaluate(New);
		CheckRange("новая машина — заводская мощность", En.PowerFactor, 0.97, 1.03);
		Install(New, {Slot::Turbo, 1.0, 2});
		Check(Evaluate(New).PowerFactor > 1.25, "турбина добавляет мощности", Evaluate(New).PowerFactor);

		// Физика: без свечей не заводится, с ними — заводится.
		{
			Vehicle Car(Presets::Sokol1500());
			PlanarRig Rig(Car);
			Car.Condition().FuelL = 30.0;
			Car.Condition().bSpark = false;
			DriverInput In;
			In.bStarter = true;
			for (int I = 0; I < 180; ++I)
			{
				Car.SetInput(In);
				Rig.Step(kDt);
			}
			Check(!Car.GetTelemetry().bEngineRunning, "без искры стартер крутит, но мотор не заводится", Car.GetTelemetry().Rpm);
			Car.Condition().bSpark = true;
			for (int I = 0; I < 180 && !Car.GetTelemetry().bEngineRunning; ++I)
			{
				Car.SetInput(In);
				Rig.Step(kDt);
			}
			Check(Car.GetTelemetry().bEngineRunning, "со свечами заводится", Car.GetTelemetry().Rpm);
			Car.Condition().StarterFactor = 0.0;
		}

		// Кузов: полный цикл ремонта гнилого крыла.
		PanelState P = Wreck.PanelAt(Panel::SillL);
		Check(NextStep(P) == BodyOp::CutRot, "гнилой порог — сначала вырезать гниль", P.Holes);
		Check(!CanApply(P, BodyOp::PaintIt).bOk, "по ржавчине не красят", 0.0);
		int Steps = 0;
		const float White[3] = {0.9f, 0.9f, 0.88f};
		while (PanelScore(P) < 0.95 && Steps < 20)
		{
			const BodyOp Op = NextStep(P);
			if (!CanApply(P, Op).bOk)
			{
				Check(false, std::string("шаг доступен: ") + OpName(Op), static_cast<double>(Steps));
				break;
			}
			Apply(P, Op, White);
			++Steps;
		}
		std::printf("  порог отремонтирован за %d шагов\n", Steps);
		Check(PanelScore(P) > 0.95 && P.Color[0] > 0.85f, "порог как новый и белый", PanelScore(P));
		Check(Steps >= 5 && Steps <= 10, "цикл: срезать → зачистить → шпаклевать → ошкурить → грунт → краска", Steps);

		// Износ: на голом металле под дождём растёт ржавчина, краска защищает.
		CarState Car2 = MakeFactoryNew(nullptr);
		Car2.PanelAt(Panel::Hood).Paint = 0.0;
		Wear(Car2, 100.0, 48.0, 1.0, true, 90.0);
		Check(Car2.PanelAt(Panel::Hood).Rust > Car2.PanelAt(Panel::Roof).Rust + 0.05, "голый металл ржавеет быстрее крашеного", Car2.PanelAt(Panel::Hood).Rust);
		Check(Car2.BatteryCharge > 0.95, "генератор держит заряд", Car2.BatteryCharge);
	}

	std::printf("\n%d проверок, %d провалено\n", GChecks, GFailures);

	return GFailures == 0 ? 0 : 1;
}
