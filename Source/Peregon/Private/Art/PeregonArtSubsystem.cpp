#include "Art/PeregonArtSubsystem.h"

#include "Components/MeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/CompositeFont.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Styling/CoreStyle.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace PeregonTexGen
{
uint32 Hash(uint32 A, uint32 B, uint32 C)
{
	uint32 H = A * 0xCC9E2D51u ^ (B + 0x6B43A9B5u) * 0x1B873593u;
	H = (H << 13) | (H >> 19);
	H ^= C * 0xE6546B64u;
	H ^= H >> 16;
	H *= 0x85EBCA6Bu;
	H ^= H >> 13;
	H *= 0xC2B2AE35u;
	H ^= H >> 16;
	return H;
}

float Rand(int32 X, int32 Y, uint32 Seed)
{
	return static_cast<float>(Hash(static_cast<uint32>(X), static_cast<uint32>(Y), Seed) >> 8) / 16777216.f;
}

// Гладкий шум, бесшовный по размеру текстуры: решётка с периодом W/Cell × H/Cell.
float TileNoise(int32 X, int32 Y, int32 Cell, int32 W, int32 H, uint32 Seed)
{
	const int32 Pw = FMath::Max(1, W / Cell);
	const int32 Ph = FMath::Max(1, H / Cell);
	const float Fx = static_cast<float>(X) / Cell;
	const float Fy = static_cast<float>(Y) / Cell;
	const int32 X0 = FMath::FloorToInt(Fx);
	const int32 Y0 = FMath::FloorToInt(Fy);
	const float Tx = FMath::SmoothStep(0.f, 1.f, Fx - X0);
	const float Ty = FMath::SmoothStep(0.f, 1.f, Fy - Y0);
	auto V = [&](int32 Ix, int32 Iy) { return Rand(((Ix % Pw) + Pw) % Pw, ((Iy % Ph) + Ph) % Ph, Seed); };
	const float A = FMath::Lerp(V(X0, Y0), V(X0 + 1, Y0), Tx);
	const float B = FMath::Lerp(V(X0, Y0 + 1), V(X0 + 1, Y0 + 1), Tx);
	return FMath::Lerp(A, B, Ty);
}

FColor Rgb(float R, float G, float B)
{
	return FColor(static_cast<uint8>(FMath::Clamp(R, 0.f, 255.f)), static_cast<uint8>(FMath::Clamp(G, 0.f, 255.f)),
		static_cast<uint8>(FMath::Clamp(B, 0.f, 255.f)), 255);
}

FColor Scale(const FColor& Base, float K)
{
	return Rgb(Base.R * K, Base.G * K, Base.B * K);
}

FColor Gray(float V)
{
	return Rgb(V, V, V);
}

// Асфальт: серый с вкраплениями щебня.
FColor AsphaltPixel(int32 X, int32 Y, int32 W, int32 H)
{
	const float N = TileNoise(X, Y, 8, W, H, 11) - 0.5f;
	const float R = Rand(X, Y, 12);
	float V = 78.f + N * 20.f + (R - 0.5f) * 10.f;
	if (R > 0.93f)
	{
		V += 35.f;
	}
	else if (R < 0.05f)
	{
		V -= 18.f;
	}
	return Rgb(V, V, V * 1.02f);
}

void Draw(EPeregonTex Type, int32& W, int32& H, TArray<FColor>& Px)
{
	W = 64;
	H = 64;
	switch (Type)
	{
	case EPeregonTex::White: W = H = 4; break;
	case EPeregonTex::Paint:
	case EPeregonTex::Plastic:
	case EPeregonTex::Fabric:
	case EPeregonTex::Rubber:
	case EPeregonTex::Glass: W = H = 32; break;
	case EPeregonTex::Chrome: W = H = 16; break;
	case EPeregonTex::RoadMarked: W = 64; H = 128; break;
	case EPeregonTex::BirchBark:
	case EPeregonTex::PineBark: W = 32; H = 64; break;
	default: break;
	}
	Px.SetNumUninitialized(W * H);

	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const float R = Rand(X, Y, static_cast<uint32>(Type) * 977u + 3u);
			FColor C = FColor::White;
			switch (Type)
			{
			case EPeregonTex::White:
				break;
			case EPeregonTex::Paint:
				C = Gray(238.f + (TileNoise(X, Y, 8, W, H, 21) - 0.5f) * 14.f + (R - 0.5f) * 8.f);
				break;
			case EPeregonTex::Plastic:
				C = Gray(215.f + (R - 0.5f) * 20.f);
				break;
			case EPeregonTex::Fabric:
				C = Gray((((X >> 1) + (Y >> 1)) & 1 ? 230.f : 192.f) + (R - 0.5f) * 12.f);
				break;
			case EPeregonTex::Rubber:
			{
				const bool bGroove = (Y % 8) < 2 || (X % 16) == 0;
				C = Gray((bGroove ? 60.f : 140.f) * (0.9f + 0.1f * R));
				break;
			}
			case EPeregonTex::Chrome:
				C = Gray(200.f + 50.f * Rand(X, Y / 4, 31));
				break;
			case EPeregonTex::Asphalt:
				C = AsphaltPixel(X, Y, W, H);
				break;
			case EPeregonTex::RoadMarked:
			{
				// Поперёк (X): 64 px = 7 м полотна. Вдоль (Y): 128 px = 12 м (штрих 3 м, разрыв 9 м).
				C = AsphaltPixel(X, Y, W, H);
				// Накатанная колея — чуть темнее и глаже.
				for (int32 Track : {9, 23, 41, 55})
				{
					if (FMath::Abs(X - Track) <= 2)
					{
						C = Scale(C, 0.9f);
					}
				}
				const bool bCenter = (X == 31 || X == 32) && Y < 32;
				const bool bEdge = X == 2 || X == 3 || X == 60 || X == 61;
				if ((bCenter || bEdge) && R > 0.12f) // краска местами стёрлась
				{
					C = Gray(222.f + (R - 0.5f) * 16.f);
				}
				break;
			}
			case EPeregonTex::Gravel:
			{
				const int32 Cx = X / 3;
				const int32 Cy = Y / 3;
				const float Tone = 0.72f + 0.5f * Rand(Cx, Cy, 41);
				C = Rgb(118.f * Tone, 108.f * Tone, 94.f * Tone);
				if (Rand(X, Y, 42) < 0.14f)
				{
					C = Scale(C, 0.6f); // щели между камнями
				}
				break;
			}
			case EPeregonTex::Grass:
			{
				const float N = TileNoise(X, Y, 16, W, H, 51) - 0.5f;
				const float Blade = Rand(X, Y / 3, 52) - 0.5f;
				C = Rgb(70.f + N * 26.f + Blade * 14.f, 98.f + N * 30.f + Blade * 22.f, 42.f + N * 10.f);
				if (R > 0.965f)
				{
					C = Rgb(150.f, 140.f, 62.f); // сухие травинки
				}
				else if (R < 0.04f)
				{
					C = Scale(C, 0.65f);
				}
				break;
			}
			case EPeregonTex::Dirt:
			{
				const float N = TileNoise(X, Y, 16, W, H, 61) - 0.5f;
				C = Rgb(104.f + N * 30.f + (R - 0.5f) * 14.f, 80.f + N * 24.f + (R - 0.5f) * 10.f, 56.f + N * 16.f);
				if (R > 0.97f)
				{
					C = Rgb(150.f, 132.f, 110.f);
				}
				break;
			}
			case EPeregonTex::Mud:
			{
				const float Puddle = TileNoise(X, Y, 32, W, H, 71);
				C = Rgb(62.f + (R - 0.5f) * 10.f, 46.f + (R - 0.5f) * 8.f, 32.f);
				if (Puddle > 0.62f)
				{
					C = Rgb(42.f, 34.f, 28.f);
				}
				break;
			}
			case EPeregonTex::Snow:
			{
				const float N = TileNoise(X, Y, 16, W, H, 81) - 0.5f;
				C = Rgb(226.f + N * 12.f + (R - 0.5f) * 6.f, 231.f + N * 10.f + (R - 0.5f) * 6.f, 241.f + N * 6.f);
				break;
			}
			case EPeregonTex::Ice:
			{
				const float S = FMath::Sin(X * 0.35f + Y * 0.12f + TileNoise(X, Y, 16, W, H, 91) * 6.f);
				C = Rgb(168.f + S * 10.f, 197.f + S * 10.f, 218.f + S * 8.f);
				if (R > 0.985f)
				{
					C = Gray(240.f);
				}
				break;
			}
			case EPeregonTex::Concrete:
				C = (X % 32 == 0 || Y % 32 == 0) ? Gray(110.f)
					: Rgb(152.f + (TileNoise(X, Y, 8, W, H, 101) - 0.5f) * 18.f + (R - 0.5f) * 8.f, 150.f, 144.f);
				break;
			case EPeregonTex::WoodPlanks:
			{
				const int32 Plank = Y / 8;
				const float Tone = 0.82f + 0.25f * Rand(Plank, 0, 111);
				const float Grain = FMath::Sin((X + Rand(Plank, 1, 112) * 64.f) * 0.45f + TileNoise(X, Y, 8, W, H, 113) * 4.f) * 0.07f;
				C = Rgb(210.f * (Tone + Grain), 196.f * (Tone + Grain), 178.f * (Tone + Grain));
				if (Y % 8 == 0)
				{
					C = Scale(C, 0.45f);
				}
				break;
			}
			case EPeregonTex::Plaster:
				C = Rgb(215.f + (TileNoise(X, Y, 8, W, H, 121) - 0.5f) * 16.f + (R - 0.5f) * 8.f, 210.f, 198.f);
				break;
			case EPeregonTex::RoofMetal:
				C = Gray(180.f * (0.78f + 0.22f * FMath::Sin(X * 2.f * PI / 8.f)) + (R - 0.5f) * 6.f);
				if (R > 0.972f)
				{
					C = Rgb(150.f, 92.f, 60.f); // ржавчина
				}
				break;
			case EPeregonTex::BirchBark:
				C = Rgb(226.f + (R - 0.5f) * 10.f, 224.f, 216.f);
				if (Rand(0, Y / 2, 131) > 0.78f && Rand(X / 6, Y / 2, 132) > 0.45f)
				{
					C = Gray(42.f);
				}
				break;
			case EPeregonTex::PineBark:
			{
				const float Streak = Rand(X / 2, 0, 141) - 0.5f;
				C = Rgb(112.f + Streak * 40.f + (R - 0.5f) * 10.f, 70.f + Streak * 24.f, 45.f + Streak * 12.f);
				if (Rand(X / 3, Y / 5, 142) > 0.9f)
				{
					C = Scale(C, 0.55f);
				}
				break;
			}
			case EPeregonTex::Needles:
				C = Rgb(34.f + (R - 0.5f) * 18.f, 62.f + (R - 0.5f) * 26.f + (TileNoise(X, Y, 8, W, H, 151) - 0.5f) * 20.f, 36.f + (R - 0.5f) * 10.f);
				break;
			case EPeregonTex::Leaves:
			{
				const float B = Rand(X / 4, Y / 4, 161);
				C = Rgb(80.f + (B - 0.5f) * 40.f, 118.f + (B - 0.5f) * 46.f + (R - 0.5f) * 10.f, 46.f + (B - 0.5f) * 16.f);
				break;
			}
			case EPeregonTex::Glass:
				C = ((X + Y) % 32) < 4 ? Rgb(80.f, 96.f, 108.f) : Rgb(38.f, 52.f, 62.f);
				break;
			default:
				break;
			}
			Px[Y * W + X] = C;
		}
	}
}
} // namespace PeregonTexGen

UPeregonArtSubsystem* UPeregonArtSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UPeregonArtSubsystem>() : nullptr;
}

void UPeregonArtSubsystem::ApplyLook(const UObject* WorldContext, UMeshComponent* Mesh, const FPeregonLook& Look)
{
	UPeregonArtSubsystem* Art = Get(WorldContext);
	if (!Art || !Mesh)
	{
		return;
	}
	UMaterialInstanceDynamic* Mid = Art->MakeMaterial(Mesh, Look);
	if (!Mid)
	{
		return;
	}
	const int32 Slots = FMath::Max(1, Mesh->GetNumMaterials());
	for (int32 I = 0; I < Slots; ++I)
	{
		Mesh->SetMaterial(I, Mid);
	}
}

void UPeregonArtSubsystem::EnsureBaseMaterial()
{
	if (bTriedLoad)
	{
		return;
	}
	bTriedLoad = true;
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PSX2.M_PSX2"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	bPsx = BaseMaterial != nullptr;
	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Peregon: нет /Game/Peregon/Materials/M_PSX2 — его создаёт модуль PeregonEditor при запуске редактора. Пока — без текстур."));
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	GlassMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Peregon/Materials/M_PSX2_Glass.M_PSX2_Glass"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!GlassMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Peregon: нет M_PSX2_Glass — стёкла машины будут скрыты (материал создаёт PeregonEditor)."));
	}
}

UMaterialInstanceDynamic* UPeregonArtSubsystem::MakeGlassMaterial(UObject* Outer, const FLinearColor& Tint, float Opacity)
{
	EnsureBaseMaterial();
	if (!GlassMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(GlassMaterial, Outer ? Outer : this);
	if (Mid)
	{
		Mid->SetTextureParameterValue(TEXT("Tex"), GetTexture(EPeregonTex::White));
		Mid->SetVectorParameterValue(TEXT("Color"), Tint);
		Mid->SetScalarParameterValue(TEXT("Roughness"), 0.05f);
		Mid->SetScalarParameterValue(TEXT("Metallic"), 0.f);
		Mid->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Mid->SetScalarParameterValue(TEXT("WorldUV"), 0.f);
	}
	return Mid;
}

bool UPeregonArtSubsystem::HasPsxMaterial()
{
	EnsureBaseMaterial();
	return bPsx;
}

UTexture2D* UPeregonArtSubsystem::GetTexture(EPeregonTex Type)
{
	const int32 Index = static_cast<int32>(Type);
	if (Index < 0 || Index >= static_cast<int32>(EPeregonTex::Count))
	{
		return nullptr;
	}
	if (Textures.Num() <= Index)
	{
		Textures.SetNum(static_cast<int32>(EPeregonTex::Count));
		AverageColors.SetNum(static_cast<int32>(EPeregonTex::Count));
	}
	if (!Textures[Index])
	{
		Textures[Index] = Generate(Type);
	}
	return Textures[Index];
}

UTexture2D* UPeregonArtSubsystem::Generate(EPeregonTex Type)
{
	int32 W = 0;
	int32 H = 0;
	TArray<FColor> Pixels;
	PeregonTexGen::Draw(Type, W, H, Pixels);

	FLinearColor Sum(0.f, 0.f, 0.f, 0.f);
	for (const FColor& C : Pixels)
	{
		Sum += FLinearColor(C);
	}
	AverageColors[static_cast<int32>(Type)] = Pixels.Num() > 0 ? Sum / static_cast<float>(Pixels.Num()) : FLinearColor::White;

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->Filter = TF_Nearest;   // пиксели без размытия — PSX
	Tex->SRGB = true;
	Tex->LODGroup = TEXTUREGROUP_Pixels2D;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}

UMaterialInstanceDynamic* UPeregonArtSubsystem::MakeMaterial(UObject* Outer, const FPeregonLook& Look)
{
	EnsureBaseMaterial();
	if (!BaseMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMaterial, Outer ? Outer : this);
	if (!Mid)
	{
		return nullptr;
	}
	UTexture2D* Tex = GetTexture(Look.Texture);
	if (bPsx)
	{
		Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
		Mid->SetVectorParameterValue(TEXT("Color"), Look.Tint);
		Mid->SetScalarParameterValue(TEXT("Roughness"), Look.Roughness);
		Mid->SetScalarParameterValue(TEXT("Metallic"), Look.Metallic);
		Mid->SetScalarParameterValue(TEXT("Emissive"), Look.Emissive);
		Mid->SetScalarParameterValue(TEXT("WorldUV"), Look.WorldTileCm > 0.f ? 1.f : 0.f);
		Mid->SetScalarParameterValue(TEXT("TileSize"), Look.WorldTileCm > 0.f ? Look.WorldTileCm : 400.f);
	}
	else
	{
		// Запасной вариант: средний цвет текстуры × оттенок.
		const int32 Index = static_cast<int32>(Look.Texture);
		const FLinearColor Avg = AverageColors.IsValidIndex(Index) ? AverageColors[Index] : FLinearColor::White;
		Mid->SetVectorParameterValue(TEXT("Color"), Avg * Look.Tint);
	}
	return Mid;
}

// ---------------------------------------------------------------------------------------------
// Шрифты и надписи
// ---------------------------------------------------------------------------------------------
TWeakObjectPtr<UPeregonArtSubsystem> UPeregonArtSubsystem::Instance;

namespace PeregonFonts
{
FString Path(EPeregonFont Style)
{
	const TCHAR* File = TEXT("GolosText-Regular.ttf");
	if (Style == EPeregonFont::SemiBold)
	{
		File = TEXT("GolosText-SemiBold.ttf");
	}
	else if (Style == EPeregonFont::Display)
	{
		File = TEXT("Unbounded-Bold.ttf");
	}
	return FPaths::ProjectContentDir() / TEXT("Fonts") / File;
}
} // namespace PeregonFonts

void UPeregonArtSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Instance = this;
}

void UPeregonArtSubsystem::Deinitialize()
{
	if (Instance.Get() == this)
	{
		Instance.Reset();
	}
	Super::Deinitialize();
}

FSlateFontInfo UPeregonArtSubsystem::SlateFont(EPeregonFont Style, int32 Size)
{
	static TSharedPtr<const FCompositeFont> Fonts[3];
	static bool bTried[3] = {false, false, false};
	const int32 Index = FMath::Clamp(static_cast<int32>(Style), 0, 2);
	if (!bTried[Index])
	{
		bTried[Index] = true;
		const FString File = PeregonFonts::Path(Style);
		if (FPaths::FileExists(File))
		{
			Fonts[Index] = MakeShared<FStandaloneCompositeFont>(NAME_None, File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Peregon: не найден шрифт %s"), *File);
		}
	}
	if (Fonts[Index].IsValid())
	{
		return FSlateFontInfo(Fonts[Index], Size);
	}
	return FCoreStyle::GetDefaultFontStyle(Style == EPeregonFont::Regular ? "Regular" : "Bold", Size);
}

UFont* UPeregonArtSubsystem::CanvasFont(EPeregonFont Style, int32 Size)
{
	UPeregonArtSubsystem* Self = Instance.Get();
	if (!Self)
	{
		return nullptr;
	}
	const int32 Key = static_cast<int32>(Style) * 1000 + Size;
	if (TObjectPtr<UFont>* Found = Self->CanvasFonts.Find(Key))
	{
		return Found->Get();
	}
	UFont* Font = nullptr;
	const FString File = PeregonFonts::Path(Style);
	if (FPaths::FileExists(File))
	{
		Font = NewObject<UFont>(Self);
		Font->FontCacheType = EFontCacheType::Runtime;
		Font->LegacyFontSize = Size;
		FTypefaceEntry& Entry = Font->CompositeFont.DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = TEXT("Regular");
		Entry.Font = FFontData(File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	}
	Self->CanvasFonts.Add(Key, Font);
	return Font;
}

UMaterialInstanceDynamic* UPeregonArtSubsystem::MakeLabelMaterial(UObject* Outer, const FString& Text, const FLinearColor& Ink, const FLinearColor& Paper,
	int32 WidthPx, int32 HeightPx, int32 FontPx, EPeregonFont Style)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	const FString Key = FString::Printf(TEXT("%s|%s|%s|%d|%d|%d|%d"), *Text, *Ink.ToString(), *Paper.ToString(), WidthPx, HeightPx, FontPx, static_cast<int32>(Style));
	UTextureRenderTarget2D* Target = nullptr;
	if (TObjectPtr<UTextureRenderTarget2D>* Found = LabelCache.Find(Key))
	{
		Target = Found->Get();
	}
	if (!Target)
	{
		Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, WidthPx, HeightPx, RTF_RGBA8_SRGB, Paper, false);
		if (!Target)
		{
			return nullptr;
		}
		UKismetRenderingLibrary::ClearRenderTarget2D(World, Target, Paper);
		UCanvas* Canvas = nullptr;
		FVector2D Size(WidthPx, HeightPx);
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Target, Canvas, Size, Context);
		if (Canvas)
		{
			UFont* Font = CanvasFont(Style, FontPx);
			if (!Font && GEngine)
			{
				Font = GEngine->GetLargeFont();
			}
			float TextW = 0.f;
			float TextH = 0.f;
			Canvas->TextSize(Font, Text, TextW, TextH);
			Canvas->SetDrawColor(Ink.ToFColor(true));
			Canvas->DrawText(Font, Text, (Size.X - TextW) * 0.5f, (Size.Y - TextH) * 0.5f);
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
		LabelCache.Add(Key, Target);
	}
	UMaterialInstanceDynamic* Mid = MakeMaterial(Outer, FPeregonLook(EPeregonTex::White, FLinearColor::White, 0.7f));
	if (Mid && bPsx)
	{
		Mid->SetTextureParameterValue(TEXT("Tex"), Target);
	}
	return Mid;
}

UProceduralMeshComponent* UPeregonArtSubsystem::AddLabelQuad(AActor* Owner, USceneComponent* Parent, const FTransform& Relative, float WidthCm, float HeightCm,
	UMaterialInterface* Material)
{
	if (!Owner)
	{
		return nullptr;
	}
	UProceduralMeshComponent* Quad = NewObject<UProceduralMeshComponent>(Owner);
	Quad->SetMobility(EComponentMobility::Movable);
	Quad->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
	Quad->SetRelativeTransform(Relative);
	Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Quad->RegisterComponent();
	// Лицом к +X: для зрителя, смотрящего вдоль -X, +Y — слева.
	const float W = WidthCm * 0.5f;
	const float H = HeightCm * 0.5f;
	const TArray<FVector> Vertices = {FVector(0.0, W, H), FVector(0.0, -W, H), FVector(0.0, W, -H), FVector(0.0, -W, -H)};
	const TArray<int32> Triangles = {0, 2, 1, 1, 2, 3};
	const TArray<FVector> Normals = {FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector};
	const TArray<FVector2D> UVs = {FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(1.0, 1.0)};
	Quad->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	if (Material)
	{
		Quad->SetMaterial(0, Material);
	}
	return Quad;
}
