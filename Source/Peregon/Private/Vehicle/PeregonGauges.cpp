// Циферблаты щитка «шестёрки»: спидометр 0–180 км/ч с одометром, тахометр 0–8000 (×100) с красной зоной,
// указатели топлива и температуры, контрольные лампы. Рисуются в текстуру при старте (свой пиксельный шрифт),
// кладутся на чашу прибора перед стрелкой; ночью подсвечиваются вместе с фарами (GaugeMaterials).
#include "Vehicle/PeregonCar.h"

#include "Art/PeregonArtSubsystem.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "TextureResource.h"

namespace PeregonGaugeDraw
{
constexpr int32 Size = 256;

// Шрифт 5×7: цифры, «x», «/», «к», «м», «ч», «°», «С».
const TCHAR* Glyph(TCHAR C)
{
	switch (C)
	{
	case '0': return TEXT("01110100011001110101110011000101110");
	case '1': return TEXT("00100011000010000100001000010001110");
	case '2': return TEXT("01110100010000100010001000100011111");
	case '3': return TEXT("11111000100010000010000011000101110");
	case '4': return TEXT("00010001100101010010111110001000010");
	case '5': return TEXT("11111100001111000001000011000101110");
	case '6': return TEXT("00110010001000011110100011000101110");
	case '7': return TEXT("11111000010001000100010000100001000");
	case '8': return TEXT("01110100011000101110100011000101110");
	case '9': return TEXT("01110100011000101111000010001001100");
	case 'x': return TEXT("00000000001000101010001000101010001");
	case '/': return TEXT("00001000010001000100010001000010000");
	case TEXT('к'): return TEXT("00000000001001010100110001010010010");
	case TEXT('м'): return TEXT("00000000001000111011101011000110001");
	case TEXT('ч'): return TEXT("00000000001000110001011110000100001");
	case TEXT('С'): return TEXT("01110100011000010000100001000101110");
	case TEXT('°'): return TEXT("01100100101001001100000000000000000");
	default: return nullptr;
	}
}

struct FCanvas
{
	TArray<FColor> Px;
	FCanvas() { Px.Init(FColor(0, 0, 0, 0), Size * Size); }

	void Put(int32 X, int32 Y, const FColor& C)
	{
		if (X >= 0 && Y >= 0 && X < Size && Y < Size)
		{
			Px[Y * Size + X] = C;
		}
	}
	void Disc(float Cx, float Cy, float R, const FColor& C)
	{
		for (int32 Y = FMath::FloorToInt(Cy - R); Y <= FMath::CeilToInt(Cy + R); ++Y)
		{
			for (int32 X = FMath::FloorToInt(Cx - R); X <= FMath::CeilToInt(Cx + R); ++X)
			{
				if (FMath::Square(X + 0.5f - Cx) + FMath::Square(Y + 0.5f - Cy) <= R * R)
				{
					Put(X, Y, C);
				}
			}
		}
	}
	// Отрезок толщиной W.
	void Line(const FVector2f& A, const FVector2f& B, float W, const FColor& C)
	{
		const float Len = FVector2f::Distance(A, B);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len * 2.f));
		for (int32 K = 0; K <= Steps; ++K)
		{
			const FVector2f P = FMath::Lerp(A, B, static_cast<float>(K) / Steps);
			Disc(P.X, P.Y, W * 0.5f, C);
		}
	}
	// Дуга шкалы (красная зона) от A0 до A1 градусов (0 — вверх, по часовой).
	void Arc(float R0, float R1, float A0, float A1, const FColor& C)
	{
		const FVector2f O(Size * 0.5f, Size * 0.5f);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const FVector2f D(X + 0.5f - O.X, Y + 0.5f - O.Y);
				const float R = D.Size();
				const float A = FMath::RadiansToDegrees(FMath::Atan2(D.X, -D.Y));
				if (R >= R0 && R <= R1 && A >= A0 && A <= A1)
				{
					Put(X, Y, C);
				}
			}
		}
	}
	void Text(const FString& Str, float Cx, float Cy, int32 Scale, const FColor& C)
	{
		const int32 Wide = Str.Len() * 6 * Scale - Scale;
		int32 X0 = FMath::RoundToInt(Cx - Wide * 0.5f);
		const int32 Y0 = FMath::RoundToInt(Cy - 3.5f * Scale);
		for (TCHAR Ch : Str)
		{
			if (const TCHAR* G = Glyph(Ch))
			{
				for (int32 Row = 0; Row < 7; ++Row)
				{
					for (int32 Col = 0; Col < 5; ++Col)
					{
						if (G[Row * 5 + Col] == '1')
						{
							for (int32 Sy = 0; Sy < Scale; ++Sy)
							{
								for (int32 Sx = 0; Sx < Scale; ++Sx)
								{
									Put(X0 + Col * Scale + Sx, Y0 + Row * Scale + Sy, C);
								}
							}
						}
					}
				}
			}
			X0 += 6 * Scale;
		}
	}
};

FVector2f Polar(float AngleDeg, float R)
{
	const float A = FMath::DegreesToRadians(AngleDeg);
	return FVector2f(Size * 0.5f + FMath::Sin(A) * R, Size * 0.5f - FMath::Cos(A) * R);
}

// Kind: 0 — спидометр, 1 — тахометр, 2 — топливо, 3 — температура.
UTexture2D* Draw(int32 Kind)
{
	const FColor Face(28, 26, 24, 255);  // чёрная шкала с белыми цифрами, как у «шестёрки»
	const FColor Ink(235, 232, 220, 255);
	const FColor Red(210, 40, 25, 255);
	FCanvas C;
	C.Disc(Size * 0.5f, Size * 0.5f, Size * 0.5f - 1.f, Face);
	const bool bBig = Kind < 2;
	const float Sweep = bBig ? 125.f : 60.f;
	const float ROut = Size * 0.46f;
	auto Ang = [Sweep](float Fraction) { return FMath::Lerp(-Sweep, Sweep, Fraction); };
	if (Kind == 0)
	{
		for (int32 V = 0; V <= 180; V += 5)
		{
			const bool bMajor = V % 20 == 0;
			const bool bMid = V % 10 == 0;
			C.Line(Polar(Ang(V / 180.f), ROut), Polar(Ang(V / 180.f), ROut - (bMajor ? 20.f : (bMid ? 13.f : 8.f))), bMajor ? 4.f : 2.f, Ink);
			if (bMajor)
			{
				const FVector2f P = Polar(Ang(V / 180.f), ROut - 34.f);
				C.Text(FString::FromInt(V), P.X, P.Y, 2, Ink);
			}
		}
		C.Text(TEXT("км/ч"), Size * 0.5f, Size * 0.64f, 2, Ink);
		// Одометр.
		for (int32 K = 0; K < 6; ++K)
		{
			const float X = Size * 0.5f - 32.5f + K * 13.f;
			for (int32 Y = 0; Y < 18; ++Y)
			{
				for (int32 Dx = 0; Dx < 12; ++Dx)
				{
					C.Put(FMath::RoundToInt(X) + Dx - 6, FMath::RoundToInt(Size * 0.75f) + Y - 9, K == 5 ? FColor(220, 215, 200, 255) : FColor(8, 8, 8, 255));
				}
			}
			C.Text(K == 5 ? TEXT("5") : (K == 1 ? TEXT("8") : (K == 2 ? TEXT("1") : (K == 3 ? TEXT("2") : TEXT("0")))), X, Size * 0.75f, 2,
				K == 5 ? FColor(10, 10, 10, 255) : Ink);
		}
		// Лампы: габариты, поворотники (зелёные), дальний (синий).
		C.Disc(Size * 0.38f, Size * 0.88f, 5.f, FColor(40, 110, 40, 255));
		C.Disc(Size * 0.5f, Size * 0.89f, 5.f, FColor(40, 120, 50, 255));
		C.Disc(Size * 0.62f, Size * 0.88f, 5.f, FColor(40, 60, 150, 255));
	}
	else if (Kind == 1)
	{
		C.Arc(ROut - 10.f, ROut, Ang(0.75f), Ang(1.f), Red);
		for (int32 V = 0; V <= 80; V += 5)
		{
			const bool bMajor = V % 10 == 0;
			C.Line(Polar(Ang(V / 80.f), ROut), Polar(Ang(V / 80.f), ROut - (bMajor ? 20.f : 10.f)), bMajor ? 4.f : 2.f, V >= 60 ? Red : Ink);
			if (bMajor && V > 0)
			{
				const FVector2f P = Polar(Ang(V / 80.f), ROut - 34.f);
				C.Text(FString::FromInt(V), P.X, P.Y, 2, V >= 60 ? Red : Ink);
			}
		}
		C.Text(TEXT("x100"), Size * 0.5f, Size * 0.66f, 2, Ink);
		// Лампы: ручник, зарядка (красные), давление масла (жёлтая).
		C.Disc(Size * 0.38f, Size * 0.86f, 5.f, FColor(130, 25, 20, 255));
		C.Disc(Size * 0.5f, Size * 0.88f, 5.f, FColor(130, 25, 20, 255));
		C.Disc(Size * 0.62f, Size * 0.86f, 5.f, FColor(150, 120, 20, 255));
	}
	else
	{
		const bool bFuel = Kind == 2;
		if (bFuel)
		{
			C.Arc(ROut - 22.f, ROut, Ang(0.f), Ang(0.15f), Red);
		}
		else
		{
			C.Arc(ROut - 22.f, ROut, Ang(80.f / 90.f), Ang(1.f), Red);
		}
		for (int32 K = 0; K <= 4; ++K)
		{
			const float F = K / 4.f;
			C.Line(Polar(Ang(F), ROut), Polar(Ang(F), ROut - (K % 2 == 0 ? 30.f : 18.f)), 8.f, Ink);
		}
		// Мелкий прибор — цифры крупнее (он сам меньше).
		const TCHAR* L0 = bFuel ? TEXT("0") : TEXT("50");
		const TCHAR* L1 = bFuel ? TEXT("1/2") : TEXT("90");
		const TCHAR* L2 = bFuel ? TEXT("1") : TEXT("130");
		const float LabelR = ROut - 66.f;
		const FVector2f P0 = Polar(Ang(bFuel ? 0.f : 10.f / 90.f), LabelR);
		const FVector2f P1 = Polar(Ang(bFuel ? 0.5f : 50.f / 90.f), LabelR);
		const FVector2f P2 = Polar(Ang(1.f), LabelR);
		// Подписи в углах шкалы (дальше от центра, где стрелка), значок — внизу по центру.
		const int32 Big = bFuel ? 5 : 3;
		C.Text(L0, P0.X + 8.f, P0.Y + 10.f, Big, Ink);
		C.Text(L1, P1.X, P1.Y, 3, Ink);
		C.Text(L2, P2.X - 8.f, P2.Y + 10.f, Big, bFuel ? Ink : Red);
		C.Text(bFuel ? TEXT("") : TEXT("°С"), Size * 0.5f, Size * 0.8f, 4, Ink);
		if (bFuel)
		{
			// Значок колонки — квадратик с носиком.
			for (int32 Y = 0; Y < 22; ++Y)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					C.Put(Size / 2 - 8 + X, static_cast<int32>(Size * 0.8f) + Y - 11, Ink);
				}
			}
		}
	}
	// Ободок.
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const float R = FVector2f(X + 0.5f - Size * 0.5f, Y + 0.5f - Size * 0.5f).Size();
			if (R > Size * 0.5f - 6.f && R < Size * 0.5f - 1.f)
			{
				C.Put(X, Y, FColor(70, 68, 64, 255));
			}
		}
	}
	UTexture2D* Tex = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->Filter = TF_Trilinear;
	Tex->SRGB = true;
	FTexturePlatformData* Platform = Tex->GetPlatformData();
	FTexture2DMipMap& Mip = Platform->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, C.Px.GetData(), C.Px.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}
} // namespace PeregonGaugeDraw

void APeregonCar::BuildGaugeFaces()
{
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	if (!Art)
	{
		return;
	}
	// Положение приборов — как в модели кузова (kGaugeX = 71.8, kGaugeZ = 41.5).
	struct FGaugeSpot
	{
		float Y;
		float R;
		int32 Kind;
	};
	const FGaugeSpot Spots[] = {{-42.5f, 5.6f, 0}, {-27.5f, 5.6f, 1}, {-51.6f, 2.3f, 2}, {-18.4f, 2.3f, 3}};
	for (const FGaugeSpot& G : Spots)
	{
		UTexture2D* Tex = PeregonGaugeDraw::Draw(G.Kind);
		if (!Tex)
		{
			continue;
		}
		GaugeTextures.Add(Tex);
		FPeregonLook Look(EPeregonTex::White, FLinearColor::White, 0.35f);
		Look.WetMask = 0.f;
		Look.SnowMask = 0.f;
		UMaterialInstanceDynamic* Mid = Art->MakeMaterial(this, Look);
		if (!Mid)
		{
			continue;
		}
		Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
		// Лицом в салон (к −X), между чашей прибора и стрелкой.
		UPeregonArtSubsystem::AddLabelQuad(this, Body, FTransform(FRotator(0.f, 180.f, 0.f), FVector(71.74f, G.Y, 41.5f)), G.R * 2.f, G.R * 2.f, Mid);
		GaugeMaterials.Add(Mid); // подсветка шкал — вместе с фарами
	}
}
