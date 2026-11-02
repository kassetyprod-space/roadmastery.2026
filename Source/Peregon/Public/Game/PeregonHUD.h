// Отладочный HUD: приборка + состояние деталей. Позже заменим на UMG-приборную панель в кабине.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PeregonHUD.generated.h"

UCLASS()
class PEREGON_API APeregonHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
