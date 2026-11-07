#include "Art/PeregonArtSubsystem.h"
#include "Game/PeregonGameUserSettings.h"

#include "Components/MeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Styling/CoreStyle.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

static TAutoConsoleVariable<int32> CVarPeregonTextureMips(TEXT("peregon.TextureMips"), 1,
	TEXT("Мип-карты процедурных текстур (1 — да, против ряби вдали; 0 — один уровень)."), ECVF_Default);

namespace PeregonTexGen
{
uint32 Hash(uint32 A, uint32 B, uint32 C)
{
	uint32 H = A * 0xCC9E2D51u ^ (B + 0x6B43A9B5u) * 0x1B873593u;
	H = (H << 13) | (H >> 19);
	H ^= C * 0xE6546B64u;
	H ^= H >> 16;
	H *= 0x85EBCA6Bu;
	H ^= H >> 13;
	H *= 0xC2B2AE35u;
	H ^= H >> 16;
	return H;
}

float Rand(int32 X, int32 Y, uint32 Seed)
{
	return static_cast<float>(Hash(static_cast<uint32>(X), static_cast<uint32>(Y), Seed) >> 8) / 16777216.f;
}

// Гладкий шум, бесшовный по размеру текстуры: решётка с периодом W/Cell × H/Cell.
float TileNoise(int32 X, int32 Y, int32 Cell, int32 W, int32 H, uint32 Seed)
{
	const int32 Pw = FMath::Max(1, W / Cell);
	const int32 Ph = FMath::Max(1, H / Cell);
	const float Fx = static_cast<float>(X) / Cell;
	const float Fy = static_cast<float>(Y) / Cell;
	const int32 X0 = FMath::FloorToInt(Fx);
	const int32 Y0 = FMath::FloorToInt(Fy);
	const float Tx = FMath::SmoothStep(0.f, 1.f, Fx - X0);
	const float Ty = FMath::SmoothStep(0.f, 1.f, Fy - Y0);
	auto V = [&](int32 Ix, int32 Iy) { return Rand(((Ix % Pw) + Pw) % Pw, ((Iy % Ph) + Ph) % Ph, Seed); };
	const float A = FMath::Lerp(V(X0, Y0), V(X0 + 1, Y0), Tx);
	const float B = FMath::Lerp(V(X0, Y0 + 1), V(X0 + 1, Y0 + 1), Tx);
	return FMath::Lerp(A, B, Ty);
}

FColor Rgb(float R, float G, float B)
{
	return FColor(static_cast<uint8>(FMath::Clamp(R, 0.f, 255.f)), static_cast<uint8>(FMath::Clamp(G, 0.f, 255.f)),
		static_cast<uint8>(FMath::Clamp(B, 0.f, 255.f)), 255);
}

FColor Rgba(float R, float G, float B, float A)
{
	FColor C = Rgb(R, G, B);
	C.A = static_cast<uint8>(FMath::Clamp(A, 0.f, 255.f));
	return C;
}

FColor Scale(const FColor& Base, float K)
{
	return Rgb(Base.R * K, Base.G * K, Base.B * K);
}

FColor Gray(float V)
{
	return Rgb(V, V, V);
}

// Асфальт: серый с вкраплениями щебня.
FColor AsphaltPixel(int32 X, int32 Y, int32 W, int32 H)
{
	const float N = TileNoise(X, Y, 8, W, H, 11) - 0.5f;
	const float R = Rand(X, Y, 12);
	float V = 78.f + N * 20.f + (R - 0.5f) * 10.f;
	if (R > 0.93f)
	{
		V += 35.f;
	}
	else if (R < 0.05f)
	{
		V -= 18.f;
	}
	return Rgb(V, V, V * 1.02f);
}

void Draw(EPeregonTex Type, int32& W, int32& H, TArray<FColor>& Px)
{
	W = 64;
	H = 64;
	switch (Type)
	{
	case EPeregonTex::White: W = H = 4; break;
	case EPeregonTex::Paint:
	case EPeregonTex::Plastic:
	case EPeregonTex::Fabric:
	case EPeregonTex::Rubber:
	case EPeregonTex::Glass: W = H = 32; break;
	case EPeregonTex::Chrome: W = H = 16; break;
	case EPeregonTex::RoadMarked: W = 64; H = 128; break;
	case EPeregonTex::BirchBark:
	case EPeregonTex::PineBark: W = 32; H = 64; break;
	case EPeregonTex::GrassTuft: W = 32; H = 32; break;
	case EPeregonTex::Leaf: W = H = 8; break;
	case EPeregonTex::PanelFacade:
	case EPeregonTex::BrickFacade: W = 32; H = 32; break;
	case EPeregonTex::Clouds: W = H = 128; break;
	case EPeregonTex::GlassDirt: W = H = 64; break;
	default: break;
	}
	Px.SetNumUninitialized(W * H);

	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const float R = Rand(X, Y, static_cast<uint32>(Type) * 977u + 3u);
			FColor C = FColor::White;
			switch (Type)
			{
			case EPeregonTex::White:
				break;
			case EPeregonTex::Paint:
				C = Gray(238.f + (TileNoise(X, Y, 8, W, H, 21) - 0.5f) * 14.f + (R - 0.5f) * 8.f);
				break;
			case EPeregonTex::Plastic:
				C = Gray(215.f + (R - 0.5f) * 20.f);
				break;
			case EPeregonTex::Fabric:
				C = Gray((((X >> 1) + (Y >> 1)) & 1 ? 230.f : 192.f) + (R - 0.5f) * 12.f);
				break;
			case EPeregonTex::Rubber:
			{
				const bool bGroove = (Y % 8) < 2 || (X % 16) == 0;
				C = Gray((bGroove ? 60.f : 140.f) * (0.9f + 0.1f * R));
				break;
			}
			case EPeregonTex::Chrome:
				C = Gray(200.f + 50.f * Rand(X, Y / 4, 31));
				break;
			case EPeregonTex::Asphalt:
				C = AsphaltPixel(X, Y, W, H);
				break;
			case EPeregonTex::RoadMarked:
			{
				// Поперёк (X): 64 px = 7 м полотна. Вдоль (Y): 128 px = 12 м (штрих 3 м, разрыв 9 м).
				C = AsphaltPixel(X, Y, W, H);
				// Накатанная колея — чуть темнее и глаже.
				for (int32 Track : {9, 23, 41, 55})
				{
					if (FMath::Abs(X - Track) <= 2)
					{
						C = Scale(C, 0.9f);
					}
				}
				const bool bCenter = (X == 31 || X == 32) && Y < 32;
				const bool bEdge = X == 2 || X == 3 || X == 60 || X == 61;
				if ((bCenter || bEdge) && R > 0.12f) // краска местами стёрлась
				{
					C = Gray(222.f + (R - 0.5f) * 16.f);
				}
				break;
			}
			case EPeregonTex::Gravel:
			{
				const int32 Cx = X / 3;
				const int32 Cy = Y / 3;
				const float Tone = 0.72f + 0.5f * Rand(Cx, Cy, 41);
				C = Rgb(118.f * Tone, 108.f * Tone, 94.f * Tone);
				if (Rand(X, Y, 42) < 0.14f)
				{
					C = Scale(C, 0.6f); // щели между камнями
				}
				break;
			}
			case EPeregonTex::Grass:
			{
				const float N = TileNoise(X, Y, 16, W, H, 51) - 0.5f;
				const float Blade = Rand(X, Y / 3, 52) - 0.5f;
				C = Rgb(70.f + N * 26.f + Blade * 14.f, 98.f + N * 30.f + Blade * 22.f, 42.f + N * 10.f);
				if (R > 0.965f)
				{
					C = Rgb(150.f, 140.f, 62.f); // сухие травинки
				}
				else if (R < 0.04f)
				{
					C = Scale(C, 0.65f);
				}
				break;
			}
			case EPeregonTex::Dirt:
			{
				const float N = TileNoise(X, Y, 16, W, H, 61) - 0.5f;
				C = Rgb(104.f + N * 30.f + (R - 0.5f) * 14.f, 80.f + N * 24.f + (R - 0.5f) * 10.f, 56.f + N * 16.f);
				if (R > 0.97f)
				{
					C = Rgb(150.f, 132.f, 110.f);
				}
				break;
			}
			case EPeregonTex::Mud:
			{
				const float Puddle = TileNoise(X, Y, 32, W, H, 71);
				C = Rgb(62.f + (R - 0.5f) * 10.f, 46.f + (R - 0.5f) * 8.f, 32.f);
				if (Puddle > 0.62f)
				{
					C = Rgb(42.f, 34.f, 28.f);
				}
				break;
			}
			case EPeregonTex::Snow:
			{
				const float N = TileNoise(X, Y, 16, W, H, 81) - 0.5f;
				C = Rgb(226.f + N * 12.f + (R - 0.5f) * 6.f, 231.f + N * 10.f + (R - 0.5f) * 6.f, 241.f + N * 6.f);
				break;
			}
			case EPeregonTex::Ice:
			{
				const float S = FMath::Sin(X * 0.35f + Y * 0.12f + TileNoise(X, Y, 16, W, H, 91) * 6.f);
				C = Rgb(168.f + S * 10.f, 197.f + S * 10.f, 218.f + S * 8.f);
				if (R > 0.985f)
				{
					C = Gray(240.f);
				}
				break;
			}
			case EPeregonTex::Concrete:
				C = (X % 32 == 0 || Y % 32 == 0) ? Gray(110.f)
					: Rgb(152.f + (TileNoise(X, Y, 8, W, H, 101) - 0.5f) * 18.f + (R - 0.5f) * 8.f, 150.f, 144.f);
				break;
			case EPeregonTex::WoodPlanks:
			{
				const int32 Plank = Y / 8;
				const float Tone = 0.82f + 0.25f * Rand(Plank, 0, 111);
				const float Grain = FMath::Sin((X + Rand(Plank, 1, 112) * 64.f) * 0.45f + TileNoise(X, Y, 8, W, H, 113) * 4.f) * 0.07f;
				C = Rgb(210.f * (Tone + Grain), 196.f * (Tone + Grain), 178.f * (Tone + Grain));
				if (Y % 8 == 0)
				{
					C = Scale(C, 0.45f);
				}
				break;
			}
			case EPeregonTex::Plaster:
				C = Rgb(215.f + (TileNoise(X, Y, 8, W, H, 121) - 0.5f) * 16.f + (R - 0.5f) * 8.f, 210.f, 198.f);
				break;
			case EPeregonTex::RoofMetal:
				C = Gray(180.f * (0.78f + 0.22f * FMath::Sin(X * 2.f * PI / 8.f)) + (R - 0.5f) * 6.f);
				if (R > 0.972f)
				{
					C = Rgb(150.f, 92.f, 60.f); // ржавчина
				}
				break;
			case EPeregonTex::BirchBark:
				C = Rgb(226.f + (R - 0.5f) * 10.f, 224.f, 216.f);
				if (Rand(0, Y / 2, 131) > 0.78f && Rand(X / 6, Y / 2, 132) > 0.45f)
				{
					C = Gray(42.f);
				}
				break;
			case EPeregonTex::PineBark:
			{
				const float Streak = Rand(X / 2, 0, 141) - 0.5f;
				C = Rgb(112.f + Streak * 40.f + (R - 0.5f) * 10.f, 70.f + Streak * 24.f, 45.f + Streak * 12.f);
				if (Rand(X / 3, Y / 5, 142) > 0.9f)
				{
					C = Scale(C, 0.55f);
				}
				break;
			}
			case EPeregonTex::Needles:
				C = Rgb(34.f + (R - 0.5f) * 18.f, 62.f + (R - 0.5f) * 26.f + (TileNoise(X, Y, 8, W, H, 151) - 0.5f) * 20.f, 36.f + (R - 0.5f) * 10.f);
				break;
			case EPeregonTex::Leaves:
			{
				const float B = Rand(X / 4, Y / 4, 161);
				C = Rgb(80.f + (B - 0.5f) * 40.f, 118.f + (B - 0.5f) * 46.f + (R - 0.5f) * 10.f, 46.f + (B - 0.5f) * 16.f);
				break;
			}
			case EPeregonTex::Glass:
				C = ((X + Y) % 32) < 4 ? Rgb(80.f, 96.f, 108.f) : Rgb(38.f, 52.f, 62.f);
				break;
			case EPeregonTex::GrassTuft:
			{
				// Травинки растут от нижнего края (V = 1) вверх; каждая — со своим наклоном и высотой.
				C = Rgba(0.f, 0.f, 0.f, 0.f);
				const float Up = static_cast<float>(H - 1 - Y) / static_cast<float>(H); // 0 внизу, 1 вверху
				for (int32 Blade = 0; Blade < 11; ++Blade)
				{
					const float Root = 2.f + 28.f * Rand(Blade, 0, 171);
					const float Lean = (Rand(Blade, 1, 172) - 0.5f) * 14.f;
					const float Height = 0.45f + 0.55f * Rand(Blade, 2, 173);
					const float Bx = Root + Lean * Up * Up;
					const float Width = 1.3f * (1.f - Up / Height) + 0.35f;
					if (Up < Height && FMath::Abs(static_cast<float>(X) - Bx) < Width)
					{
						const float Tip = Up / Height;
						C = Rgba(150.f + 60.f * Tip, 180.f + 50.f * Tip, 120.f + 30.f * Tip, 255.f);
					}
				}
				break;
			}
			case EPeregonTex::Bush:
			{
				// Овал листвы с просветами: шум по краю, тёмная середина, светлые листья по кромке.
				const float Dx = (static_cast<float>(X) - 31.5f) / 31.5f;
				const float Dy = (static_cast<float>(Y) - 36.f) / 30.f;
				const float Dist = FMath::Sqrt(Dx * Dx + Dy * Dy);
				const float Edge = 0.78f + 0.25f * (TileNoise(X, Y, 8, W, H, 181) - 0.5f) * 2.f;
				const bool bHole = Rand(X / 3, Y / 3, 182) > 0.86f;
				if (Dist < Edge && !bHole)
				{
					const float Leaf = Rand(X / 2, Y / 2, 183);
					const float Shade = 0.7f + 0.3f * (1.f - Dy) * 0.5f + (Leaf - 0.5f) * 0.25f;
					C = Rgba(190.f * Shade, 215.f * Shade, 170.f * Shade, 255.f);
				}
				else
				{
					C = Rgba(0.f, 0.f, 0.f, 0.f);
				}
				break;
			}
			case EPeregonTex::Leaf:
			{
				// Ромбик листа с черешком; цвет — светлый, оттенок даёт материал (зелёный, жёлтый, рыжий).
				const float Dx = FMath::Abs(static_cast<float>(X) - 3.5f);
				const float Dy = FMath::Abs(static_cast<float>(Y) - 3.f);
				C = (Dx + Dy * 0.8f < 3.6f) || (X == 4 && Y == 7) ? Rgba(230.f + (R - 0.5f) * 30.f, 225.f, 200.f, 255.f) : Rgba(0.f, 0.f, 0.f, 0.f);
				break;
			}
			case EPeregonTex::Raindrops:
			{
				// Капли разного размера; между ними прозрачно.
				C = Rgba(0.f, 0.f, 0.f, 0.f);
				for (int32 Drop = 0; Drop < 70; ++Drop)
				{
					const float Cx = 64.f * Rand(Drop, 0, 191);
					const float Cy = 64.f * Rand(Drop, 1, 192);
					const float Rad = 0.8f + 2.2f * Rand(Drop, 2, 193) * Rand(Drop, 3, 194);
					float Ddx = FMath::Abs(static_cast<float>(X) - Cx);
					float Ddy = FMath::Abs(static_cast<float>(Y) - Cy);
					Ddx = FMath::Min(Ddx, 64.f - Ddx);
					Ddy = FMath::Min(Ddy, 64.f - Ddy);
					const float D = FMath::Sqrt(Ddx * Ddx + Ddy * Ddy * 0.8f);
					if (D < Rad)
					{
						const bool bHighlight = (static_cast<float>(X) - Cx) < -Rad * 0.2f && (static_cast<float>(Y) - Cy) < -Rad * 0.2f;
						C = bHighlight ? Rgba(250.f, 252.f, 255.f, 235.f) : Rgba(150.f, 165.f, 175.f, 150.f + 80.f * (1.f - D / Rad));
					}
				}
				break;
			}
			case EPeregonTex::PanelFacade:
			{
				// Пролёт панельного дома: 32 px = 3 м × 2.8 м. Швы между панелями, окно с рамой, подоконный пояс.
				const float Panel = 196.f + (TileNoise(X, Y, 8, W, H, 201) - 0.5f) * 16.f + (R - 0.5f) * 10.f;
				C = Gray(Panel);
				if (X == 0 || Y == 0)
				{
					C = Gray(120.f); // шов
				}
				const bool bWindow = X >= 9 && X <= 23 && Y >= 7 && Y <= 21;
				const bool bFrame = bWindow && (X == 9 || X == 23 || Y == 7 || Y == 21 || X == 16);
				if (bFrame)
				{
					C = Gray(225.f);
				}
				else if (bWindow)
				{
					C = Y < 11 ? Rgb(95.f, 112.f, 128.f) : Rgb(52.f, 62.f, 72.f);
				}
				if (Y >= 24 && Y <= 26 && X >= 7 && X <= 25)
				{
					C = Gray(150.f); // отлив
				}
				break;
			}
			case EPeregonTex::BrickFacade:
			{
				const int32 Row = Y / 3;
				const int32 Brick = (X + (Row & 1) * 3) / 6;
				const float Tone = 0.85f + 0.3f * Rand(Brick, Row, 211);
				C = Rgb(168.f * Tone, 82.f * Tone, 58.f * Tone);
				if (Y % 3 == 0 || (X + (Row & 1) * 3) % 6 == 0)
				{
					C = Rgb(176.f, 166.f, 150.f); // раствор
				}
				const bool bWindow = X >= 10 && X <= 22 && Y >= 6 && Y <= 22;
				if (bWindow)
				{
					C = (X == 10 || X == 22 || Y == 6 || Y == 22 || Y == 12) ? Gray(232.f) : Rgb(48.f, 58.f, 66.f);
				}
				if (Y == 5 && X >= 9 && X <= 23)
				{
					C = Rgb(210.f, 200.f, 186.f); // перемычка
				}
				break;
			}
			case EPeregonTex::GlassDirt:
			{
				// Почти прозрачное стекло: пыль пятнами, засохшие капли, разводы — блики видно и из салона.
				const float Dust = 0.5f * TileNoise(X, Y, 16, W, H, 231) + 0.5f * TileNoise(X, Y, 4, W, H, 232);
				const float Streak = FMath::Max(0.f, FMath::Sin((static_cast<float>(X) * 0.35f + static_cast<float>(Y) * 0.12f)) - 0.85f) * 4.f;
				const float Speck = Rand(X, Y, 233) > 0.985f ? 0.6f : 0.f;
				const float A = FMath::Clamp(0.55f + 0.35f * Dust + 0.25f * Streak + Speck, 0.f, 1.f);
				C = Rgba(205.f + 30.f * Dust, 208.f + 25.f * Dust, 200.f + 20.f * Dust, 255.f * A);
				break;
			}
			case EPeregonTex::Clouds:
			{
				// Кучевые: несколько октав бесшовного шума, порог — края облаков, низ чуть темнее.
				const float N = 0.5f * TileNoise(X, Y, 32, W, H, 221) + 0.3f * TileNoise(X, Y, 16, W, H, 222) + 0.2f * TileNoise(X, Y, 8, W, H, 223);
				const float A = FMath::SmoothStep(0.48f, 0.68f, N);
				const float Shade = 175.f + 80.f * FMath::SmoothStep(0.5f, 0.8f, N);
				C = Rgba(Shade, Shade, Shade * 1.03f, 255.f * A);
				break;
			}
			default:
				break;
			}
			Px[Y * W + X] = C;
		}
	}
}

// «Глубина и качество» (Настройки → Детализация текстур): исходный рисунок увеличивается в Factor раз
// с мягкой интерполяцией, поверх — мелкая фактура (зерно бумаги/мазка) и рельеф: яркость считается высотой,
// свет падает слева сверху — швы, щели и камни выглядят вдавленными, а не нарисованными.
void Enhance(EPeregonTex Type, int32& W, int32& H, TArray<FColor>& Px, int32 Factor)
{
	if (Factor <= 1 || Type == EPeregonTex::White || W * Factor > 1024 || H * Factor > 1024)
	{
		return;
	}
	const bool bAlpha = Type == EPeregonTex::GrassTuft || Type == EPeregonTex::Bush || Type == EPeregonTex::Leaf || Type == EPeregonTex::Raindrops ||
		Type == EPeregonTex::Clouds || Type == EPeregonTex::GlassDirt || Type == EPeregonTex::Glass;
	const bool bSoft = Type == EPeregonTex::Clouds || Type == EPeregonTex::GlassDirt || Type == EPeregonTex::Raindrops;
	const int32 Nw = W * Factor;
	const int32 Nh = H * Factor;
	TArray<FLinearColor> Src;
	Src.SetNumUninitialized(W * H);
	for (int32 I = 0; I < W * H; ++I)
	{
		Src[I] = FLinearColor(Px[I].R, Px[I].G, Px[I].B, Px[I].A);
	}
	auto At = [&](int32 X, int32 Y) -> const FLinearColor& { return Src[((Y % H + H) % H) * W + ((X % W + W) % W)]; };
	TArray<FLinearColor> Big;
	Big.SetNumUninitialized(Nw * Nh);
	for (int32 Y = 0; Y < Nh; ++Y)
	{
		for (int32 X = 0; X < Nw; ++X)
		{
			// Билинейно, но с «плечом» smoothstep: края пятен мягкие, но не мыльные.
			const float Fx = (X + 0.5f) / Factor - 0.5f;
			const float Fy = (Y + 0.5f) / Factor - 0.5f;
			const int32 X0 = FMath::FloorToInt(Fx);
			const int32 Y0 = FMath::FloorToInt(Fy);
			const float Tx = FMath::SmoothStep(0.f, 1.f, Fx - X0);
			const float Ty = FMath::SmoothStep(0.f, 1.f, Fy - Y0);
			const FLinearColor A = FMath::Lerp(At(X0, Y0), At(X0 + 1, Y0), Tx);
			const FLinearColor B = FMath::Lerp(At(X0, Y0 + 1), At(X0 + 1, Y0 + 1), Tx);
			Big[Y * Nw + X] = FMath::Lerp(A, B, Ty);
		}
	}
	const uint32 Seed = static_cast<uint32>(Type) * 7349u + 11u;
	const float Relief = bSoft ? 0.f : (Type == EPeregonTex::Paint || Type == EPeregonTex::Glass || Type == EPeregonTex::Chrome ? 0.25f : 1.f);
	auto Lum = [&](int32 X, int32 Y) {
		const FLinearColor& C = Big[((Y % Nh + Nh) % Nh) * Nw + ((X % Nw + Nw) % Nw)];
		return (C.R * 0.3f + C.G * 0.55f + C.B * 0.15f) * (bAlpha ? C.A / 255.f : 1.f);
	};
	Px.SetNumUninitialized(Nw * Nh);
	for (int32 Y = 0; Y < Nh; ++Y)
	{
		for (int32 X = 0; X < Nw; ++X)
		{
			FLinearColor C = Big[Y * Nw + X];
			// Рельеф: разность высот вдоль диагонали света.
			const float Slope = (Lum(X - 1, Y - 1) - Lum(X + 1, Y + 1)) * 0.5f;
			const float Shade = FMath::Clamp(Slope * 0.55f * Relief, -40.f, 40.f);
			// Фактура: два слоя бесшовного шума (крупный мазок и мелкое зерно).
			const float Stroke = (TileNoise(X, Y, FMath::Max(2, Factor * 3), Nw, Nh, Seed) - 0.5f) * 10.f;
			const float Grain = (Rand(X, Y, Seed + 1u) - 0.5f) * 7.f;
			const float D = bSoft ? Grain * 0.3f : Shade + Stroke + Grain;
			C.R = FMath::Clamp(C.R + D, 0.f, 255.f);
			C.G = FMath::Clamp(C.G + D, 0.f, 255.f);
			C.B = FMath::Clamp(C.B + D * 0.92f, 0.f, 255.f);
			Px[Y * Nw + X] = FColor(static_cast<uint8>(C.R), static_cast<uint8>(C.G), static_cast<uint8>(C.B), static_cast<uint8>(FMath::Clamp(C.A, 0.f, 255.f)));
		}
	}
	W = Nw;
	H = Nh;
}
} // namespace PeregonTexGen

UPeregonArtSubsystem* UPeregonArtSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UPeregonArtSubsystem>() : nullptr;
}

void UPeregonArtSubsystem::ApplyLook(const UObject* WorldContext, UMeshComponent* Mesh, const FPeregonLook& Look)
{
	UPeregonArtSubsystem* Art = Get(WorldContext);
	if (!Art || !Mesh)
	{
		return;
	}
	UMaterialInstanceDynamic* Mid = Art->MakeMaterial(Mesh, Look);
	if (!Mid)
	{
		return;
	}
	const int32 Slots = FMath::Max(1, Mesh->GetNumMaterials());
	for (int32 I = 0; I < Slots; ++I)
	{
		Mesh->SetMaterial(I, Mid);
	}
}

void UPeregonArtSubsystem::EnsureBaseMaterial()
{
	if (bTriedLoad)
	{
		return;
	}
	bTriedLoad = true;
	// Сначала второе поколение (погода, ветер, альфа), потом старое.
	// «Живопись» — третье поколение (рельеф по текстуре и лужи), если редактор его уже создал.
	if (UPeregonGameUserSettings::Get()->VisualStyle == 0)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PSX4.M_PSX4"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PSX3.M_PSX3"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	bPsx3 = BaseMaterial != nullptr;
	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Peregon: нет M_PSX3 (погода, ветер, трава) — перезапустите редактор, его создаст PeregonEditor. Пока — M_PSX2."));
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PSX2.M_PSX2"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	bPsx = BaseMaterial != nullptr;
	LabelMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PSX2.M_PSX2"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!LabelMaterial)
	{
		LabelMaterial = BaseMaterial;
	}
	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Peregon: нет /Game/Peregon/Materials/M_PSX2 — его создаёт модуль PeregonEditor при запуске редактора. Пока — без текстур."));
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	GlassMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PSX3_Glass.M_PSX3_Glass"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	bGlass3 = GlassMaterial != nullptr;
	if (!GlassMaterial)
	{
		GlassMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PSX2_Glass.M_PSX2_Glass"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!GlassMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Peregon: нет M_PSX2_Glass — стёкла машины будут скрыты (материал создаёт PeregonEditor)."));
	}
}

UMaterialInstanceDynamic* UPeregonArtSubsystem::MakeGlassMaterial(UObject* Outer, const FLinearColor& Tint, float Opacity)
{
	EnsureBaseMaterial();
	if (!GlassMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(GlassMaterial, Outer ? Outer : this);
	if (Mid)
	{
		Mid->SetTextureParameterValue(TEXT("Tex"), GetTexture(EPeregonTex::White));
		Mid->SetVectorParameterValue(TEXT("Color"), Tint);
		Mid->SetScalarParameterValue(TEXT("Roughness"), 0.05f);
		Mid->SetScalarParameterValue(TEXT("Metallic"), 0.f);
		Mid->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Mid->SetScalarParameterValue(TEXT("WorldUV"), 0.f);
	}
	return Mid;
}

bool UPeregonArtSubsystem::HasPsxMaterial()
{
	EnsureBaseMaterial();
	return bPsx;
}

bool UPeregonArtSubsystem::HasWeatherMaterials()
{
	EnsureBaseMaterial();
	return bPsx3 && bGlass3;
}

UMaterialInstanceDynamic* UPeregonArtSubsystem::MakeTranslucent(UObject* Outer, EPeregonTex Texture, const FLinearColor& Tint, float Opacity, float Emissive,
	float NearFadeCm)
{
	EnsureBaseMaterial();
	if (!GlassMaterial || !bGlass3)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(GlassMaterial, Outer ? Outer : this);
	if (Mid)
	{
		Mid->SetTextureParameterValue(TEXT("Tex"), GetTexture(Texture));
		Mid->SetVectorParameterValue(TEXT("Color"), Tint);
		Mid->SetScalarParameterValue(TEXT("Roughness"), 0.1f);
		Mid->SetScalarParameterValue(TEXT("Metallic"), 0.f);
		Mid->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Mid->SetScalarParameterValue(TEXT("Emissive"), Emissive);
		Mid->SetScalarParameterValue(TEXT("NearFade"), NearFadeCm);
		Mid->SetScalarParameterValue(TEXT("WorldUV"), 0.f);
	}
	return Mid;
}

void UPeregonArtSubsystem::ApplyWeather(UMaterialInstanceDynamic* Mid) const
{
	Mid->SetScalarParameterValue(TEXT("Wetness"), CurWetness);
	Mid->SetScalarParameterValue(TEXT("Snow"), CurSnow);
	Mid->SetVectorParameterValue(TEXT("Wind"), FLinearColor(CurWind.X, CurWind.Y, CurWind.Z, 0.f));
}

void UPeregonArtSubsystem::SetWorldParams(float Wetness, float Snow, const FVector& WindCm, float Night)
{
	const bool bWeather = FMath::Abs(Wetness - CurWetness) > 0.01f || FMath::Abs(Snow - CurSnow) > 0.01f || !WindCm.Equals(CurWind, 0.5f);
	const bool bNight = FMath::Abs(Night - CurNight) > 0.01f;
	if (bWeather)
	{
		CurWetness = Wetness;
		CurSnow = Snow;
		CurWind = WindCm;
		WeatherMids.RemoveAllSwap([](const TWeakObjectPtr<UMaterialInstanceDynamic>& M) { return !M.IsValid(); });
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : WeatherMids)
		{
			ApplyWeather(M.Get());
		}
	}
	if (bNight)
	{
		CurNight = Night;
		GlowMids.RemoveAllSwap([](const FGlow& G) { return !G.Mid.IsValid(); });
		for (const FGlow& G : GlowMids)
		{
			G.Mid->SetScalarParameterValue(TEXT("Emissive"), G.Base + G.Glow * CurNight);
		}
	}
}

UTexture2D* UPeregonArtSubsystem::GetTexture(EPeregonTex Type)
{
	const int32 Index = static_cast<int32>(Type);
	if (Index < 0 || Index >= static_cast<int32>(EPeregonTex::Count))
	{
		return nullptr;
	}
	if (Textures.Num() <= Index)
	{
		Textures.SetNum(static_cast<int32>(EPeregonTex::Count));
		AverageColors.SetNum(static_cast<int32>(EPeregonTex::Count));
	}
	if (!Textures[Index])
	{
		Textures[Index] = Generate(Type);
	}
	return Textures[Index];
}

UTexture2D* UPeregonArtSubsystem::Generate(EPeregonTex Type)
{
	int32 W = 0;
	int32 H = 0;
	TArray<FColor> Pixels;
	PeregonTexGen::Draw(Type, W, H, Pixels);
	{
		const UPeregonGameUserSettings* Settings = UPeregonGameUserSettings::Get();
		static const int32 Factors[] = {1, 4, 8};
		if (Settings->VisualStyle == 0)
		{
			PeregonTexGen::Enhance(Type, W, H, Pixels, Factors[FMath::Clamp(Settings->TextureDetail, 0, 2)]);
		}
	}

	FLinearColor Sum(0.f, 0.f, 0.f, 0.f);
	for (const FColor& C : Pixels)
	{
		Sum += FLinearColor(C);
	}
	AverageColors[static_cast<int32>(Type)] = Pixels.Num() > 0 ? Sum / static_cast<float>(Pixels.Num()) : FLinearColor::White;

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	// «Живопись»: мягкая фильтрация, мазки сливаются; «PSX»: крупные пиксели без размытия.
	Tex->Filter = UPeregonGameUserSettings::Get()->VisualStyle == 1 ? TF_Nearest : TF_Trilinear;
	Tex->SRGB = true;
	Tex->LODGroup = TEXTUREGROUP_World;
	FTexturePlatformData* Platform = Tex->GetPlatformData();
	{
		FTexture2DMipMap& Mip = Platform->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
	}
	// Мип-карты — от ряби вдали: мелкий узор асфальта и травы на расстоянии усредняется, а не мерцает.
	// Уровни считаются здесь (среднее 2×2) и кладутся рядом с нулевым; peregon.TextureMips 0 — выключить.
	if (CVarPeregonTextureMips.GetValueOnGameThread() != 0 && Platform->Mips.Num() == 1)
	{
		TArray<FColor> Prev = Pixels;
		int32 Pw = W;
		int32 Ph = H;
		while (Pw > 4 && Ph > 4)
		{
			const int32 Nw = Pw / 2;
			const int32 Nh = Ph / 2;
			TArray<FColor> Next;
			Next.SetNumUninitialized(Nw * Nh);
			for (int32 Y = 0; Y < Nh; ++Y)
			{
				for (int32 X = 0; X < Nw; ++X)
				{
					uint32 Acc[4] = {0, 0, 0, 0};
					for (int32 K = 0; K < 4; ++K)
					{
						const FColor& P = Prev[(Y * 2 + K / 2) * Pw + X * 2 + K % 2];
						Acc[0] += P.R;
						Acc[1] += P.G;
						Acc[2] += P.B;
						Acc[3] += P.A;
					}
					Next[Y * Nw + X] = FColor(static_cast<uint8>(Acc[0] / 4), static_cast<uint8>(Acc[1] / 4), static_cast<uint8>(Acc[2] / 4),
						static_cast<uint8>(Acc[3] / 4));
				}
			}
			FTexture2DMipMap* Mip = new FTexture2DMipMap();
			Mip->SizeX = Nw;
			Mip->SizeY = Nh;
			Mip->SizeZ = 1;
			Mip->BulkData.Lock(LOCK_READ_WRITE);
			void* Data = Mip->BulkData.Realloc(static_cast<int64>(Next.Num()) * static_cast<int64>(sizeof(FColor)));
			FMemory::Memcpy(Data, Next.GetData(), Next.Num() * sizeof(FColor));
			Mip->BulkData.Unlock();
			Platform->Mips.Add(Mip);
			Prev = MoveTemp(Next);
			Pw = Nw;
			Ph = Nh;
		}
	}
	Tex->UpdateResource();
	return Tex;
}

UMaterialInstanceDynamic* UPeregonArtSubsystem::MakeMaterial(UObject* Outer, const FPeregonLook& Look)
{
	EnsureBaseMaterial();
	if (!BaseMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMaterial, Outer ? Outer : this);
	if (!Mid)
	{
		return nullptr;
	}
	UTexture2D* Tex = GetTexture(Look.Texture);
	if (bPsx)
	{
		Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
		Mid->SetVectorParameterValue(TEXT("Color"), Look.Tint);
		Mid->SetScalarParameterValue(TEXT("Roughness"), Look.Roughness);
		Mid->SetScalarParameterValue(TEXT("Metallic"), Look.Metallic);
		Mid->SetScalarParameterValue(TEXT("Emissive"), Look.Emissive);
		Mid->SetScalarParameterValue(TEXT("WorldUV"), Look.WorldTileCm > 0.f ? 1.f : 0.f);
		Mid->SetScalarParameterValue(TEXT("TileSize"), Look.WorldTileCm > 0.f ? Look.WorldTileCm : 400.f);
		// Глубина рельефа (M_PSX4), см: кирпич, кора и камень — глубоко, краска, хром и пластик — почти гладко.
		{
			float Relief = 0.6f;
			switch (Look.Texture)
			{
			case EPeregonTex::Paint:
			case EPeregonTex::Chrome:
			case EPeregonTex::Glass:
			case EPeregonTex::White:
			case EPeregonTex::Plastic: Relief = 0.06f; break;
			case EPeregonTex::BrickFacade:
			case EPeregonTex::BirchBark:
			case EPeregonTex::PineBark:
			case EPeregonTex::Gravel: Relief = 1.6f; break;
			case EPeregonTex::Asphalt:
			case EPeregonTex::RoadMarked:
			case EPeregonTex::Concrete:
			case EPeregonTex::WoodPlanks:
			case EPeregonTex::Dirt:
			case EPeregonTex::Mud: Relief = 1.0f; break;
			default: break;
			}
			Mid->SetScalarParameterValue(TEXT("Relief"), Relief);
		}
		if (bPsx3)
		{
			// Как поверхность ведёт себя в непогоду, если не сказано явно.
			float Wet = 0.f;
			float SnowCover = 0.f;
			switch (Look.Texture)
			{
			case EPeregonTex::Asphalt:
			case EPeregonTex::RoadMarked: Wet = 1.f; SnowCover = 0.55f; break;
			case EPeregonTex::Gravel:
			case EPeregonTex::Dirt:
			case EPeregonTex::Mud: Wet = 0.8f; SnowCover = 0.9f; break;
			case EPeregonTex::Grass:
			case EPeregonTex::GrassTuft: Wet = 0.45f; SnowCover = 1.f; break;
			case EPeregonTex::Concrete:
			case EPeregonTex::Plaster:
			case EPeregonTex::PanelFacade:
			case EPeregonTex::BrickFacade: Wet = 0.7f; SnowCover = 0.85f; break;
			case EPeregonTex::RoofMetal: Wet = 1.f; SnowCover = 1.f; break;
			case EPeregonTex::WoodPlanks: Wet = 0.6f; SnowCover = 0.8f; break;
			case EPeregonTex::Needles:
			case EPeregonTex::Leaves:
			case EPeregonTex::Bush: Wet = 0.3f; SnowCover = 0.75f; break;
			case EPeregonTex::BirchBark:
			case EPeregonTex::PineBark: Wet = 0.5f; SnowCover = 0.25f; break;
			default: break;
			}
			Wet = Look.WetMask >= 0.f ? Look.WetMask : Wet;
			SnowCover = Look.SnowMask >= 0.f ? Look.SnowMask : SnowCover;
			Mid->SetScalarParameterValue(TEXT("WetMask"), Wet);
			Mid->SetScalarParameterValue(TEXT("SnowMask"), SnowCover);
			Mid->SetScalarParameterValue(TEXT("Sway"), Look.Sway);
			if (Wet > 0.f || SnowCover > 0.f || Look.Sway > 0.f)
			{
				ApplyWeather(Mid);
				WeatherMids.Add(Mid);
			}
		}
		if (Look.NightGlow > 0.f)
		{
			Mid->SetScalarParameterValue(TEXT("Emissive"), Look.Emissive + Look.NightGlow * CurNight);
			GlowMids.Add({Mid, Look.Emissive, Look.NightGlow});
		}
	}
	else
	{
		// Запасной вариант: средний цвет текстуры × оттенок.
		const int32 Index = static_cast<int32>(Look.Texture);
		const FLinearColor Avg = AverageColors.IsValidIndex(Index) ? AverageColors[Index] : FLinearColor::White;
		Mid->SetVectorParameterValue(TEXT("Color"), Avg * Look.Tint);
	}
	return Mid;
}

// ---------------------------------------------------------------------------------------------
// Шрифты и надписи
// ---------------------------------------------------------------------------------------------
TWeakObjectPtr<UPeregonArtSubsystem> UPeregonArtSubsystem::Instance;

namespace PeregonFonts
{
FString Path(EPeregonFont Style)
{
	const TCHAR* File = TEXT("GolosText-Regular.ttf");
	if (Style == EPeregonFont::SemiBold)
	{
		File = TEXT("GolosText-SemiBold.ttf");
	}
	else if (Style == EPeregonFont::Display)
	{
		File = TEXT("Unbounded-Bold.ttf");
	}
	return FPaths::ProjectContentDir() / TEXT("Fonts") / File;
}
} // namespace PeregonFonts

void UPeregonArtSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Instance = this;
}

void UPeregonArtSubsystem::Deinitialize()
{
	if (Instance.Get() == this)
	{
		Instance.Reset();
	}
	Super::Deinitialize();
}

FSlateFontInfo UPeregonArtSubsystem::SlateFont(EPeregonFont Style, int32 Size)
{
	static TSharedPtr<const FCompositeFont> Fonts[3];
	static bool bTried[3] = {false, false, false};
	const int32 Index = FMath::Clamp(static_cast<int32>(Style), 0, 2);
	if (!bTried[Index])
	{
		bTried[Index] = true;
		const FString File = PeregonFonts::Path(Style);
		if (FPaths::FileExists(File))
		{
			Fonts[Index] = MakeShared<FStandaloneCompositeFont>(NAME_None, File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Peregon: не найден шрифт %s"), *File);
		}
	}
	if (Fonts[Index].IsValid())
	{
		return FSlateFontInfo(Fonts[Index], Size);
	}
	return FCoreStyle::GetDefaultFontStyle(Style == EPeregonFont::Regular ? "Regular" : "Bold", Size);
}

FVector2D UPeregonArtSubsystem::MeasureText(const FString& Text, EPeregonFont Style, int32 Size)
{
	if (!FSlateApplication::IsInitialized())
	{
		return FVector2D(Text.Len() * Size * 0.62, Size * 1.3);
	}
	const auto Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, SlateFont(Style, Size));
	return FVector2D(Measured.X, Measured.Y);
}

UFont* UPeregonArtSubsystem::CanvasFont(EPeregonFont Style, int32 Size)
{
	UPeregonArtSubsystem* Self = Instance.Get();
	if (!Self)
	{
		return nullptr;
	}
	const int32 Key = static_cast<int32>(Style) * 1000 + Size;
	if (TObjectPtr<UFont>* Found = Self->CanvasFonts.Find(Key))
	{
		return Found->Get();
	}
	UFont* Font = nullptr;
	const FString File = PeregonFonts::Path(Style);
	if (FPaths::FileExists(File))
	{
		Font = NewObject<UFont>(Self);
		Font->FontCacheType = EFontCacheType::Runtime;
		Font->LegacyFontSize = Size;
		FTypefaceEntry& Entry = Font->CompositeFont.DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = TEXT("Regular");
		Entry.Font = FFontData(File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	}
	Self->CanvasFonts.Add(Key, Font);
	return Font;
}

UMaterialInstanceDynamic* UPeregonArtSubsystem::MakeLabelMaterial(UObject* Outer, const FString& Text, const FLinearColor& Ink, const FLinearColor& Paper,
	int32 WidthPx, int32 HeightPx, int32 FontPx, EPeregonFont Style)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	const FString Key = FString::Printf(TEXT("%s|%s|%s|%d|%d|%d|%d"), *Text, *Ink.ToString(), *Paper.ToString(), WidthPx, HeightPx, FontPx, static_cast<int32>(Style));
	UTextureRenderTarget2D* Target = nullptr;
	if (TObjectPtr<UTextureRenderTarget2D>* Found = LabelCache.Find(Key))
	{
		Target = Found->Get();
	}
	if (!Target)
	{
		Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, WidthPx, HeightPx, RTF_RGBA8_SRGB, Paper, false);
		if (!Target)
		{
			return nullptr;
		}
		UKismetRenderingLibrary::ClearRenderTarget2D(World, Target, Paper);
		UCanvas* Canvas = nullptr;
		FVector2D Size(WidthPx, HeightPx);
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Target, Canvas, Size, Context);
		if (Canvas)
		{
			UFont* Font = CanvasFont(Style, FontPx);
			if (!Font && GEngine)
			{
				Font = GEngine->GetLargeFont();
			}
			// Надпись всегда помещается: при необходимости переносим на две строки и уменьшаем шрифт.
			TArray<FString> Lines;
			Text.ParseIntoArray(Lines, TEXT("\n"), true);
			if (Lines.Num() == 0)
			{
				Lines.Add(Text);
			}
			const float MaxW = static_cast<float>(Size.X) * 0.9f;
			const float MaxH = static_cast<float>(Size.Y) * 0.84f;
			auto Measure = [Canvas](UFont* F, const TArray<FString>& In, float& OutW, float& OutH) {
				OutW = 0.f;
				OutH = 0.f;
				for (const FString& L : In)
				{
					float LW = 0.f;
					float LH = 0.f;
					Canvas->TextSize(F, L, LW, LH);
					OutW = FMath::Max(OutW, LW);
					OutH += LH;
				}
			};
			// Длинную однострочную надпись разбиваем по пробелу ближе к середине, если так крупнее.
			if (Lines.Num() == 1 && Lines[0].Contains(TEXT(" ")))
			{
				float W1 = 0.f;
				float H1 = 0.f;
				Measure(Font, Lines, W1, H1);
				if (W1 > MaxW)
				{
					const FString& One = Lines[0];
					int32 Best = INDEX_NONE;
					for (int32 I = 0; I < One.Len(); ++I)
					{
						if (One[I] == TEXT(' ') && (Best == INDEX_NONE || FMath::Abs(I - One.Len() / 2) < FMath::Abs(Best - One.Len() / 2)))
						{
							Best = I;
						}
					}
					const TArray<FString> Two = {One.Left(Best), One.Mid(Best + 1)};
					float W2 = 0.f;
					float H2 = 0.f;
					Measure(Font, Two, W2, H2);
					const float Scale1 = FMath::Min(MaxW / FMath::Max(W1, 1.f), MaxH / FMath::Max(H1, 1.f));
					const float Scale2 = FMath::Min(MaxW / FMath::Max(W2, 1.f), MaxH / FMath::Max(H2, 1.f));
					if (Scale2 > Scale1)
					{
						Lines = Two;
					}
				}
			}
			int32 Px = FontPx;
			float TextW = 0.f;
			float TextH = 0.f;
			for (;;)
			{
				Measure(Font, Lines, TextW, TextH);
				if ((TextW <= MaxW && TextH <= MaxH) || Px <= 8)
				{
					break;
				}
				const float Fit = FMath::Min(MaxW / FMath::Max(TextW, 1.f), MaxH / FMath::Max(TextH, 1.f));
				Px = FMath::Max(8, FMath::Min(Px - 1, FMath::FloorToInt(static_cast<float>(Px) * Fit)));
				if (UFont* Smaller = CanvasFont(Style, Px))
				{
					Font = Smaller;
				}
				else
				{
					break;
				}
			}
			Canvas->SetDrawColor(Ink.ToFColor(true));
			float Y = (static_cast<float>(Size.Y) - TextH) * 0.5f;
			for (const FString& L : Lines)
			{
				float LW = 0.f;
				float LH = 0.f;
				Canvas->TextSize(Font, L, LW, LH);
				Canvas->DrawText(Font, L, (static_cast<float>(Size.X) - LW) * 0.5f, Y);
				Y += LH;
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
		LabelCache.Add(Key, Target);
	}
	EnsureBaseMaterial();
	UMaterialInstanceDynamic* Mid = nullptr;
	if (bPsx && LabelMaterial)
	{
		Mid = UMaterialInstanceDynamic::Create(LabelMaterial, Outer ? Outer : this);
		if (Mid)
		{
			Mid->SetTextureParameterValue(TEXT("Tex"), Target);
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
			Mid->SetScalarParameterValue(TEXT("Roughness"), 0.7f);
			Mid->SetScalarParameterValue(TEXT("Metallic"), 0.f);
			Mid->SetScalarParameterValue(TEXT("Emissive"), 0.f);
			Mid->SetScalarParameterValue(TEXT("WorldUV"), 0.f);
		}
	}
	else
	{
		Mid = MakeMaterial(Outer, FPeregonLook(EPeregonTex::White, FLinearColor::White, 0.7f));
	}
	return Mid;
}

UProceduralMeshComponent* UPeregonArtSubsystem::AddLabelQuad(AActor* Owner, USceneComponent* Parent, const FTransform& Relative, float WidthCm, float HeightCm,
	UMaterialInterface* Material)
{
	if (!Owner)
	{
		return nullptr;
	}
	UProceduralMeshComponent* Quad = NewObject<UProceduralMeshComponent>(Owner);
	Quad->SetMobility(EComponentMobility::Movable);
	Quad->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
	Quad->SetRelativeTransform(Relative);
	Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Quad->RegisterComponent();
	// Лицом к +X: для зрителя, смотрящего вдоль -X, +Y — слева.
	const float W = WidthCm * 0.5f;
	const float H = HeightCm * 0.5f;
	const TArray<FVector> Vertices = {FVector(0.0, W, H), FVector(0.0, -W, H), FVector(0.0, W, -H), FVector(0.0, -W, -H)};
	const TArray<int32> Triangles = {0, 2, 1, 1, 2, 3};
	const TArray<FVector> Normals = {FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector};
	const TArray<FVector2D> UVs = {FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(1.0, 1.0)};
	Quad->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	if (Material)
	{
		Quad->SetMaterial(0, Material);
	}
	return Quad;
}
