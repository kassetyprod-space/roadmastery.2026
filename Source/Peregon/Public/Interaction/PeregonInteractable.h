// Всё, с чем можно взаимодействовать взглядом + клавишей: двери, капот, ключ зажигания, рычаги…
// Интерактивные детали — это компоненты с коллизией только по каналу Interact (ECC_GameTraceChannel1,
// см. Config/DefaultEngine.ini) и тегом, по которому владелец понимает, что это за деталь.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Interface.h"
#include "PeregonInteractable.generated.h"

class APawn;
class UPrimitiveComponent;

// Канал трассировки взаимодействия.
constexpr ECollisionChannel PeregonInteractChannel = ECC_GameTraceChannel1;

UINTERFACE(MinimalAPI)
class UPeregonInteractable : public UInterface
{
	GENERATED_BODY()
};

class PEREGON_API IPeregonInteractable
{
	GENERATED_BODY()

public:
	// Подсказка под прицелом («Открыть капот»). Пустая строка — взаимодействовать нельзя.
	virtual FString GetInteractPrompt(const UPrimitiveComponent* /*Part*/, const APawn* /*User*/) const { return FString(); }
	// Нажали клавишу взаимодействия.
	virtual void InteractStart(UPrimitiveComponent* /*Part*/, APawn* /*User*/) {}
	// Отпустили (нужно для «держать», например стартер).
	virtual void InteractStop(UPrimitiveComponent* /*Part*/, APawn* /*User*/) {}
};

// Результат поиска того, на что смотрит игрок.
struct FPeregonFocus
{
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UPrimitiveComponent> Part;
	FString Prompt;

	bool IsValid() const { return Actor.IsValid() && Part.IsValid() && !Prompt.IsEmpty(); }
	IPeregonInteractable* Target() const { return Cast<IPeregonInteractable>(Actor.Get()); }
};

namespace PeregonInteraction
{
// Луч из камеры по каналу Interact. Возвращает деталь и подсказку.
PEREGON_API FPeregonFocus Trace(const UWorld* World, const FVector& Start, const FVector& Direction, float LengthCm, const APawn* User);

// Настроить компонент как интерактивный: ловит только луч взаимодействия, физике не мешает.
PEREGON_API void MakeInteractable(UPrimitiveComponent* Component, FName Tag);
} // namespace PeregonInteraction
