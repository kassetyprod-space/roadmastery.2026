// Инвентарь: вещи в карманах, в бардачке и в багажнике. Предметы описаны в коде (PeregonItems),
// контейнеры — простые списки стопок, хранятся в GameInstance и попадают в сохранение.
#pragma once

#include "CoreMinimal.h"
#include "PeregonInventory.generated.h"

USTRUCT(BlueprintType)
struct FPeregonItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") int32 Count = 0;
	// Детали: состояние 0..1. Расходники (банки, упаковки дисков, канистры масла): сколько осталось 0..1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") float Condition = 1.f;
	// Детали: 0 — стоковая, 1 — улучшенная, 2 — спортивная.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") int32 Tier = 0;
	// Аккумулятор: заряд 0..1 (−1 — как состояние).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") float Charge = -1.f;
};

// Чем предмет является для механики.
enum class EPeregonItemKind : uint8
{
	Misc,
	Part,       // ставится в машину (PartSlot — vsim::parts::Slot)
	Tool,       // инструмент (ToolKind — vsim::parts::Tool); нужен в карманах или в руках
	Consumable, // расходник кузовных работ (ConsumableKind — vsim::parts::Consumable)
	Fluid       // масло, антифриз
};

// Описание предмета.
struct FPeregonItemDef
{
	FName Id;
	FString Name;
	FString Description;
	float Price = 0.f;
	int32 MaxStack = 1;
	int32 Region = -1;        // для карт — индекс области маршрута
	bool bUsable = false;
	bool bQuest = false;      // нельзя выбросить/продать
	FLinearColor Color = FLinearColor::White;
	EPeregonItemKind Kind = EPeregonItemKind::Misc;
	int32 PartSlot = -1;
	int32 ToolKind = 0;
	int32 ConsumableKind = 0;
	FLinearColor PaintColor = FLinearColor::White; // для краски
	FVector SizeCm = FVector(20.f, 15.f, 10.f);    // габарит предмета, когда он лежит на земле или в руках
};

namespace PeregonItems
{
extern PEREGON_API const FName MapNovosibirsk;
extern PEREGON_API const FName MapKemerovo;
extern PEREGON_API const FName CanisterFull;
extern PEREGON_API const FName CanisterEmpty;
extern PEREGON_API const FName Documents;
extern PEREGON_API const FName Thermos;

extern PEREGON_API const FName Oil;
extern PEREGON_API const FName Manual;
extern PEREGON_API const FName Coolant;

constexpr float CanisterLiters = 20.f;
constexpr float OilCanLiters = 4.f;
constexpr float CoolantCanLiters = 5.f;

PEREGON_API const TArray<FPeregonItemDef>& All();
PEREGON_API const FPeregonItemDef* Find(FName Id);
// Карта области маршрута по её индексу (rgen::Route::Regions()).
PEREGON_API FName MapForRegion(int32 Region);
PEREGON_API FString DisplayName(FName Id);
// «Аккумулятор · 40%» — имя с состоянием или остатком.
PEREGON_API FString StackLabel(const FPeregonItemStack& Stack);
// Предметы механики по номерам vsim::parts.
PEREGON_API FName PartItem(int32 Slot);
PEREGON_API FName ToolItem(int32 Tool);
PEREGON_API FName ConsumableItem(int32 Consumable); // для краски — красная
// Все краски (ассортимент магазина).
PEREGON_API const TArray<FName>& Paints();
// Ассортимент фирменного магазина запчастей и лавки на разборке.
PEREGON_API TArray<FName> PartsShopStock();
// Цена с учётом состояния и уровня (б/у дешевле).
PEREGON_API float PriceOf(const FPeregonItemStack& Stack);
} // namespace PeregonItems

USTRUCT(BlueprintType)
struct FPeregonContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") int32 Capacity = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory") TArray<FPeregonItemStack> Slots;

	int32 Count(FName Id) const
	{
		int32 N = 0;
		for (const FPeregonItemStack& S : Slots)
		{
			N += S.Id == Id ? S.Count : 0;
		}
		return N;
	}

	bool Has(FName Id) const { return Count(Id) > 0; }

	bool CanAdd(FName Id, int32 N = 1) const
	{
		const FPeregonItemDef* Def = PeregonItems::Find(Id);
		const int32 MaxStack = Def ? FMath::Max(1, Def->MaxStack) : 1;
		int32 Room = 0;
		for (const FPeregonItemStack& S : Slots)
		{
			Room += S.Id == Id ? FMath::Max(0, MaxStack - S.Count) : 0;
		}
		Room += FMath::Max(0, Capacity - Slots.Num()) * MaxStack;
		return Room >= N;
	}

	bool Add(FName Id, int32 N = 1)
	{
		if (N <= 0 || !CanAdd(Id, N))
		{
			return false;
		}
		const FPeregonItemDef* Def = PeregonItems::Find(Id);
		const int32 MaxStack = Def ? FMath::Max(1, Def->MaxStack) : 1;
		for (FPeregonItemStack& S : Slots)
		{
			if (S.Id == Id && S.Count < MaxStack)
			{
				const int32 Put = FMath::Min(N, MaxStack - S.Count);
				S.Count += Put;
				N -= Put;
			}
		}
		while (N > 0)
		{
			FPeregonItemStack& S = Slots.AddDefaulted_GetRef();
			S.Id = Id;
			S.Count = FMath::Min(N, MaxStack);
			N -= S.Count;
		}
		return true;
	}

	// Положить стопку с её состоянием. Детали и расходники с разным состоянием не смешиваются.
	bool AddStack(const FPeregonItemStack& Stack)
	{
		const FPeregonItemDef* Def = PeregonItems::Find(Stack.Id);
		const int32 MaxStack = Def ? FMath::Max(1, Def->MaxStack) : 1;
		if (MaxStack > 1 && FMath::IsNearlyEqual(Stack.Condition, 1.f) && Stack.Tier == 0)
		{
			return Add(Stack.Id, Stack.Count);
		}
		if (Slots.Num() >= Capacity || Stack.Count <= 0)
		{
			return false;
		}
		Slots.Add(Stack);
		return true;
	}

	bool CanAddStack(const FPeregonItemStack& Stack) const
	{
		const FPeregonItemDef* Def = PeregonItems::Find(Stack.Id);
		const int32 MaxStack = Def ? FMath::Max(1, Def->MaxStack) : 1;
		if (MaxStack > 1 && FMath::IsNearlyEqual(Stack.Condition, 1.f) && Stack.Tier == 0)
		{
			return CanAdd(Stack.Id, Stack.Count);
		}
		return Slots.Num() < Capacity;
	}

	// Взять одну штуку из ячейки (с её состоянием).
	bool TakeAt(int32 Index, FPeregonItemStack& Out)
	{
		if (!Slots.IsValidIndex(Index) || Slots[Index].Count <= 0)
		{
			return false;
		}
		Out = Slots[Index];
		Out.Count = 1;
		if (--Slots[Index].Count <= 0)
		{
			Slots.RemoveAt(Index);
		}
		return true;
	}

	// Ячейка с этим предметом в лучшем состоянии (или с наибольшим остатком). −1 — нет.
	int32 Best(FName Id) const
	{
		int32 Found = -1;
		for (int32 I = 0; I < Slots.Num(); ++I)
		{
			if (Slots[I].Id == Id && (Found < 0 || Slots[I].Condition > Slots[Found].Condition))
			{
				Found = I;
			}
		}
		return Found;
	}

	bool Remove(FName Id, int32 N = 1)
	{
		if (Count(Id) < N)
		{
			return false;
		}
		for (int32 I = Slots.Num() - 1; I >= 0 && N > 0; --I)
		{
			if (Slots[I].Id == Id)
			{
				const int32 Take = FMath::Min(N, Slots[I].Count);
				Slots[I].Count -= Take;
				N -= Take;
				if (Slots[I].Count <= 0)
				{
					Slots.RemoveAt(I);
				}
			}
		}
		return true;
	}
};

// Какой контейнер открыт рядом с карманами.
enum class EPeregonStorage : uint8
{
	None,
	Glovebox,
	Trunk
};
