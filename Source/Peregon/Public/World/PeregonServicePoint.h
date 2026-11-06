// Придорожный сервис: АЗС (заправка за деньги) или кафе (отдых и сохранение).
// Локальные оси: +X смотрит на дорогу.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/PeregonInteractable.h"
#include "Art/PeregonArtSubsystem.h"
#include "PeregonServicePoint.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EPeregonService : uint8
{
	Fuel,
	Cafe
};

UCLASS()
class PEREGON_API APeregonServicePoint : public AActor, public IPeregonInteractable
{
	GENERATED_BODY()

public:
	APeregonServicePoint();

	virtual void BeginPlay() override;

	virtual FString GetInteractPrompt(const UPrimitiveComponent* Part, const APawn* User) const override;
	virtual void InteractStart(UPrimitiveComponent* Part, APawn* User) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon")
	EPeregonService Kind = EPeregonService::Fuel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon")
	FString DisplayName;

	// ₽ за литр.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon")
	float FuelPrice = 52.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon")
	float NightStayPrice = 400.f;

private:
	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& SizeM, const FPeregonLook& Look, bool bCollision, FName InteractTag = NAME_None);
	// Надпись-табличка (лицом к дороге). SizeCm — высота букв.
	void AddLabel(const FVector& Location, const FString& Text, float SizeCm, const FColor& Ink, const FLinearColor& Paper);
	void BuildFuel();
	void BuildCafe();

	// Сколько литров нужно машине игрока и стоит ли она у колонки.
	bool GetCarNeed(float& OutLiters) const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;
};
