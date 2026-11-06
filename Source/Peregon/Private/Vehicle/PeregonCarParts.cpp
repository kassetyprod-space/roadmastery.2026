// Узлы, детали и кузовные работы «Сокола»: что стоит под капотом и как это снять, поставить, починить.
// Логика деталей — vsim::parts (чистый C++, покрыт тестами); здесь — мишени, подсказки, работа «держа клавишу»,
// видимые узлы в моторном отсеке и влияние на физику.
#include "Vehicle/PeregonCar.h"

#include "Art/PeregonArtSubsystem.h"
#include "Audio/PeregonAudio.h"
#include "Audio/PeregonRadio.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Game/PeregonGameMode.h"
#include "Game/PeregonPlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/PeregonCharacter.h"
#include "ProceduralMeshComponent.h"
#include "Vehicle/PeregonVehicleMovementComponent.h"
#include "World/PeregonKit.h"
#include "World/PeregonSky.h"

namespace PeregonCarParts
{
using vsim::parts::Panel;
using vsim::parts::Slot;

// Где стоит узел (основание, см, в системе кузова) и какого он размера — для мишени и видимой коробки.
struct FSlotPlace
{
	FVector Base;
	FVector Size;
	bool bVisible; // рисовать коробку (то, что внутри салона и фары, уже есть в модели)
};
const FSlotPlace Places[vsim::parts::SlotCount] = {
	{FVector(170.f, 48.f, 10.f), FVector(28.f, 17.f, 20.f), true},   // аккумулятор
	{FVector(130.f, -10.f, 28.f), FVector(14.f, 30.f, 4.f), true},   // свечи
	{FVector(108.f, 16.f, 20.f), FVector(10.f, 10.f, 16.f), true},   // трамблёр
	{FVector(120.f, 4.f, 22.f), FVector(18.f, 16.f, 12.f), true},    // карбюратор
	{FVector(120.f, 4.f, 34.f), FVector(28.f, 28.f, 5.f), true},     // воздушный фильтр
	{FVector(104.f, 34.f, 24.f), FVector(8.f, 4.f, 4.f), true},      // топливный фильтр
	{FVector(112.f, -30.f, 0.f), FVector(9.f, 9.f, 12.f), true},     // масляный фильтр
	{FVector(190.f, 0.f, -5.f), FVector(6.f, 60.f, 40.f), true},     // радиатор
	{FVector(170.f, 0.f, 8.f), FVector(14.f, 14.f, 12.f), true},     // помпа
	{FVector(180.f, -25.f, 24.f), FVector(10.f, 36.f, 7.f), true},   // патрубки
	{FVector(160.f, -34.f, 10.f), FVector(16.f, 16.f, 16.f), true},  // генератор
	{FVector(98.f, -28.f, -12.f), FVector(26.f, 12.f, 12.f), true},  // стартер
	{FVector(140.f, -40.f, 14.f), FVector(20.f, 18.f, 18.f), true},  // турбина
	{FVector(-150.f, -30.f, -32.f), FVector(80.f, 18.f, 12.f), true}, // глушитель
	{FVector(116.f, -66.f, -24.f), FVector(16.f, 16.f, 30.f), false}, // передняя подвеска (у левого переднего колеса)
	{FVector(-126.f, -66.f, -24.f), FVector(16.f, 16.f, 30.f), false}, // задняя подвеска
	{FVector(116.f, 66.f, -24.f), FVector(16.f, 16.f, 30.f), false},  // колодки (у правого переднего)
	{FVector(201.f, 0.f, 14.f), FVector(6.f, 140.f, 16.f), false},    // фары
	{FVector(-203.f, 0.f, 17.f), FVector(6.f, 140.f, 16.f), false},   // фонари
	{FVector(73.f, -35.f, 37.f), FVector(4.f, 20.f, 9.f), false},     // щиток приборов
	{FVector(73.f, 0.f, 21.f), FVector(6.f, 18.f, 4.f), false},       // магнитола (под корпусом, мишень — нижняя кромка)
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
const FVector FillPlaces[2] = {FVector(125.f, -12.f, 36.f), FVector(172.f, 40.f, 30.f)};
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
		SlotBoxes.Add(MakeBox(FString::Printf(TEXT("PartSlot%d"), I), P.Base + FVector(0.f, 0.f, P.Size.Z * 0.5f), P.Size * 0.5f + FVector(1.5f),
			FName(TEXT("PartSlot"))));
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
		const FPeregonItemDef* Def = PeregonItems::Find(PeregonItems::PartItem(I));
		FLinearColor C = Def ? Def->Color : FLinearColor::Gray;
		// Старая деталь — тусклая и рыжая.
		const float Worn = 1.f - static_cast<float>(R.Part.Condition);
		C = FMath::Lerp(C, FLinearColor(0.3f, 0.17f, 0.08f), Worn * 0.55f) * (1.f - 0.35f * Worn);
		const FVector& Size = Places[I].Size;
		Buf.AddBox(Places[I].Base, 0.f, Size, FVector2D(20.f, 20.f), C.ToFColor(false), true, true);
		// Немного «деталировки»: клеммы аккумулятора, крышка трамблёра, горловина фильтра.
		if (static_cast<Slot>(I) == Slot::Battery)
		{
			for (float Y : {-5.f, 5.f})
			{
				Buf.AddBox(Places[I].Base + FVector(-8.f, Y, Size.Z), 0.f, FVector(3.f, 3.f, 2.5f), FVector2D(10.f, 10.f),
					(Y < 0.f ? FLinearColor(0.6f, 0.1f, 0.08f) : FLinearColor(0.1f, 0.1f, 0.12f)).ToFColor(false), true, false);
			}
		}
		else if (static_cast<Slot>(I) == Slot::Turbo)
		{
			Buf.AddBox(Places[I].Base + FVector(0.f, 9.f, Size.Z * 0.3f), 0.f, FVector(8.f, 12.f, 8.f), FVector2D(10.f, 10.f),
				FLinearColor(0.35f, 0.33f, 0.3f).ToFColor(false), true, true);
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
