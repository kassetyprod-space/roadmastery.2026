// Ход игры без загрузки уровней: главное меню (на фоне — облёт трассы), новая игра на дороге,
// испытательный полигон, продолжение из сохранения. Всё строится на одном (любом, хоть пустом) уровне.
#pragma once

#include "CoreMinimal.h"
#include "Game/PeregonGameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "PeregonGameMode.generated.h"

class ACameraActor;
class APeregonCar;
class APeregonCharacter;
class APeregonProvingGround;
class APeregonRoadWorld;
class APeregonSky;
class UPeregonSaveGame;

UENUM(BlueprintType)
enum class EPeregonMode : uint8
{
	Menu,
	Road,
	ProvingGround
};

struct FPeregonToast
{
	FString Text;
	double Until = 0.0;
	double Duration = 3.0;
};

UCLASS()
class PEREGON_API APeregonGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APeregonGameMode();

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

	// Страховка: если машина или пешеход провалились под землю — вернуть на дорогу.
	void RecoverFallen();

	void EnterMenu();
	void StartNewGame(const FPeregonRunConfig& Config);
	bool ContinueGame();
	bool HasSave() const;
	FString DescribeSave() const;
	bool SaveGame(const FString& PlaceName = FString());

	void Toast(const FString& Text, float Seconds = 3.5f);
	const TArray<FPeregonToast>& GetToasts() const { return Toasts; }

	EPeregonMode GetMode() const { return Mode; }
	APeregonCar* GetCar() const { return Car; }
	APeregonCharacter* GetWalker() const { return Walker; }
	APeregonRoadWorld* GetRoadWorld() const { return RoadWorld; }
	APeregonSky* GetSky() const { return Sky; }

	// В редакторе можно сразу начинать поездку, минуя меню.
	UPROPERTY(EditAnywhere, Category = "Peregon")
	bool bSkipMenu = false;

private:
	void ClearSession();
	void EnsureSky();
	void StartSession(const FPeregonRunConfig& Config, const UPeregonSaveGame* Save);
	void ApplyAssists(const FPeregonRunConfig& Config);
	APlayerController* GetPlayer() const;

	UPROPERTY(Transient) TObjectPtr<APeregonRoadWorld> RoadWorld;
	UPROPERTY(Transient) TObjectPtr<APeregonProvingGround> ProvingGround;
	UPROPERTY(Transient) TObjectPtr<APeregonCar> Car;
	UPROPERTY(Transient) TObjectPtr<APeregonCharacter> Walker;
	UPROPERTY(Transient) TObjectPtr<APeregonSky> Sky;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> MenuCamera;

	EPeregonMode Mode = EPeregonMode::Menu;
	double MenuCameraS = 300.0;
	TArray<FPeregonToast> Toasts;
};
