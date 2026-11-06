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
	bool HasPsxMaterial();

private:
	void EnsureBaseMaterial();
	UTexture2D* Generate(EPeregonTex Type);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Textures;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UFont>> CanvasFonts;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTextureRenderTarget2D>> LabelCache;

	static TWeakObjectPtr<UPeregonArtSubsystem> Instance;

	TArray<FLinearColor> AverageColors;
	bool bPsx = false;
	bool bTriedLoad = false;
};
