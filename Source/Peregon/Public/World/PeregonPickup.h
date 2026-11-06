// Вещь, лежащая в мире: деталь на помойке, инструмент на полке гаража, банка краски у стены.
// Подойти, навести прицел и нажать E — вещь уходит в карманы (с её состоянием). Положить обратно — G (то, что в руках).
// Цена > 0 — вещь продаётся (разборка): деньги списываются при взятии.
#pragma once

#include "CoreMinimal.h"
#include "Game/PeregonInventory.h"
#include "GameFramework/Actor.h"
#include "Interaction/PeregonInteractable.h"
#include "PeregonPickup.generated.h"

class UStaticMeshComponent;

UCLASS()
class PEREGON_API APeregonPickup : public AActor, public IPeregonInteractable
{
	GENERATED_BODY()

public:
	APeregonPickup();

	// Положить вещь: Transform — точка на земле (низ предмета), поворот — по yaw.
	static APeregonPickup* Place(UWorld* World, const FPeregonItemStack& InStack, const FTransform& Transform, float InPrice = 0.f);

	virtual FString GetInteractPrompt(const UPrimitiveComponent* Part, const APawn* User) const override;
	virtual void InteractStart(UPrimitiveComponent* Part, APawn* User) override;

	const FPeregonItemStack& GetStack() const { return Stack; }

	UPROPERTY(EditAnywhere, Category = "Peregon") FPeregonItemStack Stack;
	UPROPERTY(EditAnywhere, Category = "Peregon") float Price = 0.f;

private:
	void BuildLook();

	UPROPERTY(VisibleAnywhere, Category = "Peregon") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Peregon") TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere, Category = "Peregon") TObjectPtr<UStaticMeshComponent> Detail;
};
