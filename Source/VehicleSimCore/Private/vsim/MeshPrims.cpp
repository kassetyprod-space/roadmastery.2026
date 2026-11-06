// Peregon — VehicleSimCore
#include "MeshPrims.h"

#include <algorithm>

namespace vmodel
{
namespace prim
{
// ---------------------------------------------------------------------------------------------
// Сплайн
// ---------------------------------------------------------------------------------------------
Curve::Curve(std::vector<float> InX, std::vector<float> InY) : Xs(std::move(InX)), Ys(std::move(InY))
{
	const size_t N = Xs.size();
	Ms.assign(N, 0.f);
	if (N < 2)
	{
		return;
	}
	std::vector<float> D(N - 1);
	for (size_t K = 0; K + 1 < N; ++K)
	{
		D[K] = (Ys[K + 1] - Ys[K]) / (Xs[K + 1] - Xs[K]);
	}
	Ms[0] = D[0];
	Ms[N - 1] = D[N - 2];
	for (size_t K = 1; K + 1 < N; ++K)
	{
		Ms[K] = (D[K - 1] * D[K] <= 0.f) ? 0.f : 2.f / (1.f / D[K - 1] + 1.f / D[K]);
	}
}

float Curve::operator()(float X) const
{
	const size_t N = Xs.size();
	if (N == 0)
	{
		return 0.f;
	}
	if (X <= Xs.front())
	{
		return Ys.front() + Ms.front() * (X - Xs.front());
	}
	if (X >= Xs.back())
	{
		return Ys.back() + Ms.back() * (X - Xs.back());
	}
	size_t K = 0;
	while (K + 2 < N && X > Xs[K + 1])
	{
		++K;
	}
	const float H = Xs[K + 1] - Xs[K];
	const float T = (X - Xs[K]) / H;
	const float T2 = T * T;
	const float T3 = T2 * T;
	return (2.f * T3 - 3.f * T2 + 1.f) * Ys[K] + (T3 - 2.f * T2 + T) * H * Ms[K] + (-2.f * T3 + 3.f * T2) * Ys[K + 1] +
		(T3 - T2) * H * Ms[K + 1];
}

// ---------------------------------------------------------------------------------------------
// Секции
// ---------------------------------------------------------------------------------------------
Section& MeshSet::Get(Mat M)
{
	for (Section& S : Sections)
	{
		if (S.Material == M)
		{
			return S;
		}
	}
	Sections.emplace_back();
	Sections.back().Material = M;
	return Sections.back();
}

std::vector<Section> MeshSet::Take()
{
	std::vector<Section> Out;
	for (Section& S : Sections)
	{
		if (!S.Indices.empty())
		{
			Out.push_back(std::move(S));
		}
	}
	Sections.clear();
	return Out;
}

int32_t MeshSet::Vertex(Section& S, const V3& P, const V3& N, float U, float V)
{
	S.Positions.push_back(P);
	S.Normals.push_back(N);
	S.U.push_back(U);
	S.V.push_back(V);
	return static_cast<int32_t>(S.Positions.size() - 1);
}

void MeshSet::Tri(Section& S, int32_t A, int32_t B, int32_t C)
{
	const V3& PA = S.Positions[static_cast<size_t>(A)];
	const V3& PB = S.Positions[static_cast<size_t>(B)];
	const V3& PC = S.Positions[static_cast<size_t>(C)];
	const V3 Face = Cross(PC - PA, PB - PA); // лицевая нормаль по правилу Unreal
	if (Dot(Face, Face) < 1e-10f)
	{
		return; // вырожденный (стянутый край сетки)
	}
	const V3 Avg = S.Normals[static_cast<size_t>(A)] + S.Normals[static_cast<size_t>(B)] + S.Normals[static_cast<size_t>(C)];
	if (Dot(Face, Avg) >= 0.f)
	{
		S.Indices.insert(S.Indices.end(), {A, B, C});
	}
	else
	{
		S.Indices.insert(S.Indices.end(), {A, C, B});
	}
}

// ---------------------------------------------------------------------------------------------
// Сетки
// ---------------------------------------------------------------------------------------------
namespace
{
V3 SurfaceNormal(const SurfFn& Surf, float A, float B, float Eps, const V3& HintDir)
{
	const V3 DA = Surf(A + Eps, B) - Surf(A - Eps, B);
	const V3 DB = Surf(A, B + Eps) - Surf(A, B - Eps);
	V3 N = Cross(DA, DB);
	if (Dot(N, N) < 1e-12f)
	{
		return Normalize(HintDir);
	}
	N = Normalize(N);
	return Dot(N, HintDir) < 0.f ? -N : N;
}

// Проекция для UV: по главной оси средней нормали.
void PlanarUV(const V3& P, const V3& N, float Scale, float& U, float& V)
{
	const float AX = std::fabs(N.X);
	const float AY = std::fabs(N.Y);
	const float AZ = std::fabs(N.Z);
	if (AZ >= AX && AZ >= AY)
	{
		U = P.X * Scale;
		V = P.Y * Scale;
	}
	else if (AY >= AX)
	{
		U = P.X * Scale;
		V = -P.Z * Scale;
	}
	else
	{
		U = P.Y * Scale;
		V = -P.Z * Scale;
	}
}
} // namespace

Grid SampleGrid(int32_t NU, int32_t NV, const std::function<std::pair<float, float>(int32_t, int32_t)>& Domain, const SurfFn& Surf,
	const HintFn& Hint, float Eps)
{
	Grid G;
	G.NU = NU;
	G.NV = NV;
	G.P.reserve(static_cast<size_t>(NU * NV));
	G.N.reserve(static_cast<size_t>(NU * NV));
	for (int32_t J = 0; J < NV; ++J)
	{
		for (int32_t I = 0; I < NU; ++I)
		{
			const std::pair<float, float> AB = Domain(I, J);
			const V3 P = Surf(AB.first, AB.second);
			G.P.push_back(P);
			G.N.push_back(SurfaceNormal(Surf, AB.first, AB.second, Eps, Hint(P)));
		}
	}
	return G;
}

Grid SampleGrid(const std::vector<float>& As, const std::vector<float>& Bs, const SurfFn& Surf, const HintFn& Hint, float Eps)
{
	return SampleGrid(
		static_cast<int32_t>(As.size()), static_cast<int32_t>(Bs.size()),
		[&](int32_t I, int32_t J) { return std::make_pair(As[static_cast<size_t>(I)], Bs[static_cast<size_t>(J)]); }, Surf, Hint, Eps);
}

void EmitGrid(MeshSet& Out, Mat M, const Grid& G, float UvScale)
{
	if (G.NU < 2 || G.NV < 2)
	{
		return;
	}
	Section& S = Out.Get(M);
	V3 Avg;
	for (const V3& N : G.N)
	{
		Avg = Avg + N;
	}
	const int32_t Base = static_cast<int32_t>(S.Positions.size());
	for (size_t K = 0; K < G.P.size(); ++K)
	{
		float U = 0.f;
		float V = 0.f;
		PlanarUV(G.P[K], Avg, UvScale, U, V);
		Out.Vertex(S, G.P[K], G.N[K], U, V);
	}
	for (int32_t J = 0; J + 1 < G.NV; ++J)
	{
		for (int32_t I = 0; I + 1 < G.NU; ++I)
		{
			const int32_t A = Base + J * G.NU + I;
			const int32_t B = A + 1;
			const int32_t C = A + G.NU;
			const int32_t D = C + 1;
			Out.Tri(S, A, B, C);
			Out.Tri(S, B, D, C);
		}
	}
}

void EmitThick(MeshSet& Out, const Grid& G, float Thickness, Mat Outer, Mat Inner, Mat Rim, const HintFn& InwardDir)
{
	EmitGrid(Out, Outer, G);
	Grid In = G;
	V3 Center;
	for (size_t K = 0; K < G.P.size(); ++K)
	{
		const V3 Dir = InwardDir ? Normalize(InwardDir(G.P[K])) : -G.N[K];
		In.P[K] = G.P[K] + Dir * Thickness;
		In.N[K] = -G.N[K];
		Center = Center + G.P[K];
	}
	Center = Center * (1.f / static_cast<float>(std::max<size_t>(1, G.P.size())));
	EmitGrid(Out, Inner, In);

	// Торцы: полоса из двух рядов (лицо → изнанка) по каждому краю.
	auto EdgeStrip = [&](int32_t Count, const std::function<int32_t(int32_t)>& IndexOf) {
		Grid Strip;
		Strip.NU = Count;
		Strip.NV = 2;
		Strip.P.resize(static_cast<size_t>(Count * 2));
		Strip.N.resize(static_cast<size_t>(Count * 2));
		for (int32_t K = 0; K < Count; ++K)
		{
			const size_t Idx = static_cast<size_t>(IndexOf(K));
			const size_t Prev = static_cast<size_t>(IndexOf(std::max(0, K - 1)));
			const size_t Next = static_cast<size_t>(IndexOf(std::min(Count - 1, K + 1)));
			const V3 Tangent = G.P[Next] - G.P[Prev];
			V3 N = Normalize(Cross(Tangent, G.N[Idx]), Normalize(G.P[Idx] - Center));
			if (Dot(N, G.P[Idx] - Center) < 0.f)
			{
				N = -N;
			}
			Strip.P[static_cast<size_t>(K)] = G.P[Idx];
			Strip.P[static_cast<size_t>(Count + K)] = In.P[Idx];
			Strip.N[static_cast<size_t>(K)] = N;
			Strip.N[static_cast<size_t>(Count + K)] = N;
		}
		EmitGrid(Out, Rim, Strip);
	};
	EdgeStrip(G.NU, [&](int32_t K) { return K; });
	EdgeStrip(G.NU, [&](int32_t K) { return (G.NV - 1) * G.NU + K; });
	EdgeStrip(G.NV, [&](int32_t K) { return K * G.NU; });
	EdgeStrip(G.NV, [&](int32_t K) { return K * G.NU + G.NU - 1; });
}

// ---------------------------------------------------------------------------------------------
// Профили
// ---------------------------------------------------------------------------------------------
namespace
{
struct RingEntry
{
	float A, B, NA, NB;
};

// Нормали профиля (2D) с дублированием острых вершин. Hint (если не ноль) задаёт сторону, иначе сторона
// берётся из обхода многоугольника (открытый профиль замыкается отрезком «конец → начало»): точно и для вогнутых.
std::vector<RingEntry> BuildRings(const std::vector<ProfilePoint>& Profile, bool bClosed, float HintA, float HintB)
{
	const size_t N = Profile.size();
	float Area2 = 0.f;
	for (size_t K = 0; K < N; ++K)
	{
		const ProfilePoint& P0 = Profile[K];
		const ProfilePoint& P1 = Profile[(K + 1) % N];
		Area2 += P0.A * P1.B - P1.A * P0.B;
	}
	const bool bHint = HintA != 0.f || HintB != 0.f;
	const float Orient = Area2 >= 0.f ? 1.f : -1.f;

	auto SegNormal = [&](size_t I0, size_t I1) {
		const float TA = Profile[I1].A - Profile[I0].A;
		const float TB = Profile[I1].B - Profile[I0].B;
		float NA = TB;
		float NB = -TA;
		const float L = std::sqrt(NA * NA + NB * NB);
		if (L < 1e-8f)
		{
			return std::make_pair(0.f, 0.f);
		}
		NA /= L;
		NB /= L;
		// (TB, −TA) смотрит наружу при обходе против часовой (площадь > 0).
		const float Side = bHint ? (NA * HintA + NB * HintB) : Orient;
		if (Side < 0.f)
		{
			NA = -NA;
			NB = -NB;
		}
		return std::make_pair(NA, NB);
	};

	std::vector<RingEntry> Rings;
	for (size_t K = 0; K < N; ++K)
	{
		const bool bFirst = K == 0;
		const bool bLast = K + 1 == N;
		std::pair<float, float> In(0.f, 0.f);
		std::pair<float, float> OutN(0.f, 0.f);
		if (!bFirst || bClosed)
		{
			In = SegNormal(bFirst ? N - 1 : K - 1, K);
		}
		if (!bLast || bClosed)
		{
			OutN = SegNormal(K, bLast ? 0 : K + 1);
		}
		if (bFirst && !bClosed)
		{
			In = OutN;
		}
		if (bLast && !bClosed)
		{
			OutN = In;
		}
		const ProfilePoint& P = Profile[K];
		if (P.bSharp)
		{
			Rings.push_back({P.A, P.B, In.first, In.second});
			Rings.push_back({P.A, P.B, OutN.first, OutN.second});
		}
		else
		{
			float NA = In.first + OutN.first;
			float NB = In.second + OutN.second;
			const float L = std::sqrt(NA * NA + NB * NB);
			if (L > 1e-8f)
			{
				NA /= L;
				NB /= L;
			}
			Rings.push_back({P.A, P.B, NA, NB});
		}
	}
	if (bClosed && !Rings.empty())
	{
		Rings.push_back(Rings.front());
	}
	return Rings;
}
} // namespace

void Lathe(MeshSet& Out, Mat M, const V3& Origin, const V3& Axis, const V3& Ref, const std::vector<ProfilePoint>& Profile, int32_t Segments,
	float HintR, float HintH, float A0, float A1)
{
	if (Profile.size() < 2)
	{
		return;
	}
	const V3 Ax = Normalize(Axis);
	const V3 R0 = Normalize(Ref - Ax * Dot(Ref, Ax));
	const V3 R1 = Cross(Ax, R0);
	const std::vector<RingEntry> Rings = BuildRings(Profile, false, HintR, HintH);
	Section& S = Out.Get(M);
	const int32_t Cols = Segments + 1;
	const int32_t Base = static_cast<int32_t>(S.Positions.size());
	for (const RingEntry& R : Rings)
	{
		for (int32_t K = 0; K < Cols; ++K)
		{
			const float T = Lerpf(A0, A1, static_cast<float>(K) / static_cast<float>(Segments));
			const V3 Radial = R0 * std::cos(T) + R1 * std::sin(T);
			const V3 P = Origin + Ax * R.B + Radial * R.A;
			const V3 N = Normalize(Radial * R.NA + Ax * R.NB, Ax);
			const float U = T / (2.f * Pi) * 4.f;
			const float V = (R.A + R.B) * 0.05f;
			Out.Vertex(S, P, N, U, V);
		}
	}
	for (size_t J = 0; J + 1 < Rings.size(); ++J)
	{
		for (int32_t K = 0; K < Segments; ++K)
		{
			const int32_t A = Base + static_cast<int32_t>(J) * Cols + K;
			const int32_t B = A + 1;
			const int32_t C = A + Cols;
			const int32_t D = C + 1;
			Out.Tri(S, A, B, C);
			Out.Tri(S, B, D, C);
		}
	}
}

void Sweep(MeshSet& Out, Mat M, const std::vector<V3>& Path, const std::vector<ProfilePoint>& Profile, const V3& Up, bool bClosedPath,
	bool bCaps, const V3* OutwardRef)
{
	const size_t NP = Path.size();
	if (NP < 2 || Profile.size() < 3)
	{
		return;
	}
	const std::vector<RingEntry> Rings = BuildRings(Profile, true, 0.f, 0.f);
	std::vector<V3> Ts(NP);
	std::vector<V3> Ns(NP);
	std::vector<V3> Bs(NP);
	for (size_t K = 0; K < NP; ++K)
	{
		size_t Prev = K == 0 ? (bClosedPath ? NP - 1 : 0) : K - 1;
		size_t Next = K + 1 == NP ? (bClosedPath ? 0 : NP - 1) : K + 1;
		const V3 T = Normalize(Path[Next] - Path[Prev], V3(1.f, 0.f, 0.f));
		V3 N = Up - T * Dot(Up, T);
		if (Dot(N, N) < 1e-6f)
		{
			N = Cross(T, V3(0.f, 1.f, 0.f));
		}
		N = Normalize(N);
		Ts[K] = T;
		Ns[K] = N;
		Bs[K] = Cross(T, N);
	}
	if (OutwardRef)
	{
		const size_t Mid = NP / 2;
		if (Dot(Bs[Mid], Path[Mid] - *OutwardRef) < 0.f)
		{
			for (V3& B : Bs)
			{
				B = -B;
			}
		}
	}
	Section& S = Out.Get(M);
	const int32_t RingCount = static_cast<int32_t>(Rings.size());
	const int32_t Base = static_cast<int32_t>(S.Positions.size());
	float Along = 0.f;
	for (size_t K = 0; K < NP; ++K)
	{
		if (K > 0)
		{
			Along += Length(Path[K] - Path[K - 1]);
		}
		for (int32_t R = 0; R < RingCount; ++R)
		{
			const RingEntry& E = Rings[static_cast<size_t>(R)];
			const V3 P = Path[K] + Ns[K] * E.A + Bs[K] * E.B;
			const V3 N = Normalize(Ns[K] * E.NA + Bs[K] * E.NB, Ns[K]);
			Out.Vertex(S, P, N, Along * 0.02f, static_cast<float>(R) / static_cast<float>(RingCount));
		}
	}
	const size_t Segs = bClosedPath ? NP : NP - 1;
	for (size_t K = 0; K < Segs; ++K)
	{
		const int32_t Row0 = Base + static_cast<int32_t>(K) * RingCount;
		const int32_t Row1 = Base + static_cast<int32_t>((K + 1) % NP) * RingCount;
		for (int32_t R = 0; R + 1 < RingCount; ++R)
		{
			Out.Tri(S, Row0 + R, Row0 + R + 1, Row1 + R);
			Out.Tri(S, Row0 + R + 1, Row1 + R + 1, Row1 + R);
		}
	}
	if (bCaps && !bClosedPath)
	{
		for (int32_t End = 0; End < 2; ++End)
		{
			const size_t K = End == 0 ? 0 : NP - 1;
			const V3 Nc = End == 0 ? -Ts[K] : Ts[K];
			const int32_t Center = Out.Vertex(S, Path[K], Nc, 0.f, 0.f);
			std::vector<int32_t> Ring;
			for (const ProfilePoint& P : Profile)
			{
				Ring.push_back(Out.Vertex(S, Path[K] + Ns[K] * P.A + Bs[K] * P.B, Nc, P.A * 0.05f, P.B * 0.05f));
			}
			for (size_t I = 0; I < Ring.size(); ++I)
			{
				Out.Tri(S, Center, Ring[I], Ring[(I + 1) % Ring.size()]);
			}
		}
	}
}

void Tube(MeshSet& Out, Mat M, const std::vector<V3>& Path, float Radius, int32_t Sides, bool bClosedPath, bool bCaps)
{
	// «Вверх» для рамок — перпендикуляр к среднему направлению пути.
	V3 Dir = Path.size() >= 2 ? Path.back() - Path.front() : V3(1.f, 0.f, 0.f);
	if (Length(Dir) < 1e-3f && Path.size() >= 3)
	{
		Dir = Path[1] - Path[0];
	}
	Dir = Normalize(Dir, V3(1.f, 0.f, 0.f));
	V3 Up = std::fabs(Dir.Z) < 0.9f ? V3(0.f, 0.f, 1.f) : V3(1.f, 0.f, 0.f);
	if (bClosedPath && Path.size() >= 3)
	{
		// Нормаль плоскости кольца: обод руля, уплотнители стёкол.
		V3 Plane;
		for (size_t K = 0; K < Path.size(); ++K)
		{
			Plane = Plane + Cross(Path[K], Path[(K + 1) % Path.size()]);
		}
		Up = Normalize(Plane, Up);
	}
	Sweep(Out, M, Path, CircleProfile(Radius, Sides), Up, bClosedPath, bCaps);
}

void RoundedBox(MeshSet& Out, Mat M, const V3& Center, const V3& Half, float Radius, const V3& Ex, const V3& Ey, const V3& Ez, int32_t Segments)
{
	const float H[3] = {Half.X, Half.Y, Half.Z};
	const float R = std::max(0.f, std::min(Radius, std::min(H[0], std::min(H[1], H[2])) * 0.999f));
	// Мелким скруглениям хватает одного-двух сегментов.
	const int32_t Seg = std::max(1, std::min(Segments, R < 0.8f ? 1 : (R < 2.5f ? 2 : Segments)));
	auto Samples = [&](float Hh) {
		std::vector<float> Out1D;
		const float Inner = Hh - R;
		if (R <= 0.f)
		{
			return std::vector<float>{-Hh, Hh};
		}
		for (int32_t K = Seg; K >= 0; --K)
		{
			Out1D.push_back(-(Inner + R * std::tan(Rad(45.f) * static_cast<float>(K) / static_cast<float>(Seg))));
		}
		const int32_t Mid = std::max(1, static_cast<int32_t>(std::ceil(2.f * Inner / 30.f)));
		for (int32_t K = 1; K < Mid; ++K)
		{
			Out1D.push_back(-Inner + 2.f * Inner * static_cast<float>(K) / static_cast<float>(Mid));
		}
		for (int32_t K = 0; K <= Seg; ++K)
		{
			Out1D.push_back(Inner + R * std::tan(Rad(45.f) * static_cast<float>(K) / static_cast<float>(Seg)));
		}
		return Out1D;
	};
	const V3 Axes[3] = {Ex, Ey, Ez};
	for (int32_t Axis = 0; Axis < 3; ++Axis)
	{
		const int32_t A1 = (Axis + 1) % 3;
		const int32_t A2 = (Axis + 2) % 3;
		const std::vector<float> S1 = Samples(H[A1]);
		const std::vector<float> S2 = Samples(H[A2]);
		for (int32_t Sign = -1; Sign <= 1; Sign += 2)
		{
			Grid G;
			G.NU = static_cast<int32_t>(S1.size());
			G.NV = static_cast<int32_t>(S2.size());
			for (float V2 : S2)
			{
				for (float V1 : S1)
				{
					float Q[3];
					Q[Axis] = static_cast<float>(Sign) * H[Axis];
					Q[A1] = V1;
					Q[A2] = V2;
					float C[3];
					for (int32_t I = 0; I < 3; ++I)
					{
						C[I] = Clampf(Q[I], -(H[I] - R), H[I] - R);
					}
					V3 D(Q[0] - C[0], Q[1] - C[1], Q[2] - C[2]);
					V3 Local;
					V3 NLocal;
					if (R > 0.f)
					{
						NLocal = Normalize(D);
						Local = V3(C[0], C[1], C[2]) + NLocal * R;
					}
					else
					{
						Local = V3(Q[0], Q[1], Q[2]);
						float NF[3] = {0.f, 0.f, 0.f};
						NF[Axis] = static_cast<float>(Sign);
						NLocal = V3(NF[0], NF[1], NF[2]);
					}
					G.P.push_back(Center + Axes[0] * Local.X + Axes[1] * Local.Y + Axes[2] * Local.Z);
					G.N.push_back(Normalize(Axes[0] * NLocal.X + Axes[1] * NLocal.Y + Axes[2] * NLocal.Z));
				}
			}
			EmitGrid(Out, M, G);
		}
	}
}

std::vector<ProfilePoint> CircleProfile(float Radius, int32_t Sides)
{
	std::vector<ProfilePoint> Out;
	for (int32_t K = 0; K < Sides; ++K)
	{
		const float T = 2.f * Pi * static_cast<float>(K) / static_cast<float>(Sides);
		Out.push_back({Radius * std::cos(T), Radius * std::sin(T), false});
	}
	return Out;
}

std::vector<ProfilePoint> RoundRectProfile(float HalfA, float HalfB, float Radius, int32_t CornerSegments)
{
	std::vector<ProfilePoint> Out;
	const float R = std::min(Radius, std::min(HalfA, HalfB));
	const float CA[4] = {HalfA - R, -(HalfA - R), -(HalfA - R), HalfA - R};
	const float CB[4] = {HalfB - R, HalfB - R, -(HalfB - R), -(HalfB - R)};
	for (int32_t Corner = 0; Corner < 4; ++Corner)
	{
		for (int32_t K = 0; K <= CornerSegments; ++K)
		{
			const float T = Pi * 0.5f * (static_cast<float>(Corner) + static_cast<float>(K) / static_cast<float>(std::max(1, CornerSegments)));
			Out.push_back({CA[Corner] + R * std::cos(T), CB[Corner] + R * std::sin(T), R <= 0.f});
		}
	}
	return Out;
}

std::vector<float> Range(float A, float B, float Step)
{
	const int32_t N = std::max(1, static_cast<int32_t>(std::ceil(std::fabs(B - A) / Step)));
	std::vector<float> Out;
	for (int32_t K = 0; K <= N; ++K)
	{
		Out.push_back(Lerpf(A, B, static_cast<float>(K) / static_cast<float>(N)));
	}
	return Out;
}
} // namespace prim
} // namespace vmodel
