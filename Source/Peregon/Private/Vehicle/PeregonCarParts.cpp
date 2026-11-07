// Узлы, детали и кузовные работы «Сокола»: что стоит под капотом и как это снять, поставить, починить.
// Логика деталей — vsim::parts (чистый C++, покрыт тестами); здесь — мишени, подсказки, работа «держа клавишу»,
// видимые узлы в моторном отсеке и влияние на физику.
#include "Vehicle/PeregonCar.h"

#include "Art/PeregonArtSubsystem.h"
#include "Audio/PeregonAudio.h"
#include "Audio/PeregonRadio.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Game/PeregonGameInstance.h"
#include "Game/PeregonGameMode.h"
#include "Game/PeregonPlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/PeregonCharacter.h"
#include "ProceduralMeshComponent.h"
#include "Vehicle/PeregonPartMeshes.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "World/PeregonKit.h"
#include "World/PeregonSky.h"

#include "vsim/BodyModel.h"

// Модель кузова (строится один раз, PeregonCar.cpp).
const vmodel::Model& PeregonCarModelData();

namespace PeregonCarParts
{
using vsim::parts::Panel;
using vsim::parts::Slot;

// Где стоит узел (основание, см, в системе кузова) и какого он размера — для мишени и видимой коробки.
struct FSlotPlace
{
	FVector Base;      // основание модели узла (низ по центру), см кузова
	float Yaw;         // поворот модели
	FVector HitCenter; // мишень (центр и полный размер)
	FVector HitSize;
	bool bVisible;     // рисовать модель (фары, щиток, магнитола, подвеска — уже есть в кузове)
};
// Места выверены по моторному отсеку кузова (vmodel::BuildEngineBay): блок у X 115–165, головка и клапанная
// крышка до Z 23, выпускной коллектор справа (Y 19–22), вентилятор у X 178, рамка радиатора у X 192,
// внутренние крылья — Y ±44, низ капота над фильтром — около Z 33.
const FSlotPlace Places[vsim::parts::SlotCount] = {
	{FVector(172.f, 31.f, -6.2f), 90.f, FVector(172.f, 31.f, 4.f), FVector(18.f, 28.f, 22.f), true},     // аккумулятор (на полке справа)
	{FVector(137.f, -16.f, 12.f), 0.f, FVector(137.f, -16.f, 17.f), FVector(32.f, 6.f, 10.f), true},       // свечи (слева в головке)
	{FVector(110.f, -10.f, 5.f), 0.f, FVector(110.f, -10.f, 11.5f), FVector(12.f, 12.f, 14.f), true},      // трамблёр (сзади слева)
	{FVector(142.f, 19.f, 8.f), 0.f, FVector(142.f, 19.f, 14.f), FVector(15.f, 14.f, 12.f), true},         // карбюратор (под фильтром справа)
	{FVector(142.f, 4.f, 24.5f), 0.f, FVector(142.f, 4.f, 28.f), FVector(30.f, 30.f, 7.f), true},         // воздушный фильтр
	{FVector(126.f, 30.f, 15.f), 0.f, FVector(126.f, 30.f, 17.f), FVector(15.f, 5.f, 5.f), true},          // топливный фильтр
	{FVector(152.f, -22.f, -16.f), 0.f, FVector(152.f, -22.f, -10.f), FVector(10.f, 10.f, 13.f), true},    // масляный фильтр
	{FVector(188.f, 0.f, -18.f), 0.f, FVector(188.f, 0.f, 4.f), FVector(7.f, 58.f, 44.f), true},          // радиатор
	{FVector(168.f, 0.f, -6.f), 0.f, FVector(169.f, 0.f, 0.f), FVector(12.f, 14.f, 14.f), true},           // помпа
	{FVector::ZeroVector, 0.f, FVector(175.f, -9.f, 19.f), FVector(22.f, 7.f, 7.f), true},                 // патрубки (модель — в координатах кузова)
	{FVector(160.f, -26.f, -6.f), 0.f, FVector(160.f, -26.f, 2.f), FVector(16.f, 16.f, 16.f), true},       // генератор
	{FVector(110.f, -21.f, -20.f), 0.f, FVector(110.f, -21.f, -15.f), FVector(24.f, 10.f, 12.f), true},    // стартер
	{FVector(150.f, 32.f, -8.f), 0.f, FVector(150.f, 32.f, 2.f), FVector(16.f, 24.f, 22.f), true},         // турбина (у коллектора справа)
	{FVector(-150.f, -30.f, -32.f), 0.f, FVector(-150.f, -30.f, -25.f), FVector(80.f, 18.f, 14.f), true},  // глушитель
	{FVector(116.f, -66.f, -24.f), 0.f, FVector(116.f, -66.f, -9.f), FVector(16.f, 16.f, 30.f), false},   // передняя подвеска
	{FVector(-126.f, -66.f, -24.f), 0.f, FVector(-126.f, -66.f, -9.f), FVector(16.f, 16.f, 30.f), false}, // задняя подвеска
	{FVector(116.f, 66.f, -24.f), 0.f, FVector(116.f, 66.f, -9.f), FVector(16.f, 16.f, 30.f), false},     // колодки
	{FVector(201.f, 0.f, 14.f), 0.f, FVector(201.f, 0.f, 22.f), FVector(6.f, 140.f, 16.f), false},        // фары
	{FVector(-203.f, 0.f, 17.f), 0.f, FVector(-203.f, 0.f, 25.f), FVector(6.f, 140.f, 16.f), false},      // фонари
	{FVector(73.f, -35.f, 37.f), 0.f, FVector(73.f, -35.f, 41.5f), FVector(4.f, 20.f, 9.f), false},       // щиток приборов
	{FVector(73.f, 0.f, 17.f), 0.f, FVector(73.f, 0.f, 18.f), FVector(6.f, 18.f, 3.f), false},            // магнитола (мишень — под корпусом)
};

// Мишени панелей, у которых нет своей подвижной детали.
struct FPanelPlace
{
	Panel Which;
	FVector Center;
	FVector Half;
};
const FPanelPlace PanelPlaces[] = {
	{Panel::FenderFL, FVector(150.f, -79.f, 15.f), FVector(50.f, 2.f, 20.f)},
	{Panel::FenderFR, FVector(150.f, 79.f, 15.f), FVector(50.f, 2.f, 20.f)},
	{Panel::QuarterRL, FVector(-155.f, -79.f, 15.f), FVector(46.f, 2.f, 20.f)},
	{Panel::QuarterRR, FVector(-155.f, 79.f, 15.f), FVector(46.f, 2.f, 20.f)},
	{Panel::Roof, FVector(-15.f, 0.f, 89.f), FVector(55.f, 50.f, 2.f)},
	{Panel::SillL, FVector(-8.f, -80.f, -18.f), FVector(55.f, 3.f, 4.f)},
	{Panel::SillR, FVector(-8.f, 80.f, -18.f), FVector(55.f, 3.f, 4.f)},
};

// Масло и антифриз — горловины под капотом.
// Горловина масла — на клапанной крышке, антифриз — горловина радиатора.
const FVector FillPlaces[2] = {FVector(128.f, -6.f, 25.f), FVector(188.f, 18.f, 28.5f)};
constexpr double OilNorm = 3.75;
constexpr double CoolantNorm = 7.8;

// Работа идёт быстрее, чем в жизни: секунды из vsim — «настоящие», в игре — доля от них.
constexpr float WorkScale = 0.3f;

int32 FindBox(const TArray<TObjectPtr<UBoxComponent>>& Boxes, const UPrimitiveComponent* Part)
{
	for (int32 I = 0; I < Boxes.Num(); ++I)
	{
		if (Boxes[I].Get() == Part)
		{
			return I;
		}
	}
	return INDEX_NONE;
}

FString U(const char* Utf8) { return FString(UTF8_TO_TCHAR(Utf8)); }

FString Percent(double V) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(static_cast<float>(V) * 100.f)); }

FString ProgressBar(float Fraction)
{
	const int32 Filled = FMath::Clamp(FMath::RoundToInt(Fraction * 12.f), 0, 12);
	FString Bar;
	for (int32 I = 0; I < 12; ++I)
	{
		Bar += I < Filled ? TEXT("|") : TEXT("·");
	}
	return Bar;
}

FString PanelStatus(const vsim::parts::PanelState& P)
{
	TArray<FString> Bits;
	if (P.Holes > 0.02)
	{
		Bits.Add(TEXT("дыры ") + Percent(P.Holes));
	}
	if (P.Rust > 0.05)
	{
		Bits.Add(TEXT("ржавчина ") + Percent(P.Rust));
	}
	if (P.Putty > 0.01)
	{
		Bits.Add(TEXT("шпаклёвка не ошкурена"));
	}
	if (P.Paint > 0.02)
	{
		Bits.Add(TEXT("ЛКП ") + Percent(P.Paint));
	}
	else
	{
		Bits.Add(P.bPrimed ? TEXT("в грунте") : (P.bSanded ? TEXT("ошкурено, голый металл") : TEXT("голый металл")));
	}
	return FString::Join(Bits, TEXT(" · "));
}

float WorkSeconds(double RealSeconds) { return FMath::Clamp(static_cast<float>(RealSeconds) * WorkScale, 1.5f, 25.f); }

APeregonPlayerController* ControllerOf(const APawn* User)
{
	return User ? Cast<APeregonPlayerController>(User->GetController()) : nullptr;
}

vsim::parts::PartItem ToPart(const FPeregonItemStack& Stack, int32 SlotIndex)
{
	vsim::parts::PartItem Out;
	Out.Kind = static_cast<Slot>(SlotIndex);
	Out.Condition = FMath::Clamp(Stack.Condition, 0.f, 1.f);
	Out.Tier = Stack.Tier;
	Out.Charge = Stack.Charge;
	return Out;
}

FPeregonItemStack ToStack(const vsim::parts::PartItem& Part)
{
	FPeregonItemStack Out;
	Out.Id = PeregonItems::PartItem(static_cast<int32>(Part.Kind));
	Out.Count = 1;
	Out.Condition = static_cast<float>(Part.Condition);
	Out.Tier = Part.Tier;
	Out.Charge = static_cast<float>(Part.Charge);
	return Out;
}
} // namespace PeregonCarParts

void APeregonCar::SetParts(const vsim::parts::CarState& State)
{
	PartsState = State;
	PartsEffects = vsim::parts::Evaluate(PartsState);
	bPartsAssigned = true;
	bPartsDirty = true;
	bBodyDirty = true;
}

FPeregonContainer* APeregonCar::GetStorage(EPeregonStorage Storage)
{
	return Storage == EPeregonStorage::Glovebox ? &GloveStore : (Storage == EPeregonStorage::Trunk ? &TrunkStore : nullptr);
}

bool APeregonCar::IsHoodOpen() const
{
	const FPeregonMovingPart* Hood = MovingParts.FindByPredicate([](const FPeregonMovingPart& P) { return P.Tag == FName(TEXT("Hood")); });
	return Hood && Hood->Alpha > 0.7f;
}

void APeregonCar::BuildPartSlots()
{
	using namespace PeregonCarParts;
	auto MakeBox = [this](const FString& Name, const FVector& Center, const FVector& Half, FName Tag) {
		UBoxComponent* Box = NewObject<UBoxComponent>(this, *Name);
		Box->SetupAttachment(Body);
		Box->SetRelativeLocation(Center);
		Box->SetBoxExtent(Half);
		Box->SetHiddenInGame(true);
		Box->RegisterComponent();
		PeregonInteraction::MakeInteractable(Box, Tag);
		return Box;
	};
	SlotBoxes.Reset();
	for (int32 I = 0; I < vsim::parts::SlotCount; ++I)
	{
		const FSlotPlace& P = Places[I];
		SlotBoxes.Add(MakeBox(FString::Printf(TEXT("PartSlot%d"), I), P.HitCenter, P.HitSize * 0.5f + FVector(1.f), FName(TEXT("PartSlot"))));
	}
	PanelBoxes.Reset();
	for (const FPanelPlace& P : PanelPlaces)
	{
		PanelBoxes.Add(MakeBox(FString::Printf(TEXT("BodyPanel%d"), static_cast<int32>(P.Which)), P.Center, P.Half, FName(TEXT("BodyPanel"))));
	}
	FillBoxes.Reset();
	for (int32 I = 0; I < 2; ++I)
	{
		FillBoxes.Add(MakeBox(FString::Printf(TEXT("Fill%d"), I), FillPlaces[I], FVector(4.f), FName(TEXT("Fill"))));
	}

	// Номера: «А 123 ВС 54» — регион 54 (Новосибирская область), буквы — только те, что есть и в латинице.
	if (UPeregonArtSubsystem* PlateArt = UPeregonArtSubsystem::Get(this))
	{
		static const TCHAR* Letters[] = {TEXT("А"), TEXT("В"), TEXT("Е"), TEXT("К"), TEXT("М"), TEXT("Н"), TEXT("О"), TEXT("Р"), TEXT("С"), TEXT("Т"), TEXT("У"), TEXT("Х")};
		FRandomStream PlateRand(static_cast<int32>(GetUniqueID() * 7919u + 13u));
		auto Letter = [&PlateRand]() { return Letters[PlateRand.RandRange(0, static_cast<int32>(UE_ARRAY_COUNT(Letters)) - 1)]; };
		const FString Number = FString::Printf(TEXT("%s %03d %s%s  54"), Letter(), PlateRand.RandRange(1, 999), Letter(), Letter());
		UMaterialInstanceDynamic* PlateMat = PlateArt->MakeLabelMaterial(this, Number, FLinearColor(0.02f, 0.02f, 0.02f), FLinearColor(0.92f, 0.92f, 0.9f), 512,
			112, 74, EPeregonFont::Display);
		UPeregonArtSubsystem::AddLabelQuad(this, Body, FTransform(FRotator::ZeroRotator, FVector(203.f + 9.2f + 0.35f, 0.f, -15.5f)), 43.f, 10.f, PlateMat);
		UPeregonArtSubsystem::AddLabelQuad(this, Body, FTransform(FRotator(0.f, 180.f, 0.f), FVector(-204.f - 0.6f - 0.35f, 0.f, 2.f)), 43.f, 10.f, PlateMat);
	}

	PartsMesh = NewObject<UProceduralMeshComponent>(this, TEXT("PartsMesh"));
	PartsMesh->SetupAttachment(Body);
	PartsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PartsMesh->SetCanEverAffectNavigation(false);
	PartsMesh->RegisterComponent();
	if (UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this))
	{
		PartsMaterial = Art->MakeMaterial(this, FPeregonLook(EPeregonTex::Paint, FLinearColor::White, 0.6f, 0.3f));
	}
	bPartsDirty = true;

	// Ручки стеклоподъёмников: розетка, рычаг и вращающаяся рукоятка; крутятся вместе со стеклом.
	Cranks.Reset();
	const vmodel::Model& CarModel = PeregonCarModelData();
	for (int32 K = 0; K < 4; ++K)
	{
		FPeregonMovingPart* Door = FindMovingPart(SeatDoor(K));
		if (!Door || !Door->Pivot)
		{
			Cranks.Add(nullptr);
			continue;
		}
		const FVector Center(CarModel.WindowCrank[K].X, CarModel.WindowCrank[K].Y, CarModel.WindowCrank[K].Z);
		const FVector Axis(CarModel.WindowCrankAxis[K].X, CarModel.WindowCrankAxis[K].Y, CarModel.WindowCrankAxis[K].Z);
		FPeregonMeshBuffers Crank;
		const FColor Chrome = FLinearColor(0.8f, 0.8f, 0.82f).ToFColor(false);
		const FColor Dark = FLinearColor(0.05f, 0.048f, 0.045f).ToFColor(false);
		// Локально: ось вращения — +X (смотрит в салон), рычаг — вдоль +Y.
		Crank.AddBox(FVector(0.f, 0.f, -1.5f), 0.f, FVector(0.8f, 3.f, 3.f), FVector2D(5.f, 5.f), Chrome, true, true);   // розетка
		Crank.AddBox(FVector(0.7f, 2.6f, -0.45f), 0.f, FVector(0.5f, 6.f, 0.9f), FVector2D(5.f, 5.f), Chrome, true, true); // рычаг
		Crank.AddBox(FVector(2.1f, 5.4f, -0.9f), 0.f, FVector(2.8f, 1.8f, 1.8f), FVector2D(5.f, 5.f), Dark, true, true);   // рукоятка
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Crank%d"), K));
		Mesh->SetupAttachment(Door->Pivot);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->RegisterComponent();
		Mesh->CreateMeshSection(0, Crank.Vertices, Crank.Triangles, Crank.Normals, Crank.UVs, Crank.Colors, TArray<FProcMeshTangent>(), false);
		if (PartsMaterial)
		{
			Mesh->SetMaterial(0, PartsMaterial);
		}
		// Дверь — в своей системе (от петли), стоит закрытой: переводим точку кузова в систему двери.
		const FTransform DoorRest = Door->Pivot->GetRelativeTransform();
		Mesh->SetRelativeLocation(DoorRest.InverseTransformPosition(Center));
		CrankBase[K] = DoorRest.GetRotation().Inverse() * FRotationMatrix::MakeFromX(Axis).ToQuat();
		Mesh->SetRelativeRotation(CrankBase[K]);
		Cranks.Add(Mesh);
	}
}

void APeregonCar::RebuildPartsMesh()
{
	using namespace PeregonCarParts;
	bPartsDirty = false;
	if (!PartsMesh)
	{
		return;
	}
	FPeregonMeshBuffers Buf;
	for (int32 I = 0; I < vsim::parts::SlotCount; ++I)
	{
		const vsim::parts::SlotRecord& R = PartsState.Slots[static_cast<size_t>(I)];
		if (!R.bInstalled || !Places[I].bVisible)
		{
			continue;
		}
		// Настоящая модель узла (PeregonPartMeshes): клеммы, рёбра, шкивы, провода — цвет тускнеет с износом.
		PeregonPartMeshes::BuildInstalled(I, Buf, FTransform(FRotator(0.f, Places[I].Yaw, 0.f), Places[I].Base), static_cast<float>(R.Part.Condition));
	}
	// Что лежит в багажнике — видно, когда его открыть: вещи рядами у спинки заднего сиденья, дальше — ярусами.
	{
		float Y = -44.f;
		float Z = -19.5f;
		float RowDepth = 0.f;
		float X = -114.f;
		for (const FPeregonItemStack& Stack : TrunkStore.Slots)
		{
			const FPeregonItemDef* Def = PeregonItems::Find(Stack.Id);
			if (!Def || Stack.Count <= 0)
			{
				continue;
			}
			const FVector3f Size(Def->SizeCm);
			if (Y + Size.Y > 44.f)
			{
				// Ряд кончился — следующий ряд ближе к крышке, потом — ярусом выше.
				Y = -44.f;
				X -= RowDepth + 2.f;
				RowDepth = 0.f;
				if (X < -134.f)
				{
					X = -114.f;
					Z += 14.f;
				}
			}
			const FVector Base(X - Size.X * 0.5f, Y + Size.Y * 0.5f, Z);
			if (PeregonPartMeshes::Has(Stack.Id))
			{
				PeregonPartMeshes::Build(Stack.Id, Buf, FTransform(Base), Stack.Condition);
			}
			else
			{
				Buf.AddBox(Base, 0.f, FVector(Size), FVector2D(20.f, 20.f), Def->Color.ToFColor(false), true, true);
			}
			Y += Size.Y + 2.f;
			RowDepth = FMath::Max(RowDepth, Size.X);
		}
	}

	// Брызговики за задними колёсами — всегда.
	{
		const FColor Rubber = FLinearColor(0.035f, 0.035f, 0.04f).ToFColor(false);
		const FColor Chrome = FLinearColor(0.75f, 0.76f, 0.78f).ToFColor(false);
		for (float Y : {-66.f, 66.f})
		{
			Buf.AddBox(FVector(-160.f, Y, -34.f), 0.f, FVector(0.8f, 20.f, 22.f), FVector2D(20.f, 20.f), Rubber, true, true);
			Buf.AddBox(FVector(-160.f, Y, -12.5f), 0.f, FVector(1.5f, 22.f, 1.5f), FVector2D(20.f, 20.f), Chrome, true, true);
		}
	}
	PartsMesh->ClearAllMeshSections();
	if (Buf.Vertices.Num() > 0)
	{
		PartsMesh->CreateMeshSection(0, Buf.Vertices, Buf.Triangles, Buf.Normals, Buf.UVs, Buf.Colors, TArray<FProcMeshTangent>(), false);
		if (PartsMaterial)
		{
			PartsMesh->SetMaterial(0, PartsMaterial);
		}
	}
}

int32 APeregonCar::PanelOf(const UPrimitiveComponent* Part) const
{
	using vsim::parts::Panel;
	if (!Part)
	{
		return -1;
	}
	const int32 Box = PeregonCarParts::FindBox(PanelBoxes, Part);
	if (Box != INDEX_NONE)
	{
		return static_cast<int32>(PeregonCarParts::PanelPlaces[Box].Which);
	}
	struct FTagPanel
	{
		const TCHAR* Tag;
		Panel Which;
	};
	static const FTagPanel TagPanels[] = {{TEXT("Hood"), Panel::Hood}, {TEXT("Trunk"), Panel::Trunk}, {TEXT("DoorFL"), Panel::DoorFL},
		{TEXT("DoorFR"), Panel::DoorFR}, {TEXT("DoorRL"), Panel::DoorRL}, {TEXT("DoorRR"), Panel::DoorRR}};
	for (const FTagPanel& T : TagPanels)
	{
		if (Part->ComponentHasTag(FName(T.Tag)))
		{
			return static_cast<int32>(T.Which);
		}
	}
	return -1;
}

namespace PeregonCarParts
{
// Какую кузовную работу сделает то, что в руках. Grinder — по состоянию панели: срезать, зачистить или ошкурить.
bool PickBodyOp(const FPeregonItemDef* Held, const vsim::parts::PanelState& P, const APeregonPlayerController* PC, vsim::parts::BodyOp& OutOp,
	FString& OutProblem)
{
	using vsim::parts::BodyOp;
	using vsim::parts::Consumable;
	if (!Held)
	{
		return false;
	}
	if (Held->Kind == EPeregonItemKind::Tool && Held->ToolKind == static_cast<int32>(vsim::parts::Tool::Grinder))
	{
		const BodyOp Order[] = {BodyOp::CutRot, BodyOp::Sand, BodyOp::Grind};
		const BodyOp Next = vsim::parts::NextStep(P);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (BodyOp Op : Order)
			{
				if ((Pass == 0) != (Op == Next) || !vsim::parts::CanApply(P, Op).bOk)
				{
					continue;
				}
				const vsim::parts::OpNeeds Need = vsim::parts::Needs(Op);
				if (!PC || !PC->HasConsumable(static_cast<int32>(Need.Use)))
				{
					OutProblem = FString::Printf(TEXT("Болгарка: %s — нужен «%s» в карманах"), *U(vsim::parts::OpName(Op)),
						*PeregonItems::DisplayName(PeregonItems::ConsumableItem(static_cast<int32>(Need.Use))));
					continue;
				}
				OutOp = Op;
				return true;
			}
		}
		if (OutProblem.IsEmpty())
		{
			OutProblem = TEXT("Болгаркой здесь делать нечего");
		}
		return false;
	}
	if (Held->Kind != EPeregonItemKind::Consumable)
	{
		return false;
	}
	BodyOp Op = BodyOp::Count;
	switch (static_cast<Consumable>(Held->ConsumableKind))
	{
	case Consumable::Putty: Op = BodyOp::Putty; break;
	case Consumable::Primer: Op = BodyOp::Prime; break;
	case Consumable::Paint: Op = BodyOp::PaintIt; break;
	default:
		OutProblem = TEXT("Диски ставят на болгарку — возьмите её в руки");
		return false;
	}
	const vsim::parts::Verdict V = vsim::parts::CanApply(P, Op);
	if (!V.bOk)
	{
		OutProblem = FString::Printf(TEXT("%s: %s"), *U(vsim::parts::OpName(Op)), *U(V.Reason));
		return false;
	}
	OutOp = Op;
	return true;
}
} // namespace PeregonCarParts

FString APeregonCar::PartsPrompt(const UPrimitiveComponent* Part, const APawn* User) const
{
	using namespace PeregonCarParts;
	if (!Part)
	{
		return FString();
	}
	if (Job.bActive && Job.Part.Get() == Part)
	{
		return FString::Printf(TEXT("%s  %s %d%%"), *Job.Label, *ProgressBar(Job.Elapsed / FMath::Max(0.01f, Job.Duration)),
			FMath::RoundToInt(100.f * Job.Elapsed / FMath::Max(0.01f, Job.Duration)));
	}
	const APeregonPlayerController* PC = ControllerOf(User);
	const FPeregonItemStack* Held = PC ? PC->GetHeld() : nullptr;
	const FPeregonItemDef* HeldDef = Held ? PeregonItems::Find(Held->Id) : nullptr;
	const bool bHood = IsHoodOpen();

	// Узлы.
	const int32 SlotIndex = FindBox(SlotBoxes, Part);
	if (SlotIndex != INDEX_NONE)
	{
		const vsim::parts::SlotInfo& Info = vsim::parts::Info(static_cast<Slot>(SlotIndex));
		if (Info.bUnderHood && !bHood)
		{
			return FString();
		}
		const FString Name = U(Info.Name);
		const FString ToolName = Info.NeedTool == vsim::parts::Tool::None ? FString(TEXT("руками"))
																		: PeregonItems::DisplayName(PeregonItems::ToolItem(static_cast<int32>(Info.NeedTool)));
		const bool bTool = PC && PC->HasTool(static_cast<int32>(Info.NeedTool));
		if (HeldDef && HeldDef->Kind == EPeregonItemKind::Part && HeldDef->PartSlot == SlotIndex)
		{
			const vsim::parts::Verdict V = vsim::parts::CanInstall(PartsState, ToPart(*Held, SlotIndex), bTool, bHood);
			if (V.bOk)
			{
				return FString::Printf(TEXT("Поставить: %s — держать E (%s)"), *PeregonItems::StackLabel(*Held), *ToolName);
			}
			const FString Missing = bTool ? FString() : FString::Printf(TEXT(" (%s)"), *ToolName);
			return FString::Printf(TEXT("%s: %s%s"), *Name, *U(V.Reason), *Missing);
		}
		if (const vsim::parts::PartItem* In = PartsState.Get(static_cast<Slot>(SlotIndex)))
		{
			FString Extra;
			if (static_cast<Slot>(SlotIndex) == Slot::Battery)
			{
				Extra = FString::Printf(TEXT(", заряд %s"), *Percent(PartsState.BatteryCharge));
			}
			return FString::Printf(TEXT("%s · %s%s — держать E: снять (%s%s)"), *Name, *Percent(In->Condition), *Extra, *ToolName,
				bTool ? TEXT("") : TEXT(" — нет в карманах"));
		}
		return FString::Printf(TEXT("%s: пусто — возьмите деталь в руки (1–0)"), *Name);
	}

	// Масло и антифриз.
	const int32 FillIndex = FindBox(FillBoxes, Part);
	if (FillIndex != INDEX_NONE)
	{
		if (!bHood)
		{
			return FString();
		}
		const bool bOil = FillIndex == 0;
		const double Level = bOil ? PartsState.OilL : PartsState.CoolantL;
		const double Norm = bOil ? OilNorm : CoolantNorm;
		const FName Need = bOil ? PeregonItems::Oil : PeregonItems::Coolant;
		const FString What = FString::Printf(TEXT("%s: %.1f из %.2f л"), bOil ? TEXT("Масло") : TEXT("Антифриз"), Level, Norm);
		if (Held && Held->Id == Need)
		{
			return Level >= Norm - 0.05 ? What + TEXT(" — уже норма") : What + TEXT(" — держать E: долить");
		}
		return What + (bOil ? TEXT(" · возьмите в руки масло") : TEXT(" · возьмите в руки антифриз"));
	}

	// Кузов.
	const int32 PanelIndex = PanelOf(Part);
	if (PanelIndex >= 0)
	{
		const vsim::parts::PanelState& P = PartsState.Panels[static_cast<size_t>(PanelIndex)];
		const FString Name = U(vsim::parts::PanelName(static_cast<Panel>(PanelIndex)));
		vsim::parts::BodyOp Op = vsim::parts::BodyOp::Count;
		FString Problem;
		if (PickBodyOp(HeldDef, P, PC, Op, Problem))
		{
			return FString::Printf(TEXT("%s (%s) — держать E: %s"), *Name, *PanelStatus(P), *U(vsim::parts::OpName(Op)).ToLower());
		}
		if (!Problem.IsEmpty())
		{
			return FString::Printf(TEXT("%s (%s) · %s"), *Name, *PanelStatus(P), *Problem);
		}
		// Без инструмента — осмотр (у дверей, капота и багажника своя подсказка «открыть»).
		if ((FindBox(PanelBoxes, Part) != INDEX_NONE))
		{
			return FString::Printf(TEXT("%s: %s"), *Name, *PanelStatus(P));
		}
	}
	return FString();
}

void APeregonCar::StartJob(const FString& Label, float Seconds, UPrimitiveComponent* Part, APawn* User, TFunction<void()> Finish)
{
	Job.bActive = true;
	Job.Label = Label;
	Job.Elapsed = 0.f;
	Job.Duration = FMath::Max(0.2f, Seconds);
	Job.Part = Part;
	Job.User = User;
	Job.Finish = MoveTemp(Finish);
}

bool APeregonCar::PartsInteract(UPrimitiveComponent* Part, APawn* User)
{
	using namespace PeregonCarParts;
	APeregonPlayerController* PC = ControllerOf(User);
	if (!Part || !PC || Job.bActive)
	{
		return Job.bActive;
	}
	const FPeregonItemStack* Held = PC->GetHeld();
	const FPeregonItemDef* HeldDef = Held ? PeregonItems::Find(Held->Id) : nullptr;
	const bool bHood = IsHoodOpen();
	TWeakObjectPtr<APeregonCar> Self(this);
	TWeakObjectPtr<APeregonPlayerController> WeakPC(PC);

	const int32 SlotIndex = FindBox(SlotBoxes, Part);
	if (SlotIndex != INDEX_NONE)
	{
		const Slot S = static_cast<Slot>(SlotIndex);
		const vsim::parts::SlotInfo& Info = vsim::parts::Info(S);
		if (Info.bUnderHood && !bHood)
		{
			return false;
		}
		const bool bTool = PC->HasTool(static_cast<int32>(Info.NeedTool));
		const float Seconds = WorkSeconds(Info.Seconds);
		if (HeldDef && HeldDef->Kind == EPeregonItemKind::Part && HeldDef->PartSlot == SlotIndex)
		{
			const vsim::parts::Verdict V = vsim::parts::CanInstall(PartsState, ToPart(*Held, SlotIndex), bTool, bHood);
			if (!V.bOk)
			{
				PC->Toast(U(V.Reason));
				return true;
			}
			StartJob(FString::Printf(TEXT("Ставим: %s"), *U(Info.Name)), Seconds, Part, User, [Self, WeakPC, SlotIndex]() {
				APeregonCar* Car = Self.Get();
				APeregonPlayerController* P = WeakPC.Get();
				FPeregonItemStack Item;
				if (!Car || !P || !P->TakeHeld(Item) || PeregonItems::Find(Item.Id) == nullptr || PeregonItems::Find(Item.Id)->PartSlot != SlotIndex)
				{
					return;
				}
				vsim::parts::Install(Car->PartsState, PeregonCarParts::ToPart(Item, SlotIndex));
				Car->bPartsDirty = true;
				PeregonAudio::PlayEvent(Car, TEXT("door.close"), Car->SlotBoxes[SlotIndex]->GetComponentLocation(), 0.35f, 1.6f);
				P->Toast(FString::Printf(TEXT("Поставлено: %s"), *PeregonItems::StackLabel(Item)));
				if (UPeregonGameInstance* GI = Car->GetWorld()->GetGameInstance<UPeregonGameInstance>())
				{
					++GI->Journal.PartsInstalled;
				}
			});
			return true;
		}
		if (PartsState.Has(S))
		{
			const vsim::parts::Verdict V = vsim::parts::CanRemove(PartsState, S, bTool, bHood);
			if (!V.bOk)
			{
				PC->Toast(V.Reason[0] ? U(V.Reason) : FString(TEXT("Нельзя")));
				return true;
			}
			StartJob(FString::Printf(TEXT("Снимаем: %s"), *U(Info.Name)), Seconds, Part, User, [Self, WeakPC, S]() {
				APeregonCar* Car = Self.Get();
				APeregonPlayerController* P = WeakPC.Get();
				if (!Car || !P || !Car->PartsState.Has(S))
				{
					return;
				}
				const FPeregonItemStack Item = PeregonCarParts::ToStack(vsim::parts::Remove(Car->PartsState, S));
				Car->bPartsDirty = true;
				P->GiveOrDrop(Item);
				if (Car->Movement && (S == Slot::SparkPlugs || S == Slot::Carburetor || S == Slot::Distributor || S == Slot::Battery))
				{
					Car->Movement->SetIgnition(false);
				}
			});
			return true;
		}
		PC->Toast(FString::Printf(TEXT("%s: на месте ничего нет. Найдите деталь и возьмите в руки"), *U(Info.Name)));
		return true;
	}

	const int32 FillIndex = FindBox(FillBoxes, Part);
	if (FillIndex != INDEX_NONE)
	{
		if (!bHood)
		{
			return false;
		}
		const bool bOil = FillIndex == 0;
		const FName Need = bOil ? PeregonItems::Oil : PeregonItems::Coolant;
		if (!Held || Held->Id != Need)
		{
			PC->Toast(bOil ? TEXT("Нужна канистра масла в руках") : TEXT("Нужен антифриз в руках"));
			return true;
		}
		StartJob(bOil ? TEXT("Доливаем масло") : TEXT("Заливаем антифриз"), 4.f, Part, User, [Self, WeakPC, bOil]() {
			APeregonCar* Car = Self.Get();
			APeregonPlayerController* P = WeakPC.Get();
			const FPeregonItemStack* H = P ? P->GetHeld() : nullptr;
			if (!Car || !H)
			{
				return;
			}
			double& Level = bOil ? Car->PartsState.OilL : Car->PartsState.CoolantL;
			const double Norm = bOil ? PeregonCarParts::OilNorm : PeregonCarParts::CoolantNorm;
			const double Can = bOil ? PeregonItems::OilCanLiters : PeregonItems::CoolantCanLiters;
			const double Pour = FMath::Min(Norm - Level, static_cast<double>(H->Condition) * Can);
			if (Pour <= 0.01)
			{
				return;
			}
			Level += Pour;
			P->UseHeld(static_cast<float>(Pour / Can));
			P->Toast(FString::Printf(TEXT("Залито %.1f л. Теперь %.1f из %.2f л"), Pour, Level, Norm));
		});
		return true;
	}

	const int32 PanelIndex = PanelOf(Part);
	if (PanelIndex >= 0)
	{
		vsim::parts::BodyOp Op = vsim::parts::BodyOp::Count;
		FString Problem;
		if (!PickBodyOp(HeldDef, PartsState.Panels[static_cast<size_t>(PanelIndex)], PC, Op, Problem))
		{
			if (!Problem.IsEmpty())
			{
				PC->Toast(Problem);
				return true;
			}
			return (FindBox(PanelBoxes, Part) != INDEX_NONE); // осмотр панели; двери и капот откроются как обычно
		}
		const vsim::parts::OpNeeds Need = vsim::parts::Needs(Op);
		const FString Label = FString::Printf(TEXT("%s: %s"), *U(vsim::parts::OpName(Op)), *U(vsim::parts::PanelName(static_cast<Panel>(PanelIndex))).ToLower());
		const bool bGrinder = Need.NeedTool == vsim::parts::Tool::Grinder;
		StartJob(Label, WorkSeconds(Need.Seconds), Part, User, [Self, WeakPC, PanelIndex, Op, Need, bGrinder]() {
			APeregonCar* Car = Self.Get();
			APeregonPlayerController* P = WeakPC.Get();
			if (!Car || !P)
			{
				return;
			}
			vsim::parts::PanelState& State = Car->PartsState.Panels[static_cast<size_t>(PanelIndex)];
			if (!vsim::parts::CanApply(State, Op).bOk)
			{
				return;
			}
			float Color[3] = {0.f, 0.f, 0.f};
			if (bGrinder)
			{
				if (!P->UseConsumable(static_cast<int32>(Need.Use), static_cast<float>(Need.Amount)))
				{
					return;
				}
			}
			else
			{
				const FPeregonItemStack* H = P->GetHeld();
				const FPeregonItemDef* D = H ? PeregonItems::Find(H->Id) : nullptr;
				if (!D || D->ConsumableKind != static_cast<int32>(Need.Use))
				{
					return;
				}
				Color[0] = D->PaintColor.R;
				Color[1] = D->PaintColor.G;
				Color[2] = D->PaintColor.B;
				P->UseHeld(static_cast<float>(Need.Amount));
			}
			vsim::parts::Apply(State, Op, Op == vsim::parts::BodyOp::PaintIt ? Color : nullptr);
			Car->bBodyDirty = true;
			if (UPeregonGameInstance* GI = Car->GetWorld()->GetGameInstance<UPeregonGameInstance>())
			{
				++GI->Journal.BodyJobs;
			}
			const vsim::parts::BodyOp Next = vsim::parts::NextStep(State);
			P->Toast(vsim::parts::PanelScore(State) > 0.95 ? FString(TEXT("Готово — панель как новая"))
															: FString::Printf(TEXT("Готово. Дальше: %s"), *PeregonCarParts::U(vsim::parts::OpName(Next)).ToLower()));
		});
		return true;
	}
	return false;
}

void APeregonCar::PartsInteractStop(UPrimitiveComponent* Part)
{
	if (Job.bActive && (!Part || Job.Part.Get() == Part))
	{
		Job = FPartsJob(); // отпустили — работа прервана
	}
}

void APeregonCar::UpdateParts(float DeltaSeconds, const FPeregonTelemetry& T)
{
	// Работа «держа клавишу»: идёт, пока игрок рядом и смотрит на ту же мишень.
	if (Job.bActive)
	{
		const APawn* User = Job.User.Get();
		const APeregonCharacter* Walker = Cast<APeregonCharacter>(User);
		const bool bLooking = User == this ? Focus.Part == Job.Part : (Walker && Walker->GetFocus().Part == Job.Part);
		if (!User || !Job.Part.IsValid() || !bLooking)
		{
			Job = FPartsJob();
		}
		else
		{
			Job.Elapsed += DeltaSeconds;
			if (FMath::Fmod(Job.Elapsed, 0.9f) < DeltaSeconds)
			{
				PeregonAudio::PlayEvent(this, TEXT("handbrake"), Job.Part->GetComponentLocation(), 0.18f, FMath::FRandRange(1.4f, 2.f));
			}
			if (Job.Elapsed >= Job.Duration)
			{
				TFunction<void()> Finish = MoveTemp(Job.Finish);
				Job = FPartsJob();
				if (Finish)
				{
					Finish();
				}
			}
		}
	}

	// Износ: по пробегу (одометр) и времени; ржавчина — от сырости.
	const double Odo = T.OdometerKm;
	const double Km = LastOdometerKm < 0.0 ? 0.0 : FMath::Max(0.0, Odo - LastOdometerKm);
	LastOdometerKm = Odo;
	if (Controller && Controller->IsPlayerController())
	{
		if (UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>())
		{
			GI->Journal.Km += static_cast<float>(Km);
			GI->Journal.bEngineStarted |= T.bEngineRunning;
		}
	}
	const UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	const double Wet = Art ? Art->GetWetness() : 0.0;
	const double Hours = DeltaSeconds / 60.0; // минута игры ≈ час износа
	const double RustBefore = PartsState.Panels[0].Rust + PartsState.Panels[static_cast<size_t>(vsim::parts::Panel::SillL)].Rust;
	vsim::parts::Wear(PartsState, Km, Hours, Wet, T.bEngineRunning, T.EngineTempC);
	const double RustAfter = PartsState.Panels[0].Rust + PartsState.Panels[static_cast<size_t>(vsim::parts::Panel::SillL)].Rust;
	if (FMath::Abs(RustAfter - RustBefore) > 0.0 && FMath::Fmod(CockpitTime, 20.f) < DeltaSeconds)
	{
		bBodyDirty = true; // ржавчина ползёт медленно — перекрашивать раз в 20 с достаточно
	}

	PartsEffects = vsim::parts::Evaluate(PartsState);
	// Багажник поменялся — перерисовать то, что в нём лежит.
	{
		uint32 Hash = static_cast<uint32>(TrunkStore.Slots.Num());
		for (const FPeregonItemStack& Stack : TrunkStore.Slots)
		{
			Hash = HashCombine(Hash, HashCombine(GetTypeHash(Stack.Id), GetTypeHash(Stack.Count)));
		}
		if (Hash != TrunkHash)
		{
			TrunkHash = Hash;
			bPartsDirty = true;
		}
	}
	// Без масла и антифриза мотор долго не живёт.
	if (PartsState.OilL < 1.5)
	{
		PartsEffects.EngineWearFactor *= 1.0 + 6.0 * (1.5 - PartsState.OilL);
	}
	if (Movement)
	{
		Movement->ApplyPartEffects(PartsEffects);
	}
	if (bPartsDirty)
	{
		RebuildPartsMesh();
	}

	// Фары и магнитола без тока или без детали гаснут.
	if (HeadlightL)
	{
		const bool bPower = PartsState.BatteryCharge > 0.03 || (T.bEngineRunning && PartsEffects.ChargeRate > 0.1);
		const bool bShould = bHeadlights && PartsEffects.bHeadlights && bPower;
		if (HeadlightL->IsVisible() != bShould)
		{
			SetHeadlights(bHeadlights);
		}
	}
	if (Radio && Radio->IsPowered() && (!PartsEffects.bRadio || PartsState.BatteryCharge < 0.03))
	{
		Radio->SetPower(false);
	}
	PartsToastCooldown = FMath::Max(0.f, PartsToastCooldown - DeltaSeconds);
	if (Controller && bKeyCranking && PartsToastCooldown <= 0.f)
	{
		APeregonGameMode* GM = GetWorld()->GetAuthGameMode<APeregonGameMode>();
		FString Why;
		if (PartsEffects.StarterFactor < 0.05)
		{
			Why = PartsState.Has(vsim::parts::Slot::Battery) ? TEXT("Стартер не крутит: сел аккумулятор") : TEXT("Тишина. Аккумулятора нет");
		}
		else if (!PartsEffects.bSpark)
		{
			Why = TEXT("Крутит, но не схватывает — нет искры (свечи? трамблёр?)");
		}
		else if (!PartsEffects.bFuel)
		{
			Why = TEXT("Крутит, но не схватывает — не идёт бензин (карбюратор? фильтр?)");
		}
		if (GM && !Why.IsEmpty())
		{
			GM->Toast(Why, 2.5f);
			PartsToastCooldown = 3.f;
		}
	}
}
