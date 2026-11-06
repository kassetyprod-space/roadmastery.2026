#include "World/PeregonNightLights.h"

#include "Art/PeregonArtSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Game/PeregonGameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "World/PeregonKit.h"

namespace PeregonNightTuning
{
// Натриевый фонарь: тёплый оранжевый, широкий конус вниз.
const FLinearColor LampColor(1.f, 0.72f, 0.42f);
constexpr float LampCandela = 2600.f;
constexpr float LampRadiusCm = 2600.f;
// Окно: лампа накаливания за шторой.
const FLinearColor WindowColor(1.f, 0.78f, 0.5f);
constexpr float WindowCandela = 70.f;
constexpr float WindowRadiusCm = 1100.f;
constexpr int32 ShadowedLamps = 6; // тени — только у ближайших
} // namespace PeregonNightTuning

APeregonNightLights::APeregonNightLights()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void APeregonNightLights::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	const float Night = Art ? Art->GetNight() : 0.f;
	Fade = FMath::FInterpTo(Fade, Night, DeltaSeconds, 1.5f);
	Timer -= DeltaSeconds;
	if (Timer > 0.f)
	{
		return;
	}
	Timer = 0.3f;
	FVector Camera = FVector::ZeroVector;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager)
	{
		Camera = PC->PlayerCameraManager->GetCameraLocation();
	}
	Reassign(Camera, Fade);
}

void APeregonNightLights::Reassign(const FVector& Camera, float Night)
{
	using namespace PeregonNightTuning;
	const int32 Budget = Night > 0.05f ? FMath::Clamp(UPeregonGameUserSettings::Get()->MaxLampLights, 0, 64) : 0;
	PeregonLights::Prune();
	const TArray<FPeregonLightSpot>& All = PeregonLights::Spots();

	// Ближайшие: фонари важнее окон (фонарь освещает дорогу), поэтому окна «дальше» в 1.6 раза.
	TArray<TPair<float, int32>> LampOrder;
	TArray<TPair<float, int32>> WindowOrder;
	for (int32 I = 0; I < All.Num(); ++I)
	{
		const float D = static_cast<float>(FVector::DistSquared(All[I].Location, Camera));
		if (D > FMath::Square(25000.f))
		{
			continue;
		}
		(All[I].bWindow ? WindowOrder : LampOrder).Emplace(All[I].bWindow ? D * 2.56f : D, I);
	}
	LampOrder.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	WindowOrder.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	const int32 LampCount = FMath::Min(LampOrder.Num(), FMath::Max(Budget / 2, Budget - WindowOrder.Num()));
	const int32 WindowCount = FMath::Min(WindowOrder.Num(), Budget - LampCount);

	auto Ensure = [this](int32 NeedLamps, int32 NeedWindows) {
		while (Lamps.Num() < NeedLamps)
		{
			USpotLightComponent* L = NewObject<USpotLightComponent>(this);
			L->SetupAttachment(Root);
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensityUnits(ELightUnits::Candelas);
			L->SetLightColor(LampColor);
			L->SetAttenuationRadius(LampRadiusCm);
			L->SetInnerConeAngle(35.f);
			L->SetOuterConeAngle(68.f);
			L->SetSourceRadius(12.f);
			L->SetVolumetricScatteringIntensity(1.2f); // ореол в тумане и под снегом
			L->RegisterComponent();
			L->SetAbsolute(true, true, true);
			Lamps.Add(L);
		}
		while (Windows.Num() < NeedWindows)
		{
			URectLightComponent* W = NewObject<URectLightComponent>(this);
			W->SetupAttachment(Root);
			W->SetMobility(EComponentMobility::Movable);
			W->SetIntensityUnits(ELightUnits::Candelas);
			W->SetLightColor(WindowColor);
			W->SetAttenuationRadius(WindowRadiusCm);
			W->SetBarnDoorAngle(55.f);
			W->SetBarnDoorLength(25.f);
			W->SetCastShadows(false);
			W->SetVolumetricScatteringIntensity(0.4f);
			W->RegisterComponent();
			W->SetAbsolute(true, true, true);
			Windows.Add(W);
		}
	};
	Ensure(LampCount, WindowCount);

	for (int32 K = 0; K < Lamps.Num(); ++K)
	{
		USpotLightComponent* L = Lamps[K];
		const bool bOn = K < LampCount;
		L->SetVisibility(bOn);
		if (!bOn)
		{
			continue;
		}
		const FPeregonLightSpot& S = All[LampOrder[K].Value];
		L->SetWorldLocationAndRotation(S.Location, S.Direction.Rotation());
		L->SetIntensity(LampCandela * Night);
		L->SetCastShadows(K < ShadowedLamps);
	}
	for (int32 K = 0; K < Windows.Num(); ++K)
	{
		URectLightComponent* W = Windows[K];
		const bool bOn = K < WindowCount;
		W->SetVisibility(bOn);
		if (!bOn)
		{
			continue;
		}
		const FPeregonLightSpot& S = All[WindowOrder[K].Value];
		W->SetWorldLocationAndRotation(S.Location, S.Direction.Rotation());
		W->SetSourceWidth(S.SizeCm.X);
		W->SetSourceHeight(S.SizeCm.Y);
		// У каждого окна свой оттенок: где лампа, где телевизор, где абажур.
		const float Tone = static_cast<float>(S.Seed % 100u) / 100.f;
		W->SetLightColor(FMath::Lerp(WindowColor, Tone < 0.15f ? FLinearColor(0.7f, 0.8f, 1.f) : FLinearColor(1.f, 0.62f, 0.35f), Tone < 0.15f ? 0.7f : Tone * 0.5f));
		W->SetIntensity(WindowCandela * Night);
	}
}
