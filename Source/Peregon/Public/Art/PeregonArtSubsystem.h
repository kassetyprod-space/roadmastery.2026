// Первичное текстурирование. Все текстуры рисуются кодом (детерминированный шум, 32–128 px,
// без фильтрации — PSX-вид), поэтому в репозитории нет бинарных картинок.
// Материал один на всё — M_PSX2 (его создаёт модуль PeregonEditor при запуске редактора).
// Если M_PSX2 нет, используется стандартный материал движка, и детали просто красятся цветом.
// Здесь же шрифты с кириллицей (Content/Fonts) и надписи для табличек.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PeregonArtSubsystem.generated.h"

class AActor;
class UFont;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USceneComponent;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class EPeregonTex : uint8
{
	White,
	Paint,
	Plastic,
	Fabric,
	Rubber,
	Chrome,
	Asphalt,
	RoadMarked,
	Gravel,
	Grass,
	Dirt,
	Mud,
	Snow,
	Ice,
	Concrete,
	WoodPlanks,
	Plaster,
	RoofMetal,
	BirchBark,
	PineBark,
	Needles,
	Leaves,
	Glass,
	GrassTuft,    // пучок травы с прозрачностью (альфа)
	Bush,         // куст с просветами
	Leaf,         // один лист (листопад)
	Raindrops,    // капли на стекле
	PanelFacade,  // панельный дом: шов, окно, балкон — один пролёт 3 × 2.8 м
	BrickFacade,  // кирпичный дом
	Clouds,       // бесшовные облака (альфа — плотность), для PSX-неба
	GlassDirt,    // стекло с пылью и разводами (альфа — плотность); видно и изнутри
	Count UMETA(Hidden)
};

// Шрифты из Content/Fonts (OFL, с кириллицей).
UENUM(BlueprintType)
enum class EPeregonFont : uint8
{
	Regular,   // Golos Text
	SemiBold,  // Golos Text SemiBold
	Display    // Unbounded Bold — заголовки и крупные цифры
};

// Внешний вид поверхности.
USTRUCT(BlueprintType)
struct FPeregonLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") EPeregonTex Texture = EPeregonTex::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") FLinearColor Tint = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") float Roughness = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") float Metallic = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") float Emissive = 0.f;
	// > 0 — мировые UV с этим размером плитки (см): для растянутых боксов.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") float WorldTileCm = 0.f;
	// Погода (материал M_PSX3). −1 — по типу текстуры: дорога мокнет, трава и крыши белеют от снега, салон — нет.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") float WetMask = -1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") float SnowMask = -1.f;
	// Гибкость на ветру (множитель смещения; вес по вершинам — альфа цвета вершин).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") float Sway = 0.f;
	// Свечение в темноте (окна домов, фонари): добавляется к Emissive ночью.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look") float NightGlow = 0.f;

	FPeregonLook() = default;
	FPeregonLook(EPeregonTex InTexture, const FLinearColor& InTint, float InRoughness = 0.8f, float InMetallic = 0.f)
		: Texture(InTexture), Tint(InTint), Roughness(InRoughness), Metallic(InMetallic) {}
};

UCLASS()
class PEREGON_API UPeregonArtSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UPeregonArtSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Шрифт для Slate (меню). Без файла — шрифт движка.
	static FSlateFontInfo SlateFont(EPeregonFont Style, int32 Size);
	// Шрифт для Canvas (HUD, таблички). Может вернуть null — тогда используйте шрифт движка.
	static UFont* CanvasFont(EPeregonFont Style, int32 Size);
	// Размер надписи в пикселях (Slate-измеритель). Без Slate — грубая оценка.
	static FVector2D MeasureText(const FString& Text, EPeregonFont Style, int32 Size);

	// Материал-надпись: текст рисуется кодом в текстуру (кириллица, любой шрифт).
	UMaterialInstanceDynamic* MakeLabelMaterial(UObject* Outer, const FString& Text, const FLinearColor& Ink, const FLinearColor& Paper,
		int32 WidthPx, int32 HeightPx, int32 FontPx, EPeregonFont Style = EPeregonFont::SemiBold);
	// Плоская табличка (лицом к +X локальной системы), без коллизии.
	static UProceduralMeshComponent* AddLabelQuad(AActor* Owner, USceneComponent* Parent, const FTransform& Relative, float WidthCm, float HeightCm,
		UMaterialInterface* Material);

	// Покрасить все слоты меша. Безопасно вызывать без подсистемы (ничего не делает).
	static void ApplyLook(const UObject* WorldContext, UMeshComponent* Mesh, const FPeregonLook& Look);

	UTexture2D* GetTexture(EPeregonTex Type);
	UMaterialInstanceDynamic* MakeMaterial(UObject* Outer, const FPeregonLook& Look);
	// Полупрозрачное стекло (M_PSX3_Glass или M_PSX2_Glass). Null, если материала нет — тогда стёкла лучше скрыть.
	UMaterialInstanceDynamic* MakeGlassMaterial(UObject* Outer, const FLinearColor& Tint, float Opacity);
	// Полупрозрачное с текстурой (альфа — прозрачность) и исчезанием ближе NearFadeCm к камере. Только M_PSX3_Glass.
	UMaterialInstanceDynamic* MakeTranslucent(UObject* Outer, EPeregonTex Texture, const FLinearColor& Tint, float Opacity, float Emissive,
		float NearFadeCm = -100000.f);
	bool HasPsxMaterial();
	// Есть материалы второго поколения (погода, ветер, прозрачность по альфе).
	bool HasWeatherMaterials();

	// Общая погода для всех материалов мира: мокрость, снег, ветер (см смещения), ночь (0..1, для NightGlow).
	void SetWorldParams(float Wetness, float Snow, const FVector& WindCm, float Night);
	float GetWetness() const { return CurWetness; }
	float GetSnow() const { return CurSnow; }
	float GetNight() const { return CurNight; }

private:
	void EnsureBaseMaterial();
	UTexture2D* Generate(EPeregonTex Type);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Textures;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlassMaterial;

	// Надписи — на непрозрачном M_PSX2 (альфа текстуры-холста не всегда 1, маска M_PSX3 могла бы её вырезать).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LabelMaterial;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UFont>> CanvasFonts;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTextureRenderTarget2D>> LabelCache;

	static TWeakObjectPtr<UPeregonArtSubsystem> Instance;

	TArray<FLinearColor> AverageColors;

	struct FGlow
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Mid;
		float Base = 0.f;
		float Glow = 0.f;
	};
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> WeatherMids;
	TArray<FGlow> GlowMids;
	float CurWetness = 0.f;
	float CurSnow = 0.f;
	FVector CurWind = FVector::ZeroVector;
	float CurNight = 0.f;
	void ApplyWeather(UMaterialInstanceDynamic* Mid) const;

	bool bPsx3 = false;
	bool bGlass3 = false;
	bool bPsx = false;
	bool bTriedLoad = false;
};
