#include "Game/PeregonGameUserSettings.h"

#include "Audio/PeregonAudio.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

UPeregonGameUserSettings* UPeregonGameUserSettings::Get()
{
	if (GEngine)
	{
		if (UPeregonGameUserSettings* Settings = Cast<UPeregonGameUserSettings>(GEngine->GetGameUserSettings()))
		{
			return Settings;
		}
	}
	return GetMutableDefault<UPeregonGameUserSettings>();
}

void UPeregonGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	MouseSensitivity = 1.f;
	bInvertY = false;
	FieldOfView = 80.f;
	HeadMotion = 1.f;
	bStartFirstPerson = true;
	TimeScale = 0.5f;
	TextureDetail = 1;
	NightDarkness = 0.8f;
	bVolumetricFog = true;
	bFilmGrain = true;
	PaintStrength = 0.7f;
	MaxLampLights = 24;
	CloudQuality = 2;
	TrafficDensity = 2;
	MasterVolume = EngineVolume = EffectsVolume = AmbienceVolume = RadioVolume = 1.f;
}

void UPeregonGameUserSettings::ApplyRuntime() const
{
	PeregonAudio::SetVolumes(MasterVolume, EngineVolume, EffectsVolume, AmbienceVolume, RadioVolume);
	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricFog")))
	{
		Var->Set(bVolumetricFog ? 1 : 0, ECVF_SetByGameSetting);
	}
}
