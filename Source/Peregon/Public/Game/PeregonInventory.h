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
};

namespace PeregonItems
{
extern PEREGON_API const FName MapNovosibirsk;
extern PEREGON_API const FName MapKemerovo;
extern PEREGON_API const FName CanisterFull;
extern PEREGON_API const FName CanisterEmpty;
extern PEREGON_API const FName Documents;
extern PEREGON_API const FName Thermos;

constexpr float CanisterLiters = 20.f;

PEREGON_API const TArray<FPeregonItemDef>& All();
PEREGON_API const FPeregonItemDef* Find(FName Id);
// Карта области маршрута по её индексу (rgen::Route::Regions()).
PEREGON_API FName MapForRegion(int32 Region);
PEREGON_API FString DisplayName(FName Id);
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
