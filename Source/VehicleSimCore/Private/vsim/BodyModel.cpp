// Peregon — VehicleSimCore
// «Сокол-1500» — оригинальный седан в духе 70-х: строгий «трёхобъёмный» кузов, прямоугольные фары,
// хромированные бамперы, рамочные двери. Вся геометрия описана функциями:
//   • боковина кузова — полуширина H(x, z) (сечение с рёбром на уровне z = 8 и «заваленным» верхом),
//     скругления углов в плане и арки колёс; двери, крылья, пороги — куски этой одной поверхности,
//     поэтому кузов цельный, а щели между панелями настоящие;
//   • капот, крыша, крышка багажника — поверхности z(x, y) с выпуклостью;
//   • «оранжерея» (стойки, стёкла) — наклонная плоскость Gy(z) и линейчатые поверхности стёкол.
// Панели имеют толщину и изнанку: открытая дверь видна как дверь, а не как лист бумаги.
#include "vsim/BodyModel.h"

#include "MeshPrims.h"

#include <algorithm>
#include <cmath>

namespace vmodel
{
using namespace prim;

namespace
{
// ---------------------------------------------------------------------------------------------
// Размеры, см
// ---------------------------------------------------------------------------------------------
constexpr float kXf = 203.f;         // передняя панель
constexpr float kXr = -204.f;        // задняя панель
constexpr float kRcF = 4.f;         // скругление углов в плане
constexpr float kRcR = 4.5f;
constexpr float kCrease = 8.f;       // боковое ребро
constexpr float kArchR = 35.f;       // арки
constexpr float kArchZ = -21.f;
constexpr float kWheelXF = 116.4f;   // оси колёс (как у физики)
constexpr float kWheelXR = -126.f;
constexpr float kDoorBottom = -22.f;
constexpr float kGap = 0.6f;         // щель между панелями
constexpr float kBelt = 40.f;        // низ остекления
constexpr float kRail = 81.f;        // верх проёмов
constexpr float kRoofEdge = 84.f;
constexpr float kRoofCrown = 1.8f;
constexpr float kTumble = 0.19f;     // завал боковин «оранжереи»

constexpr float kFDoorFront = 89.7f;
constexpr float kFDoorRear = -12.f;
constexpr float kBFront = -12.4f;
constexpr float kBRear = -17.6f;
constexpr float kRDoorFront = -18.f;
constexpr float kRDoorRear = -104.7f;
constexpr float kFenderRear = 90.3f;
constexpr float kQuarterFront = -105.3f;
constexpr float kHoodRear = 93.6f;
constexpr float kTrunkFront = -111.f;

// Узлы (шарниры) подвижных частей.
const V3 kHoodHinge(93.f, 0.f, 39.f);
const V3 kTrunkHinge(-110.5f, 0.f, 38.5f);
const V3 kGloveHinge(72.4f, 32.f, 21.6f);
const V3 kSteeringCenter(60.f, -35.f, 40.f);
const V3 kKeyPos(72.6f, -10.5f, 30.f); // правее щитка, в ровной части торпедо (а не в выступе приборов)
const V3 kKnobPos(72.8f, -60.f, 31.f);
const V3 kGearPos(34.f, 0.f, -13.5f);
const V3 kHandbrakePos(-4.f, 0.f, -13.3f);
constexpr float kPedalX = 84.f;
constexpr float kPedalZ = 2.f;
constexpr float kGaugeX = 71.8f;
constexpr float kGaugeZ = 41.5f;
constexpr float kBigGauge = 5.6f;
constexpr float kSmallGauge = 2.3f;

V3 DoorHinge(Part Door)
{
	switch (Door)
	{
	case Part::DoorFL: return V3(90.2f, -81.f, 0.f);
	case Part::DoorFR: return V3(90.2f, 81.f, 0.f);
	case Part::DoorRL: return V3(-17.8f, -81.f, 0.f);
	default: return V3(-17.8f, 81.f, 0.f);
	}
}

// ---------------------------------------------------------------------------------------------
// Поверхности
// ---------------------------------------------------------------------------------------------
float ZTop(float X)
{
	if (X > 93.f)
	{
		return Lerpf(39.f, 37.5f, Clampf((X - 93.f) / (kXf - 93.f), 0.f, 1.f)); // капот почти плоский
	}
	if (X < -110.f)
	{
		return Lerpf(39.f, 38.5f, Clampf((-110.f - X) / (-110.f - kXr), 0.f, 1.f));
	}
	return 39.f;
}

const Curve& LowerSection()
{
	// Строгий борт: почти вертикальный, низ подогнут коротко и резко (порог — ступенькой).
	static const Curve C({-31.f, -29.5f, -27.5f, -24.f, kCrease}, {75.f, 78.6f, 79.6f, 79.9f, 80.f});
	return C;
}

const Curve& UpperSection()
{
	// Над ребром — плоско, у кромки капота — короткий скос (угловатое «плечо», а не бочка).
	static const Curve C({0.f, 0.7f, 0.88f, 0.95f, 0.985f, 1.f}, {80.f, 79.9f, 79.6f, 78.9f, 77.6f, 76.8f});
	return C;
}

// Полуширина сечения кузова на высоте Z при верхней кромке Top.
float Section2D(float Z, float Top)
{
	if (Z <= kCrease)
	{
		return LowerSection()(Z);
	}
	return UpperSection()(Clampf((Z - kCrease) / (Top - kCrease), 0.f, 1.03f));
}

float EndInset(float X)
{
	const float FrontStart = kXf - kRcF;
	if (X > FrontStart)
	{
		const float D = std::min(X - FrontStart, kRcF);
		return kRcF - std::sqrt(std::max(0.f, kRcF * kRcF - D * D));
	}
	const float RearStart = kXr + kRcR;
	if (X < RearStart)
	{
		const float D = std::min(RearStart - X, kRcR);
		return kRcR - std::sqrt(std::max(0.f, kRcR * kRcR - D * D));
	}
	return 0.f;
}

float HalfW(float X, float Z) { return Section2D(Z, ZTop(X)) - EndInset(X); }
V3 SidePoint(float X, float Z, float S) { return V3(X, S * HalfW(X, Z), Z); }

float ArchTop(float X, float Center)
{
	const float D = X - Center;
	if (std::fabs(D) >= kArchR)
	{
		return -1e6f;
	}
	return kArchZ + std::sqrt(kArchR * kArchR - D * D);
}
float Arches(float X) { return std::max(ArchTop(X, kWheelXF), ArchTop(X, kWheelXR)); }

// Направление «наружу» для боковин: от продольной оси, на концах — вперёд/назад.
V3 SideOut(const V3& P)
{
	const V3 Spine(Clampf(P.X, kXr + 40.f, kXf - 40.f), 0.f, P.Z);
	return Normalize(P - Spine, V3(0.f, P.Y >= 0.f ? 1.f : -1.f, 0.f));
}

// «Оранжерея».
float Gy(float Z) { return 72.f - (Z - kBelt) * kTumble; }
V3 GlassSide(float X, float Z, float S, float Inset = 0.f) { return V3(X, S * (Gy(Z) - Inset), Z); }
float RoofHalf() { return Gy(kRoofEdge); }
float RoofZ(float Y)
{
	const float T = Y / RoofHalf();
	return kRoofEdge + kRoofCrown * (1.f - T * T);
}
float Wrap(float T) { return 1.f - T * T; }
V3 WindshieldBase(float T) { return V3(86.f + 0.8f * Wrap(T), T * Gy(kBelt), kBelt); }
V3 WindshieldTop(float T) { return V3(57.f + 0.8f * Wrap(T), T * RoofHalf(), RoofZ(T * RoofHalf())); }
V3 RearGlassBase(float T) { return V3(-108.f - 0.6f * Wrap(T), T * Gy(kBelt), kBelt); }
V3 RearGlassTop(float T) { return V3(-76.f - 0.8f * Wrap(T), T * RoofHalf(), RoofZ(T * RoofHalf())); }
// Передний край проёма передней двери (за А-стойкой) и задний край стекла задней двери.
float AFrontEdge(float Z) { return 77.f - (Z - kBelt) * (31.f / 44.f); }
float CRearEdge(float Z) { return -84.f + (Z - kBelt) * (15.5f / 41.f); }

// ---------------------------------------------------------------------------------------------
// Выборки
// ---------------------------------------------------------------------------------------------
std::vector<float> SortedUnique(std::vector<float> V, float MinStep)
{
	std::sort(V.begin(), V.end());
	std::vector<float> Out;
	for (float X : V)
	{
		if (Out.empty() || X - Out.back() > MinStep)
		{
			Out.push_back(X);
		}
	}
	return Out;
}

// Колонки боковины: равномерно + сгущение у арок и на скруглениях углов.
std::vector<float> SideColumns(float X0, float X1)
{
	std::vector<float> Xs = Range(X0, X1, 5.f);
	for (int32_t K = 0; K <= 24; ++K)
	{
		const float Phi = Pi * static_cast<float>(K) / 24.f;
		for (float Center : {kWheelXF, kWheelXR})
		{
			const float X = Center + kArchR * std::cos(Phi);
			if (X > X0 && X < X1)
			{
				Xs.push_back(X);
			}
		}
	}
	for (int32_t K = 0; K <= 10; ++K)
	{
		const float Th = 0.5f * Pi * static_cast<float>(K) / 10.f;
		const float XF = kXf - kRcF + kRcF * std::sin(Th);
		const float XR = kXr + kRcR - kRcR * std::sin(Th);
		if (XF > X0 && XF < X1) { Xs.push_back(XF); }
		if (XR > X0 && XR < X1) { Xs.push_back(XR); }
	}
	return SortedUnique(Xs, 0.2f);
}

// Ряды по высоте: внизу — абсолютные z, выше ребра — доля до верхней кромки (повторяет скругление).
const std::vector<float>& LowerRows()
{
	static const std::vector<float> R = {-31.f, -29.5f, -28.f, -26.5f, -25.f, -23.f, -20.f, -15.f, -10.f, -5.f, 0.f, 4.f, kCrease};
	return R;
}
const std::vector<float>& UpperRows()
{
	static const std::vector<float> R = {0.12f, 0.26f, 0.4f, 0.54f, 0.66f, 0.76f, 0.84f, 0.9f, 0.945f, 0.975f, 1.f};
	return R;
}
float RowZ(float X, size_t Row)
{
	const std::vector<float>& L = LowerRows();
	if (Row < L.size())
	{
		return L[Row];
	}
	return kCrease + UpperRows()[Row - L.size()] * (ZTop(X) - kCrease);
}
size_t RowCount() { return LowerRows().size() + UpperRows().size(); }

// Кусок боковины между X0..X1, по высоте Lo(x)..Hi(x).
Grid SidePanel(float S, float X0, float X1, const std::function<float(float)>& Lo, const std::function<float(float)>& Hi)
{
	const std::vector<float> Xs = SideColumns(X0, X1);
	const int32_t NV = static_cast<int32_t>(RowCount());
	return SampleGrid(
		static_cast<int32_t>(Xs.size()), NV,
		[&](int32_t I, int32_t J) {
			const float X = Xs[static_cast<size_t>(I)];
			const float Z = Clampf(RowZ(X, static_cast<size_t>(J)), Lo(X), Hi(X));
			return std::make_pair(X, Z);
		},
		[S](float X, float Z) { return SidePoint(X, Z, S); }, SideOut, 0.05f);
}

// Четырёхугольник (x, z) на боковине «оранжереи». Порядок углов: P0(0,0) P1(1,0) P2(1,1) P3(0,1).
Grid GlassSidePatch(float S, float X0, float Z0, float X1, float Z1, float X2, float Z2, float X3, float Z3, int32_t NA, int32_t NB,
	float Inset = 0.f)
{
	auto Surf = [=](float A, float B) {
		const float X = (1.f - B) * Lerpf(X0, X1, A) + B * Lerpf(X3, X2, A);
		const float Z = (1.f - B) * Lerpf(Z0, Z1, A) + B * Lerpf(Z3, Z2, A);
		return GlassSide(X, Z, S, Inset);
	};
	return SampleGrid(Range(0.f, 1.f, 1.f / static_cast<float>(NA)), Range(0.f, 1.f, 1.f / static_cast<float>(NB)), Surf,
		[S](const V3&) { return V3(0.f, S, 0.f); }, 0.002f);
}

HintFn Constant(const V3& D)
{
	return [D](const V3&) { return D; };
}

void Shift(std::vector<Section>& Sections, const V3& By)
{
	for (Section& S : Sections)
	{
		for (V3& P : S.Positions)
		{
			P = P + By;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Колесо: шина 175/70 R13, штампованный диск с колпаком. Ось — Axis, наружу — +Axis.
// ---------------------------------------------------------------------------------------------
void BuildWheel(MeshSet& M, const V3& Center, const V3& Axis, const V3& Ref)
{
	const std::vector<ProfilePoint> Tire = {{16.4f, -7.6f}, {19.f, -8.6f}, {23.f, -9.f}, {26.5f, -8.4f}, {28.1f, -7.1f}, {28.5f, -5.f},
		{28.5f, 5.f}, {28.1f, 7.1f}, {26.5f, 8.4f}, {23.f, 9.f}, {19.f, 8.6f}, {16.4f, 7.6f}};
	Lathe(M, Mat::Rubber, Center, Axis, Ref, Tire, 36);
	// Протектор: три кольцевые канавки.
	for (float H : {-2.6f, 0.f, 2.6f})
	{
		Lathe(M, Mat::BlackTrim, Center, Axis, Ref, {{28.56f, H - 0.35f}, {28.56f, H + 0.35f}}, 36, 1.f, 0.f);
	}
	Lathe(M, Mat::Metal, Center, Axis, Ref, {{16.3f, -7.2f}, {16.3f, 6.3f}}, 24, 1.f, 0.f);
	{
		// Поперечные ламели протектора (шашки) и боковой поясок с «надписью» — шина читается как шина.
		const V3 Ax0 = Normalize(Axis);
		const V3 Q0 = Normalize(Ref - Ax0 * Dot(Ref, Ax0));
		const V3 Q1 = Cross(Ax0, Q0);
		for (int32_t K = 0; K < 60; ++K)
		{
			const float T = 2.f * Pi * static_cast<float>(K) / 60.f;
			const V3 Radial = Q0 * std::cos(T) + Q1 * std::sin(T);
			const V3 Tangent = Cross(Ax0, Radial);
			for (float H : {-5.4f, -1.3f, 1.3f, 5.4f})
			{
				const float Shift2 = (K % 2 == 0) ? 0.6f : -0.6f;
				Box(M, Mat::BlackTrim, Center + Radial * 28.5f + Ax0 * (H + (std::fabs(H) < 2.f ? 0.f : Shift2)), V3(0.22f, std::fabs(H) < 2.f ? 0.9f : 1.6f, 0.1f),
					Tangent, Ax0, Radial);
			}
		}
		for (int32_t K = 0; K < 24; ++K)
		{
			if (K % 6 == 5)
			{
				continue;
			}
			const float T = 2.f * Pi * static_cast<float>(K) / 24.f;
			const V3 Radial = Q0 * std::cos(T) + Q1 * std::sin(T);
			const V3 Tangent = Cross(Ax0, Radial);
			Box(M, Mat::BlackTrim, Center + Radial * 22.5f + Ax0 * 9.f, V3(1.6f, 0.08f, 0.9f), Tangent, Ax0, Radial);
		}
		// Вентиль.
		const V3 ValveAt = Center + Q0 * 14.8f + Ax0 * 5.6f;
		Tube(M, Mat::Metal, {ValveAt, ValveAt + Ax0 * 1.6f + Q0 * 0.4f}, 0.35f, 6);
	}
	const std::vector<ProfilePoint> Dish = {{17.2f, 6.9f}, {16.2f, 7.45f, true}, {14.8f, 6.1f}, {12.9f, 3.3f}, {10.2f, 3.0f}, {9.7f, 4.3f}};
	Lathe(M, Mat::Metal, Center, Axis, Ref, Dish, 30, 0.f, 1.f);
	const std::vector<ProfilePoint> Cap = {{0.f, 8.7f}, {3.6f, 8.5f}, {7.f, 7.5f}, {9.4f, 5.7f}, {9.95f, 4.4f}};
	Lathe(M, Mat::Chrome, Center, Axis, Ref, Cap, 24, 0.f, 1.f);
	// Окна в диске — по ним видно вращение.
	const V3 Ax = Normalize(Axis);
	const V3 R0 = Normalize(Ref - Ax * Dot(Ref, Ax));
	const V3 R1 = Cross(Ax, R0);
	for (int32_t K = 0; K < 6; ++K)
	{
		const float T = 2.f * Pi * static_cast<float>(K) / 6.f;
		const V3 Radial = R0 * std::cos(T) + R1 * std::sin(T);
		const V3 Tangent = Cross(Ax, Radial);
		RoundedBox(M, Mat::BlackTrim, Center + Ax * 3.25f + Radial * 11.6f, V3(1.3f, 0.12f, 2.1f), 0.6f, Radial, Ax, Tangent, 2);
	}
}

// ---------------------------------------------------------------------------------------------
// Части
// ---------------------------------------------------------------------------------------------
struct Builder
{
	Model Out;

	void Add(Part Id, MeshSet& M, const V3& Origin, bool bShiftToOrigin = true)
	{
		PartMesh P;
		P.Id = Id;
		P.Origin = Origin;
		P.Sections = M.Take();
		if (bShiftToOrigin)
		{
			Shift(P.Sections, -Origin);
		}
		Out.Parts.push_back(std::move(P));
	}

	void Hit(Part Owner, const char* Tag, const V3& WorldCenter, const V3& Half, const V3& Origin)
	{
		Out.HitBoxes.push_back({Owner, Tag, WorldCenter - Origin, Half});
	}
};

void BuildLowerBody(MeshSet& M)
{
	for (float S : {-1.f, 1.f})
	{
		// Передние крылья и задние боковины (с арками).
		EmitThick(M,
			SidePanel(S, kFenderRear, kXf, [](float X) { return std::max(-27.f, ArchTop(X, kWheelXF)); }, [](float X) { return ZTop(X); }),
			1.f, Mat::Paint, Mat::Metal, Mat::Paint, [](const V3& P) { return -SideOut(P); });
		EmitThick(M,
			SidePanel(S, kXr, kQuarterFront, [](float X) { return std::max(-27.f, ArchTop(X, kWheelXR)); }, [](float X) { return ZTop(X); }),
			1.f, Mat::Paint, Mat::Metal, Mat::Paint, [](const V3& P) { return -SideOut(P); });
		// Пороги под дверями и низ средней стойки.
		EmitThick(M,
			SidePanel(S, kWheelXR + kArchR, kWheelXF - kArchR, [](float X) { return std::max(-31.f, Arches(X)); },
				[](float) { return kDoorBottom - kGap; }),
			1.f, Mat::Paint, Mat::Metal, Mat::Paint, [](const V3& P) { return -SideOut(P); });
		EmitThick(M, SidePanel(S, kBRear, kBFront, [](float) { return kDoorBottom - kGap; }, [](float X) { return ZTop(X); }), 6.f, Mat::Paint,
			Mat::Interior, Mat::Paint, [S](const V3&) { return V3(0.f, -S, 0.f); });
		// Ступенька порога (видна при открытой двери).
		EmitGrid(M, Mat::BlackTrim,
			SampleGrid(Range(-88.f, 78.f, 8.f), Range(0.f, 1.f, 0.5f),
				[S](float X, float B) {
					const float Z = kDoorBottom - kGap;
					return V3(X, S * Lerpf(64.f, HalfW(X, Z) - 1.f, B), Z);
				},
				Constant(V3(0.f, 0.f, 1.f)), 0.01f));
	}

	// Передняя и задняя панели.
	auto EndFace = [&](float X, float Rc, float Dir) {
		std::vector<float> Zs = {-27.f, -24.f, -20.f, -15.f, -10.f, -5.f, 0.f, 4.f, kCrease};
		const float Top = ZTop(X);
		for (float R : UpperRows())
		{
			Zs.push_back(std::min(kCrease + R * (Top - kCrease), Top - kGap));
		}
		Zs = SortedUnique(Zs, 0.05f);
		EmitThick(M,
			SampleGrid(Range(-1.f, 1.f, 0.05f), Zs, [=](float T, float Z) { return V3(X, T * (Section2D(Z, Top) - Rc), Z); },
				Constant(V3(Dir, 0.f, 0.f)), 0.005f),
			1.f, Mat::Paint, Mat::Metal, Mat::Paint, Constant(V3(-Dir, 0.f, 0.f)));
	};
	EndFace(kXf, kRcF, 1.f);
	EndFace(kXr, kRcR, -1.f);

	// Жабо под лобовым стеклом (пластик) и полка под задним стеклом.
	EmitGrid(M, Mat::BlackTrim,
		SampleGrid(Range(0.f, 1.f, 0.25f), Range(-1.f, 1.f, 0.1f),
			[](float A, float T) {
				const V3 B = WindshieldBase(T);
				const float Z = Lerpf(kBelt, ZTop(93.f) + 2.f * Wrap(T), A);
				return V3(Lerpf(B.X, 93.f, A), T * (HalfW(90.f, ZTop(90.f)) - 0.3f), Z);
			},
			Constant(V3(0.f, 0.f, 1.f)), 0.002f));
	for (int32_t K = 0; K < 7; ++K) // решётка воздухозаборника
	{
		const float Y = -36.f + 12.f * static_cast<float>(K);
		RoundedBox(M, Mat::BlackTrim, V3(91.f, Y, 41.2f - 0.0002f * Y * Y), V3(1.6f, 4.5f, 0.35f), 0.3f, V3(1.f, 0.f, 0.f),
			V3(0.f, 1.f, 0.f), V3(0.f, 0.f, 1.f), 1);
	}
	EmitGrid(M, Mat::Paint,
		SampleGrid(Range(0.f, 1.f, 0.5f), Range(-1.f, 1.f, 0.1f),
			[](float A, float T) {
				const V3 B = RearGlassBase(T);
				const float Z = Lerpf(ZTop(-110.4f) + 1.6f * Wrap(T) - 0.2f, kBelt, A);
				return V3(Lerpf(-110.4f, B.X, A), T * (HalfW(-108.f, ZTop(-108.f)) - 0.3f), Z);
			},
			Constant(V3(0.f, 0.f, 1.f)), 0.002f));

	// Днище, подкрылки.
	auto Floor = [&](float X0, float X1, float Y0, float Y1) {
		EmitGrid(M, Mat::Metal,
			SampleGrid(Range(X0, X1, 20.f), Range(Y0, Y1, 20.f), [](float X, float Y) { return V3(X, Y, -31.2f); }, Constant(V3(0.f, 0.f, -1.f)),
				0.1f));
	};
	Floor(-198.f, 198.f, -52.f, 52.f);
	for (float S : {-1.f, 1.f})
	{
		Floor(kWheelXR + kArchR, kWheelXF - kArchR, S > 0 ? 52.f : -73.f, S > 0 ? 73.f : -52.f);
		Floor(kWheelXF + kArchR, 198.f, S > 0 ? 52.f : -70.f, S > 0 ? 70.f : -52.f);
		Floor(-198.f, kWheelXR - kArchR, S > 0 ? 52.f : -70.f, S > 0 ? 70.f : -52.f);
		for (float Center : {kWheelXF, kWheelXR})
		{
			const float Inner = Center > 0.f ? 44.f : 55.f;
			// Спереди за аркой (x < 94) — уже салон: там подкрылок начинается от боковины ниши ног (y = 64).
			const float Split = Center > 0.f ? std::acos(Clampf((94.f - Center) / (kArchR + 0.5f), -1.f, 1.f)) : Pi;
			auto Liner = [&](float Phi0, float Phi1, float Y0) {
				EmitGrid(M, Mat::BlackTrim,
					SampleGrid(Range(Phi0, Phi1, Pi / 20.f), Range(Y0, 78.8f, 7.f),
						[S, Center](float Phi, float Y) {
							return V3(Center + (kArchR + 0.5f) * std::cos(Phi), S * Y, kArchZ + (kArchR + 0.5f) * std::sin(Phi));
						},
						[Center](const V3& P) { return V3(Center, P.Y, kArchZ) - P; }, 0.01f));
			};
			Liner(0.f, Split, Inner);
			if (Split < Pi)
			{
				Liner(Split, Pi, 64.f);
			}
			if (Center < 0.f)
			{
				EmitGrid(M, Mat::BlackTrim,
					SampleGrid(Range(0.f, Pi, Pi / 20.f), Range(0.f, kArchR + 0.5f, 6.f),
						[S, Center, Inner](float Phi, float R) { return V3(Center + R * std::cos(Phi), S * Inner, kArchZ + R * std::sin(Phi)); },
						Constant(V3(0.f, S, 0.f)), 0.01f));
			}
		}
	}
	// Выхлоп и глушитель.
	Tube(M, Mat::Metal, {V3(100.f, 22.f, -31.5f), V3(40.f, 24.f, -32.f), V3(-60.f, 24.f, -32.f), V3(-140.f, 28.f, -31.f), V3(-200.f, 32.f, -31.5f),
		V3(-213.f, 32.f, -32.5f)}, 2.4f, 10);
	RoundedBox(M, Mat::Metal, V3(-165.f, 30.f, -31.f), V3(22.f, 10.f, 5.f), 4.f);
}

void BuildGreenhouse(MeshSet& M)
{
	for (float S : {-1.f, 1.f})
	{
		const HintFn Inward = [S](const V3&) { return V3(0.f, -S, 0.f); };
		// А-стойка, рейка крыши, B-стойка, C-стойка.
		EmitThick(M, GlassSidePatch(S, 86.f, kBelt, 55.f, kRoofEdge, 46.f, kRoofEdge, 77.f, kBelt, 12, 3), 1.5f, Mat::Paint, Mat::Interior,
			Mat::Paint, Inward);
		EmitThick(M, GlassSidePatch(S, AFrontEdge(kRail), kRail, AFrontEdge(kRoofEdge), kRoofEdge, -72.f, kRoofEdge, CRearEdge(kRail), kRail, 2, 24),
			1.5f, Mat::Paint, Mat::Interior, Mat::Paint, Inward);
		EmitThick(M, GlassSidePatch(S, kBFront, kBelt, kBFront, kRail, kBRear, kRail, kBRear, kBelt, 8, 2), 1.5f, Mat::Paint, Mat::Interior,
			Mat::Paint, Inward);
		EmitThick(M, GlassSidePatch(S, CRearEdge(kBelt), kBelt, CRearEdge(kRail), kRail, -72.f, kRoofEdge, -108.f, kBelt, 10, 10), 1.5f,
			Mat::Paint, Mat::Interior, Mat::Paint, Inward);
		// Водосток по краю крыши.
		Tube(M, Mat::Chrome, {V3(53.f, S * (RoofHalf() + 0.5f), kRoofEdge + 0.2f), V3(-70.f, S * (RoofHalf() + 0.5f), kRoofEdge + 0.2f)}, 0.7f,
			8);
		// Молдинг по низу стёкол на кузове (над С-стойкой).
		Tube(M, Mat::Chrome, {V3(-84.5f, S * 73.4f, 39.6f), V3(-107.f, S * 73.4f, 39.6f)}, 0.7f, 8);
	}

	// Крыша: снаружи краска, изнутри потолок.
	const Grid Roof = SampleGrid(Range(0.f, 1.f, 1.f / 24.f), Range(-1.f, 1.f, 0.1f),
		[](float A, float T) {
			const float Y = T * RoofHalf();
			return V3(Lerpf(RearGlassTop(T).X, WindshieldTop(T).X, A), Y, RoofZ(Y));
		},
		Constant(V3(0.f, 0.f, 1.f)), 0.002f);
	EmitThick(M, Roof, 2.f, Mat::Paint, Mat::Headliner, Mat::Paint, Constant(V3(0.f, 0.f, -1.f)));

	// Лобовое и заднее стёкла с резиновыми уплотнителями.
	auto Glass = [&](V3 (*Base)(float), V3 (*Top)(float), const V3& Out) {
		Grid G = SampleGrid(Range(0.f, 1.f, 0.125f), Range(-1.f, 1.f, 0.1f), [=](float A, float T) { return Lerp(Base(T), Top(T), A); },
			Constant(Out), 0.002f);
		for (size_t K = 0; K < G.P.size(); ++K)
		{
			G.P[K] = G.P[K] - G.N[K] * 0.8f;
		}
		EmitGrid(M, Mat::Glass, G);
		std::vector<V3> Loop;
		for (float T : Range(-1.f, 1.f, 0.1f))
		{
			Loop.push_back(Base(T));
		}
		for (float T : Range(1.f, -1.f, 0.1f))
		{
			Loop.push_back(Top(T));
		}
		Tube(M, Mat::BlackTrim, Loop, 1.f, 6, true, false);
		// Хромовый кант по уплотнителю.
		const V3 OutN = Normalize(Out);
		std::vector<V3> Reveal;
		for (const V3& P : Loop)
		{
			Reveal.push_back(P + OutN * 0.85f);
		}
		Tube(M, Mat::Chrome, Reveal, 0.3f, 6, true, false);
	};
	Glass(WindshieldBase, WindshieldTop, V3(1.f, 0.f, 1.f));
	Glass(RearGlassBase, RearGlassTop, V3(-1.f, 0.f, 1.f));


	// Плёнка воды на лобовом стекле — снаружи, поверх стекла; её прозрачность задаёт игра (дождь, дворники).
	{
		Grid G = SampleGrid(Range(0.f, 1.f, 0.125f), Range(-1.f, 1.f, 0.1f), [](float A, float T) { return Lerp(WindshieldBase(T), WindshieldTop(T), A); },
			Constant(V3(1.f, 0.f, 1.f)), 0.002f);
		for (size_t K = 0; K < G.P.size(); ++K)
		{
			G.P[K] = G.P[K] - G.N[K] * 0.6f;
		}
		EmitGrid(M, Mat::Water, G, 0.025f);
	}
}

// Дворники: отдельные узлы, лежат вдоль низа лобового стекла; ось — перпендикуляр к стеклу.
void BuildWipers(Builder& B)
{
	const V3 GlassN = Normalize(V3(1.f, 0.f, 0.7f));
	auto OnGlass = [&](float T, float A, float Lift) { return Lerp(WindshieldBase(T), WindshieldTop(T), A) + GlassN * Lift; };
	for (float T0 : {-0.05f, 0.6f})
	{
		MeshSet M;
		const V3 Spindle = WindshieldBase(T0) + V3(2.5f, 0.f, 0.4f);
		Lathe(M, Mat::BlackTrim, Spindle, V3(0.f, 0.f, 1.f), V3(1.f, 0.f, 0.f), {{1.2f, 0.f}, {1.1f, 1.4f}, {0.f, 1.5f}}, 10);
		Tube(M, Mat::BlackTrim, {WindshieldBase(T0) + V3(2.5f, 0.f, 1.6f), OnGlass(T0 - 0.08f, 0.06f, 1.3f), OnGlass(T0 - 0.45f, 0.1f, 1.3f)}, 0.45f, 6);
		Tube(M, Mat::BlackTrim, {OnGlass(T0 - 0.1f, 0.06f, 0.8f), OnGlass(T0 - 0.68f, 0.12f, 0.8f)}, 0.4f, 6);
		B.Add(T0 < 0.f ? Part::WiperL : Part::WiperR, M, Spindle, true);
	}
	B.Out.WiperAxis = GlassN;
	// Щётка лежит влево (−Y). Поворот вокруг оси n на угол θ: d' ≈ d + θ (n × d); n × (−Y) смотрит вниз,
	// значит поднимает щётку отрицательный угол.
	B.Out.WiperSweepDeg = -74.f;
}

void BuildFrontRear(MeshSet& M, Model& Out)
{
	const V3 Ex(1.f, 0.f, 0.f);
	const V3 Ey(0.f, 1.f, 0.f);
	const V3 Ez(0.f, 0.f, 1.f);
	// Перед: строгая чёрная панель во всю ширину, в ней — прямоугольные фары по краям и решётка из тонких
	// горизонтальных хромовых планок между ними; посередине — вертикальная планка с эмблемой «С». Под фарами —
	// прямоугольные оранжевые поворотники. Все углы — почти острые.
	Box(M, Mat::BlackTrim, V3(kXf + 0.4f, 0.f, 17.f), V3(0.8f, 72.f, 11.f));
	for (float Z : {9.5f, 12.5f, 15.5f, 18.5f, 21.5f, 24.5f})
	{
		Box(M, Mat::Chrome, V3(kXf + 1.25f, 0.f, Z), V3(0.3f, 44.f, 0.35f));
	}
	Tube(M, Mat::Chrome, {V3(kXf + 1.3f, -72.6f, 5.6f), V3(kXf + 1.3f, 72.6f, 5.6f), V3(kXf + 1.3f, 72.6f, 28.4f), V3(kXf + 1.3f, -72.6f, 28.4f)},
		0.5f, 6, true, false);
	Box(M, Mat::Chrome, V3(kXf + 1.5f, 0.f, 17.f), V3(0.45f, 1.6f, 9.5f));
	RoundedBox(M, Mat::Chrome, V3(kXf + 1.9f, 0.f, 17.f), V3(0.3f, 3.2f, 3.2f), 0.2f);
	for (float S : {-1.f, 1.f})
	{
		RoundedBox(M, Mat::Chrome, V3(kXf + 0.7f, S * 58.f, 18.f), V3(1.3f, 13.5f, 8.5f), 0.4f);
		RoundedBox(M, Mat::Headlamp, V3(kXf + 1.6f, S * 58.f, 18.f), V3(0.6f, 12.6f, 7.6f), 0.3f);
		// Рассеиватель — вертикальные рёбра.
		for (float Dy : {-8.f, -4.f, 0.f, 4.f, 8.f})
		{
			Box(M, Mat::Chrome, V3(kXf + 2.25f, S * 58.f + Dy, 18.f), V3(0.05f, 0.08f, 7.f));
		}
		RoundedBox(M, Mat::Indicator, V3(kXf + 0.9f, S * 58.f, 7.f), V3(0.6f, 12.5f, 1.8f), 0.2f);
		RoundedBox(M, Mat::Indicator, V3(168.f, S * (HalfW(168.f, 12.f) + 0.3f), 12.f), V3(3.5f, 0.5f, 1.2f), 0.2f);
	}
	Out.HeadlightL = V3(kXf + 3.f, -58.f, 18.f);
	Out.HeadlightR = V3(kXf + 3.f, 58.f, 18.f);

	// Задние фонари: красный, поворотник, задний ход.
	for (float S : {-1.f, 1.f})
	{
		// Широкий горизонтальный блок: стоп-габарит сверху, поворотник и задний ход снизу, хромовая окантовка.
		RoundedBox(M, Mat::Chrome, V3(kXr - 0.2f, S * 53.f, 13.5f), V3(0.9f, 18.f, 9.8f), 0.3f);
		RoundedBox(M, Mat::BlackTrim, V3(kXr - 0.6f, S * 53.f, 13.5f), V3(0.8f, 17.3f, 9.1f), 0.3f);
		RoundedBox(M, Mat::Taillamp, V3(kXr - 1.2f, S * 53.f, 17.6f), V3(0.5f, 16.8f, 4.6f), 0.2f);
		RoundedBox(M, Mat::Indicator, V3(kXr - 1.2f, S * 60.f, 8.3f), V3(0.5f, 9.8f, 3.4f), 0.2f);
		RoundedBox(M, Mat::Glass, V3(kXr - 1.2f, S * 43.5f, 8.3f), V3(0.5f, 6.6f, 3.4f), 0.2f);
	}

	// Бамперы: хромированная полоса с резиновой вставкой, загнутая на крылья.
	auto Bumper = [&](float Sign) {
		const float BaseX = Sign > 0.f ? kXf : kXr;
		std::vector<V3> Path;
		for (float Y : {-83.f, -81.f, -78.f, -74.f, -68.f, -60.f, -40.f, 0.f, 40.f, 60.f, 68.f, 74.f, 78.f, 81.f, 83.f})
		{
			const float A = std::fabs(Y);
			// Прямой бампер, концы коротко заломлены на крылья.
			const float Bend = 0.2f * (A / 62.f) * (A / 62.f) + (A > 70.f ? 0.06f * (A - 70.f) * (A - 70.f) : 0.f);
			Path.push_back(V3(BaseX + Sign * (6.f - Bend), Y, -15.f));
		}
		const V3 Ref(0.f, 0.f, -15.f);
		Sweep(M, Mat::Chrome, Path, RoundRectProfile(5.f, 2.8f, 0.5f, 2), Ez, false, true, &Ref);
		// «Клыки» — чёрные резиновые накладки.
		for (float Y : {-30.f, 30.f})
		{
			RoundedBox(M, Mat::Rubber, V3(BaseX + Sign * 8.6f, Y, -14.f), V3(2.2f, 3.f, 6.5f), 0.6f);
		}
		std::vector<ProfilePoint> Strip = RoundRectProfile(1.1f, 0.8f, 0.5f, 2);
		for (ProfilePoint& P : Strip)
		{
			P.B += 2.6f;
		}
		Sweep(M, Mat::BlackTrim, Path, Strip, Ez, false, true, &Ref);
		// Номер с рамкой.
		const float PlateX = Sign > 0.f ? kXf + 9.2f : kXr - 0.6f;
		const float PlateZ = Sign > 0.f ? -15.5f : 2.f;
		RoundedBox(M, Mat::BlackTrim, V3(PlateX - Sign * 0.2f, 0.f, PlateZ), V3(0.25f, 23.f, 6.2f), 0.5f);
		Box(M, Mat::Plate, V3(PlateX, 0.f, PlateZ), V3(0.3f, 22.f, 5.5f));
	};
	Bumper(1.f);
	Bumper(-1.f);

	// Лючок бензобака справа сзади.
	const V3 CapPos(-150.f, HalfW(-150.f, 18.f), 18.f);
	Lathe(M, Mat::Paint, CapPos, Ey, Ez, {{0.f, 0.5f}, {4.6f, 0.45f}, {5.f, 0.f}}, 20, 0.f, 1.f);
	Lathe(M, Mat::Chrome, CapPos, Ey, Ez, {{5.f, 0.05f}, {5.5f, 0.35f}, {5.9f, 0.f}}, 20);
	(void)Ex;

	// Молдинги по ребру боковины на кузове (на дверях — свои).
	for (float S : {-1.f, 1.f})
	{
		std::vector<V3> Front;
		for (float X : Range(kWheelXF + kArchR + 1.f, kXf - kRcF, 6.f))
		{
			Front.push_back(SidePoint(X, kCrease, S) + V3(0.f, S * 0.3f, 0.f));
		}
		Tube(M, Mat::Chrome, Front, 0.5f, 6);
		std::vector<V3> Rear;
		for (float X : Range(kXr + kRcR, kWheelXR - kArchR - 1.f, 6.f))
		{
			Rear.push_back(SidePoint(X, kCrease, S) + V3(0.f, S * 0.3f, 0.f));
		}
		Tube(M, Mat::Chrome, Rear, 0.5f, 6);
	}
}

void BuildHoodTrunk(Builder& B)
{
	auto Lid = [&](Part Id, float X0, float X1, float Crown, const V3& Hinge, const char* Tag) {
		MeshSet M;
		std::vector<float> Xs = SideColumns(X0, X1);
		const Grid G = SampleGrid(Xs, Range(-1.f, 1.f, 0.08f),
			[Crown](float X, float T) { return V3(X, T * (HalfW(X, ZTop(X)) - kGap), ZTop(X) + Crown * Wrap(T) + 0.2f); }, Constant(V3(0.f, 0.f, 1.f)),
			0.01f);
		EmitThick(M, G, 1.2f, Mat::Paint, Mat::Paint, Mat::Paint, Constant(V3(0.f, 0.f, -1.f)));
		// Усилители изнанки.
		const float Mid = 0.5f * (X0 + X1);
		Box(M, Mat::Paint, V3(Mid, 0.f, ZTop(Mid) - 2.2f), V3(0.5f * (X1 - X0) - 6.f, 2.f, 0.8f));
		for (float Y : {-38.f, 38.f})
		{
			Box(M, Mat::Paint, V3(Mid, Y, ZTop(Mid) - 2.f), V3(0.5f * (X1 - X0) - 8.f, 1.6f, 0.7f));
		}
		if (Id == Part::Hood)
		{
			// Шумоизоляция на изнанке, петли-«гусаки», замок, эмблема на передней кромке.
			Box(M, Mat::Rubber, V3(Mid + 4.f, 0.f, ZTop(Mid) - 1.3f), V3(0.5f * (X1 - X0) - 16.f, 30.f, 0.2f));
			for (float S : {-1.f, 1.f})
			{
				Tube(M, Mat::Metal, {V3(X0 + 9.f, S * 40.f, ZTop(X0) - 1.2f), V3(X0 + 5.f, S * 40.f, ZTop(X0) - 4.5f), V3(X0 + 0.5f, S * 40.f, ZTop(X0) - 3.f),
					Hinge + V3(0.f, S * 40.f, 0.f)}, 0.5f, 6);
			}
			RoundedBox(M, Mat::Metal, V3(X1 - 6.f, 0.f, ZTop(X1) - 2.2f), V3(1.5f, 2.f, 1.2f), 0.3f);
			RoundedBox(M, Mat::Chrome, V3(X1 - 4.f, 0.f, ZTop(X1 - 4.f) + Crown + 0.5f), V3(2.4f, 4.5f, 0.35f), 0.3f);
			RoundedBox(M, Mat::BlackTrim, V3(X1 - 4.f, 0.f, ZTop(X1 - 4.f) + Crown + 0.75f), V3(1.6f, 3.4f, 0.15f), 0.1f);
		}
		else
		{
			for (float S : {-1.f, 1.f})
			{
				Tube(M, Mat::Metal, {V3(X1 - 7.f, S * 42.f, ZTop(X1) - 1.2f), V3(X1 - 3.5f, S * 42.f, ZTop(X1) - 5.f), V3(X1 + 0.5f, S * 42.f, ZTop(X1) - 3.5f),
					Hinge + V3(0.f, S * 42.f, 0.f)}, 0.55f, 6);
			}
			RoundedBox(M, Mat::Metal, V3(X0 + 4.f, 0.f, ZTop(X0) - 2.f), V3(1.2f, 3.f, 1.5f), 0.3f);
		}
		B.Add(Id, M, Hinge);
		const float HalfX = 0.5f * (X1 - X0);
		B.Hit(Id, Tag, V3(Mid, 0.f, ZTop(Mid) + 1.f), V3(HalfX, 62.f, 3.f), Hinge);
	};
	Lid(Part::Hood, kHoodRear, kXf - 0.8f, 0.6f, kHoodHinge, "Hood");
	Lid(Part::Trunk, kXr + 0.8f, kTrunkFront, 0.5f, kTrunkHinge, "Trunk");
}

void BuildDoor(Builder& B, Part Id)
{
	const bool bFront = Id == Part::DoorFL || Id == Part::DoorFR;
	const float S = (Id == Part::DoorFL || Id == Part::DoorRL) ? -1.f : 1.f;
	const float X0 = bFront ? kFDoorRear : kRDoorRear;
	const float X1 = bFront ? kFDoorFront : kRDoorFront;
	const V3 Hinge = DoorHinge(Id);
	MeshSet M;

	// Обшивка снаружи — кусок боковины, изнутри — карта двери (8 см толщины).
	const Grid Skin = SidePanel(S, X0, X1, [](float X) { return std::max(kDoorBottom, Arches(X)); }, [](float X) { return ZTop(X); });
	EmitThick(M, Skin, 8.f, Mat::Paint, Mat::Interior, Mat::Paint, [S](const V3&) { return V3(0.f, -S, 0.f); });

	// Стекло в рамке.
	const float GX0 = bFront ? kFDoorRear + 0.2f : kRDoorFront - 0.4f;
	auto GX1 = [bFront](float Z) { return bFront ? AFrontEdge(Z) - 0.8f : CRearEdge(Z) + 0.8f; };
	const float GZ0 = kBelt + 0.6f;
	const float GZ1 = kRail - 0.4f;
	EmitGrid(M, Mat::Glass,
		SampleGrid(Range(0.f, 1.f, 0.1f), Range(GZ0, GZ1, 4.f), [=](float U, float Z) { return GlassSide(Lerpf(GX0, GX1(Z), U), Z, S, 1.2f); },
			Constant(V3(0.f, S, 0.f)), 0.002f));
	Tube(M, Mat::Chrome,
		{GlassSide(GX0, GZ1 + 0.3f, S, 0.4f), GlassSide(GX1(GZ1) + (bFront ? 0.3f : -0.3f), GZ1 + 0.3f, S, 0.4f),
			GlassSide(GX1(GZ0) + (bFront ? 0.3f : -0.3f), GZ0, S, 0.4f)},
		0.55f, 6);
	Tube(M, Mat::Chrome, {GlassSide(GX0 + (bFront ? -0.3f : 0.3f), GZ0, S, 0.4f), GlassSide(GX0 + (bFront ? -0.3f : 0.3f), GZ1 + 0.3f, S, 0.4f)},
		0.55f, 6);
	// Молдинги: по низу стекла и по ребру.
	const float BeltX1 = bFront ? 86.f : -84.f;
	Tube(M, Mat::Chrome, {V3(X0 + 0.4f, S * 73.4f, 39.6f), V3(BeltX1, S * 73.4f, 39.6f)}, 0.7f, 8);
	std::vector<V3> Crease;
	const float CX0 = bFront ? X0 + 0.5f : std::max(X0, kWheelXR + kArchR) + 1.f;
	const float CX1 = bFront ? std::min(X1, kWheelXF - kArchR) - 0.5f : X1 - 0.5f;
	for (float X : Range(CX0, CX1, 8.f))
	{
		Crease.push_back(SidePoint(X, kCrease, S) + V3(0.f, S * 0.3f, 0.f));
	}
	Tube(M, Mat::Chrome, Crease, 0.5f, 6);

	// Наружная ручка.
	const float HandleX = bFront ? 2.f : -92.f;
	const V3 Outer(HandleX, S * (HalfW(HandleX, 27.f) + 0.9f), 27.f);
	RoundedBox(M, Mat::Chrome, Outer, V3(6.f, 0.9f, 1.3f), 0.8f);
	RoundedBox(M, Mat::BlackTrim, Outer - V3(0.f, S * 0.6f, 0.f), V3(6.6f, 0.5f, 1.9f), 0.5f);

	// Салонная сторона: подлокотник, ручка, стеклоподъёмник, кнопка замка.
	auto Card = [S](float X, float Z, float Off) { return V3(X, S * (HalfW(X, Z) - 8.f - Off), Z); };
	const float ArmX = bFront ? 35.f : -55.f;
	RoundedBox(M, Mat::Interior, Card(ArmX, 6.f, 2.5f), V3(14.f, 2.5f, 2.2f), 1.5f);
	RoundedBox(M, Mat::InteriorDark, Card(ArmX + 4.f, 20.f, 0.3f), V3(30.f, 0.3f, 9.f), 0.25f);
	const float InnerX = bFront ? 62.f : -30.f;
	const V3 Inner = Card(InnerX, 20.f, 0.8f);
	RoundedBox(M, Mat::Chrome, Inner, V3(4.f, 0.8f, 1.f), 0.6f);
	const float CrankX = bFront ? 22.f : -70.f;
	const V3 Crank = Card(CrankX, 18.f, 0.f);
	const V3 Inward(0.f, -S, 0.f);
	// Ручку стеклоподъёмника рисует и крутит игра (плавно, вместе со стеклом).
	{
		const int32_t Door = Id == Part::DoorFL ? 0 : Id == Part::DoorFR ? 1 : Id == Part::DoorRL ? 2 : 3;
		B.Out.WindowCrank[Door] = Crank;
		B.Out.WindowCrankAxis[Door] = Inward;
		B.Out.GlassTumble = kTumble;
	}
	// Кнопка замка — в полке двери у стекла, торчит на 3 см (не сквозь обшивку).
	const float KnobX = bFront ? -4.f : -74.f;
	// Изнутри, на полке двери у самого стекла: снаружи её видно через стекло.
	const V3 KnobBase = Card(KnobX, kBelt - 1.5f, 1.2f);
	Tube(M, Mat::Chrome, {KnobBase, KnobBase + V3(0.f, 0.f, 3.f)}, 0.5f, 8);
	Lathe(M, Mat::Chrome, KnobBase + V3(0.f, 0.f, 3.f), V3(0.f, 0.f, 1.f), V3(1.f, 0.f, 0.f), {{0.5f, 0.f}, {0.6f, 0.3f}, {0.f, 0.5f}}, 8);

	// Карман внизу, хромовый молдинг по карте, динамик (передние) или пепельница (задние), уплотнитель по
	// периметру проёма, красный катафот на торце.
	{
		const float PX0 = bFront ? 10.f : -94.f;
		const float PX1 = bFront ? 70.f : -40.f;
		const float PMid = 0.5f * (PX0 + PX1);
		RoundedBox(M, Mat::InteriorDark, Card(PMid, -11.f, 1.6f), V3(0.5f * (PX1 - PX0), 1.6f, 5.f), 0.6f);
		RoundedBox(M, Mat::BlackTrim, Card(PMid, -7.f, 2.9f), V3(0.5f * (PX1 - PX0) - 1.f, 0.3f, 0.6f), 0.2f);
		std::vector<V3> Trim;
		for (float X : Range(X0 + 4.f, X1 - 4.f, 10.f))
		{
			Trim.push_back(Card(X, 31.f, 0.25f));
		}
		Tube(M, Mat::Chrome, Trim, 0.3f, 6);
		// Вертикальная «стёжка» нижней части карты.
		for (float X : Range(X0 + 8.f, X1 - 8.f, 6.f))
		{
			if (X > PX0 - 1.f && X < PX1 + 1.f)
			{
				continue;
			}
			Box(M, Mat::InteriorDark, Card(X, -6.f, 0.05f), V3(0.18f, 0.12f, 12.f));
		}
		if (bFront)
		{
			const V3 Spk = Card(78.f, -4.f, 0.2f);
			Lathe(M, Mat::BlackTrim, Spk, V3(0.f, -S, 0.f), V3(1.f, 0.f, 0.f), {{0.f, 0.3f}, {5.f, 0.3f}, {5.6f, 0.f}}, 20, 0.f, 1.f);
			for (float Dz : {-3.f, -1.5f, 0.f, 1.5f, 3.f})
			{
				Box(M, Mat::InteriorDark, Spk + V3(0.f, -S * 0.32f, Dz), V3(std::sqrt(std::max(0.f, 25.f - Dz * Dz)), 0.05f, 0.25f));
			}
		}
		else
		{
			RoundedBox(M, Mat::Chrome, Card(-48.f, 24.f, 0.4f), V3(5.f, 0.6f, 2.f), 0.3f);
			Box(M, Mat::BlackTrim, Card(-48.f, 24.f, 0.95f), V3(4.f, 0.1f, 0.3f));
		}
		// Уплотнитель по торцу двери и катафот.
		const float EdgeX = bFront ? X0 + 0.6f : X0 + 0.6f;
		Box(M, Mat::Taillamp, V3(EdgeX - 0.6f, S * (HalfW(EdgeX, 0.f) - 4.f), -6.f), V3(0.12f, 2.2f, 1.3f));
		std::vector<V3> Seal;
		for (float Z : Range(kDoorBottom + 1.f, kBelt - 1.f, 4.f))
		{
			Seal.push_back(V3(X0 + 1.f, S * (HalfW(X0, Z) - 6.f), Z));
		}
		Tube(M, Mat::Rubber, Seal, 0.7f, 5);
		// Личинка замка (передние двери).
		if (bFront)
		{
			Lathe(M, Mat::Chrome, V3(HandleX - 9.f, S * (HalfW(HandleX - 9.f, 27.f) + 0.1f), 27.f), V3(0.f, S, 0.f), V3(1.f, 0.f, 0.f),
				{{0.f, 0.5f}, {0.9f, 0.4f}, {1.f, 0.f}}, 12, 0.f, 1.f);
		}
		// Внутренний уплотнитель стекла («бархотка») по верху карты.
		Tube(M, Mat::BlackTrim, {Card(X0 + 3.f, kBelt - 0.8f, -1.f), Card(X1 - 3.f, kBelt - 0.8f, -1.f)}, 0.5f, 5);
	}

	// Зеркало заднего вида на водительской двери.
	if (Id == Part::DoorFL)
	{
		Tube(M, Mat::Chrome, {V3(83.f, -73.5f, 40.6f), V3(80.f, -83.f, 44.f)}, 0.7f, 8);
		RoundedBox(M, Mat::BlackTrim, V3(78.8f, -86.5f, 45.f), V3(2.f, 6.5f, 4.5f), 2.f);
		Box(M, Mat::Chrome, V3(76.7f, -86.5f, 45.f), V3(0.1f, 5.6f, 3.6f)); // зеркальце — хром, стекло двери опускается отдельно
	}

	B.Add(Id, M, Hinge);
	const char* Tag = Id == Part::DoorFL ? "DoorFL" : Id == Part::DoorFR ? "DoorFR" : Id == Part::DoorRL ? "DoorRL" : "DoorRR";
	const float MidX = 0.5f * (X0 + X1);
	B.Hit(Id, Tag, V3(MidX, S * 75.5f, 12.f), V3(0.5f * (X1 - X0), 5.f, 27.f), Hinge);
	B.Hit(Id, Tag, Inner, V3(4.5f, 2.f, 2.f), Hinge);
	B.Hit(Id, Tag, Outer, V3(6.5f, 2.f, 2.f), Hinge);
	// Ручка стеклоподъёмника: колесо мыши опускает и поднимает стекло.
	const char* WinTag = Id == Part::DoorFL ? "WindowFL" : Id == Part::DoorFR ? "WindowFR" : Id == Part::DoorRL ? "WindowRL" : "WindowRR";
	B.Hit(Id, WinTag, Crank + Inward * 2.f + V3(-2.5f, 0.f, -2.f), V3(4.5f, 2.5f, 4.5f), Hinge);
}

// ---------------------------------------------------------------------------------------------
// Салон
// ---------------------------------------------------------------------------------------------
float Tunnel(float X, float Y) { return 12.5f * (1.f - Smooth(8.f, 16.f, std::fabs(Y))) * Smooth(-92.f, -62.f, X); }

void BuildInterior(MeshSet& M)
{
	const V3 Ex(1.f, 0.f, 0.f);
	const V3 Ey(0.f, 1.f, 0.f);
	const V3 Ez(0.f, 0.f, 1.f);

	// Пол с тоннелем и наклонным щитом под педалями.
	EmitGrid(M, Mat::Carpet,
		SampleGrid(Range(-100.f, 92.f, 6.f), Range(-64.f, 64.f, 2.f),
			[](float X, float Y) { return V3(X, Y, -26.f + Tunnel(X, Y) + (X > 72.f ? (X - 72.f) * 1.1f : 0.f)); }, Constant(Ez), 0.05f));
	for (float S : {-1.f, 1.f})
	{
		EmitGrid(M, Mat::Carpet,
			SampleGrid(Range(-100.f, 92.f, 12.f), Range(-26.f, kDoorBottom - kGap, 4.f), [S](float X, float Z) { return V3(X, S * 64.f, Z); },
				Constant(V3(0.f, -S, 0.f)), 0.05f));
	}
	EmitGrid(M, Mat::InteriorDark,
		SampleGrid(Range(-64.f, 64.f, 16.f), Range(-4.f, 22.f, 13.f), [](float Y, float Z) { return V3(92.f, Y, Z); }, Constant(V3(-1.f, 0.f, 0.f)),
			0.05f));
	for (float S : {-1.f, 1.f}) // боковины ниши ног: закрывают арку со стороны салона
	{
		EmitGrid(M, Mat::Interior,
			SampleGrid(Range(66.f, 94.f, 7.f), Range(-26.f, 19.f, 9.f), [S](float X, float Z) { return V3(X, S * 64.f, Z); },
				Constant(V3(0.f, -S, 0.f)), 0.05f));
	}

	// Торпедо: профиль (z, x) протянут поперёк машины; над приборами — козырёк и ниша.
	auto Dash = [&](float Y0, float Y1, const std::vector<ProfilePoint>& ZX) {
		std::vector<ProfilePoint> Local = ZX;
		for (ProfilePoint& P : Local)
		{
			P.A -= 32.f;
			P.B -= 77.f;
		}
		Sweep(M, Mat::InteriorDark, {V3(77.f, Y0, 32.f), V3(77.f, Y1, 32.f)}, Local, Ez, false, true);
	};
	const std::vector<ProfilePoint> Plain = {{38.6f, 86.3f, true}, {41.f, 84.f}, {45.5f, 76.f}, {45.f, 68.f}, {43.5f, 65.5f}, {40.5f, 65.5f},
		{38.5f, 68.f}, {37.f, 73.f}, {21.f, 73.f}, {19.f, 75.f}, {18.f, 90.f, true}, {38.f, 90.f, true}};
	const std::vector<ProfilePoint> Cluster = {{38.6f, 86.3f, true}, {41.f, 84.f}, {46.5f, 76.f}, {50.5f, 70.f}, {51.f, 66.5f}, {49.5f, 65.5f},
		{47.8f, 67.f}, {47.5f, 72.f, true}, {35.f, 72.f, true}, {34.5f, 68.f}, {31.f, 68.f}, {30.f, 73.f}, {21.f, 73.f}, {19.f, 75.f},
		{18.f, 90.f, true}, {38.f, 90.f, true}};
	Dash(-64.f, -56.f, Plain);
	Dash(-56.f, -14.f, Cluster);
	// Над бардачком торпедо имеет нишу (глубиной до щита), её стенки — торцы соседних кусков.
	std::vector<ProfilePoint> Hole;
	for (const ProfilePoint& P : Plain)
	{
		Hole.push_back(P);
		if (P.A == 37.f && P.B == 73.f)
		{
			Hole.push_back({36.6f, 88.f, true});
			Hole.push_back({21.4f, 88.f, true});
		}
	}
	Dash(-14.f, 15.5f, Plain);
	Dash(15.5f, 48.5f, Hole);
	Dash(48.5f, 64.f, Plain);
	// Дно ниши — ковролином (вещи не гремят).
	Box(M, Mat::Carpet, V3(80.5f, 32.f, 21.6f), V3(7.4f, 16.4f, 0.15f));
	Box(M, Mat::BlackTrim, V3(71.93f, -35.f, 41.3f), V3(0.03f, 20.5f, 6.1f));

	// Приборы: спидометр, тахометр, топливо, температура.
	auto Gauge = [&](float Y, float R, int32_t Ticks, float Sweep2) {
		const V3 C(kGaugeX, Y, kGaugeZ);
		const V3 Face(-1.f, 0.f, 0.f);
		Lathe(M, Mat::GaugeFace, C, Face, Ez, {{0.f, 0.f}, {R, 0.f}}, 24, 0.f, 1.f);
		Lathe(M, Mat::Chrome, C, Face, Ez, {{R, 0.f}, {R + 0.25f, 0.5f}, {R + 0.55f, 0.15f}, {R + 0.55f, -0.2f}}, 24);
		// Шкалы с цифрами — текстурой поверх (игра рисует циферблаты), здесь только чаша и ободок.
		(void)Ticks;
		(void)Sweep2;
	};
	Gauge(-42.5f, kBigGauge, 10, 125.f);
	Gauge(-27.5f, kBigGauge, 9, 125.f);
	Gauge(-51.6f, kSmallGauge, 3, 60.f);
	Gauge(-18.4f, kSmallGauge, 3, 60.f);

	// Центральная консоль: приёмник, отопитель, дефлекторы, замок зажигания.
	// Магнитолу (корпус, шкалу, ручки) рисует игра — она светится и крутится. Ниже — пульт отопителя:
	// три прорези с рычажками (заслонка, кран печки, вентилятор), под ним — монетница с парой монет.
	RoundedBox(M, Mat::InteriorDark, V3(72.9f, 2.f, 24.f), V3(0.4f, 9.f, 2.6f), 0.4f);
	for (int32_t K = 0; K < 3; ++K)
	{
		const float Z = 25.6f - 1.6f * static_cast<float>(K);
		Box(M, Mat::BlackTrim, V3(72.45f, 2.f, Z), V3(0.05f, 6.5f, 0.22f));
		RoundedBox(M, Mat::Chrome, V3(72.2f, -3.f + 3.5f * static_cast<float>(K), Z), V3(0.35f, 0.5f, 0.45f), 0.15f);
	}
	// Консоль под торпедо до тоннеля.
	RoundedBox(M, Mat::InteriorDark, V3(79.f, 2.f, 13.f), V3(6.f, 10.f, 7.f), 1.2f);
	// Монетница: ниша в лицевой панели консоли.
	Box(M, Mat::BlackTrim, V3(73.6f, 2.f, 15.8f), V3(1.6f, 6.f, 1.4f));
	RoundedBox(M, Mat::Chrome, V3(72.95f, 2.f, 17.3f), V3(0.08f, 6.4f, 0.12f), 0.05f);
	for (float Y : {-1.5f, 1.8f, 4.6f})
	{
		Lathe(M, Mat::Chrome, V3(73.4f + 0.3f * Y * 0.1f, Y, 14.5f), Ez, Ex, {{1.1f, 0.f}, {1.1f, 0.25f}, {0.f, 0.25f}}, 12);
	}
	for (float Y : {-4.f, 8.f})
	{
		RoundedBox(M, Mat::BlackTrim, V3(72.7f, Y, 34.f), V3(0.4f, 4.5f, 1.5f), 0.4f);
		for (float Z : {33.3f, 34.f, 34.7f})
		{
			Box(M, Mat::Chrome, V3(72.25f, Y, Z), V3(0.08f, 4.1f, 0.12f));
		}
	}
	for (float Y : {-61.f, 60.f})
	{
		Lathe(M, Mat::BlackTrim, V3(72.8f, Y, 35.f), V3(-1.f, 0.f, 0.f), Ez, {{2.2f, 0.f}, {2.4f, 0.5f}, {1.6f, 0.4f, true}, {1.6f, -0.6f}, {0.f, -0.6f}},
			16);
	}
	Lathe(M, Mat::Chrome, V3(73.f, kKeyPos.Y, kKeyPos.Z), V3(-1.f, 0.f, 0.f), Ez, {{1.2f, 0.3f}, {1.8f, 0.2f}, {1.9f, 0.f}}, 16, 0.f, 1.f);

	// Передние сиденья.
	for (float S : {-1.f, 1.f})
	{
		const float Y = S * 35.f;
		// Передние сиденья — на 12 см вперёд: между спинкой и задним диваном остаётся место для колен.
		const float Fx = 6.f;
		RoundedBox(M, Mat::Fabric, V3(Fx - 6.f, Y, -11.5f), V3(25.f, 24.f, 5.5f), 4.5f);
		for (float B2 : {-20.5f, 20.5f})
		{
			RoundedBox(M, Mat::Fabric, V3(Fx - 7.f, Y + B2, -5.6f), V3(22.f, 3.5f, 2.2f), 2.f);
		}
		const float Tilt = Rad(12.f);
		const V3 BackZ(-std::sin(Tilt), 0.f, std::cos(Tilt));
		const V3 BackX(std::cos(Tilt), 0.f, std::sin(Tilt));
		const V3 BackC(Fx - 35.f, Y, 18.f);
		RoundedBox(M, Mat::Fabric, BackC, V3(5.f, 23.5f, 27.f), 4.5f, BackX, Ey, BackZ);
		for (float B2 : {-19.5f, 19.5f})
		{
			RoundedBox(M, Mat::Fabric, BackC + BackX * 3.5f + V3(0.f, B2, 0.f), V3(3.f, 3.8f, 23.f), 2.5f, BackX, Ey, BackZ);
		}
		const V3 Head = BackC + BackZ * 36.f - BackX * 0.5f;
		RoundedBox(M, Mat::Fabric, Head, V3(3.5f, 12.f, 6.5f), 3.2f, BackX, Ey, BackZ);
		for (float B2 : {-6.f, 6.f})
		{
			Tube(M, Mat::Chrome, {BackC + BackZ * 26.f + V3(0.f, B2, 0.f), Head - BackZ * 5.f + V3(0.f, B2, 0.f)}, 0.6f, 6);
		}
		for (float B2 : {-18.f, 18.f})
		{
			// Салазки: от пола до подушки, по длине — с запасом на регулировку.
			RoundedBox(M, Mat::Metal, V3(Fx - 6.f, Y + B2, -22.5f), V3(26.f, 1.5f, 3.5f), 0.5f);
			RoundedBox(M, Mat::BlackTrim, V3(Fx - 6.f, Y + B2, -18.f), V3(22.f, 1.2f, 1.5f), 0.4f);
		}
		RoundedBox(M, Mat::BlackTrim, V3(Fx - 27.f, Y + S * 24.5f, -9.f), V3(4.f, 1.f, 4.f), 1.f);
		// Ремень не пристёгнут: висит вдоль средней стойки — от петли наверху до катушки на пороге (всё на стойке,
		// не в проёме двери). Замок — на коротком стебле у тоннеля.
		const float Pillar = -15.f;
		const float PillarIn = S * (HalfW(Pillar, 62.f) - 4.5f);
		const float SillIn = S * 61.5f;
		Tube(M, Mat::BlackTrim, {V3(Pillar - 0.8f, PillarIn, 62.f), V3(Pillar - 1.f, S * 64.f, 30.f), V3(Pillar - 1.f, SillIn, -9.f)}, 0.45f, 5);
		RoundedBox(M, Mat::Chrome, V3(Pillar - 0.5f, PillarIn + S * 0.2f, 62.f), V3(1.6f, 0.5f, 2.2f), 0.4f);
		RoundedBox(M, Mat::BlackTrim, V3(Pillar, SillIn + S * 0.5f, -14.f), V3(3.f, 2.f, 5.f), 0.8f);
		const float In = S * 12.f;
		const float Bx = Fx - 20.f;
		Tube(M, Mat::Metal, {V3(Bx, In, -18.f), V3(Bx + 1.f, In, -6.f)}, 0.5f, 6);
		RoundedBox(M, Mat::BlackTrim, V3(Bx + 1.f, In, -4.f), V3(2.f, 1.2f, 3.f), 0.6f);
	}

	// Заднее сиденье и полка.
	// Задний диван — от борта до борта (внутренние боковины на ±62), подушка лежит на полу без щелей.
	RoundedBox(M, Mat::Fabric, V3(-76.f, 0.f, -14.f), V3(22.f, 60.f, 7.f), 5.f);
	RoundedBox(M, Mat::InteriorDark, V3(-76.f, 0.f, -23.f), V3(20.f, 58.f, 3.f), 1.f);
	{
		const float Tilt = Rad(15.f);
		RoundedBox(M, Mat::Fabric, V3(-98.f, 0.f, 13.f), V3(5.5f, 60.f, 25.f), 5.f, V3(std::cos(Tilt), 0.f, std::sin(Tilt)), Ey,
			V3(-std::sin(Tilt), 0.f, std::cos(Tilt)));
	}
	RoundedBox(M, Mat::InteriorDark, V3(-106.f, 0.f, 38.f), V3(3.f, 62.f, 0.6f), 0.5f);
	EmitGrid(M, Mat::Carpet,
		SampleGrid(Range(-60.f, 60.f, 20.f), Range(-26.f, 38.f, 16.f), [](float Y, float Z) { return V3(-107.f, Y, Z); }, Constant(V3(1.f, 0.f, 0.f)),
			0.05f));

	// Потолок: плафон, козырьки, зеркало.
	RoundedBox(M, Mat::Dome, V3(-10.f, 0.f, 86.6f), V3(5.f, 3.f, 0.5f), 0.4f);
	for (float S : {-1.f, 1.f})
	{
		RoundedBox(M, Mat::Interior, V3(49.5f, S * 30.f, 82.6f), V3(6.5f, 18.f, 0.7f), 0.6f, V3(std::cos(Rad(20.f)), 0.f, std::sin(Rad(20.f))), Ey,
			V3(-std::sin(Rad(20.f)), 0.f, std::cos(Rad(20.f))));
	}
	Tube(M, Mat::BlackTrim, {V3(57.f, 0.f, 87.5f), V3(52.5f, 0.f, 81.5f)}, 0.6f, 6);
	RoundedBox(M, Mat::BlackTrim, V3(52.f, 0.f, 79.5f), V3(1.2f, 11.f, 3.f), 1.2f);
	Box(M, Mat::Glass, V3(50.75f, 0.f, 79.5f), V3(0.08f, 10.2f, 2.4f));

	// Чехол рычага КПП на тоннеле.
	Lathe(M, Mat::Chrome, kGearPos, Ez, Ex, {{6.f, 0.f}, {6.6f, 0.2f}, {6.f, 0.5f}}, 16, 0.f, 1.f);
}

void BuildEngineBay(MeshSet& M)
{
	const V3 Ex(1.f, 0.f, 0.f);
	const V3 Ey(0.f, 1.f, 0.f);
	const V3 Ez(0.f, 0.f, 1.f);
	EmitGrid(M, Mat::Metal,
		SampleGrid(Range(94.f, 196.f, 17.f), Range(-44.f, 44.f, 22.f), [](float X, float Y) { return V3(X, Y, -24.f); }, Constant(Ez), 0.05f));
	for (float S : {-1.f, 1.f})
	{
		EmitGrid(M, Mat::Paint,
			SampleGrid(Range(94.f, 198.f, 13.f), Range(-24.f, 34.f, 14.5f), [S](float X, float Z) { return V3(X, S * 44.f, Z); },
				Constant(V3(0.f, -S, 0.f)), 0.05f));
		EmitGrid(M, Mat::Paint,
			SampleGrid(Range(94.f, 198.f, 13.f), Range(44.f, 72.5f, 7.f), [S](float X, float Y) { return V3(X, S * Y, 34.f); }, Constant(Ez), 0.05f));
		Lathe(M, Mat::Paint, V3(kWheelXF, S * 60.f, 34.f), Ez, Ex, {{9.f, 0.f}, {8.f, 4.f}, {5.f, 5.f}, {0.f, 5.f}}, 18);
		Lathe(M, Mat::Metal, V3(kWheelXF, S * 60.f, 39.f), Ez, Ex, {{1.2f, 0.f}, {1.2f, 1.f}, {0.f, 1.f}}, 8);
	}
	EmitGrid(M, Mat::Paint,
		SampleGrid(Range(-44.f, 44.f, 11.f), Range(-24.f, 38.f, 15.5f), [](float Y, float Z) { return V3(94.f, Y, Z); }, Constant(Ex), 0.05f));

	// Мотор: блок, головка, клапанная крышка, воздушный фильтр, коллектор, генератор.
	RoundedBox(M, Mat::Engine, V3(140.f, 0.f, -4.f), V3(25.f, 17.f, 16.f), 3.f);
	RoundedBox(M, Mat::Engine, V3(140.f, 0.f, 14.f), V3(24.f, 15.f, 4.f), 2.f);
	RoundedBox(M, Mat::BlackTrim, V3(140.f, 0.f, 20.f), V3(23.f, 11.f, 3.2f), 2.5f);
	for (float X : {125.f, 133.f, 141.f, 149.f})
	{
		RoundedBox(M, Mat::Chrome, V3(X, 0.f, 23.3f), V3(0.6f, 8.f, 0.3f), 0.2f);
	}
	// Воздушный фильтр, карбюратор, генератор, радиатор, патрубки и аккумулятор — съёмные узлы: их рисует игра
	// (PeregonPartMeshes) по тому, что стоит в машине. Здесь — только «железо», которое не снимают.
	Tube(M, Mat::Metal, {V3(158.f, 19.f, 2.f), V3(125.f, 19.f, 2.f), V3(112.f, 22.f, -12.f), V3(104.f, 22.f, -30.f)}, 2.2f, 8);
	// Вентилятор, радиатор, патрубки.
	for (int32_t K = 0; K < 4; ++K)
	{
		const float T = Rad(45.f + 90.f * static_cast<float>(K));
		const V3 Dir(0.f, std::cos(T), std::sin(T));
		const V3 Tan(0.f, -std::sin(T), std::cos(T));
		RoundedBox(M, Mat::BlackTrim, V3(178.f, 0.f, 2.f) + Dir * 9.f, V3(0.4f, 2.8f, 8.f), 0.3f, Ex, Tan, Dir, 1);
	}
	Lathe(M, Mat::Metal, V3(178.f, 0.f, 2.f), Ex, Ez, {{0.f, -2.f}, {3.f, -2.f}, {3.f, 2.f}, {0.f, 2.f}}, 12);
	// Полка аккумулятора (сам аккумулятор — съёмный узел), бачок омывателя, главный тормозной цилиндр.
	RoundedBox(M, Mat::Metal, V3(172.f, 31.f, -7.f), V3(11.f, 12.f, 0.8f), 0.3f);
	// Рамка радиатора — сам радиатор съёмный.
	RoundedBox(M, Mat::Metal, V3(192.f, 0.f, -19.f), V3(1.2f, 44.f, 1.5f), 0.4f);
	RoundedBox(M, Mat::Metal, V3(192.f, 0.f, 28.f), V3(1.2f, 44.f, 1.5f), 0.4f);
	Lathe(M, Mat::GaugeFace, V3(176.f, -32.f, -4.f), Ez, Ex,
		{{0.f, 0.f}, {5.5f, 0.f}, {6.f, 1.f}, {6.f, 16.f}, {5.f, 18.f}, {1.5f, 18.5f, true}, {1.5f, 21.f, true}, {0.f, 21.f}}, 18);
	Lathe(M, Mat::Metal, V3(100.f, -36.f, 24.f), Ex, Ez, {{0.f, 0.f}, {3.5f, 0.f, true}, {3.5f, 10.f, true}, {0.f, 10.f}}, 14);
	RoundedBox(M, Mat::GaugeFace, V3(105.f, -36.f, 29.5f), V3(3.f, 3.f, 2.f), 0.8f);
	(void)Ey;
}

void BuildTrunkInside(MeshSet& M)
{
	const V3 Ez(0.f, 0.f, 1.f);
	// Багажник — от борта до борта: пол, боковины по внутренней стороне кузова (с нишами над арками), перегородка за диваном.
	const float TW = 70.f;
	EmitGrid(M, Mat::Carpet,
		SampleGrid(Range(-200.f, -108.f, 8.f), Range(-TW, TW, 10.f), [](float X, float Y) { return V3(X, Y, -20.f); }, Constant(Ez), 0.05f));
	for (float S : {-1.f, 1.f})
	{
		EmitGrid(M, Mat::Carpet,
			SampleGrid(Range(-200.f, -108.f, 8.f), Range(-20.f, 38.f, 8.f),
				[S](float X, float Z) { return V3(X, S * std::min(70.f, HalfW(X, Z) - 4.f), Z); }, Constant(V3(0.f, -S, 0.f)), 0.05f));
		// Арка колеса в багажнике — короб.
		RoundedBox(M, Mat::Carpet, V3(kWheelXR, S * 60.f, -6.f), V3(26.f, 10.f, 14.f), 4.f);
	}
	EmitGrid(M, Mat::Carpet,
		SampleGrid(Range(-TW, TW, 10.f), Range(-20.f, 38.f, 8.f), [](float Y, float Z) { return V3(-108.f, Y, Z); }, Constant(V3(-1.f, 0.f, 0.f)),
			0.05f));
	EmitGrid(M, Mat::Carpet,
		SampleGrid(Range(-TW, TW, 10.f), Range(-20.f, 30.f, 10.f), [](float Y, float Z) { return V3(-200.f, Y, Z); }, Constant(V3(1.f, 0.f, 0.f)),
			0.05f));
	BuildWheel(M, V3(-165.f, -20.f, -10.f), Ez, V3(1.f, 0.f, 0.f));
	RoundedBox(M, Mat::Metal, V3(-150.f, 32.f, -8.f), V3(14.f, 18.f, 11.f), 3.f);
	Lathe(M, Mat::BlackTrim, V3(-150.f, 32.f, 3.f), Ez, V3(1.f, 0.f, 0.f), {{0.f, 2.f}, {2.5f, 2.f, true}, {2.5f, 0.f}}, 12);
}

// Подвижные мелочи салона — в своих системах координат.
void BuildControls(Builder& B)
{
	const V3 Ex(1.f, 0.f, 0.f);
	const V3 Ey(0.f, 1.f, 0.f);
	const V3 Ez(0.f, 0.f, 1.f);

	// Руль: X узла — по колонке к водителю, обод в плоскости YZ.
	{
		MeshSet M;
		std::vector<V3> Rim;
		for (int32_t K = 0; K < 32; ++K)
		{
			const float T = 2.f * Pi * static_cast<float>(K) / 32.f;
			Rim.push_back(V3(0.f, 18.f * std::cos(T), 18.f * std::sin(T)));
		}
		Tube(M, Mat::InteriorDark, Rim, 1.5f, 10, true, false);
		for (float S : {-1.f, 1.f})
		{
			Tube(M, Mat::InteriorDark, {V3(-2.6f, S * 3.5f, -1.f), V3(-1.2f, S * 10.f, -2.5f), V3(0.f, S * 16.8f, -3.5f)}, 1.1f, 8);
		}
		Tube(M, Mat::InteriorDark, {V3(-2.6f, 0.f, -3.5f), V3(-1.f, 0.f, -10.f), V3(0.f, 0.f, -16.8f)}, 0.9f, 8);
		Lathe(M, Mat::BlackTrim, V3(-3.f, 0.f, 0.f), Ex, Ez, {{0.f, 1.6f}, {3.6f, 1.5f}, {4.6f, 0.6f}, {4.6f, -1.2f}, {3.f, -2.f}}, 20);
		Lathe(M, Mat::Chrome, V3(-3.f, 0.f, 0.f), Ex, Ez, {{0.f, 1.7f}, {1.6f, 1.62f}}, 14, 0.f, 1.f);
		Lathe(M, Mat::InteriorDark, V3(-3.f, 0.f, 0.f), V3(-1.f, 0.f, 0.f), Ez, {{3.1f, 1.8f}, {3.4f, 5.f}, {3.4f, 22.f}, {0.f, 22.f}}, 16);
		B.Add(Part::Steering, M, kSteeringCenter, false);
	}
	// Стрелки: X — от водителя к прибору, стрелка — в +Z.
	auto NeedleMesh = [&](Part Id, float Y, float R) {
		MeshSet M;
		Box(M, Mat::Needle, V3(-0.15f, 0.f, R * 0.35f), V3(0.05f, 0.16f + R * 0.02f, R * 0.5f));
		Lathe(M, Mat::BlackTrim, V3(-0.2f, 0.f, 0.f), V3(-1.f, 0.f, 0.f), Ez, {{0.f, 0.35f}, {0.5f + R * 0.04f, 0.3f}, {0.6f + R * 0.04f, 0.f}}, 12,
			0.f, 1.f);
		B.Add(Id, M, V3(kGaugeX - 0.3f, Y, kGaugeZ), false);
	};
	NeedleMesh(Part::SpeedoNeedle, -42.5f, kBigGauge);
	NeedleMesh(Part::TachoNeedle, -27.5f, kBigGauge);
	NeedleMesh(Part::FuelNeedle, -51.6f, kSmallGauge);
	NeedleMesh(Part::TempNeedle, -18.4f, kSmallGauge);

	// Ключ.
	{
		MeshSet M;
		Box(M, Mat::Metal, V3(-1.2f, 0.f, 0.f), V3(1.2f, 0.12f, 0.35f));
		RoundedBox(M, Mat::BlackTrim, V3(-3.4f, 0.f, 0.f), V3(1.f, 0.25f, 1.4f), 0.4f);
		Tube(M, Mat::Chrome, {V3(-4.6f, 0.f, -1.6f), V3(-5.6f, 0.f, -3.2f)}, 0.25f, 6);
		B.Add(Part::Key, M, kKeyPos, false);
		B.Out.HitBoxes.push_back({Part::Key, "Key", V3(-2.2f, 0.f, 0.f), V3(2.6f, 1.2f, 1.8f)});
	}
	// Переключатель света.
	{
		MeshSet M;
		Lathe(M, Mat::InteriorDark, V3(0.f, 0.f, 0.f), V3(-1.f, 0.f, 0.f), Ez,
			{{0.5f, 0.f}, {0.5f, 1.f, true}, {1.5f, 1.2f}, {1.6f, 2.4f}, {1.2f, 2.8f}, {0.f, 2.8f}}, 14);
		B.Add(Part::LightKnob, M, kKnobPos, false);
		B.Out.HitBoxes.push_back({Part::LightKnob, "Lights", V3(-1.5f, 0.f, 0.f), V3(1.8f, 2.f, 2.f)});
	}
	// Бардачок: петля внизу.
	{
		MeshSet M;
		RoundedBox(M, Mat::Interior, V3(-0.6f, 0.f, 7.6f), V3(0.8f, 17.f, 7.4f), 1.f);
		Lathe(M, Mat::Chrome, V3(-1.4f, 0.f, 12.6f), V3(-1.f, 0.f, 0.f), Ez, {{0.f, 0.6f}, {0.9f, 0.5f}, {0.9f, 0.f}}, 12, 0.f, 1.f);
		RoundedBox(M, Mat::Interior, V3(4.f, 0.f, 13.f), V3(4.f, 16.f, 0.5f), 0.3f);
		B.Add(Part::Glovebox, M, kGloveHinge, false);
		B.Out.HitBoxes.push_back({Part::Glovebox, "Glovebox", V3(-0.6f, 0.f, 7.6f), V3(1.5f, 17.5f, 8.f)});
	}
	// Рычаг КПП: чехол, рычаг, шар.
	{
		MeshSet M;
		Lathe(M, Mat::Rubber, V3(0.f, 0.f, 0.f), Ez, Ex, {{5.6f, 0.f}, {4.2f, 2.5f}, {2.4f, 5.5f}, {1.1f, 8.f}}, 16);
		Tube(M, Mat::Chrome, {V3(0.f, 0.f, 5.f), V3(-1.f, 0.f, 16.f), V3(-2.5f, 0.f, 25.f)}, 0.7f, 8);
		Lathe(M, Mat::InteriorDark, V3(-2.5f, 0.f, 26.5f), Ez, Ex,
			{{0.f, -2.4f}, {1.2f, -2.08f}, {2.08f, -1.2f}, {2.4f, 0.f}, {2.08f, 1.2f}, {1.2f, 2.08f}, {0.f, 2.4f}}, 14);
		B.Add(Part::GearLever, M, kGearPos, false);
	}
	// Ручник.
	{
		MeshSet M;
		RoundedBox(M, Mat::Metal, V3(-8.f, 0.f, 1.f), V3(8.f, 1.3f, 1.3f), 1.f);
		RoundedBox(M, Mat::InteriorDark, V3(-19.f, 0.f, 1.6f), V3(6.f, 1.9f, 1.9f), 1.6f);
		Lathe(M, Mat::Chrome, V3(-25.f, 0.f, 1.8f), V3(-1.f, 0.f, 0.f), Ez, {{0.f, 0.6f}, {0.7f, 0.5f}, {0.7f, 0.f}}, 10, 0.f, 1.f);
		RoundedBox(M, Mat::Rubber, V3(0.f, 0.f, -0.3f), V3(4.f, 3.f, 0.6f), 0.5f);
		B.Add(Part::Handbrake, M, kHandbrakePos, false);
		B.Out.HitBoxes.push_back({Part::Handbrake, "Handbrake", V3(-14.f, 0.f, 1.5f), V3(14.f, 2.5f, 2.5f)});
	}
	// Педали (ось качания вверху).
	auto Pedal = [&](Part Id, float Y, bool bGas) {
		MeshSet M;
		Tube(M, Mat::Metal, {V3(0.f, 0.f, 0.f), V3(-0.6f, 0.f, -8.f), V3(-1.6f, 0.f, -14.f)}, 0.6f, 6);
		if (bGas)
		{
			RoundedBox(M, Mat::Rubber, V3(-2.f, 0.f, -15.f), V3(0.6f, 2.6f, 5.f), 0.8f);
		}
		else
		{
			RoundedBox(M, Mat::Rubber, V3(-2.2f, 0.f, -16.f), V3(0.6f, 3.6f, 3.2f), 0.8f);
		}
		B.Add(Id, M, V3(kPedalX, Y, kPedalZ), false);
	};
	Pedal(Part::ClutchPedal, -47.f, false);
	Pedal(Part::BrakePedal, -35.5f, false);
	Pedal(Part::GasPedal, -25.f, true);

	// Колёса.
	for (Part Id : {Part::WheelLeft, Part::WheelRight})
	{
		MeshSet M;
		BuildWheel(M, V3(), V3(0.f, Id == Part::WheelLeft ? -1.f : 1.f, 0.f), V3(1.f, 0.f, 0.f));
		B.Add(Id, M, V3(), false);
	}
	(void)Ey;
}

// Спираль пружины.
std::vector<V3> Helix(const V3& Base, float Radius, float Height, float Turns)
{
	std::vector<V3> P;
	const int32_t N = static_cast<int32_t>(Turns * 14.f);
	for (int32_t K = 0; K <= N; ++K)
	{
		const float T = static_cast<float>(K) / static_cast<float>(N);
		const float A = 2.f * Pi * Turns * T;
		P.push_back(Base + V3(Radius * std::cos(A), Radius * std::sin(A), Height * T));
	}
	return P;
}

// Мелочи, из которых складывается «настоящая» машина: снаружи, в салоне, под капотом, в багажнике и снизу.
void BuildDetails(MeshSet& M)
{
	const V3 Ex(1.f, 0.f, 0.f);
	const V3 Ey(0.f, 1.f, 0.f);
	const V3 Ez(0.f, 0.f, 1.f);

	// ---- Снаружи ----
	for (float S : {-1.f, 1.f})
	{
		// Резиновый молдинг порога и хромовые окантовки арок.
		std::vector<V3> Sill;
		for (float X : Range(kWheelXR + kArchR + 1.5f, kWheelXF - kArchR - 1.5f, 10.f))
		{
			Sill.push_back(SidePoint(X, -26.f, S) + V3(0.f, S * 0.2f, 0.f));
		}
		Tube(M, Mat::BlackTrim, Sill, 0.8f, 6);
		for (float Center : {kWheelXF, kWheelXR})
		{
			std::vector<V3> Lip;
			for (int32_t K = 0; K <= 28; ++K)
			{
				const float Phi = Pi * static_cast<float>(K) / 28.f;
				const float X = Center + (kArchR + 0.2f) * std::cos(Phi);
				const float Z = std::max(-27.f, kArchZ + (kArchR + 0.2f) * std::sin(Phi));
				Lip.push_back(SidePoint(X, Z, S) + V3(0.f, S * 0.15f, 0.f));
			}
			Tube(M, Mat::Chrome, Lip, 0.4f, 6);
		}
		// Вентиляционная решётка на задней стойке.
		for (int32_t K = 0; K < 5; ++K)
		{
			const float Z = 50.f + 2.f * static_cast<float>(K);
			const float X = -96.f + 0.35f * static_cast<float>(K);
			RoundedBox(M, Mat::BlackTrim, GlassSide(X, Z, S, -0.5f), V3(4.6f, 0.4f, 0.55f), 0.2f);
		}
		Tube(M, Mat::Chrome, {GlassSide(-101.5f, 48.5f, S, -0.6f), GlassSide(-90.f, 48.5f, S, -0.6f), GlassSide(-89.3f, 59.5f, S, -0.6f),
			GlassSide(-100.8f, 59.5f, S, -0.6f)}, 0.3f, 6, true, false);
		// Форсунки омывателя на жабо.
		RoundedBox(M, Mat::BlackTrim, V3(90.5f, S * 14.f, 41.9f), V3(1.f, 0.6f, 0.5f), 0.3f);
		// Буксирные проушины.
		Tube(M, Mat::Metal, {V3(kXf - 4.f, S * 42.f, -28.f), V3(kXf + 3.f, S * 42.f, -28.f), V3(kXf + 3.f, S * 42.f, -24.f)}, 0.7f, 6);
		// Кронштейны бамперов.
		for (float Sign : {-1.f, 1.f})
		{
			const float BaseX = Sign > 0.f ? kXf : kXr;
			RoundedBox(M, Mat::Metal, V3(BaseX + Sign * 2.f, S * 48.f, -15.f), V3(2.5f, 1.5f, 2.f), 0.3f);
		}
	}
	// Воздухозаборные щели под решёткой и надпись-шильдик на задней панели, замок багажника, фонари номера.
	Box(M, Mat::BlackTrim, V3(kXf + 0.15f, 0.f, 1.2f), V3(0.25f, 34.f, 1.2f));
	Box(M, Mat::BlackTrim, V3(kXf + 0.15f, 0.f, -2.8f), V3(0.25f, 30.f, 0.8f));
	Box(M, Mat::Chrome, V3(kXr - 0.4f, 0.f, 25.f), V3(0.3f, 34.f, 0.5f));
	Lathe(M, Mat::Chrome, V3(kXr - 0.5f, 0.f, 21.5f), V3(-1.f, 0.f, 0.f), Ez, {{0.f, 0.6f}, {1.3f, 0.5f}, {1.5f, 0.f}}, 14, 0.f, 1.f);
	for (int32_t K = 0; K < 5; ++K)
	{
		// Шильдик: пять коротких хромовых «букв» справа над фонарём.
		RoundedBox(M, Mat::Chrome, V3(kXr - 0.35f, 37.f + 2.2f * static_cast<float>(K), 27.6f), V3(0.2f, 0.8f, 1.f), 0.2f);
	}
	for (float Y : {-15.f, 15.f})
	{
		RoundedBox(M, Mat::Chrome, V3(kXr - 0.8f, Y, 9.f), V3(0.8f, 3.f, 1.2f), 0.4f);
		Box(M, Mat::Headlamp, V3(kXr - 0.9f, Y, 8.f), V3(0.8f, 2.4f, 0.3f));
	}
	// Наконечник глушителя.
	Lathe(M, Mat::Chrome, V3(-213.f, 32.f, -32.5f), V3(-1.f, 0.f, 0.f), Ez, {{2.6f, -2.f}, {2.9f, -2.f}, {2.9f, 1.5f}, {2.5f, 1.5f}}, 14);

	// ---- Снизу: подвеска, мосты, вал ----
	for (float S : {-1.f, 1.f})
	{
		// Передняя: пружина, амортизатор, нижний рычаг, поворотный кулак.
		Tube(M, Mat::BlackTrim, Helix(V3(kWheelXF, S * 52.f, -18.f), 5.f, 26.f, 5.f), 0.75f, 6);
		Tube(M, Mat::Metal, {V3(kWheelXF, S * 52.f, -22.f), V3(kWheelXF, S * 52.f, 12.f)}, 1.6f, 8);
		Tube(M, Mat::Metal, {V3(kWheelXF - 14.f, S * 40.f, -30.f), V3(kWheelXF, S * 60.f, -27.f), V3(kWheelXF + 14.f, S * 40.f, -30.f)}, 1.3f, 6);
		Tube(M, Mat::Metal, {V3(kWheelXF, S * 60.f, -27.f), V3(kWheelXF, S * 61.f, -15.f)}, 1.4f, 6);
		Lathe(M, Mat::Metal, V3(kWheelXF, S * 61.f, -24.f), V3(0.f, S, 0.f), Ex, {{0.f, 0.f}, {12.f, 0.f}, {12.f, 2.f}, {0.f, 2.f}}, 20);
		// Задняя: мост, продольные тяги, амортизатор, тормозной барабан.
		Tube(M, Mat::Metal, {V3(kWheelXR + 48.f, S * 46.f, -29.f), V3(kWheelXR + 2.f, S * 50.f, -27.f)}, 1.4f, 6);
		Tube(M, Mat::Metal, {V3(kWheelXR - 6.f, S * 54.f, -26.f), V3(kWheelXR - 8.f, S * 54.f, -12.f)}, 1.4f, 8);
		Tube(M, Mat::BlackTrim, Helix(V3(kWheelXR + 4.f, S * 50.f, -22.f), 5.f, 18.f, 4.f), 0.75f, 6);
		Lathe(M, Mat::Metal, V3(kWheelXR, S * 59.f, -24.f), V3(0.f, S, 0.f), Ex, {{0.f, 0.f}, {11.5f, 0.f}, {12.f, 3.5f}, {0.f, 3.5f}}, 20);
	}
	Tube(M, Mat::Metal, {V3(kWheelXR, -60.f, -24.f), V3(kWheelXR, 60.f, -24.f)}, 3.6f, 10);
	Lathe(M, Mat::Metal, V3(kWheelXR - 4.f, 0.f, -26.f), Ex, Ez, {{0.f, -9.f}, {6.f, -8.f}, {10.f, -3.f}, {10.f, 3.f}, {6.f, 8.f}, {0.f, 10.f}}, 16);
	Tube(M, Mat::Metal, {V3(80.f, 0.f, -31.f), V3(kWheelXR + 10.f, 0.f, -28.f)}, 3.f, 10);
	RoundedBox(M, Mat::Engine, V3(100.f, 0.f, -26.f), V3(18.f, 10.f, 7.f), 3.f); // коробка передач
	RoundedBox(M, Mat::Metal, V3(-150.f, 0.f, -33.f), V3(24.f, 30.f, 1.5f), 1.f); // днище багажника/бак
	Tube(M, Mat::Metal, {V3(kWheelXF + 18.f, -52.f, -27.f), V3(kWheelXF + 18.f, 52.f, -27.f)}, 1.2f, 8); // стабилизатор

	// ---- Салон ----
	// Обдув лобового стекла: щели на верхней полке торпедо.
	{
		const V3 TX = Normalize(V3(1.f, 0.f, -0.56f));
		const V3 TZ = Normalize(V3(0.56f, 0.f, 1.f));
		for (float S : {-1.f, 1.f})
		{
			RoundedBox(M, Mat::BlackTrim, V3(81.f, S * 30.f, 42.45f), V3(1.4f, 15.f, 0.25f), 0.2f, TX, Ey, TZ);
			for (float Dy : {-10.f, -5.f, 0.f, 5.f, 10.f})
			{
				Box(M, Mat::InteriorDark, V3(81.f, S * 30.f + Dy, 42.6f), V3(1.3f, 0.15f, 0.2f), TX, Ey, TZ);
			}
		}
		// Решётка динамика посередине.
		RoundedBox(M, Mat::BlackTrim, V3(78.5f, 0.f, 44.f), V3(2.2f, 7.f, 0.25f), 0.3f, TX, Ey, TZ);
		for (float Dy : {-5.f, -3.f, -1.f, 1.f, 3.f, 5.f})
		{
			Box(M, Mat::Chrome, V3(78.5f, Dy, 44.15f), V3(2.f, 0.12f, 0.2f), TX, Ey, TZ);
		}
	}
	// Хромовый молдинг по торпедо со стороны пассажира, пепельница.
	Box(M, Mat::Chrome, V3(72.95f, 31.5f, 37.2f), V3(0.15f, 32.f, 0.25f));
	RoundedBox(M, Mat::Chrome, V3(72.7f, 2.f, 19.3f), V3(0.4f, 6.f, 1.4f), 0.4f);
	Box(M, Mat::InteriorDark, V3(72.3f, 2.f, 19.6f), V3(0.08f, 4.f, 0.2f));
	// Контрольные лампы между приборами.
	{
		const Mat Lamps[] = {Mat::Indicator, Mat::Taillamp, Mat::Headlamp, Mat::Indicator};
		for (int32_t K = 0; K < 4; ++K)
		{
			Lathe(M, Lamps[K], V3(71.85f, -35.f, 45.6f - 2.7f * static_cast<float>(K)), V3(-1.f, 0.f, 0.f), Ez, {{0.f, 0.15f}, {0.75f, 0.1f}, {0.8f, 0.f}}, 10,
				0.f, 1.f);
		}
	}
	// Ряд тумблеров слева от рулевой колонки.
	for (int32_t K = 0; K < 4; ++K)
	{
		const float Y = -60.f + 3.2f * static_cast<float>(K);
		RoundedBox(M, Mat::BlackTrim, V3(72.9f, Y, 27.f), V3(0.3f, 1.2f, 1.6f), 0.3f);
		Tube(M, Mat::Chrome, {V3(72.6f, Y, 27.f), V3(71.2f, Y, 27.8f)}, 0.25f, 6);
	}
	// Кожух рулевой колонки с подрулевыми рычагами.
	{
		const V3 Col = Normalize(V3(1.f, 0.f, -0.45f));
		const V3 Up = Cross(Col, Ey);
		const V3 C = kSteeringCenter + Col * 12.f;
		RoundedBox(M, Mat::InteriorDark, C, V3(6.5f, 4.2f, 3.8f), 1.5f, Col, Ey, Up * -1.f);
		for (float S : {-1.f, 1.f})
		{
			const V3 Root = C - Col * 4.f + Ey * (S * 4.f);
			Tube(M, Mat::BlackTrim, {Root, Root + Ey * (S * 9.f) - Col * 2.f + Up * 0.5f}, 0.55f, 6);
			Lathe(M, Mat::BlackTrim, Root + Ey * (S * 9.f) - Col * 2.f + Up * 0.5f, Ey * S, Ex, {{0.8f, 0.f}, {0.9f, 1.f}, {0.f, 1.4f}}, 8);
		}
	}
	// Коврики, накладки порогов, ручки над дверьми, дуги потолка, динамики на полке.
	for (float S : {-1.f, 1.f})
	{
		RoundedBox(M, Mat::Rubber, V3(46.f, S * 35.f, -25.6f), V3(20.f, 16.f, 0.45f), 0.4f);
		RoundedBox(M, Mat::Rubber, V3(-44.f, S * 35.f, -25.6f), V3(12.f, 16.f, 0.45f), 0.4f);
		for (float Dx : {-12.f, -6.f, 0.f, 6.f, 12.f})
		{
			Box(M, Mat::BlackTrim, V3(46.f + Dx, S * 35.f, -25.12f), V3(0.3f, 13.f, 0.06f));
		}
		Box(M, Mat::Chrome, V3(39.f, S * 70.5f, -22.45f), V3(46.f, 4.f, 0.12f));
		Box(M, Mat::Chrome, V3(-61.f, S * 70.5f, -22.45f), V3(38.f, 4.f, 0.12f));
		for (float X : {32.f, -60.f})
		{
			Tube(M, Mat::InteriorDark, {V3(X - 10.f, S * 61.8f, 83.f), V3(X - 8.5f, S * 60.f, 81.4f), V3(X + 8.5f, S * 60.f, 81.4f), V3(X + 10.f, S * 61.8f, 83.f)},
				0.8f, 8);
		}
		RoundedBox(M, Mat::Chrome, V3(-30.f, S * 61.8f, 81.5f), V3(0.6f, 0.6f, 1.2f), 0.3f); // крючок для одежды
		Lathe(M, Mat::BlackTrim, V3(-106.f, S * 38.f, 38.6f), Ez, Ex, {{0.f, 0.3f}, {6.f, 0.3f}, {6.5f, 0.f}}, 20, 0.f, 1.f);
		for (float Dx : {-3.f, -1.5f, 0.f, 1.5f, 3.f})
		{
			Box(M, Mat::InteriorDark, V3(-106.f + Dx, S * 38.f, 38.92f), V3(0.2f, std::sqrt(std::max(0.f, 30.f - Dx * Dx)), 0.05f));
		}
	}
	for (float X : {34.f, 8.f, -22.f, -50.f})
	{
		std::vector<V3> Bow;
		for (float T : Range(-1.f, 1.f, 0.1f))
		{
			const float Y = T * (RoofHalf() - 4.f);
			Bow.push_back(V3(X, Y, RoofZ(Y) - 2.15f));
		}
		Tube(M, Mat::Headliner, Bow, 0.35f, 6);
	}
	// Сиденья: поперечная «стёжка» подушек и спинок, задняя панель спинок с карманом.
	for (float S : {-1.f, 1.f})
	{
		const float Y = S * 35.f;
		const float Fx = 6.f;
		for (float X : Range(Fx - 26.f, Fx + 10.f, 6.f))
		{
			RoundedBox(M, Mat::Fabric, V3(X, Y, -6.2f), V3(2.4f, 16.f, 0.8f), 0.7f);
		}
		const float Tilt = Rad(12.f);
		const V3 BackZ(-std::sin(Tilt), 0.f, std::cos(Tilt));
		const V3 BackX(std::cos(Tilt), 0.f, std::sin(Tilt));
		const V3 BackC(Fx - 35.f, Y, 18.f);
		for (float T : Range(-20.f, 18.f, 6.4f))
		{
			RoundedBox(M, Mat::Fabric, BackC + BackX * 4.9f + BackZ * T, V3(0.8f, 15.5f, 2.4f), 0.7f, BackX, Ey, BackZ);
		}
		RoundedBox(M, Mat::InteriorDark, BackC - BackX * 5.1f, V3(0.4f, 21.f, 24.f), 0.3f, BackX, Ey, BackZ);
		RoundedBox(M, Mat::InteriorDark, BackC - BackX * 5.8f - BackZ * 10.f, V3(0.5f, 17.f, 8.f), 0.4f, BackX, Ey, BackZ);
		// Рычаг наклона спинки.
		Tube(M, Mat::Chrome, {V3(Fx - 26.f, Y + S * 25.f, -8.f), V3(Fx - 20.f, Y + S * 26.f, -6.f)}, 0.5f, 6);
		// Задний диван: стёжка.
		const float RY = S * 30.f;
		for (float X : Range(-92.f, -60.f, 6.4f))
		{
			RoundedBox(M, Mat::Fabric, V3(X, RY, -7.2f), V3(2.4f, 22.f, 0.8f), 0.7f);
		}
		const float RT = Rad(15.f);
		const V3 RBX(std::cos(RT), 0.f, std::sin(RT));
		const V3 RBZ(-std::sin(RT), 0.f, std::cos(RT));
		for (float T : Range(-18.f, 18.f, 6.f))
		{
			RoundedBox(M, Mat::Fabric, V3(-98.f, RY, 13.f) + RBX * 5.4f + RBZ * T, V3(0.8f, 22.f, 2.3f), 0.7f, RBX, Ey, RBZ);
		}
		// Задние ремни: замки на подушке.
		RoundedBox(M, Mat::BlackTrim, V3(-97.f, S * 8.f, -6.f), V3(1.5f, 1.f, 2.5f), 0.5f);
	}
	// Подлокотник-перегородка в спинке дивана.
	RoundedBox(M, Mat::Fabric, V3(-96.f, 0.f, 4.f), V3(4.5f, 6.f, 14.f), 2.5f);

	// ---- Под капотом ----
	Lathe(M, Mat::BlackTrim, V3(94.f, -36.f, 24.f), Ex, Ez, {{0.f, 0.f}, {9.5f, 0.f}, {10.f, 1.5f}, {9.5f, 5.f}, {0.f, 6.f}}, 24); // вакуумник
	RoundedBox(M, Mat::Engine, V3(99.f, 26.f, 22.f), V3(5.f, 14.f, 9.f), 2.f);  // корпус печки
	Lathe(M, Mat::Metal, V3(98.f, -12.f, 31.f), Ey, Ez, {{0.f, -4.f}, {3.f, -4.f}, {3.f, 4.f}, {0.f, 4.f}}, 12); // моторчик дворников
	RoundedBox(M, Mat::BlackTrim, V3(97.f, -22.f, 29.f), V3(2.f, 5.f, 3.5f), 0.5f); // блок предохранителей
	Lathe(M, Mat::Metal, V3(110.f, 34.f, 16.f), Ez, Ex, {{0.f, 0.f}, {2.8f, 0.f}, {2.8f, 11.f}, {1.6f, 12.5f}, {0.f, 12.5f}}, 12); // катушка
	Tube(M, Mat::BlackTrim, {V3(110.f, 34.f, 28.5f), V3(118.f, 22.f, 30.f), V3(150.f, 12.f, 26.f)}, 0.4f, 6);
	Tube(M, Mat::BlackTrim, {V3(95.5f, -40.f, 33.f), V3(95.5f, 40.f, 33.f)}, 0.6f, 6); // жгут проводов
	Tube(M, Mat::BlackTrim, {V3(95.5f, -40.f, 31.f), V3(150.f, -42.f, 30.f), V3(188.f, -40.f, 24.f), V3(190.f, -20.f, 22.f)}, 0.6f, 6);
	Lathe(M, Mat::BlackTrim, V3(183.f, 0.f, 2.f), Ex, Ez, {{13.f, 0.f}, {14.f, 0.f}, {14.f, 5.f}, {13.f, 5.f}}, 24); // кожух вентилятора
	Lathe(M, Mat::BlackTrim, V3(194.f, 32.f, -12.f), V3(1.f, 0.f, 0.f), Ez, {{0.f, 0.f}, {2.f, 0.f}, {5.f, 3.f}, {5.f, 3.5f}, {0.f, 3.5f}}, 14); // сигнал
	RoundedBox(M, Mat::Metal, V3(193.f, 0.f, 30.5f), V3(1.5f, 4.f, 2.f), 0.4f); // замок капота
	Tube(M, Mat::Indicator, {V3(150.f, -17.f, 8.f), V3(151.f, -18.f, 26.f)}, 0.35f, 6); // щуп
	Lathe(M, Mat::Indicator, V3(151.f, -18.f, 26.f), Ez, Ex, {{0.f, 1.f}, {1.f, 1.f}, {1.f, 0.f}}, 8);
	Tube(M, Mat::Metal, {V3(152.f, 18.f, 6.f), V3(150.f, 30.f, -6.f), V3(160.f, 36.f, -20.f)}, 1.f, 6); // трубка топлива/тормозов

	// ---- Багажник ----
	Box(M, Mat::Rubber, V3(-156.f, 0.f, -19.7f), V3(42.f, 60.f, 0.25f));
	Tube(M, Mat::Metal, {V3(-150.f, 48.f, 2.f), V3(-150.f, 62.f, 10.f), V3(-150.f, HalfW(-150.f, 18.f) - 2.f, 18.f)}, 2.2f, 10); // горловина бака
	RoundedBox(M, Mat::Dome, V3(-110.f, 0.f, 35.f), V3(0.6f, 4.f, 1.2f), 0.3f); // плафон багажника
	(void)Ez;
}
} // namespace

// ---------------------------------------------------------------------------------------------
Model BuildSokol()
{
	Builder B;
	{
		MeshSet M;
		BuildLowerBody(M);
		BuildGreenhouse(M);
		BuildFrontRear(M, B.Out);
		BuildInterior(M);
		BuildEngineBay(M);
		BuildTrunkInside(M);
		BuildDetails(M);
		B.Add(Part::Body, M, V3(), false);
		B.Out.HitBoxes.push_back({Part::Body, "Fuel", V3(-150.f, 80.5f, 18.f), V3(2.f, 6.f, 6.f)});
		// Полости бардачка и багажника: видны лучу, только когда крышка открыта (закрытая — ближе и перекрывает).
		B.Out.HitBoxes.push_back({Part::Body, "GloveInside", V3(79.f, 32.f, 29.f), V3(5.f, 15.f, 6.f)});
		B.Out.HitBoxes.push_back({Part::Body, "TrunkInside", V3(-158.f, 0.f, 4.f), V3(36.f, 46.f, 16.f)});
	}
	BuildHoodTrunk(B);
	for (Part Door : {Part::DoorFL, Part::DoorFR, Part::DoorRL, Part::DoorRR})
	{
		BuildDoor(B, Door);
	}
	BuildControls(B);
	BuildWipers(B);
	B.Out.SteeringColumnDir = Normalize(V3(-1.f, 0.f, 0.45f));
	B.Out.DriverEye = V3(-22.f, -35.f, 74.f);
	B.Out.ExitPoint = V3(-25.f, -150.f, 60.f);
	return B.Out;
}

const PartMesh* Model::Find(Part Id) const
{
	for (const PartMesh& P : Parts)
	{
		if (P.Id == Id)
		{
			return &P;
		}
	}
	return nullptr;
}

size_t Model::TriangleCount() const
{
	size_t N = 0;
	for (const PartMesh& P : Parts)
	{
		for (const Section& S : P.Sections)
		{
			N += S.Indices.size() / 3;
		}
	}
	return N;
}

const char* PartName(Part Id)
{
	static const char* Names[] = {"Body", "Hood", "Trunk", "DoorFL", "DoorFR", "DoorRL", "DoorRR", "Glovebox", "Steering", "Key", "LightKnob",
		"GearLever", "Handbrake", "ClutchPedal", "BrakePedal", "GasPedal", "SpeedoNeedle", "TachoNeedle", "FuelNeedle", "TempNeedle", "WheelLeft",
		"WheelRight", "WiperL", "WiperR"};
	const size_t I = static_cast<size_t>(Id);
	return I < sizeof(Names) / sizeof(Names[0]) ? Names[I] : "?";
}

const char* MatName(Mat Id)
{
	static const char* Names[] = {"Paint", "Chrome", "BlackTrim", "Rubber", "Glass", "Headlamp", "Taillamp", "Indicator", "Interior", "InteriorDark",
		"Fabric", "Carpet", "Headliner", "GaugeFace", "Needle", "Engine", "Metal", "Plate", "Water", "Dome"};
	const size_t I = static_cast<size_t>(Id);
	return I < sizeof(Names) / sizeof(Names[0]) ? Names[I] : "?";
}
} // namespace vmodel
