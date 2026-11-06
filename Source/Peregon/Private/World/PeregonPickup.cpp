#include "World/PeregonPickup.h"

#include "Art/PeregonArtSubsystem.h"
#include "Audio/PeregonAudio.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/PeregonGameInstance.h"
#include "Game/PeregonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "ProceduralMeshComponent.h"
#include "Vehicle/PeregonPartMeshes.h"
#include "World/PeregonKit.h"

APeregonPickup::APeregonPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Detail = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Detail"));
	Detail->SetupAttachment(Root);
	Detail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

APeregonPickup* APeregonPickup::Place(UWorld* World, const FPeregonItemStack& InStack, const FTransform& Transform, float InPrice)
{
	if (!World || InStack.Id.IsNone() || InStack.Count <= 0)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APeregonPickup* Item = World->SpawnActorDeferred<APeregonPickup>(APeregonPickup::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Item)
	{
		return nullptr;
	}
	Item->Stack = InStack;
	Item->Price = InPrice;
	Item->FinishSpawning(Transform);
	Item->BuildLook();
	return Item;
}

void APeregonPickup::BuildLook()
{
	const FPeregonItemDef* Def = PeregonItems::Find(Stack.Id);
	const FVector Size = Def ? Def->SizeCm : FVector(20.f, 15.f, 10.f);
	UPeregonArtSubsystem* Art = UPeregonArtSubsystem::Get(this);
	if (PeregonPartMeshes::Has(Stack.Id) && Art && Art->HasWeatherMaterials())
	{
		// Подробная модель вместо коробки. Мишень для взгляда — невидимая коробка по габариту.
		FPeregonMeshBuffers Buf;
		PeregonPartMeshes::Build(Stack.Id, Buf, FTransform::Identity, Stack.Condition);
		Model = NewObject<UProceduralMeshComponent>(this, TEXT("Model"));
		Model->SetupAttachment(Root);
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Model->RegisterComponent();
		Model->CreateMeshSection(0, Buf.Vertices, Buf.Triangles, Buf.Normals, Buf.UVs, Buf.Colors, TArray<FProcMeshTangent>(), false);
		Model->SetMaterial(0, Art->MakeMaterial(this, FPeregonLook(EPeregonTex::Paint, FLinearColor::White, 0.55f, 0.25f)));
		Mesh->SetStaticMesh(FPeregonKit::Cube());
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, Size.Z * 0.5f));
		Mesh->SetRelativeScale3D((Size + FVector(4.f)) / 100.f);
		Mesh->SetVisibility(false);
		PeregonInteraction::MakeInteractable(Mesh, FName(TEXT("Pickup")));
		Detail->SetVisibility(false);
		return;
	}
	FLinearColor Color = Def ? Def->Color : FLinearColor::Gray;
	if (Def && Def->Kind == EPeregonItemKind::Part)
	{
		const float Worn = 1.f - FMath::Clamp(Stack.Condition, 0.f, 1.f);
		Color = FMath::Lerp(Color, FLinearColor(0.3f, 0.17f, 0.08f), Worn * 0.6f) * (1.f - 0.35f * Worn);
	}
	// Баллоны и банки — цилиндры, остальное — коробки.
	const bool bRound = Def && Def->Kind == EPeregonItemKind::Consumable && Size.X < 15.f;
	Mesh->SetStaticMesh(bRound ? FPeregonKit::Cylinder() : FPeregonKit::Cube());
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, Size.Z * 0.5f));
	Mesh->SetRelativeScale3D(Size / 100.f);
	if (Art)
	{
		Mesh->SetMaterial(0, Art->MakeMaterial(this, FPeregonLook(EPeregonTex::Paint, Color, 0.5f, Def && Def->Kind == EPeregonItemKind::Tool ? 0.4f : 0.1f)));
	}
	PeregonInteraction::MakeInteractable(Mesh, FName(TEXT("Pickup")));
	// Крышка у банок и баллонов, ручка у канистр.
	if (Def && (Def->Kind == EPeregonItemKind::Consumable || Def->Kind == EPeregonItemKind::Fluid))
	{
		Detail->SetStaticMesh(FPeregonKit::Cube());
		Detail->SetRelativeLocation(FVector(0.f, 0.f, Size.Z + 1.5f));
		Detail->SetRelativeScale3D(FVector(Size.X * 0.5f, Size.Y * 0.5f, 3.f) / 100.f);
		if (Art)
		{
			Detail->SetMaterial(0, Art->MakeMaterial(this, FPeregonLook(EPeregonTex::Plastic, FLinearColor(0.08f, 0.08f, 0.08f), 0.6f)));
		}
	}
	else
	{
		Detail->SetVisibility(false);
	}
}

FString APeregonPickup::GetInteractPrompt(const UPrimitiveComponent* /*Part*/, const APawn* User) const
{
	if (!User || !User->IsPlayerControlled())
	{
		return FString();
	}
	const FString Label = PeregonItems::StackLabel(Stack);
	return Price > 0.f ? FString::Printf(TEXT("Купить: %s — %.0f ₽"), *Label, Price) : FString::Printf(TEXT("Взять: %s"), *Label);
}

void APeregonPickup::InteractStart(UPrimitiveComponent* /*Part*/, APawn* User)
{
	APeregonPlayerController* PC = User ? Cast<APeregonPlayerController>(User->GetController()) : nullptr;
	UPeregonGameInstance* GI = GetWorld()->GetGameInstance<UPeregonGameInstance>();
	FPeregonContainer* Pockets = PC ? PC->GetContainer(EPeregonStorage::None) : nullptr;
	if (!PC || !GI || !Pockets)
	{
		return;
	}
	if (!Pockets->CanAddStack(Stack))
	{
		PC->Toast(TEXT("Карманы полны — отнесите что-нибудь в багажник (Tab у открытого багажника)"));
		return;
	}
	if (Price > 0.f && !GI->TrySpend(Price))
	{
		PC->Toast(TEXT("Не хватает денег"));
		return;
	}
	Pockets->AddStack(Stack);
	PC->Toast((Price > 0.f ? FString(TEXT("Куплено: ")) : FString(TEXT("Взято: "))) + PeregonItems::StackLabel(Stack));
	PeregonAudio::PlayEvent(this, TEXT("item.pickup"), GetActorLocation(), 0.5f);
	Destroy();
}
