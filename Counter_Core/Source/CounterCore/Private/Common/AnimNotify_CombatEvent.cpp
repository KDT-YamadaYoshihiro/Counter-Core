#include "Common/AnimNotify_CombatEvent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UAnimNotify_CombatEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!Owner || EventName.IsNone())
	{
		return;
	}
	if (Owner->GetClass()->ImplementsInterface(UCombatEventReceiver::StaticClass()))
	{
		ICombatEventReceiver::Execute_ReceiveCombatEvent(Owner, EventName);
	}
	for (UActorComponent* Comp : Owner->GetComponents())
	{
		if (Comp && Comp->GetClass()->ImplementsInterface(UCombatEventReceiver::StaticClass()))
		{
			ICombatEventReceiver::Execute_ReceiveCombatEvent(Comp, EventName);
		}
	}
}

FString UAnimNotify_CombatEvent::GetNotifyName_Implementation() const
{
	return EventName.IsNone() ? Super::GetNotifyName_Implementation() : EventName.ToString();
}
