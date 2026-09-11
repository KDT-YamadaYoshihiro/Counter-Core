#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "MonsterAnimInstance.generated.h"

class UAnimSequence;

/**
 * ボス敵の見た目を「AnimBlueprint アセット無し」で成立させるためのネイティブ AnimInstance プロキシ。
 *
 * ノード構成（AnimBP の AnimGraph に相当するものを C++ で組む）:
 *
 *   [Idle SequencePlayer] --A--\
 *                                [TwoWayBlend] --> [Slot "DefaultSlot"] --> Output
 *   [Run  SequencePlayer] --B--/        ^                  ^
 *                                     Alpha            攻撃/やられ/スタンの
 *                                  (Speed から)         モンタージュがここに乗る
 *
 * AnimBP を作らなくても、移動ブレンドとモンタージュ再生の両方が動く。
 * 数値（ブレンド速度・走り閾値）は下の定数で調整。
 */
USTRUCT()
struct FMonsterAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FMonsterAnimInstanceProxy() = default;
	explicit FMonsterAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance) {}

	// FAnimInstanceProxy
	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	// AnimBlueprint を持たない UAnimInstance では、これが評価のルートノードになる。
	virtual FAnimNode_Base* GetCustomRootNode() override { return &SlotNode; }

private:
	FAnimNode_SequencePlayer IdlePlayer;
	FAnimNode_SequencePlayer RunPlayer;
	FAnimNode_TwoWayBlend    LocomotionBlend;
	FAnimNode_Slot           SlotNode;

	float BlendAlpha = 0.f; // 0=Idle, 1=Run（補間後）
};

template<> struct TStructOpsTypeTraits<FMonsterAnimInstanceProxy>
	: public TStructOpsTypeTraitsBase2<FMonsterAnimInstanceProxy>
{
	enum { WithCopy = false };
};

/**
 * BP_Enemy の Mesh の AnimClass に設定するネイティブ AnimInstance。
 * 見た目アセットは /Game/MonsterAnimation。
 */
UCLASS()
class COUNTERCORE_API UMonsterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UMonsterAnimInstance();

	/** Idle ループ（既定: MM_MonsterIdle）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Monster|Anim")
	TObjectPtr<UAnimSequence> IdleAnim;

	/** 走りループ（既定: MM_MonsterRun）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Monster|Anim")
	TObjectPtr<UAnimSequence> RunAnim;

	/** この速度(cm/s)で走りブレンド 100%。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Monster|Anim", meta = (ClampMin = "1"))
	float RunSpeedThreshold = 300.f;

	/** Idle↔Run ブレンドの追従速度（大きいほど機敏）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Monster|Anim", meta = (ClampMin = "0"))
	float BlendInterpSpeed = 8.f;

	/** 直近フレームの水平移動速度(cm/s)。プロキシがブレンド率に使う。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Monster|Anim")
	float GroundSpeed = 0.f;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	friend struct FMonsterAnimInstanceProxy;
};
