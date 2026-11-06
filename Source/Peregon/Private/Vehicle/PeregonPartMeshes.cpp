#include "Vehicle/PeregonPartMeshes.h"

#include "Game/PeregonInventory.h"
#include "World/PeregonKit.h"
#include "vsim/CarParts.h"

namespace PeregonPartMeshDetail
{
// Палитра.
const FLinearColor Black(0.04f, 0.04f, 0.045f);
const FLinearColor Rubber(0.06f, 0.06f, 0.06f);
const FLinearColor Steel(0.55f, 0.56f, 0.58f);
const FLinearColor DarkSteel(0.28f, 0.29f, 0.3f);
const FLinearColor Alu(0.68f, 0.67f, 0.62f);
const FLinearColor Brass(0.62f, 0.45f, 0.2f);
const FLinearColor Copper(0.6f, 0.32f, 0.16f);
const FLinearColor Ceramic(0.9f, 0.9f, 0.86f);
const FLinearColor RedPlus(0.65f, 0.08f, 0.06f);
const FLinearColor Rust(0.32f, 0.16f, 0.07f);
const FLinearColor Chrome(0.85f, 0.86f, 0.88f);
const FLinearColor Glass(0.75f, 0.82f, 0.86f);

// Построитель: всё в локальных координатах детали (см, X — вперёд, Z — вверх, основание в z = 0).
struct FShape
{
	FPeregonMeshBuffers& Out;
	float Wear = 0.f; // 0 — новая, 1 — убитая

	FColor Paint(const FLinearColor& C, bool bMetal = true) const
	{
		FLinearColor R = bMetal ? FMath::Lerp(C, Rust, Wear * 0.5f) : C;
		R *= 1.f - 0.3f * Wear;
		return R.ToFColor(false);
	}

	void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FColor& Col)
	{
		const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		Out.AddQuad(A, B, C, D, N.IsNearlyZero() ? FVector::UpVector : N, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), Col, 0);
	}

	// Коробка по центру и размеру.
	void Box(const FVector& Center, const FVector& Size, const FLinearColor& C, bool bMetal = true, const FRotator& Rot = FRotator::ZeroRotator)
	{
		const FVector H = Size * 0.5;
		auto P = [&](float X, float Y, float Z) { return Center + Rot.RotateVector(FVector(X * H.X, Y * H.Y, Z * H.Z)); };
		const FColor Col = Paint(C, bMetal);
		Quad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Col);
		Quad(P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), Col);
		Quad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), Col);
		Quad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), Col);
		Quad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), Col);
		Quad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), Col);
	}

	static void Frame(const FVector& Axis, FVector& U, FVector& V)
	{
		U = FVector::CrossProduct(Axis, FMath::Abs(Axis.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		V = FVector::CrossProduct(Axis, U).GetSafeNormal();
	}

	// Цилиндр: центр основания, ось, радиус (Ry — для овала), длина.
	void Cyl(const FVector& Base, const FVector& Axis, float R, float Length, const FLinearColor& C, int32 Seg = 12, bool bCaps = true, float Ry = -1.f,
		bool bMetal = true)
	{
		FVector U, V;
		Frame(Axis.GetSafeNormal(), U, V);
		const FVector Top = Base + Axis.GetSafeNormal() * Length;
		const float R2 = Ry > 0.f ? Ry : R;
		const FColor Col = Paint(C, bMetal);
		for (int32 K = 0; K < Seg; ++K)
		{
			const float A0 = 2.f * PI * K / Seg;
			const float A1 = 2.f * PI * (K + 1) / Seg;
			const FVector D0 = U * (FMath::Cos(A0) * R) + V * (FMath::Sin(A0) * R2);
			const FVector D1 = U * (FMath::Cos(A1) * R) + V * (FMath::Sin(A1) * R2);
			Quad(Base + D0, Base + D1, Top + D1, Top + D0, Col);
			if (bCaps)
			{
				Quad(Top, Top + D0, Top + D1, Top + D1, Col);
				Quad(Base, Base + D1, Base + D0, Base + D0, Col);
			}
		}
	}

	// Трубка по ломаной (шланги, провода, выхлоп).
	void Tube(const TArray<FVector>& Points, float R, const FLinearColor& C, int32 Seg = 8, bool bMetal = false)
	{
		for (int32 I = 0; I + 1 < Points.Num(); ++I)
		{
			const FVector D = Points[I + 1] - Points[I];
			Cyl(Points[I], D, R, static_cast<float>(D.Size()), C, Seg, false, -1.f, bMetal);
			// Шарнир на изломе, чтобы не было щелей.
			Cyl(Points[I + 1] - D.GetSafeNormal() * R * 0.5f, D, R * 1.02f, R, C, Seg, false, -1.f, bMetal);
		}
	}

	// Рёбра охлаждения / резьба: кольца вдоль оси.
	void Ribs(const FVector& Base, const FVector& Axis, float R, float Length, int32 Count, const FLinearColor& C)
	{
		const FVector A = Axis.GetSafeNormal();
		for (int32 K = 0; K < Count; ++K)
		{
			const float T = (K + 0.5f) / Count * Length;
			Cyl(Base + A * (T - 0.4f), A, R, 0.8f, C, 12, true);
		}
	}
};

void Battery(FShape& S)
{
	S.Box(FVector(0, 0, 9.5f), FVector(27, 17, 19), Black, false);
	S.Box(FVector(0, 0, 19.5f), FVector(27.5f, 17.5f, 1.2f), FLinearColor(0.1f, 0.1f, 0.11f), false);
	// Пробки шести банок.
	for (int32 K = 0; K < 6; ++K)
	{
		S.Cyl(FVector(-10.f + K * 4.f, 3.5f, 20.f), FVector::UpVector, 1.1f, 0.9f, FLinearColor(0.15f, 0.15f, 0.16f), 8, true, -1.f, false);
	}
	// Клеммы: «+» красная, «−» чёрная, с наконечниками проводов.
	S.Cyl(FVector(-10.f, -5.f, 20.f), FVector::UpVector, 1.3f, 2.2f, Brass, 10);
	S.Cyl(FVector(10.f, -5.f, 20.f), FVector::UpVector, 1.2f, 2.2f, Brass, 10);
	S.Box(FVector(-10.f, -5.f, 21.8f), FVector(4.f, 3.f, 1.6f), RedPlus, false);
	S.Box(FVector(10.f, -5.f, 21.8f), FVector(4.f, 3.f, 1.6f), Black, false);
	// Наклейка спереди.
	S.Box(FVector(0, -8.6f, 11.f), FVector(16.f, 0.3f, 6.f), FLinearColor(0.75f, 0.7f, 0.2f), false);
}

void SparkPlug(FShape& S, const FVector& Base)
{
	S.Cyl(Base, FVector::UpVector, 0.7f, 1.8f, Steel, 8);                                  // резьба
	S.Cyl(Base + FVector(0, 0, 1.8f), FVector::UpVector, 1.25f, 1.4f, Chrome, 6);            // шестигранник
	S.Cyl(Base + FVector(0, 0, 3.2f), FVector::UpVector, 0.85f, 3.6f, Ceramic, 10, true, -1.f, false); // изолятор
	S.Cyl(Base + FVector(0, 0, 6.8f), FVector::UpVector, 0.35f, 0.9f, Steel, 6);             // контакт
}

void SparkPlugs(FShape& S, bool bInstalled)
{
	for (int32 K = 0; K < 4; ++K)
	{
		const FVector Base(-6.f + K * 4.f, 0.f, 0.f);
		SparkPlug(S, Base);
		if (bInstalled)
		{
			// Колпачки и высоковольтные провода к трамблёру.
			S.Cyl(Base + FVector(0, 0, 6.f), FVector::UpVector, 1.1f, 3.f, Black, 8, true, -1.f, false);
			S.Tube({Base + FVector(0, 0, 9.f), Base + FVector(0, 4.f, 12.f), FVector(-20.f, 22.f, 8.f) + FVector(K * 1.2f, 0, 0)}, 0.45f,
				FLinearColor(0.12f, 0.12f, 0.13f), 6);
		}
	}
}

void Distributor(FShape& S)
{
	S.Cyl(FVector(0, 0, 0), FVector::UpVector, 3.2f, 9.f, Alu, 12);
	S.Cyl(FVector(0, 0, 9.f), FVector::UpVector, 5.2f, 3.5f, FLinearColor(0.12f, 0.06f, 0.04f), 14, true, -1.f, false); // крышка
	for (int32 K = 0; K < 5; ++K)
	{
		const float A = 2.f * PI * K / 5.f;
		const FVector P = K == 0 ? FVector(0, 0, 12.5f) : FVector(FMath::Cos(A) * 3.3f, FMath::Sin(A) * 3.3f, 12.5f);
		S.Cyl(P, FVector::UpVector, 0.8f, 1.8f, FLinearColor(0.1f, 0.05f, 0.04f), 8, true, -1.f, false);
	}
	S.Box(FVector(3.6f, 0, 4.f), FVector(2.5f, 3.f, 3.f), DarkSteel); // вакуумный корректор
}

void Carburetor(FShape& S)
{
	S.Box(FVector(0, 0, 0.6f), FVector(14, 12, 1.2f), DarkSteel);              // фланец
	S.Box(FVector(0, 0, 6.f), FVector(11, 10, 9.6f), Alu);                     // корпус
	S.Cyl(FVector(0, -6.5f, 2.f), FVector::UpVector, 3.f, 7.f, Alu, 12);       // поплавковая камера
	S.Cyl(FVector(-2.5f, 0, 10.8f), FVector::UpVector, 2.6f, 1.5f, Alu, 12);   // диффузоры
	S.Cyl(FVector(2.5f, 0, 10.8f), FVector::UpVector, 2.6f, 1.5f, Alu, 12);
	S.Cyl(FVector(6.f, 2.f, 5.f), FVector::ForwardVector, 2.4f, 0.6f, Steel, 12); // сектор газа
	S.Box(FVector(6.5f, 3.5f, 7.f), FVector(0.5f, 0.6f, 4.f), Steel);
	S.Cyl(FVector(-5.5f, -4.f, 7.f), FVector(-1, 0, 0), 0.5f, 3.f, Brass, 6);  // штуцер бензина
	S.Cyl(FVector(5.5f, -3.f, 3.f), FVector(1, 0, 0), 0.7f, 2.f, Brass, 6);    // винт качества
}

void AirFilter(FShape& S)
{
	S.Cyl(FVector(0, 0, 0), FVector::UpVector, 13.f, 5.f, FLinearColor(0.15f, 0.15f, 0.16f), 20);
	S.Cyl(FVector(0, 0, 5.f), FVector::UpVector, 13.6f, 1.2f, FLinearColor(0.18f, 0.18f, 0.19f), 20); // крышка
	S.Cyl(FVector(0, 0, 6.2f), FVector::UpVector, 1.6f, 1.2f, Chrome, 6);                              // барашек
	S.Cyl(FVector(11.f, 0, 2.5f), FVector::ForwardVector, 3.f, 12.f, FLinearColor(0.15f, 0.15f, 0.16f), 12, true); // заборник
	S.Cyl(FVector(0, 0, -0.6f), FVector::UpVector, 12.5f, 0.6f, FLinearColor(0.85f, 0.75f, 0.4f), 20, false, -1.f, false); // бумага
}

void FuelFilter(FShape& S)
{
	S.Cyl(FVector(-3.5f, 0, 2.f), FVector::ForwardVector, 1.8f, 7.f, Glass, 10, true, -1.f, false);
	S.Cyl(FVector(-1.5f, 0, 2.f), FVector::ForwardVector, 1.5f, 3.f, FLinearColor(0.9f, 0.9f, 0.85f), 10, true, -1.f, false); // сетка
	S.Tube({FVector(-7.f, 0, 2.f), FVector(-3.5f, 0, 2.f)}, 0.6f, Rubber);
	S.Tube({FVector(3.5f, 0, 2.f), FVector(7.f, 0, 2.f)}, 0.6f, Rubber);
}

void OilFilter(FShape& S)
{
	S.Cyl(FVector(0, 0, 0), FVector::UpVector, 4.5f, 11.f, FLinearColor(0.15f, 0.3f, 0.7f), 14);
	S.Ribs(FVector(0, 0, 0.5f), FVector::UpVector, 4.7f, 2.5f, 3, FLinearColor(0.12f, 0.25f, 0.6f));
	S.Cyl(FVector(0, 0, 11.f), FVector::UpVector, 3.8f, 0.8f, DarkSteel, 14);
}

void Radiator(FShape& S)
{
	// Соты: тёмная сердцевина и светлые трубки поперёк.
	S.Box(FVector(0, 0, 20.f), FVector(3.f, 54.f, 32.f), FLinearColor(0.12f, 0.09f, 0.06f));
	for (int32 K = 0; K < 14; ++K)
	{
		S.Box(FVector(-1.6f, 0, 5.5f + K * 2.2f), FVector(0.3f, 53.f, 0.6f), Copper);
	}
	S.Box(FVector(0, 0, 38.f), FVector(5.f, 58.f, 4.f), Brass);   // верхний бачок
	S.Box(FVector(0, 0, 2.f), FVector(5.f, 58.f, 4.f), Brass);    // нижний
	S.Cyl(FVector(0, 18.f, 40.f), FVector::UpVector, 1.8f, 2.f, Brass, 10); // горловина
	S.Cyl(FVector(0, 18.f, 42.f), FVector::UpVector, 2.4f, 0.8f, Chrome, 10);
	S.Cyl(FVector(-2.f, -22.f, 36.f), FVector(-1, 0, 0), 1.8f, 4.f, Brass, 10); // патрубки
	S.Cyl(FVector(-2.f, 22.f, 4.f), FVector(-1, 0, 0), 1.8f, 4.f, Brass, 10);
	S.Box(FVector(0, -29.5f, 20.f), FVector(4.f, 1.f, 38.f), DarkSteel); // боковины
	S.Box(FVector(0, 29.5f, 20.f), FVector(4.f, 1.f, 38.f), DarkSteel);
}

void WaterPump(FShape& S)
{
	S.Cyl(FVector(-4.f, 0, 6.f), FVector::ForwardVector, 5.f, 6.f, Alu, 14);
	S.Cyl(FVector(2.f, 0, 6.f), FVector::ForwardVector, 1.2f, 3.f, Steel, 8);       // вал
	S.Cyl(FVector(4.5f, 0, 6.f), FVector::ForwardVector, 6.5f, 1.6f, DarkSteel, 18); // шкив
	S.Cyl(FVector(6.1f, 0, 6.f), FVector::ForwardVector, 2.f, 0.6f, Steel, 10);
	S.Cyl(FVector(-1.f, 4.f, 6.f), FVector::RightVector, 1.6f, 4.f, Alu, 10);       // штуцер
	// Крыльчатка видна у снятой помпы сзади.
	for (int32 K = 0; K < 6; ++K)
	{
		S.Box(FVector(-4.6f, 0, 6.f), FVector(0.6f, 7.f, 1.f), Steel, true, FRotator(0.f, 0.f, K * 30.f));
	}
}

void Hoses(FShape& S)
{
	// Верхний патрубок (радиатор → термостат) и нижний (помпа → радиатор), с хомутами.
	const TArray<FVector> Upper = {FVector(9.f, 22.f, 8.f), FVector(4.f, 18.f, 9.f), FVector(-3.f, 10.f, 6.f), FVector(-9.f, 4.f, 5.f)};
	const TArray<FVector> Lower = {FVector(9.f, -22.f, -18.f), FVector(4.f, -16.f, -16.f), FVector(-5.f, -8.f, -10.f)};
	S.Tube(Upper, 1.9f, Rubber, 10);
	S.Tube(Lower, 1.9f, Rubber, 10);
	for (const FVector& P : {Upper[0], Upper.Last(), Lower[0], Lower.Last()})
	{
		S.Cyl(P - FVector(0.5f, 0, 0), FVector::ForwardVector, 2.2f, 1.f, Steel, 10, false);
	}
	S.Tube({FVector(-6.f, -18.f, 6.f), FVector(-2.f, -20.f, 10.f), FVector(4.f, -20.f, 10.f)}, 0.9f, Rubber, 8); // печка
}

void Alternator(FShape& S)
{
	S.Cyl(FVector(-6.5f, 0, 8.f), FVector::ForwardVector, 7.5f, 12.f, Alu, 16);
	S.Ribs(FVector(-6.f, 0, 8.f), FVector::ForwardVector, 7.9f, 11.f, 6, Alu);
	S.Cyl(FVector(5.5f, 0, 8.f), FVector::ForwardVector, 1.f, 2.f, Steel, 8);
	S.Cyl(FVector(6.5f, 0, 8.f), FVector::ForwardVector, 4.f, 1.5f, DarkSteel, 16);  // шкив
	S.Cyl(FVector(8.f, 0, 8.f), FVector::ForwardVector, 5.5f, 0.4f, DarkSteel, 16);  // крыльчатка вентилятора
	S.Box(FVector(0, 0, 16.f), FVector(4.f, 3.f, 2.5f), DarkSteel);                   // ухо натяжителя
	S.Box(FVector(-7.f, -5.f, 12.f), FVector(2.f, 2.f, 2.f), Black, false);           // разъём
}

void Starter(FShape& S)
{
	S.Cyl(FVector(-8.f, 0, 5.f), FVector::ForwardVector, 4.8f, 13.f, DarkSteel, 14);
	S.Cyl(FVector(-6.f, 0, 10.f), FVector::ForwardVector, 2.4f, 9.f, Steel, 12);   // втягивающее реле
	S.Cyl(FVector(5.f, 0, 5.f), FVector::ForwardVector, 3.2f, 4.f, Alu, 12);       // нос
	S.Cyl(FVector(9.f, 0, 5.f), FVector::ForwardVector, 1.6f, 1.5f, Steel, 10);    // бендикс
	S.Cyl(FVector(-6.f, 1.5f, 12.4f), FVector::UpVector, 0.5f, 1.2f, Brass, 6);
}

void Turbo(FShape& S)
{
	// Холодная улитка (компрессор, алюминий) и горячая (турбина, чугун), между ними — картридж.
	S.Cyl(FVector(0, -8.f, 9.f), FVector::RightVector, 8.f, 6.f, Alu, 18);
	S.Cyl(FVector(0, 2.f, 9.f), FVector::RightVector, 3.5f, 4.f, Steel, 12);
	S.Cyl(FVector(0, 6.f, 9.f), FVector::RightVector, 7.f, 6.f, FLinearColor(0.25f, 0.18f, 0.14f), 18);
	S.Cyl(FVector(0, -11.f, 9.f), FVector::LeftVector, 3.5f, 5.f, Alu, 12);         // вход воздуха
	S.Tube({FVector(0, -6.f, 16.f), FVector(0, -6.f, 20.f), FVector(-8.f, -6.f, 22.f)}, 2.2f, Alu, 10, true); // выход к карбюратору
	S.Tube({FVector(6.f, 8.f, 9.f), FVector(10.f, 10.f, 4.f), FVector(10.f, 10.f, -4.f)}, 2.4f, FLinearColor(0.25f, 0.18f, 0.14f), 10, true);
	S.Tube({FVector(-2.f, 2.f, 13.f), FVector(-4.f, 2.f, 18.f), FVector(-10.f, 0.f, 20.f)}, 0.5f, Steel, 6, true); // маслопровод
}

void Exhaust(FShape& S)
{
	S.Tube({FVector(40.f, 0, 6.f), FVector(22.f, 0, 6.f)}, 2.5f, DarkSteel, 10, true);
	S.Cyl(FVector(-22.f, 0, 6.f), FVector::ForwardVector, 6.f, 44.f, DarkSteel, 16, true, 9.f);   // банка
	S.Tube({FVector(-22.f, 3.f, 6.f), FVector(-34.f, 3.f, 5.f), FVector(-42.f, 3.f, 4.f)}, 2.3f, DarkSteel, 10, true);
	S.Cyl(FVector(-42.f, 3.f, 4.f), FVector::BackwardVector, 2.6f, 2.f, Chrome, 10, false);
	S.Box(FVector(0, 0, 13.f), FVector(1.f, 3.f, 3.f), Rubber, false); // подушка
}

void Suspension(FShape& S)
{
	// Пружина (виток трубкой) и амортизатор внутри.
	TArray<FVector> Coil;
	for (int32 K = 0; K <= 48; ++K)
	{
		const float A = K / 48.f * 2.f * PI * 6.f;
		Coil.Add(FVector(FMath::Cos(A) * 5.f, FMath::Sin(A) * 5.f, 2.f + K * 0.5f));
	}
	S.Tube(Coil, 0.65f, FLinearColor(0.2f, 0.22f, 0.25f), 6, true);
	S.Cyl(FVector(0, 0, 0), FVector::UpVector, 2.2f, 16.f, Black, 12);
	S.Cyl(FVector(0, 0, 16.f), FVector::UpVector, 0.8f, 10.f, Chrome, 8);
	S.Cyl(FVector(0, 0, 26.f), FVector::UpVector, 6.f, 1.f, DarkSteel, 14);
}

void BrakePads(FShape& S)
{
	for (float Y : {-2.f, 2.f})
	{
		S.Box(FVector(0, Y, 3.f), FVector(12.f, 0.6f, 5.5f), DarkSteel);
		S.Box(FVector(0, Y * 0.6f, 3.f), FVector(11.f, 1.f, 4.5f), FLinearColor(0.3f, 0.26f, 0.22f), false);
	}
}

void Headlights(FShape& S)
{
	for (float Y : {-55.f, 55.f})
	{
		S.Box(FVector(-3.f, Y, 7.f), FVector(6.f, 22.f, 13.f), Black, false);
		S.Box(FVector(0.2f, Y, 7.f), FVector(0.6f, 21.f, 12.f), Chrome);
		S.Box(FVector(0.7f, Y, 7.f), FVector(0.4f, 19.f, 10.f), Glass, false);
	}
}

void Taillights(FShape& S)
{
	for (float Y : {-55.f, 55.f})
	{
		S.Box(FVector(0, Y, 7.f), FVector(4.f, 26.f, 12.f), Black, false);
		S.Box(FVector(-2.2f, Y - 6.f * FMath::Sign(Y), 7.f), FVector(0.6f, 12.f, 10.f), FLinearColor(0.7f, 0.05f, 0.04f), false);
		S.Box(FVector(-2.2f, Y + 4.f * FMath::Sign(Y), 7.f), FVector(0.6f, 6.f, 10.f), FLinearColor(0.85f, 0.45f, 0.05f), false);
		S.Box(FVector(-2.2f, Y + 9.5f * FMath::Sign(Y), 7.f), FVector(0.6f, 4.f, 10.f), FLinearColor(0.85f, 0.85f, 0.82f), false);
	}
}

void Dashboard(FShape& S)
{
	S.Box(FVector(0, 0, 6.f), FVector(4.f, 34.f, 12.f), Black, false);
	for (float Y : {-8.f, 8.f})
	{
		S.Cyl(FVector(-2.2f, Y, 6.5f), FVector::BackwardVector, 5.f, 0.5f, Chrome, 18, true);
		S.Cyl(FVector(-2.8f, Y, 6.5f), FVector::BackwardVector, 4.4f, 0.3f, FLinearColor(0.05f, 0.05f, 0.05f), 18, true, -1.f, false);
		S.Box(FVector(-3.f, Y + 1.5f, 7.5f), FVector(0.2f, 3.5f, 0.4f), FLinearColor(0.95f, 0.5f, 0.15f), false, FRotator(0.f, 0.f, 30.f));
	}
	S.Box(FVector(-2.3f, 0, 2.f), FVector(0.4f, 8.f, 2.5f), FLinearColor(0.15f, 0.3f, 0.15f), false); // лампы
}

void Radio(FShape& S)
{
	S.Box(FVector(0, 0, 2.5f), FVector(15.f, 18.f, 5.f), FLinearColor(0.1f, 0.1f, 0.11f), false);
	S.Box(FVector(-7.6f, 0, 2.5f), FVector(0.3f, 17.f, 4.6f), FLinearColor(0.2f, 0.2f, 0.22f), false);
	S.Box(FVector(-7.9f, 0, 3.4f), FVector(0.3f, 7.f, 0.8f), FLinearColor(0.02f, 0.02f, 0.02f), false); // кассетная щель
	S.Box(FVector(-7.9f, 0, 1.8f), FVector(0.2f, 6.f, 0.8f), FLinearColor(0.95f, 0.6f, 0.2f), false);   // шкала
	S.Cyl(FVector(-7.8f, -7.f, 2.5f), FVector::BackwardVector, 1.1f, 1.4f, Chrome, 10);
	S.Cyl(FVector(-7.8f, 7.f, 2.5f), FVector::BackwardVector, 1.1f, 1.4f, Chrome, 10);
}

// --- Инструменты ---
void Wrenches(FShape& S)
{
	S.Box(FVector(0, 0, 3.f), FVector(40.f, 18.f, 6.f), FLinearColor(0.55f, 0.1f, 0.08f), false); // кейс
	S.Box(FVector(0, 0, 6.2f), FVector(40.5f, 18.5f, 0.4f), FLinearColor(0.45f, 0.08f, 0.06f), false);
	S.Box(FVector(0, -9.6f, 4.f), FVector(10.f, 1.2f, 1.6f), Black, false); // ручка
	for (int32 K = 0; K < 6; ++K)
	{
		S.Box(FVector(-14.f + K * 5.6f, 2.f, 6.7f), FVector(1.2f, 12.f, 0.4f), Chrome);
	}
}

void PlugSocket(FShape& S)
{
	S.Cyl(FVector(0, 0, 0), FVector::UpVector, 1.4f, 9.f, Chrome, 6);
	S.Cyl(FVector(0, -7.f, 8.f), FVector::RightVector, 0.6f, 14.f, Chrome, 8);
}

void Screwdriver(FShape& S)
{
	for (int32 K = 0; K < 3; ++K)
	{
		const FVector Base(0, -4.f + K * 4.f, 1.5f);
		S.Cyl(Base + FVector(-10.f, 0, 0), FVector::ForwardVector, 1.4f, 9.f, K == 1 ? FLinearColor(0.8f, 0.6f, 0.05f) : FLinearColor(0.7f, 0.1f, 0.08f), 8, true,
			-1.f, false);
		S.Cyl(Base + FVector(-1.f, 0, 0), FVector::ForwardVector, 0.35f, 10.f, Chrome, 6);
	}
}

void Pliers(FShape& S)
{
	S.Box(FVector(-6.f, -1.5f, 0.8f), FVector(11.f, 1.4f, 1.f), FLinearColor(0.15f, 0.25f, 0.6f), false, FRotator(0.f, 8.f, 0.f));
	S.Box(FVector(-6.f, 1.5f, 0.8f), FVector(11.f, 1.4f, 1.f), FLinearColor(0.15f, 0.25f, 0.6f), false, FRotator(0.f, -8.f, 0.f));
	S.Box(FVector(3.f, 0, 0.8f), FVector(6.f, 2.4f, 1.2f), Chrome);
	S.Cyl(FVector(0.5f, 0, 0.1f), FVector::UpVector, 1.2f, 1.4f, Steel, 10);
}

void Jack(FShape& S)
{
	S.Box(FVector(0, 0, 0.8f), FVector(18.f, 10.f, 1.6f), FLinearColor(0.6f, 0.08f, 0.06f));
	// Ромб из четырёх рычагов и винт.
	S.Box(FVector(-4.5f, 0, 5.f), FVector(11.f, 4.f, 1.f), FLinearColor(0.6f, 0.08f, 0.06f), true, FRotator(45.f, 0.f, 0.f));
	S.Box(FVector(4.5f, 0, 5.f), FVector(11.f, 4.f, 1.f), FLinearColor(0.6f, 0.08f, 0.06f), true, FRotator(-45.f, 0.f, 0.f));
	S.Box(FVector(-4.5f, 0, 12.f), FVector(11.f, 4.f, 1.f), FLinearColor(0.6f, 0.08f, 0.06f), true, FRotator(-45.f, 0.f, 0.f));
	S.Box(FVector(4.5f, 0, 12.f), FVector(11.f, 4.f, 1.f), FLinearColor(0.6f, 0.08f, 0.06f), true, FRotator(45.f, 0.f, 0.f));
	S.Cyl(FVector(-12.f, 0, 8.5f), FVector::ForwardVector, 0.6f, 24.f, Steel, 8);
	S.Box(FVector(0, 0, 16.5f), FVector(6.f, 4.f, 1.f), DarkSteel);
	// Баллонник рядом.
	S.Box(FVector(0, 7.f, 0.6f), FVector(30.f, 1.4f, 1.2f), Steel);
	S.Cyl(FVector(15.f, 7.f, 0.f), FVector::UpVector, 1.4f, 3.f, Steel, 6);
}

void Grinder(FShape& S)
{
	S.Cyl(FVector(-14.f, 0, 5.f), FVector::ForwardVector, 3.8f, 18.f, FLinearColor(0.1f, 0.42f, 0.28f), 14, true, -1.f, false); // корпус
	S.Ribs(FVector(-14.f, 0, 5.f), FVector::ForwardVector, 3.9f, 5.f, 4, Black);
	S.Box(FVector(6.f, 0, 5.f), FVector(6.f, 7.f, 6.f), Alu);                        // редуктор
	S.Cyl(FVector(8.f, 0, 1.f), FVector::DownVector, 0.8f, 1.5f, Steel, 8);          // шпиндель
	S.Cyl(FVector(8.f, 0, -0.8f), FVector::UpVector, 6.2f, 0.4f, FLinearColor(0.25f, 0.28f, 0.45f), 20, true, -1.f, false); // диск
	S.Cyl(FVector(6.5f, 0, 0.2f), FVector::UpVector, 6.8f, 0.3f, Steel, 20, true);   // кожух
	S.Cyl(FVector(5.f, 3.5f, 6.f), FVector::RightVector, 1.1f, 8.f, Black, 8, true, -1.f, false); // боковая ручка
}

void SprayCan(FShape& S, const FLinearColor& Body)
{
	S.Cyl(FVector(0, 0, 0), FVector::UpVector, 3.3f, 19.f, Body, 14, true, -1.f, false);
	S.Cyl(FVector(0, 0, 19.f), FVector::UpVector, 2.6f, 1.5f, Chrome, 12);
	S.Cyl(FVector(0, 0, 20.5f), FVector::UpVector, 2.9f, 3.f, Body * 0.8f, 12, true, -1.f, false);
	S.Box(FVector(0, -3.2f, 10.f), FVector(4.f, 0.3f, 8.f), FLinearColor(0.9f, 0.88f, 0.8f), false); // этикетка
}

void PuttyCan(FShape& S)
{
	S.Cyl(FVector(0, 0, 0), FVector::UpVector, 6.5f, 10.f, FLinearColor(0.75f, 0.72f, 0.45f), 16, true, -1.f, false);
	S.Cyl(FVector(0, 0, 10.f), FVector::UpVector, 6.8f, 1.2f, FLinearColor(0.2f, 0.2f, 0.22f), 16, true, -1.f, false);
	S.Cyl(FVector(4.f, 0, 11.2f), FVector::UpVector, 1.5f, 3.f, FLinearColor(0.8f, 0.2f, 0.1f), 8, true, -1.f, false); // тюбик отвердителя
	S.Box(FVector(-2.f, 0, 11.5f), FVector(8.f, 3.f, 0.3f), Steel); // шпатель
}

void Discs(FShape& S, const FLinearColor& C, int32 Count)
{
	for (int32 K = 0; K < Count; ++K)
	{
		S.Cyl(FVector(0, 0, K * 0.7f), FVector::UpVector, 6.2f, 0.5f, C, 18, true, -1.f, false);
	}
	S.Cyl(FVector(0, 0, Count * 0.7f), FVector::UpVector, 1.1f, 0.4f, Steel, 10, true);
}

void Canister(FShape& S, const FLinearColor& C)
{
	S.Box(FVector(0, 0, 11.f), FVector(16.f, 9.f, 22.f), C, false);
	S.Box(FVector(2.f, 0, 23.5f), FVector(9.f, 2.f, 3.f), C, false); // ручка
	S.Cyl(FVector(-5.f, 0, 22.f), FVector::UpVector, 1.8f, 3.f, FLinearColor(0.1f, 0.1f, 0.1f), 10, true, -1.f, false);
	S.Box(FVector(0, -4.6f, 11.f), FVector(10.f, 0.3f, 9.f), FLinearColor(0.9f, 0.85f, 0.7f), false); // этикетка
}
} // namespace PeregonPartMeshDetail

bool PeregonPartMeshes::Has(FName ItemId)
{
	const FPeregonItemDef* Def = PeregonItems::Find(ItemId);
	return Def && Def->Kind != EPeregonItemKind::Misc;
}

FVector PeregonPartMeshes::Size(FName ItemId)
{
	const FPeregonItemDef* Def = PeregonItems::Find(ItemId);
	return Def ? Def->SizeCm : FVector(20.f);
}

void PeregonPartMeshes::Build(FName ItemId, FPeregonMeshBuffers& Out, const FTransform& Where, float Condition, const FLinearColor& Tint)
{
	using namespace PeregonPartMeshDetail;
	using vsim::parts::Consumable;
	using vsim::parts::Slot;
	using vsim::parts::Tool;
	const FPeregonItemDef* Def = PeregonItems::Find(ItemId);
	if (!Def)
	{
		return;
	}
	const int32 Start = Out.Vertices.Num();
	FShape S{Out, FMath::Clamp(1.f - Condition, 0.f, 1.f)};
	switch (Def->Kind)
	{
	case EPeregonItemKind::Part:
		switch (static_cast<Slot>(Def->PartSlot))
		{
		case Slot::Battery: Battery(S); break;
		case Slot::SparkPlugs: SparkPlugs(S, false); break;
		case Slot::Distributor: Distributor(S); break;
		case Slot::Carburetor: Carburetor(S); break;
		case Slot::AirFilter: AirFilter(S); break;
		case Slot::FuelFilter: FuelFilter(S); break;
		case Slot::OilFilter: OilFilter(S); break;
		case Slot::Radiator: Radiator(S); break;
		case Slot::WaterPump: WaterPump(S); break;
		case Slot::Hoses: Hoses(S); break;
		case Slot::Alternator: Alternator(S); break;
		case Slot::Starter: Starter(S); break;
		case Slot::Turbo: Turbo(S); break;
		case Slot::Exhaust: Exhaust(S); break;
		case Slot::SuspensionFront:
		case Slot::SuspensionRear: Suspension(S); break;
		case Slot::BrakePads: BrakePads(S); break;
		case Slot::Headlights: Headlights(S); break;
		case Slot::Taillights: Taillights(S); break;
		case Slot::Dashboard: Dashboard(S); break;
		case Slot::Radio: Radio(S); break;
		default: S.Box(FVector(0, 0, Def->SizeCm.Z * 0.5f), Def->SizeCm, Def->Color); break;
		}
		break;
	case EPeregonItemKind::Tool:
		switch (static_cast<Tool>(Def->ToolKind))
		{
		case Tool::Wrenches: Wrenches(S); break;
		case Tool::PlugSocket: PlugSocket(S); break;
		case Tool::Screwdriver: Screwdriver(S); break;
		case Tool::Pliers: Pliers(S); break;
		case Tool::Jack: Jack(S); break;
		case Tool::Grinder: Grinder(S); break;
		default: break;
		}
		break;
	case EPeregonItemKind::Consumable:
		switch (static_cast<Consumable>(Def->ConsumableKind))
		{
		case Consumable::CutDisc: Discs(S, FLinearColor(0.2f, 0.25f, 0.5f), 5); break;
		case Consumable::FlapDisc: Discs(S, FLinearColor(0.75f, 0.55f, 0.3f), 3); break;
		case Consumable::Putty: PuttyCan(S); break;
		case Consumable::Primer: SprayCan(S, FLinearColor(0.5f, 0.5f, 0.5f)); break;
		case Consumable::Paint: SprayCan(S, Def->PaintColor * Tint); break;
		default: break;
		}
		break;
	case EPeregonItemKind::Fluid:
		Canister(S, Def->Color);
		break;
	default:
		break;
	}
	// В место: поворот и сдвиг.
	for (int32 I = Start; I < Out.Vertices.Num(); ++I)
	{
		Out.Vertices[I] = Where.TransformPosition(Out.Vertices[I]);
		Out.Normals[I] = Where.TransformVectorNoScale(Out.Normals[I]);
	}
}

namespace PeregonPartMeshes
{
// Узел, стоящий в машине: свечи — с колпачками и проводами (используется PeregonCarParts).
void BuildInstalled(int32 SlotIndex, FPeregonMeshBuffers& Out, const FTransform& Where, float Condition)
{
	using namespace PeregonPartMeshDetail;
	if (static_cast<vsim::parts::Slot>(SlotIndex) == vsim::parts::Slot::SparkPlugs)
	{
		const int32 Start = Out.Vertices.Num();
		FShape S{Out, FMath::Clamp(1.f - Condition, 0.f, 1.f)};
		SparkPlugs(S, true);
		for (int32 I = Start; I < Out.Vertices.Num(); ++I)
		{
			Out.Vertices[I] = Where.TransformPosition(Out.Vertices[I]);
			Out.Normals[I] = Where.TransformVectorNoScale(Out.Normals[I]);
		}
		return;
	}
	Build(PeregonItems::PartItem(SlotIndex), Out, Where, Condition);
}
} // namespace PeregonPartMeshes
