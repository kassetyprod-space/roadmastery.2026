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

	// --- Графика ---
	// Детализация процедурных текстур (только «живопись»): 0 — как есть, 1 — высокая (×4), 2 — ультра (×8). После перезапуска.
	UPROPERTY(Config) int32 TextureDetail = 1;
	// Насколько тёмная ночь: 0 — светлая «игровая», 1 — настоящая (без фар и фонарей почти ничего не видно).
	UPROPERTY(Config) float NightDarkness = 0.8f;
	UPROPERTY(Config) bool bVolumetricFog = true;
	UPROPERTY(Config) bool bFilmGrain = true;
	// Сила живописного фильтра (мазки и контур), 0 — выключен.
	UPROPERTY(Config) float PaintStrength = 0.7f;
	// Настоящий свет фонарей и окон (прожекторы на землю и фасады) — для ближайших; дальние — только свечение.
	UPROPERTY(Config) int32 MaxLampLights = 24;
	// Облака: 0 — один слой, 1 — три слоя, 2 — пять слоёв (перистые, кучевые, разорванные).
	UPROPERTY(Config) int32 CloudQuality = 2;
	// Машин на дороге: 0 — нет, 1 — мало, 2 — обычно, 3 — много.
	UPROPERTY(Config) int32 TrafficDensity = 2;

	// --- Звук (0..1.5) ---
	UPROPERTY(Config) float MasterVolume = 1.f;
	UPROPERTY(Config) float EngineVolume = 1.f;
	UPROPERTY(Config) float EffectsVolume = 1.f;
	UPROPERTY(Config) float AmbienceVolume = 1.f;
	UPROPERTY(Config) float RadioVolume = 1.f;

	// Применить то, что не требует перезапуска: громкость, туман. Вызывается меню и при старте игры.
	void ApplyRuntime() const;
};
