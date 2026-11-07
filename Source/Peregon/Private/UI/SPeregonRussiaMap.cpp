#include "UI/SPeregonRussiaMap.h"

#include "Art/PeregonArtSubsystem.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TextureResource.h"

namespace PeregonRussia
{
struct FLatLon
{
	float Lat;
	float Lon;
};

// Упрощённый контур материковой России (широта, долгота), по часовой стрелке от Печенги.
// Калининград, острова и Чукотка за 180° опущены — это карта-схема, а не атлас.
const FLatLon Outline[] = {
	{69.7f, 30.9f}, {69.2f, 36.0f}, {68.0f, 40.5f}, {66.4f, 44.0f}, {67.7f, 47.0f}, {68.6f, 53.0f}, {68.8f, 58.0f}, {69.7f, 61.0f}, {68.5f, 66.0f},
	{70.5f, 67.0f}, {72.8f, 69.5f}, {71.5f, 72.5f}, {72.5f, 75.0f}, {71.0f, 78.5f}, {72.4f, 80.5f}, {73.5f, 80.0f}, {73.8f, 86.0f}, {75.5f, 89.0f},
	{76.4f, 96.0f}, {77.7f, 104.0f}, {76.0f, 111.0f}, {73.6f, 113.0f}, {73.0f, 118.0f}, {73.5f, 124.0f}, {72.5f, 129.0f}, {71.0f, 131.5f},
	{71.9f, 139.0f}, {72.4f, 146.0f}, {71.3f, 152.0f}, {70.8f, 160.0f}, {69.6f, 167.0f}, {69.9f, 172.0f}, {69.0f, 176.0f}, {68.8f, 180.0f},
	{66.5f, 180.0f}, {64.8f, 178.0f}, {62.5f, 179.0f}, {61.0f, 174.0f}, {60.3f, 166.0f}, {58.0f, 162.5f}, {56.0f, 163.3f}, {53.0f, 160.0f},
	{51.0f, 156.7f}, {52.5f, 156.0f}, {57.5f, 156.5f}, {59.5f, 160.0f}, {61.8f, 160.5f}, {59.4f, 154.0f}, {59.6f, 150.0f}, {59.4f, 143.0f},
	{56.5f, 138.0f}, {54.0f, 137.0f}, {53.5f, 141.4f}, {51.0f, 140.5f}, {48.0f, 140.0f}, {45.5f, 137.5f}, {43.2f, 132.0f}, {42.4f, 130.7f},
	{45.0f, 131.8f}, {47.6f, 134.7f}, {48.4f, 135.0f}, {47.7f, 132.5f}, {49.0f, 128.5f}, {53.4f, 125.0f}, {53.3f, 120.0f}, {49.9f, 117.8f},
	{49.5f, 116.0f}, {50.0f, 114.0f}, {49.2f, 109.0f}, {50.3f, 106.0f}, {50.3f, 102.0f}, {51.5f, 98.0f}, {50.0f, 95.0f}, {50.5f, 90.0f},
	{49.5f, 87.5f}, {50.8f, 83.0f}, {50.7f, 80.0f}, {53.6f, 77.8f}, {54.8f, 73.0f}, {53.8f, 69.0f}, {55.3f, 65.0f}, {54.3f, 61.0f}, {51.0f, 61.0f},
	{50.7f, 57.0f}, {51.0f, 55.0f}, {51.5f, 50.5f}, {49.5f, 48.0f}, {48.0f, 47.0f}, {46.5f, 49.0f}, {44.8f, 47.0f}, {43.0f, 47.5f}, {41.2f, 47.8f},
	{42.6f, 44.5f}, {43.6f, 40.0f}, {44.6f, 37.6f}, {45.3f, 36.8f}, {46.6f, 38.2f}, {47.3f, 39.4f}, {47.9f, 39.9f}, {50.0f, 39.5f}, {51.2f, 35.5f},
	{52.3f, 34.0f}, {52.0f, 31.8f}, {53.9f, 31.5f}, {55.8f, 30.9f}, {56.8f, 28.0f}, {57.8f, 27.6f}, {59.4f, 28.1f}, {60.6f, 28.6f}, {61.7f, 30.2f},
	{64.0f, 29.6f}, {66.0f, 29.5f}, {68.5f, 28.6f},
};

struct FRegion
{
	const TCHAR* Name;
	const TCHAR* City;  // крупнейший город — там старт
	FLatLon Capital;
	bool bPlayable;
	bool bLabel;        // подписывать город на карте
};
// Регион и его «семена» (столица + точки для больших территорий) — границы на карте — по ближайшему семени.
const FRegion Regions[] = {
	{TEXT("Мурманская область"), TEXT("Мурманск"), {68.97f, 33.08f}, false, true},
	{TEXT("Карелия"), TEXT("Петрозаводск"), {61.79f, 34.36f}, false, false},
	{TEXT("Архангельская область"), TEXT("Архангельск"), {64.54f, 40.54f}, false, true},
	{TEXT("Ненецкий АО"), TEXT("Нарьян-Мар"), {67.64f, 53.0f}, false, false},
	{TEXT("Республика Коми"), TEXT("Сыктывкар"), {61.67f, 50.81f}, false, false},
	{TEXT("Ленинградская область"), TEXT("Санкт-Петербург"), {59.94f, 30.31f}, false, true},
	{TEXT("Псковская область"), TEXT("Псков"), {57.82f, 28.33f}, false, false},
	{TEXT("Новгородская область"), TEXT("Великий Новгород"), {58.52f, 31.27f}, false, false},
	{TEXT("Вологодская область"), TEXT("Череповец"), {59.6f, 40.5f}, false, false},
	{TEXT("Тверская область"), TEXT("Тверь"), {56.86f, 35.9f}, false, false},
	{TEXT("Смоленская область"), TEXT("Смоленск"), {54.78f, 32.04f}, false, false},
	{TEXT("Москва и область"), TEXT("Москва"), {55.75f, 37.62f}, false, true},
	{TEXT("Ярославская область"), TEXT("Ярославль"), {57.63f, 39.87f}, false, false},
	{TEXT("Костромская область"), TEXT("Кострома"), {58.2f, 43.5f}, false, false},
	{TEXT("Нижегородская область"), TEXT("Нижний Новгород"), {56.33f, 44.0f}, false, true},
	{TEXT("Брянская область"), TEXT("Брянск"), {53.24f, 34.36f}, false, false},
	{TEXT("Тульская область"), TEXT("Тула"), {54.19f, 37.62f}, false, false},
	{TEXT("Рязанская область"), TEXT("Рязань"), {54.63f, 40.5f}, false, false},
	{TEXT("Курская область"), TEXT("Курск"), {51.73f, 36.19f}, false, false},
	{TEXT("Воронежская область"), TEXT("Воронеж"), {51.0f, 40.0f}, false, true},
	{TEXT("Тамбовская область"), TEXT("Тамбов"), {52.72f, 41.45f}, false, false},
	{TEXT("Пензенская область"), TEXT("Пенза"), {53.2f, 45.0f}, false, false},
	{TEXT("Саратовская область"), TEXT("Саратов"), {51.53f, 46.03f}, false, false},
	{TEXT("Волгоградская область"), TEXT("Волгоград"), {48.71f, 44.51f}, false, true},
	{TEXT("Ростовская область"), TEXT("Ростов-на-Дону"), {47.5f, 40.5f}, false, true},
	{TEXT("Краснодарский край"), TEXT("Краснодар"), {45.04f, 38.98f}, false, true},
	{TEXT("Ставропольский край"), TEXT("Ставрополь"), {44.8f, 42.6f}, false, false},
	{TEXT("Дагестан"), TEXT("Махачкала"), {42.98f, 47.0f}, false, false},
	{TEXT("Калмыкия"), TEXT("Элиста"), {46.31f, 45.5f}, false, false},
	{TEXT("Астраханская область"), TEXT("Астрахань"), {46.8f, 47.6f}, false, false},
	{TEXT("Самарская область"), TEXT("Самара"), {53.2f, 50.15f}, false, true},
	{TEXT("Ульяновская область"), TEXT("Ульяновск"), {54.0f, 47.8f}, false, false},
	{TEXT("Татарстан"), TEXT("Казань"), {55.79f, 50.0f}, false, true},
	{TEXT("Чувашия и Марий Эл"), TEXT("Чебоксары"), {56.3f, 47.5f}, false, false},
	{TEXT("Кировская область"), TEXT("Киров"), {58.6f, 49.66f}, false, false},
	{TEXT("Удмуртия"), TEXT("Ижевск"), {57.2f, 53.0f}, false, false},
	{TEXT("Пермский край"), TEXT("Пермь"), {59.0f, 56.25f}, false, true},
	{TEXT("Башкортостан"), TEXT("Уфа"), {54.5f, 56.5f}, false, true},
	{TEXT("Оренбургская область"), TEXT("Оренбург"), {51.77f, 55.1f}, false, false},
	{TEXT("Свердловская область"), TEXT("Екатеринбург"), {57.5f, 61.0f}, false, true},
	{TEXT("Челябинская область"), TEXT("Челябинск"), {54.5f, 61.0f}, false, true},
	{TEXT("Курганская область"), TEXT("Курган"), {55.44f, 65.34f}, false, false},
	{TEXT("Тюменская область"), TEXT("Тюмень"), {57.5f, 68.5f}, false, true},
	{TEXT("Ханты-Мансийский АО"), TEXT("Сургут"), {61.5f, 72.0f}, false, false},
	{TEXT("Ямало-Ненецкий АО"), TEXT("Новый Уренгой"), {66.5f, 74.0f}, false, false},
	{TEXT("Омская область"), TEXT("Омск"), {56.0f, 73.37f}, false, true},
	{TEXT("Новосибирская область"), TEXT("Новосибирск"), {55.03f, 80.5f}, true, true},
	{TEXT("Томская область"), TEXT("Томск"), {58.5f, 82.0f}, false, false},
	{TEXT("Кемеровская область — Кузбасс"), TEXT("Кемерово"), {55.0f, 87.0f}, false, true},
	{TEXT("Алтайский край"), TEXT("Барнаул"), {52.8f, 82.0f}, false, true},
	{TEXT("Республика Алтай"), TEXT("Горно-Алтайск"), {50.8f, 87.0f}, false, false},
	{TEXT("Красноярский край"), TEXT("Красноярск"), {58.0f, 93.0f}, false, true},
	{TEXT("Хакасия"), TEXT("Абакан"), {53.5f, 90.2f}, false, false},
	{TEXT("Тыва"), TEXT("Кызыл"), {51.2f, 94.5f}, false, false},
	{TEXT("Иркутская область"), TEXT("Иркутск"), {55.0f, 104.0f}, false, true},
	{TEXT("Бурятия"), TEXT("Улан-Удэ"), {52.5f, 109.0f}, false, true},
	{TEXT("Забайкальский край"), TEXT("Чита"), {52.0f, 116.0f}, false, true},
	{TEXT("Якутия"), TEXT("Якутск"), {63.0f, 128.0f}, false, true},
	{TEXT("Амурская область"), TEXT("Благовещенск"), {53.0f, 127.0f}, false, true},
	{TEXT("Еврейская АО"), TEXT("Биробиджан"), {48.6f, 132.5f}, false, false},
	{TEXT("Хабаровский край"), TEXT("Хабаровск"), {53.0f, 136.0f}, false, true},
	{TEXT("Приморский край"), TEXT("Владивосток"), {44.5f, 133.5f}, false, true},
	{TEXT("Магаданская область"), TEXT("Магадан"), {62.0f, 152.0f}, false, true},
	{TEXT("Камчатский край"), TEXT("Петропавловск-Камчатский"), {56.5f, 159.5f}, false, false},
	{TEXT("Чукотский АО"), TEXT("Анадырь"), {66.5f, 172.0f}, false, false},
};
constexpr int32 RegionNum = UE_ARRAY_COUNT(Regions);

// Дополнительные «семена» больших территорий: регион и точка.
struct FSeed
{
	int32 Region;
	FLatLon At;
};
int32 FindRegion(const TCHAR* Name)
{
	for (int32 I = 0; I < RegionNum; ++I)
	{
		if (FCString::Strcmp(Regions[I].Name, Name) == 0)
		{
			return I;
		}
	}
	return 0;
}
const TArray<FSeed>& Seeds()
{
	static TArray<FSeed> List = []() {
		TArray<FSeed> Out;
		for (int32 I = 0; I < RegionNum; ++I)
		{
			Out.Add({I, Regions[I].Capital});
		}
		auto Extra = [&Out](const TCHAR* Name, float Lat, float Lon) { Out.Add({FindRegion(Name), {Lat, Lon}}); };
		Extra(TEXT("Красноярский край"), 64.f, 92.f);
		Extra(TEXT("Красноярский край"), 70.5f, 97.f);
		Extra(TEXT("Красноярский край"), 75.f, 100.f);
		Extra(TEXT("Якутия"), 68.f, 125.f);
		Extra(TEXT("Якутия"), 66.f, 140.f);
		Extra(TEXT("Якутия"), 70.5f, 135.f);
		Extra(TEXT("Якутия"), 60.f, 120.f);
		Extra(TEXT("Якутия"), 70.f, 115.f);
		Extra(TEXT("Иркутская область"), 58.f, 110.f);
		Extra(TEXT("Хабаровский край"), 57.5f, 137.f);
		Extra(TEXT("Хабаровский край"), 50.5f, 137.5f);
		Extra(TEXT("Магаданская область"), 64.5f, 156.f);
		Extra(TEXT("Камчатский край"), 60.5f, 164.f);
		Extra(TEXT("Чукотский АО"), 68.5f, 165.f);
		Extra(TEXT("Чукотский АО"), 64.5f, 176.f);
		Extra(TEXT("Ямало-Ненецкий АО"), 70.f, 70.f);
		Extra(TEXT("Ханты-Мансийский АО"), 61.f, 64.f);
		Extra(TEXT("Тюменская область"), 59.f, 70.f);
		Extra(TEXT("Республика Коми"), 65.f, 57.f);
		Extra(TEXT("Архангельская область"), 63.f, 45.f);
		Extra(TEXT("Томская область"), 59.5f, 79.f);
		Extra(TEXT("Забайкальский край"), 55.5f, 117.f);
		Extra(TEXT("Амурская область"), 55.f, 125.f);
		Extra(TEXT("Пермский край"), 60.5f, 57.f);
		return Out;
	}();
	return List;
}

// Трассы: номер и города по порядку (широта, долгота). Р-255 — наша дорога.
struct FRoad
{
	const TCHAR* Number;
	TArray<FLatLon> Points;
	bool bOurs;
};
const TArray<FRoad>& Roads()
{
	static TArray<FRoad> List = {
		{TEXT("М-11"), {{59.94f, 30.31f}, {58.52f, 31.27f}, {56.86f, 35.9f}, {55.75f, 37.62f}}, false},
		{TEXT("Р-21"), {{59.94f, 30.31f}, {61.79f, 34.36f}, {64.5f, 34.6f}, {68.97f, 33.08f}}, false},
		{TEXT("М-8"), {{55.75f, 37.62f}, {57.63f, 39.87f}, {59.22f, 39.89f}, {61.6f, 40.4f}, {64.54f, 40.54f}}, false},
		{TEXT("М-1"), {{55.75f, 37.62f}, {54.78f, 32.04f}}, false},
		{TEXT("М-4"), {{55.75f, 37.62f}, {54.19f, 37.62f}, {51.67f, 39.18f}, {47.22f, 39.72f}, {45.04f, 38.98f}, {44.6f, 37.8f}}, false},
		{TEXT("М-12"), {{55.75f, 37.62f}, {56.13f, 40.4f}, {56.33f, 44.0f}, {56.13f, 47.25f}, {55.79f, 49.12f}, {55.6f, 51.4f}, {54.73f, 55.96f}}, false},
		{TEXT("М-5"), {{55.75f, 37.62f}, {54.63f, 39.74f}, {53.2f, 45.0f}, {53.2f, 50.15f}, {54.73f, 55.96f}, {55.16f, 61.4f}}, false},
		{TEXT("Р-22"), {{55.75f, 37.62f}, {52.72f, 41.45f}, {48.71f, 44.51f}, {46.35f, 48.04f}}, false},
		{TEXT("Р-242"), {{58.01f, 56.25f}, {56.84f, 60.6f}}, false},
		{TEXT("Р-351"), {{56.84f, 60.6f}, {57.15f, 65.53f}}, false},
		{TEXT("Р-254"), {{55.16f, 61.4f}, {55.44f, 65.34f}, {54.99f, 73.37f}, {55.03f, 82.92f}}, false},
		{TEXT("Р-404"), {{57.15f, 65.53f}, {58.2f, 68.25f}, {61.0f, 69.0f}, {61.25f, 73.4f}}, false},
		{TEXT("Р-256"), {{55.03f, 82.92f}, {53.35f, 83.78f}, {51.96f, 85.96f}, {50.0f, 88.6f}}, false},
		{TEXT("Р-255 «Сибирь»"), {{55.03f, 82.92f}, {55.35f, 86.09f}}, true},
		{TEXT("Р-255"), {{55.35f, 86.09f}, {56.01f, 92.85f}, {55.9f, 98.0f}, {54.6f, 100.6f}, {52.29f, 104.3f}}, false},
		{TEXT("Р-258"), {{52.29f, 104.3f}, {51.83f, 107.58f}, {52.03f, 113.5f}}, false},
		{TEXT("Р-297 «Амур»"), {{52.03f, 113.5f}, {53.8f, 121.5f}, {53.9f, 126.0f}, {51.4f, 128.1f}, {49.5f, 130.5f}, {48.79f, 132.92f}, {48.48f, 135.08f}}, false},
		{TEXT("А-370 «Уссури»"), {{48.48f, 135.08f}, {46.5f, 134.0f}, {44.8f, 132.0f}, {43.12f, 131.89f}}, false},
		{TEXT("А-360 «Лена»"), {{53.9f, 124.0f}, {56.7f, 124.7f}, {58.6f, 125.4f}, {62.03f, 129.73f}}, false},
		{TEXT("Р-504 «Колыма»"), {{62.03f, 129.73f}, {63.0f, 136.0f}, {62.5f, 145.0f}, {61.5f, 149.0f}, {59.56f, 150.8f}}, false},
		{TEXT("Р-257"), {{56.01f, 92.85f}, {53.72f, 91.44f}, {51.72f, 94.45f}}, false},
	};
	return List;
}

// Коническая проекция «полюс сверху»: Россия ложится дугой, как на школьной карте.
FVector2f Project(const FLatLon& P)
{
	const float N = 0.62f;
	const float Theta = FMath::DegreesToRadians(N * (P.Lon - 100.f));
	const float Rho = 90.f - P.Lat;
	return FVector2f(Rho * FMath::Sin(Theta), Rho * FMath::Cos(Theta));
}

// Нормированные координаты карты 0..1 (рамка — контур с полями).
struct FFrame
{
	FVector2f Min;
	FVector2f Size;
};
const FFrame& Frame()
{
	static FFrame F = []() {
		FVector2f Lo(1e9f), Hi(-1e9f);
		for (const FLatLon& P : Outline)
		{
			const FVector2f V = Project(P);
			Lo = FVector2f(FMath::Min(Lo.X, V.X), FMath::Min(Lo.Y, V.Y));
			Hi = FVector2f(FMath::Max(Hi.X, V.X), FMath::Max(Hi.Y, V.Y));
		}
		const FVector2f Pad = (Hi - Lo) * 0.04f;
		return FFrame{Lo - Pad, Hi - Lo + Pad * 2.f};
	}();
	return F;
}
FVector2f ToUV(const FLatLon& P)
{
	const FFrame& F = Frame();
	return (Project(P) - F.Min) / F.Size;
}

bool InsideOutline(const FVector2f& UV)
{
	static TArray<FVector2f> Poly = []() {
		TArray<FVector2f> Out;
		for (const FLatLon& P : Outline)
		{
			Out.Add(ToUV(P));
		}
		return Out;
	}();
	bool bIn = false;
	for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
	{
		if ((Poly[I].Y > UV.Y) != (Poly[J].Y > UV.Y)
			&& UV.X < (Poly[J].X - Poly[I].X) * (UV.Y - Poly[I].Y) / (Poly[J].Y - Poly[I].Y) + Poly[I].X)
		{
			bIn = !bIn;
		}
	}
	return bIn;
}

const FLinearColor Sea(0.035f, 0.06f, 0.085f);
const FLinearColor Ink(0.93f, 0.92f, 0.88f);
const FLinearColor Accent(0.95f, 0.62f, 0.25f);
} // namespace PeregonRussia

int32 SPeregonRussiaMap::RegionCount() { return PeregonRussia::RegionNum; }
FString SPeregonRussiaMap::RegionName(int32 Region) { return Region >= 0 && Region < PeregonRussia::RegionNum ? PeregonRussia::Regions[Region].Name : TEXT(""); }
FString SPeregonRussiaMap::RegionCity(int32 Region) { return Region >= 0 && Region < PeregonRussia::RegionNum ? PeregonRussia::Regions[Region].City : TEXT(""); }
bool SPeregonRussiaMap::IsPlayable(int32 Region) { return Region >= 0 && Region < PeregonRussia::RegionNum && PeregonRussia::Regions[Region].bPlayable; }

void SPeregonRussiaMap::Construct(const FArguments& InArgs)
{
	OnChosen = InArgs._OnChosen;
	OnBack = InArgs._OnBack;
	OnOptions = InArgs._OnOptions;
	BuildTexture();
}

void SPeregonRussiaMap::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Texture);
}

void SPeregonRussiaMap::BuildTexture()
{
	using namespace PeregonRussia;
	// Регион каждого пикселя считается один раз: внутри контура — ближайшее «семя».
	PixelRegion.SetNumUninitialized(TexW * TexH);
	TArray<FVector2f> SeedUV;
	for (const FSeed& S : Seeds())
	{
		SeedUV.Add(ToUV(S.At));
	}
	const float Aspect = static_cast<float>(TexW) / TexH;
	for (int32 Y = 0; Y < TexH; ++Y)
	{
		for (int32 X = 0; X < TexW; ++X)
		{
			const FVector2f UV((X + 0.5f) / TexW, (Y + 0.5f) / TexH);
			uint8 Id = 255;
			if (InsideOutline(UV))
			{
				float Best = 1e9f;
				for (int32 K = 0; K < SeedUV.Num(); ++K)
				{
					const FVector2f D = (UV - SeedUV[K]) * FVector2f(Aspect, 1.f);
					// Немного «шума» на границах, чтобы они не были идеально прямыми.
					const float Wobble = 1.f + 0.06f * FMath::Sin(UV.X * 90.f + K) * FMath::Cos(UV.Y * 70.f - K * 0.7f);
					const float Dist = D.SizeSquared() * Wobble;
					if (Dist < Best)
					{
						Best = Dist;
						Id = static_cast<uint8>(Seeds()[K].Region);
					}
				}
			}
			PixelRegion[Y * TexW + X] = Id;
		}
	}
	Texture = UTexture2D::CreateTransient(TexW, TexH, PF_B8G8R8A8);
	if (Texture)
	{
		Texture->Filter = TF_Bilinear;
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_EditorIcon;
	}
	PaintTexture();
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = FVector2D(TexW, TexH);
	Brush.DrawAs = ESlateBrushDrawType::Image;
}

void SPeregonRussiaMap::PaintTexture()
{
	using namespace PeregonRussia;
	if (!Texture)
	{
		return;
	}
	PaintedHover = Hovered;
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(TexW * TexH);
	for (int32 Y = 0; Y < TexH; ++Y)
	{
		for (int32 X = 0; X < TexW; ++X)
		{
			const uint8 Id = PixelRegion[Y * TexW + X];
			FLinearColor C = Sea;
			if (Id != 255)
			{
				const bool bPlay = Regions[Id].bPlayable;
				C = bPlay ? FLinearColor(0.9f, 0.89f, 0.85f) : FLinearColor(0.32f, 0.33f, 0.34f);
				if (Id == Hovered)
				{
					C = bPlay ? FLinearColor(1.f, 0.93f, 0.8f) : FLinearColor(0.42f, 0.43f, 0.44f);
				}
				// Граница региона — тёмная линия; побережье — светлая.
				const uint8 R = X + 1 < TexW ? PixelRegion[Y * TexW + X + 1] : Id;
				const uint8 D = Y + 1 < TexH ? PixelRegion[(Y + 1) * TexW + X] : Id;
				if (R == 255 || D == 255)
				{
					C = FLinearColor(0.75f, 0.78f, 0.8f);
				}
				else if (R != Id || D != Id)
				{
					C *= bPlay ? 0.55f : 0.6f;
				}
				// Лёгкая бумажная фактура.
				const float Grain = 0.96f + 0.04f * FMath::Frac(FMath::Sin(X * 12.9898f + Y * 78.233f) * 43758.5453f);
				C *= Grain;
			}
			else
			{
				// Море: тонкие «изолинии» вдоль берега.
				const float Wave = 0.5f + 0.5f * FMath::Sin((X + Y * 0.3f) * 0.05f);
				C = FMath::Lerp(Sea, Sea * 1.25f, Wave * 0.3f);
			}
			Pixels[Y * TexW + X] = C.ToFColor(true);
		}
	}
	FTexturePlatformData* Platform = Texture->GetPlatformData();
	if (!Platform || Platform->Mips.Num() == 0)
	{
		return;
	}
	FTexture2DMipMap& Mip = Platform->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
}

FVector2f SPeregonRussiaMap::MapToScreen(const FVector2f& MapUV, const FVector2f& WidgetSize) const
{
	using namespace PeregonRussia;
	// Карта вписана по ширине/высоте; при выборе — плавно приближается к городу.
	const float Fit = FMath::Min(WidgetSize.X * 0.86f / TexW, WidgetSize.Y * 0.8f / TexH);
	const FVector2f Base = FVector2f(TexW, TexH) * Fit;
	const FVector2f Origin = (WidgetSize - Base) * 0.5f + FVector2f(0.f, WidgetSize.Y * 0.03f);
	FVector2f P = Origin + MapUV * Base;
	if (Chosen >= 0)
	{
		const float T = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(Anim / 1.6f, 0.f, 1.f));
		const float Zoom = FMath::Lerp(1.f, 6.f, T);
		const FVector2f Focus = Origin + ToUV(Regions[Chosen].Capital) * Base;
		const FVector2f Center = WidgetSize * 0.5f;
		P = FMath::Lerp(Focus, Center, T) + (P - Focus) * Zoom;
	}
	return P;
}

FVector2f SPeregonRussiaMap::ScreenToMap(const FVector2f& Screen, const FVector2f& WidgetSize) const
{
	const float Fit = FMath::Min(WidgetSize.X * 0.86f / TexW, WidgetSize.Y * 0.8f / TexH);
	const FVector2f Base = FVector2f(TexW, TexH) * Fit;
	const FVector2f Origin = (WidgetSize - Base) * 0.5f + FVector2f(0.f, WidgetSize.Y * 0.03f);
	return (Screen - Origin) / Base;
}

int32 SPeregonRussiaMap::RegionAt(const FVector2f& MapUV) const
{
	const int32 X = FMath::FloorToInt(MapUV.X * TexW);
	const int32 Y = FMath::FloorToInt(MapUV.Y * TexH);
	if (X < 0 || Y < 0 || X >= TexW || Y >= TexH)
	{
		return -1;
	}
	const uint8 Id = PixelRegion[Y * TexW + X];
	return Id == 255 ? -1 : Id;
}

void SPeregonRussiaMap::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Hovered != PaintedHover && Chosen < 0)
	{
		PaintTexture();
	}
	if (Chosen >= 0)
	{
		Anim += InDeltaTime;
		// 1.6 с приближение, 0.6 с в чёрное, кадр «Загрузка» — и старт.
		if (!bFired && Anim > 2.45f)
		{
			bFired = true;
			OnChosen.ExecuteIfBound(Chosen);
		}
	}
}

FReply SPeregonRussiaMap::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Chosen < 0)
	{
		const FVector2f Local = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		Hovered = RegionAt(ScreenToMap(Local, FVector2f(MyGeometry.GetLocalSize())));
	}
	return FReply::Handled();
}

FReply SPeregonRussiaMap::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Chosen >= 0 || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}
	const FVector2f Local = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	const FVector2f Size = FVector2f(MyGeometry.GetLocalSize());
	// Кнопки внизу: «Назад» и «Параметры заезда».
	if (Local.Y > Size.Y - 70.f)
	{
		if (Local.X < 260.f)
		{
			OnBack.ExecuteIfBound();
		}
		else if (Local.X > Size.X - 340.f)
		{
			OnOptions.ExecuteIfBound();
		}
		return FReply::Handled();
	}
	const int32 Region = RegionAt(ScreenToMap(Local, Size));
	if (IsPlayable(Region))
	{
		Chosen = Region;
		Anim = 0.f;
	}
	return FReply::Handled();
}

FReply SPeregonRussiaMap::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Chosen < 0 && (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F10))
	{
		OnBack.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Chosen < 0 && InKeyEvent.GetKey() == EKeys::Enter)
	{
		for (int32 I = 0; I < PeregonRussia::RegionNum; ++I)
		{
			if (IsPlayable(I))
			{
				Chosen = I;
				Anim = 0.f;
				break;
			}
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

int32 SPeregonRussiaMap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace PeregonRussia;
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	auto Rect = [&](const FVector2f& Pos, const FVector2f& Extent, const FLinearColor& Color, int32 Layer, const FSlateBrush* B = nullptr) {
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Pos)), B ? B : WhiteBrush,
			ESlateDrawEffect::None, Color);
	};
	auto Text = [&](const FString& Str, const FVector2f& Pos, int32 FontSize, const FLinearColor& Color, int32 Layer, bool bBold = false, bool bCenter = false) {
		const FSlateFontInfo Font = UPeregonArtSubsystem::SlateFont(bBold ? EPeregonFont::SemiBold : EPeregonFont::Regular, FontSize);
		FVector2f At = Pos;
		if (bCenter)
		{
			const FVector2D Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Str, Font);
			At -= FVector2f(static_cast<float>(Measured.X) * 0.5f, static_cast<float>(Measured.Y) * 0.5f);
		}
		FSlateDrawElement::MakeText(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, 1.f), FSlateLayoutTransform(At)), Str, Font,
			ESlateDrawEffect::None, Color);
	};

	// Фон — тёмное «море» во весь экран.
	Rect(FVector2f::ZeroVector, Size, Sea, LayerId);
	// Карта.
	const FVector2f TopLeft = MapToScreen(FVector2f(0.f, 0.f), Size);
	const FVector2f BottomRight = MapToScreen(FVector2f(1.f, 1.f), Size);
	Rect(TopLeft, BottomRight - TopLeft, FLinearColor::White, LayerId + 1, &Brush);

	// Трассы.
	for (const FRoad& Road : Roads())
	{
		TArray<FVector2f> Points;
		for (const FLatLon& P : Road.Points)
		{
			Points.Add(MapToScreen(ToUV(P), Size));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
			Road.bOurs ? Accent : FLinearColor(0.85f, 0.7f, 0.35f, 0.75f), true, Road.bOurs ? 4.f : 1.8f);
	}
	// Города.
	for (int32 I = 0; I < RegionNum; ++I)
	{
		const FRegion& R = Regions[I];
		if (!R.bLabel && I != Hovered)
		{
			continue;
		}
		const FVector2f P = MapToScreen(ToUV(R.Capital), Size);
		const float Dot = R.bPlayable ? 9.f : 5.f;
		Rect(P - FVector2f(Dot * 0.5f), FVector2f(Dot), R.bPlayable ? Accent : FLinearColor(0.1f, 0.1f, 0.11f), LayerId + 3);
		Text(R.City, P + FVector2f(7.f, -9.f), R.bPlayable ? 13 : 10, R.bPlayable ? FLinearColor(0.08f, 0.07f, 0.06f) : FLinearColor(0.85f, 0.85f, 0.83f, 0.85f),
			LayerId + 3, R.bPlayable);
	}

	// Заголовок, подсказка, легенда, кнопки.
	if (Chosen < 0)
	{
		Text(TEXT("ВЫБЕРИТЕ, ОТКУДА НАЧАТЬ"), FVector2f(48.f, 36.f), 26, Ink, LayerId + 4, true);
		Text(TEXT("Белые регионы — можно начать: старт в крупнейшем городе региона. Серые — в разработке."), FVector2f(48.f, 76.f), 13,
			FLinearColor(Ink.R, Ink.G, Ink.B, 0.6f), LayerId + 4);
		Text(TEXT("Оранжевая линия — Р-255 «Сибирь», доступный участок Новосибирск — Кемерово. В кооперативе все появятся там же, где хост."),
			FVector2f(48.f, 98.f), 13, FLinearColor(Ink.R, Ink.G, Ink.B, 0.6f), LayerId + 4);
		if (Hovered >= 0)
		{
			const FRegion& R = Regions[Hovered];
			const FString Line1 = R.Name;
			const FString Line2 = R.bPlayable ? FString::Printf(TEXT("Старт: %s · нажмите, чтобы начать"), R.City)
											 : FString::Printf(TEXT("%s · регион в разработке"), R.City);
			Rect(FVector2f(Size.X - 520.f, 36.f), FVector2f(472.f, 74.f), FLinearColor(0.f, 0.f, 0.f, 0.55f), LayerId + 4);
			Text(Line1, FVector2f(Size.X - 500.f, 46.f), 18, Ink, LayerId + 5, true);
			Text(Line2, FVector2f(Size.X - 500.f, 76.f), 13, R.bPlayable ? Accent : FLinearColor(0.7f, 0.7f, 0.7f), LayerId + 5);
		}
		Rect(FVector2f(40.f, Size.Y - 62.f), FVector2f(200.f, 40.f), FLinearColor(0.f, 0.f, 0.f, 0.5f), LayerId + 4);
		Text(TEXT("← Назад (Esc)"), FVector2f(140.f, Size.Y - 42.f), 14, Ink, LayerId + 5, false, true);
		Rect(FVector2f(Size.X - 320.f, Size.Y - 62.f), FVector2f(280.f, 40.f), FLinearColor(0.f, 0.f, 0.f, 0.5f), LayerId + 4);
		Text(TEXT("Параметры заезда…"), FVector2f(Size.X - 180.f, Size.Y - 42.f), 14, Ink, LayerId + 5, false, true);
	}
	else
	{
		// Приближение: город и надпись; затем — в чёрное, «Загрузка».
		const FRegion& R = Regions[Chosen];
		const float Show = FMath::Clamp((Anim - 0.6f) / 0.6f, 0.f, 1.f);
		Text(R.City, Size * 0.5f + FVector2f(0.f, -60.f), 30, FLinearColor(0.08f, 0.07f, 0.06f, Show), LayerId + 4, true, true);
		const float Black = FMath::Clamp((Anim - 1.6f) / 0.6f, 0.f, 1.f);
		Rect(FVector2f::ZeroVector, Size, FLinearColor(0.f, 0.f, 0.f, Black), LayerId + 6);
		if (Black > 0.95f)
		{
			const int32 Dots = 1 + static_cast<int32>(Anim * 3.f) % 3;
			Text(FString::Printf(TEXT("%s. Утро. Загрузка%s"), R.City, *FString::ChrN(Dots, TEXT('.'))), Size * 0.5f, 18,
				FLinearColor(Ink.R, Ink.G, Ink.B, 0.8f), LayerId + 7, false, true);
		}
	}
	return LayerId + 8;
}
