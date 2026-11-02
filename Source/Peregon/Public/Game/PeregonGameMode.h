// Режим прототипа: машина игрока + испытательный полигон. Работает на пустом уровне.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PeregonGameMode.generated.h"

class APeregonProvingGround;

UCLASS()
class PEREGON_API APeregonGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APeregonGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	UPROPERTY(EditAnywhere, Category = "Peregon")
	bool bSpawnProvingGround = true;

private:
	UPROPERTY(Transient)
	TObjectPtr<APeregonProvingGround> ProvingGround;
};
