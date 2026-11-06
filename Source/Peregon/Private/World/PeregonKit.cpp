#include "World/PeregonKit.h"

#include "Art/PeregonArtSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "rgen/Noise.h"

namespace PeregonKitDetail
{
FColor ToColor(const FLinearColor& C)
{
	return FColor(static_cast<uint8>(FMath::Clamp(C.R, 0.f, 1.f) * 255.f), static_cast<uint8>(FMath::Clamp(C.G, 0.f, 1.f) * 255.f),
		static_cast<uint8>(FMath::Clamp(C.B, 0.f, 1.f) * 255.f), static_cast<uint8>(FMath::Clamp(C.A, 0.f, 1.f) * 255.f));
}

// Выцветшие советские панели: белые, бежевые, голубоватые, жёлтые, розоватые.
const FLinearColor PanelColors[] = {FLinearColor(0.93f, 0.93f, 0.9f), FLinearColor(0.96f, 0.88f, 0.74f), FLinearColor(0.8f, 0.88f, 0.96f),
	FLinearColor(0.96f, 0.92f, 0.68f), FLinearColor(0.96f, 0.82f, 0.8f), FLinearColor(0.82f, 0.83f, 0.84f)};

UStaticMesh* LoadShape(const TCHAR* Path)
{
	return LoadObject<UStaticMesh>(nullptr, Path);
}
} // namespace PeregonKitDetail

// ---------------------------------------------------------------------------------------------
// Геометрия
// ---------------------------------------------------------------------------------------------
void FPeregonMeshBuffers::AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal, const FVector2D& UvA,
	const FVector2D& UvC, const FColor& Color, uint8 TopAlpha)
{
	const int32 Base = Vertices.Num();
	Vertices.Append({A, B, C, D});
	UVs.Append({UvA, FVector2D(UvC.X, UvA.Y), UvC, FVector2D(UvA.X, UvC.Y)});
	const FVector N = Normal.GetSafeNormal();
	for (int32 I = 0; I < 4; ++I)
	{
		Normals.Add(N);
	}
	// Низ (A, B) стоит на месте, верх (C, D) гнётся ветром — вес в альфе цвета.
	FColor Bottom = Color;
	Bottom.A = 0;
	FColor Top = Color;
	Top.A = TopAlpha;
	Colors.Append({Bottom, Bottom, Top, Top});
	// Лицевая сторона в Unreal — куда смотрит (C − A) × (B − A).
	if (FVector::DotProduct(FVector::CrossProduct(C - A, B - A), N) >= 0.0)
	{
		Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
	}
	else
	{
		Triangles.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
	}
}

void FPeregonMeshBuffers::AddBox(const FVector& BaseCenter, float YawDeg, const FVector& SizeCm, const FVector2D& TileCm, const FColor& Color, bool bTop,
	bool bBottom)
{
	const FRotator Rot(0.f, YawDeg, 0.f);
	const FVector Ax = Rot.RotateVector(FVector::ForwardVector);
	const FVector Ay = Rot.RotateVector(FVector::RightVector);
	const FVector Hx = Ax * SizeCm.X * 0.5;
	const FVector Hy = Ay * SizeCm.Y * 0.5;
	const FVector Up(0.f, 0.f, SizeCm.Z);
	const float Vh = SizeCm.Z / TileCm.Y;
	auto Wall = [&](const FVector& From, const FVector& To, const FVector& Out) {
		const float Len = (To - From).Size() / TileCm.X;
		AddQuad(From, To, To + Up, From + Up, Out, FVector2D(0.f, Vh), FVector2D(Len, 0.f), Color, 0);
	};
	const FVector P00 = BaseCenter - Hx - Hy;
	const FVector P10 = BaseCenter + Hx - Hy;
	const FVector P11 = BaseCenter + Hx + Hy;
	const FVector P01 = BaseCenter - Hx + Hy;
	Wall(P00, P10, -Ay);
	Wall(P10, P11, Ax);
	Wall(P11, P01, Ay);
	Wall(P01, P00, -Ax);
	if (bTop)
	{
		AddQuad(P00 + Up, P10 + Up, P11 + Up, P01 + Up, FVector::UpVector, FVector2D(0.f, 0.f), FVector2D(SizeCm.X / TileCm.X, SizeCm.Y / TileCm.X), Color, 0);
	}
	if (bBottom)
	{
		AddQuad(P00, P01, P11, P10, FVector::DownVector, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), Color, 0);
	}
}

void FPeregonMeshBuffers::AddClump(const FVector& Root, float Radius, float Height, int32 Planes, float Yaw0, const FColor& Tint)
{
	for (int32 K = 0; K < Planes; ++K)
	{
		const float A = Yaw0 + PI * K / Planes;
		const FVector Axis(FMath::Cos(A), FMath::Sin(A), 0.f);
		const FVector Side = Axis * Radius;
		const FVector Up(0.f, 0.f, Height);
		AddQuad(Root - Side, Root + Side, Root + Side + Up, Root - Side + Up, FVector::CrossProduct(Axis, FVector::UpVector) + FVector(0, 0, 0.6f),
			FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), Tint, 255);
	}
}

// ---------------------------------------------------------------------------------------------
// Материалы
// ---------------------------------------------------------------------------------------------
FPeregonKitMaterials FPeregonKitMaterials::Create(UObject* Outer)
{
	FPeregonKitMaterials M;
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(Outer);
	if (!Art)
	{
		return M;
	}
	auto Make = [&](const FPeregonLook& Look) { return static_cast<UMaterialInterface*>(Art->MakeMaterial(Outer, Look)); };
	M.Panel = Make(FPeregonLook(EPeregonTex::PanelFacade, FLinearColor::White, 0.85f));
	M.Brick = Make(FPeregonLook(EPeregonTex::BrickFacade, FLinearColor::White, 0.9f));
	M.Roof = Make(FPeregonLook(EPeregonTex::Concrete, FLinearColor(0.42f, 0.42f, 0.44f), 0.9f));
	M.MetalRoof = Make(FPeregonLook(EPeregonTex::RoofMetal, FLinearColor(0.45f, 0.5f, 0.52f), 0.5f, 0.5f));
	FPeregonLook Window(EPeregonTex::White, FLinearColor(0.85f, 0.68f, 0.38f), 0.6f);
	Window.NightGlow = 1.6f;
	Window.WetMask = 0.f;
	Window.SnowMask = 0.f;
	M.LitWindow = Make(Window);
	M.Concrete = Make(FPeregonLook(EPeregonTex::Concrete, FLinearColor::White, 0.9f));
	M.Stele = Make(FPeregonLook(EPeregonTex::Concrete, FLinearColor(0.95f, 0.95f, 0.93f), 0.8f));
	M.Accent = Make(FPeregonLook(EPeregonTex::Paint, FLinearColor(0.08f, 0.22f, 0.5f), 0.5f));
	M.Red = Make(FPeregonLook(EPeregonTex::Paint, FLinearColor(0.55f, 0.06f, 0.04f), 0.6f));
	M.White = Make(FPeregonLook(EPeregonTex::Paint, FLinearColor(0.85f, 0.85f, 0.83f), 0.5f));
	FPeregonLook Gold(EPeregonTex::Chrome, FLinearColor(1.f, 0.72f, 0.28f), 0.25f, 1.f);
	Gold.SnowMask = 0.f;
	M.Gold = Make(Gold);
	M.Wood = Make(FPeregonLook(EPeregonTex::WoodPlanks, FLinearColor(0.75f, 0.6f, 0.42f), 0.9f));
	M.DarkWood = Make(FPeregonLook(EPeregonTex::WoodPlanks, FLinearColor(0.42f, 0.32f, 0.24f), 0.9f));
	M.Pole = Make(FPeregonLook(EPeregonTex::PineBark, FLinearColor(0.45f, 0.4f, 0.35f), 0.95f));
	FPeregonLook Lamp(EPeregonTex::White, FLinearColor(1.f, 0.78f, 0.45f), 0.5f);
	Lamp.NightGlow = 12.f;
	Lamp.WetMask = 0.f;
	Lamp.SnowMask = 0.f;
	M.Lamp = Make(Lamp);
	FPeregonLook Tuft(EPeregonTex::GrassTuft, FLinearColor(0.55f, 0.62f, 0.38f), 0.95f);
	Tuft.Sway = 1.f;
	M.GrassTuft = Make(Tuft);
	FPeregonLook Bush(EPeregonTex::Bush, FLinearColor(0.42f, 0.55f, 0.3f), 0.9f);
	Bush.Sway = 0.45f;
	M.Bush = Make(Bush);
	FPeregonLook Glass(EPeregonTex::Glass, FLinearColor::White, 0.1f);
	Glass.WetMask = 0.f;
	Glass.SnowMask = 0.f;
	M.Glass = Make(Glass);
	FPeregonLook Leaves(EPeregonTex::Leaves, FLinearColor::White, 0.9f);
	Leaves.Sway = 0.3f;
	M.Leaves = Make(Leaves);
	FPeregonLook Needles(EPeregonTex::Needles, FLinearColor::White, 0.95f);
	Needles.Sway = 0.12f;
	M.Needles = Make(Needles);
	M.BirchBark = Make(FPeregonLook(EPeregonTex::BirchBark, FLinearColor::White, 0.9f));
	M.PineBark = Make(FPeregonLook(EPeregonTex::PineBark, FLinearColor::White, 0.95f));
	M.Asphalt = Make(FPeregonLook(EPeregonTex::Asphalt, FLinearColor(0.95f, 0.95f, 0.95f), 0.85f));
	M.Dirt = Make(FPeregonLook(EPeregonTex::Dirt, FLinearColor::White, 0.95f));
	M.Paint = Make(FPeregonLook(EPeregonTex::Paint, FLinearColor::White, 0.6f));
	return M;
}

// ---------------------------------------------------------------------------------------------
// Базовые операции
// ---------------------------------------------------------------------------------------------
FPeregonKit::FPeregonKit(AActor* InOwner, USceneComponent* InRoot, TArray<TWeakObjectPtr<UActorComponent>>& InOut, const FPeregonKitMaterials& InMats)
	: Owner(InOwner), Root(InRoot), Out(InOut), Mats(InMats)
{
}

UStaticMesh* FPeregonKit::Cube()
{
	static TWeakObjectPtr<UStaticMesh> Cached;
	if (!Cached.IsValid())
	{
		Cached = PeregonKitDetail::LoadShape(TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	return Cached.Get();
}

UStaticMesh* FPeregonKit::Cylinder()
{
	static TWeakObjectPtr<UStaticMesh> Cached;
	if (!Cached.IsValid())
	{
		Cached = PeregonKitDetail::LoadShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	}
	return Cached.Get();
}

UStaticMesh* FPeregonKit::Sphere()
{
	static TWeakObjectPtr<UStaticMesh> Cached;
	if (!Cached.IsValid())
	{
		Cached = PeregonKitDetail::LoadShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	}
	return Cached.Get();
}

UStaticMesh* FPeregonKit::Cone()
{
	static TWeakObjectPtr<UStaticMesh> Cached;
	if (!Cached.IsValid())
	{
		Cached = PeregonKitDetail::LoadShape(TEXT("/Engine/BasicShapes/Cone.Cone"));
	}
	return Cached.Get();
}

UProceduralMeshComponent* FPeregonKit::AddProc(const FPeregonMeshBuffers& Data, UMaterialInterface* Material, bool bCollision, bool bShadow)
{
	if (Data.Vertices.Num() == 0 || !Owner)
	{
		return nullptr;
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetupAttachment(Root);
	Mesh->bUseAsyncCooking = true;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Mesh->SetCastShadow(bShadow);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->RegisterComponent();
	const bool bColors = Data.Colors.Num() == Data.Vertices.Num();
	Mesh->CreateMeshSection(0, Data.Vertices, Data.Triangles, Data.Normals, Data.UVs, bColors ? Data.Colors : TArray<FColor>(), TArray<FProcMeshTangent>(),
		bCollision);
	if (Material)
	{
		Mesh->SetMaterial(0, Material);
	}
	Out.Add(Mesh);
	return Mesh;
}

UStaticMeshComponent* FPeregonKit::AddMesh(UStaticMesh* InMesh, const FTransform& Transform, UMaterialInterface* Material, bool bCollision)
{
	if (!Owner || !InMesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(Root);
	Comp->SetStaticMesh(InMesh);
	Comp->SetRelativeTransform(Transform);
	Comp->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Comp->SetCanEverAffectNavigation(false);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->RegisterComponent();
	Out.Add(Comp);
	return Comp;
}

UStaticMeshComponent* FPeregonKit::Box(const FVector& Center, const FRotator& Rot, const FVector& SizeM, UMaterialInterface* Material, bool bCollision)
{
	return AddMesh(Cube(), FTransform(Rot, Center, SizeM), Material, bCollision);
}

void FPeregonKit::Label(const FTransform& Transform, const FString& Text, float WidthCm, float HeightCm, int32 FontPx, const FLinearColor& Ink,
	const FLinearColor& Paper)
{
	UPeregonArtSubsystem* Art = Owner ? UPeregonArtSubsystem::Get(Owner) : nullptr;
	if (!Art)
	{
		return;
	}
	const int32 HeightPx = FMath::Max(64, FMath::RoundToInt(512.f * HeightCm / FMath::Max(1.f, WidthCm)));
	UMaterialInstanceDynamic* LabelMat = Art->MakeLabelMaterial(Owner, Text, Ink, Paper, 512, HeightPx, FontPx, EPeregonFont::Display);
	if (UProceduralMeshComponent* Quad = UPeregonArtSubsystem::AddLabelQuad(Owner, Root, Transform, WidthCm, HeightCm, LabelMat))
	{
		Out.Add(Quad);
	}
}

// ---------------------------------------------------------------------------------------------
// Город
// ---------------------------------------------------------------------------------------------
void FPeregonKit::Buildings(const std::vector<rgen::Building>& List)
{
	using namespace PeregonKitDetail;
	constexpr double M = 100.0;
	FPeregonMeshBuffers PanelWalls;
	FPeregonMeshBuffers BrickWalls;
	FPeregonMeshBuffers Roofs;
	FPeregonMeshBuffers Windows;
	FPeregonMeshBuffers Plant;
	FPeregonMeshBuffers PlantRoof;
	UInstancedStaticMeshComponent* RedBands = nullptr;
	UInstancedStaticMeshComponent* WhiteBands = nullptr;
	auto Stripes = [&]() {
		if (RedBands)
		{
			return;
		}
		auto MakeIsm = [&](UMaterialInterface* Material) {
			UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(Owner);
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetupAttachment(Root);
			Comp->SetStaticMesh(Cylinder());
			Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Comp->SetMaterial(0, Material);
			Comp->RegisterComponent();
			Out.Add(Comp);
			return Comp;
		};
		RedBands = MakeIsm(Mats.Red);
		WhiteBands = MakeIsm(Mats.White);
	};

	FPeregonMeshBuffers Props;   // окрашено цветом вершин (машины, лавки, баки, площадки, гаражные ворота)
	FPeregonMeshBuffers Wheels;  // тёмное: колёса, шины-клумбы
	FPeregonMeshBuffers ShopWalls;
	FPeregonMeshBuffers GarageBlocks;
	for (const rgen::Building& B : List)
	{
		const FVector Base = FVector(B.X, B.Y, B.Z) * M;
		const float Yaw = FMath::RadiansToDegrees(static_cast<float>(B.Yaw));
		const float H = static_cast<float>(B.Height() * M);
		const FVector Size(B.Length * M, B.Width * M, H);
		rgen::Rng Rand(B.Seed);
		switch (B.Kind)
		{
		case rgen::BuildingKind::Panel5:
		case rgen::BuildingKind::Panel9:
		case rgen::BuildingKind::Tower:
		case rgen::BuildingKind::Brick:
		{
			const bool bBrick = B.Kind == rgen::BuildingKind::Brick;
			const FLinearColor Tint = bBrick ? FLinearColor::White : PanelColors[B.Seed % UE_ARRAY_COUNT(PanelColors)];
			(bBrick ? BrickWalls : PanelWalls).AddBox(Base, Yaw, Size, FVector2D(300.f, 280.f), ToColor(Tint), false, false);
			Roofs.AddBox(Base + FVector(0.f, 0.f, H), Yaw, FVector(Size.X, Size.Y, 45.f), FVector2D(400.f, 400.f), FColor::White, true, false);
			// Козырьки подъездов на длинной стороне.
			const FRotator Rot(0.f, Yaw, 0.f);
			const FVector Ax = Rot.RotateVector(FVector::ForwardVector);
			const FVector Ay = Rot.RotateVector(FVector::RightVector);
			if (!bBrick && B.Kind != rgen::BuildingKind::Tower)
			{
				for (float Entrance = -0.5f * static_cast<float>(Size.X) + 1500.f; Entrance < 0.5f * static_cast<float>(Size.X) - 600.f; Entrance += 1500.f)
				{
					Roofs.AddBox(Base + Ax * Entrance - Ay * (Size.Y * 0.5 + 75.0) + FVector(0.f, 0.f, 260.f), Yaw, FVector(220.f, 150.f, 15.f), FVector2D(400.f, 400.f),
						FColor::White, true, true);
				}
			}
			Yard(B, Props, Wheels);
			// Свет в окнах вечером: на месте окон текстуры (пролёт 3 м, этаж 2.8 м от верха стены).
			const int32 Floors = FMath::FloorToInt(H / 280.f);
			const float LitShare = 0.1f + 0.1f * static_cast<float>(Rand.Unit());
			for (int32 Face = 0; Face < 4; ++Face)
			{
				const bool bLong = Face % 2 == 0;
				const FVector Outward = Face == 0 ? -Ay : (Face == 1 ? Ax : (Face == 2 ? Ay : -Ax));
				const FVector Along = Face == 0 ? Ax : (Face == 1 ? Ay : (Face == 2 ? -Ax : -Ay));
				const float Width = static_cast<float>(bLong ? Size.X : Size.Y);
				const float Depth = static_cast<float>(bLong ? Size.Y : Size.X);
				const FVector Corner = Base + Outward * (Depth * 0.5f + 4.f) - Along * Width * 0.5f;
				const int32 Bays = FMath::FloorToInt(Width / 300.f);
				for (int32 Bay = 0; Bay < Bays; ++Bay)
				{
					for (int32 Floor = 0; Floor < Floors; ++Floor)
					{
						if (static_cast<float>(Rand.Unit()) > LitShare)
						{
							continue;
						}
						const float U0 = Bay * 300.f + (bBrick ? 94.f : 84.f);
						const float U1 = Bay * 300.f + (bBrick ? 206.f : 216.f);
						const float Z1 = H - Floor * 280.f - (bBrick ? 56.f : 61.f);
						const float Z0 = H - Floor * 280.f - (bBrick ? 206.f : 184.f);
						Windows.AddQuad(Corner + Along * U0 + FVector(0, 0, Z0), Corner + Along * U1 + FVector(0, 0, Z0), Corner + Along * U1 + FVector(0, 0, Z1),
							Corner + Along * U0 + FVector(0, 0, Z1), Outward, FVector2D(0, 1), FVector2D(1, 0), FColor::White, 0);
					}
				}
			}
			break;
		}
		case rgen::BuildingKind::Garages:
		{
			// Ряд кирпичных боксов 3.5 × 7 м с воротами разного цвета и общей плоской крышей.
			const FRotator Rot(0.f, Yaw, 0.f);
			const FVector Ax = Rot.RotateVector(FVector::ForwardVector);
			const FVector Ay = Rot.RotateVector(FVector::RightVector);
			GarageBlocks.AddBox(Base, Yaw, Size, FVector2D(300.f, 280.f), ToColor(FLinearColor(0.85f, 0.78f, 0.7f)), false, false);
			Roofs.AddBox(Base + FVector(0.f, 0.f, H), Yaw, FVector(Size.X + 40.f, Size.Y + 60.f, 20.f), FVector2D(400.f, 400.f), FColor::White, true, true);
			static const FLinearColor Doors[] = {FLinearColor(0.25f, 0.4f, 0.25f), FLinearColor(0.45f, 0.28f, 0.15f), FLinearColor(0.2f, 0.3f, 0.5f),
				FLinearColor(0.5f, 0.5f, 0.5f), FLinearColor(0.55f, 0.18f, 0.12f)};
			const int32 Boxes = FMath::Max(1, FMath::FloorToInt(static_cast<float>(Size.X) / 350.f));
			const FVector Front = Base - Ay * (Size.Y * 0.5 + 2.0);
			for (int32 K = 0; K < Boxes; ++K)
			{
				const FVector C = Front + Ax * (-Size.X * 0.5 + 175.0 + K * 350.0);
				const FColor Tint = ToColor(Doors[(B.Seed + K * 7) % UE_ARRAY_COUNT(Doors)]);
				Props.AddQuad(C - Ax * 130.f, C + Ax * 130.f, C + Ax * 130.f + FVector(0, 0, 220.f), C - Ax * 130.f + FVector(0, 0, 220.f), -Ay, FVector2D(0, 1),
					FVector2D(1, 0), Tint, 0);
			}
			break;
		}
		case rgen::BuildingKind::Shop:
		{
			// Магазинчик: оштукатуренный короб, витрина (вечером горит), вывеска.
			const FRotator Rot(0.f, Yaw, 0.f);
			const FVector Ay = Rot.RotateVector(FVector::RightVector);
			const FVector Ax = Rot.RotateVector(FVector::ForwardVector);
			static const FLinearColor Walls[] = {FLinearColor(0.9f, 0.85f, 0.7f), FLinearColor(0.8f, 0.85f, 0.9f), FLinearColor(0.85f, 0.9f, 0.8f)};
			ShopWalls.AddBox(Base, Yaw, Size, FVector2D(300.f, 280.f), ToColor(Walls[B.Seed % 3]), false, false);
			Roofs.AddBox(Base + FVector(0.f, 0.f, H), Yaw, FVector(Size.X + 30.f, Size.Y + 30.f, 25.f), FVector2D(400.f, 400.f), FColor::White, true, false);
			const FVector Face = Base - Ay * (Size.Y * 0.5 + 3.0);
			Windows.AddQuad(Face - Ax * (Size.X * 0.35) + FVector(0, 0, 80.f), Face + Ax * (Size.X * 0.35) + FVector(0, 0, 80.f),
				Face + Ax * (Size.X * 0.35) + FVector(0, 0, 230.f), Face - Ax * (Size.X * 0.35) + FVector(0, 0, 230.f), -Ay, FVector2D(0, 1), FVector2D(1, 0),
				FColor::White, 0);
			static const TCHAR* Signs[] = {TEXT("ПРОДУКТЫ"), TEXT("АПТЕКА"), TEXT("ХОЗТОВАРЫ")};
			static const FLinearColor Paper[] = {FLinearColor(0.6f, 0.08f, 0.06f), FLinearColor(0.1f, 0.45f, 0.25f), FLinearColor(0.12f, 0.25f, 0.5f)};
			const int32 Sign = FMath::Clamp(B.Floors, 0, 2);
			Label(FTransform((-Ay).Rotation(), Face - Ay * 2.0 + FVector(0, 0, 290.f)), Signs[Sign], static_cast<float>(Size.X) * 0.6f, 70.f, 64,
				FLinearColor(0.95f, 0.95f, 0.9f), Paper[Sign]);
			break;
		}
		case rgen::BuildingKind::Playground:
		{
			// Детская площадка: качели, горка-лесенка, песочница, лавки — всё в одном меше с цветом вершин.
			const FRotator Rot(0.f, Yaw, 0.f);
			auto At = [&](float Lx, float Ly, float Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz)); };
			const FColor Red = ToColor(FLinearColor(0.8f, 0.15f, 0.1f));
			const FColor Yellow = ToColor(FLinearColor(0.9f, 0.75f, 0.15f));
			const FColor Blue = ToColor(FLinearColor(0.15f, 0.35f, 0.7f));
			const FColor Brown = ToColor(FLinearColor(0.45f, 0.32f, 0.2f));
			const FColor Sand = ToColor(FLinearColor(0.85f, 0.75f, 0.5f));
			// Качели: две стойки и перекладина, сиденье.
			for (float Y : {-120.f, 120.f})
			{
				Props.AddBox(At(-500.f, Y, 0.f), Yaw, FVector(10.f, 10.f, 250.f), FVector2D(100.f, 100.f), Blue, false, false);
			}
			Props.AddBox(At(-500.f, 0.f, 245.f), Yaw, FVector(10.f, 260.f, 10.f), FVector2D(100.f, 100.f), Blue, true, true);
			Props.AddBox(At(-500.f, 0.f, 50.f), Yaw, FVector(35.f, 50.f, 5.f), FVector2D(100.f, 100.f), Yellow, true, true);
			// Горка: лесенка-башенка и скат ступеньками.
			Props.AddBox(At(200.f, -300.f, 0.f), Yaw, FVector(120.f, 120.f, 150.f), FVector2D(100.f, 100.f), Red, true, false);
			for (int32 Step = 0; Step < 5; ++Step)
			{
				Props.AddBox(At(200.f, -180.f + Step * 50.f, 0.f), Yaw, FVector(70.f, 50.f, 140.f - Step * 28.f), FVector2D(100.f, 100.f), Yellow, true, false);
			}
			// Песочница и «грибок».
			Props.AddBox(At(400.f, 350.f, 0.f), Yaw, FVector(300.f, 300.f, 25.f), FVector2D(100.f, 100.f), Brown, false, false);
			Props.AddBox(At(400.f, 350.f, 0.f), Yaw, FVector(280.f, 280.f, 18.f), FVector2D(100.f, 100.f), Sand, true, false);
			Props.AddBox(At(400.f, 350.f, 0.f), Yaw, FVector(10.f, 10.f, 200.f), FVector2D(100.f, 100.f), Brown, false, false);
			Props.AddBox(At(400.f, 350.f, 200.f), Yaw, FVector(220.f, 220.f, 15.f), FVector2D(100.f, 100.f), Red, true, true);
			// Лавки по краю и шины-клумбы.
			for (float X : {-800.f, 800.f})
			{
				Props.AddBox(At(X, 700.f, 40.f), Yaw, FVector(170.f, 40.f, 6.f), FVector2D(100.f, 100.f), Brown, true, true);
				Wheels.AddBox(At(X, -700.f, 0.f), Yaw, FVector(70.f, 70.f, 25.f), FVector2D(100.f, 100.f), FColor::White, true, false);
			}
			break;
		}
		case rgen::BuildingKind::Industrial:
			Plant.AddBox(Base, Yaw, Size, FVector2D(400.f, 400.f), ToColor(FLinearColor(0.75f, 0.73f, 0.7f)), false, false);
			PlantRoof.AddBox(Base + FVector(0.f, 0.f, H), Yaw, FVector(Size.X, Size.Y, 60.f), FVector2D(400.f, 400.f), FColor::White, true, false);
			break;
		case rgen::BuildingKind::Chimney:
		case rgen::BuildingKind::Mast:
		{
			// Красно-белые кольца, к верху тоньше.
			Stripes();
			const bool bMast = B.Kind == rgen::BuildingKind::Mast;
			constexpr int32 Bands = 8;
			const float R0 = bMast ? 1.6f : 3.4f;
			const float R1 = bMast ? 0.5f : 2.2f;
			for (int32 K = 0; K < Bands; ++K)
			{
				const float T = (K + 0.5f) / Bands;
				const float Radius = FMath::Lerp(R0, R1, T);
				const float BandH = H / Bands;
				const FTransform Xf(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, BandH * (K + 0.5f)), FVector(Radius * 2.f, Radius * 2.f, BandH / 100.f));
				(K % 2 == 0 ? RedBands : WhiteBands)->AddInstance(Xf, false);
			}
			if (bMast)
			{
				for (float Level : {0.45f, 0.75f})
				{
					WhiteBands->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, H * Level), FVector(8.f, 8.f, 0.8f)), false);
				}
			}
			break;
		}
		case rgen::BuildingKind::Church:
		{
			// Белый храм: четверик, барабан, пять золотых глав, колокольня с шатром.
			const FRotator Rot(0.f, Yaw, 0.f);
			auto At = [&](float Lx, float Ly, float Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.f); };
			AddMesh(Cube(), FTransform(Rot, At(0, 0, 5.5f), FVector(16.f, 14.f, 11.f)), Mats.Stele, true);
			AddMesh(Cylinder(), FTransform(Rot, At(0, 0, 13.f), FVector(6.f, 6.f, 4.f)), Mats.Stele, false);
			AddMesh(Sphere(), FTransform(Rot, At(0, 0, 16.5f), FVector(6.6f, 6.6f, 7.5f)), Mats.Gold, false);
			AddMesh(Cone(), FTransform(Rot, At(0, 0, 21.f), FVector(1.2f, 1.2f, 3.f)), Mats.Gold, false);
			for (float Sx : {-5.f, 5.f})
			{
				for (float Sy : {-4.5f, 4.5f})
				{
					AddMesh(Cylinder(), FTransform(Rot, At(Sx, Sy, 12.f), FVector(2.6f, 2.6f, 2.f)), Mats.Stele, false);
					AddMesh(Sphere(), FTransform(Rot, At(Sx, Sy, 14.f), FVector(3.f, 3.f, 3.6f)), Mats.Gold, false);
					AddMesh(Cone(), FTransform(Rot, At(Sx, Sy, 16.4f), FVector(0.6f, 0.6f, 1.6f)), Mats.Gold, false);
				}
			}
			AddMesh(Cube(), FTransform(Rot, At(-12.f, 0, 10.f), FVector(6.f, 6.f, 20.f)), Mats.Stele, true);
			AddMesh(Cone(), FTransform(Rot, At(-12.f, 0, 23.5f), FVector(5.f, 5.f, 7.f)), Mats.Gold, false);
			break;
		}
		default:
			break;
		}
	}
	AddProc(PanelWalls, Mats.Panel, true);
	AddProc(BrickWalls, Mats.Brick, true);
	AddProc(Roofs, Mats.Roof, false);
	AddProc(Windows, Mats.LitWindow, false, false);
	AddProc(Plant, Mats.Concrete, true);
	AddProc(PlantRoof, Mats.MetalRoof, false);
	AddProc(GarageBlocks, Mats.Concrete, true);
	AddProc(ShopWalls, Mats.Concrete, true);
	AddProc(Props, Mats.Paint, true);
	AddProc(Wheels, Mats.Pole, false);
}

UInstancedStaticMeshComponent* FPeregonKit::Instanced(UStaticMesh* InMesh, UMaterialInterface* Material, bool bCollision, bool bShadow)
{
	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(Root);
	Comp->SetStaticMesh(InMesh);
	Comp->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Comp->SetCastShadow(bShadow);
	Comp->SetCanEverAffectNavigation(false);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->RegisterComponent();
	Out.Add(Comp);
	return Comp;
}

void FPeregonKit::Yard(const rgen::Building& B, FPeregonMeshBuffers& Props, FPeregonMeshBuffers& Wheels)
{
	using namespace PeregonKitDetail;
	constexpr double M = 100.0;
	rgen::Rng Rand(B.Seed ^ 0x9A3Du);
	const FVector Base = FVector(B.X, B.Y, B.Z) * M;
	const float Yaw = FMath::RadiansToDegrees(static_cast<float>(B.Yaw));
	const FRotator Rot(0.f, Yaw, 0.f);
	const FVector Ax = Rot.RotateVector(FVector::ForwardVector);
	const FVector Ay = Rot.RotateVector(FVector::RightVector);
	const float Half = static_cast<float>(B.Length * M * 0.5);
	const float Depth = static_cast<float>(B.Width * M * 0.5);
	// Дворовый проезд вдоль подъездов и машины на нём.
	const FVector Lane = Base - Ay * (Depth + 650.f);
	Props.AddBox(Lane + FVector(0, 0, -40.f), Yaw, FVector(Half * 2.f + 600.f, 600.f, 48.f), FVector2D(400.f, 400.f), ToColor(FLinearColor(0.28f, 0.28f, 0.3f)), true,
		false);
	static const FLinearColor CarColors[] = {FLinearColor(0.75f, 0.1f, 0.08f), FLinearColor(0.85f, 0.85f, 0.8f), FLinearColor(0.15f, 0.25f, 0.5f),
		FLinearColor(0.3f, 0.45f, 0.3f), FLinearColor(0.8f, 0.65f, 0.2f), FLinearColor(0.2f, 0.2f, 0.22f), FLinearColor(0.55f, 0.6f, 0.62f)};
	for (float X = -Half + 300.f; X < Half - 300.f; X += 520.f)
	{
		if (Rand.Chance(0.45))
		{
			continue;
		}
		const FVector C = Lane + Ax * X - Ay * 180.f + FVector(0, 0, 8.f);
		const FColor Paint = ToColor(CarColors[Rand.Next() % UE_ARRAY_COUNT(CarColors)]);
		// Кузов, кабина, колёса: условная «легковушка».
		Props.AddBox(C + FVector(0, 0, 30.f), Yaw + 90.f, FVector(420.f, 165.f, 60.f), FVector2D(100.f, 100.f), Paint, true, false);
		Props.AddBox(C + FVector(0, 0, 90.f) - Ay * 20.f, Yaw + 90.f, FVector(230.f, 150.f, 50.f), FVector2D(100.f, 100.f), ToColor(FLinearColor(0.25f, 0.3f, 0.33f)),
			true, false);
		for (float Wx : {-140.f, 140.f})
		{
			for (float Wy : {-80.f, 80.f})
			{
				Wheels.AddBox(C + Ay * Wx + Ax * Wy, Yaw, FVector(25.f, 60.f, 60.f), FVector2D(100.f, 100.f), FColor::White, true, false);
			}
		}
	}
	// Лавочки у подъездов, мусорные баки в конце дома.
	for (float X = -Half + 1500.f; X < Half - 600.f; X += 1500.f)
	{
		const FVector P = Base - Ay * (Depth + 180.f) + Ax * (X + 250.f);
		Props.AddBox(P + FVector(0, 0, 40.f), Yaw, FVector(160.f, 40.f, 6.f), FVector2D(100.f, 100.f), ToColor(FLinearColor(0.45f, 0.3f, 0.2f)), true, true);
	}
	for (int32 K = 0; K < 3; ++K)
	{
		Props.AddBox(Base + Ax * (Half + 300.f) - Ay * (Depth - 200.f - K * 140.f), Yaw, FVector(120.f, 100.f, 110.f), FVector2D(100.f, 100.f),
			ToColor(FLinearColor(0.2f, 0.4f, 0.25f)), true, false);
	}
	// Берёзы и тополя с обратной стороны дома.
	if (!YardTrunks)
	{
		YardTrunks = Instanced(Cylinder(), Mats.BirchBark, true);
		YardCrowns = Instanced(Sphere(), Mats.Leaves, false);
	}
	for (float X = -Half; X < Half; X += 900.f)
	{
		if (Rand.Chance(0.4))
		{
			const float Scale = static_cast<float>(Rand.Range(0.8, 1.25));
			const FVector Foot = Base + Ay * (Depth + 700.f + static_cast<float>(Rand.Range(0.0, 600.0))) + Ax * X;
			YardTrunks->AddInstance(FTransform(FRotator::ZeroRotator, Foot + FVector(0, 0, 500.f * Scale), FVector(0.25f * Scale, 0.25f * Scale, 10.f * Scale)), false);
			YardCrowns->AddInstance(FTransform(FRotator::ZeroRotator, Foot + FVector(0, 0, 800.f * Scale), FVector(4.2f * Scale, 4.2f * Scale, 6.f * Scale)), false);
		}
	}
}

void FPeregonKit::Streets(const std::vector<rgen::Street>& List)
{
	FPeregonMeshBuffers Road;
	UInstancedStaticMeshComponent* Poles = Instanced(Cylinder(), Mats.Pole, false);
	UInstancedStaticMeshComponent* Heads = Instanced(Cube(), Mats.Lamp, false, false);
	int32 Index = 0;
	for (const rgen::Street& St : List)
	{
		const FVector A = FVector(St.X0, St.Y0, St.Z0 + 0.05) * 100.0;
		const FVector B = FVector(St.X1, St.Y1, St.Z1 + 0.05) * 100.0;
		const FVector Dir = (B - A).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir) * 350.f;
		const float Len = static_cast<float>(FVector::Dist2D(A, B) / 400.0);
		// Чуть длиннее отрезка — перекрёстки без щелей; бортики вниз прячут ступеньки на склонах.
		const FVector A2 = A - Dir * 350.f;
		const FVector B2 = B + Dir * 350.f;
		Road.AddQuad(A2 - Side, B2 - Side, B2 + Side, A2 + Side, FVector::UpVector, FVector2D(0, 0), FVector2D(Len, 1.75f), FColor::White, 0);
		for (const FVector& S : {Side, -Side})
		{
			Road.AddQuad(A2 + S - FVector(0, 0, 60.f), B2 + S - FVector(0, 0, 60.f), B2 + S, A2 + S, S, FVector2D(0, 0.15f), FVector2D(Len, 0), FColor::White, 0);
		}
		// Фонарь на каждом третьем отрезке.
		if (++Index % 3 == 0)
		{
			const FVector Foot = (A + B) * 0.5 + Side * 1.15f;
			Poles->AddInstance(FTransform(FRotator::ZeroRotator, Foot + FVector(0, 0, 350), FVector(0.14, 0.14, 7.0)), false);
			Heads->AddInstance(FTransform(Dir.Rotation(), Foot + FVector(0, 0, 700) - Side.GetSafeNormal() * 60.0, FVector(0.5, 0.25, 0.1)), false);
		}
	}
	AddProc(Road, Mats.Asphalt ? Mats.Asphalt : Mats.Concrete, true, false);
}

void FPeregonKit::Garden(const FVector& Base, const FRotator& Rot)
{
	// Огород: грядки (тёмные земляные валики) рядами и пугало-кол.
	for (int32 Row = 0; Row < 6; ++Row)
	{
		Box(Base + Rot.RotateVector(FVector(0.f, -500.f + Row * 200.f, 10.f)), Rot, FVector(9.f, 0.9f, 0.25f), Mats.Dirt, false);
	}
	Box(Base + Rot.RotateVector(FVector(500.f, 600.f, 80.f)), Rot, FVector(0.06f, 0.06f, 1.6f), Mats.DarkWood, false);
}

void FPeregonKit::Greenhouse(const FVector& Base, const FRotator& Rot)
{
	// Теплица под плёнкой: каркас и полупрозрачные скаты.
	UPeregonArtSubsystem* Art = Owner ? UPeregonArtSubsystem::Get(Owner) : nullptr;
	UMaterialInterface* Film = Art ? Art->MakeGlassMaterial(Owner, FLinearColor(0.85f, 0.9f, 0.88f), 0.45f) : Mats.Glass;
	for (float Y : {-140.f, 140.f})
	{
		Box(Base + Rot.RotateVector(FVector(0.f, Y * 0.55f, 120.f)), FRotator(0.0, Rot.Yaw, Y > 0.f ? 32.0 : -32.0), FVector(5.f, 1.7f, 0.02f), Film, false);
		Box(Base + Rot.RotateVector(FVector(0.f, Y, 55.f)), Rot, FVector(5.f, 0.02f, 1.1f), Film, false);
	}
	Box(Base + Rot.RotateVector(FVector(0.f, 0.f, 205.f)), Rot, FVector(5.1f, 0.08f, 0.08f), Mats.DarkWood, false);
}

void FPeregonKit::Haystack(const FVector& Base)
{
	AddMesh(Sphere(), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, 120.f), FVector(3.2f, 3.2f, 3.4f)), Mats.Dirt ? Mats.Dirt : Mats.Wood, true);
	AddMesh(Cylinder(), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, 300.f), FVector(0.08f, 0.08f, 1.5f)), Mats.DarkWood, false);
}

void FPeregonKit::DirtRoad(const TArray<FVector>& Points, float WidthCm)
{
	FPeregonMeshBuffers Data;
	for (int32 K = 0; K + 1 < Points.Num(); ++K)
	{
		const FVector A = Points[K];
		const FVector B = Points[K + 1];
		const FVector Side = FVector::CrossProduct(FVector::UpVector, (B - A).GetSafeNormal2D()) * (WidthCm * 0.5f);
		Data.AddQuad(A - Side, B - Side, B + Side, A + Side, FVector::UpVector, FVector2D(0, K * 0.5f), FVector2D(1, (K + 1) * 0.5f), FColor::White, 0);
	}
	AddProc(Data, Mats.Dirt, false, false);
}

void FPeregonKit::StreetLamp(const FVector& Foot, const FVector& Toward)
{
	const FVector Dir = Toward.GetSafeNormal2D();
	AddMesh(Cylinder(), FTransform(FRotator::ZeroRotator, Foot + FVector(0, 0, 450), FVector(0.16, 0.16, 9.0)), Mats.Pole, false);
	AddMesh(Cylinder(), FTransform(FRotationMatrix::MakeFromZ(Dir).Rotator(), Foot + FVector(0, 0, 890) + Dir * 90.0, FVector(0.1, 0.1, 1.8)), Mats.Pole, false);
	if (UStaticMeshComponent* Shade = AddMesh(Cube(), FTransform(Dir.Rotation(), Foot + FVector(0, 0, 880) + Dir * 175.0, FVector(0.6, 0.3, 0.12)), Mats.Lamp, false))
	{
		Shade->SetCastShadow(false);
	}
}

// ---------------------------------------------------------------------------------------------
// Стелы
// ---------------------------------------------------------------------------------------------
void FPeregonKit::CityStele(const FVector& Base, const FRotator& Rot, const FString& Name, int32 Size, const FString& Year, bool bLeaving)
{
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	const FRotator Back(0.0, Rot.Yaw + 180.0, 0.0);
	auto Write = [&](const FString& Str, double Lx, double Ly, double Lz, float WidthCm, float HeightCm, int32 FontPx, const FLinearColor& Ink,
					const FLinearColor& Paper, bool bBack) {
		Label(FTransform(bBack ? Back : Rot, At(bBack ? -Lx : Lx, bBack ? -Ly : Ly, Lz)), Str, WidthCm, HeightCm, FontPx, Ink, Paper);
	};
	const FLinearColor WhiteInk(0.92f, 0.92f, 0.9f);
	const FLinearColor Blue(0.08f, 0.22f, 0.5f);
	const FLinearColor Dark(0.05f, 0.05f, 0.06f);
	const FLinearColor Light(0.9f, 0.9f, 0.87f);
	if (Size >= 2)
	{
		// Областной центр: широкий цоколь, два пилона и ригель с названием, год — на цоколе.
		Box(At(0, 0, 0.4), Rot, FVector(1.6, 15.0, 0.8), Mats.Stele, true);
		for (double Y : {-5.6, 5.6})
		{
			Box(At(0, Y, 4.8), Rot, FVector(1.3, 1.3, 8.8), Mats.Stele, true);
		}
		Box(At(0, 0, 7.6), Rot, FVector(1.0, 12.6, 2.4), Mats.Accent, false);
		Write(Name.ToUpper(), 0.52, 0, 7.6, 1080.f, 190.f, 96, WhiteInk, Blue, false);
		Write(bLeaving ? FString(TEXT("Счастливого пути!")) : Year, 0.82, 0, 0.4, bLeaving ? 900.f : 360.f, 60.f, 44, Dark, Light, false);
		Write(TEXT("Счастливого пути!"), 0.52, 0, 7.6, 1080.f, 190.f, 80, WhiteInk, Blue, true);
	}
	else if (Size == 1)
	{
		// Город: высокая плита с цветной вставкой, название крупно, год под ним.
		Box(At(0, 0, 0.3), Rot, FVector(2.4, 6.0, 0.6), Mats.Stele, true);
		Box(At(0, 0, 4.0), Rot, FVector(0.9, 3.4, 7.4), Mats.Stele, true);
		Box(At(0.05, 1.25, 4.0), Rot, FVector(0.9, 0.5, 7.4), Mats.Accent, false);
		Write(Name.ToUpper(), 0.47, -0.25, 6.0, 280.f, 90.f, 72, Dark, Light, false);
		if (!Year.IsEmpty())
		{
			Write(Year, 0.47, -0.25, 4.6, 200.f, 60.f, 48, Dark, Light, false);
		}
	}
	else
	{
		// Посёлок: кирпичная стенка с табличкой.
		Box(At(0, 0, 1.1), Rot, FVector(0.7, 7.0, 2.2), Mats.Red, true);
		Box(At(0, 0, 2.3), Rot, FVector(0.9, 7.3, 0.25), Mats.Stele, false);
		Write(Name.ToUpper(), 0.37, 0, 1.35, 600.f, 110.f, 72, Dark, Light, false);
		if (!Year.IsEmpty())
		{
			Write(Year, 0.37, 0, 0.55, 160.f, 40.f, 40, Dark, Light, false);
		}
	}
	// Клумба перед стелой.
	Box(At(2.4, 0, 0.15), Rot, FVector(1.6, Size >= 2 ? 10.0 : 5.0, 0.3), Mats.Concrete, false);
}

void FPeregonKit::RegionStele(const FVector& Base, const FRotator& Rot, const FString& Before, const FString& After)
{
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	const double Yaw = Rot.Yaw;
	// Цоколь, две наклонные плиты-«крыла», между ними — щит с названием области; с обратной стороны — прежняя область.
	Box(At(0, 0, 0.35), Rot, FVector(2.4, 13.0, 0.7), Mats.Stele, true);
	Box(At(0, -5.2, 3.6), FRotator(0.0, Yaw, 8.0), FVector(0.9, 1.6, 6.6), Mats.Stele, true);
	Box(At(0, 5.2, 3.6), FRotator(0.0, Yaw, -8.0), FVector(0.9, 1.6, 6.6), Mats.Stele, true);
	Box(At(0, 0, 4.2), Rot, FVector(0.6, 8.2, 3.4), Mats.Accent, true);
	Box(At(0, 0, 6.2), Rot, FVector(0.65, 8.2, 0.35), Mats.White, false);
	Box(At(0, 0, 6.55), Rot, FVector(0.65, 8.2, 0.35), Mats.Red, false);
	const FLinearColor Ink(0.93f, 0.93f, 0.9f);
	const FLinearColor Paper(0.08f, 0.22f, 0.5f);
	Label(FTransform(Rot, At(0.32, 0, 4.2)), After.Replace(TEXT(" — "), TEXT("\n")), 780.f, 300.f, 64, Ink, Paper);
	if (!Before.IsEmpty())
	{
		Label(FTransform(FRotator(0.0, Yaw + 180.0, 0.0), At(-0.32, 0, 4.2)), Before.Replace(TEXT(" — "), TEXT("\n")), 780.f, 300.f, 64, Ink, Paper);
	}
}

// ---------------------------------------------------------------------------------------------
// Остановка и двор
// ---------------------------------------------------------------------------------------------
void FPeregonKit::BusStop(const FVector& Base, const FRotator& Rot, const FString& Name)
{
	// Бетонный павильон: задняя стенка с «мозаикой», боковины, плоская крыша-козырёк, лавка внутри.
	// Лицом (+X) к дороге.
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	Box(At(0, 0, 0.08), Rot, FVector(2.6, 5.4, 0.16), Mats.Concrete, true);              // платформа
	Box(At(-0.9, 0, 1.35), Rot, FVector(0.18, 4.6, 2.5), Mats.Stele, true);               // задняя стенка
	Box(At(-0.8, 0, 1.4), Rot, FVector(0.04, 3.6, 1.2), Mats.Accent, false);              // мозаика
	Box(At(-0.78, -0.9, 1.4), Rot, FVector(0.04, 1.0, 0.7), Mats.Red, false);
	Box(At(-0.78, 1.0, 1.7), Rot, FVector(0.04, 0.8, 0.5), Mats.White, false);
	for (double Y : {-2.25, 2.25})
	{
		Box(At(-0.25, Y, 1.35), Rot, FVector(1.3, 0.14, 2.5), Mats.Stele, true);          // боковины
	}
	Box(At(-0.1, 0, 2.68), Rot, FVector(2.2, 5.0, 0.16), Mats.Stele, false);              // крыша
	Box(At(-0.55, 0, 0.45), Rot, FVector(0.4, 3.6, 0.06), Mats.DarkWood, true);           // лавка
	for (double Y : {-1.5, 1.5})
	{
		Box(At(-0.55, Y, 0.22), Rot, FVector(0.3, 0.08, 0.44), Mats.Concrete, false);
	}
	// Знак «А» на столбе у края и табличка с названием.
	Box(At(0.9, 2.9, 1.3), Rot, FVector(0.08, 0.08, 2.6), Mats.Pole, false);
	Label(FTransform(Rot, At(0.95, 2.9, 2.3)), TEXT("А"), 50.f, 50.f, 90, FLinearColor(0.05f, 0.05f, 0.08f), FLinearColor(0.95f, 0.85f, 0.15f));
	Label(FTransform(Rot, At(0.95, 2.9, 1.85)), Name, 120.f, 30.f, 40, FLinearColor(0.05f, 0.05f, 0.06f), FLinearColor(0.9f, 0.9f, 0.87f));
	// Урна.
	AddMesh(Cylinder(), FTransform(Rot, At(0.5, -2.6, 0.35), FVector(0.4, 0.4, 0.55)), Mats.Concrete, true);
}

void FPeregonKit::Well(const FVector& Base, const FRotator& Rot)
{
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	// Сруб и двускатный домик с воротом.
	Box(At(0, 0, 0.4), Rot, FVector(1.1, 1.1, 0.8), Mats.DarkWood, true);
	for (double Y : {-0.5, 0.5})
	{
		Box(At(0, Y, 1.25), Rot, FVector(0.1, 0.1, 1.0), Mats.DarkWood, false);
	}
	AddMesh(Cylinder(), FTransform(FRotator(Rot.Pitch, Rot.Yaw, 90.0), At(0, 0, 1.25), FVector(0.18, 0.18, 1.0)), Mats.Wood, false);
	Box(At(0.25, 0, 1.85), FRotator(-35.0, Rot.Yaw, 0.0), FVector(0.65, 1.3, 0.05), Mats.MetalRoof, false);
	Box(At(-0.25, 0, 1.85), FRotator(35.0, Rot.Yaw, 0.0), FVector(0.65, 1.3, 0.05), Mats.MetalRoof, false);
}

void FPeregonKit::Bench(const FVector& Base, const FRotator& Rot)
{
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	Box(At(0, 0, 0.45), Rot, FVector(0.4, 1.8, 0.05), Mats.Wood, true);
	Box(At(-0.2, 0, 0.75), Rot, FVector(0.05, 1.8, 0.3), Mats.Wood, false);
	for (double Y : {-0.75, 0.75})
	{
		Box(At(0, Y, 0.22), Rot, FVector(0.35, 0.06, 0.44), Mats.DarkWood, false);
	}
}

void FPeregonKit::Woodpile(const FVector& Base, const FRotator& Rot)
{
	// Поленница: ряды брёвнышек под навесом.
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 K = 0; K < 10; ++K)
		{
			AddMesh(Cylinder(), FTransform(FRotator(90.0, Rot.Yaw, 0.0), At(0, -1.0 + K * 0.22 + (Row % 2) * 0.11, 0.12 + Row * 0.21),
				FVector(0.2, 0.2, 0.6)), Mats.Wood, false);
		}
	}
	Box(At(0, 0, 1.05), Rot, FVector(0.9, 2.6, 0.05), Mats.MetalRoof, false);
}

void FPeregonKit::Shed(const FVector& Base, const FRotator& Rot)
{
	auto At = [&](double Lx, double Ly, double Lz) { return Base + Rot.RotateVector(FVector(Lx, Ly, Lz) * 100.0); };
	Box(At(0, 0, 1.1), Rot, FVector(2.6, 3.2, 2.2), Mats.DarkWood, true);
	Box(At(0, 0, 2.3), FRotator(0.0, Rot.Yaw, 6.0), FVector(3.0, 3.6, 0.08), Mats.MetalRoof, false);
	Box(At(1.31, 0.6, 0.95), Rot, FVector(0.04, 0.9, 1.9), Mats.Wood, false);
}

void FPeregonKit::Fence(const FVector& A, const FVector& B)
{
	const FVector Dir = B - A;
	const float Len = static_cast<float>(Dir.Size());
	if (Len < 10.f)
	{
		return;
	}
	const FRotator Rot = Dir.Rotation();
	FPeregonMeshBuffers Data;
	// Штакетины через 12 см и две поперечины.
	const FVector Side = FVector::CrossProduct(Dir / Len, FVector::UpVector) * 3.f;
	for (float D = 0.f; D < Len; D += 12.f)
	{
		const FVector P = A + Dir * (D / Len);
		const float Top = 110.f + 8.f * FMath::Sin(D * 0.7f);
		Data.AddQuad(P, P + Dir / Len * 8.f, P + Dir / Len * 8.f + FVector(0, 0, Top), P + FVector(0, 0, Top), FVector::CrossProduct(Dir, FVector::UpVector),
			FVector2D(0, 1), FVector2D(0.25f, 0), FColor::White, 0);
	}
	AddProc(Data, Mats.Wood, false);
	for (float H : {35.f, 90.f})
	{
		Box((A + B) * 0.5 + FVector(0, 0, H) - Side, Rot, FVector(Len / 100.f, 0.04f, 0.1f), Mats.DarkWood, false);
	}
}

void FPeregonKit::Tree(const FVector& Base, bool bPine, float Scale)
{
	if (bPine)
	{
		const float H = 1400.f * Scale;
		AddMesh(Cylinder(), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, H * 0.5f), FVector(0.35f * Scale, 0.35f * Scale, H / 100.f)), Mats.PineBark, true);
		AddMesh(Cone(), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, H * 0.62f), FVector(4.2f * Scale, 4.2f * Scale, H * 0.8f / 100.f)), Mats.Needles, false);
	}
	else
	{
		const float H = 1000.f * Scale;
		AddMesh(Cylinder(), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, H * 0.5f), FVector(0.25f * Scale, 0.25f * Scale, H / 100.f)), Mats.BirchBark, true);
		AddMesh(Sphere(), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, H * 0.78f), FVector(4.2f * Scale, 4.2f * Scale, 6.f * Scale)), Mats.Leaves, false);
	}
}

void FPeregonKit::GrassPatch(const FVector& Center, float RadiusCm, int32 Count, uint32 Seed)
{
	rgen::Rng Rand(Seed);
	FPeregonMeshBuffers Grass;
	FPeregonMeshBuffers Bushes;
	for (int32 K = 0; K < Count; ++K)
	{
		const float A = static_cast<float>(Rand.Range(0.0, 2.0 * PI));
		const float R = RadiusCm * FMath::Sqrt(static_cast<float>(Rand.Unit()));
		const FVector P = Center + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.f);
		const uint8 Shade = static_cast<uint8>(Rand.Range(190.0, 255.0));
		if (K % 25 == 0)
		{
			Bushes.AddClump(P, 150.f, 220.f, 3, A, FColor(Shade, Shade, Shade, 255));
		}
		else
		{
			Grass.AddClump(P, static_cast<float>(Rand.Range(30.0, 60.0)), static_cast<float>(Rand.Range(35.0, 80.0)), 3, A, FColor(Shade, Shade, Shade, 255));
		}
	}
	AddProc(Grass, Mats.GrassTuft, false, false);
	AddProc(Bushes, Mats.Bush, false, true);
}
