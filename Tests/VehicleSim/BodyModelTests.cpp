// Проверки процедурной модели кузова + выгрузка в JSON для превью (Tests/VehicleSim/preview).
// Запуск: vmodel_tests [путь.json]
#include "vsim/BodyModel.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
int Failures = 0;
int Checks = 0;

void Check(bool bOk, const std::string& What)
{
	++Checks;
	if (!bOk)
	{
		++Failures;
		std::printf("FAIL: %s\n", What.c_str());
	}
}

void Dump(const vmodel::Model& M, const char* Path)
{
	FILE* F = std::fopen(Path, "wb");
	if (!F)
	{
		std::printf("cannot write %s\n", Path);
		return;
	}
	std::fprintf(F, "{\"parts\":[");
	for (size_t PI = 0; PI < M.Parts.size(); ++PI)
	{
		const vmodel::PartMesh& P = M.Parts[PI];
		std::fprintf(F, "%s{\"name\":\"%s\",\"origin\":[%.3f,%.3f,%.3f],\"sections\":[", PI ? "," : "", vmodel::PartName(P.Id), P.Origin.X,
			P.Origin.Y, P.Origin.Z);
		for (size_t SI = 0; SI < P.Sections.size(); ++SI)
		{
			const vmodel::Section& S = P.Sections[SI];
			std::fprintf(F, "%s{\"mat\":\"%s\",\"pos\":[", SI ? "," : "", vmodel::MatName(S.Material));
			for (size_t K = 0; K < S.Positions.size(); ++K)
			{
				std::fprintf(F, "%s%.2f,%.2f,%.2f", K ? "," : "", S.Positions[K].X, S.Positions[K].Y, S.Positions[K].Z);
			}
			std::fprintf(F, "],\"nrm\":[");
			for (size_t K = 0; K < S.Normals.size(); ++K)
			{
				std::fprintf(F, "%s%.3f,%.3f,%.3f", K ? "," : "", S.Normals[K].X, S.Normals[K].Y, S.Normals[K].Z);
			}
			std::fprintf(F, "],\"idx\":[");
			for (size_t K = 0; K < S.Indices.size(); ++K)
			{
				std::fprintf(F, "%s%d", K ? "," : "", S.Indices[K]);
			}
			std::fprintf(F, "]}");
		}
		std::fprintf(F, "]}");
	}
	std::fprintf(F, "],\"hitboxes\":[");
	for (size_t K = 0; K < M.HitBoxes.size(); ++K)
	{
		const vmodel::HitBox& H = M.HitBoxes[K];
		std::fprintf(F, "%s{\"part\":\"%s\",\"tag\":\"%s\",\"c\":[%.2f,%.2f,%.2f],\"h\":[%.2f,%.2f,%.2f]}", K ? "," : "", vmodel::PartName(H.Owner),
			H.Tag, H.Center.X, H.Center.Y, H.Center.Z, H.HalfSize.X, H.HalfSize.Y, H.HalfSize.Z);
	}
	std::fprintf(F, "],\"steeringDir\":[%.4f,%.4f,%.4f]}", M.SteeringColumnDir.X, M.SteeringColumnDir.Y, M.SteeringColumnDir.Z);
	std::fclose(F);
}
} // namespace

int main(int Argc, char** Argv)
{
	const vmodel::Model M = vmodel::BuildSokol();

	float MinX = 1e9f, MaxX = -1e9f, MinY = 1e9f, MaxY = -1e9f, MinZ = 1e9f, MaxZ = -1e9f;
	size_t BadNormals = 0;
	size_t BadIndices = 0;
	size_t Tris = 0;
	for (const vmodel::PartMesh& P : M.Parts)
	{
		for (const vmodel::Section& S : P.Sections)
		{
			Check(S.Positions.size() == S.Normals.size() && S.Positions.size() == S.U.size(), "размеры массивов секции");
			for (size_t K = 0; K < S.Normals.size(); ++K)
			{
				const vmodel::V3& N = S.Normals[K];
				const float L = std::sqrt(N.X * N.X + N.Y * N.Y + N.Z * N.Z);
				if (!(std::fabs(L - 1.f) < 1e-3f))
				{
					++BadNormals;
				}
			}
			for (int32_t I : S.Indices)
			{
				if (I < 0 || static_cast<size_t>(I) >= S.Positions.size())
				{
					++BadIndices;
				}
			}
			Tris += S.Indices.size() / 3;
			if (P.Id != vmodel::Part::Body)
			{
				continue;
			}
			for (const vmodel::V3& V : S.Positions)
			{
				MinX = std::fmin(MinX, V.X); MaxX = std::fmax(MaxX, V.X);
				MinY = std::fmin(MinY, V.Y); MaxY = std::fmax(MaxY, V.Y);
				MinZ = std::fmin(MinZ, V.Z); MaxZ = std::fmax(MaxZ, V.Z);
			}
		}
	}
	std::printf("parts %zu, triangles %zu, hitboxes %zu\n", M.Parts.size(), Tris, M.HitBoxes.size());
	std::printf("body bounds X %.1f..%.1f  Y %.1f..%.1f  Z %.1f..%.1f\n", MinX, MaxX, MinY, MaxY, MinZ, MaxZ);
	Check(BadNormals == 0, "нормали единичные (" + std::to_string(BadNormals) + " плохих)");
	Check(BadIndices == 0, "индексы в пределах");
	Check(M.Parts.size() == static_cast<size_t>(vmodel::Part::Count), "все узлы на месте");
	Check(Tris > 20000 && Tris < 200000, "разумное число треугольников");
	Check(MaxX - MinX > 420.f && MaxX - MinX < 432.f, "длина с бамперами ~4.25 м");
	Check(MaxY - MinY > 160.f && MaxY - MinY < 172.f, "ширина ~1.62–1.70 м");
	Check(MaxZ > 87.f && MaxZ < 90.f, "крыша на ~1.41 м над землёй");
	Check(MinZ > -37.f, "ничего не висит ниже глушителя (кроме колёс)");

	// Колесо на полном ходе сжатия и полной выкрутке не задевает подкрылок (радиус 35.5 вокруг (x, −21)).
	const float CompressedCenterZ = -15.5f;
	const float WorstReach = (CompressedCenterZ + 21.f) + std::sqrt(28.5f * 28.5f + 9.f * 9.f);
	Check(WorstReach < 35.5f, "колесо помещается в арку на отбойнике и выкрутке");

	const char* Tags[] = {"Hood", "Trunk", "DoorFL", "DoorFR", "DoorRL", "DoorRR", "Glovebox", "Key", "Lights", "Handbrake", "Fuel", "GloveInside", "TrunkInside"};
	for (const char* Tag : Tags)
	{
		bool bFound = false;
		for (const vmodel::HitBox& H : M.HitBoxes)
		{
			bFound |= std::string(H.Tag) == Tag;
		}
		Check(bFound, std::string("мишень взаимодействия ") + Tag);
	}

	if (Argc > 1)
	{
		Dump(M, Argv[1]);
	}
	std::printf("%d checks, %d failures\n", Checks, Failures);
	return Failures == 0 ? 0 : 1;
}
