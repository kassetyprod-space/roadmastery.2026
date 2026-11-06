// Создание мастер-материалов M_PSX2 (текстура × цвет, шероховатость, металл, свечение,
// мировые или мешевые UV) и M_PSX2_Glass (то же, но полупрозрачный — стёкла машины). Раньше это делал Python-скрипт, но он мог молча не сработать;
// C++ надёжнее: ошибки видны при сборке, а результат — в Output Log с префиксом [Peregon].
#include "AssetToolsModule.h"
#include "Containers/Ticker.h"
#include "EditorAssetLibrary.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
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

void CreatePsxMaterial(const TCHAR* MaterialName, bool bGlass)
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
	if (bGlass)
	{
		Material->BlendMode = BLEND_Translucent;
		Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
		UMaterialEditingLibrary::ConnectMaterialProperty(AddScalar(Material, TEXT("Opacity"), 0.3f, -450, 760), TEXT(""), MP_Opacity);
	}

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
	UE_LOG(LogPeregonEditor, Display, TEXT("[Peregon] Материал %s %s. Закоммитьте Content/Peregon/Materials/%s.uasset."),
		*PackagePath, bSaved ? TEXT("создан и сохранён") : TEXT("создан, но НЕ сохранён"), MaterialName);
}

// Второе поколение (M_PSX3, M_PSX3_Glass): всё как у M_PSX2 плюс
//  • цвет вершин (RGB — оттенок, A — гибкость для ветра);
//  • погода: Wetness (мокрое темнее и глянцевее), Snow × SnowMask (снег на обращённых вверх гранях);
//  • ветер: WorldPositionOffset = Wind.rgb × Sway × A × (0.55 + 0.45·sin(время + фаза по месту));
//  • маска прозрачности из альфы текстуры (трава, кусты, листья);
//  • у стекла: прозрачность × альфа текстуры и плавное исчезание ближе NearFade (дождь не идёт в салоне).
// Старые M_PSX2 не трогаем — игра берёт M_PSX3, если он есть.
void CreatePsx3Material(const TCHAR* MaterialName, bool bGlass)
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
		UE_LOG(LogPeregonEditor, Error, TEXT("[Peregon] Не удалось создать материал %s"), MaterialName);
		return;
	}
	Material->TwoSided = true;
	Material->bUsedWithInstancedStaticMeshes = true;

	// --- UV (как у M_PSX2)
	UMaterialExpressionTextureCoordinate* TexCoord = AddNode<UMaterialExpressionTextureCoordinate>(Material, -1800, -100);
	UMaterialExpressionWorldPosition* WorldPos = AddNode<UMaterialExpressionWorldPosition>(Material, -2050, 100);
	UMaterialExpressionComponentMask* Mask = AddNode<UMaterialExpressionComponentMask>(Material, -1800, 100);
	Mask->R = 1;
	Mask->G = 1;
	Mask->B = 0;
	Mask->A = 0;
	UMaterialEditingLibrary::ConnectMaterialExpressions(WorldPos, TEXT(""), Mask, TEXT(""));
	UMaterialExpressionScalarParameter* Tile = AddScalar(Material, TEXT("TileSize"), 400.f, -1800, 260);
	UMaterialExpressionDivide* Divide = AddNode<UMaterialExpressionDivide>(Material, -1600, 150);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Mask, TEXT(""), Divide, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Tile, TEXT(""), Divide, TEXT("B"));
	UMaterialExpressionScalarParameter* WorldUV = AddScalar(Material, TEXT("WorldUV"), 0.f, -1600, 320);
	UMaterialExpressionLinearInterpolate* UvLerp = AddNode<UMaterialExpressionLinearInterpolate>(Material, -1400, 0);
	UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), UvLerp, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Divide, TEXT(""), UvLerp, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(WorldUV, TEXT(""), UvLerp, TEXT("Alpha"));

	UMaterialExpressionTextureSampleParameter2D* Tex = AddNode<UMaterialExpressionTextureSampleParameter2D>(Material, -1200, 0);
	Tex->ParameterName = TEXT("Tex");
	Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	Tex->SamplerType = SAMPLERTYPE_Color;
	UMaterialEditingLibrary::ConnectMaterialExpressions(UvLerp, TEXT(""), Tex, TEXT("UVs"));

	// --- Цвет: текстура × Color × цвет вершин
	UMaterialExpressionVectorParameter* Color = AddNode<UMaterialExpressionVectorParameter>(Material, -1200, 300);
	Color->ParameterName = TEXT("Color");
	Color->DefaultValue = FLinearColor::White;
	UMaterialExpressionVertexColor* VertexColor = AddNode<UMaterialExpressionVertexColor>(Material, -1200, 500);
	UMaterialExpressionMultiply* Tinted = AddNode<UMaterialExpressionMultiply>(Material, -950, 100);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Tex, TEXT("RGB"), Tinted, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Color, TEXT(""), Tinted, TEXT("B"));
	UMaterialExpressionMultiply* Base = AddNode<UMaterialExpressionMultiply>(Material, -800, 150);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Tinted, TEXT(""), Base, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(VertexColor, TEXT(""), Base, TEXT("B"));

	UMaterialExpressionScalarParameter* Emissive = AddScalar(Material, TEXT("Emissive"), 0.f, -600, 700);
	UMaterialExpressionMultiply* EmissiveColor = AddNode<UMaterialExpressionMultiply>(Material, -300, 700);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Base, TEXT(""), EmissiveColor, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Emissive, TEXT(""), EmissiveColor, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialProperty(EmissiveColor, TEXT(""), MP_EmissiveColor);

	if (bGlass)
	{
		Material->BlendMode = BLEND_Translucent;
		Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
		UMaterialEditingLibrary::ConnectMaterialProperty(Base, TEXT(""), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(AddScalar(Material, TEXT("Roughness"), 0.05f, -450, 350), TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(AddScalar(Material, TEXT("Metallic"), 0.f, -450, 480), TEXT(""), MP_Metallic);
		// Прозрачность = Opacity × альфа × saturate((глубина − NearFade) / 50).
		UMaterialExpressionScalarParameter* Opacity = AddScalar(Material, TEXT("Opacity"), 0.3f, -900, 900);
		UMaterialExpressionMultiply* OpacityTex = AddNode<UMaterialExpressionMultiply>(Material, -700, 900);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Opacity, TEXT(""), OpacityTex, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Tex, TEXT("A"), OpacityTex, TEXT("B"));
		UMaterialExpressionPixelDepth* Depth = AddNode<UMaterialExpressionPixelDepth>(Material, -1100, 1050);
		UMaterialExpressionScalarParameter* NearFade = AddScalar(Material, TEXT("NearFade"), -100000.f, -1100, 1150);
		UMaterialExpressionSubtract* Sub = AddNode<UMaterialExpressionSubtract>(Material, -900, 1080);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Depth, TEXT(""), Sub, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(NearFade, TEXT(""), Sub, TEXT("B"));
		UMaterialExpressionMultiply* Scale = AddNode<UMaterialExpressionMultiply>(Material, -750, 1080);
		Scale->ConstB = 0.02f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Sub, TEXT(""), Scale, TEXT("A"));
		UMaterialExpressionSaturate* Fade = AddNode<UMaterialExpressionSaturate>(Material, -600, 1080);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Scale, TEXT(""), Fade, TEXT(""));
		UMaterialExpressionMultiply* FinalOpacity = AddNode<UMaterialExpressionMultiply>(Material, -450, 950);
		UMaterialEditingLibrary::ConnectMaterialExpressions(OpacityTex, TEXT(""), FinalOpacity, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Fade, TEXT(""), FinalOpacity, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(FinalOpacity, TEXT(""), MP_Opacity);
	}
	else
	{
		Material->BlendMode = BLEND_Masked;
		Material->OpacityMaskClipValue = 0.5f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Tex, TEXT("A"), MP_OpacityMask);

		// Мокрое: темнее вдвое и глянцевее.
		UMaterialExpressionScalarParameter* Wetness = AddScalar(Material, TEXT("Wetness"), 0.f, -900, -400);
		UMaterialExpressionScalarParameter* WetMask = AddScalar(Material, TEXT("WetMask"), 1.f, -900, -300);
		UMaterialExpressionMultiply* WetAmount = AddNode<UMaterialExpressionMultiply>(Material, -700, -350);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Wetness, TEXT(""), WetAmount, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(WetMask, TEXT(""), WetAmount, TEXT("B"));
		UMaterialExpressionMultiply* Darker = AddNode<UMaterialExpressionMultiply>(Material, -600, 150);
		Darker->ConstB = 0.5f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Base, TEXT(""), Darker, TEXT("A"));
		UMaterialExpressionLinearInterpolate* WetBase = AddNode<UMaterialExpressionLinearInterpolate>(Material, -450, 100);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Base, TEXT(""), WetBase, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Darker, TEXT(""), WetBase, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(WetAmount, TEXT(""), WetBase, TEXT("Alpha"));

		// Снег: на гранях, смотрящих вверх; Snow — общая погода, SnowMask — свойство поверхности.
		UMaterialExpressionVertexNormalWS* Normal = AddNode<UMaterialExpressionVertexNormalWS>(Material, -1200, -700);
		UMaterialExpressionComponentMask* NormalZ = AddNode<UMaterialExpressionComponentMask>(Material, -1050, -700);
		NormalZ->R = 0;
		NormalZ->G = 0;
		NormalZ->B = 1;
		NormalZ->A = 0;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Normal, TEXT(""), NormalZ, TEXT(""));
		UMaterialExpressionAdd* UpShift = AddNode<UMaterialExpressionAdd>(Material, -900, -700);
		UpShift->ConstB = -0.35f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(NormalZ, TEXT(""), UpShift, TEXT("A"));
		UMaterialExpressionMultiply* UpScale = AddNode<UMaterialExpressionMultiply>(Material, -780, -700);
		UpScale->ConstB = 3.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(UpShift, TEXT(""), UpScale, TEXT("A"));
		UMaterialExpressionScalarParameter* Snow = AddScalar(Material, TEXT("Snow"), 0.f, -900, -600);
		UMaterialExpressionScalarParameter* SnowMask = AddScalar(Material, TEXT("SnowMask"), 0.f, -900, -500);
		UMaterialExpressionMultiply* SnowAmountRaw = AddNode<UMaterialExpressionMultiply>(Material, -700, -560);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Snow, TEXT(""), SnowAmountRaw, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SnowMask, TEXT(""), SnowAmountRaw, TEXT("B"));
		UMaterialExpressionMultiply* SnowUp = AddNode<UMaterialExpressionMultiply>(Material, -600, -640);
		UMaterialEditingLibrary::ConnectMaterialExpressions(UpScale, TEXT(""), SnowUp, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SnowAmountRaw, TEXT(""), SnowUp, TEXT("B"));
		UMaterialExpressionSaturate* SnowAmount = AddNode<UMaterialExpressionSaturate>(Material, -480, -640);
		UMaterialEditingLibrary::ConnectMaterialExpressions(SnowUp, TEXT(""), SnowAmount, TEXT(""));
		UMaterialExpressionConstant3Vector* SnowColor = AddNode<UMaterialExpressionConstant3Vector>(Material, -450, -500);
		SnowColor->Constant = FLinearColor(0.86f, 0.89f, 0.93f);
		UMaterialExpressionLinearInterpolate* FinalBase = AddNode<UMaterialExpressionLinearInterpolate>(Material, -250, 50);
		UMaterialEditingLibrary::ConnectMaterialExpressions(WetBase, TEXT(""), FinalBase, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SnowColor, TEXT(""), FinalBase, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SnowAmount, TEXT(""), FinalBase, TEXT("Alpha"));
		UMaterialEditingLibrary::ConnectMaterialProperty(FinalBase, TEXT(""), MP_BaseColor);

		// Шероховатость: мокрое — 0.12, снег — 0.8.
		UMaterialExpressionScalarParameter* Roughness = AddScalar(Material, TEXT("Roughness"), 0.8f, -700, 350);
		UMaterialExpressionLinearInterpolate* WetRough = AddNode<UMaterialExpressionLinearInterpolate>(Material, -450, 350);
		WetRough->ConstB = 0.12f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Roughness, TEXT(""), WetRough, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(WetAmount, TEXT(""), WetRough, TEXT("Alpha"));
		UMaterialExpressionLinearInterpolate* SnowRough = AddNode<UMaterialExpressionLinearInterpolate>(Material, -250, 350);
		SnowRough->ConstB = 0.8f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(WetRough, TEXT(""), SnowRough, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SnowAmount, TEXT(""), SnowRough, TEXT("Alpha"));
		UMaterialEditingLibrary::ConnectMaterialProperty(SnowRough, TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(AddScalar(Material, TEXT("Metallic"), 0.f, -450, 480), TEXT(""), MP_Metallic);

		// Ветер.
		UMaterialExpressionTime* Time = AddNode<UMaterialExpressionTime>(Material, -1500, 1000);
		UMaterialExpressionMultiply* TimeScale = AddNode<UMaterialExpressionMultiply>(Material, -1350, 1000);
		TimeScale->ConstB = 2.1f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Time, TEXT(""), TimeScale, TEXT("A"));
		UMaterialExpressionWorldPosition* SwayPos = AddNode<UMaterialExpressionWorldPosition>(Material, -1650, 1150);
		SwayPos->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
		UMaterialExpressionComponentMask* PosX = AddNode<UMaterialExpressionComponentMask>(Material, -1450, 1120);
		PosX->R = 1;
		PosX->G = 0;
		PosX->B = 0;
		PosX->A = 0;
		UMaterialExpressionComponentMask* PosY = AddNode<UMaterialExpressionComponentMask>(Material, -1450, 1220);
		PosY->R = 0;
		PosY->G = 1;
		PosY->B = 0;
		PosY->A = 0;
		UMaterialEditingLibrary::ConnectMaterialExpressions(SwayPos, TEXT(""), PosX, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SwayPos, TEXT(""), PosY, TEXT(""));
		UMaterialExpressionAdd* PosSum = AddNode<UMaterialExpressionAdd>(Material, -1300, 1170);
		UMaterialEditingLibrary::ConnectMaterialExpressions(PosX, TEXT(""), PosSum, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(PosY, TEXT(""), PosSum, TEXT("B"));
		UMaterialExpressionMultiply* Phase = AddNode<UMaterialExpressionMultiply>(Material, -1150, 1170);
		Phase->ConstB = 0.003f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(PosSum, TEXT(""), Phase, TEXT("A"));
		UMaterialExpressionAdd* Arg = AddNode<UMaterialExpressionAdd>(Material, -1000, 1080);
		UMaterialEditingLibrary::ConnectMaterialExpressions(TimeScale, TEXT(""), Arg, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Phase, TEXT(""), Arg, TEXT("B"));
		UMaterialExpressionSine* Sine = AddNode<UMaterialExpressionSine>(Material, -850, 1080);
		Sine->Period = 6.2831853f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Arg, TEXT(""), Sine, TEXT(""));
		UMaterialExpressionMultiply* Swing = AddNode<UMaterialExpressionMultiply>(Material, -720, 1080);
		Swing->ConstB = 0.45f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Sine, TEXT(""), Swing, TEXT("A"));
		UMaterialExpressionAdd* Lean = AddNode<UMaterialExpressionAdd>(Material, -600, 1080);
		Lean->ConstB = 0.55f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Swing, TEXT(""), Lean, TEXT("A"));
		UMaterialExpressionScalarParameter* Sway = AddScalar(Material, TEXT("Sway"), 0.f, -750, 1250);
		UMaterialExpressionMultiply* SwayWeight = AddNode<UMaterialExpressionMultiply>(Material, -600, 1250);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Sway, TEXT(""), SwayWeight, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(VertexColor, TEXT("A"), SwayWeight, TEXT("B"));
		UMaterialExpressionMultiply* SwayAmount = AddNode<UMaterialExpressionMultiply>(Material, -450, 1150);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Lean, TEXT(""), SwayAmount, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SwayWeight, TEXT(""), SwayAmount, TEXT("B"));
		UMaterialExpressionVectorParameter* Wind = AddNode<UMaterialExpressionVectorParameter>(Material, -450, 1300);
		Wind->ParameterName = TEXT("Wind");
		Wind->DefaultValue = FLinearColor(0.f, 0.f, 0.f, 0.f);
		UMaterialExpressionMultiply* Offset = AddNode<UMaterialExpressionMultiply>(Material, -250, 1200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Wind, TEXT(""), Offset, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SwayAmount, TEXT(""), Offset, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Offset, TEXT(""), MP_WorldPositionOffset);
	}

	UMaterialEditingLibrary::RecompileMaterial(Material);
	const bool bSaved = UEditorAssetLibrary::SaveAsset(Material->GetPathName(), false);
	UE_LOG(LogPeregonEditor, Display, TEXT("[Peregon] Материал %s %s. Закоммитьте Content/Peregon/Materials/%s.uasset."),
		*PackagePath, bSaved ? TEXT("создан и сохранён") : TEXT("создан, но НЕ сохранён"), MaterialName);
}
} // namespace PeregonEditorContent

class FPeregonEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Через секунду после старта: реестр ассетов и редактор уже готовы.
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) {
			PeregonEditorContent::CreatePsxMaterial(TEXT("M_PSX2"), false);
			PeregonEditorContent::CreatePsxMaterial(TEXT("M_PSX2_Glass"), true);
			PeregonEditorContent::CreatePsx3Material(TEXT("M_PSX3"), false);
			PeregonEditorContent::CreatePsx3Material(TEXT("M_PSX3_Glass"), true);
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
