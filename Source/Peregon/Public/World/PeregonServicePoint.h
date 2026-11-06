// Придорожный сервис: АЗС (заправка, магазин), кафе (отдых и сохранение), магазин «Продукты» (карты, мелочи).
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
class UMaterialInterface;

UENUM(BlueprintType)
enum class EPeregonService : uint8
{
	Fuel,
	Cafe,
	Shop,
	Parts // фирменный магазин автозапчастей: узлы, инструмент, краска, масло
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

	// Область маршрута (какую карту продают).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peregon")
	int32 Region = 0;

	// Что продают за прилавком.
	TArray<FName> GetShopCatalog() const;

private:
	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& SizeM, const FPeregonLook& Look, bool bCollision, FName InteractTag = NAME_None);
	// Надпись-табличка (лицом к дороге). SizeCm — высота букв.
	void AddLabel(const FVector& Location, const FString& Text, float SizeCm, const FColor& Ink, const FLinearColor& Paper);
	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, const FVector& Center, const FVector& SizeM, UMaterialInterface* Material, bool bCollision);
	void AddLight(const FVector& Location, float Candela, const FLinearColor& Color);
	// Помещение, в которое можно войти: дверной проём DoorY и окна (Y от–до) в передней стене (+X).
	void AddRoom(const FVector& FloorCenter, const FVector& SizeM, float DoorY, const TArray<FVector2D>& WindowsY, const FPeregonLook& Walls,
		const FPeregonLook& Floor, const FPeregonLook& Inner);
	void AddPerson(const FVector& Feet, const FLinearColor& Clothes);
	void AddShelf(const FVector& Base, double LengthY, int32 Levels, const TArray<FName>& Items, int32& ItemCursor);
	void AddItemProp(FName Id, const FVector& OnSurface);
	float ItemPrice(FName Id) const;
	void BuildFuel();
	void BuildCafe();
	void BuildShop();

	// Сколько литров нужно машине игрока и стоит ли она у колонки.
	bool GetCarNeed(float& OutLiters) const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;
};
