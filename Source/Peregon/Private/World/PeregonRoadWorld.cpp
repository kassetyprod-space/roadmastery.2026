#include "World/PeregonRoadWorld.h"

#include "Algo/Reverse.h"

#include "Art/PeregonArtSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/PeregonGameMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProceduralMeshComponent.h"
#include "rgen/Noise.h"
#include "World/PeregonServicePoint.h"

namespace PeregonRoad
{
constexpr double M = 100.0; // м → см

// Совпадает с Config/DefaultEngine.ini → PhysicalSurfaces.
constexpr EPhysicalSurface AsphaltSurface = SurfaceType1;
constexpr EPhysicalSurface GravelSurface = SurfaceType2;
constexpr EPhysicalSurface GrassSurface = SurfaceType7;

// Поперечные отметки рельефа (м от оси): густо у дороги, редко вдали.
const double TerrainT[] = {5.5, 7.0, 9.0, 11.0, 14.0, 20.0, 28.0, 40.0, 55.0, 75.0, 100.0, 135.0, 180.0, 230.0, 285.0};

// Цвета домов: выцветшая краска сибирской деревни.
const FLinearColor HouseColors[] = {
	FLinearColor(0.20f, 0.33f, 0.55f), FLinearColor(0.25f, 0.45f, 0.30f), FLinearColor(0.70f, 0.55f, 0.25f),
	FLinearColor(0.45f, 0.30f, 0.20f), FLinearColor(0.75f, 0.72f, 0.62f), FLinearColor(0.42f, 0.55f, 0.58f)};

FString PoiName(const rgen::Poi& Poi)
{
	return FString(UTF8_TO_TCHAR(Poi.Name.c_str()));
}
} // namespace PeregonRoad

APeregonRoadWorld::APeregonRoadWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);

	FarGround = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FarGround"));
	FarGround->SetupAttachment(RootComponent);
	FarGround->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FarGround->SetMobility(EComponentMobility::Movable);
	FarGround->SetRelativeScale3D(FVector(400.f, 400.f, 0.1f)); // 40 × 40 км
	FarGround->CastShadow = false;
}

void APeregonRoadWorld::SetupAssets()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	auto MakePhys = [this](EPhysicalSurface Surface) {
		UPhysicalMaterial* Phys = NewObject<UPhysicalMaterial>(this);
		Phys->SurfaceType = Surface;
		return Phys;
	};
	AsphaltPhys = MakePhys(PeregonRoad::AsphaltSurface);
	GravelPhys = MakePhys(PeregonRoad::GravelSurface);
	GrassPhys = MakePhys(PeregonRoad::GrassSurface);

	Materials.SetNum(static_cast<int32>(EPeregonWorldMat::Count));
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	if (!Art)
	{
		return;
	}
	auto Set = [&](EPeregonWorldMat Slot, const FPeregonLook& Look) { Materials[static_cast<int32>(Slot)] = Art->MakeMaterial(this, Look); };
	Set(EPeregonWorldMat::Road, FPeregonLook(EPeregonTex::RoadMarked, FLinearColor::White, 0.85f));
	Set(EPeregonWorldMat::Shoulder, FPeregonLook(EPeregonTex::Gravel, FLinearColor::White, 0.95f));
	Set(EPeregonWorldMat::Grass, FPeregonLook(EPeregonTex::Grass, FLinearColor::White, 0.95f));
	Set(EPeregonWorldMat::Pad, FPeregonLook(EPeregonTex::Asphalt, FLinearColor(0.95f, 0.95f, 0.95f), 0.85f));
	Set(EPeregonWorldMat::Wood, FPeregonLook(EPeregonTex::WoodPlanks, FLinearColor(0.62f, 0.48f, 0.34f), 0.9f));
	Set(EPeregonWorldMat::Roof, FPeregonLook(EPeregonTex::RoofMetal, FLinearColor(0.55f, 0.58f, 0.6f), 0.5f, 0.6f));
	Set(EPeregonWorldMat::WhitePaint, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.85f, 0.85f, 0.82f), 0.6f));
	Set(EPeregonWorldMat::DarkPaint, FPeregonLook(EPeregonTex::Paint, FLinearColor(0.05f, 0.05f, 0.05f), 0.6f));
	Set(EPeregonWorldMat::Concrete, FPeregonLook(EPeregonTex::Concrete, FLinearColor::White, 0.9f));
	Set(EPeregonWorldMat::Glass, FPeregonLook(EPeregonTex::Glass, FLinearColor::White, 0.1f));
	Set(EPeregonWorldMat::BirchBark, FPeregonLook(EPeregonTex::BirchBark, FLinearColor::White, 0.9f));
	Set(EPeregonWorldMat::PineBark, FPeregonLook(EPeregonTex::PineBark, FLinearColor::White, 0.95f));
	Set(EPeregonWorldMat::Needles, FPeregonLook(EPeregonTex::Needles, FLinearColor::White, 0.95f));
	Set(EPeregonWorldMat::Leaves, FPeregonLook(EPeregonTex::Leaves, FLinearColor::White, 0.9f));
	Set(EPeregonWorldMat::Pole, FPeregonLook(EPeregonTex::PineBark, FLinearColor(0.45f, 0.4f, 0.35f), 0.95f));
	FPeregonLook FarLook(EPeregonTex::Needles, FLinearColor(0.8f, 0.9f, 0.8f), 1.f);
	FarLook.WorldTileCm = 1200.f;
	Set(EPeregonWorldMat::Far, FarLook);
}

UMaterialInterface* APeregonRoadWorld::Mat(EPeregonWorldMat Type) const
{
	const int32 Index = static_cast<int32>(Type);
	return Materials.IsValidIndex(Index) ? Materials[Index].Get() : nullptr;
}

void APeregonRoadWorld::EndPlay(const EEndPlayReason::Type Reason)
{
	// АЗС и кафе — отдельные акторы, их нужно убрать вместе с миром.
	TArray<int32> Keys;
	Chunks.GetKeys(Keys);
	for (int32 Key : Keys)
	{
		DestroyChunk(Key);
	}
	Super::EndPlay(Reason);
}

void APeregonRoadWorld::Generate(int32 Seed)
{
	TArray<int32> Keys;
	Chunks.GetKeys(Keys);
	for (int32 Key : Keys)
	{
		DestroyChunk(Key);
	}
	if (!CubeMesh)
	{
		SetupAssets();
	}

	rgen::RouteParams Params;
	Params.Seed = static_cast<uint32_t>(Seed);
	Route.Build(Params);
	TrackedS = 0.0;
	bGenerated = true;

	FarGround->SetStaticMesh(CubeMesh);
	FarGround->SetMaterial(0, Mat(EPeregonWorldMat::Far));
}

void APeregonRoadWorld::BuildAround(double S)
{
	TrackedS = FMath::Clamp(S, 0.0, Route.Length());
	UpdateStreaming(true);
}

double APeregonRoadWorld::FindS(const FVector& WorldLocation, double Hint) const
{
	return Route.FindNearestS(WorldLocation.X / PeregonRoad::M, WorldLocation.Y / PeregonRoad::M, Hint);
}

double APeregonRoadWorld::LateralOffset(const FVector& WorldLocation, double S) const
{
	return Route.LateralOffset(WorldLocation.X / PeregonRoad::M, WorldLocation.Y / PeregonRoad::M, S);
}

FVector APeregonRoadWorld::RouteToWorld(double S, double T, double ZOffsetM) const
{
	double X = 0.0;
	double Y = 0.0;
	Route.ToWorld(S, T, X, Y);
	return FVector(X, Y, Route.GroundHeight(S, T) + ZOffsetM) * PeregonRoad::M;
}

FTransform APeregonRoadWorld::RoadTransform(double S, double T, double ZOffsetM) const
{
	const rgen::RouteSample Smp = Route.Sample(S);
	const FRotator Rot(FMath::RadiansToDegrees(FMath::Atan(Smp.Grade)), FMath::RadiansToDegrees(Smp.Heading), 0.0);
	return FTransform(Rot, RouteToWorld(S, T, ZOffsetM));
}

FString APeregonRoadWorld::GetPlaceName(double S) const
{
	for (const rgen::Poi& Poi : Route.Pois())
	{
		const double Radius = (Poi.Type == rgen::PoiType::Village || Poi.Type == rgen::PoiType::Finish) ? 230.0 : 80.0;
		if (FMath::Abs(Poi.S - S) < Radius || (Poi.Type == rgen::PoiType::Town && S < 150.0))
		{
			return PeregonRoad::PoiName(Poi);
		}
	}
	return TEXT("Трасса");
}

FVector APeregonRoadWorld::FocusLocation() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return FVector::ZeroVector;
	}
	if (const APawn* Pawn = PC->GetPawn())
	{
		return Pawn->GetActorLocation();
	}
	return PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
}

void APeregonRoadWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bGenerated)
	{
		return;
	}
	const FVector Focus = FocusLocation();
	const double PrevS = TrackedS;
	TrackedS = FindS(Focus, TrackedS);
	if (bAnnouncePlaces)
	{
		AnnouncePlaces(PrevS, TrackedS);
	}
	UpdateStreaming(false);
	FarGround->SetWorldLocation(FVector(Focus.X, Focus.Y, (Route.MinZ() - 6.0) * PeregonRoad::M));
}

void APeregonRoadWorld::AnnouncePlaces(double PrevS, double NewS)
{
	APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
	if (!GM || NewS <= PrevS || NewS - PrevS > 100.0)
	{
		return;
	}
	for (const rgen::Poi& Poi : Route.Pois())
	{
		const double Entry = Poi.S - 220.0;
		if ((Poi.Type == rgen::PoiType::Village || Poi.Type == rgen::PoiType::Finish) && PrevS < Entry && NewS >= Entry)
		{
			GM->Toast(PeregonRoad::PoiName(Poi));
		}
		if (Poi.Type == rgen::PoiType::Finish && PrevS < Poi.S && NewS >= Poi.S)
		{
			GM->Toast(FString::Printf(TEXT("Вы доехали до посёлка %s! Конец тестового участка."), *PeregonRoad::PoiName(Poi)), 8.f);
		}
	}
}

void APeregonRoadWorld::UpdateStreaming(bool bBuildAll)
{
	const int32 Last = FMath::FloorToInt(Route.Length() / ChunkLength) + 1;
	const int32 Current = FMath::FloorToInt(TrackedS / ChunkLength);
	const int32 From = FMath::Max(-1, Current - ChunksBehind);
	const int32 To = FMath::Min(Last, Current + ChunksAhead);

	TArray<int32> ToRemove;
	for (const auto& Pair : Chunks)
	{
		if (Pair.Key < From - 1 || Pair.Key > To + 1)
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (int32 Index : ToRemove)
	{
		DestroyChunk(Index);
	}

	// Сначала ближайшие к игроку чанки, по одному за кадр.
	for (int32 Distance = 0; Distance <= FMath::Max(ChunksAhead, ChunksBehind); ++Distance)
	{
		for (int32 Index : {Current + Distance, Current - Distance})
		{
			if (Index < From || Index > To || Chunks.Contains(Index))
			{
				continue;
			}
			BuildChunk(Index);
			if (!bBuildAll)
			{
				return;
			}
		}
	}
}

void APeregonRoadWorld::DestroyChunk(int32 Index)
{
	if (FChunk* Chunk = Chunks.Find(Index))
	{
		for (TWeakObjectPtr<UActorComponent>& Comp : Chunk->Components)
		{
			if (Comp.IsValid())
			{
				Comp->DestroyComponent();
			}
		}
		for (TWeakObjectPtr<AActor>& Actor : Chunk->Actors)
		{
			if (Actor.IsValid())
			{
				Actor->Destroy();
			}
		}
	}
	Chunks.Remove(Index);
}

// ---------------------------------------------------------------------------------------------
// Геометрия
// ---------------------------------------------------------------------------------------------
void APeregonRoadWorld::BuildGrid(FMeshBuffers& Out, const TArray<double>& Ss, const TArray<double>& Ts, double ZOffsetM, double UvTileM, bool bRoadUV) const
{
	const int32 Rows = Ss.Num();
	const int32 Cols = Ts.Num();
	const int32 Base = Out.Vertices.Num();
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 C = 0; C < Cols; ++C)
		{
			double X = 0.0;
			double Y = 0.0;
			Route.ToWorld(Ss[R], Ts[C], X, Y);
			const double Z = (bRoadUV ? Route.Sample(Ss[R]).Z : Route.GroundHeight(Ss[R], Ts[C])) + ZOffsetM;
			Out.Vertices.Add(FVector(X, Y, Z) * PeregonRoad::M);
			if (bRoadUV)
			{
				const double Half = Route.RoadHalfWidth();
				Out.UVs.Add(FVector2D((Ts[C] + Half) / (2.0 * Half), Ss[R] / 12.0));
			}
			else
			{
				Out.UVs.Add(FVector2D(X / UvTileM, Y / UvTileM));
			}
		}
	}
	// Нормали по соседям сетки: вдоль S × вдоль T (T растёт вправо) = вверх.
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 C = 0; C < Cols; ++C)
		{
			const FVector& Fwd0 = Out.Vertices[Base + FMath::Max(R - 1, 0) * Cols + C];
			const FVector& Fwd1 = Out.Vertices[Base + FMath::Min(R + 1, Rows - 1) * Cols + C];
			const FVector& Rt0 = Out.Vertices[Base + R * Cols + FMath::Max(C - 1, 0)];
			const FVector& Rt1 = Out.Vertices[Base + R * Cols + FMath::Min(C + 1, Cols - 1)];
			FVector N = FVector::CrossProduct(Fwd1 - Fwd0, Rt1 - Rt0).GetSafeNormal();
			if (N.Z < 0.0)
			{
				N = -N;
			}
			Out.Normals.Add(N.IsNearlyZero() ? FVector::UpVector : N);
		}
	}
	// Треугольники по часовой стрелке при взгляде сверху — лицевая сторона вверх.
	for (int32 R = 0; R + 1 < Rows; ++R)
	{
		for (int32 C = 0; C + 1 < Cols; ++C)
		{
			const int32 A = Base + R * Cols + C;
			const int32 B = A + 1;
			const int32 Cn = A + Cols;
			const int32 D = Cn + 1;
			Out.Triangles.Append({A, Cn, B, B, Cn, D});
		}
	}
}

UProceduralMeshComponent* APeregonRoadWorld::AddSurface(FChunk& Chunk, const FMeshBuffers& Data, EPeregonWorldMat MatType, UPhysicalMaterial* Phys)
{
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetupAttachment(RootComponent);
	Mesh->bUseAsyncCooking = false;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionProfileName(Phys ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	if (Phys)
	{
		Mesh->SetPhysMaterialOverride(Phys);
	}
	Mesh->RegisterComponent();
	Mesh->CreateMeshSection(0, Data.Vertices, Data.Triangles, Data.Normals, Data.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), Phys != nullptr);
	if (UMaterialInterface* Material = Mat(MatType))
	{
		Mesh->SetMaterial(0, Material);
	}
	Chunk.Components.Add(Mesh);
	return Mesh;
}

UStaticMeshComponent* APeregonRoadWorld::AddMesh(FChunk& Chunk, UStaticMesh* InMesh, const FTransform& Transform, UMaterialInterface* Material, bool bCollision)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(RootComponent);
	Comp->SetStaticMesh(InMesh);
	Comp->SetRelativeTransform(Transform);
	Comp->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->RegisterComponent();
	Chunk.Components.Add(Comp);
	return Comp;
}

UInstancedStaticMeshComponent* APeregonRoadWorld::AddInstanced(FChunk& Chunk, UStaticMesh* InMesh, EPeregonWorldMat MatType, bool bCollision)
{
	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(RootComponent);
	Comp->SetStaticMesh(InMesh);
	Comp->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	if (UMaterialInterface* Material = Mat(MatType))
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->RegisterComponent();
	Chunk.Components.Add(Comp);
	return Comp;
}

void APeregonRoadWorld::AddSign(FChunk& Chunk, double S, double T, const FString& Text, bool bCrossed, bool bFacingDriver)
{
	const rgen::RouteSample Smp = Route.Sample(S);
	const double Yaw = FMath::RadiansToDegrees(Smp.Heading) + (bFacingDriver ? 180.0 : 0.0);
	const FRotator Rot(0.0, Yaw, 0.0);
	const FVector Base = RouteToWorld(S, T, 0.0);

	AddMesh(Chunk, CubeMesh, FTransform(Rot, Base + FVector(0, 0, 110), FVector(0.08, 0.08, 2.2)), Mat(EPeregonWorldMat::DarkPaint), false);
	// Белая табличка с чёрной окантовкой (как настоящий знак населённого пункта).
	AddMesh(Chunk, CubeMesh, FTransform(Rot, Base + FVector(0, 0, 230), FVector(0.04, 2.3, 0.75)), Mat(EPeregonWorldMat::DarkPaint), false);
	// Белое поле с надписью шрифтом Golos (кириллица рисуется в текстуру).
	if (UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this))
	{
		const int32 FontPx = Text.Len() > 12 ? 24 : 38;
		UMaterialInstanceDynamic* LabelMat = Art->MakeLabelMaterial(this, Text, FLinearColor(0.01f, 0.01f, 0.01f), FLinearColor(0.85f, 0.85f, 0.82f), 256, 72, FontPx);
		if (UProceduralMeshComponent* Quad = UPeregonArtSubsystem::AddLabelQuad(this, RootComponent,
				FTransform(Rot, Base + FVector(0, 0, 230) + Rot.Vector() * 2.5), 215.f, 62.f, LabelMat))
		{
			Chunk.Components.Add(Quad);
		}
	}

	if (bCrossed)
	{
		// Красная полоса «конец населённого пункта».
		const FRotator Slash(0.0, Yaw, 18.0);
		AddMesh(Chunk, CubeMesh, FTransform(Slash, Base + FVector(0, 0, 230) + Rot.Vector() * 5.0, FVector(0.01, 2.2, 0.06)),
			Mat(EPeregonWorldMat::Roof), false);
	}
}

void APeregonRoadWorld::BuildChunk(int32 Index)
{
	using namespace PeregonRoad;
	FChunk& Chunk = Chunks.Add(Index);
	const double S0 = Index * static_cast<double>(ChunkLength);
	const double S1 = S0 + ChunkLength;

	TArray<double> Ss;
	const int32 Rows = FMath::Max(2, FMath::CeilToInt(ChunkLength / 5.0) + 1);
	for (int32 R = 0; R < Rows; ++R)
	{
		Ss.Add(S0 + (S1 - S0) * R / (Rows - 1));
	}
	const double Half = Route.RoadHalfWidth();
	const double Shoulder = Route.ShoulderOuter();

	// Полотно, обочины, рельеф.
	{
		FMeshBuffers Road;
		BuildGrid(Road, Ss, {-Half, Half}, 0.0, 1.0, true);
		AddSurface(Chunk, Road, EPeregonWorldMat::Road, AsphaltPhys);

		FMeshBuffers Shoulders;
		BuildGrid(Shoulders, Ss, {-Shoulder, -(Half + Shoulder) * 0.5, -Half}, 0.0, 2.5, false);
		BuildGrid(Shoulders, Ss, {Half, (Half + Shoulder) * 0.5, Shoulder}, 0.0, 2.5, false);
		AddSurface(Chunk, Shoulders, EPeregonWorldMat::Shoulder, GravelPhys);

		TArray<double> Right;
		TArray<double> Left;
		for (double T : TerrainT)
		{
			Right.Add(T);
			Left.Insert(-T, 0);
		}
		FMeshBuffers Terrain;
		BuildGrid(Terrain, Ss, Left, 0.0, 4.0, false);
		BuildGrid(Terrain, Ss, Right, 0.0, 4.0, false);
		AddSurface(Chunk, Terrain, EPeregonWorldMat::Grass, GrassPhys);
	}

	// Придорожная обстановка: столбики, ЛЭП, километровые знаки.
	UInstancedStaticMeshComponent* Posts = AddInstanced(Chunk, CubeMesh, EPeregonWorldMat::WhitePaint, false);
	UInstancedStaticMeshComponent* Poles = AddInstanced(Chunk, CylinderMesh, EPeregonWorldMat::Pole, true);
	UInstancedStaticMeshComponent* Wires = AddInstanced(Chunk, CubeMesh, EPeregonWorldMat::DarkPaint, false);
	for (double S = FMath::CeilToDouble(S0 / 50.0) * 50.0; S < S1; S += 50.0)
	{
		for (int32 Side : {-1, 1})
		{
			const FVector P = RouteToWorld(S, Side * 7.0, 0.0);
			Posts->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0, 0, 45), FVector(0.12, 0.12, 0.9)));
		}
		const FVector Pole = RouteToWorld(S, -16.0, 0.0);
		Poles->AddInstance(FTransform(FRotator::ZeroRotator, Pole + FVector(0, 0, 450), FVector(0.26, 0.26, 9.0)));
		Posts->AddInstance(FTransform(FRotator(0.0, FMath::RadiansToDegrees(Route.Sample(S).Heading) + 90.0, 0.0),
			Pole + FVector(0, 0, 840), FVector(0.12, 1.6, 0.12))); // траверса
		const FVector Next = RouteToWorld(S + 50.0, -16.0, 0.0);
		for (double Offset : {-60.0, 60.0})
		{
			const FVector Right = FVector::CrossProduct(FVector::UpVector, (Next - Pole).GetSafeNormal()) * Offset;
			const FVector A = Pole + FVector(0, 0, 845) + Right;
			const FVector B = Next + FVector(0, 0, 845) + Right;
			const FVector Mid = (A + B) * 0.5 - FVector(0, 0, 40); // провис
			Wires->AddInstance(FTransform(FRotationMatrix::MakeFromX(B - A).Rotator(), Mid, FVector((B - A).Size() / 100.0, 0.02, 0.02)));
		}
	}
	for (double S = FMath::CeilToDouble(FMath::Max(S0, 1.0) / 1000.0) * 1000.0; S < S1; S += 1000.0)
	{
		if (S <= Route.Length())
		{
			AddSign(Chunk, S, 6.8, FString::Printf(TEXT("%d"), FMath::RoundToInt(S / 1000.0)), false);
		}
	}

	// Начало и конец участка: бетонные блоки поперёк дороги.
	for (double EndS : {-12.0, Route.Length() + 12.0})
	{
		if (EndS >= S0 && EndS < S1)
		{
			const rgen::RouteSample Smp = Route.Sample(EndS);
			for (double T = -Half + 0.6; T <= Half; T += 1.75)
			{
				AddMesh(Chunk, CubeMesh, FTransform(FRotator(0.0, FMath::RadiansToDegrees(Smp.Heading) + 90.0, 0.0), RouteToWorld(EndS, T, 0.45),
					FVector(1.5, 0.6, 0.9)), Mat(EPeregonWorldMat::Concrete), true);
			}
			AddSign(Chunk, EndS + (EndS < 0.0 ? 3.0 : -3.0), Half + 2.5, EndS < 0.0 ? TEXT("ДОРОГА ЗАКРЫТА") : TEXT("ПРОДОЛЖЕНИЕ СЛЕДУЕТ"), false, EndS > 0.0);
		}
	}

	// Точки интереса.
	for (const rgen::Poi& Poi : Route.Pois())
	{
		const FString Name = PoiName(Poi);
		switch (Poi.Type)
		{
		case rgen::PoiType::Town:
			if (S0 <= 120.0 && 120.0 < S1)
			{
				AddSign(Chunk, 120.0, Half + 3.0, Name, true);
			}
			break;
		case rgen::PoiType::Village:
		case rgen::PoiType::Finish:
			BuildVillage(Chunk, Poi, S0, S1);
			break;
		case rgen::PoiType::Fuel:
		case rgen::PoiType::Cafe:
		{
			if (Poi.S < S0 || Poi.S >= S1)
			{
				break;
			}
			// Асфальтовая площадка от края полотна до здания.
			TArray<double> PadS;
			for (double S = Poi.S - 40.0; S <= Poi.S + 40.0; S += 5.0)
			{
				PadS.Add(S);
			}
			TArray<double> PadT;
			for (double T : {Half + 0.1, 8.0, 16.0, 26.0, 36.0})
			{
				PadT.Add(T);
			}
			if (Poi.Side < 0)
			{
				for (double& T : PadT)
				{
					T = -T;
				}
				Algo::Reverse(PadT);
			}
			FMeshBuffers Pad;
			BuildGrid(Pad, PadS, PadT, 0.0, 4.0, false);
			for (int32 V = 0; V < Pad.Vertices.Num(); ++V)
			{
				// Площадка ровная, вровень с дорогой (рельеф под ней выровнен генератором).
				const double S = PadS[V / PadT.Num()];
				Pad.Vertices[V].Z = (Route.Sample(S).Z + 0.01) * M;
				Pad.Normals[V] = FVector::UpVector;
			}
			AddSurface(Chunk, Pad, EPeregonWorldMat::Pad, AsphaltPhys);

			const rgen::RouteSample Smp = Route.Sample(Poi.S);
			const double Yaw = FMath::RadiansToDegrees(Smp.Heading) + (Poi.Side > 0 ? -90.0 : 90.0);
			FVector Location = RouteToWorld(Poi.S, Poi.Side * 24.0, 0.0);
			Location.Z = (Smp.Z + 0.01) * M;
			APeregonServicePoint* Service = GetWorld()->SpawnActorDeferred<APeregonServicePoint>(APeregonServicePoint::StaticClass(),
				FTransform(FRotator(0.0, Yaw, 0.0), Location), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Service)
			{
				Service->Kind = Poi.Type == rgen::PoiType::Fuel ? EPeregonService::Fuel : EPeregonService::Cafe;
				Service->DisplayName = Name;
				Service->FinishSpawning(FTransform(FRotator(0.0, Yaw, 0.0), Location));
				Chunk.Actors.Add(Service);
			}
			break;
		}
		default:
			break;
		}
	}

	BuildTrees(Chunk, Index, S0, S1);
}

bool APeregonRoadWorld::IsClearZone(double S, double T) const
{
	// Без деревьев: у дороги, под ЛЭП, на площадках и улицах сёл.
	if (FMath::Abs(T) < 13.0 || (T < -11.0 && T > -21.0))
	{
		return true;
	}
	for (const rgen::FlattenZone& Z : Route.Flatten())
	{
		if (S > Z.S0 - 30.0 && S < Z.S1 + 30.0 && T > Z.TMin - 15.0 && T < Z.TMax + 15.0)
		{
			return true;
		}
	}
	return false;
}

void APeregonRoadWorld::BuildTrees(FChunk& Chunk, int32 Index, double S0, double S1)
{
	UInstancedStaticMeshComponent* PineTrunks = AddInstanced(Chunk, CylinderMesh, EPeregonWorldMat::PineBark, true);
	UInstancedStaticMeshComponent* PineCrowns = AddInstanced(Chunk, ConeMesh, EPeregonWorldMat::Needles, false);
	UInstancedStaticMeshComponent* BirchTrunks = AddInstanced(Chunk, CylinderMesh, EPeregonWorldMat::BirchBark, true);
	UInstancedStaticMeshComponent* BirchCrowns = AddInstanced(Chunk, SphereMesh, EPeregonWorldMat::Leaves, false);
	for (UInstancedStaticMeshComponent* Crowns : {PineCrowns, BirchCrowns})
	{
		Crowns->SetCastShadow(true);
	}

	rgen::Rng Rand(rgen::Hash(static_cast<uint32_t>(Index + 100000), Route.Params().Seed));
	const uint32_t ForestSeed = rgen::Hash(Route.Params().Seed, 0xF0E57u);
	for (double S = S0; S < S1; S += 9.0)
	{
		for (int32 Side : {-1, 1})
		{
			for (double A = 14.0; A < 280.0; A += (A < 80.0 ? 10.0 : 16.0))
			{
				const double T = Side * (A + Rand.Range(-3.0, 3.0));
				const double Sj = S + Rand.Range(-3.5, 3.5);
				if (IsClearZone(Sj, T))
				{
					continue;
				}
				double X = 0.0;
				double Y = 0.0;
				Route.ToWorld(Sj, T, X, Y);
				// Лес пятнами: перелески, поля, опушки.
				const double Forest = rgen::Noise2D(X / 380.0, Y / 380.0, ForestSeed);
				const double Density = FMath::SmoothStep(-0.25, 0.35, Forest) * 0.75 + 0.04;
				if (!Rand.Chance(Density))
				{
					continue;
				}
				const FVector Base = FVector(X, Y, Route.GroundHeight(Sj, T) - 0.2) * PeregonRoad::M;
				const double Scale = Rand.Range(0.75, 1.3);
				const double Yaw = Rand.Range(0.0, 360.0);
				const bool bPine = rgen::Noise2D(X / 160.0, Y / 160.0, ForestSeed + 3) + Rand.Range(-0.4, 0.4) > -0.1;
				if (bPine)
				{
					const double H = 13.0 * Scale;
					PineTrunks->AddInstance(FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, H * 50.0), FVector(0.38 * Scale, 0.38 * Scale, H)));
					PineCrowns->AddInstance(FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, H * 100.0 * 0.55), FVector(4.6 * Scale, 4.6 * Scale, H * 0.7)));
					PineCrowns->AddInstance(FTransform(FRotator(0, Yaw + 40, 0), Base + FVector(0, 0, H * 100.0 * 0.85), FVector(3.0 * Scale, 3.0 * Scale, H * 0.45)));
				}
				else
				{
					const double H = 10.0 * Scale;
					BirchTrunks->AddInstance(FTransform(FRotator(Rand.Range(-3.0, 3.0), Yaw, 0), Base + FVector(0, 0, H * 50.0), FVector(0.26 * Scale, 0.26 * Scale, H)));
					BirchCrowns->AddInstance(FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, H * 100.0 * 0.78), FVector(4.2 * Scale, 4.2 * Scale, 6.0 * Scale)));
				}
			}
		}
	}
}

void APeregonRoadWorld::BuildVillage(FChunk& Chunk, const rgen::Poi& Poi, double S0, double S1)
{
	const FString Name = PeregonRoad::PoiName(Poi);
	const double Half = Route.RoadHalfWidth();
	const double EntryS = Poi.S - 220.0;
	const double ExitS = Poi.S + 220.0;
	if (EntryS >= S0 && EntryS < S1)
	{
		AddSign(Chunk, EntryS, Half + 3.0, Name, false);
	}
	if (ExitS >= S0 && ExitS < S1 && Poi.Type != rgen::PoiType::Finish)
	{
		AddSign(Chunk, ExitS, Half + 3.0, Name, true);
	}

	UInstancedStaticMeshComponent* Roofs = nullptr;
	UInstancedStaticMeshComponent* Fence = nullptr;
	rgen::Rng Rand(rgen::Hash(static_cast<uint32_t>(Poi.S), Route.Params().Seed));
	for (double S = Poi.S - 180.0; S < Poi.S + 180.0; S += Rand.Range(26.0, 38.0))
	{
		for (int32 Side : {-1, 1})
		{
			const uint32 HouseSeed = rgen::Hash(static_cast<uint32_t>(S * 10.0), static_cast<uint32_t>(Side + 7), Route.Params().Seed);
			const bool bHouse = rgen::HashToUnit(HouseSeed) < 0.82;
			if (S < S0 || S >= S1 || !bHouse)
			{
				continue;
			}
			if (!Roofs)
			{
				Roofs = AddInstanced(Chunk, CubeMesh, EPeregonWorldMat::Roof, false);
				Fence = AddInstanced(Chunk, CubeMesh, EPeregonWorldMat::Wood, false);
			}
			BuildHouse(Chunk, S, Side, HouseSeed, Roofs);

			// Штакетник вдоль улицы перед домом.
			for (double Fs = S - 12.0; Fs < S + 12.0; Fs += 2.0)
			{
				const FVector P = RouteToWorld(Fs, Side * 14.0, 0.0);
				Fence->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0, 0, 60), FVector(0.1, 0.1, 1.2)));
				const FVector Q = RouteToWorld(Fs + 2.0, Side * 14.0, 0.0);
				for (double H : {35.0, 95.0})
				{
					Fence->AddInstance(FTransform(FRotationMatrix::MakeFromX(Q - P).Rotator(), (P + Q) * 0.5 + FVector(0, 0, H),
						FVector((Q - P).Size() / 100.0, 0.04, 0.12)));
				}
			}
		}
	}
}

void APeregonRoadWorld::BuildHouse(FChunk& Chunk, double S, int32 Side, uint32 Seed, UInstancedStaticMeshComponent* Roofs)
{
	rgen::Rng Rand(Seed);
	const double Length = Rand.Range(7.0, 11.0);  // вдоль улицы
	const double Depth = Rand.Range(6.0, 8.0);    // поперёк
	const double Wall = Rand.Range(2.6, 3.1);
	const double T = Side * (Rand.Range(20.0, 26.0) + Depth * 0.5);

	double Ground = 1e9;
	for (double Ds : {-Length * 0.5, Length * 0.5})
	{
		for (double Dt : {-Depth * 0.5, Depth * 0.5})
		{
			Ground = FMath::Min(Ground, Route.GroundHeight(S + Ds, T + Dt));
		}
	}
	double X = 0.0;
	double Y = 0.0;
	Route.ToWorld(S, T, X, Y);
	const double Yaw = FMath::RadiansToDegrees(Route.Sample(S).Heading);
	const FTransform House(FRotator(0.0, Yaw, 0.0), FVector(X, Y, Ground - 0.25) * PeregonRoad::M);
	auto At = [&House](double Lx, double Ly, double Lz) { return House.TransformPosition(FVector(Lx, Ly, Lz) * PeregonRoad::M); };
	const FRotator Rot = House.Rotator();

	const FLinearColor Color = PeregonRoad::HouseColors[Seed % UE_ARRAY_COUNT(PeregonRoad::HouseColors)];
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	UMaterialInterface* WallMat = Art ? Art->MakeMaterial(this, FPeregonLook(EPeregonTex::WoodPlanks, Color, 0.9f)) : nullptr;

	// Сруб + фронтон (ромб, нижняя половина спрятана в стенах).
	AddMesh(Chunk, CubeMesh, FTransform(Rot, At(0, 0, (Wall + 0.25) * 0.5), FVector(Length, Depth, Wall + 0.25)), WallMat, true);
	AddMesh(Chunk, CubeMesh, FTransform(FRotator(0.0, Yaw, 45.0), At(0, 0, Wall + 0.25), FVector(Length - 0.05, Depth / FMath::Sqrt(2.0), Depth / FMath::Sqrt(2.0))), WallMat, false);
	// Двускатная крыша из профлиста.
	const double SlabWidth = Depth * 0.5 * FMath::Sqrt(2.0) + 0.5;
	for (int32 Slope : {-1, 1})
	{
		Roofs->AddInstance(FTransform(FRotator(0.0, Yaw, 45.0 * Slope), At(0, Slope * Depth * 0.25, Wall + 0.25 + Depth * 0.25 + 0.08),
			FVector(Length + 0.6, SlabWidth, 0.1)));
	}
	// Окна в наличниках на улицу.
	const double FaceY = -Side * (Depth * 0.5 + 0.03);
	for (double Wx : {-Length * 0.25, Length * 0.25})
	{
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(Wx, FaceY, 1.55), FVector(1.25, 0.06, 1.35)), Mat(EPeregonWorldMat::WhitePaint), false);
		AddMesh(Chunk, CubeMesh, FTransform(Rot, At(Wx, FaceY - Side * 0.02, 1.55), FVector(0.95, 0.06, 1.05)), Mat(EPeregonWorldMat::Glass), false);
	}
}

