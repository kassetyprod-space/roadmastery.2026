#include "Game/PeregonGameUserSettings.h"

#include "Engine/Engine.h"

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
}
