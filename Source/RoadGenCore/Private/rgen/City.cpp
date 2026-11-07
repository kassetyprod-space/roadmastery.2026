// Peregon — RoadGenCore
// Город из OpenStreetMap: чтение .city и построение геометрии (см. rgen/City.h).
#include "rgen/City.h"

#include "rgen/Noise.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <sstream>
#include <utility>

namespace rgen
{
namespace
{
constexpr double kPiC = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------------
// Чтение
// ---------------------------------------------------------------------------------------------
bool ReadDouble(const char*& Cur, const char* End, double& Out)
{
	while (Cur < End && (*Cur == ' ' || *Cur == '\t'))
	{
		++Cur;
	}
	if (Cur >= End)
	{
		return false;
	}
	const auto Res = std::from_chars(Cur, End, Out);
	if (Res.ec != std::errc())
	{
		return false;
	}
	Cur = Res.ptr;
	return true;
}

bool ReadInt(const char*& Cur, const char* End, int& Out)
{
	double V = 0.0;
	if (!ReadDouble(Cur, End, V))
	{
		return false;
	}
	Out = static_cast<int>(V);
	return true;
}

std::string ReadWord(const char*& Cur, const char* End)
{
	while (Cur < End && *Cur == ' ')
	{
		++Cur;
	}
	const char* Start = Cur;
	while (Cur < End && *Cur != ' ' && *Cur != '|')
	{
		++Cur;
	}
	return std::string(Start, Cur);
}

std::vector<CityPt> ReadPoints(const char* Cur, const char* End)
{
	std::vector<CityPt> Pts;
	double X = 0.0;
	double Y = 0.0;
	while (ReadDouble(Cur, End, X) && ReadDouble(Cur, End, Y))
	{
		Pts.push_back(CityPt{X, Y});
	}
	return Pts;
}

// Поля, разделённые «|»: заголовок | название | хвост.
void SplitBars(const std::string& Line, size_t From, std::string& Head, std::string& Name, std::string& Tail)
{
	const size_t A = Line.find('|', From);
	const size_t B = A == std::string::npos ? std::string::npos : Line.find('|', A + 1);
	Head = Line.substr(From, A == std::string::npos ? std::string::npos : A - From);
	Name = (A == std::string::npos) ? std::string() : Line.substr(A + 1, B == std::string::npos ? std::string::npos : B - A - 1);
	Tail = (B == std::string::npos) ? std::string() : Line.substr(B + 1);
}

// ---------------------------------------------------------------------------------------------
// Геометрия
// ---------------------------------------------------------------------------------------------
struct V2
{
	double X = 0.0;
	double Y = 0.0;
};
V2 operator+(V2 A, V2 B) { return V2{A.X + B.X, A.Y + B.Y}; }
V2 operator-(V2 A, V2 B) { return V2{A.X - B.X, A.Y - B.Y}; }
V2 operator*(V2 A, double K) { return V2{A.X * K, A.Y * K}; }
double Len(V2 A) { return std::sqrt(A.X * A.X + A.Y * A.Y); }
V2 Unit(V2 A)
{
	const double L = Len(A);
	return L > 1e-9 ? V2{A.X / L, A.Y / L} : V2{1.0, 0.0};
}
// Вправо от направления (Unreal: X — вперёд, Y — вправо).
V2 RightOf(V2 D) { return V2{-D.Y, D.X}; }
double Cross2(V2 A, V2 B) { return A.X * B.Y - A.Y * B.X; }
double Smooth01(double T)
{
	T = std::clamp(T, 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}
double YawDeg(V2 D) { return std::atan2(D.Y, D.X) * 180.0 / kPiC; }

double SignedArea(const std::vector<CityPt>& R)
{
	double A = 0.0;
	for (size_t I = 0; I < R.size(); ++I)
	{
		const CityPt& P = R[I];
		const CityPt& Q = R[(I + 1) % R.size()];
		A += P.X * Q.Y - Q.X * P.Y;
	}
	return 0.5 * A;
}

// Ушная триангуляция (контур без самопересечений, любой обход). Индексы — в исходный контур.
std::vector<int> Triangulate(std::vector<CityPt> R)
{
	std::vector<int> Out;
	if (R.size() >= 2 && std::fabs(R.front().X - R.back().X) < 1e-6 && std::fabs(R.front().Y - R.back().Y) < 1e-6)
	{
		R.pop_back();
	}
	const int N = static_cast<int>(R.size());
	if (N < 3)
	{
		return Out;
	}
	std::vector<int> Idx(static_cast<size_t>(N));
	for (int I = 0; I < N; ++I)
	{
		Idx[static_cast<size_t>(I)] = I;
	}
	if (SignedArea(R) < 0.0)
	{
		std::reverse(Idx.begin(), Idx.end());
	}
	auto P = [&](int I) { return V2{R[static_cast<size_t>(I)].X, R[static_cast<size_t>(I)].Y}; };
	int Guard = 0;
	while (Idx.size() > 3 && Guard < N * N)
	{
		++Guard;
		bool Clipped = false;
		const size_t M = Idx.size();
		for (size_t K = 0; K < M; ++K)
		{
			const int A = Idx[(K + M - 1) % M];
			const int B = Idx[K];
			const int C = Idx[(K + 1) % M];
			const V2 Pa = P(A), Pb = P(B), Pc = P(C);
			if (Cross2(Pb - Pa, Pc - Pb) <= 1e-9)
			{
				continue; // вогнутая вершина
			}
			bool Empty = true;
			for (size_t Q = 0; Q < M && Empty; ++Q)
			{
				const int D = Idx[Q];
				if (D == A || D == B || D == C)
				{
					continue;
				}
				const V2 Pd = P(D);
				if (Cross2(Pb - Pa, Pd - Pa) >= 0.0 && Cross2(Pc - Pb, Pd - Pb) >= 0.0 && Cross2(Pa - Pc, Pd - Pc) >= 0.0)
				{
					Empty = false;
				}
			}
			if (Empty)
			{
				Out.insert(Out.end(), {A, B, C});
				Idx.erase(Idx.begin() + static_cast<std::ptrdiff_t>(K));
				Clipped = true;
				break;
			}
		}
		if (!Clipped)
		{
			break;
		}
	}
	if (Idx.size() >= 3)
	{
		for (size_t K = 1; K + 1 < Idx.size(); ++K)
		{
			Out.insert(Out.end(), {Idx[0], Idx[K], Idx[K + 1]}); // остаток — веером
		}
	}
	return Out;
}

std::vector<V2> ConvexHull(std::vector<V2> Pts)
{
	std::sort(Pts.begin(), Pts.end(), [](V2 A, V2 B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
	if (Pts.size() < 3)
	{
		return Pts;
	}
	std::vector<V2> H(2 * Pts.size());
	size_t K = 0;
	for (size_t I = 0; I < Pts.size(); ++I)
	{
		while (K >= 2 && Cross2(H[K - 1] - H[K - 2], Pts[I] - H[K - 2]) <= 0.0)
		{
			--K;
		}
		H[K++] = Pts[I];
	}
	for (size_t I = Pts.size() - 1, T = K + 1; I > 0; --I)
	{
		while (K >= T && Cross2(H[K - 1] - H[K - 2], Pts[I - 1] - H[K - 2]) <= 0.0)
		{
			--K;
		}
		H[K++] = Pts[I - 1];
	}
	H.resize(K - 1);
	return H;
}

uint32_t Rgba(double R, double G, double B)
{
	auto C = [](double V) { return static_cast<uint32_t>(std::clamp(V, 0.0, 1.0) * 255.0 + 0.5); };
	return C(R) | (C(G) << 8) | (C(B) << 16) | (255u << 24);
}

// ---------------------------------------------------------------------------------------------
// Полилиния с высотами (ось улицы)
// ---------------------------------------------------------------------------------------------
struct Axis
{
	std::vector<V2> P;
	std::vector<double> Z;
	std::vector<double> S; // накопленная длина

	void Finish()
	{
		S.assign(P.size(), 0.0);
		for (size_t I = 1; I < P.size(); ++I)
		{
			S[I] = S[I - 1] + Len(P[I] - P[I - 1]);
		}
	}
	double Length() const { return S.empty() ? 0.0 : S.back(); }
	size_t SegAt(double D) const
	{
		const auto It = std::upper_bound(S.begin(), S.end(), D);
		size_t I = static_cast<size_t>(std::max<std::ptrdiff_t>(1, It - S.begin()));
		return std::min(I, P.size() - 1) - 1;
	}
	V2 At(double D) const
	{
		const size_t I = SegAt(D);
		const double L = std::max(1e-9, S[I + 1] - S[I]);
		const double T = std::clamp((D - S[I]) / L, 0.0, 1.0);
		return P[I] + (P[I + 1] - P[I]) * T;
	}
	double ZAt(double D) const
	{
		const size_t I = SegAt(D);
		const double L = std::max(1e-9, S[I + 1] - S[I]);
		const double T = std::clamp((D - S[I]) / L, 0.0, 1.0);
		return Z[I] + (Z[I + 1] - Z[I]) * T;
	}
	V2 Dir(double D) const
	{
		const size_t I = SegAt(D);
		return Unit(P[I + 1] - P[I]);
	}
	// Вправо в точке D; на изломах — биссектриса со «скосом» (не больше ×2).
	V2 RightAt(double D, double& Miter) const
	{
		Miter = 1.0;
		const size_t I = SegAt(D);
		const V2 R0 = RightOf(Unit(P[I + 1] - P[I]));
		const double Eps = 1e-4;
		if (I + 2 < P.size() && std::fabs(D - S[I + 1]) < Eps)
		{
			const V2 R1 = RightOf(Unit(P[I + 2] - P[I + 1]));
			const V2 B = Unit(R0 + R1);
			const double C = R0.X * B.X + R0.Y * B.Y;
			Miter = std::min(2.0, 1.0 / std::max(0.5, C));
			return B;
		}
		if (I > 0 && std::fabs(D - S[I]) < Eps)
		{
			const V2 Rp = RightOf(Unit(P[I] - P[I - 1]));
			const V2 B = Unit(R0 + Rp);
			const double C = R0.X * B.X + R0.Y * B.Y;
			Miter = std::min(2.0, 1.0 / std::max(0.5, C));
			return B;
		}
		return R0;
	}
	// Точки разбиения [A, B]: концы, вершины, не реже чем через MaxStep.
	std::vector<double> Stations(double A, double B, double MaxStep) const
	{
		std::vector<double> Out;
		if (B <= A)
		{
			return Out;
		}
		Out.push_back(A);
		for (size_t I = 1; I + 1 < S.size(); ++I)
		{
			if (S[I] > A + 1e-3 && S[I] < B - 1e-3)
			{
				Out.push_back(S[I]);
			}
		}
		Out.push_back(B);
		std::vector<double> Dense;
		for (size_t I = 0; I + 1 < Out.size(); ++I)
		{
			const int N = std::max(1, static_cast<int>(std::ceil((Out[I + 1] - Out[I]) / MaxStep)));
			for (int K = 0; K < N; ++K)
			{
				Dense.push_back(Out[I] + (Out[I + 1] - Out[I]) * K / N);
			}
		}
		Dense.push_back(B);
		return Dense;
	}
};

struct Edge
{
	int Way = -1;
	std::vector<int> Nodes;
	CityRoadClass Class = CityRoadClass::Residential;
	double HalfW = 3.0;
	double LaneW = 3.0;
	int Lanes = 2;
	bool OneWay = false;
	bool Ring = false;
	bool Bridge = false;
	bool OverWater = false;
	int Speed = 0;
	std::string Style;
	std::string Name;
	Axis Ax;
	double Trim0 = 0.0;
	double Trim1 = 0.0;
	double WaterS0 = 0.0, WaterS1 = 0.0; // где под мостом вода
};

// ---------------------------------------------------------------------------------------------
// Построитель
// ---------------------------------------------------------------------------------------------
class Builder
{
public:
	Builder(const CityData& InData, const CityBuildOptions& InOpt, CityResult& InOut) : D(InData), O(InOpt), R(InOut) {}

	void Run()
	{
		Bounds();
		BuildWaterMask();
		BuildEdges();
		SolveHeights();
		for (Edge& E : Edges)
		{
			FinishAxis(E);
		}
		TrimAtJunctions();
		for (const Edge& E : Edges)
		{
			RoadSurface(E);
			Markings(E);
			Sides(E);
			if (E.Bridge)
			{
				BridgeStructure(E);
				++R.Bridges;
			}
			RoadProps(E);
		}
		Junctions();
		RingIslands();
		Buildings();
		Areas();
		Ground();
		for (const CityLandmark& L : D.Landmarks)
		{
			CityProp P;
			P.Kind = CityPropKind::Landmark;
			P.X = L.X;
			P.Y = L.Y;
			P.Z = 0.0;
			P.Yaw = L.Yaw * 180.0 / kPiC;
			P.Value = static_cast<int>(L.Radius);
			P.Text = L.Key;
			R.Props.push_back(P);
		}
		R.Edges = static_cast<int>(Edges.size());
		R.NodeHeight = H;
	}

private:
	const CityData& D;
	const CityBuildOptions& O;
	CityResult& R;
	std::vector<Edge> Edges;
	std::vector<double> H;                 // высота узла
	std::vector<std::vector<int>> NodeEdges; // рёбра, которые начинаются/кончаются в узле
	std::vector<double> JunctionR;
	std::map<std::pair<int, int>, size_t> TileIndex;

	// Маска воды на сетке.
	double Gx0 = 0.0, Gy0 = 0.0, Step = 16.0;
	int Gnx = 0, Gny = 0;
	std::vector<uint8_t> Wet;
	std::vector<float> WetDist;

	CitySection& Sec(double X, double Y, CityMat M)
	{
		const int Tx = static_cast<int>(std::floor(X / O.TileSize));
		const int Ty = static_cast<int>(std::floor(Y / O.TileSize));
		const auto Key = std::make_pair(Tx, Ty);
		auto It = TileIndex.find(Key);
		if (It == TileIndex.end())
		{
			It = TileIndex.emplace(Key, R.Tiles.size()).first;
			R.Tiles.emplace_back();
			R.Tiles.back().TX = Tx;
			R.Tiles.back().TY = Ty;
		}
		return R.Tiles[It->second].Sections[static_cast<int>(M)];
	}

	void Bounds()
	{
		double X0 = 1e18, Y0 = 1e18, X1 = -1e18, Y1 = -1e18;
		auto Add = [&](double X, double Y) {
			X0 = std::min(X0, X);
			Y0 = std::min(Y0, Y);
			X1 = std::max(X1, X);
			Y1 = std::max(Y1, Y);
		};
		for (const CityNode& N : D.Nodes)
		{
			Add(N.X, N.Y);
		}
		for (const CityBuilding& B : D.Buildings)
		{
			for (const CityPt& P : B.Ring)
			{
				Add(P.X, P.Y);
			}
		}
		if (X0 > X1)
		{
			X0 = Y0 = -100.0;
			X1 = Y1 = 100.0;
		}
		const double M = 250.0;
		R.MinX = X0 - M;
		R.MinY = Y0 - M;
		R.MaxX = X1 + M;
		R.MaxY = Y1 + M;
	}

	// ---- Вода: заливка строк сетки и расстояние до берега ----
	void BuildWaterMask()
	{
		Step = std::max(4.0, O.GroundStep);
		Gx0 = R.MinX;
		Gy0 = R.MinY;
		Gnx = static_cast<int>(std::ceil((R.MaxX - R.MinX) / Step)) + 1;
		Gny = static_cast<int>(std::ceil((R.MaxY - R.MinY) / Step)) + 1;
		Wet.assign(static_cast<size_t>(Gnx) * static_cast<size_t>(Gny), 0);
		auto Fill = [&](const std::vector<CityPt>& Ring, uint8_t Value) {
			if (Ring.size() < 3)
			{
				return;
			}
			std::vector<double> Cuts;
			for (int I = 0; I < Gnx; ++I)
			{
				const double X = Gx0 + I * Step;
				Cuts.clear();
				for (size_t K = 0, J = Ring.size() - 1; K < Ring.size(); J = K++)
				{
					const CityPt& A = Ring[K];
					const CityPt& B = Ring[J];
					if ((A.X > X) != (B.X > X))
					{
						Cuts.push_back(A.Y + (X - A.X) * (B.Y - A.Y) / (B.X - A.X));
					}
				}
				std::sort(Cuts.begin(), Cuts.end());
				for (size_t K = 0; K + 1 < Cuts.size(); K += 2)
				{
					const int J0 = std::max(0, static_cast<int>(std::ceil((Cuts[K] - Gy0) / Step)));
					const int J1 = std::min(Gny - 1, static_cast<int>(std::floor((Cuts[K + 1] - Gy0) / Step)));
					for (int J = J0; J <= J1; ++J)
					{
						Wet[static_cast<size_t>(I) * static_cast<size_t>(Gny) + static_cast<size_t>(J)] = Value;
					}
				}
			}
		};
		for (const auto& W : D.Water)
		{
			Fill(W, 1);
		}
		for (const auto& Isl : D.Islands)
		{
			Fill(Isl, 0);
		}
		// Расстояние (в метрах) от мокрой точки до ближайшей сухой — двухпроходная «фаска».
		const float Big = 1e9f;
		WetDist.assign(Wet.size(), 0.f);
		for (size_t K = 0; K < Wet.size(); ++K)
		{
			WetDist[K] = Wet[K] ? Big : 0.f;
		}
		const float S1 = static_cast<float>(Step);
		const float S2 = static_cast<float>(Step * 1.41421356);
		auto At = [&](int I, int J) -> float& { return WetDist[static_cast<size_t>(I) * static_cast<size_t>(Gny) + static_cast<size_t>(J)]; };
		for (int I = 0; I < Gnx; ++I)
		{
			for (int J = 0; J < Gny; ++J)
			{
				float& V = At(I, J);
				if (I > 0) V = std::min(V, At(I - 1, J) + S1);
				if (J > 0) V = std::min(V, At(I, J - 1) + S1);
				if (I > 0 && J > 0) V = std::min(V, At(I - 1, J - 1) + S2);
				if (I > 0 && J + 1 < Gny) V = std::min(V, At(I - 1, J + 1) + S2);
			}
		}
		for (int I = Gnx - 1; I >= 0; --I)
		{
			for (int J = Gny - 1; J >= 0; --J)
			{
				float& V = At(I, J);
				if (I + 1 < Gnx) V = std::min(V, At(I + 1, J) + S1);
				if (J + 1 < Gny) V = std::min(V, At(I, J + 1) + S1);
				if (I + 1 < Gnx && J + 1 < Gny) V = std::min(V, At(I + 1, J + 1) + S2);
				if (I + 1 < Gnx && J > 0) V = std::min(V, At(I + 1, J - 1) + S2);
			}
		}
	}

	bool IsWet(double X, double Y) const
	{
		const int I = static_cast<int>(std::lround((X - Gx0) / Step));
		const int J = static_cast<int>(std::lround((Y - Gy0) / Step));
		if (I < 0 || J < 0 || I >= Gnx || J >= Gny)
		{
			return false;
		}
		return Wet[static_cast<size_t>(I) * static_cast<size_t>(Gny) + static_cast<size_t>(J)] != 0;
	}

	double GroundZ(int I, int J) const
	{
		const size_t K = static_cast<size_t>(I) * static_cast<size_t>(Gny) + static_cast<size_t>(J);
		if (!Wet[K])
		{
			return 0.0;
		}
		return -O.WaterDepth * Smooth01(static_cast<double>(WetDist[K]) / 40.0);
	}

	// ---- Улицы: рёбра между перекрёстками ----
	void BuildEdges()
	{
		std::vector<int> Refs(D.Nodes.size(), 0);
		for (const CityWay& W : D.Ways)
		{
			for (size_t K = 0; K < W.Nodes.size(); ++K)
			{
				const int N = W.Nodes[K];
				if (N >= 0 && static_cast<size_t>(N) < Refs.size())
				{
					Refs[static_cast<size_t>(N)] += (K == 0 || K + 1 == W.Nodes.size()) ? 1 : 2;
				}
			}
		}
		for (size_t Wi = 0; Wi < D.Ways.size(); ++Wi)
		{
			const CityWay& W = D.Ways[Wi];
			std::vector<int> Valid;
			for (int N : W.Nodes)
			{
				if (N >= 0 && static_cast<size_t>(N) < D.Nodes.size() && (Valid.empty() || Valid.back() != N))
				{
					Valid.push_back(N);
				}
			}
			if (Valid.size() < 2)
			{
				continue;
			}
			std::vector<int> Cur{Valid[0]};
			for (size_t K = 1; K < Valid.size(); ++K)
			{
				Cur.push_back(Valid[K]);
				const bool Split = K + 1 < Valid.size() && Refs[static_cast<size_t>(Valid[K])] > 2;
				if (Split || K + 1 == Valid.size())
				{
					Edge E;
					E.Way = static_cast<int>(Wi);
					E.Nodes = Cur;
					E.Class = W.Class;
					E.Lanes = std::max(1, W.Lanes);
					E.OneWay = W.OneWay;
					E.Ring = W.Ring;
					E.Bridge = W.Bridge;
					E.Speed = W.Speed;
					E.Style = W.Style;
					E.Name = W.Name;
					E.LaneW = CityLaneWidth(W.Class);
					E.HalfW = 0.5 * E.Lanes * E.LaneW;
					Edges.push_back(E);
					Cur.assign(1, Valid[K]);
				}
			}
		}
		NodeEdges.assign(D.Nodes.size(), {});
		for (size_t K = 0; K < Edges.size(); ++K)
		{
			NodeEdges[static_cast<size_t>(Edges[K].Nodes.front())].push_back(static_cast<int>(K));
			NodeEdges[static_cast<size_t>(Edges[K].Nodes.back())].push_back(static_cast<int>(K));
		}
	}

	V2 NodeP(int N) const { return V2{D.Nodes[static_cast<size_t>(N)].X, D.Nodes[static_cast<size_t>(N)].Y}; }

	// ---- Высоты: путепроводы подняты, к ним — пандусы с уклоном не круче 5 % ----
	void SolveHeights()
	{
		H.assign(D.Nodes.size(), 0.0);
		std::vector<uint8_t> Fixed(D.Nodes.size(), 0);
		for (Edge& E : Edges)
		{
			if (!E.Bridge)
			{
				continue;
			}
			// Над водой мост идёт вровень с городом: русло ниже на WaterDepth.
			double WetLen = 0.0, Total = 0.0;
			for (size_t K = 0; K + 1 < E.Nodes.size(); ++K)
			{
				const V2 A = NodeP(E.Nodes[K]);
				const V2 B = NodeP(E.Nodes[K + 1]);
				const double L = Len(B - A);
				const int N = std::max(1, static_cast<int>(L / 8.0));
				for (int I = 0; I < N; ++I)
				{
					const V2 P = A + (B - A) * ((I + 0.5) / N);
					WetLen += IsWet(P.X, P.Y) ? L / N : 0.0;
					Total += L / N;
				}
			}
			E.OverWater = Total > 0.0 && WetLen > 0.25 * Total;
			// Через реку мост поднят над водой (судоходный пролёт), путепровод — на 6.5 м за уровень.
			const int Layer = std::max(1, D.Ways[static_cast<size_t>(E.Way)].Layer);
			const double Lift = E.OverWater ? O.RiverBridgeLift : 6.5 * Layer;
			for (int N : E.Nodes)
			{
				H[static_cast<size_t>(N)] = std::max(H[static_cast<size_t>(N)], Lift);
				Fixed[static_cast<size_t>(N)] = 1;
			}
		}
		// Распространение «сверху вниз»: соседний узел не ниже, чем (высота − 5 % расстояния).
		using Item = std::pair<double, int>;
		std::priority_queue<Item> Q;
		for (size_t N = 0; N < H.size(); ++N)
		{
			if (H[N] > 0.0)
			{
				Q.push({H[N], static_cast<int>(N)});
			}
		}
		std::vector<std::vector<std::pair<int, double>>> Adj(D.Nodes.size());
		for (const Edge& E : Edges)
		{
			for (size_t K = 0; K + 1 < E.Nodes.size(); ++K)
			{
				const double L = Len(NodeP(E.Nodes[K + 1]) - NodeP(E.Nodes[K]));
				Adj[static_cast<size_t>(E.Nodes[K])].push_back({E.Nodes[K + 1], L});
				Adj[static_cast<size_t>(E.Nodes[K + 1])].push_back({E.Nodes[K], L});
			}
		}
		while (!Q.empty())
		{
			const auto [Hv, N] = Q.top();
			Q.pop();
			if (Hv < H[static_cast<size_t>(N)] - 1e-9)
			{
				continue;
			}
			for (const auto& [M, L] : Adj[static_cast<size_t>(N)])
			{
				const double Want = Hv - 0.05 * L;
				if (!Fixed[static_cast<size_t>(M)] && Want > H[static_cast<size_t>(M)] + 1e-6)
				{
					H[static_cast<size_t>(M)] = Want;
					Q.push({Want, M});
				}
			}
		}
	}

	void FinishAxis(Edge& E)
	{
		for (int N : E.Nodes)
		{
			E.Ax.P.push_back(NodeP(N));
			E.Ax.Z.push_back(H[static_cast<size_t>(N)] + 0.05);
		}
		E.Ax.Finish();
		if (E.Bridge && E.OverWater)
		{
			E.WaterS0 = E.Ax.Length();
			E.WaterS1 = 0.0;
			for (double S = 0.0; S <= E.Ax.Length(); S += 4.0)
			{
				const V2 P = E.Ax.At(S);
				if (IsWet(P.X, P.Y))
				{
					E.WaterS0 = std::min(E.WaterS0, S);
					E.WaterS1 = std::max(E.WaterS1, S);
				}
			}
		}
	}

	// ---- Перекрёстки: подрезка улиц ----
	void TrimAtJunctions()
	{
		JunctionR.assign(D.Nodes.size(), 0.0);
		for (size_t N = 0; N < NodeEdges.size(); ++N)
		{
			if (NodeEdges[N].size() < 3)
			{
				continue;
			}
			double Rad = 0.0;
			for (int K : NodeEdges[N])
			{
				Rad = std::max(Rad, Edges[static_cast<size_t>(K)].HalfW);
			}
			JunctionR[N] = Rad + ((D.Nodes[N].Flags & CityFlag::Signals) ? 3.0 : 1.5);
			++R.Junctions;
		}
		for (Edge& E : Edges)
		{
			const double L = E.Ax.Length();
			E.Trim0 = std::min(JunctionR[static_cast<size_t>(E.Nodes.front())], L * 0.45);
			E.Trim1 = std::min(JunctionR[static_cast<size_t>(E.Nodes.back())], L * 0.45);
			// Стык двух улиц без перекрёстка — продлеваем немного, чтобы не было щели.
			if (E.Trim0 == 0.0)
			{
				E.Trim0 = -0.3;
			}
			if (E.Trim1 == 0.0)
			{
				E.Trim1 = -0.3;
			}
		}
	}

	// Полоса вдоль оси: от T0 до T1 (вправо от оси), на высоте оси + Lift, лицом вверх.
	void Ribbon(const Edge& E, double S0, double S1, double T0, double T1, double Lift, CityMat M, double UScale, double VScale,
		uint32_t Color = 0xFFFFFFFFu, double MaxStep = 6.0, bool bFaceDown = false)
	{
		const double Nz = bFaceDown ? -1.0 : 1.0;
		const double A = std::max(-0.3, S0);
		const double B = std::min(E.Ax.Length() + 0.3, S1);
		if (B - A < 0.05)
		{
			return;
		}
		const std::vector<double> St = E.Ax.Stations(std::max(0.0, A), std::min(E.Ax.Length(), B), MaxStep);
		if (St.size() < 2)
		{
			return;
		}
		// Порции по ~60 м — каждая в свой тайл по середине.
		size_t From = 0;
		while (From + 1 < St.size())
		{
			size_t To = From + 1;
			while (To + 1 < St.size() && St[To] - St[From] < 60.0)
			{
				++To;
			}
			const V2 Mid = E.Ax.At(0.5 * (St[From] + St[To]));
			CitySection& Sc = Sec(Mid.X, Mid.Y, M);
			int32_t PrevL = -1, PrevR = -1;
			for (size_t K = From; K <= To; ++K)
			{
				double S = St[K];
				// Продление за концы (стыки без перекрёстка).
				double Ext = 0.0;
				if (K == 0 && A < 0.0)
				{
					Ext = A;
				}
				if (K + 1 == St.size() && B > E.Ax.Length())
				{
					Ext = B - E.Ax.Length();
				}
				double Miter = 1.0;
				const V2 Rt = E.Ax.RightAt(S, Miter);
				const V2 C = E.Ax.At(S) + E.Ax.Dir(S) * Ext;
				const double Z = E.Ax.ZAt(S) + Lift;
				const V2 Pl = C + Rt * (T0 * Miter);
				const V2 Pr = C + Rt * (T1 * Miter);
				const int32_t Il = Sc.Vertex(Pl.X, Pl.Y, Z, 0, 0, Nz, T0 * UScale, (S + Ext) * VScale, Color);
				const int32_t Ir = Sc.Vertex(Pr.X, Pr.Y, Z, 0, 0, Nz, T1 * UScale, (S + Ext) * VScale, Color);
				if (PrevL >= 0)
				{
					Sc.Tri(PrevL, PrevR, Ir, 0, 0, Nz);
					Sc.Tri(PrevL, Ir, Il, 0, 0, Nz);
				}
				PrevL = Il;
				PrevR = Ir;
				(void)S;
			}
			From = To;
		}
	}

	// Вертикальная стенка вдоль оси на смещении T: от Zb до Zt (относительно оси, если bRelative), лицом вправо/влево.
	void Wall(const Edge& E, double S0, double S1, double T, double Zb, double Zt, bool bFaceRight, CityMat M, bool bBottomAbsolute,
		uint32_t Color = 0xFFFFFFFFu)
	{
		const double A = std::max(0.0, S0);
		const double B = std::min(E.Ax.Length(), S1);
		if (B - A < 0.05)
		{
			return;
		}
		const std::vector<double> St = E.Ax.Stations(A, B, 8.0);
		const V2 Mid = E.Ax.At(0.5 * (A + B));
		CitySection& Sc = Sec(Mid.X, Mid.Y, M);
		int32_t Pb = -1, Pt = -1;
		for (double S : St)
		{
			double Miter = 1.0;
			const V2 Rt = E.Ax.RightAt(S, Miter);
			const V2 P = E.Ax.At(S) + Rt * (T * Miter);
			const double Z = E.Ax.ZAt(S);
			const double Z0 = bBottomAbsolute ? Zb : Z + Zb;
			const double Z1 = Z + Zt;
			const V2 N = bFaceRight ? Rt : Rt * -1.0;
			const int32_t Ib = Sc.Vertex(P.X, P.Y, Z0, N.X, N.Y, 0, S * 0.25, (Z1 - Z0) * 0.25, Color);
			const int32_t It = Sc.Vertex(P.X, P.Y, Z1, N.X, N.Y, 0, S * 0.25, 0.0, Color);
			if (Pb >= 0)
			{
				Sc.Tri(Pb, Ib, It, N.X, N.Y, 0);
				Sc.Tri(Pb, It, Pt, N.X, N.Y, 0);
			}
			Pb = Ib;
			Pt = It;
		}
	}

	// Коробка в мире: центр основания, оси (Fwd — горизонтальная), размеры.
	void Box(CitySection& Sc, V2 C, double Z0, V2 Fwd, double Lx, double Ly, double Hz, uint32_t Color = 0xFFFFFFFFu)
	{
		const V2 F = Unit(Fwd);
		const V2 Rt = RightOf(F);
		V2 Q[4] = {C - F * (Lx * 0.5) - Rt * (Ly * 0.5), C + F * (Lx * 0.5) - Rt * (Ly * 0.5), C + F * (Lx * 0.5) + Rt * (Ly * 0.5),
			C - F * (Lx * 0.5) + Rt * (Ly * 0.5)};
		for (int K = 0; K < 4; ++K)
		{
			const V2 A = Q[K];
			const V2 B = Q[(K + 1) % 4];
			const V2 N = RightOf(Unit(B - A)) * -1.0;
			const int32_t I0 = Sc.Vertex(A.X, A.Y, Z0, N.X, N.Y, 0, 0, 1, Color);
			const int32_t I1 = Sc.Vertex(B.X, B.Y, Z0, N.X, N.Y, 0, Len(B - A) * 0.25, 1, Color);
			const int32_t I2 = Sc.Vertex(B.X, B.Y, Z0 + Hz, N.X, N.Y, 0, Len(B - A) * 0.25, 0, Color);
			const int32_t I3 = Sc.Vertex(A.X, A.Y, Z0 + Hz, N.X, N.Y, 0, 0, 0, Color);
			Sc.Tri(I0, I1, I2, N.X, N.Y, 0);
			Sc.Tri(I0, I2, I3, N.X, N.Y, 0);
		}
		const int32_t T0 = Sc.Vertex(Q[0].X, Q[0].Y, Z0 + Hz, 0, 0, 1, 0, 0, Color);
		const int32_t T1 = Sc.Vertex(Q[1].X, Q[1].Y, Z0 + Hz, 0, 0, 1, 1, 0, Color);
		const int32_t T2 = Sc.Vertex(Q[2].X, Q[2].Y, Z0 + Hz, 0, 0, 1, 1, 1, Color);
		const int32_t T3 = Sc.Vertex(Q[3].X, Q[3].Y, Z0 + Hz, 0, 0, 1, 0, 1, Color);
		Sc.Tri(T0, T1, T2, 0, 0, 1);
		Sc.Tri(T0, T2, T3, 0, 0, 1);
	}

	// ---- Полотно ----
	void RoadSurface(const Edge& E)
	{
		Ribbon(E, E.Trim0, E.Ax.Length() - E.Trim1, -E.HalfW, E.HalfW, 0.0, CityMat::Asphalt, 1.0 / 3.5, 0.25);
	}

	// Штрихи вдоль оси на смещении T: штрих Dash, разрыв Gap (Gap = 0 — сплошная).
	void Dashes(const Edge& E, double S0, double S1, double T, double W, double Dash, double Gap)
	{
		if (S1 - S0 < 0.3)
		{
			return;
		}
		if (Gap <= 0.0)
		{
			Ribbon(E, S0, S1, T - W * 0.5, T + W * 0.5, 0.015, CityMat::Marking, 0.5, 0.5, 0xFFFFFFFFu, 4.0);
			return;
		}
		const double Period = Dash + Gap;
		for (double S = std::ceil(S0 / Period) * Period; S < S1; S += Period)
		{
			Ribbon(E, std::max(S0, S), std::min(S1, S + Dash), T - W * 0.5, T + W * 0.5, 0.015, CityMat::Marking, 0.5, 0.5, 0xFFFFFFFFu, 4.0);
		}
	}

	void Zebra(const Edge& E, double Sc, double T0, double T1)
	{
		for (double T = T0 + 0.3; T < T1 - 0.2; T += 1.0)
		{
			Ribbon(E, Sc - 2.0, Sc + 2.0, T, std::min(T + 0.5, T1), 0.015, CityMat::Marking, 0.5, 0.5);
		}
	}

	int Degree(int N) const { return static_cast<int>(NodeEdges[static_cast<size_t>(N)].size()); }

	void Markings(const Edge& E)
	{
		const double S0 = std::max(0.0, E.Trim0);
		const double S1 = E.Ax.Length() - std::max(0.0, E.Trim1);
		if (S1 - S0 < 1.0)
		{
			return;
		}
		const double W = E.HalfW;
		const double Lw = E.LaneW;
		const bool Big = E.Class <= CityRoadClass::Primary;
		const double Approach = 20.0;
		const bool JStart = Degree(E.Nodes.front()) >= 3;
		const bool JEnd = Degree(E.Nodes.back()) >= 3;
		if (E.OneWay || E.Ring)
		{
			for (int K = 1; K < E.Lanes; ++K)
			{
				const double T = -W + K * Lw;
				Dashes(E, S0, JEnd ? S1 - Approach : S1, T, 0.12, 3.0, 6.0);
				if (JEnd)
				{
					Dashes(E, S1 - Approach, S1, T, 0.12, 0.0, 0.0);
				}
			}
			if (Big || E.Ring)
			{
				Dashes(E, S0, S1, -W + 0.3, 0.15, 0.0, 0.0);
				Dashes(E, S0, S1, W - 0.3, 0.15, 0.0, 0.0);
			}
			if (JEnd && !E.Ring)
			{
				Ribbon(E, S1 - 1.6, S1 - 1.2, -W + 0.2, W - 0.2, 0.015, CityMat::Marking, 0.5, 0.5); // стоп-линия
			}
		}
		else
		{
			const int PerDir = std::max(1, E.Lanes / 2);
			if (E.Lanes >= 4)
			{
				Dashes(E, S0, S1, -0.15, 0.12, 0.0, 0.0); // двойная сплошная
				Dashes(E, S0, S1, 0.15, 0.12, 0.0, 0.0);
			}
			else
			{
				const double A = JStart ? S0 + Approach : S0;
				const double B = JEnd ? S1 - Approach : S1;
				Dashes(E, A, B, 0.0, 0.12, 3.0, 6.0);
				if (JStart)
				{
					Dashes(E, S0, std::min(S1, S0 + Approach), 0.0, 0.12, 0.0, 0.0);
				}
				if (JEnd)
				{
					Dashes(E, std::max(S0, S1 - Approach), S1, 0.0, 0.12, 0.0, 0.0);
				}
			}
			for (int K = 1; K < PerDir; ++K)
			{
				const double T = K * Lw;
				// Правые полосы едут вдоль оси: сплошная — перед концом; левые — навстречу: перед началом.
				Dashes(E, S0, JEnd ? S1 - Approach : S1, T, 0.12, 3.0, 6.0);
				Dashes(E, JStart ? S0 + Approach : S0, S1, -T, 0.12, 3.0, 6.0);
				if (JEnd)
				{
					Dashes(E, std::max(S0, S1 - Approach), S1, T, 0.12, 0.0, 0.0);
				}
				if (JStart)
				{
					Dashes(E, S0, std::min(S1, S0 + Approach), -T, 0.12, 0.0, 0.0);
				}
			}
			if (Big)
			{
				Dashes(E, S0, S1, -W + 0.3, 0.15, 0.0, 0.0);
				Dashes(E, S0, S1, W - 0.3, 0.15, 0.0, 0.0);
			}
			if (JEnd)
			{
				Ribbon(E, S1 - 1.6, S1 - 1.2, 0.2, W - 0.2, 0.015, CityMat::Marking, 0.5, 0.5);
			}
			if (JStart)
			{
				Ribbon(E, S0 + 1.2, S0 + 1.6, -W + 0.2, -0.2, 0.015, CityMat::Marking, 0.5, 0.5);
			}
		}
		// «Зебры»: у регулируемых перекрёстков и на отмеченных переходах.
		const bool SigStart = (D.Nodes[static_cast<size_t>(E.Nodes.front())].Flags & CityFlag::Signals) && JStart;
		const bool SigEnd = (D.Nodes[static_cast<size_t>(E.Nodes.back())].Flags & CityFlag::Signals) && JEnd;
		if (SigStart && S1 - S0 > 12.0)
		{
			Zebra(E, S0 + 4.5, -W, W);
		}
		if (SigEnd && S1 - S0 > 12.0)
		{
			Zebra(E, S1 - 4.5, -W, W);
		}
		for (size_t K = 1; K + 1 < E.Nodes.size(); ++K)
		{
			const int N = E.Nodes[K];
			if (D.Nodes[static_cast<size_t>(N)].Flags & (CityFlag::Crossing | CityFlag::Signals))
			{
				const double S = E.Ax.S[K];
				if (S > S0 + 3.0 && S < S1 - 3.0)
				{
					Zebra(E, S, -W, W);
				}
			}
		}
	}

	// ---- Тротуары, бордюры, подпорные стенки ----
	void Sides(const Edge& E)
	{
		const double S0 = std::max(0.0, E.Trim0);
		const double S1 = E.Ax.Length() - std::max(0.0, E.Trim1);
		const double W = E.HalfW;
		const bool Fast = E.Class <= CityRoadClass::Trunk || E.Class == CityRoadClass::Link;
		const double MaxZ = *std::max_element(E.Ax.Z.begin(), E.Ax.Z.end());
		if (E.Bridge)
		{
			// Узкий служебный тротуар и перила на мосту/путепроводе.
			for (double Side : {-1.0, 1.0})
			{
				const double Inner = Side * W;
				const double Outer = Side * (W + 1.6);
				Ribbon(E, S0, S1, std::min(Inner, Outer), std::max(Inner, Outer), 0.2, CityMat::Sidewalk, 0.5, 0.5);
				Wall(E, S0, S1, Inner, 0.0, 0.2, Side < 0.0, CityMat::Curb, false);
				for (bool Face : {true, false})
				{
					Wall(E, S0, S1, Outer, 0.2, 1.25, Face, CityMat::Steel, false);
				}
			}
			return;
		}
		if (MaxZ > 0.6)
		{
			// Пандус к путепроводу: подпорные стенки до земли и отбойники.
			for (double Side : {-1.0, 1.0})
			{
				Wall(E, S0, S1, Side * (W + 0.3), 0.0, 0.0, Side > 0.0, CityMat::Concrete, true);
				Wall(E, S0, S1, Side * (W + 0.3), 0.0, 0.9, Side > 0.0, CityMat::Concrete, false);
				Wall(E, S0, S1, Side * (W + 0.1), 0.0, 0.9, Side < 0.0, CityMat::Concrete, false);
			}
			return;
		}
		if (Fast)
		{
			return;
		}
		const double Sw = CitySidewalkWidth(E.Class);
		for (double Side : {-1.0, 1.0})
		{
			const double Inner = Side * W;
			const double Outer = Side * (W + Sw);
			Ribbon(E, S0, S1, std::min(Inner, Outer), std::max(Inner, Outer), 0.15, CityMat::Sidewalk, 0.5, 0.5);
			Wall(E, S0, S1, Inner, 0.0, 0.15, Side < 0.0, CityMat::Curb, false);
			Wall(E, S0, S1, Outer, -0.2, 0.15, Side > 0.0, CityMat::Curb, false);
		}
	}

	// ---- Мосты и путепроводы ----
	void BridgeStructure(const Edge& E)
	{
		const double W = E.HalfW + 1.6;
		const double L = E.Ax.Length();
		const double Deck = 1.4;
		// Низ пролётного строения и фасадные балки.
		Ribbon(E, 0.0, L, -W, W, -Deck, CityMat::Concrete, 0.25, 0.25, 0xFFFFFFFFu, 6.0, true);
		for (double Side : {-1.0, 1.0})
		{
			Wall(E, 0.0, L, Side * W, -Deck, 0.2, Side > 0.0, CityMat::Concrete, false);
		}
		const bool Arches = E.Style == "oktyabrsky";
		double A = 0.0;
		double B = L;
		double Spacing = 32.0;
		double BedZ = 0.0;
		if (E.OverWater)
		{
			A = std::max(0.0, E.WaterS0 - 20.0);
			B = std::min(L, E.WaterS1 + 20.0);
			Spacing = Arches ? 128.0 : 64.0;
			BedZ = -O.WaterDepth;
		}
		const int Spans = std::max(1, static_cast<int>(std::lround((B - A) / Spacing)));
		const double Span = (B - A) / Spans;
		const double Rise = Arches ? 6.0 : 0.0;
		for (int K = 0; K <= Spans; ++K)
		{
			const double S = A + K * Span;
			const V2 C = E.Ax.At(S);
			const V2 F = E.Ax.Dir(S);
			const double Top = E.Ax.ZAt(S) - Deck - Rise;
			if (Top - BedZ < 0.5)
			{
				continue;
			}
			// Опора: столбы под каждой половиной, ригель сверху; в воде — массивная, с ледорезом.
			CitySection& Sc = Sec(C.X, C.Y, CityMat::Concrete);
			if (E.OverWater)
			{
				Box(Sc, C, BedZ - 1.0, F, 5.0, 2.0 * W - 2.0, Top - BedZ + 1.0);
				Box(Sc, C + F * 3.0, BedZ - 1.0, F, 2.0, 4.0, 3.5);
			}
			else
			{
				for (double Side : {-0.5, 0.5})
				{
					Box(Sc, C + RightOf(F) * (Side * W), BedZ, F, 1.2, 1.2, Top - BedZ);
				}
				Box(Sc, C, Top - 1.0, F, 1.6, 2.0 * W - 1.0, 1.0);
			}
		}
		if (Arches)
		{
			// Арочно-балочные пролёты: две стальные арки под краями настила, стойки к настилу.
			for (int K = 0; K < Spans; ++K)
			{
				const double Sa = A + K * Span;
				for (double Side : {-1.0, 1.0})
				{
					const double T = Side * (W - 2.5);
					const int Seg = 16;
					for (int Q = 0; Q < Seg; ++Q)
					{
						const double U0 = static_cast<double>(Q) / Seg;
						const double U1 = static_cast<double>(Q + 1) / Seg;
						const double S0 = Sa + Span * U0;
						const double S1 = Sa + Span * U1;
						// У опор арка опускается на Rise ниже низа настила, в середине пролёта почти касается его.
						auto ArchZ = [&](double U, double S) { return E.Ax.ZAt(S) - Deck - Rise + (Rise - 0.3) * (1.0 - 4.0 * (U - 0.5) * (U - 0.5)); };
						const V2 P0 = E.Ax.At(S0) + RightOf(E.Ax.Dir(S0)) * T;
						const V2 P1 = E.Ax.At(S1) + RightOf(E.Ax.Dir(S1)) * T;
						const double Z0 = ArchZ(U0, S0);
						const double Z1 = ArchZ(U1, S1);
						CitySection& Sc = Sec(P0.X, P0.Y, CityMat::Steel);
						const V2 F = Unit(P1 - P0);
						const V2 Rt = RightOf(F);
						// Коробчатое сечение 1.0 × 1.6 м.
						const V2 Off[4] = {Rt * -0.5, Rt * 0.5, Rt * 0.5, Rt * -0.5};
						const double Dz[4] = {0.0, 0.0, -1.6, -1.6};
						for (int Face = 0; Face < 4; ++Face)
						{
							const int Fn = (Face + 1) % 4;
							const V2 N2 = Face == 0 ? V2{0, 0} : (Face == 1 ? Rt : (Face == 2 ? V2{0, 0} : Rt * -1.0));
							const double Nz = Face == 0 ? 1.0 : (Face == 2 ? -1.0 : 0.0);
							const int32_t I0 = Sc.Vertex(P0.X + Off[Face].X, P0.Y + Off[Face].Y, Z0 + Dz[Face], N2.X, N2.Y, Nz, 0, 0);
							const int32_t I1 = Sc.Vertex(P0.X + Off[Fn].X, P0.Y + Off[Fn].Y, Z0 + Dz[Fn], N2.X, N2.Y, Nz, 1, 0);
							const int32_t I2 = Sc.Vertex(P1.X + Off[Fn].X, P1.Y + Off[Fn].Y, Z1 + Dz[Fn], N2.X, N2.Y, Nz, 1, 1);
							const int32_t I3 = Sc.Vertex(P1.X + Off[Face].X, P1.Y + Off[Face].Y, Z1 + Dz[Face], N2.X, N2.Y, Nz, 0, 1);
							Sc.Tri(I0, I1, I2, N2.X, N2.Y, Nz);
							Sc.Tri(I0, I2, I3, N2.X, N2.Y, Nz);
						}
						// Стойка от арки к настилу.
						if (Q % 2 == 1)
						{
							const double Gap = E.Ax.ZAt(S0) - Deck - Z0;
							if (Gap > 0.3)
							{
								Box(Sc, P0, Z0, F, 0.5, 0.5, Gap);
							}
						}
					}
				}
			}
		}
	}

	// ---- Перекрёстки ----
	void Junctions()
	{
		for (size_t N = 0; N < NodeEdges.size(); ++N)
		{
			if (NodeEdges[N].size() < 3)
			{
				continue;
			}
			std::vector<V2> Pts;
			const V2 C = NodeP(static_cast<int>(N));
			for (int K : NodeEdges[N])
			{
				const Edge& E = Edges[static_cast<size_t>(K)];
				const bool AtStart = E.Nodes.front() == static_cast<int>(N);
				const double S = AtStart ? std::max(0.0, E.Trim0) : E.Ax.Length() - std::max(0.0, E.Trim1);
				double Miter = 1.0;
				const V2 Rt = E.Ax.RightAt(S, Miter);
				const V2 P = E.Ax.At(S);
				Pts.push_back(P + Rt * E.HalfW);
				Pts.push_back(P - Rt * E.HalfW);
			}
			std::vector<V2> Hull = ConvexHull(Pts);
			if (Hull.size() < 3)
			{
				continue;
			}
			const double Z = H[N] + 0.05;
			CitySection& Sc = Sec(C.X, C.Y, CityMat::Asphalt);
			V2 Mid{0, 0};
			for (const V2& P : Hull)
			{
				Mid = Mid + P;
			}
			Mid = Mid * (1.0 / static_cast<double>(Hull.size()));
			const int32_t Ic = Sc.Vertex(Mid.X, Mid.Y, Z, 0, 0, 1, Mid.X / 3.5, Mid.Y / 4.0);
			std::vector<int32_t> Ring;
			for (const V2& P : Hull)
			{
				Ring.push_back(Sc.Vertex(P.X, P.Y, Z, 0, 0, 1, P.X / 3.5, P.Y / 4.0));
			}
			for (size_t K = 0; K < Ring.size(); ++K)
			{
				Sc.Tri(Ic, Ring[K], Ring[(K + 1) % Ring.size()], 0, 0, 1);
			}
		}
	}

	// Острова колец: многоугольник по оси кольца, ужатый на полуширину; газон и бордюр.
	void RingIslands()
	{
		for (const CityWay& W : D.Ways)
		{
			if (!W.Ring || W.Nodes.size() < 4 || W.Nodes.front() != W.Nodes.back())
			{
				continue;
			}
			std::vector<CityPt> Poly;
			V2 C{0, 0};
			for (size_t K = 0; K + 1 < W.Nodes.size(); ++K)
			{
				const V2 P = NodeP(W.Nodes[K]);
				C = C + P;
			}
			C = C * (1.0 / static_cast<double>(W.Nodes.size() - 1));
			const double Half = 0.5 * W.Lanes * CityLaneWidth(W.Class) + 0.3;
			for (size_t K = 0; K + 1 < W.Nodes.size(); ++K)
			{
				const V2 P = NodeP(W.Nodes[K]);
				const V2 In = Unit(C - P);
				const V2 Q = P + In * Half;
				Poly.push_back(CityPt{Q.X, Q.Y});
			}
			const std::vector<int> Tri = Triangulate(Poly);
			CitySection& Sc = Sec(C.X, C.Y, CityMat::Grass);
			const double Z = 0.25;
			std::vector<int32_t> Id;
			for (const CityPt& P : Poly)
			{
				Id.push_back(Sc.Vertex(P.X, P.Y, Z, 0, 0, 1, P.X * 0.2, P.Y * 0.2));
			}
			for (size_t K = 0; K + 2 < Tri.size(); K += 3)
			{
				Sc.Tri(Id[static_cast<size_t>(Tri[K])], Id[static_cast<size_t>(Tri[K + 1])], Id[static_cast<size_t>(Tri[K + 2])], 0, 0, 1);
			}
			CitySection& Cb = Sec(C.X, C.Y, CityMat::Curb);
			for (size_t K = 0; K < Poly.size(); ++K)
			{
				const CityPt& A = Poly[K];
				const CityPt& B = Poly[(K + 1) % Poly.size()];
				const V2 Out = RightOf(Unit(V2{B.X - A.X, B.Y - A.Y}));
				const V2 Mid = V2{(A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5};
				const V2 Nrm = (Out.X * (Mid.X - C.X) + Out.Y * (Mid.Y - C.Y)) > 0.0 ? Out : Out * -1.0;
				const int32_t I0 = Cb.Vertex(A.X, A.Y, 0.0, Nrm.X, Nrm.Y, 0, 0, 1);
				const int32_t I1 = Cb.Vertex(B.X, B.Y, 0.0, Nrm.X, Nrm.Y, 0, 1, 1);
				const int32_t I2 = Cb.Vertex(B.X, B.Y, Z, Nrm.X, Nrm.Y, 0, 1, 0);
				const int32_t I3 = Cb.Vertex(A.X, A.Y, Z, Nrm.X, Nrm.Y, 0, 0, 0);
				Cb.Tri(I0, I1, I2, Nrm.X, Nrm.Y, 0);
				Cb.Tri(I0, I2, I3, Nrm.X, Nrm.Y, 0);
			}
		}
	}

	// ---- Реквизит улиц ----
	void RoadProps(const Edge& E)
	{
		const double L = E.Ax.Length();
		const double S0 = std::max(0.0, E.Trim0);
		const double S1 = L - std::max(0.0, E.Trim1);
		const double W = E.HalfW;
		const double Sw = CitySidewalkWidth(E.Class);
		auto Add = [&](CityPropKind Kind, double S, double T, double YawD, int Value, const std::string& Text) {
			double Miter = 1.0;
			const V2 P = E.Ax.At(S) + E.Ax.RightAt(S, Miter) * T;
			CityProp Pr;
			Pr.Kind = Kind;
			Pr.X = P.X;
			Pr.Y = P.Y;
			Pr.Z = E.Ax.ZAt(S) + (E.Bridge ? 0.2 : 0.1);
			Pr.Yaw = YawD;
			Pr.Value = Value;
			Pr.Text = Text;
			R.Props.push_back(Pr);
		};
		const int Nf = E.Nodes.front();
		const int Nb = E.Nodes.back();
		const int Ff = D.Nodes[static_cast<size_t>(Nf)].Flags;
		const int Fb = D.Nodes[static_cast<size_t>(Nb)].Flags;
		const double YawFwd = YawDeg(E.Ax.Dir(std::min(L, S1)));
		const double YawBack = YawDeg(E.Ax.Dir(std::max(0.0, S0))) + 180.0;
		const double Curb = W + std::min(Sw, 1.2);
		// Въезд в конец ребра (по ходу оси) — правая сторона; в начало (против оси) — левая.
		const bool InEnd = Degree(Nb) >= 3 || (Fb & (CityFlag::Signals | CityFlag::Stop | CityFlag::GiveWay));
		const bool InStart = !E.OneWay && (Degree(Nf) >= 3 || (Ff & (CityFlag::Signals | CityFlag::Stop | CityFlag::GiveWay)));
		auto RingAt = [&](int N) {
			for (int K : NodeEdges[static_cast<size_t>(N)])
			{
				if (Edges[static_cast<size_t>(K)].Ring)
				{
					return true;
				}
			}
			return false;
		};
		if (S1 - S0 > 8.0 && !E.Ring)
		{
			if (InEnd)
			{
				const double S = S1 - 1.0;
				if (Fb & CityFlag::Signals)
				{
					Add(CityPropKind::TrafficLight, S, Curb, YawFwd + 180.0, 0, "");
				}
				else if (Fb & CityFlag::Stop)
				{
					Add(CityPropKind::SignStop, S, Curb, YawFwd + 180.0, 0, "");
				}
				else if ((Fb & CityFlag::GiveWay) || RingAt(Nb))
				{
					Add(CityPropKind::SignYield, S, Curb, YawFwd + 180.0, 0, "");
				}
				if (RingAt(Nb))
				{
					Add(CityPropKind::SignRoundabout, S - 25.0 > S0 ? S - 25.0 : S, Curb, YawFwd + 180.0, 0, "");
				}
			}
			if (InStart)
			{
				const double S = S0 + 1.0;
				if (Ff & CityFlag::Signals)
				{
					Add(CityPropKind::TrafficLight, S, -Curb, YawBack + 180.0, 0, "");
				}
				else if (Ff & CityFlag::Stop)
				{
					Add(CityPropKind::SignStop, S, -Curb, YawBack + 180.0, 0, "");
				}
				else if ((Ff & CityFlag::GiveWay) || RingAt(Nf))
				{
					Add(CityPropKind::SignYield, S, -Curb, YawBack + 180.0, 0, "");
				}
				if (RingAt(Nf))
				{
					Add(CityPropKind::SignRoundabout, S + 25.0 < S1 ? S + 25.0 : S, -Curb, YawBack + 180.0, 0, "");
				}
			}
			// Названия улиц на углах перекрёстков.
			if (!E.Name.empty() && Degree(Nb) >= 3 && E.Class <= CityRoadClass::Tertiary)
			{
				Add(CityPropKind::StreetName, S1 - 0.5, W + Sw - 0.3, YawFwd + 90.0, 0, E.Name);
			}
			// Ограничение скорости в начале улицы (если не городские 60).
			if (E.Speed > 0 && E.Speed != 60 && L > 120.0)
			{
				Add(CityPropKind::SignSpeed, S0 + 20.0, Curb, YawFwd + 180.0, E.Speed, "");
			}
		}
		// Переходы — знаки с обеих сторон.
		for (size_t K = 1; K + 1 < E.Nodes.size(); ++K)
		{
			const int N = E.Nodes[K];
			if (D.Nodes[static_cast<size_t>(N)].Flags & CityFlag::Crossing)
			{
				const double S = E.Ax.S[K];
				Add(CityPropKind::SignCrossing, S, Curb, YawDeg(E.Ax.Dir(S)) + 180.0, 0, "");
				Add(CityPropKind::SignCrossing, S, -Curb, YawDeg(E.Ax.Dir(S)), 0, "");
			}
		}
		// Фонари: на проспектах и улицах — с обеих сторон через 32 м, на мостах — по краям.
		if (E.Class <= CityRoadClass::Tertiary && S1 - S0 > 20.0)
		{
			const double Spacing = 32.0;
			int K = 0;
			for (double S = S0 + 8.0; S < S1 - 4.0; S += Spacing, ++K)
			{
				const double T = E.Bridge ? W + 1.4 : W + 0.6;
				const double Dy = YawDeg(E.Ax.Dir(S));
				if (E.OneWay || E.Ring)
				{
					Add(CityPropKind::StreetLamp, S, T, Dy - 90.0, 0, "");
				}
				else
				{
					Add(CityPropKind::StreetLamp, S + (K % 2) * Spacing * 0.5, T, Dy - 90.0, 0, "");
					Add(CityPropKind::StreetLamp, S + ((K + 1) % 2) * Spacing * 0.5, -T, Dy + 90.0, 0, "");
				}
			}
		}
	}

	// ---- Дома ----
	void Buildings()
	{
		for (size_t Bi = 0; Bi < D.Buildings.size(); ++Bi)
		{
			const CityBuilding& B = D.Buildings[Bi];
			std::vector<CityPt> Ring = B.Ring;
			if (Ring.size() < 3)
			{
				continue;
			}
			if (SignedArea(Ring) < 0.0)
			{
				std::reverse(Ring.begin(), Ring.end()); // против часовой (сверху): наружу — вправо от ребра
			}
			const uint32_t Seed = Hash(static_cast<uint32_t>(Bi), 0xB1D6u);
			CityMat Wm = CityMat::WallPanel;
			double FloorH = 2.8;
			uint32_t Tint = 0xFFFFFFFFu;
			const double Pick = HashToUnit(Seed);
			if (B.Kind == "panel")
			{
				static const double Tints[][3] = {{0.93, 0.93, 0.9}, {0.96, 0.88, 0.74}, {0.8, 0.88, 0.96}, {0.96, 0.92, 0.7}, {0.9, 0.85, 0.95},
					{0.82, 0.94, 0.84}};
				const auto& T = Tints[Seed % 6u];
				Tint = Rgba(T[0], T[1], T[2]);
			}
			else if (B.Kind == "stucco" || B.Kind == "church")
			{
				Wm = CityMat::WallStucco;
				FloorH = 3.4;
				static const double Tints[][3] = {{0.98, 0.86, 0.6}, {0.95, 0.78, 0.7}, {0.94, 0.92, 0.82}, {0.84, 0.9, 0.78}, {0.88, 0.8, 0.68}};
				const auto& T = Tints[Seed % 5u];
				Tint = Rgba(T[0], T[1], T[2]);
			}
			else if (B.Kind == "brick" || B.Kind == "garage")
			{
				Wm = Pick < 0.6 ? CityMat::WallBrick : CityMat::WallPanel;
			}
			else if (B.Kind == "office" || B.Kind == "shop")
			{
				Wm = CityMat::WallOffice;
				FloorH = 3.3;
				Tint = Pick < 0.5 ? Rgba(0.95, 0.96, 0.98) : Rgba(0.85, 0.9, 0.95);
			}
			else if (B.Kind == "industrial")
			{
				Wm = CityMat::WallIndustrial;
				FloorH = 4.5;
				Tint = Rgba(0.78, 0.76, 0.72);
			}
			else if (B.Kind == "house")
			{
				Wm = CityMat::WallHouse;
				static const double Tints[][3] = {{0.2, 0.33, 0.55}, {0.25, 0.45, 0.3}, {0.7, 0.55, 0.25}, {0.45, 0.3, 0.2}, {0.75, 0.72, 0.62}};
				const auto& T = Tints[Seed % 5u];
				Tint = Rgba(T[0], T[1], T[2]);
			}
			const int Levels = std::max(1, B.Levels);
			const double Height = Levels * FloorH + (Levels > 2 ? 0.8 : 0.4);
			V2 C{0, 0};
			for (const CityPt& P : Ring)
			{
				C = C + V2{P.X, P.Y};
			}
			C = C * (1.0 / static_cast<double>(Ring.size()));
			if (IsWet(C.X, C.Y))
			{
				continue;
			}
			CitySection& Walls = Sec(C.X, C.Y, Wm);
			CitySection& Lit = Sec(C.X, C.Y, CityMat::LitWindow);
			const double Zb = -0.6;
			const bool bWindows = Wm != CityMat::WallIndustrial && Wm != CityMat::WallHouse;
			rgen::Rng Rand(Seed);
			for (size_t K = 0; K < Ring.size(); ++K)
			{
				const V2 A{Ring[K].X, Ring[K].Y};
				const V2 Bp{Ring[(K + 1) % Ring.size()].X, Ring[(K + 1) % Ring.size()].Y};
				const double L = Len(Bp - A);
				if (L < 0.05)
				{
					continue;
				}
				const V2 Dir = Unit(Bp - A);
				const V2 Out = RightOf(Dir) * -1.0; // контур против часовой — наружу слева от направления обхода… проверено ниже
				V2 Nrm = Out;
				{
					const V2 Mid = (A + Bp) * 0.5;
					if ((Mid.X - C.X) * Nrm.X + (Mid.Y - C.Y) * Nrm.Y < 0.0)
					{
						Nrm = Nrm * -1.0;
					}
				}
				const double Vb = (Height - Zb) / FloorH;
				const int32_t I0 = Walls.Vertex(A.X, A.Y, Zb, Nrm.X, Nrm.Y, 0, 0.0, Vb, Tint);
				const int32_t I1 = Walls.Vertex(Bp.X, Bp.Y, Zb, Nrm.X, Nrm.Y, 0, L / 3.0, Vb, Tint);
				const int32_t I2 = Walls.Vertex(Bp.X, Bp.Y, Height, Nrm.X, Nrm.Y, 0, L / 3.0, 0.0, Tint);
				const int32_t I3 = Walls.Vertex(A.X, A.Y, Height, Nrm.X, Nrm.Y, 0, 0.0, 0.0, Tint);
				Walls.Tri(I0, I1, I2, Nrm.X, Nrm.Y, 0);
				Walls.Tri(I0, I2, I3, Nrm.X, Nrm.Y, 0);
				// Окна, которые горят вечером (по сетке текстуры: пролёт 3 м, этаж FloorH).
				if (bWindows)
				{
					const int Bays = static_cast<int>(L / 3.0);
					for (int Bay = 0; Bay < Bays; ++Bay)
					{
						for (int Fl = 0; Fl < Levels; ++Fl)
						{
							if (!Rand.Chance(0.12))
							{
								continue;
							}
							const double U0 = Bay * 3.0 + 0.85;
							const double U1 = Bay * 3.0 + 2.15;
							const double Top = Height - Fl * FloorH - 0.6;
							const double Bot = Top - 1.25;
							const V2 Pa = A + Dir * U0 + Nrm * 0.05;
							const V2 Pb = A + Dir * U1 + Nrm * 0.05;
							const int32_t W0 = Lit.Vertex(Pa.X, Pa.Y, Bot, Nrm.X, Nrm.Y, 0, 0, 1);
							const int32_t W1 = Lit.Vertex(Pb.X, Pb.Y, Bot, Nrm.X, Nrm.Y, 0, 1, 1);
							const int32_t W2 = Lit.Vertex(Pb.X, Pb.Y, Top, Nrm.X, Nrm.Y, 0, 1, 0);
							const int32_t W3 = Lit.Vertex(Pa.X, Pa.Y, Top, Nrm.X, Nrm.Y, 0, 0, 0);
							Lit.Tri(W0, W1, W2, Nrm.X, Nrm.Y, 0);
							Lit.Tri(W0, W2, W3, Nrm.X, Nrm.Y, 0);
						}
					}
				}
			}
			// Крыша.
			const std::vector<int> Tri = Triangulate(Ring);
			CitySection& Roof = Sec(C.X, C.Y, CityMat::Roof);
			std::vector<int32_t> Id;
			for (const CityPt& P : Ring)
			{
				Id.push_back(Roof.Vertex(P.X, P.Y, Height, 0, 0, 1, P.X * 0.25, P.Y * 0.25));
			}
			for (size_t K = 0; K + 2 < Tri.size(); K += 3)
			{
				Roof.Tri(Id[static_cast<size_t>(Tri[K])], Id[static_cast<size_t>(Tri[K + 1])], Id[static_cast<size_t>(Tri[K + 2])], 0, 0, 1);
			}
			++R.BuildingsBuilt;
		}
	}

	// ---- Парки и железная дорога ----
	void Areas()
	{
		for (const auto& Park : D.Parks)
		{
			if (Park.size() < 3)
			{
				continue;
			}
			const std::vector<int> Tri = Triangulate(Park);
			V2 C{0, 0};
			for (const CityPt& P : Park)
			{
				C = C + V2{P.X, P.Y};
			}
			C = C * (1.0 / static_cast<double>(Park.size()));
			CitySection& Sc = Sec(C.X, C.Y, CityMat::Grass);
			std::vector<int32_t> Id;
			for (const CityPt& P : Park)
			{
				Id.push_back(Sc.Vertex(P.X, P.Y, 0.03, 0, 0, 1, P.X * 0.2, P.Y * 0.2));
			}
			for (size_t K = 0; K + 2 < Tri.size(); K += 3)
			{
				Sc.Tri(Id[static_cast<size_t>(Tri[K])], Id[static_cast<size_t>(Tri[K + 1])], Id[static_cast<size_t>(Tri[K + 2])], 0, 0, 1);
			}
		}
		for (const auto& Line : D.Rails)
		{
			if (Line.size() < 2)
			{
				continue;
			}
			Edge E;
			for (const CityPt& P : Line)
			{
				E.Ax.P.push_back(V2{P.X, P.Y});
				E.Ax.Z.push_back(0.0);
			}
			E.Ax.Finish();
			Ribbon(E, 0.0, E.Ax.Length(), -2.2, 2.2, 0.15, CityMat::Ballast, 0.5, 0.5);
			for (double T : {-0.76, 0.76})
			{
				Ribbon(E, 0.0, E.Ax.Length(), T - 0.04, T + 0.04, 0.33, CityMat::Steel, 1.0, 1.0);
			}
		}
	}

	// ---- Земля города: плоская, с руслом реки; рядом с трассой не строится ----
	void Ground()
	{
		const double Ts = O.TileSize;
		const int Tx0 = static_cast<int>(std::floor(R.MinX / Ts));
		const int Tx1 = static_cast<int>(std::floor(R.MaxX / Ts));
		const int Ty0 = static_cast<int>(std::floor(R.MinY / Ts));
		const int Ty1 = static_cast<int>(std::floor(R.MaxY / Ts));
		const double Kr2 = O.KeepoutRadius * O.KeepoutRadius;
		auto Blocked = [&](double X, double Y) {
			for (const CityPt& K : O.Keepout)
			{
				const double Dx = X - K.X;
				const double Dy = Y - K.Y;
				if (Dx * Dx + Dy * Dy < Kr2)
				{
					return true;
				}
			}
			return false;
		};
		for (int Tx = Tx0; Tx <= Tx1; ++Tx)
		{
			for (int Ty = Ty0; Ty <= Ty1; ++Ty)
			{
				const double X0 = Tx * Ts, Y0 = Ty * Ts;
				const int N = static_cast<int>(std::ceil(Ts / Step));
				// Есть ли в тайле вода или запретная зона — тогда мелкая сетка, иначе один квадрат.
				bool Fine = false;
				for (int I = 0; I <= N && !Fine; I += 2)
				{
					for (int J = 0; J <= N && !Fine; J += 2)
					{
						const double X = X0 + I * Step, Y = Y0 + J * Step;
						Fine = IsWet(X, Y) || Blocked(X, Y);
					}
				}
				const double Cx = X0 + Ts * 0.5, Cy = Y0 + Ts * 0.5;
				CitySection& G = Sec(Cx, Cy, CityMat::Ground);
				if (!Fine)
				{
					const int32_t A = G.Vertex(X0, Y0, 0.0, 0, 0, 1, X0 / 20.0, Y0 / 20.0);
					const int32_t B = G.Vertex(X0 + Ts, Y0, 0.0, 0, 0, 1, (X0 + Ts) / 20.0, Y0 / 20.0);
					const int32_t Cc = G.Vertex(X0 + Ts, Y0 + Ts, 0.0, 0, 0, 1, (X0 + Ts) / 20.0, (Y0 + Ts) / 20.0);
					const int32_t Dd = G.Vertex(X0, Y0 + Ts, 0.0, 0, 0, 1, X0 / 20.0, (Y0 + Ts) / 20.0);
					G.Tri(A, B, Cc, 0, 0, 1);
					G.Tri(A, Cc, Dd, 0, 0, 1);
					continue;
				}
				CitySection& Wt = Sec(Cx, Cy, CityMat::Water);
				auto Zat = [&](double X, double Y) {
					const int I = static_cast<int>(std::lround((X - Gx0) / Step));
					const int J = static_cast<int>(std::lround((Y - Gy0) / Step));
					if (I < 0 || J < 0 || I >= Gnx || J >= Gny)
					{
						return 0.0;
					}
					return GroundZ(I, J);
				};
				for (int I = 0; I < N; ++I)
				{
					for (int J = 0; J < N; ++J)
					{
						const double Xa = X0 + I * Step, Ya = Y0 + J * Step;
						const double Xb = Xa + Step, Yb = Ya + Step;
						if (Blocked(Xa + Step * 0.5, Ya + Step * 0.5))
						{
							continue;
						}
						const double Z00 = Zat(Xa, Ya), Z10 = Zat(Xb, Ya), Z11 = Zat(Xb, Yb), Z01 = Zat(Xa, Yb);
						const int32_t A = G.Vertex(Xa, Ya, Z00, 0, 0, 1, Xa / 20.0, Ya / 20.0);
						const int32_t B = G.Vertex(Xb, Ya, Z10, 0, 0, 1, Xb / 20.0, Ya / 20.0);
						const int32_t Cc = G.Vertex(Xb, Yb, Z11, 0, 0, 1, Xb / 20.0, Yb / 20.0);
						const int32_t Dd = G.Vertex(Xa, Yb, Z01, 0, 0, 1, Xa / 20.0, Yb / 20.0);
						G.Tri(A, B, Cc, 0, 0, 1);
						G.Tri(A, Cc, Dd, 0, 0, 1);
						if (std::min(std::min(Z00, Z10), std::min(Z11, Z01)) < -0.3)
						{
							const double Zw = O.WaterLevel;
							const int32_t Wa = Wt.Vertex(Xa, Ya, Zw, 0, 0, 1, Xa / 30.0, Ya / 30.0);
							const int32_t Wb = Wt.Vertex(Xb, Ya, Zw, 0, 0, 1, Xb / 30.0, Ya / 30.0);
							const int32_t Wc = Wt.Vertex(Xb, Yb, Zw, 0, 0, 1, Xb / 30.0, Yb / 30.0);
							const int32_t Wd = Wt.Vertex(Xa, Yb, Zw, 0, 0, 1, Xa / 30.0, Yb / 30.0);
							Wt.Tri(Wa, Wb, Wc, 0, 0, 1);
							Wt.Tri(Wa, Wc, Wd, 0, 0, 1);
						}
					}
				}
			}
		}
	}
};
} // namespace

// ---------------------------------------------------------------------------------------------
int32_t CitySection::Vertex(double X, double Y, double Z, double Nx, double Ny, double Nz, double U, double V, uint32_t Rgba32)
{
	const int32_t Index = static_cast<int32_t>(P.size() / 3);
	P.insert(P.end(), {static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z)});
	const double L = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
	const double K = L > 1e-9 ? 1.0 / L : 0.0;
	N.insert(N.end(), {static_cast<float>(Nx * K), static_cast<float>(Ny * K), static_cast<float>(Nz * K)});
	UV.insert(UV.end(), {static_cast<float>(U), static_cast<float>(V)});
	C.insert(C.end(), {static_cast<uint8_t>(Rgba32 & 0xFFu), static_cast<uint8_t>((Rgba32 >> 8) & 0xFFu), static_cast<uint8_t>((Rgba32 >> 16) & 0xFFu),
						  static_cast<uint8_t>((Rgba32 >> 24) & 0xFFu)});
	return Index;
}

void CitySection::Tri(int32_t A, int32_t B, int32_t Cv, double Hx, double Hy, double Hz)
{
	auto Pt = [&](int32_t Vi) {
		const size_t K = static_cast<size_t>(Vi) * 3;
		return std::array<double, 3>{P[K], P[K + 1], P[K + 2]};
	};
	const auto Pa = Pt(A), Pb = Pt(B), Pc = Pt(Cv);
	const double Ux = Pc[0] - Pa[0], Uy = Pc[1] - Pa[1], Uz = Pc[2] - Pa[2];
	const double Vx = Pb[0] - Pa[0], Vy = Pb[1] - Pa[1], Vz = Pb[2] - Pa[2];
	const double Nx = Uy * Vz - Uz * Vy;
	const double Ny = Uz * Vx - Ux * Vz;
	const double Nz = Ux * Vy - Uy * Vx;
	if (Nx * Hx + Ny * Hy + Nz * Hz >= 0.0)
	{
		I.insert(I.end(), {A, B, Cv});
	}
	else
	{
		I.insert(I.end(), {A, Cv, B});
	}
}

size_t CityResult::TriangleCount() const
{
	size_t N = 0;
	for (const CityTile& T : Tiles)
	{
		for (const CitySection& S : T.Sections)
		{
			N += S.I.size() / 3;
		}
	}
	return N;
}

double CityLaneWidth(CityRoadClass Class)
{
	switch (Class)
	{
	case CityRoadClass::Motorway:
	case CityRoadClass::Trunk: return 3.75;
	case CityRoadClass::Primary:
	case CityRoadClass::Secondary:
	case CityRoadClass::Link: return 3.5;
	case CityRoadClass::Tertiary: return 3.25;
	default: return 3.0;
	}
}

double CitySidewalkWidth(CityRoadClass Class)
{
	switch (Class)
	{
	case CityRoadClass::Primary: return 4.0;
	case CityRoadClass::Secondary: return 3.0;
	case CityRoadClass::Tertiary: return 2.5;
	default: return 1.8;
	}
}

CityRoadClass CityClassFromOsm(const std::string& Hw)
{
	if (Hw == "motorway") return CityRoadClass::Motorway;
	if (Hw == "trunk") return CityRoadClass::Trunk;
	if (Hw == "primary") return CityRoadClass::Primary;
	if (Hw == "secondary") return CityRoadClass::Secondary;
	if (Hw == "tertiary") return CityRoadClass::Tertiary;
	if (Hw.size() > 5 && Hw.compare(Hw.size() - 5, 5, "_link") == 0) return CityRoadClass::Link;
	return CityRoadClass::Residential;
}

bool CityData::Parse(const std::string& Text, std::string* Error)
{
	*this = CityData();
	std::istringstream In(Text);
	std::string Line;
	int LineNo = 0;
	bool bHeader = false;
	auto Fail = [&](const char* What) {
		if (Error)
		{
			*Error = std::string(What) + " (строка " + std::to_string(LineNo) + ")";
		}
		return false;
	};
	while (std::getline(In, Line))
	{
		++LineNo;
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.pop_back();
		}
		if (Line.empty())
		{
			continue;
		}
		const char* Cur = Line.data();
		const char* End = Line.data() + Line.size();
		const std::string Tag = ReadWord(Cur, End);
		if (!bHeader)
		{
			if (Tag != "PEREGON-CITY")
			{
				return Fail("не файл города");
			}
			bHeader = true;
			continue;
		}
		if (Tag == "CITY")
		{
			Name = Line.size() > 5 ? Line.substr(5) : std::string();
		}
		else if (Tag == "CENTER")
		{
			ReadDouble(Cur, End, CenterX);
			ReadDouble(Cur, End, CenterY);
			ReadDouble(Cur, End, Core);
		}
		else if (Tag == "N")
		{
			CityNode Nd;
			if (!ReadDouble(Cur, End, Nd.X) || !ReadDouble(Cur, End, Nd.Y))
			{
				return Fail("узел без координат");
			}
			ReadInt(Cur, End, Nd.Flags);
			Nodes.push_back(Nd);
		}
		else if (Tag == "W")
		{
			std::string Head, WayName, Tail;
			SplitBars(Line, 2, Head, WayName, Tail);
			const char* H = Head.data();
			const char* He = Head.data() + Head.size();
			CityWay W;
			W.Class = CityClassFromOsm(ReadWord(H, He));
			int OneWay = 0, Ring = 0, Bridge = 0, Tunnel = 0;
			if (!ReadInt(H, He, W.Lanes) || !ReadInt(H, He, OneWay) || !ReadInt(H, He, Ring) || !ReadInt(H, He, Bridge) || !ReadInt(H, He, Tunnel) ||
				!ReadInt(H, He, W.Layer) || !ReadInt(H, He, W.Speed))
			{
				return Fail("неполная дорога");
			}
			W.OneWay = OneWay != 0;
			W.Ring = Ring != 0;
			W.Bridge = Bridge != 0;
			W.Tunnel = Tunnel != 0;
			W.Style = ReadWord(H, He);
			if (W.Style == "-")
			{
				W.Style.clear();
			}
			W.Name = WayName;
			const char* T = Tail.data();
			const char* Te = Tail.data() + Tail.size();
			int Idx = 0;
			while (ReadInt(T, Te, Idx))
			{
				if (Idx < 0 || static_cast<size_t>(Idx) >= Nodes.size())
				{
					return Fail("дорога ссылается на несуществующий узел");
				}
				W.Nodes.push_back(Idx);
			}
			if (W.Nodes.size() >= 2)
			{
				Ways.push_back(W);
			}
		}
		else if (Tag == "B")
		{
			std::string Head, BName, Tail;
			SplitBars(Line, 2, Head, BName, Tail);
			const char* H = Head.data();
			const char* He = Head.data() + Head.size();
			CityBuilding B;
			B.Kind = ReadWord(H, He);
			ReadInt(H, He, B.Levels);
			B.Name = BName;
			B.Ring = ReadPoints(Tail.data(), Tail.data() + Tail.size());
			if (B.Ring.size() >= 3)
			{
				Buildings.push_back(B);
			}
		}
		else if (Tag == "L")
		{
			CityLandmark L;
			L.Key = ReadWord(Cur, End);
			ReadDouble(Cur, End, L.X);
			ReadDouble(Cur, End, L.Y);
			ReadDouble(Cur, End, L.Yaw);
			ReadDouble(Cur, End, L.Radius);
			Landmarks.push_back(L);
		}
		else if (Tag == "WATER" || Tag == "ISLAND" || Tag == "PARK" || Tag == "RAIL")
		{
			std::vector<CityPt> Pts = ReadPoints(Cur, End);
			if (Pts.size() >= 2)
			{
				(Tag == "WATER" ? Water : Tag == "ISLAND" ? Islands : Tag == "PARK" ? Parks : Rails).push_back(std::move(Pts));
			}
		}
	}
	if (!bHeader)
	{
		return Fail("пустой файл");
	}
	return true;
}

CityResult BuildCity(const CityData& Data, const CityBuildOptions& Options)
{
	CityResult Result;
	Builder B(Data, Options, Result);
	B.Run();
	return Result;
}
} // namespace rgen
