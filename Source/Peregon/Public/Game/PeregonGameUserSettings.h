// Настройки игрока. Графика — стандартные UGameUserSettings (разрешение, окно, качество, VSync, FPS),
// плюс свои: камера и управление. Всё хранится в Saved/Config/.../GameUserSettings.ini.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "PeregonGameUserSettings.generated.h"

UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class PEREGON_API UPeregonGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	// Никогда не возвращает null: если класс не подключён в DefaultEngine.ini — значения по умолчанию.
	static UPeregonGameUserSettings* Get();

	virtual void SetToDefaults() override;

	// Чувствительность мыши/стика (множитель).
	UPROPERTY(Config) float MouseSensitivity = 1.f;

	UPROPERTY(Config) bool bInvertY = false;

	// Горизонтальный угол обзора, градусы.
	UPROPERTY(Config) float FieldOfView = 80.f;

	// Сила инерции головы, тряски и покачивания при ходьбе (0 — выкл, 1 — реалистично).
	UPROPERTY(Config) float HeadMotion = 1.f;

	// В машину садимся с видом из салона (иначе — снаружи).
	UPROPERTY(Config) bool bStartFirstPerson = true;

	// Скорость смены дня и ночи: игровых минут за реальную секунду.
	UPROPERTY(Config) float TimeScale = 0.5f;

	// Стиль картинки: 0 — «живопись» (мягкие текстуры, тёплый свет, как в «Заре»), 1 — пиксели PSX.
	UPROPERTY(Config) int32 VisualStyle = 0;
};
