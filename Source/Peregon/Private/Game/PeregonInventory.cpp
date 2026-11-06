#include "Game/PeregonInventory.h"

#include "vsim/CarParts.h"

namespace PeregonItems
{
const FName MapNovosibirsk(TEXT("map_nso"));
const FName MapKemerovo(TEXT("map_kem"));
const FName CanisterFull(TEXT("canister_full"));
const FName CanisterEmpty(TEXT("canister_empty"));
const FName Documents(TEXT("documents"));
const FName Thermos(TEXT("thermos"));
const FName Oil(TEXT("oil"));
const FName Manual(TEXT("manual"));
const FName Coolant(TEXT("coolant"));

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
		Add(Manual, TEXT("Техничка «Сокол-1500»"), TEXT("Руководство по эксплуатации с пометками карандашом. Откройте рядом с машиной — подскажет, чего не хватает."),
			0.f, 1, -1, true, false, FLinearColor(0.75f, 0.6f, 0.3f));
		Add(Thermos, TEXT("Термос с чаем"), TEXT("Горячий чай с чабрецом. Согревает и бодрит в дороге."), 120.f, 2, -1, true, false,
			FLinearColor(0.3f, 0.35f, 0.4f));
		auto Last = [&Out]() -> FPeregonItemDef& { return Out.Last(); };

		// --- Детали: по одной на каждое место в машине ---
		struct FPartLook
		{
			float Price;
			FLinearColor Color;
			FVector Size;
			const TCHAR* Desc;
		};
		const FPartLook Looks[vsim::parts::SlotCount] = {
			{3200.f, FLinearColor(0.08f, 0.08f, 0.09f), FVector(28, 17, 20), TEXT("6СТ-55. Без заряда стартер не крутит. Ставится справа под капотом, ключи на 10.")},
			{480.f, FLinearColor(0.85f, 0.85f, 0.8f), FVector(12, 8, 4), TEXT("Комплект из четырёх свечей А17ДВР. Нужен свечной ключ.")},
			{1400.f, FLinearColor(0.15f, 0.15f, 0.15f), FVector(10, 10, 18), TEXT("Распределитель зажигания с крышкой и бегунком.")},
			{4200.f, FLinearColor(0.6f, 0.58f, 0.5f), FVector(18, 16, 14), TEXT("«Озон» 2107. Без него мотор не получает бензин.")},
			{260.f, FLinearColor(0.9f, 0.85f, 0.6f), FVector(26, 26, 5), TEXT("Бумажный элемент в круглый корпус над карбюратором.")},
			{120.f, FLinearColor(0.85f, 0.85f, 0.85f), FVector(8, 4, 4), TEXT("Прозрачный фильтр тонкой очистки в шланг бензонасоса.")},
			{240.f, FLinearColor(0.2f, 0.35f, 0.75f), FVector(9, 9, 12), TEXT("Откручивается рукой или ключом. Меняют вместе с маслом.")},
			{3900.f, FLinearColor(0.7f, 0.45f, 0.2f), FVector(60, 6, 40), TEXT("Медный радиатор охлаждения. Старый часто течёт.")},
			{1300.f, FLinearColor(0.45f, 0.45f, 0.45f), FVector(14, 14, 12), TEXT("Водяной насос с крыльчаткой. Гоняет антифриз по мотору.")},
			{650.f, FLinearColor(0.1f, 0.1f, 0.1f), FVector(40, 10, 8), TEXT("Комплект патрубков и хомутов. Нужны пассатижи.")},
			{3600.f, FLinearColor(0.55f, 0.55f, 0.55f), FVector(16, 16, 16), TEXT("Генератор 55 А. Без него аккумулятор садится в дороге.")},
			{2900.f, FLinearColor(0.3f, 0.3f, 0.32f), FVector(26, 12, 12), TEXT("Стартер. Ставится снизу слева, у картера сцепления.")},
			{18500.f, FLinearColor(0.6f, 0.6f, 0.62f), FVector(22, 20, 20), TEXT("Самодельный турбокомплект. Мощность растёт, мотор изнашивается быстрее.")},
			{2300.f, FLinearColor(0.25f, 0.2f, 0.18f), FVector(80, 18, 14), TEXT("Глушитель с резонатором. Снимается снизу, нужен домкрат.")},
			{4800.f, FLinearColor(0.2f, 0.2f, 0.22f), FVector(40, 14, 14), TEXT("Амортизаторы, шаровые и сайлентблоки передней подвески.")},
			{3600.f, FLinearColor(0.2f, 0.2f, 0.22f), FVector(40, 14, 14), TEXT("Амортизаторы и тяги задней подвески.")},
			{700.f, FLinearColor(0.35f, 0.3f, 0.28f), FVector(14, 10, 4), TEXT("Комплект передних колодок. Нужен домкрат и ключи.")},
			{1800.f, FLinearColor(0.85f, 0.88f, 0.9f), FVector(24, 10, 18), TEXT("Пара прямоугольных фар с отражателями.")},
			{1100.f, FLinearColor(0.7f, 0.12f, 0.08f), FVector(30, 8, 12), TEXT("Пара задних фонарей.")},
			{2600.f, FLinearColor(0.12f, 0.12f, 0.12f), FVector(36, 10, 14), TEXT("Щиток приборов: спидометр, тахометр, топливо, температура.")},
			{1900.f, FLinearColor(0.15f, 0.15f, 0.17f), FVector(18, 16, 5), TEXT("Кассетная магнитола «Урал». Ставится под панелью.")},
		};
		for (int32 I = 0; I < vsim::parts::SlotCount; ++I)
		{
			const vsim::parts::SlotInfo& Info = vsim::parts::Info(static_cast<vsim::parts::Slot>(I));
			Add(PartItem(I), UTF8_TO_TCHAR(Info.Name), Looks[I].Desc, Looks[I].Price, 1, -1, false, false, Looks[I].Color);
			Last().Kind = EPeregonItemKind::Part;
			Last().PartSlot = I;
			Last().SizeCm = Looks[I].Size;
		}

		// --- Инструменты ---
		struct FToolLook
		{
			float Price;
			FLinearColor Color;
			FVector Size;
			const TCHAR* Desc;
		};
		const FToolLook Tools[] = {
			{0.f, FLinearColor::Black, FVector::ZeroVector, TEXT("")},
			{1500.f, FLinearColor(0.6f, 0.1f, 0.1f), FVector(40, 20, 8), TEXT("Рожково-накидные ключи 8–22. Почти любая работа под капотом.")},
			{250.f, FLinearColor(0.5f, 0.5f, 0.52f), FVector(16, 3, 3), TEXT("Свечной ключ на 21 с воротком.")},
			{400.f, FLinearColor(0.85f, 0.65f, 0.1f), FVector(20, 4, 4), TEXT("Набор отвёрток: карбюратор, фары, магнитола, панель.")},
			{350.f, FLinearColor(0.15f, 0.25f, 0.6f), FVector(20, 6, 2), TEXT("Пассатижи — хомуты патрубков и проводка.")},
			{1800.f, FLinearColor(0.75f, 0.1f, 0.08f), FVector(45, 14, 14), TEXT("Ромбический домкрат и баллонный ключ: подвеска, тормоза, выхлоп.")},
			{3900.f, FLinearColor(0.1f, 0.45f, 0.3f), FVector(32, 12, 12), TEXT("Болгарка 125 мм. Отрезной диск — резать гниль, лепестковый — зачищать.")},
		};
		static_assert(UE_ARRAY_COUNT(Tools) == static_cast<int32>(vsim::parts::Tool::Count), "инструменты");
		for (int32 I = 1; I < static_cast<int32>(vsim::parts::Tool::Count); ++I)
		{
			Add(ToolItem(I), UTF8_TO_TCHAR(vsim::parts::ToolName(static_cast<vsim::parts::Tool>(I))), Tools[I].Desc, Tools[I].Price, 1, -1, false, false,
				Tools[I].Color);
			Last().Kind = EPeregonItemKind::Tool;
			Last().ToolKind = I;
			Last().SizeCm = Tools[I].Size;
		}

		// --- Расходники кузовных работ ---
		auto AddConsumable = [&](FName Id, const TCHAR* Name, const TCHAR* Desc, float Price, int32 Kind, const FLinearColor& Color, const FVector& Size) {
			Add(Id, Name, Desc, Price, 1, -1, false, false, Color);
			Last().Kind = EPeregonItemKind::Consumable;
			Last().ConsumableKind = Kind;
			Last().SizeCm = Size;
		};
		using vsim::parts::Consumable;
		AddConsumable(ConsumableItem(static_cast<int32>(Consumable::CutDisc)), TEXT("Отрезные диски (5 шт)"),
			TEXT("Для болгарки: вырезать сгнивший металл."), 300.f, static_cast<int32>(Consumable::CutDisc), FLinearColor(0.2f, 0.25f, 0.5f), FVector(13, 13, 3));
		AddConsumable(ConsumableItem(static_cast<int32>(Consumable::FlapDisc)), TEXT("Лепестковые диски (3 шт)"),
			TEXT("Для болгарки: снять ржавчину и старую краску, ошкурить шпаклёвку."), 360.f, static_cast<int32>(Consumable::FlapDisc),
			FLinearColor(0.75f, 0.55f, 0.3f), FVector(13, 13, 4));
		AddConsumable(ConsumableItem(static_cast<int32>(Consumable::Putty)), TEXT("Шпаклёвка со стекловолокном"),
			TEXT("Банка 1 кг с отвердителем. Закрывает дыры после вырезки гнили."), 650.f, static_cast<int32>(Consumable::Putty),
			FLinearColor(0.75f, 0.72f, 0.45f), FVector(14, 14, 12));
		AddConsumable(ConsumableItem(static_cast<int32>(Consumable::Primer)), TEXT("Грунт (баллон)"),
			TEXT("Серый кислотный грунт по голому металлу и шпаклёвке."), 420.f, static_cast<int32>(Consumable::Primer),
			FLinearColor(0.55f, 0.55f, 0.55f), FVector(7, 7, 24));
		struct FPaintLook
		{
			const TCHAR* Id;
			const TCHAR* Name;
			FLinearColor Color;
		};
		const FPaintLook PaintList[] = {
			{TEXT("paint.red"), TEXT("Краска «Коррида»"), FLinearColor(0.32f, 0.04f, 0.04f)},
			{TEXT("paint.white"), TEXT("Краска «Белая ночь»"), FLinearColor(0.75f, 0.74f, 0.7f)},
			{TEXT("paint.beige"), TEXT("Краска «Сафари»"), FLinearColor(0.55f, 0.45f, 0.28f)},
			{TEXT("paint.green"), TEXT("Краска «Липа зелёная»"), FLinearColor(0.12f, 0.3f, 0.12f)},
			{TEXT("paint.blue"), TEXT("Краска «Балтика»"), FLinearColor(0.08f, 0.18f, 0.42f)},
			{TEXT("paint.cherry"), TEXT("Краска «Вишня»"), FLinearColor(0.2f, 0.02f, 0.06f)},
			{TEXT("paint.black"), TEXT("Краска «Чёрная»"), FLinearColor(0.03f, 0.03f, 0.035f)},
			{TEXT("paint.yellow"), TEXT("Краска «Жёлтая такси»"), FLinearColor(0.7f, 0.5f, 0.05f)},
		};
		for (const FPaintLook& P : PaintList)
		{
			AddConsumable(FName(P.Id), P.Name, TEXT("Аэрозоль 520 мл. Красят по грунту — иначе облезет."), 520.f,
				static_cast<int32>(Consumable::Paint), P.Color, FVector(7, 7, 24));
			Last().PaintColor = P.Color;
		}

		// --- Жидкости ---
		Add(Oil, TEXT("Масло моторное 4 л"), TEXT("Залить под капотом (горловина на крышке клапанов). Норма — 3,75 л."), 1100.f, 1, -1, false, false,
			FLinearColor(0.8f, 0.6f, 0.1f));
		Last().Kind = EPeregonItemKind::Fluid;
		Last().SizeCm = FVector(18, 10, 26);
		Add(Coolant, TEXT("Антифриз 5 л"), TEXT("Залить в расширительный бачок под капотом. В систему входит 7,8 л."), 750.f, 1, -1, false, false,
			FLinearColor(0.15f, 0.55f, 0.25f));
		Last().Kind = EPeregonItemKind::Fluid;
		Last().SizeCm = FVector(18, 10, 28);
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

FString StackLabel(const FPeregonItemStack& Stack)
{
	const FPeregonItemDef* Def = Find(Stack.Id);
	FString Name = Def ? Def->Name : Stack.Id.ToString();
	if (Def && Def->Kind == EPeregonItemKind::Part)
	{
		static const TCHAR* Tiers[] = {TEXT(""), TEXT(" «Люкс»"), TEXT(" «Спорт»")};
		Name += Tiers[FMath::Clamp(Stack.Tier, 0, 2)];
		Name += FString::Printf(TEXT(" · %d%%"), FMath::RoundToInt(Stack.Condition * 100.f));
		if (Def->PartSlot == static_cast<int32>(vsim::parts::Slot::Battery) && Stack.Charge >= 0.f)
		{
			Name += FString::Printf(TEXT(", заряд %d%%"), FMath::RoundToInt(Stack.Charge * 100.f));
		}
	}
	else if (Def && (Def->Kind == EPeregonItemKind::Consumable || Def->Kind == EPeregonItemKind::Fluid) && Stack.Condition < 0.995f)
	{
		Name += FString::Printf(TEXT(" · осталось %d%%"), FMath::RoundToInt(Stack.Condition * 100.f));
	}
	if (Stack.Count > 1)
	{
		Name += FString::Printf(TEXT("  ×%d"), Stack.Count);
	}
	return Name;
}

FName PartItem(int32 Slot)
{
	static TArray<FName> Ids = []() {
		TArray<FName> Out;
		for (int32 I = 0; I < vsim::parts::SlotCount; ++I)
		{
			Out.Add(FName(*(FString(TEXT("part.")) + UTF8_TO_TCHAR(vsim::parts::Info(static_cast<vsim::parts::Slot>(I)).Id))));
		}
		return Out;
	}();
	return Ids.IsValidIndex(Slot) ? Ids[Slot] : NAME_None;
}

FName ToolItem(int32 Tool)
{
	static const FName Ids[] = {NAME_None, FName(TEXT("tool.wrenches")), FName(TEXT("tool.plugsocket")), FName(TEXT("tool.screwdriver")),
		FName(TEXT("tool.pliers")), FName(TEXT("tool.jack")), FName(TEXT("tool.grinder"))};
	return Tool > 0 && Tool < static_cast<int32>(UE_ARRAY_COUNT(Ids)) ? Ids[Tool] : NAME_None;
}

FName ConsumableItem(int32 Consumable)
{
	static const FName Ids[] = {NAME_None, FName(TEXT("disc.cut")), FName(TEXT("disc.flap")), FName(TEXT("putty")), FName(TEXT("primer")),
		FName(TEXT("paint.red"))};
	return Consumable > 0 && Consumable < static_cast<int32>(UE_ARRAY_COUNT(Ids)) ? Ids[Consumable] : NAME_None;
}

const TArray<FName>& Paints()
{
	static TArray<FName> Out = []() {
		TArray<FName> List;
		for (const FPeregonItemDef& D : All())
		{
			if (D.Kind == EPeregonItemKind::Consumable && D.ConsumableKind == static_cast<int32>(vsim::parts::Consumable::Paint))
			{
				List.Add(D.Id);
			}
		}
		return List;
	}();
	return Out;
}

TArray<FName> PartsShopStock()
{
	TArray<FName> Out;
	for (const FPeregonItemDef& D : All())
	{
		if (D.Kind == EPeregonItemKind::Part || D.Kind == EPeregonItemKind::Tool || D.Kind == EPeregonItemKind::Consumable
			|| D.Kind == EPeregonItemKind::Fluid)
		{
			Out.Add(D.Id);
		}
	}
	return Out;
}

float PriceOf(const FPeregonItemStack& Stack)
{
	const FPeregonItemDef* Def = Find(Stack.Id);
	if (!Def)
	{
		return 0.f;
	}
	float Price = Def->Price;
	if (Def->Kind == EPeregonItemKind::Part)
	{
		Price *= (Stack.Tier == 2 ? 2.4f : (Stack.Tier == 1 ? 1.5f : 1.f)) * FMath::Lerp(0.15f, 1.f, FMath::Clamp(Stack.Condition, 0.f, 1.f));
	}
	return FMath::RoundToFloat(Price / 10.f) * 10.f;
}
} // namespace PeregonItems
