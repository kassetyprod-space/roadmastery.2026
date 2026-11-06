#include "Audio/PeregonRadio.h"
#include "Audio/PeregonAudio.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include <cmath>

// Декодер MP3: minimp3 (lieff/minimp3, CC0 — общественное достояние), только этот файл видит реализацию.
THIRD_PARTY_INCLUDES_START
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "../../ThirdParty/minimp3/minimp3.h"
THIRD_PARTY_INCLUDES_END
// В unity-сборке макросы декодера (S0, V1, LOAD…) иначе попали бы в соседние файлы.
#undef BITS_DEQUANTIZER_OUT
#undef BSPOS
#undef CHECK_BITS
#undef DEQ_COUNT1
#undef DQ
#undef FLUSH_BITS
#undef HAVE_ARMV6
#undef HAVE_SIMD
#undef HAVE_SSE
#undef HDR_GET_BITRATE
#undef HDR_GET_LAYER
#undef HDR_GET_MY_SAMPLE_RATE
#undef HDR_GET_SAMPLE_RATE
#undef HDR_GET_STEREO_MODE
#undef HDR_GET_STEREO_MODE_EXT
#undef HDR_IS_CRC
#undef HDR_IS_FRAME_576
#undef HDR_IS_FREE_FORMAT
#undef HDR_IS_LAYER_1
#undef HDR_IS_MONO
#undef HDR_IS_MS_STEREO
#undef HDR_SIZE
#undef HDR_TEST_I_STEREO
#undef HDR_TEST_MPEG1
#undef HDR_TEST_MS_STEREO
#undef HDR_TEST_NOT_MPEG25
#undef HDR_TEST_PADDING
#undef LOAD
#undef MAX_BITRESERVOIR_BYTES
#undef MAX_FRAME_SYNC_MATCHES
#undef MAX_FREE_FORMAT_FRAME_SIZE
#undef MAX_L3_FRAME_PAYLOAD_BYTES
#undef MAX_SCF
#undef MAX_SCFI
#undef MINIMP3_MAX
#undef MINIMP3_MIN
#undef MINIMP3_ONLY_SIMD
#undef MODE_JOINT_STEREO
#undef MODE_MONO
#undef PEEK_BITS
#undef RELOAD_SCALEFACTOR
#undef S0
#undef S1
#undef S2
#undef SHORT_BLOCK_TYPE
#undef STOP_BLOCK_TYPE
#undef V0
#undef V1
#undef V2
#undef VADD
#undef VLD
#undef VLOAD
#undef VMAC
#undef VMSB
#undef VMUL
#undef VMUL_S
#undef VREV
#undef VSAVE2
#undef VSAVE4
#undef VSET
#undef VSTORE
#undef VSUB
#undef minimp3_cpuid

// ---------------------------------------------------------------------------------------------
// Поток MP3
// ---------------------------------------------------------------------------------------------
FPeregonMp3Stream::FPeregonMp3Stream(TArray<uint8>&& InBytes) : Bytes(MoveTemp(InBytes))
{
	mp3dec_t* Dec = new mp3dec_t;
	mp3dec_init(Dec);
	Decoder = Dec;
	Frame.SetNumZeroed(MINIMP3_MAX_SAMPLES_PER_FRAME);
	// Первый кадр — узнать частоту и каналы.
	if (!DecodeFrame())
	{
		SampleRate = 0;
	}
}

FPeregonMp3Stream::~FPeregonMp3Stream()
{
	delete static_cast<mp3dec_t*>(Decoder);
}

bool FPeregonMp3Stream::DecodeFrame()
{
	mp3dec_t* Dec = static_cast<mp3dec_t*>(Decoder);
	int16 Pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	while (Offset < Bytes.Num())
	{
		mp3dec_frame_info_t Info;
		const int Samples = mp3dec_decode_frame(Dec, Bytes.GetData() + Offset, Bytes.Num() - Offset, Pcm, &Info);
		if (Info.frame_bytes <= 0)
		{
			return false; // дальше нет кадров
		}
		Offset += Info.frame_bytes;
		if (Samples <= 0)
		{
			continue; // теги ID3, мусор — пропускаем
		}
		Channels = FMath::Clamp(Info.channels, 1, 2);
		SampleRate = Info.hz;
		FrameSamples = Samples;
		FramePos = 0;
		for (int32 K = 0; K < Samples; ++K)
		{
			const float L = Pcm[K * Channels] / 32768.f;
			const float R = Channels == 2 ? Pcm[K * Channels + 1] / 32768.f : L;
			Frame[K * 2] = L;
			Frame[K * 2 + 1] = R;
		}
		return true;
	}
	return false;
}

bool FPeregonMp3Stream::Next(float& OutL, float& OutR)
{
	if (FramePos >= FrameSamples && !DecodeFrame())
	{
		OutL = OutR = 0.f;
		return false;
	}
	OutL = Frame[FramePos * 2];
	OutR = Frame[FramePos * 2 + 1];
	++FramePos;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Приёмник
// ---------------------------------------------------------------------------------------------
UPeregonRadio::UPeregonRadio(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NumChannels = 2;
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = false;
}

FString UPeregonRadio::GetFolder()
{
	return FPaths::ProjectContentDir() / TEXT("Radio");
}

void UPeregonRadio::ScanStations()
{
	Stations.Reset();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GetFolder() / TEXT("*.mp3")), true, false);
	if (Files.Num() > 0)
	{
		Stations.Add({TEXT("Радио «Р-255»"), GetFolder(), -1});
	}
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(GetFolder() / TEXT("*")), false, true);
	for (const FString& Dir : Dirs)
	{
		FStation S;
		S.Folder = GetFolder() / Dir;
		FString Name = Dir;
		FString RegionPart;
		if (Dir.Split(TEXT("@"), &Name, &RegionPart, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && RegionPart.IsNumeric())
		{
			S.Region = FCString::Atoi(*RegionPart);
		}
		S.Name = Name;
		TArray<FString> Inside;
		IFileManager::Get().FindFiles(Inside, *(S.Folder / TEXT("*.mp3")), true, false);
		if (Inside.Num() > 0)
		{
			Stations.Add(S);
		}
	}
	Station = Stations.Num() > 0 ? Station % Stations.Num() : 0;
}

void UPeregonRadio::NextStation()
{
	ScanStations();
	if (Stations.Num() <= 1 || bLoading)
	{
		NextTrack();
		return;
	}
	Station = (Station + 1) % Stations.Num();
	Playlist.Reset();
	Current = -1;
	NextTrack();
}

FString UPeregonRadio::GetStationName() const
{
	return Stations.IsValidIndex(Station) ? Stations[Station].Name : FString(TEXT("Радио"));
}

int32 UPeregonRadio::GetStationRegion() const
{
	return Stations.IsValidIndex(Station) ? Stations[Station].Region : -1;
}

void UPeregonRadio::ScanFolder()
{
	Playlist.Reset();
	if (Stations.Num() == 0)
	{
		ScanStations();
	}
	const FString Folder = Stations.IsValidIndex(Station) ? Stations[Station].Folder : GetFolder();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.mp3")), true, false);
	for (const FString& File : Files)
	{
		Playlist.Add(Folder / File);
	}
	// Перемешать (Фишер — Йейтс).
	for (int32 I = Playlist.Num() - 1; I > 0; --I)
	{
		Playlist.Swap(I, FMath::RandRange(0, I));
	}
	UE_LOG(LogTemp, Log, TEXT("[Peregon] Радио: %d песен в %s"), Playlist.Num(), *GetFolder());
}

bool UPeregonRadio::Init(int32& SampleRate)
{
	Rate = static_cast<float>(SampleRate);
	return true;
}

void UPeregonRadio::SetPower(bool bOn)
{
	bPower = bOn;
	ParamPower.store(bOn);
	if (bOn && Playlist.Num() == 0)
	{
		ScanFolder();
	}
	if (bOn && Current < 0 && Playlist.Num() > 0)
	{
		NextTrack();
	}
}

void UPeregonRadio::NextTrack()
{
	if (Playlist.Num() == 0)
	{
		ScanFolder();
	}
	if (Playlist.Num() == 0 || bLoading)
	{
		return;
	}
	Current = (Current + 1) % Playlist.Num();
	if (Current == 0 && Playlist.Num() > 2)
	{
		// Круг пройден — снова перемешать.
		for (int32 I = Playlist.Num() - 1; I > 0; --I)
		{
			Playlist.Swap(I, FMath::RandRange(0, I));
		}
	}
	StartLoad(Current);
}

void UPeregonRadio::PrevTrack()
{
	if (Playlist.Num() == 0 || bLoading)
	{
		return;
	}
	Current = (Current - 1 + Playlist.Num()) % Playlist.Num();
	StartLoad(Current);
}

void UPeregonRadio::StartLoad(int32 Index)
{
	const FString Path = Playlist[Index];
	TrackName = FPaths::GetBaseFilename(Path);
	bLoading = true;
	Loading = Async(EAsyncExecution::ThreadPool, [Path]() -> TSharedPtr<FPeregonMp3Stream, ESPMode::ThreadSafe> {
		TArray<uint8> Data;
		if (!FFileHelper::LoadFileToArray(Data, *Path))
		{
			return nullptr;
		}
		return MakeShared<FPeregonMp3Stream, ESPMode::ThreadSafe>(MoveTemp(Data));
	});
}

void UPeregonRadio::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bLoading && Loading.IsReady())
	{
		bLoading = false;
		TSharedPtr<FPeregonMp3Stream, ESPMode::ThreadSafe> Ready = Loading.Get();
		if (Ready.IsValid() && Ready->IsValid())
		{
			FScopeLock Lock(&StreamLock);
			PendingStream = Ready;
			bHasPending.store(true);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Peregon] Радио: не читается %s"), *TrackName);
			bTrackEnded.store(true);
		}
	}
	if (bTrackEnded.exchange(false) && bPower)
	{
		NextTrack();
	}
}

int32 UPeregonRadio::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (bHasPending.load())
	{
		FScopeLock Lock(&StreamLock);
		Stream = PendingStream;
		PendingStream.Reset();
		bHasPending.store(false);
		SrcPos = 0.0;
		PrevL = PrevR = CurL = CurR = 0.f;
	}
	const bool bOn = ParamPower.load();
	const float Static = FMath::Clamp(ParamStatic.load(), 0.f, 1.f);
	const float Outside = FMath::Clamp(ParamOutside.load(), 0.f, 1.f);
	const float Loudness = FMath::Clamp(ParamVolume.load(), 0.f, 1.f) * PeregonAudio::GroupGain(PeregonAudio::EGroup::Radio);
	// Полоса маленького динамика: срез низа ~220 Гц, верха — 3.6 кГц в салоне, ~1.2 кГц снаружи.
	const float Cutoff = FMath::Lerp(3600.f, 1200.f, Outside);
	const float LpA = 1.f - FMath::Exp(-2.f * PI * Cutoff / Rate);
	const float HpA = FMath::Exp(-2.f * PI * 220.f / Rate);
	const double Step = Stream.IsValid() ? static_cast<double>(Stream->GetSampleRate()) / Rate : 1.0;
	const int32 Frames = NumSamples / 2;
	for (int32 F = 0; F < Frames; ++F)
	{
		Fade = FMath::Clamp(Fade + (bOn ? 1.f : -1.f) / (0.08f * Rate), 0.f, 1.f);
		float Music = 0.f;
		if (Stream.IsValid() && Fade > 0.f)
		{
			SrcPos += Step;
			while (SrcPos >= 1.0)
			{
				SrcPos -= 1.0;
				PrevL = CurL;
				PrevR = CurR;
				if (!Stream->Next(CurL, CurR))
				{
					Stream.Reset();
					bTrackEnded.store(true);
					break;
				}
			}
			const float T = static_cast<float>(SrcPos);
			Music = 0.5f * (FMath::Lerp(PrevL, CurL, T) + FMath::Lerp(PrevR, CurR, T));
		}
		// Эфир: белый шум и редкие треск-щелчки (больше — вдали от городов и в грозу).
		Seed = Seed * 1664525u + 1013904223u;
		const float Noise = (static_cast<float>(Seed >> 8) / 8388608.f - 1.f);
		Crackle *= 0.995f;
		if ((Seed & 0xFFFFu) < static_cast<uint32>(3.f + 60.f * Static))
		{
			Crackle = 0.6f * Static;
		}
		const float Air = Noise * (0.02f + 0.22f * Static) + Noise * Crackle;
		const float Signal = Music * (1.f - 0.6f * Static) + (Stream.IsValid() ? Air : Air * 2.f);
		// Фильтры динамика.
		const float Hp = HpA * (Hp1 + Signal - Hp1In);
		Hp1In = Signal;
		Hp1 = Hp;
		Lp1 += LpA * (Hp - Lp1);
		Lp2 += LpA * (Lp1 - Lp2);
		Lp3 += LpA * (Lp2 - Lp3);
		// Лёгкий перегруз дешёвого усилителя.
		const float Driven = std::tanh(Lp3 * 1.8f) / std::tanh(1.8f);
		const float Sample = Driven * Loudness * Fade * FMath::Lerp(1.f, 0.55f, Outside);
		OutAudio[F * 2] = Sample;
		OutAudio[F * 2 + 1] = Sample;
	}
	return NumSamples;
}
