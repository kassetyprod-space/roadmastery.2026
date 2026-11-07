// Городская застройка по видам: жилые дома (панель, плитка, лоджии, силикатный кирпич, «сталинки»),
// магазины десятков видов, гаражи, заводы, школы и детсады, дом культуры, администрация, поликлиника,
// мемориал, водонапорная башня, киоски, рынок. Всё — процедурные коробки и четырёхугольники в общих
// буферах (FPeregonTownBuffers): один меш на материал на город. Вывески и таблички — надписи (Label).
#include "World/PeregonKit.h"

#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "rgen/Noise.h"

namespace PeregonTown
{
FColor Col(float R, float G, float B)
{
	return FColor(static_cast<uint8>(FMath::Clamp(R, 0.f, 1.f) * 255.f), static_cast<uint8>(FMath::Clamp(G, 0.f, 1.f) * 255.f),
		static_cast<uint8>(FMath::Clamp(B, 0.f, 1.f) * 255.f), 255);
}
FColor Col(const FLinearColor& C) { return Col(C.R, C.G, C.B); }

// Система координат здания: X — вдоль длинной стороны, Y — поперёк (−Y — двор и подъезды), Z — вверх, см.
struct FBldFrame
{
	FVector Base;
	float Yaw = 0.f;
	FVector Ax;
	FVector Ay;
	FVector At(float X, float Y, float Z) const { return Base + Ax * X + Ay * Y + FVector(0.f, 0.f, Z); }
	FVector Dir(float X, float Y) const { return (Ax * X + Ay * Y).GetSafeNormal(); }
};

FBldFrame MakeFrame(const rgen::Building& B)
{
	FBldFrame F;
	F.Base = FVector(B.X, B.Y, B.Z) * 100.0;
	F.Yaw = FMath::RadiansToDegrees(static_cast<float>(B.Yaw));
	const FRotator Rot(0.f, F.Yaw, 0.f);
	F.Ax = Rot.RotateVector(FVector::ForwardVector);
	F.Ay = Rot.RotateVector(FVector::RightVector);
	return F;
}

// Коробка в системе здания: основание по центру (X, Y, Z), размеры по осям здания (+ поворот DYaw).
void TBox(FPeregonMeshBuffers& Buf, const FBldFrame& F, float X, float Y, float Z, const FVector& Size, const FColor& C, bool bTop = true,
	bool bBottom = false, float DYaw = 0.f, const FVector2D& Tile = FVector2D(100.f, 100.f))
{
	Buf.AddBox(F.At(X, Y, Z), F.Yaw + DYaw, Size, Tile, C, bTop, bBottom);
}

void Quad(FPeregonMeshBuffers& Buf, const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& N, const FColor& Color)
{
	Buf.AddQuad(A, B, C, D, N, FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), Color, 0);
}

// Призма (цилиндр, конус, бочка): ось From → To, радиусы на концах, Sides граней, крышки по желанию.
void Prism(FPeregonMeshBuffers& Buf, const FVector& From, const FVector& To, float R0, float R1, int32 Sides, const FColor& C, bool bCapTop = true)
{
	const FVector Axis = (To - From).GetSafeNormal();
	const FVector Ref = FMath::Abs(Axis.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector;
	const FVector U = FVector::CrossProduct(Axis, Ref).GetSafeNormal();
	const FVector V = FVector::CrossProduct(Axis, U);
	for (int32 K = 0; K < Sides; ++K)
	{
		const float A0 = 2.f * PI * K / Sides;
		const float A1 = 2.f * PI * (K + 1) / Sides;
		const FVector D0 = U * FMath::Cos(A0) + V * FMath::Sin(A0);
		const FVector D1 = U * FMath::Cos(A1) + V * FMath::Sin(A1);
		const FVector N = (D0 + D1).GetSafeNormal();
		Quad(Buf, From + D0 * R0, From + D1 * R0, To + D1 * R1 + D1 * 0.001f, To + D0 * R1, N, C);
		if (bCapTop && R1 > 1.f)
		{
			Quad(Buf, To, To + D0 * R1, To + D1 * R1, To, Axis, C);
		}
	}
}

// Пятиконечная звезда (плоская, лицом по N).
void Star(FPeregonMeshBuffers& Buf, const FVector& Center, const FVector& Up, const FVector& Right, float R, const FColor& C)
{
	const FVector N = FVector::CrossProduct(Up, Right).GetSafeNormal();
	for (int32 K = 0; K < 5; ++K)
	{
		auto P = [&](float Ang, float Rad) { return Center + (Up * FMath::Cos(Ang) + Right * FMath::Sin(Ang)) * Rad; };
		const float A = 2.f * PI * K / 5.f;
		const FVector Tip = P(A, R);
		const FVector L = P(A - PI / 5.f, R * 0.4f);
		const FVector Rr = P(A + PI / 5.f, R * 0.4f);
		for (const FVector& Face : {N, -N})
		{
			Quad(Buf, Center, L, Tip, Rr, Face, C);
		}
	}
}

// Названия улиц и вывески.
const TCHAR* StreetNames[] = {TEXT("ул. Ленина"), TEXT("ул. Мира"), TEXT("ул. Советская"), TEXT("ул. Гагарина"), TEXT("ул. Строителей"),
	TEXT("ул. Молодёжная"), TEXT("ул. Школьная"), TEXT("ул. Садовая"), TEXT("ул. Кирова"), TEXT("ул. Победы"), TEXT("ул. Октябрьская"),
	TEXT("ул. Комсомольская"), TEXT("ул. Шахтёров"), TEXT("ул. Рабочая"), TEXT("ул. Лесная"), TEXT("пр. Металлургов"), TEXT("ул. Весенняя"),
	TEXT("ул. Пушкина"), TEXT("ул. Зелёная"), TEXT("ул. Химиков"), TEXT("ул. Энергетиков"), TEXT("ул. Береговая"), TEXT("пр. Октябрьский"),
	TEXT("ул. Свободы"), TEXT("ул. Чкалова"), TEXT("ул. Заводская"), TEXT("ул. Набережная"), TEXT("ул. Спортивная")};

struct FShopSign
{
	const TCHAR* Text;
	FLinearColor Paper;
	FLinearColor Ink;
};
const FLinearColor InkWhite(0.95f, 0.95f, 0.9f);
const FLinearColor InkDark(0.06f, 0.06f, 0.07f);
const FShopSign CitySigns[] = {
	{TEXT("ПРОДУКТЫ"), FLinearColor(0.6f, 0.08f, 0.06f), InkWhite},
	{TEXT("ГАСТРОНОМ"), FLinearColor(0.08f, 0.16f, 0.42f), InkWhite},
	{TEXT("УНИВЕРСАМ"), FLinearColor(0.1f, 0.4f, 0.22f), InkWhite},
	{TEXT("ХЛЕБ"), FLinearColor(0.72f, 0.5f, 0.18f), InkDark},
	{TEXT("МОЛОКО"), FLinearColor(0.2f, 0.42f, 0.7f), InkWhite},
	{TEXT("МЯСО"), FLinearColor(0.55f, 0.1f, 0.1f), InkWhite},
	{TEXT("РЫБА"), FLinearColor(0.1f, 0.35f, 0.55f), InkWhite},
	{TEXT("ОВОЩИ · ФРУКТЫ"), FLinearColor(0.25f, 0.5f, 0.15f), InkWhite},
	{TEXT("КОНДИТЕРСКАЯ"), FLinearColor(0.85f, 0.55f, 0.65f), InkDark},
	{TEXT("КУЛИНАРИЯ"), FLinearColor(0.8f, 0.65f, 0.35f), InkDark},
	{TEXT("АПТЕКА"), FLinearColor(0.1f, 0.45f, 0.25f), InkWhite},
	{TEXT("ОПТИКА"), FLinearColor(0.15f, 0.3f, 0.6f), InkWhite},
	{TEXT("ХОЗТОВАРЫ"), FLinearColor(0.12f, 0.25f, 0.5f), InkWhite},
	{TEXT("СТРОЙМАТЕРИАЛЫ"), FLinearColor(0.85f, 0.45f, 0.1f), InkDark},
	{TEXT("АВТОЗАПЧАСТИ"), FLinearColor(0.95f, 0.78f, 0.1f), InkDark},
	{TEXT("ШИНОМОНТАЖ"), FLinearColor(0.1f, 0.1f, 0.1f), FLinearColor(0.95f, 0.8f, 0.1f)},
	{TEXT("ЦВЕТЫ"), FLinearColor(0.75f, 0.25f, 0.45f), InkWhite},
	{TEXT("КНИГИ"), FLinearColor(0.4f, 0.25f, 0.15f), InkWhite},
	{TEXT("КАНЦТОВАРЫ"), FLinearColor(0.3f, 0.3f, 0.55f), InkWhite},
	{TEXT("ОДЕЖДА"), FLinearColor(0.35f, 0.15f, 0.4f), InkWhite},
	{TEXT("ОБУВЬ"), FLinearColor(0.25f, 0.2f, 0.18f), InkWhite},
	{TEXT("ИГРУШКИ"), FLinearColor(0.95f, 0.6f, 0.15f), InkDark},
	{TEXT("ТКАНИ"), FLinearColor(0.6f, 0.4f, 0.55f), InkWhite},
	{TEXT("ЭЛЕКТРОТОВАРЫ"), FLinearColor(0.15f, 0.2f, 0.3f), FLinearColor(0.95f, 0.85f, 0.2f)},
	{TEXT("ПАРИКМАХЕРСКАЯ"), FLinearColor(0.9f, 0.88f, 0.82f), FLinearColor(0.5f, 0.1f, 0.1f)},
	{TEXT("РЕМОНТ ОБУВИ"), FLinearColor(0.85f, 0.82f, 0.7f), InkDark},
	{TEXT("ФОТОАТЕЛЬЕ"), FLinearColor(0.1f, 0.1f, 0.12f), InkWhite},
	{TEXT("ПОЧТА"), FLinearColor(0.12f, 0.3f, 0.65f), InkWhite},
	{TEXT("СБЕРКАССА"), FLinearColor(0.15f, 0.45f, 0.3f), InkWhite},
	{TEXT("СТОЛОВАЯ"), FLinearColor(0.85f, 0.82f, 0.7f), FLinearColor(0.5f, 0.12f, 0.08f)},
	{TEXT("ПЕЛЬМЕННАЯ"), FLinearColor(0.55f, 0.15f, 0.1f), InkWhite},
	{TEXT("КАФЕ «ВСТРЕЧА»"), FLinearColor(0.2f, 0.15f, 0.3f), FLinearColor(0.95f, 0.75f, 0.3f)},
	{TEXT("ПИВО · ВОДЫ"), FLinearColor(0.75f, 0.55f, 0.1f), InkDark},
	{TEXT("МЕБЕЛЬ"), FLinearColor(0.45f, 0.3f, 0.2f), InkWhite},
	{TEXT("ЗООМАГАЗИН"), FLinearColor(0.3f, 0.55f, 0.6f), InkWhite},
	{TEXT("ЛОМБАРД"), FLinearColor(0.75f, 0.62f, 0.2f), InkDark},
	{TEXT("РАДИОДЕТАЛИ"), FLinearColor(0.2f, 0.2f, 0.22f), FLinearColor(0.4f, 0.85f, 0.4f)},
	{TEXT("ХОЛОДИЛЬНИКИ · ТЕЛЕВИЗОРЫ"), FLinearColor(0.15f, 0.3f, 0.5f), InkWhite}};
const FShopSign VillageSigns[] = {
	{TEXT("МАГАЗИН"), FLinearColor(0.6f, 0.08f, 0.06f), InkWhite},
	{TEXT("ПРОДУКТЫ"), FLinearColor(0.12f, 0.35f, 0.2f), InkWhite},
	{TEXT("СЕЛЬПО"), FLinearColor(0.12f, 0.25f, 0.5f), InkWhite},
	{TEXT("ХЛЕБ · ПРОДУКТЫ"), FLinearColor(0.72f, 0.5f, 0.18f), InkDark},
	{TEXT("ТОВАРЫ ПОВСЕДНЕВНОГО СПРОСА"), FLinearColor(0.85f, 0.82f, 0.7f), FLinearColor(0.4f, 0.1f, 0.08f)},
	{TEXT("ВСЁ ДЛЯ ДОМА"), FLinearColor(0.2f, 0.4f, 0.6f), InkWhite}};

const FLinearColor PanelTints[] = {FLinearColor(0.93f, 0.93f, 0.9f), FLinearColor(0.96f, 0.88f, 0.74f), FLinearColor(0.8f, 0.88f, 0.96f),
	FLinearColor(0.96f, 0.92f, 0.68f), FLinearColor(0.96f, 0.82f, 0.8f), FLinearColor(0.82f, 0.83f, 0.84f), FLinearColor(0.82f, 0.94f, 0.84f),
	FLinearColor(0.9f, 0.85f, 0.95f), FLinearColor(0.98f, 0.8f, 0.62f), FLinearColor(0.75f, 0.85f, 0.88f)};
const FLinearColor StuccoTints[] = {FLinearColor(0.98f, 0.86f, 0.6f), FLinearColor(0.95f, 0.78f, 0.7f), FLinearColor(0.94f, 0.92f, 0.82f),
	FLinearColor(0.84f, 0.9f, 0.78f), FLinearColor(0.88f, 0.8f, 0.68f), FLinearColor(0.82f, 0.86f, 0.92f)};
const FLinearColor DoorColors[] = {FLinearColor(0.3f, 0.2f, 0.12f), FLinearColor(0.15f, 0.3f, 0.2f), FLinearColor(0.4f, 0.42f, 0.44f),
	FLinearColor(0.2f, 0.25f, 0.45f), FLinearColor(0.5f, 0.15f, 0.1f)};

template <typename T, SIZE_T N>
const T& Pick(const T (&List)[N], uint32 Seed)
{
	return List[Seed % static_cast<uint32>(N)];
}

// Окна, которые горят вечером: на гранях коробки (центр, длина, ширина, высота) — пролёты 3 × 2.8 м.
void LitWindowsBox(FPeregonTownBuffers& T, const FBldFrame& F, float Cx, float Cy, float Lx, float Ly, float H, float U0, float U1, float Z0, float Z1,
	float Share, uint32 Seed, bool bLongOnly = false)
{
	rgen::Rng Rand(Seed ^ 0x51A7u);
	const int32 Floors = FMath::FloorToInt(H / 280.f);
	for (int32 Face = 0; Face < 4; ++Face)
	{
		const bool bLong = Face % 2 == 0;
		if (bLongOnly && !bLong)
		{
			continue;
		}
		const FVector Outward = Face == 0 ? -F.Ay : (Face == 1 ? F.Ax : (Face == 2 ? F.Ay : -F.Ax));
		const FVector Along = Face == 0 ? F.Ax : (Face == 1 ? F.Ay : (Face == 2 ? -F.Ax : -F.Ay));
		const float Width = bLong ? Lx : Ly;
		const float Depth = bLong ? Ly : Lx;
		const FVector Corner = F.At(Cx, Cy, 0.f) + Outward * (Depth * 0.5f + 4.f) - Along * Width * 0.5f;
		const int32 Bays = FMath::FloorToInt(Width / 300.f);
		for (int32 Bay = 0; Bay < Bays; ++Bay)
		{
			for (int32 Floor = 0; Floor < Floors; ++Floor)
			{
				if (static_cast<float>(Rand.Unit()) > Share)
				{
					continue;
				}
				const float A = Bay * 300.f + U0;
				const float B = Bay * 300.f + U1;
				const float Top = H - Floor * 280.f - Z0;
				const float Bot = H - Floor * 280.f - Z1;
				T.Windows.AddQuad(Corner + Along * A + FVector(0, 0, Bot), Corner + Along * B + FVector(0, 0, Bot), Corner + Along * B + FVector(0, 0, Top),
					Corner + Along * A + FVector(0, 0, Top), Outward, FVector2D(0, 1), FVector2D(1, 0), FColor::White, 0);
				FPeregonLightSpot Spot;
				Spot.Location = Corner + Along * (0.5f * (A + B)) + FVector(0, 0, 0.5f * (Top + Bot)) + Outward * 6.0;
				Spot.Direction = Outward;
				Spot.SizeCm = FVector2D(B - A, Top - Bot);
				Spot.bWindow = true;
				Spot.Seed = Seed * 131u + static_cast<uint32>(Bay * 17 + Floor * 3 + Face);
				T.WindowSpots.Add(Spot);
			}
		}
	}
}

// Фонарь над дверью: светящийся плафон и настоящий свет вниз.
void DoorLamp(FPeregonTownBuffers& T, const FVector& At, const FVector& Outward, uint32 Seed)
{
	T.Props.AddBox(At - FVector(0, 0, 4.f), static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Outward.Y, Outward.X))), FVector(18.f, 26.f, 4.f), FVector2D(100.f, 100.f),
		Col(0.2f, 0.2f, 0.22f), true, true);
	T.Lamps.AddBox(At - FVector(0, 0, 12.f) + Outward * 6.f, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Outward.Y, Outward.X))), FVector(12.f, 18.f, 8.f),
		FVector2D(100.f, 100.f), FColor::White, true, true);
	FPeregonLightSpot Spot;
	Spot.Location = At - FVector(0, 0, 20.f) + Outward * 30.f;
	Spot.Direction = FVector::DownVector;
	Spot.Seed = Seed;
	T.LampSpots.Add(Spot);
}

// Табличка-вывеска: доска с надписью на фасаде (лицом по Outward).
void SignBoard(FPeregonKit& Kit, FPeregonTownBuffers& T, const FVector& Center, const FVector& Outward, float W, float H, const FShopSign& Sign,
	int32 WidthPx = 512)
{
	const float Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Outward.Y, Outward.X)));
	T.Props.AddBox(Center - FVector(0, 0, H * 0.5f + 6.f), Yaw, FVector(8.f, W + 12.f, H + 12.f), FVector2D(100.f, 100.f), Col(Sign.Paper * 0.6f), true, true);
	Kit.Label(FTransform(Outward.Rotation(), Center + Outward * 5.f), Sign.Text, W, H, H > 60.f ? 64 : 44, Sign.Ink, Sign.Paper, WidthPx);
}
} // namespace PeregonTown


// ---------------------------------------------------------------------------------------------
// Жилые дома
// ---------------------------------------------------------------------------------------------
void FPeregonKit::Housing(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	rgen::Rng Rand(B.Seed);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	const float H = static_cast<float>(B.Height() * 100.0);
	const uint32 V = B.Seed >> 8;

	// Фасад и окна в текстуре: (левый, правый край окна в пролёте; верх, низ от верха этажа), см.
	FPeregonMeshBuffers* Wall = &T.Panel;
	float U0 = 84.f, U1 = 216.f, Z0 = 61.f, Z1 = 184.f;
	FLinearColor Tint = Pick(PanelTints, V);
	bool bBalconies = false;
	bool bHipRoof = false;
	switch (B.Kind)
	{
	case rgen::BuildingKind::Panel5:
		switch (V % 4)
		{
		case 1: Wall = &T.PanelTile; U0 = 56.f; U1 = 253.f; break;
		case 2: Wall = &T.Silicate; U0 = 94.f; U1 = 216.f; Z1 = 201.f; Tint = FLinearColor(0.97f, 0.96f, 0.93f); break;
		case 3: bBalconies = true; break;
		default: break;
		}
		break;
	case rgen::BuildingKind::Panel9:
		switch (V % 3)
		{
		case 1: Wall = &T.PanelTile; U0 = 56.f; U1 = 253.f; bBalconies = true; break;
		case 2: Wall = &T.PanelLoggia; U0 = 131.f; U1 = 253.f; Z0 = 52.f; Z1 = 149.f; break;
		default: bBalconies = true; break;
		}
		break;
	case rgen::BuildingKind::Tower:
		Wall = (V % 2) ? &T.PanelLoggia : &T.PanelTile;
		if (V % 2)
		{
			U0 = 131.f; U1 = 253.f; Z0 = 52.f; Z1 = 149.f;
		}
		else
		{
			U0 = 56.f; U1 = 253.f;
		}
		break;
	default: // кирпичные: «сталинка» под штукатуркой, красный или силикатный кирпич
		switch (V % 3)
		{
		case 0: Wall = &T.Stucco; U0 = 103.f; U1 = 206.f; Z0 = 52.f; Z1 = 227.f; Tint = Pick(StuccoTints, V / 3); bHipRoof = true; break;
		case 1: Wall = &T.Brick; U0 = 94.f; U1 = 206.f; Z0 = 56.f; Z1 = 206.f; Tint = FLinearColor::White; bHipRoof = (V / 3) % 2 == 0; break;
		default: Wall = &T.Silicate; U0 = 94.f; U1 = 216.f; Z1 = 201.f; Tint = FLinearColor(0.95f, 0.94f, 0.9f); break;
		}
		break;
	}

	// Стены, цоколь, парапет.
	Wall->AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), Col(Tint), false, false);
	TBox(T.Concrete, F, 0.f, 0.f, -20.f, FVector(L + 16.f, W + 16.f, 80.f), Col(0.55f, 0.55f, 0.56f), true);
	if (bHipRoof)
	{
		// Карниз и вальмовая крыша из кровельного железа.
		TBox(T.Props, F, 0.f, 0.f, H - 30.f, FVector(L + 60.f, W + 60.f, 30.f), Col(Tint * 0.95f), true, true);
		const float Rh = FMath::Min(350.f, W * 0.3f);
		const float Hx = L * 0.5f + 40.f;
		const float Hy = W * 0.5f + 40.f;
		const float Ridge = FMath::Max(0.f, Hx - Hy);
		const FVector P00 = F.At(-Hx, -Hy, H), P10 = F.At(Hx, -Hy, H), P11 = F.At(Hx, Hy, H), P01 = F.At(-Hx, Hy, H);
		const FVector R0 = F.At(-Ridge, 0.f, H + Rh), R1 = F.At(Ridge, 0.f, H + Rh);
		const FColor RoofC = (V % 2) ? Col(0.35f, 0.42f, 0.38f) : Col(0.55f, 0.25f, 0.2f);
		Quad(T.Props, P00, P10, R1, R0, (F.Dir(0, -1) + FVector(0, 0, 1)).GetSafeNormal(), RoofC);
		Quad(T.Props, P11, P01, R0, R1, (F.Dir(0, 1) + FVector(0, 0, 1)).GetSafeNormal(), RoofC);
		Quad(T.Props, P10, P11, R1, R1, (F.Dir(1, 0) + FVector(0, 0, 1)).GetSafeNormal(), RoofC);
		Quad(T.Props, P01, P00, R0, R0, (F.Dir(-1, 0) + FVector(0, 0, 1)).GetSafeNormal(), RoofC);
		// Слуховые окна и трубы.
		for (float X = -Hx + 400.f; X < Hx - 400.f; X += 900.f)
		{
			TBox(T.Brick, F, X, W * 0.15f, H + Rh * 0.4f, FVector(60.f, 60.f, Rh * 0.9f), FColor::White, true);
		}
	}
	else
	{
		T.Roofs.AddBox(F.At(0.f, 0.f, H), F.Yaw, FVector(L + 20.f, W + 20.f, 45.f), FVector2D(400.f, 400.f), FColor::White, true, false);
		TBox(T.Props, F, 0.f, -W * 0.5f - 5.f, H + 30.f, FVector(L + 24.f, 10.f, 25.f), Col(Tint * 0.85f), true); // парапет
		TBox(T.Props, F, 0.f, W * 0.5f + 5.f, H + 30.f, FVector(L + 24.f, 10.f, 25.f), Col(Tint * 0.85f), true);
	}

	// Подъезды со стороны двора (−Y): дверь, ступени, козырёк, фонарь, номер подъезда.
	const bool bTower = B.Kind == rgen::BuildingKind::Tower;
	const float Step = bTower ? L : 1500.f;
	int32 Entrance = 0;
	for (float X = bTower ? 0.f : -0.5f * L + 1500.f; X < 0.5f * L - 600.f || (bTower && Entrance == 0); X += Step)
	{
		++Entrance;
		const FVector Face = F.At(X, -W * 0.5f - 3.f, 0.f);
		const FColor Door = Col(Pick(DoorColors, B.Seed + static_cast<uint32>(Entrance)));
		Quad(T.Props, Face - F.Ax * 65.f + FVector(0, 0, 40.f), Face + F.Ax * 65.f + FVector(0, 0, 40.f), Face + F.Ax * 65.f + FVector(0, 0, 250.f),
			Face - F.Ax * 65.f + FVector(0, 0, 250.f), -F.Ay, Door);
		for (int32 S = 0; S < 3; ++S)
		{
			TBox(T.Concrete, F, X, -W * 0.5f - 60.f + S * 18.f, 0.f, FVector(220.f, 120.f - S * 36.f, 15.f + S * 13.f), Col(0.6f, 0.6f, 0.6f));
		}
		T.Roofs.AddBox(F.At(X, -W * 0.5f - 75.f, 270.f), F.Yaw, FVector(240.f, 150.f, 14.f), FVector2D(400.f, 400.f), FColor::White, true, true);
		for (float Sx : {-110.f, 110.f})
		{
			TBox(T.Props, F, X + Sx, -W * 0.5f - 140.f, 40.f, FVector(8.f, 8.f, 232.f), Col(0.35f, 0.36f, 0.38f)); // стойки козырька
		}
		DoorLamp(T, F.At(X, -W * 0.5f - 5.f, 262.f), -F.Ay, B.Seed + Entrance);
		// Номер подъезда и номера квартир — светлая табличка над дверью.
		TBox(T.Props, F, X, -W * 0.5f - 4.f, 300.f, FVector(60.f, 4.f, 30.f), Col(0.9f, 0.9f, 0.86f));
	}

	// Балконы на уличной стороне (+Y): плита, экран (цветной или рифлёный), у части — остеклены.
	if (bBalconies)
	{
		const int32 Bays = FMath::FloorToInt(L / 300.f);
		const int32 Floors = FMath::FloorToInt(H / 280.f);
		const FColor Slab = Col(0.62f, 0.62f, 0.62f);
		const FColor Screen = (V % 2) ? Col(Tint * 0.92f) : Col(0.85f, 0.85f, 0.82f);
		const int32 Pattern = static_cast<int32>(V % 3);
		for (int32 Bay = 0; Bay < Bays; ++Bay)
		{
			const bool bColumn = Pattern == 0 ? (Bay % 2 == 1) : (Pattern == 1 ? (Bay % 3 != 0) : (Bay % 4 == 1 || Bay % 4 == 2));
			if (!bColumn)
			{
				continue;
			}
			const float X = -0.5f * L + Bay * 300.f + 150.f;
			for (int32 Floor = 1; Floor < Floors; ++Floor)
			{
				const float Z = H - (Floors - Floor) * 280.f - 20.f;
				TBox(T.Concrete, F, X, W * 0.5f + 55.f, Z, FVector(270.f, 110.f, 14.f), Slab, true, true);
				TBox(T.Props, F, X, W * 0.5f + 108.f, Z + 14.f, FVector(270.f, 6.f, 100.f), Screen, true);
				for (float Sx : {-133.f, 133.f})
				{
					TBox(T.Props, F, X + Sx, W * 0.5f + 55.f, Z + 14.f, FVector(5.f, 104.f, 100.f), Screen, true);
				}
				// Остеклённые и с хламом: каждый жилец решает сам.
				const uint32 Tenant = rgen::Hash(B.Seed, static_cast<uint32>(Bay * 31 + Floor));
				if (rgen::HashToUnit(Tenant) < 0.35)
				{
					const FVector A0 = F.At(X - 133.f, W * 0.5f + 110.f, Z + 114.f);
					const FVector A1 = F.At(X + 133.f, W * 0.5f + 110.f, Z + 114.f);
					Quad(T.Glass, A0, A1, A1 + FVector(0, 0, 140.f), A0 + FVector(0, 0, 140.f), F.Ay, FColor::White);
					TBox(T.Props, F, X, W * 0.5f + 110.f, Z + 254.f, FVector(270.f, 10.f, 8.f), Col(0.9f, 0.9f, 0.88f), true, true);
					for (float Sx : {-133.f, -44.f, 44.f, 133.f})
					{
						TBox(T.Props, F, X + Sx, W * 0.5f + 110.f, Z + 114.f, FVector(5.f, 6.f, 140.f), Col(0.9f, 0.9f, 0.88f), true);
					}
				}
				else if (rgen::HashToUnit(rgen::Hash(Tenant, 7u)) < 0.25)
				{
					TBox(T.Props, F, X + 60.f, W * 0.5f + 70.f, Z + 14.f, FVector(60.f, 40.f, 70.f), Col(0.45f, 0.35f, 0.25f), true); // ящики, лыжи
				}
			}
		}
	}

	// Крыша: машинные помещения лифтов, вентшахты, антенны.
	if (!bHipRoof)
	{
		const bool bLift = B.Kind == rgen::BuildingKind::Panel9 || bTower;
		int32 K = 0;
		for (float X = bTower ? 0.f : -0.5f * L + 1500.f; X < 0.5f * L - 600.f || (bTower && K == 0); X += bTower ? L : 1500.f)
		{
			++K;
			if (bLift)
			{
				TBox(T.Concrete, F, X, 0.f, H + 45.f, FVector(400.f, 500.f, 260.f), Col(0.75f, 0.75f, 0.74f));
				T.Roofs.AddBox(F.At(X, 0.f, H + 305.f), F.Yaw, FVector(430.f, 530.f, 20.f), FVector2D(400.f, 400.f), FColor::White, true, true);
			}
			TBox(T.Concrete, F, X + 300.f, W * 0.2f, H + 45.f, FVector(120.f, 80.f, 110.f), Col(0.7f, 0.7f, 0.7f));
			if (Rand.Chance(0.7))
			{
				// Телеантенна: мачта и перекладины.
				const float Ax0 = X - 250.f;
				TBox(T.Props, F, Ax0, W * 0.25f, H + 45.f, FVector(5.f, 5.f, 320.f), Col(0.4f, 0.4f, 0.42f));
				for (int32 Bar = 0; Bar < 4; ++Bar)
				{
					TBox(T.Props, F, Ax0, W * 0.25f, H + 240.f + Bar * 25.f, FVector(4.f, 140.f - Bar * 25.f, 4.f), Col(0.4f, 0.4f, 0.42f), true, true, 0.f);
				}
			}
		}
	}

	// Торцы девятиэтажек и высоток — «суперграфика»: цветные полосы или солнце.
	if ((B.Kind == rgen::BuildingKind::Panel9 || bTower) && (V % 5) < 2)
	{
		const float Sign = (V % 2) ? 1.f : -1.f;
		const FVector OutN = F.Ax * Sign;
		const FVector Side = F.Ay * Sign;
		const FVector Face = F.At(Sign * (L * 0.5f + 5.f), 0.f, 0.f);
		static const FLinearColor Palette[] = {FLinearColor(0.85f, 0.25f, 0.15f), FLinearColor(0.15f, 0.4f, 0.75f), FLinearColor(0.95f, 0.75f, 0.15f),
			FLinearColor(0.2f, 0.6f, 0.4f)};
		if ((V / 5) % 2 == 0)
		{
			for (int32 K = 0; K < 3; ++K)
			{
				const float Y0 = -W * 0.4f + K * W * 0.27f;
				const float Top = H * (0.55f + 0.15f * K);
				const FColor C = Col(Palette[(V + K) % 4]);
				Quad(T.Props, Face + Side * Y0 + FVector(0, 0, 300.f), Face + Side * (Y0 + W * 0.22f) + FVector(0, 0, 300.f),
					Face + Side * (Y0 + W * 0.22f) + FVector(0, 0, Top), Face + Side * Y0 + FVector(0, 0, Top), OutN, C);
			}
		}
		else
		{
			const FVector C0 = Face + FVector(0, 0, H * 0.7f);
			const float R = W * 0.3f;
			const FColor Sun = Col(Palette[2]);
			for (int32 K = 0; K < 16; ++K)
			{
				const float A0 = 2.f * PI * K / 16.f;
				const float A1 = 2.f * PI * (K + 1) / 16.f;
				const FVector D0 = Side * FMath::Cos(A0) + FVector(0, 0, FMath::Sin(A0));
				const FVector D1 = Side * FMath::Cos(A1) + FVector(0, 0, FMath::Sin(A1));
				Quad(T.Props, C0, C0 + D0 * R, C0 + D1 * R, C0, OutN, Sun);
				if (K % 2 == 0)
				{
					Quad(T.Props, C0 + D0 * (R * 1.15f), C0 + D1 * (R * 1.15f), C0 + D1 * (R * 1.7f), C0 + D0 * (R * 1.7f), OutN, Col(Palette[0]));
				}
			}
		}
	}

	// Магазин в первом этаже уличной стороны у части девятиэтажек.
	if (B.Kind == rgen::BuildingKind::Panel9 && (V % 4) == 3)
	{
		const FShopSign& Sign = Pick(CitySigns, V / 4);
		const FVector Face = F.At(-L * 0.25f, W * 0.5f + 4.f, 0.f);
		Quad(T.Windows, Face - F.Ax * 600.f + FVector(0, 0, 60.f), Face + F.Ax * 600.f + FVector(0, 0, 60.f), Face + F.Ax * 600.f + FVector(0, 0, 240.f),
			Face - F.Ax * 600.f + FVector(0, 0, 240.f), F.Ay, FColor::White);
		SignBoard(*this, T, Face + F.Ay * 6.f + FVector(0, 0, 300.f), F.Ay, 900.f, 70.f, Sign);
	}

	LitWindowsBox(T, F, 0.f, 0.f, L, W, H, U0, U1, Z0, Z1, 0.1f + 0.1f * static_cast<float>(Rand.Unit()), B.Seed);
	HouseNumber(B, F.At(-L * 0.5f + 120.f, W * 0.5f + 4.f, 320.f), F.Ay);
}

void FPeregonKit::LitWindows(const rgen::Building& B, FPeregonTownBuffers& T, float WinU0, float WinU1, float WinZ0, float WinZ1, float Share)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	LitWindowsBox(T, F, 0.f, 0.f, static_cast<float>(B.Length * 100.0), static_cast<float>(B.Width * 100.0), static_cast<float>(B.Height() * 100.0), WinU0,
		WinU1, WinZ0, WinZ1, Share, B.Seed);
}

void FPeregonKit::HouseNumber(const rgen::Building& B, const FVector& At, const FVector& Outward)
{
	using namespace PeregonTown;
	const FString Text = FString::Printf(TEXT("%s\n%d"), Pick(StreetNames, B.Seed >> 3), 1 + static_cast<int32>((B.Seed >> 11) % 87u));
	Label(FTransform(Outward.Rotation(), At + Outward * 2.f), Text, 70.f, 32.f, 22, FLinearColor(0.95f, 0.95f, 0.92f), FLinearColor(0.1f, 0.22f, 0.5f), 160);
}

// ---------------------------------------------------------------------------------------------
// Магазины
// ---------------------------------------------------------------------------------------------
void FPeregonKit::ShopBuilding(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	const bool bVillage = B.Floors >= 1000;
	const uint32 Id = static_cast<uint32>(FMath::Max(0, bVillage ? B.Floors - 1000 : B.Floors));
	const FShopSign& Sign = bVillage ? Pick(VillageSigns, Id) : Pick(CitySigns, Id);
	const uint32 Style = bVillage ? (B.Seed % 3) : (B.Seed >> 4) % 5;
	static const FLinearColor Walls[] = {FLinearColor(0.9f, 0.85f, 0.7f), FLinearColor(0.8f, 0.85f, 0.9f), FLinearColor(0.85f, 0.9f, 0.8f),
		FLinearColor(0.95f, 0.8f, 0.7f), FLinearColor(0.88f, 0.88f, 0.86f)};
	const FColor Wall = Col(Pick(Walls, B.Seed >> 9));
	float H = 360.f;
	const FVector Front = -F.Ay;

	switch (Style)
	{
	case 1:
	{
		// Павильон с полосатой маркизой и вывеской на крыше.
		T.Stucco.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), Wall, false, false);
		const FColor A = Col(Sign.Paper);
		const FColor Bc = Col(0.92f, 0.92f, 0.88f);
		const int32 Stripes = FMath::Max(4, FMath::FloorToInt(L * 0.7f / 60.f));
		for (int32 K = 0; K < Stripes; ++K)
		{
			const float X0 = -L * 0.35f + K * (L * 0.7f / Stripes);
			const float X1 = X0 + L * 0.7f / Stripes;
			Quad(T.Props, F.At(X0, -W * 0.5f - 5.f, 280.f), F.At(X1, -W * 0.5f - 5.f, 280.f), F.At(X1, -W * 0.5f - 150.f, 230.f),
				F.At(X0, -W * 0.5f - 150.f, 230.f), (Front + FVector(0, 0, 2.f)).GetSafeNormal(), K % 2 ? A : Bc);
		}
		TBox(T.Props, F, 0.f, 0.f, H, FVector(L * 0.6f, 12.f, 20.f), Col(0.3f, 0.3f, 0.32f), true);
		SignBoard(*this, T, F.At(0.f, -10.f, H + 110.f), Front, L * 0.6f, 80.f, Sign);
		break;
	}
	case 2:
	{
		// Двухэтажный кирпичный: внизу магазин, наверху — контора.
		H = 650.f;
		T.Brick.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), FColor::White, false, false);
		LitWindowsBox(T, F, 0.f, 0.f, L, W, H, 94.f, 206.f, 56.f, 206.f, 0.25f, B.Seed, true);
		SignBoard(*this, T, F.At(0.f, -W * 0.5f - 5.f, 330.f), Front, L * 0.7f, 70.f, Sign);
		break;
	}
	case 3:
	{
		// Силикатный короб, буквы на металлической раме на крыше.
		T.Silicate.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), Col(0.95f, 0.94f, 0.9f), false, false);
		for (float X : {-L * 0.3f, L * 0.3f})
		{
			TBox(T.Props, F, X, -W * 0.3f, H, FVector(6.f, 6.f, 160.f), Col(0.3f, 0.3f, 0.32f));
		}
		Label(FTransform(Front.Rotation(), F.At(0.f, -W * 0.3f - 6.f, H + 100.f)), Sign.Text, L * 0.65f, 90.f, 64, Sign.Paper, FLinearColor(0.92f, 0.92f, 0.9f));
		break;
	}
	case 4:
	{
		// Магазин в морском контейнере из профлиста.
		H = 280.f;
		T.MetalRoofs.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(200.f, 200.f), FColor::White, true, false);
		SignBoard(*this, T, F.At(0.f, -W * 0.5f - 5.f, 250.f), Front, L * 0.6f, 50.f, Sign, 256);
		break;
	}
	default:
	{
		// Оштукатуренный короб, витрина во всю ширину, вывеска-фриз.
		T.Stucco.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), Wall, false, false);
		SignBoard(*this, T, F.At(0.f, -W * 0.5f - 5.f, 300.f), Front, L * 0.7f, 70.f, Sign);
		break;
	}
	}
	if (Style != 2 && Style != 4)
	{
		T.Roofs.AddBox(F.At(0.f, 0.f, H), F.Yaw, FVector(L + 30.f, W + 30.f, 25.f), FVector2D(400.f, 400.f), FColor::White, true, false);
	}
	// Витрина с переплётом, дверь, крыльцо, фонарь, урна.
	const float VitW = Style == 4 ? L * 0.25f : L * 0.32f;
	const FVector Face = F.At(L * 0.08f, -W * 0.5f - 3.f, 0.f);
	Quad(T.Windows, Face - F.Ax * VitW + FVector(0, 0, 80.f), Face + F.Ax * VitW + FVector(0, 0, 80.f), Face + F.Ax * VitW + FVector(0, 0, 230.f),
		Face - F.Ax * VitW + FVector(0, 0, 230.f), Front, FColor::White);
	for (float K = -1.f; K <= 1.01f; K += 0.5f)
	{
		TBox(T.Props, F, L * 0.08f + K * VitW, -W * 0.5f - 6.f, 75.f, FVector(6.f, 6.f, 160.f), Col(0.85f, 0.85f, 0.82f));
	}
	const float DoorX = -L * 0.38f;
	Quad(T.Props, F.At(DoorX - 55.f, -W * 0.5f - 4.f, 30.f), F.At(DoorX + 55.f, -W * 0.5f - 4.f, 30.f), F.At(DoorX + 55.f, -W * 0.5f - 4.f, 240.f),
		F.At(DoorX - 55.f, -W * 0.5f - 4.f, 240.f), Front, Col(Pick(DoorColors, B.Seed)));
	TBox(T.Concrete, F, DoorX, -W * 0.5f - 70.f, 0.f, FVector(220.f, 140.f, 30.f), Col(0.6f, 0.6f, 0.6f));
	DoorLamp(T, F.At(DoorX, -W * 0.5f - 5.f, 255.f), Front, B.Seed);
	T.Wheels.AddBox(F.At(DoorX + 140.f, -W * 0.5f - 100.f, 0.f), F.Yaw, FVector(40.f, 40.f, 70.f), FVector2D(100.f, 100.f), FColor::White, true, false);
	if (!bVillage && (B.Seed % 3) == 0)
	{
		// Кондиционер и «Режим работы» на двери.
		TBox(T.Props, F, L * 0.3f, -W * 0.5f - 20.f, 250.f, FVector(80.f, 30.f, 50.f), Col(0.9f, 0.9f, 0.9f));
	}
	Label(FTransform(Front.Rotation(), F.At(DoorX + 80.f, -W * 0.5f - 6.f, 160.f)), TEXT("8:00–20:00"), 40.f, 20.f, 18, InkDark,
		FLinearColor(0.92f, 0.92f, 0.88f), 128);
}

// ---------------------------------------------------------------------------------------------
// Гаражи
// ---------------------------------------------------------------------------------------------
void FPeregonKit::GarageRow(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	const float H = static_cast<float>(B.Height() * 100.0);
	const uint32 V = B.Seed >> 6;
	FPeregonMeshBuffers& Walls = (V % 3) == 0 ? T.Brick : ((V % 3) == 1 ? T.Silicate : T.Stucco);
	Walls.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), (V % 3) == 2 ? Col(0.72f, 0.72f, 0.7f) : FColor::White, false, false);
	T.Roofs.AddBox(F.At(0.f, 0.f, H), F.Yaw, FVector(L + 40.f, W + 60.f, 20.f), FVector2D(400.f, 400.f), FColor::White, true, true);
	static const FLinearColor Doors[] = {FLinearColor(0.25f, 0.4f, 0.25f), FLinearColor(0.45f, 0.28f, 0.15f), FLinearColor(0.2f, 0.3f, 0.5f),
		FLinearColor(0.5f, 0.5f, 0.5f), FLinearColor(0.55f, 0.18f, 0.12f), FLinearColor(0.6f, 0.35f, 0.15f), FLinearColor(0.35f, 0.55f, 0.7f),
		FLinearColor(0.15f, 0.15f, 0.15f), FLinearColor(0.7f, 0.6f, 0.2f)};
	const int32 Boxes = FMath::Max(1, FMath::FloorToInt(L / 350.f));
	for (int32 K = 0; K < Boxes; ++K)
	{
		const float X = -L * 0.5f + 175.f + K * 350.f;
		const uint32 H2 = rgen::Hash(B.Seed, static_cast<uint32>(K));
		const FColor Door = Col(Doors[H2 % UE_ARRAY_COUNT(Doors)]);
		const FVector C = F.At(X, -W * 0.5f - 2.f, 0.f);
		const bool bOpen = rgen::HashToUnit(H2) < 0.12;
		if (bOpen)
		{
			// Открытый бокс: тёмное нутро, створки распахнуты, иногда — машина внутри.
			Quad(T.Props, C - F.Ax * 130.f, C + F.Ax * 130.f, C + F.Ax * 130.f + FVector(0, 0, 220.f), C - F.Ax * 130.f + FVector(0, 0, 220.f), -F.Ay,
				Col(0.04f, 0.04f, 0.045f));
			for (float S : {-1.f, 1.f})
			{
				TBox(T.Props, F, X + S * 130.f, -W * 0.5f - 65.f, 0.f, FVector(4.f, 130.f, 220.f), Door);
			}
		}
		else
		{
			Quad(T.Props, C - F.Ax * 130.f, C + F.Ax * 130.f, C + F.Ax * 130.f + FVector(0, 0, 220.f), C - F.Ax * 130.f + FVector(0, 0, 220.f), -F.Ay, Door);
			TBox(T.Props, F, X, -W * 0.5f - 4.f, 0.f, FVector(4.f, 4.f, 220.f), Col(Doors[H2 % UE_ARRAY_COUNT(Doors)] * 0.6f)); // стык створок
			TBox(T.Props, F, X + 40.f, -W * 0.5f - 6.f, 105.f, FVector(14.f, 6.f, 8.f), Col(0.6f, 0.6f, 0.6f));             // замок
			if (rgen::HashToUnit(rgen::Hash(H2, 5u)) < 0.35)
			{
				TBox(T.Props, F, X, -W * 0.5f - 5.f, 190.f, FVector(60.f, 4.f, 14.f), Col(0.05f, 0.05f, 0.05f)); // продух
			}
		}
		TBox(T.Concrete, F, X, -W * 0.5f - 60.f, -10.f, FVector(300.f, 120.f, 20.f), Col(0.5f, 0.5f, 0.5f)); // пандус
		if (rgen::HashToUnit(rgen::Hash(H2, 9u)) < 0.15)
		{
			for (int32 Tire = 0; Tire < 3; ++Tire)
			{
				T.Wheels.AddBox(F.At(X, 0.f, H + 20.f + Tire * 22.f), F.Yaw, FVector(60.f, 60.f, 20.f), FVector2D(100.f, 100.f), FColor::White, true, false);
			}
		}
	}
	static const TCHAR* Coops[] = {TEXT("ГСК «Мотор»"), TEXT("ГСК «Автомобилист»"), TEXT("ГСК «Северный»"), TEXT("ГСК «Восход»"), TEXT("ГСК «Сибиряк»"),
		TEXT("ГСК № 12"), TEXT("ГСК «Жигули»"), TEXT("ГСК «Дорожник»")};
	Label(FTransform((-F.Ax).Rotation(), F.At(-L * 0.5f - 4.f, 0.f, 170.f)), Coops[V % UE_ARRAY_COUNT(Coops)], 200.f, 40.f, 30, InkDark,
		FLinearColor(0.9f, 0.88f, 0.8f), 256);
}

// ---------------------------------------------------------------------------------------------
// Завод
// ---------------------------------------------------------------------------------------------
void FPeregonKit::Factory(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	const float H = static_cast<float>(B.Height() * 100.0);
	const uint32 V = B.Seed >> 5;
	// Стены: серый бетон, красный кирпич старого цеха или силикат.
	switch (V % 3)
	{
	case 1: T.Brick.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), FColor::White, false, false); break;
	case 2: T.Silicate.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), Col(0.85f, 0.85f, 0.82f), false, false); break;
	default: T.Concrete.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(400.f, 400.f), Col(0.75f, 0.73f, 0.7f), false, false); break;
	}
	// Ленточное остекление по длинным сторонам (с переплётом).
	for (float S : {-1.f, 1.f})
	{
		const FVector N = F.Ay * S;
		const float Y = S * (W * 0.5f + 3.f);
		Quad(T.Glass, F.At(-L * 0.45f, Y, H * 0.45f), F.At(L * 0.45f, Y, H * 0.45f), F.At(L * 0.45f, Y, H * 0.8f), F.At(-L * 0.45f, Y, H * 0.8f), N,
			FColor::White);
		for (float X = -L * 0.45f; X <= L * 0.45f + 1.f; X += 300.f)
		{
			TBox(T.Props, F, X, Y, H * 0.45f, FVector(10.f, 6.f, H * 0.35f), Col(0.35f, 0.38f, 0.4f));
		}
	}
	if ((V / 3) % 2 == 0)
	{
		// Пилообразная крыша (шеды): скаты на юг, остеклённые вертикали на север.
		const int32 Teeth = FMath::Max(2, FMath::FloorToInt(L / 900.f));
		const float Tw = L / Teeth;
		for (int32 K = 0; K < Teeth; ++K)
		{
			const float X0 = -L * 0.5f + K * Tw;
			const float X1 = X0 + Tw;
			const FVector A0 = F.At(X0, -W * 0.5f, H), A1 = F.At(X0, W * 0.5f, H);
			const FVector B0 = F.At(X1, -W * 0.5f, H + 300.f), B1 = F.At(X1, W * 0.5f, H + 300.f);
			Quad(T.MetalRoofs, A0, B0, B1, A1, (F.Dir(-1, 0) * 0.3f + FVector(0, 0, 1)).GetSafeNormal(), FColor::White);
			Quad(T.Glass, F.At(X1, -W * 0.5f, H), F.At(X1, W * 0.5f, H), B1, B0, F.Ax, FColor::White);
			for (float S : {-1.f, 1.f})
			{
				const float Y = S * W * 0.5f;
				Quad(T.Concrete, F.At(X0, Y, H), F.At(X1, Y, H), F.At(X1, Y, H + 300.f), F.At(X1, Y, H + 300.f), F.Ay * S, Col(0.7f, 0.7f, 0.68f));
			}
		}
	}
	else
	{
		T.MetalRoofs.AddBox(F.At(0.f, 0.f, H), F.Yaw, FVector(L + 40.f, W + 40.f, 60.f), FVector2D(400.f, 400.f), FColor::White, true, false);
		// Световой фонарь вдоль конька.
		TBox(T.Glass, F, 0.f, 0.f, H + 60.f, FVector(L * 0.8f, 400.f, 180.f), FColor::White, false);
		T.MetalRoofs.AddBox(F.At(0.f, 0.f, H + 240.f), F.Yaw, FVector(L * 0.8f + 40.f, 440.f, 20.f), FVector2D(400.f, 400.f), FColor::White, true, true);
	}
	// Ворота в торцах, рампа, ёмкости и трубопровод на опорах.
	static const FLinearColor Gates[] = {FLinearColor(0.2f, 0.35f, 0.25f), FLinearColor(0.2f, 0.28f, 0.45f), FLinearColor(0.5f, 0.3f, 0.15f)};
	for (float S : {-1.f, 1.f})
	{
		const FVector N = F.Ax * S;
		const FVector C = F.At(S * (L * 0.5f + 3.f), 0.f, 0.f);
		Quad(T.Props, C - F.Ay * 220.f, C + F.Ay * 220.f, C + F.Ay * 220.f + FVector(0, 0, 480.f), C - F.Ay * 220.f + FVector(0, 0, 480.f), N,
			Col(Gates[V % 3]));
		TBox(T.Concrete, F, S * (L * 0.5f + 200.f), W * 0.3f, 0.f, FVector(400.f, 300.f, 110.f), Col(0.55f, 0.55f, 0.55f));
	}
	if ((V / 6) % 2 == 0)
	{
		for (int32 K = 0; K < 2; ++K)
		{
			const FVector P = F.At(-L * 0.25f + K * 700.f, W * 0.5f + 600.f, 0.f);
			Prism(T.Props, P, P + FVector(0, 0, 900.f), 300.f, 300.f, 16, Col(0.82f, 0.82f, 0.8f));
			Prism(T.Props, P + FVector(0, 0, 900.f), P + FVector(0, 0, 1050.f), 300.f, 20.f, 16, Col(0.7f, 0.7f, 0.68f), false);
		}
	}
	for (float X = -L * 0.5f; X <= L * 0.5f; X += 800.f)
	{
		TBox(T.Props, F, X, -W * 0.5f - 300.f, 0.f, FVector(30.f, 30.f, 500.f), Col(0.4f, 0.4f, 0.42f));
	}
	Prism(T.Props, F.At(-L * 0.5f, -W * 0.5f - 300.f, 520.f), F.At(L * 0.5f, -W * 0.5f - 300.f, 520.f), 35.f, 35.f, 8, Col(0.6f, 0.55f, 0.45f));
	static const TCHAR* Names[] = {TEXT("ЗАВОД ЖБИ"), TEXT("КОКСОХИМИЧЕСКИЙ ЗАВОД"), TEXT("МАШИНОСТРОИТЕЛЬНЫЙ ЗАВОД"), TEXT("ХИМПРОМ"), TEXT("ТЭЦ"),
		TEXT("АВТОБАЗА № 3"), TEXT("ХЛЕБОЗАВОД"), TEXT("МОЛОКОЗАВОД"), TEXT("ЛИТЕЙНЫЙ ЦЕХ"), TEXT("РЕМОНТНО-МЕХАНИЧЕСКИЙ ЦЕХ"), TEXT("ЭЛЕВАТОР"),
		TEXT("ЛОКОМОТИВНОЕ ДЕПО"), TEXT("ШВЕЙНАЯ ФАБРИКА"), TEXT("МЯСОКОМБИНАТ"), TEXT("ЗАВОД МЕТАЛЛОКОНСТРУКЦИЙ")};
	Label(FTransform((-F.Ay).Rotation(), F.At(0.f, -W * 0.5f - 8.f, H * 0.92f)), Names[V % UE_ARRAY_COUNT(Names)], FMath::Min(L * 0.7f, 2400.f), 160.f, 72,
		FLinearColor(0.95f, 0.95f, 0.9f), FLinearColor(0.12f, 0.2f, 0.4f));
}

// ---------------------------------------------------------------------------------------------
// Школа и детский сад
// ---------------------------------------------------------------------------------------------
void FPeregonKit::School(const rgen::Building& B, FPeregonTownBuffers& T, bool bKindergarten)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	const float H = static_cast<float>(B.Height() * 100.0);
	const uint32 V = B.Seed >> 7;
	static const FLinearColor Tints[] = {FLinearColor(0.98f, 0.9f, 0.7f), FLinearColor(0.9f, 0.95f, 0.85f), FLinearColor(0.98f, 0.85f, 0.78f),
		FLinearColor(0.85f, 0.9f, 0.98f)};
	const FColor Tint = Col(Pick(Tints, V));
	FPeregonMeshBuffers& Walls = bKindergarten ? T.PanelTile : T.Ribbon;
	const float U0 = bKindergarten ? 56.f : 0.f;
	const float U1 = bKindergarten ? 253.f : 300.f;
	const float Z0 = bKindergarten ? 61.f : 52.f;
	const float Z1 = bKindergarten ? 184.f : 193.f;
	// Главный корпус вдоль фасада, крылья назад (П-образно).
	const float MainW = 1200.f;
	const float MainY = -W * 0.5f + MainW * 0.5f;
	Walls.AddBox(F.At(0.f, MainY, 0.f), F.Yaw, FVector(L, MainW, H), FVector2D(300.f, 280.f), Tint, false, false);
	T.Roofs.AddBox(F.At(0.f, MainY, H), F.Yaw, FVector(L + 20.f, MainW + 20.f, 45.f), FVector2D(400.f, 400.f), FColor::White, true, false);
	LitWindowsBox(T, F, 0.f, MainY, L, MainW, H, U0, U1, Z0, Z1, 0.06f, B.Seed, true);
	const float WingL = bKindergarten ? 900.f : 1200.f;
	const float WingDepth = W - MainW;
	if (!bKindergarten)
	{
		for (float S : {-1.f, 1.f})
		{
			const float X = S * (L * 0.5f - WingL * 0.5f);
			const float Y = MainY + MainW * 0.5f + WingDepth * 0.5f;
			Walls.AddBox(F.At(X, Y, 0.f), F.Yaw, FVector(WingL, WingDepth, H - 280.f), FVector2D(300.f, 280.f), Tint, false, false);
			T.Roofs.AddBox(F.At(X, Y, H - 280.f), F.Yaw, FVector(WingL + 20.f, WingDepth + 20.f, 45.f), FVector2D(400.f, 400.f), FColor::White, true, false);
		}
		// Спортзал — высокий объём с большими окнами между крыльями.
		T.Ribbon.AddBox(F.At(0.f, MainY + MainW * 0.5f + 600.f, 0.f), F.Yaw, FVector(L - 2 * WingL - 200.f, 1200.f, 650.f), FVector2D(300.f, 280.f),
			Col(0.88f, 0.88f, 0.86f), false, false);
		T.MetalRoofs.AddBox(F.At(0.f, MainY + MainW * 0.5f + 600.f, 650.f), F.Yaw, FVector(L - 2 * WingL - 180.f, 1220.f, 30.f), FVector2D(400.f, 400.f),
			FColor::White, true, false);
	}
	else
	{
		// Веранды на участке: навесы на столбиках, в разные цвета; песочницы, горка.
		static const FLinearColor Fun[] = {FLinearColor(0.9f, 0.3f, 0.2f), FLinearColor(0.2f, 0.5f, 0.8f), FLinearColor(0.95f, 0.75f, 0.15f),
			FLinearColor(0.3f, 0.65f, 0.35f)};
		for (int32 K = 0; K < 3; ++K)
		{
			const float X = -L * 0.35f + K * L * 0.35f;
			const float Y = MainY + MainW * 0.5f + WingDepth * 0.55f;
			const FColor C = Col(Fun[(V + K) % 4]);
			for (float Sx : {-180.f, 180.f})
			{
				for (float Sy : {-120.f, 120.f})
				{
					TBox(T.Props, F, X + Sx, Y + Sy, 0.f, FVector(8.f, 8.f, 230.f), C);
				}
			}
			TBox(T.Props, F, X, Y, 230.f, FVector(420.f, 300.f, 12.f), C, true, true);
			TBox(T.Wood, F, X, Y + 140.f, 0.f, FVector(360.f, 10.f, 100.f), FColor::White);
			TBox(T.Props, F, X + 250.f, Y - 300.f, 0.f, FVector(160.f, 160.f, 25.f), Col(0.85f, 0.75f, 0.5f));
		}
	}
	// Крыльцо с козырьком и вывеской.
	const FVector Face = F.At(0.f, -W * 0.5f - 3.f, 0.f);
	TBox(T.Concrete, F, 0.f, -W * 0.5f - 150.f, 0.f, FVector(700.f, 300.f, 45.f), Col(0.62f, 0.62f, 0.6f));
	T.Roofs.AddBox(F.At(0.f, -W * 0.5f - 160.f, 320.f), F.Yaw, FVector(760.f, 330.f, 25.f), FVector2D(400.f, 400.f), FColor::White, true, true);
	for (float X : {-330.f, 330.f})
	{
		TBox(T.Props, F, X, -W * 0.5f - 300.f, 45.f, FVector(20.f, 20.f, 275.f), Col(0.85f, 0.85f, 0.82f));
	}
	Quad(T.Props, Face - F.Ax * 120.f + FVector(0, 0, 45.f), Face + F.Ax * 120.f + FVector(0, 0, 45.f), Face + F.Ax * 120.f + FVector(0, 0, 290.f),
		Face - F.Ax * 120.f + FVector(0, 0, 290.f), -F.Ay, Col(0.35f, 0.25f, 0.15f));
	DoorLamp(T, F.At(0.f, -W * 0.5f - 5.f, 305.f), -F.Ay, B.Seed);
	static const TCHAR* Kinder[] = {TEXT("«Солнышко»"), TEXT("«Ромашка»"), TEXT("«Колокольчик»"), TEXT("«Теремок»"), TEXT("«Ласточка»"), TEXT("«Сказка»"),
		TEXT("«Берёзка»"), TEXT("«Журавушка»"), TEXT("«Звёздочка»"), TEXT("«Светлячок»")};
	const FString Title = bKindergarten ? FString::Printf(TEXT("ДЕТСКИЙ САД %s"), Kinder[V % UE_ARRAY_COUNT(Kinder)])
										: FString::Printf(TEXT("ШКОЛА № %d"), 1 + static_cast<int32>(V % 64u));
	const FShopSign Sign{*Title, bKindergarten ? FLinearColor(0.95f, 0.75f, 0.2f) : FLinearColor(0.1f, 0.25f, 0.5f), bKindergarten ? InkDark : InkWhite};
	SignBoard(*this, T, F.At(0.f, -W * 0.5f - 6.f, H - 60.f), -F.Ay, FMath::Min(L * 0.5f, 1600.f), 110.f, Sign);
	if (!bKindergarten)
	{
		// Флагшток у крыльца.
		TBox(T.Props, F, 500.f, -W * 0.5f - 600.f, 0.f, FVector(10.f, 10.f, 900.f), Col(0.8f, 0.8f, 0.8f));
		const FColor Flag[] = {Col(0.95f, 0.95f, 0.95f), Col(0.1f, 0.25f, 0.65f), Col(0.75f, 0.1f, 0.08f)};
		for (int32 K = 0; K < 3; ++K)
		{
			TBox(T.Props, F, 500.f + 75.f, -W * 0.5f - 600.f, 880.f - (K + 1) * 33.f, FVector(140.f, 2.f, 33.f), Flag[K], true, true);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Дом культуры
// ---------------------------------------------------------------------------------------------
void FPeregonKit::CultureHouse(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	const float H = static_cast<float>(B.Height() * 100.0);
	const uint32 V = B.Seed >> 5;
	const FColor Tint = Col(Pick(StuccoTints, V));
	const float HallW = W - 900.f;
	const float HallY = W * 0.5f - HallW * 0.5f;
	T.Stucco.AddBox(F.At(0.f, HallY, 0.f), F.Yaw, FVector(L - 400.f, HallW, H), FVector2D(300.f, 280.f), Tint, false, false);
	TBox(T.Props, F, 0.f, HallY, H - 60.f, FVector(L - 340.f, HallW + 60.f, 60.f), Col(0.95f, 0.95f, 0.92f), true, true); // карниз
	T.Roofs.AddBox(F.At(0.f, HallY, H), F.Yaw, FVector(L - 400.f, HallW, 40.f), FVector2D(400.f, 400.f), FColor::White, true, false);
	// Сценическая коробка повыше сзади.
	T.Stucco.AddBox(F.At(0.f, HallY + HallW * 0.25f, H), F.Yaw, FVector(L * 0.5f, HallW * 0.4f, 500.f), FVector2D(300.f, 280.f), Tint, false, false);
	T.Roofs.AddBox(F.At(0.f, HallY + HallW * 0.25f, H + 500.f), F.Yaw, FVector(L * 0.5f + 20.f, HallW * 0.4f + 20.f, 30.f), FVector2D(400.f, 400.f),
		FColor::White, true, false);
	LitWindowsBox(T, F, 0.f, HallY, L - 400.f, HallW, H, 103.f, 206.f, 52.f, 227.f, 0.2f, B.Seed, true);
	// Портик: колонны, антаблемент, фронтон.
	const float PortY = HallY - HallW * 0.5f - 300.f;
	const int32 Columns = 6;
	const float Span = L * 0.55f;
	for (int32 K = 0; K < Columns; ++K)
	{
		const float X = -Span * 0.5f + Span * K / (Columns - 1);
		const FVector P = F.At(X, PortY, 120.f);
		Prism(T.Props, P, P + FVector(0, 0, H - 260.f), 45.f, 38.f, 12, Col(0.96f, 0.96f, 0.94f), false);
	}
	TBox(T.Props, F, 0.f, PortY + 60.f, H - 140.f, FVector(Span + 180.f, 360.f, 120.f), Col(0.96f, 0.96f, 0.94f), true, true);
	const FVector G0 = F.At(-Span * 0.5f - 90.f, PortY - 120.f, H - 20.f);
	const FVector G1 = F.At(Span * 0.5f + 90.f, PortY - 120.f, H - 20.f);
	const FVector G2 = F.At(0.f, PortY - 120.f, H + 280.f);
	Quad(T.Props, G0, G1, G2, G2, -F.Ay, Col(0.94f, 0.94f, 0.92f));
	for (int32 S = 0; S < 5; ++S)
	{
		TBox(T.Concrete, F, 0.f, PortY - 100.f - S * 70.f + 35.f, 0.f, FVector(Span + 200.f + S * 60.f, 420.f - S * 0.f, 120.f - S * 24.f), Col(0.66f, 0.66f, 0.64f));
	}
	static const TCHAR* Names[] = {TEXT("«Горняк»"), TEXT("«Химик»"), TEXT("«Строитель»"), TEXT("«Юность»"), TEXT("«Энергетик»"), TEXT("«Металлург»"),
		TEXT("«Октябрь»"), TEXT("«Родина»"), TEXT("«Шахтёр»"), TEXT("«Железнодорожник»")};
	Label(FTransform((-F.Ay).Rotation(), F.At(0.f, PortY - 125.f, H - 80.f)), FString::Printf(TEXT("ДОМ КУЛЬТУРЫ %s"), Names[V % UE_ARRAY_COUNT(Names)]),
		Span, 90.f, 56, FLinearColor(0.35f, 0.25f, 0.1f), FLinearColor(0.96f, 0.96f, 0.94f));
	// Афиши на щитах у входа.
	static const TCHAR* Posters[] = {TEXT("КИНО\nсегодня 19:00"), TEXT("ТАНЦЫ\nсуббота"), TEXT("КОНЦЕРТ\nхор ветеранов"), TEXT("КРУЖКИ\nзапись открыта")};
	for (int32 K = 0; K < 2; ++K)
	{
		const float X = (K == 0 ? -1.f : 1.f) * (Span * 0.5f + 350.f);
		TBox(T.Props, F, X, PortY - 300.f, 0.f, FVector(160.f, 10.f, 230.f), Col(0.3f, 0.3f, 0.32f));
		Label(FTransform((-F.Ay).Rotation(), F.At(X, PortY - 307.f, 160.f)), Posters[(V + K) % 4], 140.f, 100.f, 34, InkDark,
			FLinearColor(0.95f, 0.85f, 0.6f), 256);
	}
	DoorLamp(T, F.At(-Span * 0.25f, PortY + 60.f, H - 150.f), -F.Ay, B.Seed);
	DoorLamp(T, F.At(Span * 0.25f, PortY + 60.f, H - 150.f), -F.Ay, B.Seed + 1);
}

// ---------------------------------------------------------------------------------------------
// Администрация и поликлиника
// ---------------------------------------------------------------------------------------------
void FPeregonKit::Office(const rgen::Building& B, FPeregonTownBuffers& T, bool bClinic)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	const float H = static_cast<float>(B.Height() * 100.0);
	const uint32 V = B.Seed >> 5;
	if (bClinic)
	{
		T.Ribbon.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), Col(0.95f, 0.96f, 0.98f), false, false);
		LitWindowsBox(T, F, 0.f, 0.f, L, W, H, 10.f, 290.f, 52.f, 193.f, 0.12f, B.Seed);
		// Синий пояс над первым этажом и козырёк над входом с пандусом.
		TBox(T.Props, F, 0.f, 0.f, 300.f, FVector(L + 10.f, W + 10.f, 40.f), Col(0.2f, 0.4f, 0.7f), true, true);
		T.Roofs.AddBox(F.At(0.f, 0.f, H), F.Yaw, FVector(L + 20.f, W + 20.f, 45.f), FVector2D(400.f, 400.f), FColor::White, true, false);
		const FShopSign Sign{TEXT("ПОЛИКЛИНИКА"), FLinearColor(0.95f, 0.95f, 0.95f), FLinearColor(0.1f, 0.3f, 0.6f)};
		SignBoard(*this, T, F.At(0.f, -W * 0.5f - 6.f, H - 80.f), -F.Ay, FMath::Min(L * 0.5f, 1400.f), 100.f, Sign);
		TBox(T.Concrete, F, L * 0.25f, -W * 0.5f - 300.f, 0.f, FVector(150.f, 600.f, 20.f), Col(0.6f, 0.6f, 0.6f)); // пандус
	}
	else
	{
		const FColor Tint = Col(Pick(StuccoTints, V));
		T.Stucco.AddBox(F.Base, F.Yaw, FVector(L, W, H), FVector2D(300.f, 280.f), Tint, false, false);
		LitWindowsBox(T, F, 0.f, 0.f, L, W, H, 103.f, 206.f, 52.f, 227.f, 0.2f, B.Seed);
		// Центральный ризалит, колонны у входа, карниз.
		T.Stucco.AddBox(F.At(0.f, -W * 0.5f - 60.f, 0.f), F.Yaw, FVector(L * 0.3f, 120.f, H + 100.f), FVector2D(300.f, 280.f), Tint, false, false);
		TBox(T.Props, F, 0.f, 0.f, H - 40.f, FVector(L + 60.f, W + 60.f, 40.f), Col(0.95f, 0.95f, 0.92f), true, true);
		T.Roofs.AddBox(F.At(0.f, 0.f, H), F.Yaw, FVector(L, W, 40.f), FVector2D(400.f, 400.f), FColor::White, true, false);
		T.Roofs.AddBox(F.At(0.f, -W * 0.5f - 60.f, H + 100.f), F.Yaw, FVector(L * 0.3f + 20.f, 140.f, 30.f), FVector2D(400.f, 400.f), FColor::White, true, false);
		for (int32 K = 0; K < 4; ++K)
		{
			const FVector P = F.At(-L * 0.12f + K * L * 0.08f, -W * 0.5f - 260.f, 60.f);
			Prism(T.Props, P, P + FVector(0, 0, 560.f), 30.f, 26.f, 10, Col(0.96f, 0.96f, 0.94f), false);
		}
		T.Roofs.AddBox(F.At(0.f, -W * 0.5f - 200.f, 620.f), F.Yaw, FVector(L * 0.3f, 260.f, 30.f), FVector2D(400.f, 400.f), FColor::White, true, true);
		for (int32 S = 0; S < 3; ++S)
		{
			TBox(T.Concrete, F, 0.f, -W * 0.5f - 230.f - S * 50.f, 0.f, FVector(L * 0.3f + S * 60.f, 260.f, 60.f - S * 20.f), Col(0.66f, 0.66f, 0.64f));
		}
		// Флаг на крыше над входом.
		const FVector Pole = F.At(0.f, -W * 0.5f - 40.f, H + 130.f);
		TBox(T.Props, F, 0.f, -W * 0.5f - 40.f, H + 130.f, FVector(8.f, 8.f, 500.f), Col(0.8f, 0.8f, 0.8f));
		const FColor Flag[] = {Col(0.95f, 0.95f, 0.95f), Col(0.1f, 0.25f, 0.65f), Col(0.75f, 0.1f, 0.08f)};
		for (int32 K = 0; K < 3; ++K)
		{
			T.Props.AddBox(Pole + F.Ax * 85.f + FVector(0, 0, 480.f - (K + 1) * 38.f), F.Yaw, FVector(160.f, 2.f, 38.f), FVector2D(100.f, 100.f), Flag[K], true, true);
		}
		const FShopSign Sign{TEXT("АДМИНИСТРАЦИЯ"), FLinearColor(0.12f, 0.2f, 0.4f), FLinearColor(0.95f, 0.85f, 0.4f)};
		SignBoard(*this, T, F.At(0.f, -W * 0.5f - 125.f, 560.f), -F.Ay, FMath::Min(L * 0.28f, 900.f), 60.f, Sign);
	}
	DoorLamp(T, F.At(0.f, -W * 0.5f - (bClinic ? 5.f : 125.f), 280.f), -F.Ay, B.Seed);
}

// ---------------------------------------------------------------------------------------------
// Мемориал
// ---------------------------------------------------------------------------------------------
void FPeregonKit::Monument(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	const FColor Stone = Col(0.86f, 0.86f, 0.84f);
	const FColor Granite = Col(0.25f, 0.24f, 0.24f);
	TBox(T.Concrete, F, 0.f, 0.f, -20.f, FVector(L, W, 35.f), Col(0.6f, 0.6f, 0.58f));
	for (int32 S = 0; S < 3; ++S)
	{
		TBox(T.Concrete, F, 0.f, W * 0.15f, 15.f, FVector(800.f - S * 160.f, 800.f - S * 160.f, 30.f + S * 30.f), Col(0.7f, 0.7f, 0.68f));
	}
	// Обелиск: сужается к верху, сверху — звезда.
	const float Hb = 1300.f;
	const FVector C0 = F.At(0.f, W * 0.15f, 105.f);
	const FVector C1 = C0 + FVector(0, 0, Hb);
	const float R0 = 130.f;
	const float R1 = 55.f;
	for (int32 K = 0; K < 4; ++K)
	{
		const FVector D0 = (K == 0 ? F.Ax : (K == 1 ? F.Ay : (K == 2 ? -F.Ax : -F.Ay)));
		const FVector D1 = (K == 0 ? F.Ay : (K == 1 ? -F.Ax : (K == 2 ? -F.Ay : F.Ax)));
		const FVector A0 = C0 + (D0 - D1) * R0;
		const FVector A1 = C0 + (D0 + D1) * R0;
		const FVector B0 = C1 + (D0 - D1) * R1;
		const FVector B1 = C1 + (D0 + D1) * R1;
		Quad(T.Concrete, A0, A1, B1, B0, D0, Stone);
		Quad(T.Concrete, B0, B1, C1 + FVector(0, 0, 120.f), C1 + FVector(0, 0, 120.f), (D0 + FVector(0, 0, 0.5f)).GetSafeNormal(), Stone);
	}
	Star(T.Props, C1 + FVector(0, 0, 260.f), FVector::UpVector, F.Ax, 110.f, Col(0.75f, 0.08f, 0.05f));
	// Гранитная плита с датами перед обелиском, вечный огонь, венки.
	TBox(T.Props, F, 0.f, W * 0.15f - 300.f, 30.f, FVector(400.f, 40.f, 120.f), Granite);
	Label(FTransform((-F.Ay).Rotation(), F.At(0.f, W * 0.15f - 322.f, 100.f)), TEXT("1941 — 1945\nВечная память героям"), 360.f, 90.f, 40,
		FLinearColor(0.95f, 0.8f, 0.4f), FLinearColor(0.2f, 0.19f, 0.19f), 256);
	const FVector Fire = F.At(0.f, W * 0.15f - 600.f, 0.f);
	Prism(T.Props, Fire, Fire + FVector(0, 0, 40.f), 120.f, 120.f, 5, Granite);
	Star(T.Props, Fire + FVector(0, 0, 41.f), F.Ax, F.Ay, 100.f, Col(0.2f, 0.19f, 0.19f));
	Prism(T.Lamps, Fire + FVector(0, 0, 40.f), Fire + FVector(0, 0, 110.f), 25.f, 2.f, 6, FColor::White, false);
	FPeregonLightSpot Flame;
	Flame.Location = Fire + FVector(0, 0, 150.f);
	Flame.Direction = FVector::DownVector;
	Flame.Seed = B.Seed;
	T.LampSpots.Add(Flame);
	for (float X : {-200.f, 200.f})
	{
		const FVector Wr = F.At(X, W * 0.15f - 340.f, 30.f);
		Prism(T.Props, Wr, Wr + FVector(0, 0, 70.f), 40.f, 40.f, 10, Col(0.15f, 0.35f, 0.15f));
		TBox(T.Props, F, X, W * 0.15f - 382.f, 40.f, FVector(10.f, 4.f, 50.f), Col(0.75f, 0.1f, 0.08f));
	}
	// Ели и лавки по краям.
	for (float X : {-L * 0.4f, L * 0.4f})
	{
		Tree(F.At(X, W * 0.35f, 0.f), true, 0.55f);
		Bench(F.At(X, -W * 0.2f, 0.f), FRotator(0.f, F.Yaw + 90.f, 0.f));
	}
}

// ---------------------------------------------------------------------------------------------
// Водонапорная башня
// ---------------------------------------------------------------------------------------------
void FPeregonKit::WaterTower(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const FColor Brick = Col(0.62f, 0.3f, 0.22f);
	const FVector Base = F.At(0.f, 0.f, 0.f);
	Prism(T.Props, Base, Base + FVector(0, 0, 1500.f), 330.f, 270.f, 16, Brick, false);
	for (float Z : {500.f, 1000.f, 1480.f})
	{
		Prism(T.Props, Base + FVector(0, 0, Z), Base + FVector(0, 0, Z + 25.f), 340.f - Z * 0.04f, 340.f - Z * 0.04f, 16, Col(0.9f, 0.88f, 0.84f), false);
	}
	// Бак, конусная крыша, лестница.
	Prism(T.Props, Base + FVector(0, 0, 1500.f), Base + FVector(0, 0, 2000.f), 420.f, 420.f, 16, Col(0.45f, 0.55f, 0.5f));
	Prism(T.MetalRoofs, Base + FVector(0, 0, 2000.f), Base + FVector(0, 0, 2250.f), 450.f, 30.f, 16, FColor::White, false);
	for (float Z = 0.f; Z < 1500.f; Z += 40.f)
	{
		T.Props.AddBox(Base + F.Ax * 340.f + FVector(0, 0, Z), F.Yaw, FVector(6.f, 60.f, 4.f), FVector2D(100.f, 100.f), Col(0.3f, 0.3f, 0.3f), true, true);
	}
	Quad(T.Props, Base + F.Ax * 335.f + F.Ay * -50.f, Base + F.Ax * 335.f + F.Ay * 50.f, Base + F.Ax * 330.f + F.Ay * 50.f + FVector(0, 0, 210.f),
		Base + F.Ax * 330.f + F.Ay * -50.f + FVector(0, 0, 210.f), F.Ax, Col(0.35f, 0.22f, 0.12f));
}

// ---------------------------------------------------------------------------------------------
// Киоски
// ---------------------------------------------------------------------------------------------
void FPeregonKit::Kiosk(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const uint32 Kind = static_cast<uint32>(FMath::Max(0, B.Floors)) % 7u;
	const FVector Front = -F.Ay;
	if (Kind == 3)
	{
		// Бочка «КВАС» на колёсах.
		const FVector A = F.At(-110.f, 0.f, 95.f);
		const FVector Bq = F.At(110.f, 0.f, 95.f);
		Prism(T.Props, A, Bq, 70.f, 70.f, 14, Col(0.95f, 0.75f, 0.1f));
		for (float X : {-60.f, 60.f})
		{
			for (float S : {-1.f, 1.f})
			{
				const FVector W0 = F.At(X, S * 70.f, 30.f);
				Prism(T.Wheels, W0, W0 + F.Ay * (S * 14.f), 30.f, 30.f, 10, FColor::White);
			}
		}
		TBox(T.Props, F, -170.f, 0.f, 20.f, FVector(120.f, 6.f, 6.f), Col(0.3f, 0.3f, 0.3f));
		Label(FTransform(Front.Rotation(), F.At(0.f, -72.f, 115.f)), TEXT("КВАС"), 110.f, 40.f, 40, FLinearColor(0.6f, 0.1f, 0.05f),
			FLinearColor(0.95f, 0.75f, 0.1f), 256);
		TBox(T.Props, F, 0.f, -110.f, 0.f, FVector(80.f, 60.f, 90.f), Col(0.9f, 0.9f, 0.88f)); // столик продавщицы
		return;
	}
	static const FShopSign Kinds[] = {{TEXT("ПЕЧАТЬ"), FLinearColor(0.12f, 0.3f, 0.6f), InkWhite},
		{TEXT("МОРОЖЕНОЕ"), FLinearColor(0.95f, 0.95f, 0.98f), FLinearColor(0.2f, 0.35f, 0.75f)},
		{TEXT("ЦВЕТЫ"), FLinearColor(0.75f, 0.25f, 0.45f), InkWhite},
		{TEXT("КВАС"), FLinearColor(0.95f, 0.75f, 0.1f), InkDark},
		{TEXT("ПИРОЖКИ · ЧЕБУРЕКИ"), FLinearColor(0.8f, 0.5f, 0.15f), InkDark},
		{TEXT("ТАБАК"), FLinearColor(0.35f, 0.2f, 0.1f), InkWhite},
		{TEXT("РЕМОНТ ЧАСОВ · КЛЮЧИ"), FLinearColor(0.2f, 0.2f, 0.22f), FLinearColor(0.9f, 0.8f, 0.3f)}};
	const FShopSign& Sign = Kinds[Kind];
	const float L = 300.f;
	const float W = 220.f;
	const float H = 250.f;
	const FColor Body = Col(Sign.Paper * 0.8f + FLinearColor(0.15f, 0.15f, 0.15f));
	TBox(T.Props, F, 0.f, 0.f, 0.f, FVector(L, W, 90.f), Body);
	TBox(T.Glass, F, 0.f, 0.f, 90.f, FVector(L - 10.f, W - 10.f, 130.f), FColor::White, false);
	for (float X : {-L * 0.5f + 5.f, 0.f, L * 0.5f - 5.f})
	{
		TBox(T.Props, F, X, -W * 0.5f + 5.f, 90.f, FVector(8.f, 8.f, 130.f), Body);
	}
	TBox(T.Props, F, 0.f, 0.f, 220.f, FVector(L + 30.f, W + 30.f, 30.f), Body, true, true);
	Label(FTransform(Front.Rotation(), F.At(0.f, -W * 0.5f - 18.f, 235.f)), Sign.Text, L, 28.f, 22, Sign.Ink, Sign.Paper, 256);
	Quad(T.Windows, F.At(-L * 0.3f, -W * 0.5f + 2.f, 95.f), F.At(L * 0.3f, -W * 0.5f + 2.f, 95.f), F.At(L * 0.3f, -W * 0.5f + 2.f, 200.f),
		F.At(-L * 0.3f, -W * 0.5f + 2.f, 200.f), Front, FColor::White);
	(void)H;
}

// ---------------------------------------------------------------------------------------------
// Рынок
// ---------------------------------------------------------------------------------------------
void FPeregonKit::Market(const rgen::Building& B, FPeregonTownBuffers& T)
{
	using namespace PeregonTown;
	const FBldFrame F = MakeFrame(B);
	const float L = static_cast<float>(B.Length * 100.0);
	const float W = static_cast<float>(B.Width * 100.0);
	static const FLinearColor Awnings[] = {FLinearColor(0.15f, 0.35f, 0.7f), FLinearColor(0.75f, 0.12f, 0.1f), FLinearColor(0.2f, 0.55f, 0.3f),
		FLinearColor(0.95f, 0.7f, 0.15f)};
	static const FLinearColor Goods[] = {FLinearColor(0.95f, 0.5f, 0.1f), FLinearColor(0.3f, 0.65f, 0.2f), FLinearColor(0.8f, 0.15f, 0.12f),
		FLinearColor(0.95f, 0.85f, 0.3f), FLinearColor(0.5f, 0.3f, 0.6f)};
	TBox(T.Concrete, F, 0.f, 0.f, -10.f, FVector(L, W, 15.f), Col(0.5f, 0.5f, 0.5f));
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float Y = -W * 0.3f + Row * W * 0.3f;
		for (int32 K = 0; K < 6; ++K)
		{
			const float X = -L * 0.4f + K * L * 0.16f;
			const uint32 H2 = rgen::Hash(B.Seed, static_cast<uint32>(Row * 10 + K));
			TBox(T.Wood, F, X, Y, 0.f, FVector(L * 0.14f, 120.f, 95.f), FColor::White); // прилавок
			for (float Sx : {-L * 0.07f, L * 0.07f})
			{
				TBox(T.Props, F, X + Sx, Y + 60.f, 0.f, FVector(6.f, 6.f, 250.f), Col(0.3f, 0.3f, 0.32f));
			}
			const FColor Aw = Col(Awnings[H2 % 4]);
			Quad(T.Props, F.At(X - L * 0.075f, Y + 70.f, 250.f), F.At(X + L * 0.075f, Y + 70.f, 250.f), F.At(X + L * 0.075f, Y - 110.f, 215.f),
				F.At(X - L * 0.075f, Y - 110.f, 215.f), FVector::UpVector, Aw);
			for (int32 Crate = 0; Crate < 3; ++Crate)
			{
				TBox(T.Props, F, X - L * 0.04f + Crate * L * 0.04f, Y - 20.f, 95.f, FVector(L * 0.035f, 50.f, 25.f), Col(Goods[(H2 + Crate) % 5]));
			}
		}
	}
	// Арка входа с вывеской.
	for (float S : {-1.f, 1.f})
	{
		TBox(T.Props, F, S * 300.f, -W * 0.5f, 0.f, FVector(30.f, 30.f, 420.f), Col(0.35f, 0.4f, 0.45f));
	}
	TBox(T.Props, F, 0.f, -W * 0.5f, 400.f, FVector(640.f, 20.f, 90.f), Col(0.12f, 0.3f, 0.55f), true, true);
	Label(FTransform((-F.Ay).Rotation(), F.At(0.f, -W * 0.5f - 12.f, 445.f)), TEXT("РЫНОК"), 560.f, 80.f, 64, InkWhite, FLinearColor(0.12f, 0.3f, 0.55f));
}

// ---------------------------------------------------------------------------------------------
// Достопримечательности
// ---------------------------------------------------------------------------------------------
namespace PeregonTown
{
// Купол: стопка колец по дуге окружности (полусфера, сплющенная до высоты Hgt).
void Dome(FPeregonMeshBuffers& Buf, const FVector& Base, float Radius, float Hgt, int32 Rings, int32 Sides, const FColor& C)
{
	for (int32 K = 0; K < Rings; ++K)
	{
		const float A0 = 0.5f * PI * K / Rings;
		const float A1 = 0.5f * PI * (K + 1) / Rings;
		Prism(Buf, Base + FVector(0, 0, Hgt * FMath::Sin(A0)), Base + FVector(0, 0, Hgt * FMath::Sin(A1)), Radius * FMath::Cos(A0),
			FMath::Max(1.f, Radius * FMath::Cos(A1)), Sides, C, false);
	}
}
} // namespace PeregonTown

void FPeregonKit::Landmark(const FString& Key, const FVector& Base, float YawDeg, float RadiusCm, const FVector& FrontDir, const FString& Name)
{
	using namespace PeregonTown;
	FPeregonTownBuffers T;
	// Система координат: X — к главному фасаду, Y — вправо.
	FBldFrame F;
	F.Base = Base;
	const FVector Front = FrontDir.IsNearlyZero() ? FRotator(0.f, YawDeg, 0.f).Vector() : FrontDir.GetSafeNormal2D();
	F.Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Front.Y, Front.X)));
	F.Ax = Front;
	F.Ay = FVector::CrossProduct(FVector::UpVector, Front).GetSafeNormal();

	if (Key == TEXT("opera"))
	{
		// Новосибирский театр оперы и балета (1945): самое большое театральное здание России — прямоугольный
		// объём ~120 × 100 м, в центре — барабан и купол диаметром 60 м и высотой 35 м, серебристый; на
		// главном фасаде к площади Ленина — портик с колоннадой между пилонами, над ним — скульптурная группа;
		// сзади — сценическая коробка. Стены светло-серые с бежевым.
		const float S = RadiusCm > 1000.f ? FMath::Clamp(RadiusCm / 7800.f, 0.75f, 1.3f) : 1.f; // подгонка под контур из OSM
		const float L = 11000.f * S;   // глубина (от фасада к сцене)
		const float W = 9600.f * S;    // ширина
		const float H = 2200.f;
		const FColor Wall = Col(0.86f, 0.84f, 0.78f);
		const FColor Trim = Col(0.94f, 0.93f, 0.9f);
		const FColor Silver = Col(0.74f, 0.77f, 0.8f);
		TBox(T.Concrete, F, 0.f, 0.f, -60.f, FVector(L + 1500.f, W + 1200.f, 210.f), Col(0.62f, 0.61f, 0.58f)); // стилобат
		T.Stucco.AddBox(F.At(0.f, 0.f, 150.f), F.Yaw, FVector(L, W, H), FVector2D(600.f, 560.f), Wall, false, false);
		TBox(T.Props, F, 0.f, 0.f, 150.f + H - 120.f, FVector(L + 80.f, W + 80.f, 120.f), Trim, true, true); // карниз
		T.Roofs.AddBox(F.At(0.f, 0.f, 150.f + H), F.Yaw, FVector(L, W, 30.f), FVector2D(400.f, 400.f), FColor::White, true, false);
		// Барабан и купол.
		const FVector C0 = F.At(500.f * S, 0.f, 150.f + H);
		Prism(T.Stucco, C0, C0 + FVector(0, 0, 900.f), 3150.f, 3150.f, 40, Wall, false);
		Prism(T.Props, C0 + FVector(0, 0, 880.f), C0 + FVector(0, 0, 980.f), 3250.f, 3250.f, 40, Trim, false);
		Dome(T.Props, C0 + FVector(0, 0, 980.f), 3050.f, 3500.f, 14, 48, Silver);
		// Рёбра купола.
		for (int32 K = 0; K < 24; ++K)
		{
			const float A = 2.f * PI * K / 24.f;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
			for (int32 Q = 0; Q < 10; ++Q)
			{
				const float P0 = 0.5f * PI * Q / 10.f;
				const float P1 = 0.5f * PI * (Q + 1) / 10.f;
				const FVector A0 = C0 + FVector(0, 0, 980.f) + Dir * (3065.f * FMath::Cos(P0)) + FVector(0, 0, 3515.f * FMath::Sin(P0));
				const FVector A1 = C0 + FVector(0, 0, 980.f) + Dir * (3065.f * FMath::Cos(P1)) + FVector(0, 0, 3515.f * FMath::Sin(P1));
				Prism(T.Props, A0, A1, 25.f, 25.f, 4, Col(0.6f, 0.62f, 0.66f), false);
			}
		}
		const FVector Top = C0 + FVector(0, 0, 980.f + 3500.f);
		Prism(T.Props, Top, Top + FVector(0, 0, 350.f), 300.f, 300.f, 16, Silver);
		Dome(T.Props, Top + FVector(0, 0, 350.f), 320.f, 250.f, 4, 16, Silver);
		Prism(T.Props, Top + FVector(0, 0, 600.f), Top + FVector(0, 0, 1000.f), 30.f, 2.f, 6, Col(0.8f, 0.7f, 0.3f), false);
		// Сценическая коробка сзади.
		T.Stucco.AddBox(F.At(-L * 0.32f, 0.f, 150.f + H), F.Yaw, FVector(L * 0.3f, W * 0.62f, 900.f), FVector2D(600.f, 560.f), Wall, false, false);
		T.Roofs.AddBox(F.At(-L * 0.32f, 0.f, 150.f + H + 900.f), F.Yaw, FVector(L * 0.3f + 40.f, W * 0.62f + 40.f, 30.f), FVector2D(400.f, 400.f),
			FColor::White, true, false);
		// Главный фасад: пилоны, колоннада, антаблемент, аттик со скульптурами, лестница.
		const float Fx = L * 0.5f;
		for (float Side : {-1.f, 1.f})
		{
			T.Stucco.AddBox(F.At(Fx + 300.f, Side * 2900.f, 150.f), F.Yaw, FVector(1200.f, 1300.f, H + 300.f), FVector2D(600.f, 560.f), Wall, false, false);
			TBox(T.Props, F, Fx + 300.f, Side * 2900.f, 150.f + H + 200.f, FVector(1260.f, 1360.f, 100.f), Trim, true, true);
		}
		const int32 Columns = 10;
		for (int32 K = 0; K < Columns; ++K)
		{
			const float Y = -2000.f + 4000.f * K / (Columns - 1);
			const FVector P = F.At(Fx + 700.f, Y, 150.f);
			Prism(T.Props, P, P + FVector(0, 0, 80.f), 130.f, 130.f, 8, Trim);            // база
			Prism(T.Props, P + FVector(0, 0, 80.f), P + FVector(0, 0, 1700.f), 110.f, 95.f, 16, Trim, false);
			Prism(T.Props, P + FVector(0, 0, 1700.f), P + FVector(0, 0, 1800.f), 140.f, 140.f, 8, Trim); // капитель
		}
		TBox(T.Props, F, Fx + 450.f, 0.f, 1950.f, FVector(900.f, 4500.f, 300.f), Trim, true, true);   // антаблемент
		TBox(T.Stucco, F, Fx + 350.f, 0.f, 2250.f, FVector(700.f, 4300.f, 400.f), Wall, true, false);  // аттик
		for (int32 K = 0; K < 7; ++K)
		{
			const float Y = -1800.f + 3600.f * K / 6.f;
			const float Hf = (K == 3) ? 520.f : 420.f;
			TBox(T.Props, F, Fx + 450.f, Y, 2650.f, FVector(110.f, 120.f, Hf), Col(0.78f, 0.76f, 0.72f));   // фигуры
			Prism(T.Props, F.At(Fx + 450.f, Y, 2650.f + Hf), F.At(Fx + 450.f, Y, 2650.f + Hf + 60.f), 40.f, 30.f, 8, Col(0.78f, 0.76f, 0.72f));
		}
		for (int32 Step = 0; Step < 8; ++Step)
		{
			TBox(T.Concrete, F, Fx + 900.f + Step * 60.f, 0.f, -60.f, FVector(500.f - Step * 50.f + 200.f, 5200.f + (8 - Step) * 60.f, 210.f - Step * 20.f),
				Col(0.66f, 0.65f, 0.62f));
		}
		// Двери в портике, название, фонари.
		for (int32 K = 0; K < 5; ++K)
		{
			const float Y = -1600.f + 800.f * K;
			const FVector D0 = F.At(Fx + 3.f, Y - 150.f, 150.f);
			const FVector D1 = F.At(Fx + 3.f, Y + 150.f, 150.f);
			Quad(T.Props, D0, D1, D1 + FVector(0, 0, 550.f), D0 + FVector(0, 0, 550.f), F.Ax, Col(0.35f, 0.24f, 0.14f));
		}
		Label(FTransform(F.Ax.Rotation(), F.At(Fx + 910.f, 0.f, 2100.f)), TEXT("ТЕАТР ОПЕРЫ И БАЛЕТА"), 3600.f, 180.f, 72, FLinearColor(0.35f, 0.3f, 0.2f),
			FLinearColor(0.94f, 0.93f, 0.9f), 1024);
		for (float Y : {-2400.f, -800.f, 800.f, 2400.f})
		{
			DoorLamp(T, F.At(Fx + 1500.f, Y, 450.f), F.Ax, static_cast<uint32>(Y + 5000.f));
			TBox(T.Props, F, Fx + 1500.f, Y, 0.f, FVector(20.f, 20.f, 450.f), Col(0.2f, 0.2f, 0.22f));
		}
	}
	else if (Key == TEXT("stele"))
	{
		// Крылатая стела Новосибирска (восточный въезд, 2023): ~10 м, центральный пилон с вертикальной надписью
		// и гербом наверху, по сторонам — «крылья»: одно — раскрытая книга (наука), другое — крыло самолёта.
		const FColor White = Col(0.95f, 0.95f, 0.94f);
		const FColor Blue = Col(0.08f, 0.28f, 0.62f);
		const FColor Steel = Col(0.72f, 0.75f, 0.8f);
		TBox(T.Concrete, F, 0.f, 0.f, -20.f, FVector(900.f, 400.f, 80.f), Col(0.55f, 0.55f, 0.56f));
		TBox(T.Props, F, 0.f, 0.f, 60.f, FVector(120.f, 220.f, 1050.f), White);
		TBox(T.Props, F, 62.f, 0.f, 160.f, FVector(6.f, 150.f, 860.f), Blue);
		TBox(T.Props, F, -62.f, 0.f, 160.f, FVector(6.f, 150.f, 860.f), Blue);
		// Вертикальная надпись — с обеих сторон.
		const FString Title = Name.IsEmpty() ? FString(TEXT("НОВОСИБИРСК")) : Name.ToUpper();
		for (float Side : {1.f, -1.f})
		{
			const FVector N = F.Ax * Side;
			Label(FTransform(FRotator(0.f, static_cast<float>(N.Rotation().Yaw), 90.f), F.At(Side * 66.f, 0.f, 590.f)), Title, 820.f, 120.f, 96,
				FLinearColor(0.95f, 0.95f, 0.94f), FLinearColor(0.08f, 0.28f, 0.62f), 1024);
		}
		// Герб: зелёный щит, серебряный пояс по диагонали, золотая корона над ним.
		TBox(T.Props, F, 0.f, 0.f, 1110.f, FVector(60.f, 170.f, 200.f), Col(0.1f, 0.45f, 0.22f));
		T.Props.AddBox(F.At(32.f, 0.f, 1150.f), F.Yaw, FVector(4.f, 200.f, 30.f), FVector2D(100.f, 100.f), White, true, true);
		TBox(T.Props, F, 0.f, 0.f, 1310.f, FVector(50.f, 130.f, 50.f), Col(0.85f, 0.7f, 0.25f));
		// Крылья: наборы изогнутых лопастей, уходящих вверх и в стороны.
		for (float Side : {-1.f, 1.f})
		{
			for (int32 K = 0; K < 7; ++K)
			{
				const float Y = Side * (180.f + K * 85.f);
				const float Z0 = 150.f + K * 70.f;
				const float Hk = 520.f - K * 35.f + (Side > 0.f ? K * 20.f : 0.f);
				T.Props.AddBox(F.At(-20.f + (Side > 0.f ? K * 25.f : -K * 10.f), Y, Z0), F.Yaw + Side * (8.f + K * 7.f), FVector(Side > 0.f ? 40.f : 14.f, 70.f, Hk),
					FVector2D(100.f, 100.f), Side > 0.f ? Steel : White, true, true);
			}
		}
		// Подсветка снизу.
		for (float Y : {-300.f, 300.f})
		{
			T.Lamps.AddBox(F.At(150.f, Y, 30.f), F.Yaw, FVector(30.f, 30.f, 20.f), FVector2D(100.f, 100.f), FColor::White, true, false);
		}
	}
	EmitTown(T);
}
