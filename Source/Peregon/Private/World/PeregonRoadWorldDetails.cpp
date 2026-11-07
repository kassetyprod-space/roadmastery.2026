// Дорожная разметка и придорожные знаки.
// Разметка — отдельная геометрия поверх полотна (а не нарисована в текстуре асфальта), поэтому она меняется
// по месту, как на настоящей трассе: осевая прерывистая (1.5), перед опасными местами — линия приближения
// (1.6), на поворотах и переломах профиля — сплошная (1.1), по краям — сплошная (1.2) с разрывами у съездов,
// «зебры» (1.14.1) у остановок и в городах. Краска местами стёрта.
// Знаки: «опасный поворот» с ограничением скорости перед крутыми поворотами, «пешеходный переход» у зебр,
// указатели расстояний после сёл, рекламные щиты.
#include "World/PeregonRoadWorld.h"

#include "Art/PeregonArtSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "World/PeregonKit.h"
#include "rgen/Noise.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/CollisionProfile.h"

namespace PeregonMarks
{
constexpr double M = 100.0;
constexpr double LineW = 0.12;      // осевая, м
constexpr double EdgeW = 0.15;      // краевая
constexpr double PaintLift = 0.012; // над асфальтом, м
constexpr double SharpCurve = 1.0 / 320.0;
constexpr double SignCurve = 1.0 / 220.0;

bool IsTownPoi(const rgen::Poi& P)
{
	return P.Type == rgen::PoiType::Village || P.Type == rgen::PoiType::Finish || (P.Type == rgen::PoiType::Town && P.S > 1.0);
}

FColor C(float R, float G, float B) { return FLinearColor(R, G, B).ToFColor(false); }
} // namespace PeregonMarks

void APeregonRoadWorld::CrosswalksIn(double S0, double S1, TArray<double>& Out) const
{
	using namespace PeregonMarks;
	for (const rgen::Poi& Poi : Route.Pois())
	{
		if (!IsTownPoi(Poi))
		{
			continue;
		}
		// В селе — у остановок (по обе стороны центра).
		for (double D : {-58.0, 58.0})
		{
			const double S = Poi.S + D;
			if (S >= S0 && S < S1)
			{
				Out.Add(S);
			}
		}
	}
	// В городах, через которые идёт трасса, — несколько переходов.
	for (const rgen::Settlement& Town : Landscape.Settlements())
	{
		if (Town.Size < 1 || Town.S < 1.0)
		{
			continue;
		}
		for (double D : {-420.0, -160.0, 160.0, 420.0})
		{
			const double S = Town.S + D;
			if (S >= S0 && S < S1 && S > 0.0 && S < Route.Length())
			{
				Out.Add(S);
			}
		}
	}
}

void APeregonRoadWorld::BuildMarkings(FChunk& Chunk, double S0, double S1)
{
	using namespace PeregonMarks;
	FPeregonMeshBuffers Paint;
	const double Half = Route.RoadHalfWidth();
	auto P = [this](double S, double T) {
		double X = 0.0;
		double Y = 0.0;
		Route.ToWorld(S, T, X, Y);
		return FVector(X, Y, Route.Sample(S).Z + PaintLift) * M;
	};
	// Полоса краски от Sa до Sb на смещении T шириной W (с шагом 2 м по кривой).
	auto Strip = [&](double Sa, double Sb, double T, double W) {
		const int32 Steps = FMath::Max(1, FMath::CeilToInt((Sb - Sa) / 2.0));
		for (int32 K = 0; K < Steps; ++K)
		{
			const double A = Sa + (Sb - Sa) * K / Steps;
			const double B = Sa + (Sb - Sa) * (K + 1) / Steps;
			Paint.AddQuad(P(A, T - W * 0.5), P(B, T - W * 0.5), P(B, T + W * 0.5), P(A, T + W * 0.5), FVector::UpVector, FVector2D(T * 2.0, A * 0.5),
				FVector2D((T + W) * 2.0, B * 0.5), FColor::White, 0);
		}
	};

	TArray<double> Zebras;
	CrosswalksIn(S0 - 40.0, S1 + 40.0, Zebras);
	auto NearZebra = [&](double S, double R) {
		for (double Z : Zebras)
		{
			if (FMath::Abs(S - Z) < R)
			{
				return true;
			}
		}
		return false;
	};
	// Где осевая сплошная: крутой поворот, перелом профиля (горб — не видно встречку), подходы к «зебре».
	auto Solid = [&](double S) {
		for (double D : {-40.0, -20.0, 0.0, 20.0, 40.0})
		{
			if (FMath::Abs(Route.Sample(S + D).Curvature) > SharpCurve)
			{
				return true;
			}
		}
		const double Crest = Route.Sample(S - 70.0).Grade - Route.Sample(S + 70.0).Grade;
		return Crest > 0.05 || NearZebra(S, 25.0);
	};
	// Съезды на площадки АЗС, кафе, магазинов: краевая прерывистая.
	auto NearPad = [&](double S, int32 Side) {
		for (const rgen::Poi& Poi : Route.Pois())
		{
			const bool bPad = Poi.Type == rgen::PoiType::Fuel || Poi.Type == rgen::PoiType::Cafe || Poi.Type == rgen::PoiType::Shop ||
				Poi.Type == rgen::PoiType::Parts;
			if (bPad && Poi.Side == Side && FMath::Abs(S - Poi.S) < 42.0)
			{
				return true;
			}
		}
		return false;
	};
	const uint32 WearSeed = rgen::Hash(Route.Params().Seed, 0x9A17u);
	auto Worn = [&](double S, uint32 Line) { return rgen::HashToUnit(rgen::Hash(static_cast<uint32>(FMath::FloorToInt(S)), Line, WearSeed)) < 0.035; };

	// Осевая: решаем по метрам, соседние окрашенные метры склеиваем в одну полосу.
	const double Begin = FMath::Max(S0, 0.0);
	const double End = FMath::Min(S1, Route.Length());
	double RunStart = -1.0;
	auto Flush = [&](double S) {
		if (RunStart >= 0.0 && S > RunStart + 0.2)
		{
			Strip(RunStart, S, 0.0, LineW);
		}
		RunStart = -1.0;
	};
	bool bSolidPrev = false;
	bool bApproachPrev = false;
	for (double S = FMath::FloorToDouble(Begin); S < End; S += 1.0)
	{
		if (S < Begin)
		{
			continue;
		}
		// Каждые 10 м — заново решаем, какой тип линии (на метрах между — тот же).
		if (FMath::Fmod(S, 10.0) < 1.0 || S == FMath::FloorToDouble(Begin))
		{
			bSolidPrev = Solid(S);
			bApproachPrev = !bSolidPrev && (Solid(S + 60.0) || Solid(S - 60.0));
		}
		bool bPaint = false;
		if (bSolidPrev)
		{
			bPaint = true;
		}
		else if (bApproachPrev)
		{
			bPaint = FMath::Fmod(S, 8.0) < 6.0;
		}
		else
		{
			bPaint = FMath::Fmod(S, 12.0) < 3.0;
		}
		if (NearZebra(S, 3.0) || Worn(S, 1u))
		{
			bPaint = false;
		}
		if (bPaint && RunStart < 0.0)
		{
			RunStart = S;
		}
		else if (!bPaint)
		{
			Flush(S);
		}
	}
	Flush(End);

	// Края: сплошная, у съездов — прерывистая; на зебре — разрыв.
	for (int32 Side : {-1, 1})
	{
		const double T = Side * (Half - 0.25);
		RunStart = -1.0;
		for (double S = FMath::FloorToDouble(Begin); S < End; S += 1.0)
		{
			if (S < Begin)
			{
				continue;
			}
			bool bPaint = NearPad(S, Side) ? FMath::Fmod(S, 4.0) < 1.0 : true;
			if (NearZebra(S, 2.5) || Worn(S, Side > 0 ? 2u : 3u))
			{
				bPaint = false;
			}
			if (bPaint && RunStart < 0.0)
			{
				RunStart = S;
			}
			else if (!bPaint && RunStart >= 0.0)
			{
				if (S > RunStart + 0.2)
				{
					Strip(RunStart, S, T, EdgeW);
				}
				RunStart = -1.0;
			}
		}
		if (RunStart >= 0.0 && End > RunStart + 0.2)
		{
			Strip(RunStart, End, T, EdgeW);
		}
	}

	// «Зебры»: полосы 0.5 м вдоль дороги, длиной 4 м, через 1 м поперёк; перед ними — стоп-линии.
	TArray<double> Here;
	CrosswalksIn(S0, S1, Here);
	for (double Z : Here)
	{
		for (double T = -Half + 0.45; T < Half - 0.3; T += 1.0)
		{
			Strip(Z - 2.0, Z + 2.0, T + 0.25, 0.5);
		}
		Strip(Z - 6.2, Z - 5.8, Half * 0.5, Half - 0.3);
		Strip(Z + 5.8, Z + 6.2, -Half * 0.5, Half - 0.3);
	}

	if (Paint.Vertices.Num() > 0)
	{
		AddProc(Chunk.Components, Paint, Mat(EPeregonWorldMat::Marking), false, false);
	}
}

// ---------------------------------------------------------------------------------------------
// Знаки
// ---------------------------------------------------------------------------------------------
namespace PeregonMarks
{
// Треугольный предупреждающий знак: красная кайма, белое поле. Center — центр, Fwd — куда смотрит лицо.
void Triangle(FPeregonMeshBuffers& Buf, const FVector& Center, const FVector& Fwd, float Side, bool bInverted = false)
{
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	const float R = Side / FMath::Sqrt(3.f);
	const float Flip = bInverted ? PI : 0.f;
	auto V = [&](float Ang, float Rad, float Lift) {
		return Center + Fwd * Lift + (FVector::UpVector * FMath::Cos(Ang + Flip) + Right * FMath::Sin(Ang + Flip)) * Rad;
	};
	for (const auto& Layer : {TPair<float, FColor>(1.f, C(0.75f, 0.06f, 0.05f)), TPair<float, FColor>(0.72f, C(0.95f, 0.95f, 0.93f))})
	{
		const float Lift = Layer.Key < 1.f ? 0.6f : 0.3f;
		const FVector A = V(0.f, R * Layer.Key, Lift);
		const FVector B = V(2.f * PI / 3.f, R * Layer.Key, Lift);
		const FVector D = V(-2.f * PI / 3.f, R * Layer.Key, Lift);
		Buf.AddQuad(D, B, A, A, Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), Layer.Value, 0);
	}
	// Обратная сторона — серая.
	const FVector A = V(0.f, R, -0.3f);
	const FVector B = V(2.f * PI / 3.f, R, -0.3f);
	const FVector D = V(-2.f * PI / 3.f, R, -0.3f);
	Buf.AddQuad(D, B, A, A, -Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), C(0.5f, 0.5f, 0.52f), 0);
}

// Круглый знак: красное кольцо и белое поле (число — надписью сверху).
void Disc(FPeregonMeshBuffers& Buf, const FVector& Center, const FVector& Fwd, float Radius)
{
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	for (const auto& Layer : {TPair<float, FColor>(1.f, C(0.75f, 0.06f, 0.05f)), TPair<float, FColor>(0.78f, C(0.95f, 0.95f, 0.93f))})
	{
		const float Lift = Layer.Key < 1.f ? 0.6f : 0.3f;
		for (int32 K = 0; K < 20; ++K)
		{
			const float A0 = 2.f * PI * K / 20.f;
			const float A1 = 2.f * PI * (K + 1) / 20.f;
			const FVector P0 = Center + Fwd * Lift + (FVector::UpVector * FMath::Cos(A0) + Right * FMath::Sin(A0)) * Radius * Layer.Key;
			const FVector P1 = Center + Fwd * Lift + (FVector::UpVector * FMath::Cos(A1) + Right * FMath::Sin(A1)) * Radius * Layer.Key;
			Buf.AddQuad(Center + Fwd * Lift, P0, P1, P1, Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), Layer.Value, 0);
			if (Layer.Key >= 1.f)
			{
				Buf.AddQuad(Center - Fwd * 0.3f, P1 - Fwd * (Lift + 0.3f), P0 - Fwd * (Lift + 0.3f), P0 - Fwd * (Lift + 0.3f), -Fwd, FVector2D(0.f, 1.f),
					FVector2D(1.f, 0.f), C(0.5f, 0.5f, 0.52f), 0);
			}
		}
	}
}

// Чёрная стрелка поворота на треугольнике (вертикаль и загиб в сторону поворота).
void CurveArrow(FPeregonMeshBuffers& Buf, const FVector& Center, const FVector& Fwd, float Dir)
{
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	const FColor Ink = C(0.05f, 0.05f, 0.05f);
	const FVector O = Center + Fwd * 0.9f;
	auto Bar = [&](const FVector& A, const FVector& B, float W) {
		const FVector D = (B - A).GetSafeNormal();
		const FVector N = FVector::CrossProduct(Fwd, D) * (W * 0.5f);
		Buf.AddQuad(A - N, B - N, B + N, A + N, Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), Ink, 0);
	};
	const FVector Bottom = O - FVector::UpVector * 22.f - Right * (Dir * 6.f);
	const FVector Bend = O + FVector::UpVector * 4.f - Right * (Dir * 6.f);
	const FVector Tip = O + FVector::UpVector * 14.f + Right * (Dir * 10.f);
	Bar(Bottom, Bend, 6.f);
	Bar(Bend, Tip, 6.f);
	// Наконечник.
	const FVector Along = (Tip - Bend).GetSafeNormal();
	const FVector Side = FVector::CrossProduct(Fwd, Along);
	Buf.AddQuad(Tip + Along * 8.f, Tip - Side * 8.f, Tip + Side * 8.f, Tip + Side * 8.f, Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), Ink, 0);
}

// Синий квадратный знак «пешеходный переход»: белый треугольник, чёрная фигурка, зебра под ногами.
void Crossing(FPeregonMeshBuffers& Buf, const FVector& Center, const FVector& Fwd)
{
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	const FVector Up = FVector::UpVector;
	auto Rect = [&](const FVector& Mid, float W, float H, float Lift, const FColor& Col) {
		const FVector O = Mid + Fwd * Lift;
		Buf.AddQuad(O - Right * W - Up * H, O + Right * W - Up * H, O + Right * W + Up * H, O - Right * W + Up * H, Fwd, FVector2D(0.f, 1.f),
			FVector2D(1.f, 0.f), Col, 0);
	};
	Rect(Center, 30.f, 30.f, 0.3f, C(0.1f, 0.3f, 0.7f));
	Rect(Center, 30.f, 30.f, -0.3f, C(0.5f, 0.5f, 0.52f));
	const FVector T0 = Center + Fwd * 0.6f + Up * 24.f;
	const FVector T1 = Center + Fwd * 0.6f - Up * 20.f - Right * 25.f;
	const FVector T2 = Center + Fwd * 0.6f - Up * 20.f + Right * 25.f;
	Buf.AddQuad(T1, T2, T0, T0, Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), C(0.95f, 0.95f, 0.93f), 0);
	const FColor Ink = C(0.05f, 0.05f, 0.05f);
	Rect(Center + Up * 9.f, 2.2f, 2.2f, 0.9f, Ink);                         // голова
	Rect(Center + Up * 0.f, 2.f, 6.f, 0.9f, Ink);                            // туловище
	Rect(Center - Up * 9.f - Right * 3.f, 1.6f, 5.f, 0.9f, Ink);             // ноги
	Rect(Center - Up * 9.f + Right * 3.f, 1.6f, 5.f, 0.9f, Ink);
	for (int32 K = -2; K <= 2; ++K)
	{
		Rect(Center - Up * 16.f + Right * (K * 6.f), 2.f, 1.2f, 0.9f, Ink); // зебра
	}
}
} // namespace PeregonMarks

void APeregonRoadWorld::BuildRoadsideSigns(FChunk& Chunk, double S0, double S1)
{
	using namespace PeregonMarks;
	FPeregonMeshBuffers Plates;
	const double Half = Route.RoadHalfWidth();
	FPeregonKit Kit(this, RootComponent, Chunk.Components, KitMaterials());
	const FColor PoleC = C(0.55f, 0.56f, 0.58f);
	// Стойка знака: оцинкованная труба.
	auto Pole = [&](const FVector& Foot, float Height) {
		Plates.AddBox(Foot, 0.f, FVector(7.f, 7.f, Height), FVector2D(100.f, 100.f), PoleC, true, false);
	};
	// Направление «лицом к водителю», который едет в сторону Dir (+1 — по S, −1 — против).
	auto Facing = [&](double S, int32 Dir) {
		const double H = Route.Sample(S).Heading;
		return FVector(-FMath::Cos(H) * Dir, -FMath::Sin(H) * Dir, 0.0);
	};

	// «Опасный поворот» + ограничение скорости за 150 м до крутого поворота, для обоих направлений.
	for (double S = FMath::CeilToDouble(S0 / 10.0) * 10.0; S < S1; S += 10.0)
	{
		for (int32 Dir : {1, -1})
		{
			const double Ahead = S + Dir * 150.0;
			const double Before = S + Dir * 140.0;
			if (Ahead < 0.0 || Ahead > Route.Length())
			{
				continue;
			}
			const double K = Route.Sample(Ahead).Curvature;
			if (FMath::Abs(K) <= SignCurve || FMath::Abs(Route.Sample(Before).Curvature) > SignCurve)
			{
				continue;
			}
			const FVector Fwd = Facing(S, Dir);
			const FVector Foot = RouteToWorld(S, Dir * (Half + 2.6), 0.0);
			Pole(Foot, 300.f);
			const FVector Top = Foot + FVector(0, 0, 255.f);
			Triangle(Plates, Top, Fwd, 90.f);
			// Направление поворота для этого водителя: K > 0 — вправо по ходу S; встречному — наоборот.
			// В CurveArrow «+» — вправо в системе знака (это левая рука водителя), поэтому знак минус.
			CurveArrow(Plates, Top - FVector(0, 0, 4.f), Fwd, -(K > 0.0 ? 1.f : -1.f) * Dir);
			const FVector Lim = Foot + FVector(0, 0, 165.f);
			Disc(Plates, Lim, Fwd, 35.f);
			const int32 Speed = FMath::Abs(K) > 1.0 / 120.0 ? 40 : (FMath::Abs(K) > 1.0 / 170.0 ? 50 : 70);
			Kit.Label(FTransform(Fwd.Rotation(), Lim + Fwd * 1.2), FString::FromInt(Speed), 38.f, 26.f, 54, FLinearColor(0.03f, 0.03f, 0.03f),
				FLinearColor(0.95f, 0.95f, 0.93f), 128);
		}
	}

	// «Пешеходный переход» у каждой зебры — с обеих сторон, лицом к своему потоку.
	TArray<double> Zebras;
	CrosswalksIn(S0, S1, Zebras);
	for (double Z : Zebras)
	{
		for (int32 Dir : {1, -1})
		{
			const FVector Foot = RouteToWorld(Z - Dir * 3.0, Dir * (Half + 1.8), 0.0);
			Pole(Foot, 280.f);
			Crossing(Plates, Foot + FVector(0, 0, 250.f), Facing(Z, Dir));
		}
	}

	// Указатели расстояний после каждого села: три следующих пункта с настоящим километражем.
	for (const rgen::Poi& Poi : Route.Pois())
	{
		if (!IsTownPoi(Poi) || Poi.Type == rgen::PoiType::Finish)
		{
			continue;
		}
		const double Board = Poi.S + 380.0;
		if (Board < S0 || Board >= S1 || Board > Route.Length() - 50.0)
		{
			continue;
		}
		FString Text;
		int32 Lines = 0;
		const double Km0 = Route.RealKmAt(Board);
		for (const rgen::Poi& Next : Route.Pois())
		{
			if (Lines >= 3 || !IsTownPoi(Next) || Next.S <= Board)
			{
				continue;
			}
			const int32 Km = FMath::Max(1, FMath::RoundToInt(Route.RealKmAt(Next.S) - Km0));
			Text += FString::Printf(TEXT("%s%s  %d"), Lines > 0 ? TEXT("\n") : TEXT(""), UTF8_TO_TCHAR(Next.Name.c_str()), Km);
			++Lines;
		}
		if (Lines == 0)
		{
			continue;
		}
		const FVector Fwd = Facing(Board, 1);
		const FVector Foot = RouteToWorld(Board, Half + 4.5, 0.0);
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
		const float BoardH = 45.f * Lines + 20.f;
		for (float Off : {-120.f, 120.f})
		{
			Pole(Foot + Right * Off, 220.f + BoardH);
		}
		Plates.AddBox(Foot + FVector(0, 0, 220.f) - Fwd * 2.f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X))), FVector(3.f, 330.f, BoardH + 10.f),
			FVector2D(100.f, 100.f), C(0.95f, 0.95f, 0.95f), true, true);
		Kit.Label(FTransform(Fwd.Rotation(), Foot + Fwd * 1.0 + FVector(0, 0, 225.f + BoardH * 0.5f)), Text, 320.f, BoardH, 30, FLinearColor(0.95f, 0.95f, 0.95f),
			FLinearColor(0.08f, 0.28f, 0.62f), 512);
	}

	// Рекламные щиты: изредка, вдали от сёл и съездов.
	static const TCHAR* Ads[] = {TEXT("ПЕЛЬМЕНИ «СИБИРСКИЕ»\nсделано в Кузбассе"), TEXT("ШИНОМОНТАЖ 24 ЧАСА\nчерез 3 км"), TEXT("БЕРЕГИТЕ ЛЕС\nОТ ПОЖАРА!"),
		TEXT("ПРИСТЕГНИСЬ!\nтебя ждут дома"), TEXT("КАФЕ «У ДОРОГИ»\nгорячие обеды"), TEXT("МЁД С ПАСЕКИ\nпродажа у дома"), TEXT("ГОСТИНИЦА «ТРАССА»\nдуш · стоянка"),
		TEXT("УСТАЛ? ОТДОХНИ!\nсон за рулём опасен"), TEXT("ЛЕСОПИЛКА\nбрус · доска · горбыль"), TEXT("АВТОЗАПЧАСТИ\nдля отечественных авто")};
	static const FLinearColor AdPaper[] = {FLinearColor(0.9f, 0.85f, 0.7f), FLinearColor(0.1f, 0.12f, 0.15f), FLinearColor(0.15f, 0.45f, 0.2f),
		FLinearColor(0.75f, 0.1f, 0.08f), FLinearColor(0.95f, 0.75f, 0.2f), FLinearColor(0.85f, 0.6f, 0.15f), FLinearColor(0.15f, 0.25f, 0.5f),
		FLinearColor(0.95f, 0.95f, 0.92f), FLinearColor(0.45f, 0.3f, 0.18f), FLinearColor(0.2f, 0.2f, 0.22f)};
	static const FLinearColor AdInk[] = {FLinearColor(0.5f, 0.1f, 0.05f), FLinearColor(0.95f, 0.8f, 0.1f), FLinearColor(0.95f, 0.95f, 0.9f),
		FLinearColor(0.95f, 0.95f, 0.9f), FLinearColor(0.2f, 0.1f, 0.05f), FLinearColor(0.2f, 0.1f, 0.05f), FLinearColor(0.95f, 0.95f, 0.9f),
		FLinearColor(0.7f, 0.1f, 0.08f), FLinearColor(0.95f, 0.9f, 0.75f), FLinearColor(0.95f, 0.75f, 0.2f)};
	const uint32 AdSeed = rgen::Hash(Route.Params().Seed, 0xAD5u);
	for (double Slot = FMath::FloorToDouble(S0 / 1900.0) * 1900.0; Slot < S1; Slot += 1900.0)
	{
		const uint32 H = rgen::Hash(static_cast<uint32>(Slot / 1900.0), AdSeed);
		const double S = Slot + 200.0 + 1500.0 * rgen::HashToUnit(H);
		if (S < S0 || S >= S1 || S < 400.0 || S > Route.Length() - 400.0)
		{
			continue;
		}
		bool bClear = true;
		for (const rgen::Poi& Poi : Route.Pois())
		{
			bClear &= FMath::Abs(Poi.S - S) > (IsTownPoi(Poi) ? 600.0 : 150.0);
		}
		for (const rgen::Settlement& Town : Landscape.Settlements())
		{
			bClear &= FMath::Abs(Town.S - S) > Town.Radius + 300.0;
		}
		if (!bClear)
		{
			continue;
		}
		const int32 Side = (H >> 3) % 2 ? 1 : -1;
		const int32 Ad = static_cast<int32>((H >> 7) % UE_ARRAY_COUNT(Ads));
		FVector Base = RouteToWorld(S, Side * (Half + 11.0), 0.0);
		Base.Z -= 10.0;
		const FVector Fwd = Facing(S, Side);
		Kit.Billboard(Base, Fwd.Rotation(), Ads[Ad], AdPaper[Ad], AdInk[Ad]);
	}

	if (Plates.Vertices.Num() > 0)
	{
		AddProc(Chunk.Components, Plates, Mat(EPeregonWorldMat::VertexPaint), false);
	}
}

// ---------------------------------------------------------------------------------------------
// Настоящий город из OpenStreetMap
// ---------------------------------------------------------------------------------------------
void APeregonRoadWorld::LoadRealCity()
{
	bRealCity = false;
	CityMap = rgen::CityData();
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Cities/novosibirsk.city");
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		UE_LOG(LogTemp, Log, TEXT("[Peregon] Нет %s — Новосибирск будет процедурным. Выгрузите город: python Tools/MapPipeline/fetch_city.py novosibirsk"),
			*Path);
		return;
	}
	const std::string Text(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num()));
	std::string Error;
	if (!CityMap.Parse(Text, &Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Peregon] %s не прочитан: %s"), *Path, UTF8_TO_TCHAR(Error.c_str()));
		return;
	}
	double X0 = 1e18, Y0 = 1e18, X1 = -1e18, Y1 = -1e18;
	for (const rgen::CityNode& N : CityMap.Nodes)
	{
		X0 = FMath::Min(X0, N.X);
		Y0 = FMath::Min(Y0, N.Y);
		X1 = FMath::Max(X1, N.X);
		Y1 = FMath::Max(Y1, N.Y);
	}
	for (const rgen::CityBuilding& B : CityMap.Buildings)
	{
		for (const rgen::CityPt& P : B.Ring)
		{
			X0 = FMath::Min(X0, P.X);
			Y0 = FMath::Min(Y0, P.Y);
			X1 = FMath::Max(X1, P.X);
			Y1 = FMath::Max(Y1, P.Y);
		}
	}
	if (X0 > X1)
	{
		return;
	}
	CityBaseZ = Route.Sample(0.0).Z;
	// Трасса в пределах города (с запасом 300 м) — вровень с городом, земля у неё тоже: иначе на старте
	// дорога уходит вниз, а её холмы вылезают сквозь город и дома повисают.
	double LevelS = 0.0;
	for (double S = 0.0; S < FMath::Min(15000.0, Route.Length()); S += 25.0)
	{
		const rgen::RouteSample Smp = Route.Sample(S);
		if (Smp.X > X0 - 300.0 && Smp.X < X1 + 300.0 && Smp.Y > Y0 - 300.0 && Smp.Y < Y1 + 300.0)
		{
			LevelS = S;
		}
	}
	Route.LevelStart(LevelS, 600.0, 400.0);
	Landscape.SetRealCity(CityMap.Name, X0, Y0, X1, Y1, CityBaseZ);
	bRealCity = true;
	UE_LOG(LogTemp, Log, TEXT("[Peregon] Город %s из OSM: %d дорог, %d зданий, %d достопримечательностей"), UTF8_TO_TCHAR(CityMap.Name.c_str()),
		static_cast<int32>(CityMap.Ways.size()), static_cast<int32>(CityMap.Buildings.size()), static_cast<int32>(CityMap.Landmarks.size()));
}

namespace PeregonMarks
{
// Восьмиугольник «STOP» (2.5).
void Octagon(FPeregonMeshBuffers& Buf, const FVector& Center, const FVector& Fwd, float Radius)
{
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	for (int32 K = 0; K < 8; ++K)
	{
		const float A0 = 2.f * PI * (K + 0.5f) / 8.f;
		const float A1 = 2.f * PI * (K + 1.5f) / 8.f;
		const FVector P0 = Center + Fwd * 0.3f + (FVector::UpVector * FMath::Cos(A0) + Right * FMath::Sin(A0)) * Radius;
		const FVector P1 = Center + Fwd * 0.3f + (FVector::UpVector * FMath::Cos(A1) + Right * FMath::Sin(A1)) * Radius;
		Buf.AddQuad(Center + Fwd * 0.3f, P0, P1, P1, Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), C(0.75f, 0.06f, 0.05f), 0);
		Buf.AddQuad(Center - Fwd * 0.3f, P1 - Fwd * 0.6f, P0 - Fwd * 0.6f, P0 - Fwd * 0.6f, -Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f),
			C(0.5f, 0.5f, 0.52f), 0);
	}
}

// Синий круг «Круговое движение» (4.3): белое кольцо из трёх дуг-стрелок.
void Roundabout(FPeregonMeshBuffers& Buf, const FVector& Center, const FVector& Fwd, float Radius)
{
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	auto P = [&](float A, float R, float Lift) { return Center + Fwd * Lift + (FVector::UpVector * FMath::Cos(A) + Right * FMath::Sin(A)) * R; };
	for (int32 K = 0; K < 24; ++K)
	{
		const float A0 = 2.f * PI * K / 24.f;
		const float A1 = 2.f * PI * (K + 1) / 24.f;
		Buf.AddQuad(Center + Fwd * 0.3f, P(A0, Radius, 0.3f), P(A1, Radius, 0.3f), P(A1, Radius, 0.3f), Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f),
			C(0.1f, 0.3f, 0.7f), 0);
		Buf.AddQuad(Center - Fwd * 0.3f, P(A1, Radius, -0.3f), P(A0, Radius, -0.3f), P(A0, Radius, -0.3f), -Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f),
			C(0.5f, 0.5f, 0.52f), 0);
		if (K % 8 < 6)
		{
			Buf.AddQuad(P(A0, Radius * 0.42f, 0.6f), P(A1, Radius * 0.42f, 0.6f), P(A1, Radius * 0.62f, 0.6f), P(A0, Radius * 0.62f, 0.6f), Fwd,
				FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), C(0.95f, 0.95f, 0.93f), 0);
		}
		else if (K % 8 == 6)
		{
			Buf.AddQuad(P(A0, Radius * 0.32f, 0.6f), P(A0, Radius * 0.72f, 0.6f), P(A1 + 0.15f, Radius * 0.52f, 0.6f), P(A1 + 0.15f, Radius * 0.52f, 0.6f), Fwd,
				FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), C(0.95f, 0.95f, 0.93f), 0); // наконечник
		}
	}
}
} // namespace PeregonMarks

UMaterialInterface* APeregonRoadWorld::CityMaterial(rgen::CityMat Cm) const
{
	switch (Cm)
	{
	case rgen::CityMat::Asphalt: return Mat(EPeregonWorldMat::Road);
	case rgen::CityMat::Marking: return Mat(EPeregonWorldMat::Marking);
	case rgen::CityMat::Sidewalk: return Mat(EPeregonWorldMat::Sidewalk);
	case rgen::CityMat::Curb: return Mat(EPeregonWorldMat::Concrete);
	case rgen::CityMat::Ground: return Mat(EPeregonWorldMat::Yard);
	case rgen::CityMat::Grass: return Mat(EPeregonWorldMat::Grass);
	case rgen::CityMat::Water: return Mat(EPeregonWorldMat::Water);
	case rgen::CityMat::Concrete: return Mat(EPeregonWorldMat::Concrete);
	case rgen::CityMat::Steel: return Mat(EPeregonWorldMat::Steel);
	case rgen::CityMat::Ballast: return Mat(EPeregonWorldMat::Shoulder);
	case rgen::CityMat::WallPanel: return Mat(EPeregonWorldMat::Panel);
	case rgen::CityMat::WallBrick: return Mat(EPeregonWorldMat::Brick);
	case rgen::CityMat::WallStucco: return Mat(EPeregonWorldMat::Stucco);
	case rgen::CityMat::WallOffice: return Mat(EPeregonWorldMat::Ribbon);
	case rgen::CityMat::WallIndustrial: return Mat(EPeregonWorldMat::Silicate);
	case rgen::CityMat::WallHouse: return Mat(EPeregonWorldMat::Wood);
	case rgen::CityMat::Roof: return Mat(EPeregonWorldMat::CityRoof);
	case rgen::CityMat::LitWindow: return Mat(EPeregonWorldMat::LitWindow);
	case rgen::CityMat::LodMass: return Mat(EPeregonWorldMat::VertexPaint);
	default: return nullptr;
	}
}

void APeregonRoadWorld::ClearCityTile(FCityTileSlot& Slot)
{
	for (TWeakObjectPtr<UActorComponent>& Comp : Slot.Comps)
	{
		if (Comp.IsValid())
		{
			Comp->DestroyComponent();
		}
	}
	Slot.Comps.Reset();
	Slot.Level = 0;
}

void APeregonRoadWorld::BuildRealCity()
{
	using namespace PeregonMarks;
	for (TPair<FIntPoint, FCityTileSlot>& Pair : CityTiles)
	{
		ClearCityTile(Pair.Value);
	}
	CityTiles.Reset();
	CityRes = rgen::CityResult();
	if (!bRealCity)
	{
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	rgen::CityBuildOptions Opt;
	// Землю города не кладём рядом с трассой — у неё своя земля чанков.
	for (double S = 30.0; S < FMath::Min(4000.0, Route.Length()); S += 25.0)
	{
		const rgen::RouteSample Smp = Route.Sample(S);
		Opt.Keepout.push_back(rgen::CityPt{Smp.X, Smp.Y});
	}
	CityRes = rgen::BuildCity(CityMap, Opt);
	CityTileSize = Opt.TileSize;
	// Город грузится квадратами по мере приближения (UpdateCityStreaming): здесь — только раскладка по квадратам.
	for (int32 I = 0; I < static_cast<int32>(CityRes.Tiles.size()); ++I)
	{
		const rgen::CityTile& T = CityRes.Tiles[static_cast<size_t>(I)];
		CityTiles.FindOrAdd(FIntPoint(T.TX, T.TY)).Index = I;
	}
	FChunk Holder;
	FPeregonKit Kit(this, RootComponent, Holder.Components, KitMaterials());
	const double Z0 = CityBaseZ;
	for (int32 I = 0; I < static_cast<int32>(CityRes.Props.size()); ++I)
	{
		const rgen::CityProp& P = CityRes.Props[static_cast<size_t>(I)];
		if (P.Kind == rgen::CityPropKind::Landmark)
		{
			// Достопримечательности видны издалека — строятся сразу. Главный фасад — к центру (у театра — на площадь Ленина).
			const FVector At = FVector(P.X, P.Y, P.Z + Z0) * M;
			FVector ToCenter = FVector(CityMap.CenterX - P.X, CityMap.CenterY - P.Y, 0.0);
			if (ToCenter.Size() < 30.0)
			{
				ToCenter = FRotator(0.f, static_cast<float>(P.Yaw), 0.f).Vector();
			}
			Kit.Landmark(UTF8_TO_TCHAR(P.Text.c_str()), At - FVector(0, 0, 10.f), static_cast<float>(P.Yaw), static_cast<float>(P.Value) * 100.f,
				ToCenter.GetSafeNormal());
			continue;
		}
		const FIntPoint Key(FMath::FloorToInt(P.X / CityTileSize), FMath::FloorToInt(P.Y / CityTileSize));
		CityTiles.FindOrAdd(Key).Props.Add(I);
	}
	Scenery.Append(Holder.Components);
	bCityStreamAll = true;
	UE_LOG(LogTemp, Log,
		TEXT("[Peregon] Город %s рассчитан за %.0f мс: квадратов %d, улиц %d, перекрёстков %d (слито узлов %d), мостов %d, домов %d (убрано: с трассы %d, с проезжей части %d), дворовых проездов %d, %d треугольников"),
		UTF8_TO_TCHAR(CityMap.Name.c_str()), (FPlatformTime::Seconds() - StartTime) * 1000.0, CityTiles.Num(), CityRes.Edges, CityRes.Junctions,
		CityRes.MergedJunctionNodes, CityRes.Bridges, CityRes.BuildingsBuilt, CityRes.BuildingsCleared, CityRes.BuildingsOnRoads, CityRes.Driveways,
		static_cast<int32>(CityRes.TriangleCount()));
}

// Подгрузка города вокруг игрока: рядом — полный квадрат (коллизия, знаки, фонари), дальше — коробки домов
// на земле без коллизии и теней, ещё дальше — ничего (там горизонт и дымка).
void APeregonRoadWorld::UpdateCityStreaming(const FVector& Focus)
{
	if (!bRealCity || CityTiles.Num() == 0)
	{
		return;
	}
	const double Fx = Focus.X / PeregonMarks::M;
	const double Fy = Focus.Y / PeregonMarks::M;
	const double Half = CityTileSize * 0.5;
	struct FWant
	{
		FIntPoint Key;
		int32 Level = 0;
		double Dist = 0.0;
	};
	TArray<FWant> Ups;
	int32 Downs = 0;
	for (TPair<FIntPoint, FCityTileSlot>& Pair : CityTiles)
	{
		const double Cx = (Pair.Key.X + 0.5) * CityTileSize;
		const double Cy = (Pair.Key.Y + 0.5) * CityTileSize;
		const double Dx = FMath::Max(0.0, FMath::Abs(Fx - Cx) - Half);
		const double Dy = FMath::Max(0.0, FMath::Abs(Fy - Cy) - Half);
		const double D = FMath::Sqrt(Dx * Dx + Dy * Dy);
		const int32 Cur = Pair.Value.Level;
		int32 Want = D < CityFullRadius ? 2 : (D < CityLodRadius ? 1 : 0);
		if (Want < Cur)
		{
			// Запас, чтобы квадрат на границе не мигал туда-сюда.
			const double KeepD = Cur == 2 ? CityFullRadius + 150.0 : CityLodRadius + 300.0;
			if (D < KeepD)
			{
				Want = Cur;
			}
		}
		if (Want == Cur)
		{
			continue;
		}
		if (Want < Cur)
		{
			if (Downs < 8 || bCityStreamAll)
			{
				BuildCityTile(Pair.Value, Want);
				++Downs;
			}
			continue;
		}
		Ups.Add({Pair.Key, Want, D});
	}
	Ups.Sort([](const FWant& A, const FWant& B) { return A.Dist < B.Dist; });
	// За кадр — один полный квадрат и до четырёх дальних (при старте — всё сразу, без «проявления»).
	int32 FullBudget = bCityStreamAll ? MAX_int32 : 1;
	int32 LodBudget = bCityStreamAll ? MAX_int32 : 4;
	for (const FWant& W : Ups)
	{
		int32& Budget = W.Level == 2 ? FullBudget : LodBudget;
		if (Budget <= 0)
		{
			continue;
		}
		--Budget;
		BuildCityTile(CityTiles[W.Key], W.Level);
	}
	bCityStreamAll = false;
}

void APeregonRoadWorld::BuildCityTile(FCityTileSlot& Slot, int32 Level)
{
	using namespace PeregonMarks;
	ClearCityTile(Slot);
	Slot.Level = Level;
	if (Level == 0)
	{
		return;
	}
	const double Z0 = CityBaseZ;
	const bool bFull = Level >= 2;
	// Компоненты квадрата: дорожные поверхности (асфальт под колёсами), земля (трава), прочее, без коллизии.
	enum class EGroup : uint8
	{
		Road,
		Ground,
		Other,
		NoCollision
	};
	auto GroupOf = [bFull](rgen::CityMat Cm) {
		if (!bFull)
		{
			return EGroup::NoCollision;
		}
		switch (Cm)
		{
		case rgen::CityMat::Asphalt:
		case rgen::CityMat::Sidewalk:
		case rgen::CityMat::Curb:
		case rgen::CityMat::Concrete:
		case rgen::CityMat::Steel:
		case rgen::CityMat::Ballast: return EGroup::Road;
		case rgen::CityMat::Ground:
		case rgen::CityMat::Grass: return EGroup::Ground;
		case rgen::CityMat::Marking:
		case rgen::CityMat::Water:
		case rgen::CityMat::LitWindow: return EGroup::NoCollision;
		default: return EGroup::Other;
		}
	};
	auto Wanted = [bFull](rgen::CityMat Cm) {
		if (bFull)
		{
			return Cm != rgen::CityMat::LodMass;
		}
		return Cm == rgen::CityMat::Ground || Cm == rgen::CityMat::Grass || Cm == rgen::CityMat::Water || Cm == rgen::CityMat::Asphalt ||
			Cm == rgen::CityMat::LodMass;
	};
	if (Slot.Index >= 0)
	{
		const rgen::CityTile& Tile = CityRes.Tiles[static_cast<size_t>(Slot.Index)];
		UProceduralMeshComponent* Comps[4] = {nullptr, nullptr, nullptr, nullptr};
		int32 Next[4] = {0, 0, 0, 0};
		for (int32 Mi = 0; Mi < static_cast<int32>(rgen::CityMat::Count); ++Mi)
		{
			const rgen::CitySection& Sec = Tile.Sections[Mi];
			const rgen::CityMat CMat = static_cast<rgen::CityMat>(Mi);
			if (Sec.I.empty() || !Wanted(CMat))
			{
				continue;
			}
			const int32 G = static_cast<int32>(GroupOf(CMat));
			if (!Comps[G])
			{
				UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
				Mesh->SetMobility(EComponentMobility::Movable);
				Mesh->SetupAttachment(RootComponent);
				Mesh->bUseAsyncCooking = true;
				Mesh->bUseComplexAsSimpleCollision = true;
				const bool bCollide = G != static_cast<int32>(EGroup::NoCollision);
				Mesh->SetCollisionProfileName(bCollide ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
				if (G == static_cast<int32>(EGroup::Road))
				{
					Mesh->SetPhysMaterialOverride(AsphaltPhys);
				}
				else if (G == static_cast<int32>(EGroup::Ground))
				{
					Mesh->SetPhysMaterialOverride(GrassPhys);
				}
				if (G == static_cast<int32>(EGroup::NoCollision))
				{
					Mesh->SetCastShadow(bFull && CMat != rgen::CityMat::Marking && CMat != rgen::CityMat::Water && CMat != rgen::CityMat::LitWindow);
					Mesh->SetCanEverAffectNavigation(false);
				}
				Mesh->RegisterComponent();
				Slot.Comps.Add(Mesh);
				Comps[G] = Mesh;
			}
			const int32 NV = static_cast<int32>(Sec.VertexCount());
			TArray<FVector> V;
			TArray<FVector> N;
			TArray<FVector2D> UV;
			TArray<FColor> Col;
			TArray<int32> Tri;
			V.SetNumUninitialized(NV);
			N.SetNumUninitialized(NV);
			UV.SetNumUninitialized(NV);
			Col.SetNumUninitialized(NV);
			for (int32 K = 0; K < NV; ++K)
			{
				V[K] = FVector(Sec.P[K * 3], Sec.P[K * 3 + 1], static_cast<double>(Sec.P[K * 3 + 2]) + Z0) * M;
				N[K] = FVector(Sec.N[K * 3], Sec.N[K * 3 + 1], Sec.N[K * 3 + 2]);
				UV[K] = FVector2D(Sec.UV[K * 2], Sec.UV[K * 2 + 1]);
				Col[K] = FColor(Sec.C[K * 4], Sec.C[K * 4 + 1], Sec.C[K * 4 + 2], Sec.C[K * 4 + 3]);
			}
			Tri.Append(Sec.I.data(), static_cast<int32>(Sec.I.size()));
			const int32 Index = Next[G]++;
			const bool bCollision = G != static_cast<int32>(EGroup::NoCollision);
			Comps[G]->CreateMeshSection(Index, V, Tri, N, UV, Col, TArray<FProcMeshTangent>(), bCollision);
			Comps[G]->SetMaterial(Index, CityMaterial(CMat));
		}
	}
	if (bFull && Slot.Props.Num() > 0)
	{
		AddCityProps(Slot);
	}
}

// Реквизит квадрата: опоры и таблички — один меш с цветом вершин, головы фонарей — светящиеся.
void APeregonRoadWorld::AddCityProps(FCityTileSlot& Slot)
{
	using namespace PeregonMarks;
	const double Z0 = CityBaseZ;
	FPeregonMeshBuffers Plates;
	FPeregonMeshBuffers Heads;
	TArray<FPeregonLightSpot> LampSpots;
	FPeregonKit Kit(this, RootComponent, Slot.Comps, KitMaterials());
	const FColor PoleC = C(0.55f, 0.56f, 0.58f);
	const FColor DarkC = C(0.12f, 0.12f, 0.13f);
	// Деревья — инстансами (в парке их сотни), заводятся по первому дереву квадрата.
	FChunk Trees;
	UInstancedStaticMeshComponent* PineTrunks = nullptr;
	UInstancedStaticMeshComponent* PineCrowns = nullptr;
	UInstancedStaticMeshComponent* BirchTrunks = nullptr;
	UInstancedStaticMeshComponent* BirchCrowns = nullptr;
	const bool bBare = bSeasonApplied && AppliedSeason.Leaves < 0.15;
	// Площадки, киоски и рынки — готовые модели из набора застройки (как в процедурных городах).
	std::vector<rgen::Building> Amenities;
	FPeregonMeshBuffers Blocks; // бетонные блоки на въездах во дворы (с коллизией)
	for (const int32 Pi : Slot.Props)
	{
		const rgen::CityProp& P = CityRes.Props[static_cast<size_t>(Pi)];
		const FVector At = FVector(P.X, P.Y, P.Z + Z0) * PeregonMarks::M;
		const float Yaw = static_cast<float>(P.Yaw);
		const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
		switch (P.Kind)
		{
		case rgen::CityPropKind::StreetLamp:
		{
			// Бетонная опора 9 м с консолью над проезжей частью.
			Plates.AddBox(At, Yaw, FVector(24.f, 24.f, 900.f), FVector2D(100.f, 100.f), PoleC, true, false);
			const FVector Arm = At + FVector(0, 0, 880.f);
			Plates.AddBox(Arm + Fwd * 90.f, Yaw, FVector(180.f, 8.f, 8.f), FVector2D(100.f, 100.f), PoleC, true, true);
			Heads.AddBox(Arm + Fwd * 190.f - FVector(0, 0, 14.f), Yaw, FVector(60.f, 26.f, 14.f), FVector2D(100.f, 100.f), FColor::White, true, true);
			FPeregonLightSpot Spot;
			Spot.Location = Arm + Fwd * 190.f - FVector(0, 0, 30.f);
			Spot.Direction = FVector::DownVector;
			Spot.Seed = static_cast<uint32>(static_cast<int64>(P.X * 7.0 + P.Y * 13.0));
			LampSpots.Add(Spot);
			break;
		}
		case rgen::CityPropKind::TrafficLight:
		{
			// Стойка, корпус, три сигнала (верхний красный, нижний зелёный), козырьки.
			Plates.AddBox(At, Yaw, FVector(12.f, 12.f, 330.f), FVector2D(100.f, 100.f), PoleC, true, false);
			const FVector Box = At + FVector(0, 0, 300.f) + Fwd * 12.f;
			Plates.AddBox(Box - FVector(0, 0, 105.f), Yaw, FVector(26.f, 36.f, 105.f), FVector2D(100.f, 100.f), DarkC, true, true);
			const FColor Lamps[] = {C(0.85f, 0.08f, 0.06f), C(0.75f, 0.6f, 0.08f), C(0.1f, 0.7f, 0.25f)};
			for (int32 K = 0; K < 3; ++K)
			{
				const FVector L = Box - FVector(0, 0, 25.f + K * 32.f) + Fwd * 14.f;
				Plates.AddBox(L - FVector(0, 0, 11.f), Yaw, FVector(3.f, 22.f, 22.f), FVector2D(100.f, 100.f), Lamps[K], true, true);
				Plates.AddBox(L + FVector(0, 0, 12.f) + Fwd * 8.f, Yaw, FVector(16.f, 26.f, 2.f), FVector2D(100.f, 100.f), DarkC, true, true);
			}
			break;
		}
		case rgen::CityPropKind::SignYield:
		case rgen::CityPropKind::SignStop:
		case rgen::CityPropKind::SignCrossing:
		case rgen::CityPropKind::SignRoundabout:
		case rgen::CityPropKind::SignSpeed:
		{
			// Стойка Ø7 см; щит — перед стойкой (лицом к водителю), на двух хомутах, а не насквозь.
			Plates.AddBox(At, Yaw, FVector(7.f, 7.f, 270.f), FVector2D(100.f, 100.f), PoleC, true, false);
			const FVector Head = At + FVector(0, 0, 235.f) + Fwd * 6.f;
			for (float Dz : {-18.f, 18.f})
			{
				Plates.AddBox(At + FVector(0, 0, 235.f + Dz - 2.f) + Fwd * 2.5f, Yaw, FVector(6.f, 9.f, 4.f), FVector2D(100.f, 100.f), DarkC, true, true);
			}
			if (P.Kind == rgen::CityPropKind::SignYield)
			{
				Triangle(Plates, Head, Fwd, 90.f, true);
			}
			else if (P.Kind == rgen::CityPropKind::SignStop)
			{
				Octagon(Plates, Head, Fwd, 36.f);
				Kit.Label(FTransform(Fwd.Rotation(), Head + Fwd * 1.0), TEXT("STOP"), 44.f, 18.f, 40, FLinearColor(0.95f, 0.95f, 0.93f),
					FLinearColor(0.75f, 0.06f, 0.05f), 128);
			}
			else if (P.Kind == rgen::CityPropKind::SignCrossing)
			{
				Crossing(Plates, Head, Fwd);
			}
			else if (P.Kind == rgen::CityPropKind::SignRoundabout)
			{
				Roundabout(Plates, Head, Fwd, 35.f);
			}
			else
			{
				Disc(Plates, Head, Fwd, 35.f);
				Kit.Label(FTransform(Fwd.Rotation(), Head + Fwd * 1.2), FString::FromInt(P.Value), 38.f, 26.f, 54, FLinearColor(0.03f, 0.03f, 0.03f),
					FLinearColor(0.95f, 0.95f, 0.93f), 128);
			}
			break;
		}
		case rgen::CityPropKind::StreetName:
		{
			// Табличка на кронштейне сбоку от стойки: две стороны, между ними — пластина.
			Plates.AddBox(At, Yaw, FVector(7.f, 7.f, 300.f), FVector2D(100.f, 100.f), PoleC, true, false);
			const FVector Side90 = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
			const FVector PlateC = At + FVector(0, 0, 270.f) + Side90 * 60.f;
			Plates.AddBox(PlateC - FVector(0, 0, 13.f), Yaw, FVector(2.f, 112.f, 26.f), FVector2D(100.f, 100.f), C(0.08f, 0.24f, 0.55f), true, true);
			Plates.AddBox(At + FVector(0, 0, 268.f) + Side90 * 4.f, Yaw + 90.f, FVector(8.f, 3.f, 3.f), FVector2D(100.f, 100.f), DarkC, true, true);
			for (float Side : {1.f, -1.f})
			{
				const FVector N = Fwd * Side;
				Kit.Label(FTransform(N.Rotation(), PlateC + N * 1.6), UTF8_TO_TCHAR(P.Text.c_str()), 110.f, 22.f, 26,
					FLinearColor(0.95f, 0.95f, 0.95f), FLinearColor(0.08f, 0.24f, 0.55f), 256);
			}
			break;
		}
		case rgen::CityPropKind::Tree:
		{
			const double Scale = P.Size;
			const double TreeYaw = P.Yaw;
			const FVector Base = At - FVector(0, 0, 20.f);
			if (P.Value == 1)
			{
				if (!PineTrunks)
				{
					PineTrunks = AddInstanced(Trees, CylinderMesh, EPeregonWorldMat::PineBark, true);
					PineCrowns = AddInstanced(Trees, ConeMesh, EPeregonWorldMat::Needles, false);
					PineCrowns->SetCastShadow(true);
				}
				const double H = 13.0 * Scale;
				PineTrunks->AddInstance(FTransform(FRotator(0, TreeYaw, 0), Base + FVector(0, 0, H * 50.0), FVector(0.38 * Scale, 0.38 * Scale, H)));
				PineCrowns->AddInstance(FTransform(FRotator(0, TreeYaw, 0), Base + FVector(0, 0, H * 100.0 * 0.55), FVector(4.6 * Scale, 4.6 * Scale, H * 0.7)));
				PineCrowns->AddInstance(FTransform(FRotator(0, TreeYaw + 40, 0), Base + FVector(0, 0, H * 100.0 * 0.85), FVector(3.0 * Scale, 3.0 * Scale, H * 0.45)));
			}
			else
			{
				if (!BirchTrunks)
				{
					BirchTrunks = AddInstanced(Trees, CylinderMesh, EPeregonWorldMat::BirchBark, true);
					BirchCrowns = AddInstanced(Trees, SphereMesh, EPeregonWorldMat::Leaves, false);
					BirchCrowns->SetCastShadow(true);
				}
				const double H = 11.0 * Scale;
				BirchTrunks->AddInstance(FTransform(FRotator(0, TreeYaw, 0), Base + FVector(0, 0, H * 50.0), FVector(0.28 * Scale, 0.28 * Scale, H)));
				if (!bBare)
				{
					BirchCrowns->AddInstance(FTransform(FRotator(0, TreeYaw, 0), Base + FVector(0, 0, H * 100.0 * 0.72), FVector(5.0 * Scale, 5.0 * Scale, 6.5 * Scale)));
				}
			}
			break;
		}
		case rgen::CityPropKind::Playground:
		case rgen::CityPropKind::Kiosk:
		case rgen::CityPropKind::Market:
		{
			rgen::Building B;
			B.Kind = P.Kind == rgen::CityPropKind::Playground ? rgen::BuildingKind::Playground
				: (P.Kind == rgen::CityPropKind::Kiosk ? rgen::BuildingKind::Kiosk : rgen::BuildingKind::Market);
			B.X = P.X;
			B.Y = P.Y;
			B.Z = P.Z + Z0;
			B.Yaw = FMath::DegreesToRadians(P.Yaw);
			B.Length = P.Kind == rgen::CityPropKind::Market ? P.Size : 4.0;
			B.Width = P.Kind == rgen::CityPropKind::Market ? P.Size2 : 3.0;
			B.Floors = P.Value;
			B.Seed = static_cast<uint32_t>(static_cast<int64>(P.X * 13.0) ^ static_cast<int64>(P.Y * 7.0));
			Amenities.push_back(B);
			break;
		}
		case rgen::CityPropKind::Bench:
			Kit.Bench(At, FRotator(0.f, Yaw, 0.f));
			break;
		case rgen::CityPropKind::BusStop:
			Kit.BusStop(At, FRotator(0.f, Yaw, 0.f), UTF8_TO_TCHAR(P.Text.c_str()), P.Value % 3);
			break;
		case rgen::CityPropKind::Barrier:
		{
			// Ряд фундаментных блоков ФБС 2.4 × 0.6 × 0.6 м поперёк въезда, с зазорами для пешеходов.
			const int32 Count = FMath::Max(1, FMath::CeilToInt(static_cast<float>(P.Size) / 3.0f));
			const float Span = static_cast<float>(P.Size) * 100.f;
			for (int32 K = 0; K < Count; ++K)
			{
				const float Off = Count == 1 ? 0.f : -Span * 0.5f + 120.f + (Span - 240.f) * K / (Count - 1);
				const FColor Gray = C(0.58f + 0.05f * (K % 2), 0.58f, 0.56f);
				Blocks.AddBox(At + Fwd * Off, Yaw, FVector(240.f, 60.f, 60.f), FVector2D(100.f, 100.f), Gray, true, false);
			}
			break;
		}
		case rgen::CityPropKind::SignNoEntry:
		{
			Plates.AddBox(At, Yaw, FVector(7.f, 7.f, 270.f), FVector2D(100.f, 100.f), PoleC, true, false);
			const FVector Head = At + FVector(0, 0, 235.f) + Fwd * 6.f;
			for (float Dz : {-18.f, 18.f})
			{
				Plates.AddBox(At + FVector(0, 0, 235.f + Dz - 2.f) + Fwd * 2.5f, Yaw, FVector(6.f, 9.f, 4.f), FVector2D(100.f, 100.f), DarkC, true, true);
			}
			// 3.1: красный круг, белая горизонтальная полоса; оборот — серый.
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
			const float Rad = 35.f;
			for (int32 K = 0; K < 20; ++K)
			{
				const float A0 = 2.f * PI * K / 20.f;
				const float A1 = 2.f * PI * (K + 1) / 20.f;
				const FVector P0 = (FVector::UpVector * FMath::Cos(A0) + Right * FMath::Sin(A0)) * Rad;
				const FVector P1 = (FVector::UpVector * FMath::Cos(A1) + Right * FMath::Sin(A1)) * Rad;
				Plates.AddQuad(Head + Fwd * 0.4f, Head + Fwd * 0.4f + P0, Head + Fwd * 0.4f + P1, Head + Fwd * 0.4f + P1, Fwd, FVector2D(0.f, 1.f),
					FVector2D(1.f, 0.f), C(0.75f, 0.06f, 0.05f), 0);
				Plates.AddQuad(Head - Fwd * 0.4f, Head - Fwd * 0.4f + P1, Head - Fwd * 0.4f + P0, Head - Fwd * 0.4f + P0, -Fwd, FVector2D(0.f, 1.f),
					FVector2D(1.f, 0.f), C(0.5f, 0.5f, 0.52f), 0);
			}
			const FVector Bc = Head + Fwd * 0.9f;
			Plates.AddQuad(Bc - Right * 25.f - FVector::UpVector * 6.f, Bc + Right * 25.f - FVector::UpVector * 6.f, Bc + Right * 25.f + FVector::UpVector * 6.f,
				Bc - Right * 25.f + FVector::UpVector * 6.f, Fwd, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), C(0.95f, 0.95f, 0.93f), 0);
			break;
		}
		default: break;
		}
	}
	if (!Amenities.empty())
	{
		Kit.Buildings(Amenities);
	}
	if (Blocks.Triangles.Num() > 0)
	{
		AddProc(Slot.Comps, Blocks, Mat(EPeregonWorldMat::VertexPaint), true);
	}
	Slot.Comps.Append(Trees.Components);
	AddProc(Slot.Comps, Plates, Mat(EPeregonWorldMat::VertexPaint), false);
	if (UProceduralMeshComponent* Glow = AddProc(Slot.Comps, Heads, Mat(EPeregonWorldMat::LampGlow), false, false))
	{
		for (FPeregonLightSpot& Spot : LampSpots)
		{
			Spot.Anchor = Glow;
		}
		PeregonLights::Spots().Append(LampSpots);
	}
}
