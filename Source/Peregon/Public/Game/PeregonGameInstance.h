// Состояние «партии», которое живёт дольше одного режима: параметры новой игры, деньги, время суток.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") float StartMoney = 3000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") float StartHour = 9.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run") bool bProvingGround = false;
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
