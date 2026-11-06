// Звук. Файлы (WAV 16/24/32-бит, моно/стерео) лежат в Content/Sounds, какие из них что озвучивают —
// написано в Content/Sounds/sounds.txt («событие = файл1.wav, файл2.wav»). Так можно подключить любую
// библиотеку, например бесплатную Pro Sound Effects (PSE Free Library), не переименовывая файлы.
// Если для события файла нет — звучит процедурный синтез (мотор из «вспышек» цилиндров, шум шин и ветра,
// стуки дверей), поэтому игра звучит и без библиотеки.
//
// Мотор: слои «engine.on.<обороты>» / «engine.off.<обороты>» (под нагрузкой / сброс газа) смешиваются по
// оборотам с переменной скоростью воспроизведения — как в автосимуляторах.
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"

#include <atomic>

#include "PeregonAudio.generated.h"

struct FPeregonPcm
{
	TArray<float> Samples; // моно
	int32 SampleRate = 48000;
	FString Name;
};
using FPeregonPcmPtr = TSharedPtr<const FPeregonPcm, ESPMode::ThreadSafe>;

namespace PeregonAudio
{
// Все файлы события (загружаются при первом обращении). Пусто — события нет в sounds.txt.
PEREGON_API TArray<FPeregonPcmPtr> GetClips(const FString& Event);
PEREGON_API FPeregonPcmPtr GetRandomClip(const FString& Event);
// События, имена которых начинаются с префикса (для слоёв мотора).
PEREGON_API TArray<FString> FindEvents(const FString& Prefix);

// Разовый звук в точке мира. Pitch — множитель скорости.
PEREGON_API void PlayEvent(const UObject* WorldContext, const FString& Event, const FVector& Location, float Volume = 1.f, float Pitch = 1.f);
// Шаг: поверхность определяется лучом вниз (физматериал).
PEREGON_API void PlayFootstep(const AActor* Walker, const FVector& FootLocation, float Loudness);
} // namespace PeregonAudio

// Процедурные «голоса» на случай, когда файлов нет.
enum class EPeregonProcedural : uint8
{
	None,
	Engine,
	Wind,
	TireRoll,
	Skid,
	Thump,   // дверь, капот
	Click,   // ключ, переключатель, бардачок
	Clunk,   // рычаг КПП
	Grind,   // хруст коробки
	Ratchet, // ручник
	StepHard,
	StepGravel,
	StepSoft,
	Land,
	Paper,
	Rain,    // петля: шелест дождя и капли (интенсивность 0..1)
	Wiper    // разовый: мотор и резинка дворника по стеклу
};

UCLASS(ClassGroup = Peregon)
class PEREGON_API UPeregonSynth : public USynthComponent
{
	GENERATED_BODY()

public:
	UPeregonSynth(const FObjectInitializer& ObjectInitializer);

	// Настройка — до Start(), с игрового потока.
	void SetupEngine();
	void SetupLoop(const FString& Event, EPeregonProcedural Fallback);
	void SetupOneShot(const FString& Event, EPeregonProcedural Fallback, float Pitch);

	// Параметры — с игрового потока в любой момент.
	void SetEngineState(float InRpm, float InLoad, bool bInRunning, bool bInCranking)
	{
		ParamRpm.store(InRpm);
		ParamLoad.store(InLoad);
		ParamRunning.store(bInRunning);
		ParamCranking.store(bInCranking);
	}
	// Громкость 0..1 и «интенсивность» (скорость воспроизведения петли или параметр синтеза: скорость, скольжение).
	void SetLevel(float InVolume, float InIntensity)
	{
		ParamVolume.store(InVolume);
		ParamIntensity.store(InIntensity);
	}
	// Звук «из салона»: приглушаем верха.
	void SetMuffled(bool bMuffled) { ParamMuffled.store(bMuffled); }
	bool IsFinished() const { return bFinished.load(); }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	struct FBiquad
	{
		float B0 = 1.f, B1 = 0.f, B2 = 0.f, A1 = 0.f, A2 = 0.f, Z1 = 0.f, Z2 = 0.f;
		void Bandpass(float Freq, float Q, float Rate);
		void Lowpass(float Freq, float Q, float Rate);
		float Process(float X)
		{
			const float Y = B0 * X + Z1;
			Z1 = B1 * X - A1 * Y + Z2;
			Z2 = B2 * X - A2 * Y;
			return Y;
		}
	};

	struct FLayer
	{
		FPeregonPcmPtr Clip;
		float Rpm = 1000.f;
		bool bOn = true;
		double Pos = 0.0;
	};

	enum class EMode : uint8
	{
		Engine,
		Loop,
		OneShot
	};

	float Noise();
	float ReadLooped(const FPeregonPcm& Clip, double& Pos, double Step);
	float EngineSample();
	float ProceduralSample();

	EMode Mode = EMode::OneShot;
	EPeregonProcedural Procedural = EPeregonProcedural::None;
	TArray<FLayer> Layers;          // мотор из файлов
	FPeregonPcmPtr StarterClip;
	FPeregonPcmPtr LoopClip;        // петля или разовый звук
	float OneShotPitch = 1.f;

	std::atomic<float> ParamRpm{0.f};
	std::atomic<float> ParamLoad{0.f};
	std::atomic<bool> ParamRunning{false};
	std::atomic<bool> ParamCranking{false};
	std::atomic<float> ParamVolume{1.f};
	std::atomic<float> ParamIntensity{0.f};
	std::atomic<bool> ParamMuffled{false};
	std::atomic<bool> bFinished{false};

	// Состояние синтеза (только поток звука).
	float Rate = 48000.f;
	double ClipPos = 0.0;
	double StarterPos = 0.0;
	uint32 Seed = 0x12345u;
	float Rpm = 0.f;
	float Level = 0.f;
	float Gain = 0.f;
	float Cycle = 0.f;      // фаза цикла 4-тактного мотора, 0..4π
	float FirePhase = 0.f;
	float Excite = 0.f;
	float ExciteDecay = 0.99f;
	float Cylinder[4] = {1.f, 0.93f, 1.05f, 0.97f};
	float Time = 0.f;
	float Brown = 0.f;
	float Muffle = 0.f;
	float Intensity = 0.f;
	FBiquad Res[4];
	FBiquad Hiss;
	FBiquad Tone;
	FBiquad Tone2;
	float Env = 0.f;
	float Age = 0.f;
	bool bStarted = false;
};
