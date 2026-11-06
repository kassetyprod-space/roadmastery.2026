// Первичное текстурирование. Все текстуры рисуются кодом (детерминированный шум, 32–128 px,
// без фильтрации — PSX-вид), поэтому в репозитории нет бинарных картинок.
// Материал один на всё — M_PSX (создаётся скриптом Content/Python/init_unreal.py при запуске редактора).
// Если M_PSX нет, используется стандартный материал движка, и детали просто красятся цветом.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PeregonArtSubsystem.generated.h"

class UMaterialInstanceDynamic;
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

	TArray<FLinearColor> AverageColors;
	bool bPsx = false;
	bool bTriedLoad = false;
};
