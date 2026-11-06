// Peregon — VehicleSimCore
// Внутренние примитивы для процедурных моделей: сетки по параметрическим поверхностям, толстые панели,
// тела вращения, протяжки профиля, скруглённые боксы. Всё во float, сантиметрах, осях Unreal.
#pragma once

#include "vsim/BodyModel.h"

#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace vmodel
{
namespace prim
{
constexpr float Pi = 3.14159265358979f;

inline V3 operator+(const V3& A, const V3& B) { return V3(A.X + B.X, A.Y + B.Y, A.Z + B.Z); }
inline V3 operator-(const V3& A, const V3& B) { return V3(A.X - B.X, A.Y - B.Y, A.Z - B.Z); }
inline V3 operator-(const V3& A) { return V3(-A.X, -A.Y, -A.Z); }
inline V3 operator*(const V3& A, float S) { return V3(A.X * S, A.Y * S, A.Z * S); }
inline V3 operator*(float S, const V3& A) { return A * S; }
inline float Dot(const V3& A, const V3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
inline V3 Cross(const V3& A, const V3& B) { return V3(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X); }
inline float Length(const V3& A) { return std::sqrt(Dot(A, A)); }
inline V3 Normalize(const V3& A, const V3& Fallback = V3(0.f, 0.f, 1.f))
{
	const float L = Length(A);
	return L > 1e-8f ? A * (1.f / L) : Fallback;
}
inline V3 Lerp(const V3& A, const V3& B, float T) { return A + (B - A) * T; }
inline float Lerpf(float A, float B, float T) { return A + (B - A) * T; }
inline float Clampf(float V, float Lo, float Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
inline float Smooth(float E0, float E1, float X)
{
	const float T = Clampf((X - E0) / (E1 - E0), 0.f, 1.f);
	return T * T * (3.f - 2.f * T);
}
inline float Rad(float Deg) { return Deg * Pi / 180.f; }

// Монотонный кубический сплайн (PCHIP): без «перелётов» между контрольными точками.
class Curve
{
public:
	Curve() = default;
	Curve(std::vector<float> InX, std::vector<float> InY);
	float operator()(float X) const;

private:
	std::vector<float> Xs;
	std::vector<float> Ys;
	std::vector<float> Ms;
};

// Набор секций по материалам для одного узла.
class MeshSet
{
public:
	Section& Get(Mat M);
	std::vector<Section> Take();

	// Треугольник; порядок вершин подбирается под нормали (лицевая сторона наружу).
	void Tri(Section& S, int32_t A, int32_t B, int32_t C);
	int32_t Vertex(Section& S, const V3& P, const V3& N, float U, float V);

private:
	std::vector<Section> Sections;
};

using SurfFn = std::function<V3(float A, float B)>;
using HintFn = std::function<V3(const V3& P)>;

struct Grid
{
	int32_t NU = 0;
	int32_t NV = 0;
	std::vector<V3> P;
	std::vector<V3> N;
	const V3& At(int32_t I, int32_t J) const { return P[static_cast<size_t>(J * NU + I)]; }
	const V3& NormalAt(int32_t I, int32_t J) const { return N[static_cast<size_t>(J * NU + I)]; }
};

// Сетка по поверхности. Domain(i, j) → параметры (a, b), Surf(a, b) → точка.
// Нормали — по производным самой поверхности (не по сетке), поэтому стянутые края не портят свет.
Grid SampleGrid(int32_t NU, int32_t NV, const std::function<std::pair<float, float>(int32_t, int32_t)>& Domain, const SurfFn& Surf,
	const HintFn& Hint, float Eps);
// Прямоугольная область параметров по готовым спискам значений.
Grid SampleGrid(const std::vector<float>& As, const std::vector<float>& Bs, const SurfFn& Surf, const HintFn& Hint, float Eps);

void EmitGrid(MeshSet& Out, Mat M, const Grid& G, float UvScale = 0.01f);
// Панель с толщиной: лицо, изнанка (внутрь по нормали или по Dir) и торцы по контуру.
void EmitThick(MeshSet& Out, const Grid& G, float Thickness, Mat Outer, Mat Inner, Mat Rim, const HintFn& InwardDir = nullptr);

struct ProfilePoint
{
	float A = 0.f; // для тела вращения — радиус; для протяжки — смещение по «вверх»
	float B = 0.f; // для тела вращения — высота по оси; для протяжки — смещение «наружу»
	bool bSharp = false;
};

// Тело вращения. Нормали профиля — от центра профиля, либо в сторону Hint2D (если задан не нулевой).
void Lathe(MeshSet& Out, Mat M, const V3& Origin, const V3& Axis, const V3& Ref, const std::vector<ProfilePoint>& Profile, int32_t Segments,
	float HintR = 0.f, float HintH = 0.f, float A0 = 0.f, float A1 = 2.f * Pi);
// Протяжка замкнутого профиля вдоль ломаной. Up задаёт ориентацию профиля, OutwardRef — куда смотрит «наружу» (B > 0).
void Sweep(MeshSet& Out, Mat M, const std::vector<V3>& Path, const std::vector<ProfilePoint>& Profile, const V3& Up, bool bClosedPath,
	bool bCaps, const V3* OutwardRef = nullptr);
void Tube(MeshSet& Out, Mat M, const std::vector<V3>& Path, float Radius, int32_t Sides, bool bClosedPath = false, bool bCaps = true);
// Бокс со скруглёнными рёбрами. Оси — единичные векторы (локальный поворот), Half — полуразмеры.
void RoundedBox(MeshSet& Out, Mat M, const V3& Center, const V3& Half, float Radius, const V3& Ex = V3(1.f, 0.f, 0.f),
	const V3& Ey = V3(0.f, 1.f, 0.f), const V3& Ez = V3(0.f, 0.f, 1.f), int32_t Segments = 3);
inline void Box(MeshSet& Out, Mat M, const V3& Center, const V3& Half, const V3& Ex = V3(1.f, 0.f, 0.f), const V3& Ey = V3(0.f, 1.f, 0.f),
	const V3& Ez = V3(0.f, 0.f, 1.f))
{
	RoundedBox(Out, M, Center, Half, 0.f, Ex, Ey, Ez, 1);
}

std::vector<ProfilePoint> CircleProfile(float Radius, int32_t Sides);
std::vector<ProfilePoint> RoundRectProfile(float HalfA, float HalfB, float Radius, int32_t CornerSegments);
// Значения от A до B с шагом не больше Step (концы включены).
std::vector<float> Range(float A, float B, float Step);
} // namespace prim
} // namespace vmodel
