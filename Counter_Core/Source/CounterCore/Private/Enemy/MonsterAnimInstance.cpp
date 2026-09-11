#include "Enemy/MonsterAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

// ---------------------------------------------------------------------------
// Proxy
// ---------------------------------------------------------------------------

void FMonsterAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	if (const UMonsterAnimInstance* Owner = Cast<UMonsterAnimInstance>(InAnimInstance))
	{
		IdlePlayer.SetSequence(Owner->IdleAnim);
		IdlePlayer.SetLoopAnimation(true);
		RunPlayer.SetSequence(Owner->RunAnim);
		RunPlayer.SetLoopAnimation(true);
	}

	// AnimGraph の配線を C++ で再現する（コンパイラが張るはずのポーズリンクを手張り）。
	// Super::Initialize より前に済ませておく（RootNode 確定・スロット登録の前に構成を固める）。
	LocomotionBlend.A.SetLinkNode(&IdlePlayer);
	LocomotionBlend.B.SetLinkNode(&RunPlayer);
	LocomotionBlend.Alpha = 0.f;

	SlotNode.SlotName = FName(TEXT("DefaultSlot"));
	SlotNode.Source.SetLinkNode(&LocomotionBlend);

	// ここで RootNode = GetCustomRootNode()(= &SlotNode) が確定。
	// この後 UAnimInstance::InitializeAnimation が InitializeRootNode() を呼び、
	// SlotNode から上のリンクを辿って各ノードの Initialize_AnyThread が走る
	// （FAnimNode_Slot::Initialize_AnyThread がスロットを AnimInstance へ登録する）。
	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FMonsterAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const UMonsterAnimInstance* Owner = Cast<UMonsterAnimInstance>(InAnimInstance);
	if (!Owner)
	{
		return;
	}

	const float Threshold = FMath::Max(1.f, Owner->RunSpeedThreshold);
	const float TargetAlpha = FMath::Clamp(Owner->GroundSpeed / Threshold, 0.f, 1.f);
	BlendAlpha = (DeltaSeconds > 0.f && Owner->BlendInterpSpeed > 0.f)
		? FMath::FInterpTo(BlendAlpha, TargetAlpha, DeltaSeconds, Owner->BlendInterpSpeed)
		: TargetAlpha;

	LocomotionBlend.Alpha = BlendAlpha;
}

// ---------------------------------------------------------------------------
// UMonsterAnimInstance
// ---------------------------------------------------------------------------

UMonsterAnimInstance::UMonsterAnimInstance()
{
	static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> IdleSeq(
		TEXT("/Game/MonsterAnimation/Animation/MM_MonsterIdle.MM_MonsterIdle"));
	static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> RunSeq(
		TEXT("/Game/MonsterAnimation/Animation/MM_MonsterRun.MM_MonsterRun"));
	IdleAnim = IdleSeq.Get();
	RunAnim  = RunSeq.Get();
}

void UMonsterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (const APawn* Pawn = TryGetPawnOwner())
	{
		GroundSpeed = Pawn->GetVelocity().Size2D();
	}
}

FAnimInstanceProxy* UMonsterAnimInstance::CreateAnimInstanceProxy()
{
	return new FMonsterAnimInstanceProxy(this);
}

void UMonsterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}
