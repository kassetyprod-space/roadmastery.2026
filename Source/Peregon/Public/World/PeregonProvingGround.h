// Испытательный полигон: прямая 1.3 км, площадка для управляемости, полосы гравия/снега/льда/грязи,
// холм с уклоном 15%, «стиральная доска». Собирается из примитивов в коде — карта (.umap) не нужна.
// Если на уровне нет солнца, полигон сам ставит небо, облака и объёмный туман.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Chaos/ChaosEngineInterface.h"
#include "PeregonProvingGround.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class PEREGON_API APeregonProvingGround : public AActor
{
	GENERATED_BODY()

public:
	APeregonProvingGround();

	virtual void PostInitializeComponents() override;

	FTransform GetSpawnTransform() const;

	UPROPERTY(EditAnywhere, Category = "Peregon")
	bool bSpawnEnvironment = true;

private:
	void Build();
	void SpawnEnvironmentIfMissing();
	UStaticMeshComponent* AddBox(const FName& Name, const FVector& Center, const FVector& SizeCm, const FRotator& Rotation,
		EPhysicalSurface Surface, const FLinearColor& Color, bool bCollision = true);
	void AddHill(const FName& Name, const FVector& Start, float RampLengthCm, float Grade, float PlateauCm, float WidthCm);

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	bool bBuilt = false;
};
