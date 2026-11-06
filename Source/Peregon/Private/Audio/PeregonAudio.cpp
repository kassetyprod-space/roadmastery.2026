#include "Audio/PeregonAudio.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMath.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundAttenuation.h"

#include <cmath>

// ---------------------------------------------------------------------------------------------
// Банк звуков: sounds.txt + загрузка WAV
// ---------------------------------------------------------------------------------------------
namespace PeregonAudioVolume
{
std::atomic<float> Master{1.f};
std::atomic<float> Groups[static_cast<int32>(PeregonAudio::EGroup::Count)] = {{1.f}, {1.f}, {1.f}, {1.f}};
} // namespace PeregonAudioVolume

float PeregonAudio::GroupGain(EGroup InGroup)
{
	const int32 I = FMath::Clamp(static_cast<int32>(InGroup), 0, static_cast<int32>(EGroup::Count) - 1);
	return PeregonAudioVolume::Master.load() * PeregonAudioVolume::Groups[I].load();
}

void PeregonAudio::SetVolumes(float InMaster, float InEngine, float InEffects, float InAmbience, float InRadio)
{
	PeregonAudioVolume::Master.store(FMath::Clamp(InMaster, 0.f, 1.5f));
	PeregonAudioVolume::Groups[0].store(FMath::Clamp(InEngine, 0.f, 1.5f));
	PeregonAudioVolume::Groups[1].store(FMath::Clamp(InEffects, 0.f, 1.5f));
	PeregonAudioVolume::Groups[2].store(FMath::Clamp(InAmbience, 0.f, 1.5f));
	PeregonAudioVolume::Groups[3].store(FMath::Clamp(InRadio, 0.f, 1.5f));
}

namespace PeregonAudioBank
{
struct FBank
{
	bool bLoaded = false;
	TMap<FString, TArray<FString>> Events;           // событие → файлы
	TMap<FString, FPeregonPcmPtr> Cache;             // файл → звук
	FCriticalSection Lock;
};

FBank& Get()
{
	static FBank Bank;
	return Bank;
}

FString Dir()
{
	return FPaths::ProjectContentDir() / TEXT("Sounds");
}

void LoadManifest(FBank& Bank)
{
	Bank.bLoaded = true;
	TArray<FString> Lines;
	const FString File = Dir() / TEXT("sounds.txt");
	if (!FFileHelper::LoadFileToStringArray(Lines, *File))
	{
		UE_LOG(LogTemp, Log, TEXT("[Peregon] %s не найден — все звуки синтезируются"), *File);
		return;
	}
	int32 Mapped = 0;
	for (FString Line : Lines)
	{
		int32 Hash = INDEX_NONE;
		if (Line.FindChar(TEXT('#'), Hash))
		{
			Line.LeftInline(Hash);
		}
		FString Key;
		FString Value;
		if (!Line.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		Key.TrimStartAndEndInline();
		TArray<FString> Files;
		Value.ParseIntoArray(Files, TEXT(","), true);
		for (FString& F : Files)
		{
			F.TrimStartAndEndInline();
		}
		Files.RemoveAll([](const FString& F) { return F.IsEmpty(); });
		if (!Key.IsEmpty() && Files.Num() > 0)
		{
			Bank.Events.Add(Key.ToLower(), Files);
			++Mapped;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[Peregon] Звуки: %d событий из sounds.txt"), Mapped);
}

// Чтение WAV: PCM 8/16/24/32 бит или float32, любое число каналов → моно float.
FPeregonPcmPtr LoadWav(const FString& Path)
{
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *Path) || Data.Num() < 44)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Peregon] Нет файла звука %s"), *Path);
		return nullptr;
	}
	auto U16 = [&Data](int32 At) { return static_cast<uint32>(Data[At]) | (static_cast<uint32>(Data[At + 1]) << 8); };
	auto U32 = [&Data](int32 At) { return static_cast<uint32>(Data[At]) | (static_cast<uint32>(Data[At + 1]) << 8) | (static_cast<uint32>(Data[At + 2]) << 16) |
										   (static_cast<uint32>(Data[At + 3]) << 24); };
	if (FMemory::Memcmp(Data.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(Data.GetData() + 8, "WAVE", 4) != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Peregon] %s — не WAV"), *Path);
		return nullptr;
	}
	uint32 Format = 0;
	uint32 Channels = 0;
	uint32 SampleRate = 0;
	uint32 Bits = 0;
	int32 DataAt = -1;
	uint32 DataSize = 0;
	int32 At = 12;
	while (At + 8 <= Data.Num())
	{
		const uint32 Size = U32(At + 4);
		if (FMemory::Memcmp(Data.GetData() + At, "fmt ", 4) == 0 && At + 24 <= Data.Num())
		{
			Format = U16(At + 8);
			Channels = U16(At + 10);
			SampleRate = U32(At + 12);
			Bits = U16(At + 22);
			if (Format == 0xFFFE && At + 34 <= Data.Num())
			{
				Format = U16(At + 32); // WAVE_FORMAT_EXTENSIBLE: подтип
			}
		}
		else if (FMemory::Memcmp(Data.GetData() + At, "data", 4) == 0)
		{
			DataAt = At + 8;
			DataSize = FMath::Min<uint32>(Size, static_cast<uint32>(Data.Num() - DataAt));
			break;
		}
		At += 8 + static_cast<int32>(Size + (Size & 1u));
	}
	const uint32 Bytes = Bits / 8;
	if (DataAt < 0 || Channels == 0 || Bytes == 0 || SampleRate == 0 || (Format != 1 && Format != 3))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Peregon] %s: формат не поддерживается (нужен PCM или float)"), *Path);
		return nullptr;
	}
	TSharedPtr<FPeregonPcm, ESPMode::ThreadSafe> Pcm = MakeShared<FPeregonPcm, ESPMode::ThreadSafe>();
	Pcm->SampleRate = static_cast<int32>(SampleRate);
	Pcm->Name = FPaths::GetCleanFilename(Path);
	const uint32 Frames = DataSize / (Bytes * Channels);
	Pcm->Samples.SetNumUninitialized(static_cast<int32>(Frames));
	for (uint32 F = 0; F < Frames; ++F)
	{
		float Sum = 0.f;
		for (uint32 C = 0; C < Channels; ++C)
		{
			const int32 P = DataAt + static_cast<int32>((F * Channels + C) * Bytes);
			float V = 0.f;
			if (Format == 3 && Bytes == 4)
			{
				const uint32 Raw = U32(P);
				FMemory::Memcpy(&V, &Raw, 4);
			}
			else if (Bytes == 1)
			{
				V = (static_cast<float>(Data[P]) - 128.f) / 128.f;
			}
			else if (Bytes == 2)
			{
				V = static_cast<float>(static_cast<int16>(U16(P))) / 32768.f;
			}
			else if (Bytes == 3)
			{
				int32 S = static_cast<int32>(Data[P] | (Data[P + 1] << 8) | (Data[P + 2] << 16));
				if (S & 0x800000)
				{
					S |= ~0xFFFFFF;
				}
				V = static_cast<float>(S) / 8388608.f;
			}
			else if (Bytes == 4)
			{
				V = static_cast<float>(static_cast<int32>(U32(P))) / 2147483648.f;
			}
			Sum += V;
		}
		Pcm->Samples[static_cast<int32>(F)] = Sum / static_cast<float>(Channels);
	}
	return Pcm;
}
} // namespace PeregonAudioBank

TArray<FPeregonPcmPtr> PeregonAudio::GetClips(const FString& Event)
{
	using namespace PeregonAudioBank;
	FBank& Bank = Get();
	FScopeLock Guard(&Bank.Lock);
	if (!Bank.bLoaded)
	{
		LoadManifest(Bank);
	}
	TArray<FPeregonPcmPtr> Out;
	const TArray<FString>* Files = Bank.Events.Find(Event.ToLower());
	if (!Files)
	{
		return Out;
	}
	for (const FString& File : *Files)
	{
		FPeregonPcmPtr* Cached = Bank.Cache.Find(File);
		if (!Cached)
		{
			Cached = &Bank.Cache.Add(File, LoadWav(Dir() / File));
		}
		if (Cached->IsValid())
		{
			Out.Add(*Cached);
		}
	}
	return Out;
}

FPeregonPcmPtr PeregonAudio::GetRandomClip(const FString& Event)
{
	const TArray<FPeregonPcmPtr> Clips = GetClips(Event);
	return Clips.Num() > 0 ? Clips[FMath::RandRange(0, Clips.Num() - 1)] : nullptr;
}

TArray<FString> PeregonAudio::FindEvents(const FString& Prefix)
{
	using namespace PeregonAudioBank;
	FBank& Bank = Get();
	FScopeLock Guard(&Bank.Lock);
	if (!Bank.bLoaded)
	{
		LoadManifest(Bank);
	}
	TArray<FString> Out;
	for (const TPair<FString, TArray<FString>>& Pair : Bank.Events)
	{
		if (Pair.Key.StartsWith(Prefix.ToLower()))
		{
			Out.Add(Pair.Key);
		}
	}
	return Out;
}

namespace PeregonAudioEvents
{
EPeregonProcedural Fallback(const FString& Event)
{
	if (Event.StartsWith(TEXT("door.close")) || Event.StartsWith(TEXT("hood.close")) || Event.StartsWith(TEXT("trunk.close")))
	{
		return EPeregonProcedural::Thump;
	}
	if (Event.StartsWith(TEXT("gear.grind")))
	{
		return EPeregonProcedural::Grind;
	}
	if (Event.StartsWith(TEXT("gear.")))
	{
		return EPeregonProcedural::Clunk;
	}
	if (Event.StartsWith(TEXT("handbrake")))
	{
		return EPeregonProcedural::Ratchet;
	}
	if (Event.StartsWith(TEXT("jump.land")))
	{
		return EPeregonProcedural::Land;
	}
	if (Event.StartsWith(TEXT("map.")))
	{
		return EPeregonProcedural::Paper;
	}
	if (Event == TEXT("horn"))
	{
		return EPeregonProcedural::Horn;
	}
	if (Event.StartsWith(TEXT("camera.")))
	{
		return EPeregonProcedural::Click;
	}
	if (Event.StartsWith(TEXT("thunder")))
	{
		return EPeregonProcedural::Thunder;
	}
	if (Event.StartsWith(TEXT("ambient.dog")))
	{
		return EPeregonProcedural::Bark;
	}
	if (Event.StartsWith(TEXT("ambient.crow")))
	{
		return EPeregonProcedural::Crow;
	}
	if (Event.StartsWith(TEXT("train.horn")))
	{
		return EPeregonProcedural::Train;
	}
	if (Event.StartsWith(TEXT("traffic.pass")))
	{
		return EPeregonProcedural::Engine2;
	}
	if (Event.StartsWith(TEXT("wiper")))
	{
		return EPeregonProcedural::Wiper;
	}
	if (Event == TEXT("footstep.gravel") || Event == TEXT("footstep.dirt"))
	{
		return EPeregonProcedural::StepGravel;
	}
	if (Event == TEXT("footstep.grass") || Event == TEXT("footstep.snow"))
	{
		return EPeregonProcedural::StepSoft;
	}
	if (Event.StartsWith(TEXT("footstep.")))
	{
		return EPeregonProcedural::StepHard;
	}
	return EPeregonProcedural::Click; // открыть дверь/капот/бардачок, ключ, свет, интерфейс
}
} // namespace PeregonAudioEvents

void PeregonAudio::PlayEvent(const UObject* WorldContext, const FString& Event, const FVector& Location, float Volume, float Pitch)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	AActor* Owner = const_cast<AActor*>(Cast<AActor>(WorldContext));
	if (!World || !Owner || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UPeregonSynth* Voice = NewObject<UPeregonSynth>(Owner);
	Voice->SetupOneShot(Event, PeregonAudioEvents::Fallback(Event), Pitch * FMath::FRandRange(0.96f, 1.04f));
	Voice->SetLevel(Volume, 0.f);
	Voice->SetWorldLocation(Location);
	Voice->RegisterComponent();
	Voice->Start();
}

void PeregonAudio::PlayFootstep(const AActor* Walker, const FVector& FootLocation, float Loudness)
{
	if (!Walker || !Walker->GetWorld())
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PeregonFootstep), false, Walker);
	Params.bReturnPhysicalMaterial = true;
	FHitResult Hit;
	FString Surface = TEXT("asphalt");
	if (Walker->GetWorld()->LineTraceSingleByChannel(Hit, FootLocation + FVector(0.f, 0.f, 30.f), FootLocation - FVector(0.f, 0.f, 60.f), ECC_Visibility, Params))
	{
		// Порядок как в DefaultEngine.ini → PhysicalSurfaces.
		switch (Hit.PhysMaterial.IsValid() ? Hit.PhysMaterial->SurfaceType.GetValue() : SurfaceType_Default)
		{
		case SurfaceType2: Surface = TEXT("gravel"); break;
		case SurfaceType3: Surface = TEXT("dirt"); break;
		case SurfaceType4: Surface = TEXT("mud"); break;
		case SurfaceType5: Surface = TEXT("snow"); break;
		case SurfaceType7: Surface = TEXT("grass"); break;
		case SurfaceType8: Surface = TEXT("sand"); break;
		default: break;
		}
	}
	FString Event = TEXT("footstep.") + Surface;
	if (GetClips(Event).Num() == 0 && GetClips(TEXT("footstep.any")).Num() > 0)
	{
		Event = TEXT("footstep.any");
	}
	PlayEvent(Walker, Event, FootLocation, 0.55f * Loudness, FMath::FRandRange(0.92f, 1.08f));
}

// ---------------------------------------------------------------------------------------------
// Синтезатор-голос
// ---------------------------------------------------------------------------------------------
void UPeregonSynth::FBiquad::Bandpass(float Freq, float Q, float SampleRate)
{
	const float W = 2.f * PI * FMath::Clamp(Freq, 10.f, SampleRate * 0.45f) / SampleRate;
	const float Alpha = FMath::Sin(W) / (2.f * Q);
	const float A0 = 1.f + Alpha;
	B0 = Alpha / A0;
	B1 = 0.f;
	B2 = -Alpha / A0;
	A1 = -2.f * FMath::Cos(W) / A0;
	A2 = (1.f - Alpha) / A0;
}

void UPeregonSynth::FBiquad::Lowpass(float Freq, float Q, float SampleRate)
{
	const float W = 2.f * PI * FMath::Clamp(Freq, 10.f, SampleRate * 0.45f) / SampleRate;
	const float Alpha = FMath::Sin(W) / (2.f * Q);
	const float CosW = FMath::Cos(W);
	const float A0 = 1.f + Alpha;
	B0 = (1.f - CosW) * 0.5f / A0;
	B1 = (1.f - CosW) / A0;
	B2 = B0;
	A1 = -2.f * CosW / A0;
	A2 = (1.f - Alpha) / A0;
}

UPeregonSynth::UPeregonSynth(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = false;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	AttenuationOverrides.bAttenuate = true;
	AttenuationOverrides.bSpatialize = true;
	AttenuationOverrides.AttenuationShapeExtents = FVector(250.f, 0.f, 0.f);
	AttenuationOverrides.FalloffDistance = 6000.f;
}

void UPeregonSynth::SetupEngine()
{
	SetGroup(PeregonAudio::EGroup::Engine);
	Mode = EMode::Engine;
	Procedural = EPeregonProcedural::Engine;
	Layers.Reset();
	for (const FString& Event : PeregonAudio::FindEvents(TEXT("engine.")))
	{
		// engine.on.2500 / engine.off.2500 / engine.idle
		TArray<FString> Parts;
		Event.ParseIntoArray(Parts, TEXT("."));
		FLayer L;
		if (Parts.Num() == 3 && (Parts[1] == TEXT("on") || Parts[1] == TEXT("off")))
		{
			L.bOn = Parts[1] == TEXT("on");
			L.Rpm = FMath::Max(300.f, FCString::Atof(*Parts[2]));
		}
		else if (Event == TEXT("engine.idle"))
		{
			L.Rpm = 900.f;
		}
		else
		{
			continue;
		}
		L.Clip = PeregonAudio::GetRandomClip(Event);
		if (L.Clip.IsValid() && L.Clip->Samples.Num() > 256)
		{
			Layers.Add(L);
		}
	}
	Layers.Sort([](const FLayer& A, const FLayer& B) { return A.Rpm < B.Rpm; });
	StarterClip = PeregonAudio::GetRandomClip(TEXT("engine.start"));
	UE_LOG(LogTemp, Log, TEXT("[Peregon] Звук мотора: %s"), Layers.Num() > 0 ? *FString::Printf(TEXT("%d слоёв из файлов"), Layers.Num()) : TEXT("синтез"));
}

void UPeregonSynth::SetupLoop(const FString& Event, EPeregonProcedural Fallback)
{
	Mode = EMode::Loop;
	Procedural = Fallback;
	LoopClip = PeregonAudio::GetRandomClip(Event);
}

void UPeregonSynth::SetupOneShot(const FString& Event, EPeregonProcedural Fallback, float Pitch)
{
	Mode = EMode::OneShot;
	Procedural = Fallback;
	OneShotPitch = Pitch;
	LoopClip = PeregonAudio::GetRandomClip(Event);
	AttenuationOverrides.FalloffDistance = 2500.f;
}

bool UPeregonSynth::Init(int32& SampleRate)
{
	NumChannels = 1;
	Rate = static_cast<float>(FMath::Max(8000, SampleRate));
	Seed = static_cast<uint32>(FMath::Rand()) | 1u;
	ExciteDecay = FMath::Exp(-1.f / (0.0028f * Rate));
	// Выхлоп: несколько резонансов трубы и кузова.
	Res[0].Bandpass(85.f, 1.8f, Rate);
	Res[1].Bandpass(190.f, 2.6f, Rate);
	Res[2].Bandpass(470.f, 3.5f, Rate);
	Res[3].Bandpass(1300.f, 3.f, Rate);
	Hiss.Bandpass(2800.f, 1.2f, Rate);
	return true;
}

void UPeregonSynth::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Mode == EMode::OneShot && bFinished.load())
	{
		Stop();
		DestroyComponent();
	}
}

float UPeregonSynth::Noise()
{
	// xorshift32 → [-1, 1]
	Seed ^= Seed << 13;
	Seed ^= Seed >> 17;
	Seed ^= Seed << 5;
	return static_cast<float>(Seed) / 2147483648.f - 1.f;
}

float UPeregonSynth::ReadLooped(const FPeregonPcm& Clip, double& Pos, double Step)
{
	const int32 N = Clip.Samples.Num();
	if (N < 4)
	{
		return 0.f;
	}
	// Шов петли — перекрёстное затухание 30 мс, чтобы не щёлкало на неидеальных петлях.
	const int32 Fade = FMath::Min(N / 4, static_cast<int32>(Clip.SampleRate * 0.03f));
	const int32 Loop = N - Fade;
	if (Pos >= Loop)
	{
		Pos -= Loop;
	}
	auto At = [&Clip, N](double P) {
		const int32 I = FMath::Clamp(static_cast<int32>(P), 0, N - 2);
		const float T = static_cast<float>(P - I);
		return Clip.Samples[I] + (Clip.Samples[I + 1] - Clip.Samples[I]) * T;
	};
	float V = At(Pos);
	if (Pos < Fade)
	{
		const float W = static_cast<float>(Pos / Fade);
		V = V * W + At(Pos + Loop) * (1.f - W);
	}
	Pos += Step;
	return V;
}

float UPeregonSynth::EngineSample()
{
	const float TargetRpm = ParamRpm.load();
	const float Load = FMath::Clamp(ParamLoad.load(), 0.f, 1.f);
	const bool bRunning = ParamRunning.load();
	const bool bCranking = ParamCranking.load();
	Rpm += (TargetRpm - Rpm) * 0.003f;
	const float TargetLevel = (bRunning || bCranking) ? 1.f : 0.f;
	Level += (TargetLevel - Level) * (TargetLevel > Level ? 0.002f : 0.0004f);
	float Out = 0.f;

	if (Layers.Num() > 0 && bRunning)
	{
		// Слои из файлов: равномощное смешивание соседних по оборотам, скорость = обороты / обороты слоя.
		bool bHasOff = false;
		for (const FLayer& L : Layers)
		{
			bHasOff |= !L.bOn;
		}
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bOn = Pass == 0;
			if (!bOn && !bHasOff)
			{
				continue;
			}
			TArray<int32, TInlineAllocator<8>> Idx;
			for (int32 I = 0; I < Layers.Num(); ++I)
			{
				if (Layers[I].bOn == bOn || (!bHasOff && bOn))
				{
					Idx.Add(I);
				}
			}
			if (Idx.Num() == 0)
			{
				continue;
			}
			const float LoadGain = bHasOff ? (bOn ? FMath::Sqrt(Load) : FMath::Sqrt(1.f - Load)) : (0.55f + 0.45f * Load);
			const float R = FMath::Max(Rpm, 200.f);
			for (int32 K = 0; K < Idx.Num(); ++K)
			{
				FLayer& L = Layers[Idx[K]];
				float W = 0.f;
				const float Lo = K > 0 ? Layers[Idx[K - 1]].Rpm : -1.f;
				const float Hi = K + 1 < Idx.Num() ? Layers[Idx[K + 1]].Rpm : -1.f;
				if (R <= L.Rpm)
				{
					W = Lo < 0.f ? 1.f : FMath::Sin(0.5f * PI * FMath::Clamp(FMath::Loge(R / Lo) / FMath::Loge(L.Rpm / Lo), 0.f, 1.f));
				}
				else
				{
					W = Hi < 0.f ? 1.f : FMath::Cos(0.5f * PI * FMath::Clamp(FMath::Loge(R / L.Rpm) / FMath::Loge(Hi / L.Rpm), 0.f, 1.f));
				}
				const double Step = FMath::Clamp(R / L.Rpm, 0.4f, 2.5f) * L.Clip->SampleRate / Rate;
				const float V = ReadLooped(*L.Clip, L.Pos, Step);
				Out += V * W * LoadGain;
			}
		}
		Out *= Level;
	}
	else
	{
		// Синтез: 4 цилиндра, 2 вспышки на оборот; вспышка возбуждает резонансы выхлопа.
		const float Revs = FMath::Max(Rpm, 0.f) / 60.f;
		const float PrevCycle = Cycle;
		Cycle += 2.f * PI * Revs / Rate;
		if (Cycle >= 4.f * PI)
		{
			Cycle -= 4.f * PI;
		}
		for (int32 K = 0; K < 4; ++K)
		{
			const float At = static_cast<float>(K) * PI;
			const bool bCross = (PrevCycle < At && Cycle >= At) || (K == 0 && Cycle < PrevCycle);
			if (bCross)
			{
				const float Power = bRunning ? (0.35f + 0.65f * Load) : 0.18f; // без зажигания — только компрессия
				Excite += Cylinder[K] * Power * (0.92f + 0.16f * (Noise() * 0.5f + 0.5f));
			}
		}
		Excite *= ExciteDecay;
		const float Rasp = Excite * (0.75f + 0.5f * Noise());
		float S = Res[0].Process(Rasp) * 1.f + Res[1].Process(Rasp) * 0.75f + Res[2].Process(Rasp) * 0.35f +
				  Res[3].Process(Rasp) * (0.08f + 0.18f * Load);
		FirePhase += 2.f * PI * Revs * 2.f / Rate;
		if (FirePhase > 2.f * PI)
		{
			FirePhase -= 2.f * PI;
		}
		S += FMath::Sin(FirePhase) * Excite * 0.6f;
		S += Hiss.Process(Noise()) * (Rpm / 7000.f) * (0.02f + 0.05f * Load);
		Out = std::tanh(S * 2.2f) * 0.5f * Level;
	}

	// Стартер.
	if (bCranking)
	{
		if (StarterClip.IsValid())
		{
			Out += ReadLooped(*StarterClip, StarterPos, StarterClip->SampleRate / Rate) * 0.8f;
		}
		else
		{
			Time += 1.f / Rate;
			const float Whine = FMath::Sin(2.f * PI * 1150.f * Time) * 0.5f + FMath::Sin(2.f * PI * 2300.f * Time) * 0.2f;
			Out += Whine * (0.05f + 0.03f * FMath::Sin(2.f * PI * 7.f * Time));
		}
	}
	return Out;
}

float UPeregonSynth::ProceduralSample()
{
	const float N = Noise();
	switch (Procedural)
	{
	case EPeregonProcedural::Wind:
	{
		// Интенсивность — скорость, м/с.
		Tone.Lowpass(250.f + Intensity * 35.f, 0.8f, Rate);
		Brown = Brown * 0.995f + N * 0.05f;
		return Tone.Process(N) * FMath::Square(FMath::Clamp(Intensity / 40.f, 0.f, 1.5f)) * 0.5f + Brown * 0.02f * FMath::Clamp(Intensity / 30.f, 0.f, 1.f);
	}
	case EPeregonProcedural::TireRoll:
	{
		Tone.Lowpass(120.f + Intensity * 9.f, 0.9f, Rate);
		Brown = Brown * 0.998f + N * 0.04f;
		const float Grit = (Noise() > 0.995f - 0.004f * FMath::Clamp(Intensity / 20.f, 0.f, 1.f)) ? Noise() : 0.f; // мелкие камешки
		return (Tone.Process(N) * 0.6f + Brown * 0.1f + Grit * 0.3f) * FMath::Clamp(Intensity / 25.f, 0.f, 1.2f);
	}
	case EPeregonProcedural::Rain:
	{
		// Шелест (высокочастотный шум) + россыпь капель, чаще при сильном дожде. Intensity 0..1.
		const float Amount = FMath::Clamp(Intensity, 0.f, 1.f);
		Hiss.Bandpass(5200.f, 0.5f, Rate);
		Tone.Lowpass(900.f, 0.7f, Rate);
		Excite *= 0.993f;
		if (Noise() > 0.9995f - 0.004f * Amount)
		{
			Excite = 0.4f + 0.6f * FMath::Abs(Noise());
		}
		return Hiss.Process(N) * 0.35f * Amount + Tone.Process(N * Excite) * 1.4f * Amount;
	}
	case EPeregonProcedural::Horn:
	{
		// Два рупорных сигнала ~420 и ~510 Гц: почти меандр, срезанный по верху.
		// Фазы — по модулю 1, чтобы за долгую поездку не терялась точность.
		Cycle = FMath::Frac(Cycle + 420.f / Rate);
		FirePhase = FMath::Frac(FirePhase + 512.f / Rate);
		const float A = FMath::Sin(2.f * PI * Cycle);
		const float B = FMath::Sin(2.f * PI * FirePhase);
		Tone.Lowpass(2400.f, 0.7f, Rate);
		return Tone.Process(std::tanh(4.f * A) * 0.5f + std::tanh(4.f * B) * 0.4f) * 0.6f;
	}
	case EPeregonProcedural::Skid:
	{
		// Визг: узкая полоса вокруг ~1 кГц, «гуляет».
		Time += 1.f / Rate;
		Tone.Bandpass(950.f + 120.f * FMath::Sin(2.f * PI * 3.f * Time), 14.f, Rate);
		Tone2.Bandpass(1900.f, 10.f, Rate);
		return (Tone.Process(N) * 2.5f + Tone2.Process(N) * 1.2f) * FMath::Clamp(Intensity, 0.f, 1.f);
	}
	default:
		break;
	}

	// Разовые звуки: огибающая + фильтрованный шум/тон.
	const float Dt = 1.f / Rate;
	Age += Dt;
	auto Decay = [this](float Tau) { return FMath::Exp(-Age / Tau); };
	float S = 0.f;
	float Length = 0.3f;
	switch (Procedural)
	{
	case EPeregonProcedural::Thump:
		Tone.Lowpass(240.f, 1.2f, Rate);
		S = (FMath::Sin(2.f * PI * 62.f * Age) * 0.9f + Tone.Process(N) * 1.4f) * Decay(0.07f) + N * 0.25f * Decay(0.008f);
		Length = 0.5f;
		break;
	case EPeregonProcedural::Click:
		Tone.Bandpass(2400.f, 6.f, Rate);
		S = Tone.Process(N) * 2.5f * Decay(0.006f) + FMath::Sin(2.f * PI * 1600.f * Age) * 0.2f * Decay(0.01f);
		Length = 0.08f;
		break;
	case EPeregonProcedural::Clunk:
		Tone.Bandpass(620.f, 8.f, Rate);
		Tone2.Bandpass(1450.f, 9.f, Rate);
		S = (Tone.Process(N) * 2.f + Tone2.Process(N) * 1.2f) * Decay(0.025f);
		Length = 0.2f;
		break;
	case EPeregonProcedural::Grind:
		Tone.Bandpass(2600.f + 400.f * FMath::Sin(2.f * PI * 90.f * Age), 3.f, Rate);
		S = Tone.Process(N) * 1.6f * (Age < 0.35f ? 1.f : Decay(0.05f)) * (0.6f + 0.4f * FMath::Sin(2.f * PI * 47.f * Age));
		Length = 0.55f;
		break;
	case EPeregonProcedural::Ratchet:
	{
		Tone.Bandpass(3200.f, 7.f, Rate);
		const float Tick = FMath::Fmod(Age, 0.045f);
		S = Tone.Process(N) * 2.f * FMath::Exp(-Tick / 0.004f) * (Age < 0.25f ? 1.f : 0.f);
		Length = 0.3f;
		break;
	}
	case EPeregonProcedural::StepHard:
		Tone.Bandpass(900.f, 1.5f, Rate);
		S = Tone.Process(N) * 1.4f * Decay(0.018f) + FMath::Sin(2.f * PI * 110.f * Age) * 0.3f * Decay(0.02f);
		Length = 0.15f;
		break;
	case EPeregonProcedural::StepGravel:
	{
		// Хруст: россыпь коротких щелчков.
		Tone.Bandpass(2200.f, 1.2f, Rate);
		const float Crunch = Noise() > 0.92f ? N : N * 0.15f;
		S = Tone.Process(Crunch) * 1.6f * Decay(0.06f);
		Length = 0.25f;
		break;
	}
	case EPeregonProcedural::StepSoft:
		Tone.Bandpass(500.f, 0.9f, Rate);
		S = Tone.Process(N) * 0.9f * Decay(0.05f);
		Length = 0.2f;
		break;
	case EPeregonProcedural::Land:
		Tone.Lowpass(300.f, 1.f, Rate);
		S = (Tone.Process(N) * 1.5f + FMath::Sin(2.f * PI * 70.f * Age) * 0.6f) * Decay(0.06f);
		Length = 0.35f;
		break;
	case EPeregonProcedural::Wiper:
	{
		// Гудение моторчика и «вжик» резинки: полоса шума скользит вверх-вниз за ход щётки.
		const float Sweep = FMath::Sin(PI * FMath::Clamp(Age / 0.55f, 0.f, 1.f));
		Tone.Bandpass(700.f + 900.f * Sweep, 3.f, Rate);
		S = Tone.Process(N) * 0.9f * Sweep + FMath::Sin(2.f * PI * 95.f * Age) * 0.12f * Sweep;
		Length = 0.6f;
		break;
	}
	case EPeregonProcedural::Thunder:
	{
		// Рокот: бурый шум через низкий фильтр, огибающая из нескольких «волн» раската.
		Brown = Brown * 0.997f + N * 0.03f;
		Tone.Lowpass(160.f, 0.7f, Rate);
		const float Roll = 0.55f + 0.45f * FMath::Sin(2.f * PI * 0.9f * Age + 1.3f * FMath::Sin(2.f * PI * 0.37f * Age));
		const float Shape = FMath::Min(1.f, Age / 0.25f) * FMath::Exp(-Age / 2.6f);
		S = (Tone.Process(N) * 2.2f + Brown * 3.f) * Roll * Shape;
		Length = 8.f;
		break;
	}
	case EPeregonProcedural::Bark:
	{
		// Два-три «гав»: гармоничный тон с формантой, высота падает внутри каждого.
		const float Period = 0.34f;
		const int32 Index = FMath::FloorToInt(Age / Period);
		const float Local = Age - Index * Period;
		const float On = Index < 3 && Local < 0.14f ? FMath::Sin(PI * Local / 0.14f) : 0.f;
		Cycle = FMath::Frac(Cycle + (520.f - 900.f * Local) / Rate);
		const float Saw = 2.f * Cycle - 1.f;
		Tone.Bandpass(1100.f, 2.f, Rate);
		Tone2.Bandpass(2400.f, 3.f, Rate);
		S = (Tone.Process(Saw + N * 0.3f) * 1.6f + Tone2.Process(Saw) * 0.6f) * On;
		Length = 1.05f;
		break;
	}
	case EPeregonProcedural::Crow:
	{
		// «Кар»: хриплый тон ~900 Гц с шумом, два раза.
		const float Local = FMath::Fmod(Age, 0.55f);
		const float On = Age < 1.1f && Local < 0.3f ? FMath::Sin(PI * Local / 0.3f) : 0.f;
		Cycle = FMath::Frac(Cycle + (880.f - 200.f * Local) / Rate);
		const float Saw = 2.f * Cycle - 1.f;
		Tone.Bandpass(1500.f, 2.5f, Rate);
		S = Tone.Process(Saw * (0.7f + 0.6f * N)) * 1.4f * On;
		Length = 1.15f;
		break;
	}
	case EPeregonProcedural::Train:
	{
		// Тифон тепловоза: два низких тона (~300 и ~370 Гц), долгий гудок.
		Cycle = FMath::Frac(Cycle + 300.f / Rate);
		FirePhase = FMath::Frac(FirePhase + 372.f / Rate);
		const float Shape = FMath::Min(1.f, Age / 0.15f) * (Age < 2.2f ? 1.f : FMath::Exp(-(Age - 2.2f) / 0.2f));
		Tone.Lowpass(1600.f, 0.7f, Rate);
		S = Tone.Process(std::tanh(3.f * FMath::Sin(2.f * PI * Cycle)) * 0.5f + std::tanh(3.f * FMath::Sin(2.f * PI * FirePhase)) * 0.45f) * Shape * 0.7f;
		Length = 3.f;
		break;
	}
	case EPeregonProcedural::Engine2:
	{
		// Проезд: рокот мотора и шин, нарастает и уходит с понижением тона.
		const float T = Age / 2.6f;
		const float Doppler = T < 0.5f ? 1.12f : 0.88f;
		Cycle = FMath::Frac(Cycle + 70.f * Doppler / Rate);
		const float Shape = FMath::Exp(-FMath::Square((T - 0.5f) * 4.f));
		Tone.Lowpass(500.f, 0.8f, Rate);
		S = (FMath::Sin(2.f * PI * Cycle) * 0.5f + Tone.Process(N) * 1.3f) * Shape;
		Length = 2.6f;
		break;
	}
	case EPeregonProcedural::Paper:
		Tone.Bandpass(4200.f, 0.8f, Rate);
		S = Tone.Process(N) * 1.2f * (0.5f + 0.5f * FMath::Sin(2.f * PI * 23.f * Age)) * Decay(0.12f);
		Length = 0.5f;
		break;
	default:
		Length = 0.f;
		break;
	}
	if (Age > Length)
	{
		bFinished.store(true);
	}
	return S * 0.5f;
}

int32 UPeregonSynth::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const float TargetGain = FMath::Clamp(ParamVolume.load(), 0.f, 2.f) * PeregonAudio::GroupGain(static_cast<PeregonAudio::EGroup>(Group.load()));
	const float TargetIntensity = ParamIntensity.load();
	const bool bMuffled = ParamMuffled.load();
	if (!bStarted)
	{
		bStarted = true;
		Gain = TargetGain;
		Intensity = TargetIntensity;
	}
	for (int32 I = 0; I < NumSamples; ++I)
	{
		Gain += (TargetGain - Gain) * 0.0015f;
		Intensity += (TargetIntensity - Intensity) * 0.0015f;
		float V = 0.f;
		switch (Mode)
		{
		case EMode::Engine:
			V = EngineSample();
			break;
		case EMode::Loop:
			if (LoopClip.IsValid())
			{
				// Интенсивность меняет скорость петли: 0 → 0.8×, 1 → 1.3×.
				V = ReadLooped(*LoopClip, ClipPos, (0.8 + 0.5 * FMath::Clamp(Intensity / 30.f, 0.f, 1.f)) * LoopClip->SampleRate / Rate);
			}
			else
			{
				V = ProceduralSample();
			}
			break;
		case EMode::OneShot:
			if (LoopClip.IsValid())
			{
				const int32 Idx = static_cast<int32>(ClipPos);
				if (Idx >= LoopClip->Samples.Num() - 1)
				{
					bFinished.store(true);
					V = 0.f;
				}
				else
				{
					const float T = static_cast<float>(ClipPos - Idx);
					V = LoopClip->Samples[Idx] + (LoopClip->Samples[Idx + 1] - LoopClip->Samples[Idx]) * T;
					ClipPos += OneShotPitch * LoopClip->SampleRate / Rate;
				}
			}
			else
			{
				V = ProceduralSample();
			}
			break;
		}
		// «Из салона» — простой однополюсный фильтр: верха глушатся кузовом.
		const float Cut = bMuffled ? 0.12f : 1.f;
		Muffle += (V - Muffle) * Cut;
		OutAudio[I] = FMath::Clamp(Muffle * Gain, -1.f, 1.f);
	}
	return NumSamples;
}
