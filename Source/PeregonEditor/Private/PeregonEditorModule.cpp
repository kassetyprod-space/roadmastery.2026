// Создание мастер-материала M_PSX2 (текстура × цвет, шероховатость, металл, свечение,
// мировые или мешевые UV). Раньше это делал Python-скрипт, но он мог молча не сработать;
// C++ надёжнее: ошибки видны при сборке, а результат — в Output Log с префиксом [Peregon].
#include "AssetToolsModule.h"
#include "Containers/Ticker.h"
#include "EditorAssetLibrary.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPeregonEditor, Log, All);

namespace PeregonEditorContent
{
const TCHAR* MaterialDir = TEXT("/Game/Peregon/Materials");
const TCHAR* MaterialName = TEXT("M_PSX2");

template <typename T>
T* AddNode(UMaterial* Material, int32 X, int32 Y)
{
	return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), X, Y));
}

UMaterialExpressionScalarParameter* AddScalar(UMaterial* Material, const TCHAR* Name, float Default, int32 X, int32 Y)
{
	UMaterialExpressionScalarParameter* Node = AddNode<UMaterialExpressionScalarParameter>(Material, X, Y);
	if (Node)
	{
		Node->ParameterName = FName(Name);
		Node->DefaultValue = Default;
	}
	return Node;
}

void CreatePsxMaterial()
{
	const FString PackagePath = FString(MaterialDir) / MaterialName;
	if (FPackageName::DoesPackageExist(PackagePath))
	{
		UE_LOG(LogPeregonEditor, Log, TEXT("[Peregon] %s уже есть"), *PackagePath);
		return;
	}
	UE_LOG(LogPeregonEditor, Display, TEXT("[Peregon] Создаю %s"), *PackagePath);

	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Material = Cast<UMaterial>(Tools.CreateAsset(MaterialName, MaterialDir, UMaterial::StaticClass(), Factory));
	if (!Material)
	{
		UE_LOG(LogPeregonEditor, Error, TEXT("[Peregon] Не удалось создать материал"));
		return;
	}
	Material->TwoSided = true;
	Material->bUsedWithInstancedStaticMeshes = true;

	// UV: из меша или мировые (WorldUV = 1, плитка TileSize см).
	UMaterialExpressionTextureCoordinate* TexCoord = AddNode<UMaterialExpressionTextureCoordinate>(Material, -1400, -100);
	UMaterialExpressionWorldPosition* WorldPos = AddNode<UMaterialExpressionWorldPosition>(Material, -1650, 100);
	UMaterialExpressionComponentMask* Mask = AddNode<UMaterialExpressionComponentMask>(Material, -1400, 100);
	Mask->R = 1;
	Mask->G = 1;
	Mask->B = 0;
	Mask->A = 0;
	UMaterialEditingLibrary::ConnectMaterialExpressions(WorldPos, TEXT(""), Mask, TEXT(""));
	UMaterialExpressionScalarParameter* Tile = AddScalar(Material, TEXT("TileSize"), 400.f, -1400, 260);
	UMaterialExpressionDivide* Divide = AddNode<UMaterialExpressionDivide>(Material, -1200, 150);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Mask, TEXT(""), Divide, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Tile, TEXT(""), Divide, TEXT("B"));
	UMaterialExpressionScalarParameter* WorldUV = AddScalar(Material, TEXT("WorldUV"), 0.f, -1200, 320);
	UMaterialExpressionLinearInterpolate* UvLerp = AddNode<UMaterialExpressionLinearInterpolate>(Material, -1000, 0);
	UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), UvLerp, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Divide, TEXT(""), UvLerp, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(WorldUV, TEXT(""), UvLerp, TEXT("Alpha"));

	UMaterialExpressionTextureSampleParameter2D* Tex = AddNode<UMaterialExpressionTextureSampleParameter2D>(Material, -800, 0);
	Tex->ParameterName = TEXT("Tex");
	Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	Tex->SamplerType = SAMPLERTYPE_Color;
	UMaterialEditingLibrary::ConnectMaterialExpressions(UvLerp, TEXT(""), Tex, TEXT("UVs"));

	UMaterialExpressionVectorParameter* Color = AddNode<UMaterialExpressionVectorParameter>(Material, -800, 300);
	Color->ParameterName = TEXT("Color");
	Color->DefaultValue = FLinearColor::White;

	UMaterialExpressionMultiply* BaseColor = AddNode<UMaterialExpressionMultiply>(Material, -450, 100);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Tex, TEXT("RGB"), BaseColor, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Color, TEXT(""), BaseColor, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialProperty(BaseColor, TEXT(""), MP_BaseColor);

	UMaterialEditingLibrary::ConnectMaterialProperty(AddScalar(Material, TEXT("Roughness"), 0.8f, -450, 350), TEXT(""), MP_Roughness);
	UMaterialEditingLibrary::ConnectMaterialProperty(AddScalar(Material, TEXT("Metallic"), 0.f, -450, 480), TEXT(""), MP_Metallic);

	UMaterialExpressionScalarParameter* Emissive = AddScalar(Material, TEXT("Emissive"), 0.f, -450, 620);
	UMaterialExpressionMultiply* EmissiveColor = AddNode<UMaterialExpressionMultiply>(Material, -250, 600);
	UMaterialEditingLibrary::ConnectMaterialExpressions(BaseColor, TEXT(""), EmissiveColor, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Emissive, TEXT(""), EmissiveColor, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialProperty(EmissiveColor, TEXT(""), MP_EmissiveColor);

	UMaterialEditingLibrary::RecompileMaterial(Material);
	const bool bSaved = UEditorAssetLibrary::SaveAsset(Material->GetPathName(), false);
	UE_LOG(LogPeregonEditor, Display, TEXT("[Peregon] Материал %s %s. Закоммитьте Content/Peregon/Materials/M_PSX2.uasset."),
		*PackagePath, bSaved ? TEXT("создан и сохранён") : TEXT("создан, но НЕ сохранён"));
}
} // namespace PeregonEditorContent

class FPeregonEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Через секунду после старта: реестр ассетов и редактор уже готовы.
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) {
			PeregonEditorContent::CreatePsxMaterial();
			return false;
		}), 1.f);
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}

private:
	FTSTicker::FDelegateHandle TickerHandle;
};

IMPLEMENT_MODULE(FPeregonEditorModule, PeregonEditor);
