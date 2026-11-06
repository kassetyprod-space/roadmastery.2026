#include "Game/PeregonInventory.h"

namespace PeregonItems
{
const FName MapNovosibirsk(TEXT("map_nso"));
const FName MapKemerovo(TEXT("map_kem"));
const FName CanisterFull(TEXT("canister_full"));
const FName CanisterEmpty(TEXT("canister_empty"));
const FName Documents(TEXT("documents"));
const FName Thermos(TEXT("thermos"));

const TArray<FPeregonItemDef>& All()
{
	static const TArray<FPeregonItemDef> Items = []() {
		TArray<FPeregonItemDef> Out;
		auto Add = [&Out](FName Id, const TCHAR* Name, const TCHAR* Desc, float Price, int32 MaxStack, int32 Region, bool bUsable, bool bQuest,
					   const FLinearColor& Color) {
			FPeregonItemDef D;
			D.Id = Id;
			D.Name = Name;
			D.Description = Desc;
			D.Price = Price;
			D.MaxStack = MaxStack;
			D.Region = Region;
			D.bUsable = bUsable;
			D.bQuest = bQuest;
			D.Color = Color;
			Out.Add(D);
		};
		Add(MapNovosibirsk, TEXT("Карта Новосибирской области"), TEXT("Дорожная карта, 1:500 000. Р-255 от Новосибирска до границы Кузбасса."),
			180.f, 1, 0, true, false, FLinearColor(0.85f, 0.75f, 0.5f));
		Add(MapKemerovo, TEXT("Карта Кемеровской области"), TEXT("Дорожная карта Кузбасса, 1:500 000. Р-255 от границы до Кемерова."), 180.f, 1, 1,
			true, false, FLinearColor(0.7f, 0.8f, 0.55f));
		Add(CanisterFull, TEXT("Канистра 20 л (бензин)"), TEXT("Залить в бак: подойдите к лючку бензобака."), 1490.f, 1, -1, true, false,
			FLinearColor(0.75f, 0.2f, 0.12f));
		Add(CanisterEmpty, TEXT("Канистра 20 л (пустая)"), TEXT("Наполнить можно у колонки на АЗС."), 450.f, 1, -1, false, false,
			FLinearColor(0.45f, 0.18f, 0.12f));
		Add(Documents, TEXT("Документы на перегон"), TEXT("Сокол-1500, перегон Новосибирск — Владивосток. Без них не сдать машину."), 0.f, 1, -1,
			true, true, FLinearColor(0.8f, 0.8f, 0.75f));
		Add(Thermos, TEXT("Термос с чаем"), TEXT("Горячий чай с чабрецом. Согревает и бодрит в дороге."), 120.f, 2, -1, true, false,
			FLinearColor(0.3f, 0.35f, 0.4f));
		return Out;
	}();
	return Items;
}

const FPeregonItemDef* Find(FName Id)
{
	return All().FindByPredicate([Id](const FPeregonItemDef& D) { return D.Id == Id; });
}

FName MapForRegion(int32 Region)
{
	const FPeregonItemDef* Def = All().FindByPredicate([Region](const FPeregonItemDef& D) { return D.Region == Region; });
	return Def ? Def->Id : NAME_None;
}

FString DisplayName(FName Id)
{
	const FPeregonItemDef* Def = Find(Id);
	return Def ? Def->Name : Id.ToString();
}
} // namespace PeregonItems
