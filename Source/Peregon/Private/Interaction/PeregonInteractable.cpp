#include "Interaction/PeregonInteractable.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace PeregonInteraction
{
FPeregonFocus Trace(const UWorld* World, const FVector& Start, const FVector& Direction, float LengthCm, const APawn* User)
{
	FPeregonFocus Focus;
	if (!World)
	{
		return Focus;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PeregonInteract), false);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, Start + Direction.GetSafeNormal() * LengthCm, PeregonInteractChannel, Params))
	{
		return Focus;
	}
	AActor* Actor = Hit.GetActor();
	UPrimitiveComponent* Part = Hit.GetComponent();
	const IPeregonInteractable* Target = Cast<IPeregonInteractable>(Actor);
	if (!Target || !Part)
	{
		return Focus;
	}
	Focus.Actor = Actor;
	Focus.Part = Part;
	Focus.Prompt = Target->GetInteractPrompt(Part, User);
	return Focus;
}

void MakeInteractable(UPrimitiveComponent* Component, FName Tag)
{
	if (!Component)
	{
		return;
	}
	Component->ComponentTags.AddUnique(Tag);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Component->SetCollisionResponseToAllChannels(ECR_Ignore);
	Component->SetCollisionResponseToChannel(PeregonInteractChannel, ECR_Block);
	// Не приваривать к физическому телу машины: деталь двигается (двери) и не должна влиять на массу.
	Component->BodyInstance.bAutoWeld = false;
	Component->SetGenerateOverlapEvents(false);
}
} // namespace PeregonInteraction
