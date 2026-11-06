// Радиоприёмник в машине: перемешанные MP3 из папки Content/Radio (кладёте свои файлы), звук «из динамика»:
// узкая полоса, лёгкий перегруз, моно, эфирный шум — тем сильнее, чем дальше от городов. Вне машины — глуше
// и тише с расстоянием. Декодер MP3 — minimp3 (CC0), файлы читаются в фоне, без ассетов Unreal.
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Async/Future.h"
#include "HAL/CriticalSection.h"

#include <atomic>

#include "PeregonRadio.generated.h"

// Поток одного MP3 (весь файл в памяти, декодирование кадрами по ходу воспроизведения).
class PEREGON_API FPeregonMp3Stream
{
public:
	explicit FPeregonMp3Stream(TArray<uint8>&& InBytes);
	~FPeregonMp3Stream();
	bool IsValid() const { return SampleRate > 0; }
	int32 GetSampleRate() const { return SampleRate; }
	// Следующий стерео-отсчёт. false — файл кончился.
	bool Next(float& OutL, float& OutR);

private:
	bool DecodeFrame();
	TArray<uint8> Bytes;
	void* Decoder = nullptr; // mp3dec_t
	int32 Offset = 0;
	TArray<float> Frame;     // чередующиеся L/R
	int32 FramePos = 0;
	int32 FrameSamples = 0;
	int32 Channels = 2;
	int32 SampleRate = 0;
};

UCLASS(ClassGroup = Peregon)
class PEREGON_API UPeregonRadio : public USynthComponent
{
	GENERATED_BODY()

public:
	UPeregonRadio(const FObjectInitializer& ObjectInitializer);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetPower(bool bOn);
	bool IsPowered() const { return bPower; }
	// Следующая песня (из перемешанного списка).
	void NextTrack();
	void PrevTrack();
	FString GetTrackName() const { return TrackName; }
	// Станции: MP3 прямо в Content/Radio — «Радио Р-255» (ловится везде); каждая подпапка — своя станция.
	// Имя папки «Кузбасс FM@1» — станция области 1 (по rgen::Route::Regions): в чужой области её почти не слышно.
	void NextStation();
	FString GetStationName() const;
	int32 GetStationRegion() const;
	int32 GetStationCount() const { return Stations.Num(); }
	int32 GetTrackCount() const { return Playlist.Num(); }
	// Шум эфира 0..1, «снаружи машины» 0..1 (глуше), громкость 0..1.
	void SetReception(float InStatic, float InOutside, float InVolume)
	{
		ParamStatic.store(InStatic);
		ParamOutside.store(InOutside);
		ParamVolume.store(InVolume);
	}
	// Папка с музыкой: <проект>/Content/Radio.
	static FString GetFolder();

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	void ScanFolder();
	void ScanStations();
	struct FStation
	{
		FString Name;
		FString Folder;
		int32 Region = -1;
	};
	TArray<FStation> Stations;
	int32 Station = 0;
	void StartLoad(int32 Index);

	TArray<FString> Playlist;
	int32 Current = -1;
	FString TrackName;
	bool bPower = false;
	bool bLoading = false;
	TFuture<TSharedPtr<FPeregonMp3Stream, ESPMode::ThreadSafe>> Loading;

	// Передача потока звуковому потоку.
	FCriticalSection StreamLock;
	TSharedPtr<FPeregonMp3Stream, ESPMode::ThreadSafe> PendingStream;
	std::atomic<bool> bHasPending{false};
	std::atomic<bool> bTrackEnded{false};

	std::atomic<bool> ParamPower{false};
	std::atomic<float> ParamStatic{0.1f};
	std::atomic<float> ParamOutside{0.f};
	std::atomic<float> ParamVolume{0.7f};

	// Только звуковой поток.
	TSharedPtr<FPeregonMp3Stream, ESPMode::ThreadSafe> Stream;
	float Rate = 48000.f;
	double SrcPos = 0.0;
	float PrevL = 0.f, PrevR = 0.f, CurL = 0.f, CurR = 0.f;
	float Hp1 = 0.f, Hp1In = 0.f, Lp1 = 0.f, Lp2 = 0.f, Lp3 = 0.f;
	float Crackle = 0.f;
	float Fade = 0.f;
	uint32 Seed = 0x5ADu;
};
