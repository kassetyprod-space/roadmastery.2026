// Состояние «партии», которое живёт дольше одного режима: параметры новой игры, деньги, время суток.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Game/PeregonInventory.h"
#include "rgen/Climate.h"
#include "PeregonGameInstance.generated.h"

UENUM(BlueprintType)
enum class EPeregonAssists : uint8
{
	Casual UMETA(DisplayName = "Автомат"),          // автомат, автосцепление, ABS
	Manual UMETA(DisplayName = "Механика"),         // ручные передачи, автосцепление
	Hardcore UMETA(DisplayName = "Хардкор")         // ручные передачи и сцепление, без ABS
};

USTRUCT(BlueprintType)
struct FPeregonRunConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") int32 Seed = 2026;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") EPeregonAssists Assists = EPeregonAssists::Casual;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") FLinearColor CarColor = FLinearColor(0.32f, 0.04f, 0.04f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") float StartMoney = 1000000.f; // стартовый баланс, ₽
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") float StartHour = 9.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") bool bProvingGround = false;
	// Сюжетное начало: пятиэтажка, гаражный кооператив, «Сокол» без половины деталей, помойка и разборка рядом.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") bool bGarageStart = true;
	// День года старта (1..365). 258 — 15 сентября, золотая осень.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") int32 StartDayOfYear = 258;
};

// Погода и время года (сохраняется). Логика — в rgen::Climate.
USTRUCT(BlueprintType)
struct FPeregonClimate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Climate") float DayOfYear = 258.f;
	UPROPERTY(BlueprintReadWrite, Category = "Climate") uint8 Current = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Climate") uint8 Next = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Climate") float Blend = 1.f;
	UPROPERTY(BlueprintReadWrite, Category = "Climate") float HoursLeft = 3.f;
	UPROPERTY(BlueprintReadWrite, Category = "Climate") float Wetness = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "Climate") float SnowCover = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "Climate") float WindDirDeg = 240.f;
	UPROPERTY(BlueprintReadWrite, Category = "Climate") int32 Seed = 0x2026;

	rgen::ClimateState ToState() const
	{
		rgen::ClimateState S;
		S.DayOfYear = DayOfYear;
		constexpr uint8 Last = static_cast<uint8>(static_cast<uint8>(rgen::Weather::Count) - 1);
		S.Current = static_cast<rgen::Weather>(Current > Last ? Last : Current);
		S.Next = static_cast<rgen::Weather>(Next > Last ? Last : Next);
		S.Blend = Blend;
		S.HoursLeft = HoursLeft;
		S.Wetness = Wetness;
		S.SnowCover = SnowCover;
		S.WindDirDeg = WindDirDeg;
		S.Seed = static_cast<uint32>(Seed);
		return S;
	}
	void FromState(const rgen::ClimateState& S)
	{
		DayOfYear = static_cast<float>(S.DayOfYear);
		Current = static_cast<uint8>(S.Current);
		Next = static_cast<uint8>(S.Next);
		Blend = static_cast<float>(S.Blend);
		HoursLeft = static_cast<float>(S.HoursLeft);
		Wetness = static_cast<float>(S.Wetness);
		SnowCover = static_cast<float>(S.SnowCover);
		WindDirDeg = static_cast<float>(S.WindDirDeg);
		Seed = static_cast<int32>(S.Seed);
	}
};

UCLASS()
class PEREGON_API UPeregonGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Peregon")
	FPeregonRunConfig Config;

	UPROPERTY(BlueprintReadWrite, Category = "Peregon")
	float Money = 3000.f;

	// 0..24
	UPROPERTY(BlueprintReadWrite, Category = "Peregon")
	float TimeOfDayHours = 10.f;

	UPROPERTY(BlueprintReadWrite, Category = "Peregon")
	int32 Day = 1;

	UPROPERTY(BlueprintReadWrite, Category = "Peregon")
	FPeregonClimate Climate;

	// Новая партия: дата старта, ясная погода, сухие дороги (снег — по сезону).
	void ResetClimate(int32 StartDayOfYear, int32 Seed)
	{
		Climate = FPeregonClimate();
		Climate.DayOfYear = static_cast<float>(FMath::Clamp(StartDayOfYear, 1, 365));
		Climate.Seed = Seed ^ 0x5EA5;
		const rgen::SeasonLook Look = rgen::Climate::Season(Climate.DayOfYear);
		Climate.SnowCover = 0.f;
		Climate.Current = Climate.Next = static_cast<uint8>(Look.GroundSnow > 0.5 ? rgen::Weather::Cloudy : rgen::Weather::Clear);
	}

	// Вещи: карманы, бардачок, багажник.
	UPROPERTY(BlueprintReadWrite, Category = "Peregon|Inventory")
	FPeregonContainer Pockets;

	UPROPERTY(BlueprintReadWrite, Category = "Peregon|Inventory")
	FPeregonContainer Glovebox;

	UPROPERTY(BlueprintReadWrite, Category = "Peregon|Inventory")
	FPeregonContainer Trunk;

	// Самая дальняя точка маршрута, до которой доехали (м) — её карандашом отмечаем на карте.
	UPROPERTY(BlueprintReadWrite, Category = "Peregon")
	float TraveledMaxS = 0.f;

	FPeregonContainer* GetStorage(EPeregonStorage Storage)
	{
		return Storage == EPeregonStorage::Glovebox ? &Glovebox : (Storage == EPeregonStorage::Trunk ? &Trunk : nullptr);
	}

	// Набор новой игры: документы в бардачке, пустая канистра в багажнике, карманы пустые.
	void ResetInventory()
	{
		Pockets = FPeregonContainer();
		Pockets.Capacity = 10;
		Glovebox = FPeregonContainer();
		Glovebox.Capacity = 4;
		Glovebox.Add(PeregonItems::Documents);
		Glovebox.Add(PeregonItems::Manual);
		Trunk = FPeregonContainer();
		Trunk.Capacity = 10;
		Trunk.Add(PeregonItems::CanisterEmpty);
		TraveledMaxS = 0.f;
	}

	bool TrySpend(float Amount)
	{
		if (Money + 0.01f < Amount)
		{
			return false;
		}
		Money -= Amount;
		return true;
	}
};
