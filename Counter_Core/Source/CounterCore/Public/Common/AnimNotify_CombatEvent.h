#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "UObject/Interface.h"
#include "AnimNotify_CombatEvent.generated.h"

UINTERFACE(BlueprintType)
class COUNTERCORE_API UCombatEventReceiver : public UInterface
{
	GENERATED_BODY()
};

/** UAnimNotify_CombatEvent を受け取る側（アクター本体 or コンポーネント）が実装する。 */
class COUNTERCORE_API ICombatEventReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Combat|Event")
	void ReceiveCombatEvent(FName EventName);
};

/**
 * 汎用の戦闘イベント Notify（T0-3）。Montage に置き、EventName に
 * HitStart / HitEnd / AttackEnd / DeathEnd / WindupEnd などを入れる。
 * メッシュの持ち主アクターと、その ICombatEventReceiver 実装コンポーネントすべてへ通知する。
 */
UCLASS(meta = (DisplayName = "Combat Event"))
class COUNTERCORE_API UAnimNotify_CombatEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	FName EventName = FName(TEXT("HitStart"));

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
