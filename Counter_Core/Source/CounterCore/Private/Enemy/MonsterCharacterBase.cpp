#include "Enemy/MonsterCharacterBase.h"
#include "Enemy/MonsterCombatComponent.h"
#include "Enemy/MonsterAttackComponent.h"
#include "Player/PlayerCombatComponent.h"
#include "Player/PlayerGuardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CounterCoreDebug.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Enemy/MonsterAnimInstance.h"
#include "Common/CombatFeedbackLibrary.h"
#include "AIController.h"
#include "BrainComponent.h"

AMonsterCharacterBase::AMonsterCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	Combat = CreateDefaultSubobject<UMonsterCombatComponent>(TEXT("Combat"));
	Attack = CreateDefaultSubobject<UMonsterAttackComponent>(TEXT("Attack"));

	Hitbox = CreateDefaultSubobject<UBoxComponent>(TEXT("Hitbox"));
	Hitbox->SetupAttachment(GetMesh());
	Hitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hitbox->SetCollisionObjectType(ECC_WorldDynamic);
	Hitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	Hitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Hitbox->SetGenerateOverlapEvents(true);

	WeaponActor = CreateDefaultSubobject<UChildActorComponent>(TEXT("WeaponActor"));
	WeaponActor->SetupAttachment(GetMesh(), FName("hand_r"));

	// 見た目アセット（メッシュ・Montage・VFX）は BP_Enemy / DT_MonsterAttacks で設定する。
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// Character 既定のメッシュ配置（足を接地・前方 +X 向き）。SM_Monster の原点・スケールで要微調整。
		MeshComp->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -89.f), FRotator(0.f, -90.f, 0.f));
		// AnimBlueprint アセット無しで移動ブレンド + モンタージュを成立させるネイティブ AnimInstance。
		MeshComp->SetAnimInstanceClass(UMonsterAnimInstance::StaticClass());
	}

}

void AMonsterCharacterBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// エディタ上でも武器を表示するため、生成クラスとアタッチ先をここで確定させる。
	if (WeaponActor)
	{
		if (WeaponActor->GetChildActorClass() != WeaponClass)
		{
			WeaponActor->SetChildActorClass(WeaponClass);
		}
		if (GetMesh())
		{
			// 付け替え先（親/ソケット）が変わっていなければ再アタッチしない。
			// 毎回 SnapToTarget で貼り直すと、Details パネルで調整した WeaponActor の
			// 相対 Transform（握り位置・向き）が OnConstruction のたびに 0 へ戻ってしまう。
			const bool bNeedsReattach = WeaponActor->GetAttachParent() != GetMesh()
				|| WeaponActor->GetAttachSocketName() != WeaponSocket;
			WeaponActor->AttachToComponent(GetMesh(),
				bNeedsReattach ? FAttachmentTransformRules::SnapToTargetNotIncludingScale
				               : FAttachmentTransformRules::KeepRelativeTransform,
				WeaponSocket);
		}
	}
}

UPrimitiveComponent* AMonsterCharacterBase::ResolveAttackHitbox() const
{
	if (WeaponActor && WeaponActor->GetChildActor())
	{
		if (UShapeComponent* Shape = WeaponActor->GetChildActor()->FindComponentByClass<UShapeComponent>())
		{
			return Shape;
		}
	}
	return Hitbox;
}

void AMonsterCharacterBase::UpdateHitboxDebugVisual(bool bActive)
{
	if (!ActiveHitbox)
	{
		return;
	}
	const bool bShow = CounterCoreDebug::IsOnScreenDebugEnabled() && (bAlwaysShowHitbox || bActive);
	ActiveHitbox->SetHiddenInGame(!bShow);
	if (UShapeComponent* Shape = Cast<UShapeComponent>(ActiveHitbox))
	{
		Shape->ShapeColor = bActive ? HitboxActiveColor : HitboxInactiveColor;
		Shape->MarkRenderStateDirty();
	}
}

int32 AMonsterCharacterBase::CurrentAttackPower() const
{
	// ComboIndex は「次に撃つ攻撃」を指すので、進行中の一発は ComboIndex-1。
	const int32 Idx = ComboIndex - 1;
	if (Attack && CurrentComboAttacks.IsValidIndex(Idx))
	{
		bool bFound = false;
		return Attack->GetAttackData(CurrentComboAttacks[Idx], bFound).Damage;
	}
	return 0;
}

void AMonsterCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	if (UMonsterAnimInstance* MonsterAnim = GetMesh() ? Cast<UMonsterAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		if (LocomotionIdleAnim) { MonsterAnim->IdleAnim = LocomotionIdleAnim; }
		if (LocomotionRunAnim)  { MonsterAnim->RunAnim  = LocomotionRunAnim; }
	}

	if (Combat)
	{
		Combat->OnStateChangeRequested.AddDynamic(this, &AMonsterCharacterBase::HandleCombatStateRequest);
		Combat->OnDamaged.AddDynamic(this, &AMonsterCharacterBase::HandleCombatDamaged);
	}
	if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		Anim->OnMontageEnded.AddDynamic(this, &AMonsterCharacterBase::HandleMontageEnded);
	}

	// 頭上の HP バー（BP の WidgetComponent「HpGauge」/ UW_HpGaugeOnHead）は不要なので消す。
	// 敵 HP は HUD の画面上部ボスバーに一本化。
	if (bHideHeadHealthWidget)
	{
		TArray<UWidgetComponent*> Widgets;
		GetComponents<UWidgetComponent>(Widgets);
		for (UWidgetComponent* W : Widgets)
		{
			if (W)
			{
				W->SetHiddenInGame(true);
				W->SetVisibility(false);
				W->SetActive(false);
				W->SetComponentTickEnabled(false);
			}
		}
	}
	if (Attack)
	{
		Attack->OnAttackFinished.AddDynamic(this, &AMonsterCharacterBase::HandleAttackFinished);
		Attack->OnToggleHitbox.AddDynamic(this, &AMonsterCharacterBase::HandleToggleHitbox);
		Attack->OnPlayAttackAnim.AddDynamic(this, &AMonsterCharacterBase::HandlePlayAttackAnim);
		Attack->OnAttackHitActive.AddDynamic(this, &AMonsterCharacterBase::HandleAttackHitActive);
	}

	// 武器を生成して手にアタッチ。
	if (WeaponActor)
	{
		if (WeaponClass && WeaponActor->GetChildActorClass() != WeaponClass)
		{
			WeaponActor->SetChildActorClass(WeaponClass);
		}
		if (GetMesh())
		{
			// OnConstruction 同様、既に正しいソケットへアタッチ済みなら相対 Transform を維持する。
			const bool bNeedsReattach = WeaponActor->GetAttachParent() != GetMesh()
				|| WeaponActor->GetAttachSocketName() != WeaponSocket;
			WeaponActor->AttachToComponent(GetMesh(),
				bNeedsReattach ? FAttachmentTransformRules::SnapToTargetNotIncludingScale
				               : FAttachmentTransformRules::KeepRelativeTransform,
				WeaponSocket);
		}
	}

	// 内蔵フォールバック判定のセットアップ。
	if (Hitbox)
	{
		Hitbox->SetBoxExtent(HitboxExtent);
		if (HitboxSocket != NAME_None && GetMesh() && GetMesh()->DoesSocketExist(HitboxSocket))
		{
			Hitbox->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, HitboxSocket);
		}
	}

	// 攻撃判定の実体を解決（武器内シェイプ優先）。HitActive 中だけ Overlap を有効にする。
	ActiveHitbox = ResolveAttackHitbox();
	if (ActiveHitbox)
	{
		ActiveHitbox->OnComponentBeginOverlap.AddDynamic(this, &AMonsterCharacterBase::OnHitboxOverlap);
		ActiveHitbox->SetGenerateOverlapEvents(true);
		ActiveHitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		ActiveHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UpdateHitboxDebugVisual(false);
	}

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = ChaseSpeed;
	}

	// 近接時にプレイヤーのスプリングアームが敵に寄って画角が壊れるのを防ぐため、
	// 敵のコリジョンをカメラ判定（ECC_Camera）から除外する。
	if (bIgnoreCameraCollision)
	{
		if (UPrimitiveComponent* Cap = GetCapsuleComponent())
		{
			Cap->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
		if (USkeletalMeshComponent* M = GetMesh())
		{
			M->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
		if (WeaponActor && WeaponActor->GetChildActor())
		{
			TArray<UPrimitiveComponent*> Prims;
			WeaponActor->GetChildActor()->GetComponents<UPrimitiveComponent>(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				Prim->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			}
		}
	}

	// ターゲット未設定ならプレイヤー0を拾う（1v1 前提）。
	if (!TargetActor)
	{
		SetTarget(UGameplayStatics::GetPlayerPawn(this, 0));
	}

	EnterState(EMonsterState::Idle);
}

void AMonsterCharacterBase::SetTarget(AActor* InTarget)
{
	TargetActor = InTarget;
	if (Attack)
	{
		Attack->SetTarget(InTarget);
	}
}

float AMonsterCharacterBase::GetDistanceToTargetCm() const
{
	return TargetActor ? FVector::Dist(GetActorLocation(), TargetActor->GetActorLocation()) : TNumericLimits<float>::Max();
}

float AMonsterCharacterBase::GetSignedAngleToTargetDeg() const
{
	if (!TargetActor)
	{
		return 0.f;
	}
	const FVector ToTarget = (TargetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const float Dot = FVector::DotProduct(Forward, ToTarget);
	const float Cross = FVector::CrossProduct(Forward, ToTarget).Z;
	return FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
}

int32 AMonsterCharacterBase::StatePriority(EMonsterState S)
{
	switch (S)
	{
	case EMonsterState::Dead:    return 100;
	case EMonsterState::Stun:    return 80;
	case EMonsterState::GetUp:   return 70; // 立ち上がり完了まで AI / やられを受け付けない（Dead / Stun は割り込む）
	case EMonsterState::Hitstun: return 60;
	case EMonsterState::Attack:  return 40;
	case EMonsterState::Run:     return 20;
	case EMonsterState::Idle:    return 10;
	default:                     return 0;
	}
}

void AMonsterCharacterBase::RequestState(EMonsterState NewState)
{
	if (State == EMonsterState::Dead)
	{
		return;
	}
	// やられ判定無効区間中は Hitstun 要求を弾く（攻撃5の1段目など）。
	if (NewState == EMonsterState::Hitstun && Attack && !Attack->IsHitstunAllowed())
	{
		return;
	}
	if (StatePriority(NewState) >= StatePriority(State) || NewState == EMonsterState::Idle)
	{
		EnterState(NewState);
	}
}

void AMonsterCharacterBase::ForceState(EMonsterState NewState)
{
	EnterState(NewState);
}

void AMonsterCharacterBase::EnterState(EMonsterState NewState)
{
	const EMonsterState Old = State;
	if (Old == EMonsterState::Dead)
	{
		return; // PG-11/12: Dead は終端（GetUp よりも優先）
	}
	// PG-12: やられ / スタン終了で待機へ戻るときは立ち上がりを挟む。
	if (NewState == EMonsterState::Idle && Old != EMonsterState::GetUp && GetUpSourceStates.Contains(Old))
	{
		NewState = EMonsterState::GetUp;
	}
	if (Old == NewState && Old != EMonsterState::Idle)
	{
		return;
	}

	// 攻撃を抜けるときはタイムライン中断（＝攻撃を中止）。
	if (NewState != EMonsterState::Attack && Attack && Attack->IsAttacking())
	{
		Attack->CancelAttack();
	}
	if (NewState != EMonsterState::Attack)
	{
		EndWindupHold();
		StopTrail();
	}
	if (Old == EMonsterState::GetUp && Combat)
	{
		Combat->bInvulnerable = false;
	}
	// 判定コリジョンは攻撃以外の状態では必ず OFF。
	if (NewState != EMonsterState::Attack && ActiveHitbox)
	{
		ActiveHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UpdateHitboxDebugVisual(false);
	}

	State = NewState;

	// 移動ロック: スタン / 死亡 中は動かない。復帰時は歩行に戻す。
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		if (NewState == EMonsterState::Stun || NewState == EMonsterState::Dead || NewState == EMonsterState::GetUp)
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
		else if (Move->MovementMode == MOVE_None)
		{
			Move->SetMovementMode(MOVE_Walking);
		}
	}

	// スタン解除で待機へ戻るときは「起き上がり」時間を挟んでから AI 再開。
	if (NewState == EMonsterState::Idle && Old == EMonsterState::Stun)
	{
		LoopRestTimer = FMath::Max(LoopRestTimer, Combat ? Combat->GetUpTime : 1.0f);
		PrintAI(TEXT("スタン解除 → 起き上がり"), FColor::Green);
	}

	// 仕様書 Battle「ラッシュ」: 敵スタン中はプレイヤーをラッシュ状態にする（ゲージ MAX + 与ダメ 1.2倍）。
	if (NewState == EMonsterState::Stun || Old == EMonsterState::Stun)
	{
		if (TargetActor)
		{
			if (UPlayerCombatComponent* PlayerCombat = TargetActor->FindComponentByClass<UPlayerCombatComponent>())
			{
				PlayerCombat->SetRushActive(NewState == EMonsterState::Stun);
			}
		}
	}

	switch (NewState)
	{
	case EMonsterState::Hitstun:
	{
		// 仕様書 Monster「やられ」: 攻撃中止 → やられアニメ → [0.1]後方0.2Mノックバック+スタン+10 → [0.4]硬直終了。
		HitstunTimer = Combat ? Combat->HitstunDuration : 0.4f;
		if (bPendingGuardedReaction && bGuardedReactionUsesMontageLength)
		{
			UAnimMontage* M = GuardedReactionMontage;
			if (!M)
			{
				if (TObjectPtr<UAnimMontage>* Found = ReactionMontages.Find(EMonsterState::Hitstun)) { M = *Found; }
			}
			if (M)
			{
				HitstunTimer = M->GetPlayLength();
			}
		}
		bMovingToEngageCombo = false;
		// 中断された攻撃が「やられ連鎖しない」（攻撃5）かどうかを覚えておく。
		bInterruptedAttackNoChain = false;
		if (Attack && CurrentComboAttacks.IsValidIndex(ComboIndex - 1))
		{
			bool bFound = false;
			const FMonsterAttackFrameData D = Attack->GetAttackData(CurrentComboAttacks[ComboIndex - 1], bFound);
			bInterruptedAttackNoChain = bFound && D.bNoHitstunChain;
		}
		// スタン値 +10 は UMonsterCombatComponent::HandleIncomingHit（ガード分岐）が既に加算済み。
		if (TargetActor)
		{
			HitstunKnockbackDir = (GetActorLocation() - TargetActor->GetActorLocation()).GetSafeNormal2D();
		}
		ApplyHitStop();                          // 仕様書 Battle: ガード成功時のヒットストップ
		PlayCameraShake(DamagedCameraShake);     // 仕様書 Battle: カメラシェイク
		PrintAI(TEXT("やられ（ガード成功）"), FColor::Orange);
		break;
	}
	case EMonsterState::Stun:
		// 仕様書 Monster「スタン」: スタン値上限到達 → 15秒行動不能 / Battle: ラッシュ（被ダメ 1.2倍）。
		bMovingToEngageCombo = false;
		if (Combat)
		{
			Combat->BeginStun();
		}
		PrintAI(FString::Printf(TEXT("スタン（%.0f秒）"), Combat ? Combat->StunDuration : 15.f), FColor::Purple);
		break;
	case EMonsterState::Dead:
	{
		// PG-11: HP0 → AI / 移動 / 攻撃予約 / 判定を停止し、死亡 Montage を 1 回。
		bMovingToEngageCombo = false;
		CurrentComboAttacks.Reset();
		ComboIndex = 0;
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->StopMovement();
			if (UBrainComponent* Brain = AIC->GetBrainComponent())
			{
				Brain->StopLogic(TEXT("Dead"));
			}
		}
		if (UCapsuleComponent* Cap = GetCapsuleComponent())
		{
			if (!DeathCapsuleCollisionProfile.IsNone())
			{
				Cap->SetCollisionProfileName(DeathCapsuleCollisionProfile);
			}
			else
			{
				Cap->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			}
		}
		if (Attack)
		{
			Attack->SetComponentTickEnabled(false);
		}
		if (Combat)
		{
			Combat->bInvulnerable = true;
		}
		// 死亡 Montage が無ければ即ラグドール。あれば Montage 終了 / DeathEnd Notify でラグドール。
		{
			const TObjectPtr<UAnimMontage>* DeathM = ReactionMontages.Find(EMonsterState::Dead);
			if (!DeathM || !*DeathM)
			{
				StartDeathRagdoll();
			}
			else if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			{
				Anim->StopAllMontages(0.1f);
			}
		}
		if (DeathDestroyDelay > 0.f)
		{
			SetLifeSpan(DeathDestroyDelay);
		}
		UCombatFeedbackLibrary::PlayCombatFeedback(this, DeathFeedback, this, nullptr, GetActorLocation());
		PrintAI(TEXT("死亡"), FColor::Red);
		break;
	}
	case EMonsterState::GetUp:
	{
		// PG-12: 立ち上がり Montage（ReactionMontages[GetUp]）の尺、無ければ GetUpTime だけ AI 停止。
		bMovingToEngageCombo = false;
		GetUpTimer = Combat ? Combat->GetUpTime : 1.f;
		if (const TObjectPtr<UAnimMontage>* M = ReactionMontages.Find(EMonsterState::GetUp))
		{
			if (*M)
			{
				GetUpTimer = (*M)->GetPlayLength();
			}
		}
		if (Combat && bInvulnerableDuringGetUp)
		{
			Combat->bInvulnerable = true;
		}
		PrintAI(TEXT("立ち上がり"), FColor::Green);
		break;
	}
	case EMonsterState::Attack:
		LaunchNextAttackInCombo();
		break;
	default:
		break;
	}

	PlayReaction(NewState);
	if (NewState == EMonsterState::Hitstun)
	{
		bPendingGuardedReaction = false;
	}
	OnStateChanged.Broadcast(Old, NewState);
}

// ---------------------------------------------------------------------------
// 行動パターン（仕様書 Monster シート「行動パターン」どおり順番に実行）
// ---------------------------------------------------------------------------

bool AMonsterCharacterBase::EvaluateComboCondition(const FMonsterComboData& C) const
{
	if (!TargetActor)
	{
		return false;
	}
	const float DistM = GetDistanceToTargetCm() / 100.f;
	const float AbsAngle = FMath::Abs(GetSignedAngleToTargetDeg());
	const bool bDistOk = C.MaxDistanceM <= 0.f || DistM < C.MaxDistanceM;
	const bool bAngleOk = C.bRequireTargetBehind ? (AbsAngle > 100.f) : (AbsAngle <= C.MaxAngleDeg);
	return bDistOk && bAngleOk;
}

bool AMonsterCharacterBase::RollComboProbability(const FMonsterComboData& C) const
{
	return FMath::FRandRange(0.f, 100.f) <= C.TriggerChancePercent;
}

void AMonsterCharacterBase::PrintAI(const FString& Msg, const FColor& Color) const
{
	if (!bPrintAIEvents)
	{
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[MonsterAI] %s"), *Msg);
#if !UE_BUILD_SHIPPING
	if (GEngine && CounterCoreDebug::IsOnScreenDebugEnabled())
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, Color, FString::Printf(TEXT("[AI] %s"), *Msg));
	}
#endif
}

void AMonsterCharacterBase::BeginActionStep()
{
	bMovingToEngageCombo = false;
	CurrentComboAttacks.Reset();
	ComboIndex = 0;

	if (!Attack || ActionLoop.Num() == 0)
	{
		EnterState(EMonsterState::Idle);
		return;
	}

	// 不正エントリや条件スキップで無限ループしないよう、1フレームでの評価数を制限。
	for (int32 Guard = 0; Guard <= ActionLoop.Num(); ++Guard)
	{
		CurrentComboId = ActionLoop[ActionLoopIndex % ActionLoop.Num()];

		bool bFound = false;
		CurrentComboData = Attack->GetComboData(CurrentComboId, bFound);
		if (!bFound)
		{
			PrintAI(FString::Printf(TEXT("step %d: %s 未定義 → スキップ"), ActionLoopIndex, *CurrentComboId.ToString()), FColor::Silver);
			ActionLoopIndex = (ActionLoopIndex + 1) % ActionLoop.Num();
			continue;
		}

		if (CurrentComboData.bSkipIfConditionUnmet)
		{
			// () 付き: その場で条件判定、未達なら移動せずスキップ。
			if (EvaluateComboCondition(CurrentComboData) && RollComboProbability(CurrentComboData))
			{
				PrintAI(FString::Printf(TEXT("step %d: (%s) 条件成立 → 発動"), ActionLoopIndex, *CurrentComboId.ToString()), FColor::Cyan);
				StartCurrentCombo();
				return;
			}
			PrintAI(FString::Printf(TEXT("step %d: (%s) 条件未達 → スキップ"), ActionLoopIndex, *CurrentComboId.ToString()), FColor::Silver);
			ActionLoopIndex = (ActionLoopIndex + 1) % ActionLoop.Num();
			continue;
		}

		// 通常ステップ: 移動で間合い・角度を詰めてから発生確率判定。
		PrintAI(FString::Printf(TEXT("step %d: %s → 間合いへ移動"), ActionLoopIndex, *CurrentComboId.ToString()), FColor::White);
		bMovingToEngageCombo = true;
		EnterState(EMonsterState::Run);
		return;
	}

	// 全ステップがスキップ対象だった（通常は起こらない）→ 少し移動して次フレーム再評価。
	EnterState(EMonsterState::Run);
}

void AMonsterCharacterBase::AdvanceActionStep()
{
	const int32 Prev = ActionLoopIndex;
	ActionLoopIndex = (ActionLoopIndex + 1) % FMath::Max(1, ActionLoop.Num());

	// ループが一周したら「待機」を挟む。
	if (ActionLoop.Num() > 0 && ActionLoopIndex == 0 && Prev != 0 && LoopRestTime > 0.f)
	{
		LoopRestTimer = LoopRestTime;
		PrintAI(TEXT("行動ループ一周 → 待機"), FColor::Green);
		EnterState(EMonsterState::Idle);
		return;
	}
	BeginActionStep();
}

void AMonsterCharacterBase::StartCurrentCombo()
{
	CurrentComboAttacks = CurrentComboData.AttackSequence.Num() > 0
		? CurrentComboData.AttackSequence
		: Attack->GetComboAttacks(CurrentComboId);
	ComboIndex = 0;

	if (CurrentComboAttacks.Num() == 0)
	{
		AdvanceActionStep();
		return;
	}

	if (State == EMonsterState::Attack)
	{
		LaunchNextAttackInCombo();
	}
	else
	{
		EnterState(EMonsterState::Attack);
	}
}

void AMonsterCharacterBase::LaunchNextAttackInCombo()
{
	if (!Attack)
	{
		AdvanceActionStep();
		return;
	}
	if (CurrentComboAttacks.IsValidIndex(ComboIndex))
	{
		Attack->StartAttack(CurrentComboAttacks[ComboIndex]);
		++ComboIndex;
	}
	else
	{
		// コンボ完了 → 次のステップへ。
		AdvanceActionStep();
	}
}

void AMonsterCharacterBase::ResumeAfterHitstun()
{
	HitstunKnockbackDir = FVector::ZeroVector;
	State = EMonsterState::Idle; // 遷移制限を解除するため一旦クリア

	if (bInterruptedAttackNoChain)
	{
		// 攻撃5: やられで次コンボへ連鎖しない。中断した攻撃から続行。
		PrintAI(TEXT("やられ硬直明け: 攻撃継続（連鎖なし）"), FColor::Yellow);
		if (CurrentComboAttacks.IsValidIndex(ComboIndex - 1))
		{
			--ComboIndex; // 中断された一手をやり直す
		}
		if (CurrentComboAttacks.IsValidIndex(ComboIndex))
		{
			EnterState(EMonsterState::Attack);
		}
		else
		{
			AdvanceActionStep();
		}
	}
	else
	{
		// 仕様: やられ割り込み → 次の攻撃処理（次のコンボ）へ。
		PrintAI(TEXT("やられ硬直明け: 次のコンボへ"), FColor::Yellow);
		AdvanceActionStep();
	}
}

void AMonsterCharacterBase::HandleAttackFinished()
{
	if (State != EMonsterState::Attack)
	{
		return;
	}
	// 仕様: コンボ内の以降の攻撃は条件無視で続行。
	LaunchNextAttackInCombo();
}

void AMonsterCharacterBase::HandleCombatStateRequest(EMonsterState Requested)
{
	RequestState(Requested);
}

void AMonsterCharacterBase::HandleToggleHitbox(bool bEnable)
{
	if (!ActiveHitbox)
	{
		return;
	}
	if (bEnable)
	{
		// PG-16: 再ヒット可の区間（と最初の区間）だけリセット。
		if (!Attack || Attack->ShouldResetHitTargets())
		{
			HitActorsThisSwing.Reset();
		}
		ActiveHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		UpdateHitboxDebugVisual(true);
		// 判定ONの瞬間に既に重なっている相手も拾う。
		TArray<AActor*> Overlapping;
		ActiveHitbox->GetOverlappingActors(Overlapping, APawn::StaticClass());
		for (AActor* Other : Overlapping)
		{
			OnHitboxOverlap(ActiveHitbox, Other, nullptr, 0, false, FHitResult());
		}
	}
	else
	{
		ActiveHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UpdateHitboxDebugVisual(false);
	}
}

void AMonsterCharacterBase::HandlePlayAttackAnim(FName AttackId)
{
	// 攻撃開始（予兆の頭）で攻撃モンタージュを再生。
	HitActorsThisSwing.Reset();
	EndWindupHold();
	PlayAttackMontage(AttackId);

	// PG-18: 攻撃開始の演出（風切り SE / トレイル ON）。
	if (Attack)
	{
		const FMonsterAttackFrameData Data = Attack->GetActiveData();
		StopTrail();
		UCombatFeedbackLibrary::PlayCombatFeedback(this, Data.SwingFeedback, this, ActiveHitbox, GetActorLocation());
		if (Data.SwingFeedback.Trail == ETrailAction::Start)
		{
			CurrentTrailTag = Data.SwingFeedback.TrailComponentTag;
		}
	}
}

void AMonsterCharacterBase::StopTrail()
{
	if (!CurrentTrailTag.IsNone())
	{
		UCombatFeedbackLibrary::SetTrailActive(this, CurrentTrailTag, false);
		CurrentTrailTag = NAME_None;
	}
}

void AMonsterCharacterBase::HandleAttackHitActive(FName AttackId)
{
	// 攻撃判定 ON の瞬間に斬撃 VFX。
	PlayAttackVFX(AttackId);
}

void AMonsterCharacterBase::OnHitboxOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!OtherActor || OtherActor == this)
	{
		return;
	}
	// 攻撃対象（プレイヤー）のみ、1スイング1ヒット。
	if (TargetActor && OtherActor != TargetActor)
	{
		return;
	}
	if (HitActorsThisSwing.Contains(OtherActor))
	{
		return;
	}
	HitActorsThisSwing.Add(OtherActor);

	// PG-16: 多段判定の区間ごとの攻撃力。
	const int32 Power = (Attack && Attack->IsAttacking()) ? Attack->GetCurrentHitDamage() : CurrentAttackPower();
	if (Power <= 0)
	{
		return;
	}
	const FMonsterAttackFrameData Data = Attack ? Attack->GetActiveData() : FMonsterAttackFrameData();

	// プレイヤーのガード / コンバットコンポーネントがあれば直接そちらへ
	// （盾耐久・ゲージ変換・被弾処理は C++ コンポーネントが持つ）。
	bool bRouted = false;
	if (UPlayerGuardComponent* PlayerGuard = OtherActor->FindComponentByClass<UPlayerGuardComponent>())
	{
		if (PlayerGuard->IsGuarding() && PlayerGuard->HandleGuardedHit(Power, Power, /*bJustGuard*/ false, this))
		{
			// PG-06: 攻撃者へ通知（判定を打ち切り、被ガードやられへ）。
			OnAttackGuarded(Power);
			return;
		}
	}
	if (!bRouted)
	{
		if (UPlayerCombatComponent* PlayerCombat = OtherActor->FindComponentByClass<UPlayerCombatComponent>())
		{
			PlayerCombat->TakeIncomingHit(Power, /*bGuarded*/ false);
			bRouted = true;
		}
	}
	if (!bRouted)
	{
		UGameplayStatics::ApplyDamage(OtherActor, static_cast<float>(Power), GetController(), this,
			UDamageType::StaticClass());
	}

	// PG-19: 攻撃別ヒットストップ（DT）、無ければ既定。命中時のみ。
	if (State != EMonsterState::Dead)
	{
		UCombatFeedbackLibrary::ApplyHitStop(this, Data.HitStop.bEnabled ? Data.HitStop : HitStop);
	}
	UCombatFeedbackLibrary::PlayCombatFeedback(this, Data.HitFeedback, this, nullptr, OtherActor->GetActorLocation());
	PlayCameraShake(AttackHitCameraShake);       // 仕様書 Battle: 攻撃ヒット時のカメラシェイク
}

void AMonsterCharacterBase::OnAttackGuarded(int32 AttackPower)
{
	// 今の判定区間はここで終了（同じ振りで二重にガードさせない）。
	if (Attack)
	{
		Attack->EndCurrentHitWindow();
	}
	if (!bReactToGuard || State == EMonsterState::Dead || !Combat)
	{
		return;
	}
	// 被ガードやられ: HandleIncomingHit(true) がスタン加算と Hitstun 要求を行う（攻撃5 などやられ無効区間は弾かれる）。
	bPendingGuardedReaction = true;
	Combat->HandleIncomingHit(AttackPower, /*bGuardedByPlayer*/ true);
	bPendingGuardedReaction = false;
}

void AMonsterCharacterBase::ApplyHitStop()
{
	if (State == EMonsterState::Dead)
	{
		return;
	}
	UCombatFeedbackLibrary::ApplyHitStop(this, HitStop);
}

void AMonsterCharacterBase::PlayCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass) const
{
	if (!ShakeClass || CameraShakeScale <= 0.f)
	{
		return;
	}
	// 仕様書 Battle「カメラシェイク」: 攻撃ヒット時 / 被弾時に微小振動。CameraShakeScale で控えめに。
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraShake(ShakeClass, CameraShakeScale);
		}
	}
}

void AMonsterCharacterBase::DealDamageToTarget(int32 AttackPower)
{
	const int32 Power = AttackPower > 0 ? AttackPower : CurrentAttackPower();
	if (TargetActor && Power > 0)
	{
		UGameplayStatics::ApplyDamage(TargetActor, static_cast<float>(Power), GetController(), this,
			UDamageType::StaticClass());
	}
}

void AMonsterCharacterBase::PlayAttackMontage_Implementation(FName AttackId)
{
	// DT_MonsterAttacks.Montage を優先し、未設定なら AttackMontages[AttackId]。
	bool bRowFound = false;
	const FMonsterAttackFrameData Row = Attack ? Attack->GetAttackData(AttackId, bRowFound) : FMonsterAttackFrameData();
	UAnimMontage* Montage = Row.Montage;
	if (!Montage)
	{
		const TObjectPtr<UAnimMontage>* Found = AttackMontages.Find(AttackId);
		Montage = Found ? Found->Get() : nullptr;
	}
	if (!Montage)
	{
		return;
	}
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return;
	}
	CurrentAttackMontage = Montage;

	// 仕様書「攻撃詳細」のタイムライン（DT_MonsterAttacks.EndTime）にモンタージュ尺を合わせる。
	// AM_Monster* は攻撃ウィンドウより長め（例: AM_MonsterAttack5=3.7s / Attack05_1=1.5s）なので、
	// 等倍で流すと次の一手や硬直に食い込む。再生レートで詰める。
	float PlayRate = 1.f;
	if (bScaleAttackMontageToTimeline)
	{
		const float TimelineLen = Row.EndTime;
		const float MontageLen = Montage->GetPlayLength();
		if (bRowFound && TimelineLen > KINDA_SMALL_NUMBER && MontageLen > KINDA_SMALL_NUMBER)
		{
			PlayRate = FMath::Clamp(MontageLen / TimelineLen,
				AttackMontageRateRange.X, AttackMontageRateRange.Y);
		}
	}
	Anim->Montage_Play(Montage, PlayRate);
}

void AMonsterCharacterBase::PlayAttackVFX_Implementation(FName AttackId)
{
	if (!bPlayAttackVFX)
	{
		return;
	}
	bool bRowFound = false;
	UNiagaraSystem* VFX = Attack ? Attack->GetAttackData(AttackId, bRowFound).AttackVFX.Get() : nullptr;
	if (!VFX)
	{
		const TObjectPtr<UNiagaraSystem>* Found = AttackVFX.Find(AttackId);
		VFX = Found ? Found->Get() : nullptr;
	}
	if (!VFX)
	{
		return;
	}

	// 武器の判定コンポーネント（無ければ武器ルート／メッシュ）にアタッチしてスポーン。
	USceneComponent* Attach = ActiveHitbox;
	if (!Attach && WeaponActor && WeaponActor->GetChildActor())
	{
		Attach = WeaponActor->GetChildActor()->GetRootComponent();
	}
	if (!Attach)
	{
		Attach = GetMesh();
	}
	if (!Attach)
	{
		return;
	}

	UNiagaraFunctionLibrary::SpawnSystemAttached(VFX, Attach, NAME_None, AttackVFXOffset,
		FRotator::ZeroRotator, EAttachLocation::SnapToTargetIncludingScale, true);
}

void AMonsterCharacterBase::PlayReaction_Implementation(EMonsterState NewState)
{
	// PG-06: 被ガードのやられは専用 Montage（設定時）。
	if (NewState == EMonsterState::Hitstun && bPendingGuardedReaction && GuardedReactionMontage)
	{
		if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			Anim->Montage_Play(GuardedReactionMontage);
		}
		return;
	}
	if (TObjectPtr<UAnimMontage>* Found = ReactionMontages.Find(NewState))
	{
		if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			if (*Found)
			{
				Anim->Montage_Play(*Found);
			}
		}
	}
}

float AMonsterCharacterBase::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float Actual = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Combat && Actual > 0.f && State != EMonsterState::Dead)
	{
		// bGuardedByPlayer = false（ガード判定はプレイヤー側が別途 HandleIncomingHit(true) を呼ぶ想定）。
		Combat->HandleIncomingHit(FMath::RoundToInt(DamageAmount), false);
		ApplyHitStop();                          // 仕様書 Battle: 被弾時のヒットストップ
		PlayCameraShake(DamagedCameraShake);     // 仕様書 Battle: 被弾時のカメラシェイク
	}
	return Actual;
}

void AMonsterCharacterBase::HandleCombatDamaged(const FMonsterDamageResult& Result)
{
	if (Result.AppliedDamage > 0)
	{
		UCombatFeedbackLibrary::PlayCombatFeedback(this, DamagedFeedback, this, nullptr, GetActorLocation());
	}
}

void AMonsterCharacterBase::ReceiveCombatEvent_Implementation(FName EventName)
{
	if (EventName == CombatEventNames.HitStart)
	{
		if (Attack && State == EMonsterState::Attack) { Attack->NotifyHitStart(); }
	}
	else if (EventName == CombatEventNames.HitEnd)
	{
		if (Attack) { Attack->NotifyHitEnd(); }
	}
	else if (EventName == CombatEventNames.WindupEnd)
	{
		BeginWindupHold();
	}
	else if (EventName == CombatEventNames.DeathEnd)
	{
		if (State == EMonsterState::Dead)
		{
			StartDeathRagdoll();
		}
	}
}

void AMonsterCharacterBase::BeginWindupHold()
{
	// PG-14: WindupEnd で Montage を PlayRate 0 にし、WindupHoldTime 後に再開。タイムラインも止める。
	if (bWindupHolding || State != EMonsterState::Attack || !Attack || !Attack->IsAttacking())
	{
		return;
	}
	const float Hold = Attack->GetActiveData().WindupHoldTime;
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (Hold <= 0.f || !Anim || !CurrentAttackMontage)
	{
		return;
	}
	bWindupHolding = true;
	SavedWindupPlayRate = Anim->Montage_GetPlayRate(CurrentAttackMontage);
	Anim->Montage_SetPlayRate(CurrentAttackMontage, 0.f);
	Attack->SetTimelinePaused(true);
	GetWorldTimerManager().SetTimer(WindupTimerHandle, this, &AMonsterCharacterBase::EndWindupHold, Hold, false);
}

void AMonsterCharacterBase::EndWindupHold()
{
	if (!bWindupHolding)
	{
		return;
	}
	bWindupHolding = false;
	GetWorldTimerManager().ClearTimer(WindupTimerHandle);
	if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (CurrentAttackMontage && Anim->Montage_IsPlaying(CurrentAttackMontage))
		{
			Anim->Montage_SetPlayRate(CurrentAttackMontage, SavedWindupPlayRate > 0.f ? SavedWindupPlayRate : 1.f);
		}
	}
	if (Attack)
	{
		Attack->SetTimelinePaused(false);
	}
}

void AMonsterCharacterBase::StartDeathRagdoll()
{
	if (bRagdolled)
	{
		return;
	}
	bRagdolled = true;
	USkeletalMeshComponent* M = GetMesh();
	if (!M)
	{
		return;
	}
	if (bRagdollOnDeath && M->GetPhysicsAsset())
	{
		M->SetCollisionProfileName(TEXT("Ragdoll"));
		M->SetAllBodiesSimulatePhysics(true);
		M->SetSimulatePhysics(true);
		M->WakeAllRigidBodies();
	}
	else
	{
		// ラグドールしない場合は死亡 Montage を末尾で一時停止し、死亡ポーズのまま保持する（待機へ戻さない）。
		const TObjectPtr<UAnimMontage>* DeathM = ReactionMontages.Find(EMonsterState::Dead);
		UAnimInstance* Anim = M->GetAnimInstance();
		if (DeathM && *DeathM && Anim)
		{
			UAnimMontage* DM = *DeathM;
			if (!Anim->Montage_IsActive(DM))
			{
				// 既に終了 / ブレンドアウト済みなら末尾付近から再生し直して止める。
				Anim->Montage_Play(DM, 1.f, EMontagePlayReturnType::MontageLength, 0.f, true);
				Anim->Montage_SetPosition(DM, FMath::Max(0.f, DM->GetPlayLength() - DM->BlendOut.GetBlendTime() - 0.05f));
			}
			Anim->Montage_Pause(DM);
		}
		else
		{
			M->bPauseAnims = true;
		}
	}
}

void AMonsterCharacterBase::HandleMontageEnded(UAnimMontage* Montage, bool /*bInterrupted*/)
{
	if (State != EMonsterState::Dead)
	{
		return;
	}
	const TObjectPtr<UAnimMontage>* DeathM = ReactionMontages.Find(EMonsterState::Dead);
	if (DeathM && *DeathM == Montage)
	{
		StartDeathRagdoll();
	}
}

void AMonsterCharacterBase::TickGetUp(float Dt)
{
	GetUpTimer -= Dt;
	if (GetUpTimer > 0.f)
	{
		return;
	}
	if (Combat)
	{
		Combat->bInvulnerable = false;
	}
	if (bResumeHitstunAfterGetUp)
	{
		bResumeHitstunAfterGetUp = false;
		ResumeAfterHitstun();
	}
	else
	{
		EnterState(EMonsterState::Idle);
	}
}

void AMonsterCharacterBase::Tick(float Dt)
{
	Super::Tick(Dt);

	if (bEnableDebugKeys)
	{
		PollDebugKeys();
	}

	switch (State)
	{
	case EMonsterState::Idle:    TickIdle(Dt); break;
	case EMonsterState::Run:     TickRun(Dt); break;
	case EMonsterState::Attack:  TickAttack(Dt); break;
	case EMonsterState::Hitstun: TickHitstun(Dt); break;
	case EMonsterState::GetUp:   TickGetUp(Dt); break;
	case EMonsterState::Dead:
		// 死亡 Montage が末尾に達したらラグドール / ポーズ固定（DeathEnd Notify が無い Montage 向け）。
		if (!bRagdolled)
		{
			const TObjectPtr<UAnimMontage>* DeathM = ReactionMontages.Find(EMonsterState::Dead);
			UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
			if (DeathM && *DeathM && Anim && Anim->Montage_GetPosition(*DeathM) >= (*DeathM)->GetPlayLength() - (*DeathM)->BlendOut.GetBlendTime() - 0.05f)
			{
				StartDeathRagdoll();
			}
		}
		break;
	default: break;
	}
}

void AMonsterCharacterBase::PollDebugKeys()
{
	// cc.Debug 0 でデバッグキー（U/I/O/K/L）とヒント表示をまとめて無効化。
	if (!CounterCoreDebug::IsOnScreenDebugEnabled())
	{
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()) + 900000, 0.2f, FColor::White,
			TEXT("[敵デバッグ] U=スタン  I=やられ  O=死亡  N=ガード被弾  M=通常被弾"));
	}
#endif

	if (PC->WasInputKeyJustPressed(EKeys::U))
	{
		DebugTriggerStun();
	}
	if (PC->WasInputKeyJustPressed(EKeys::I))
	{
		DebugTriggerHitstun();
	}
	if (PC->WasInputKeyJustPressed(EKeys::O))
	{
		DebugTriggerDead();
	}
	if (PC->WasInputKeyJustPressed(EKeys::N))
	{
		DebugGuardedHit();
	}
	if (PC->WasInputKeyJustPressed(EKeys::M))
	{
		DebugPlayerHit();
	}
}

void AMonsterCharacterBase::DebugTriggerStun()
{
	if (State == EMonsterState::Dead)
	{
		return;
	}
	if (Combat)
	{
		Combat->Status.Stun = Combat->Status.MaxStun;
	}
	PrintAI(TEXT("[DEBUG] スタン発火 (U)"), FColor::Purple);
	ForceState(EMonsterState::Stun);
}

void AMonsterCharacterBase::DebugTriggerHitstun()
{
	if (State == EMonsterState::Dead)
	{
		return;
	}
	PrintAI(TEXT("[DEBUG] やられ発火 (I)"), FColor::Orange);
	ForceState(EMonsterState::Hitstun);
}

void AMonsterCharacterBase::DebugTriggerDead()
{
	if (State == EMonsterState::Dead)
	{
		return;
	}
	if (Combat)
	{
		Combat->Status.Hp = 0;
		Combat->OnDied.Broadcast();
	}
	PrintAI(TEXT("[DEBUG] 死亡発火 (O)"), FColor::Red);
	ForceState(EMonsterState::Dead);
}

void AMonsterCharacterBase::DebugGuardedHit()
{
	if (State == EMonsterState::Dead || !Combat)
	{
		return;
	}
	PrintAI(TEXT("[DEBUG] ガード被弾 (N)"), FColor::Orange);
	Combat->HandleIncomingHit(50, /*bGuardedByPlayer*/ true);
}

void AMonsterCharacterBase::DebugPlayerHit()
{
	if (State == EMonsterState::Dead || !Combat)
	{
		return;
	}
	PrintAI(TEXT("[DEBUG] 通常被弾 +スタン15 (M)"), FColor::Yellow);
	Combat->HandleIncomingHit(60, /*bGuardedByPlayer*/ false);
	Combat->AddStun(15); // プレイヤー中攻撃相当のスタン蓄積（Player シート）
}

void AMonsterCharacterBase::TickIdle(float Dt)
{
	// ループ一周後の「待機」。
	if (LoopRestTimer > 0.f)
	{
		LoopRestTimer -= Dt;
		return;
	}
	if (TargetActor && GetDistanceToTargetCm() <= DetectionRange)
	{
		BeginActionStep();
	}
}

void AMonsterCharacterBase::TickRun(float Dt)
{
	if (!TargetActor)
	{
		EnterState(EMonsterState::Idle);
		return;
	}

	// 向きは常にターゲットへ。
	const FRotator Look = UKismetMathLibrary::FindLookAtRotation(GetActorLocation(), TargetActor->GetActorLocation());
	const FRotator Cur = GetActorRotation();
	SetActorRotation(FRotator(Cur.Pitch, FMath::FInterpTo(Cur.Yaw, Look.Yaw, Dt, 8.f), Cur.Roll));

	if (bMovingToEngageCombo)
	{
		// 現在ステップのコンボの間合い・角度を満たしたか。
		if (EvaluateComboCondition(CurrentComboData))
		{
			bMovingToEngageCombo = false;
			if (RollComboProbability(CurrentComboData))
			{
				PrintAI(FString::Printf(TEXT("%s: 間合い到達・発生成功 → 発動"), *CurrentComboId.ToString()), FColor::Cyan);
				StartCurrentCombo();
			}
			else
			{
				PrintAI(FString::Printf(TEXT("%s: 発生確率 %d%% 失敗 → 次のコンボ"),
					*CurrentComboId.ToString(), FMath::RoundToInt(CurrentComboData.TriggerChancePercent)), FColor(255, 140, 0));
				AdvanceActionStep();
			}
			return;
		}
		AddMovementInput(GetActorForwardVector(), 1.f);
		return;
	}

	// ステップ未設定で Run にいる（想定外）→ 近づいたら再評価。
	if (GetDistanceToTargetCm() <= EngageRange)
	{
		BeginActionStep();
	}
	else
	{
		AddMovementInput(GetActorForwardVector(), 1.f);
	}
}

void AMonsterCharacterBase::TickAttack(float)
{
	// タイムライン駆動は Attack コンポーネント側。ここでは監視のみ。
	if (Attack && !Attack->IsAttacking() && State == EMonsterState::Attack)
	{
		HandleAttackFinished();
	}
}

void AMonsterCharacterBase::TickHitstun(float Dt)
{
	const float Duration = Combat ? Combat->HitstunDuration : 0.4f;
	const float KnockStart = Combat ? Combat->HitstunKnockbackStart : 0.1f;
	const float Elapsed = Duration - HitstunTimer;

	// 仕様: [0.1s] からノックバック開始 → [0.4s] 硬直終了。総移動量は 0.2M。
	if (!HitstunKnockbackDir.IsNearlyZero() && Elapsed >= KnockStart)
	{
		const float KnockWindow = FMath::Max(0.01f, Duration - KnockStart);
		const float SpeedCmPerSec = (Combat ? Combat->HitstunKnockbackM : 0.2f) * 100.f / KnockWindow;
		AddActorWorldOffset(HitstunKnockbackDir * SpeedCmPerSec * Dt, true);
	}

	HitstunTimer -= Dt;
	if (HitstunTimer <= 0.f)
	{
		// PG-12: やられも GetUp 対象なら立ち上がってから復帰。
		if (GetUpSourceStates.Contains(EMonsterState::Hitstun))
		{
			bResumeHitstunAfterGetUp = true;
			EnterState(EMonsterState::GetUp);
			return;
		}
		ResumeAfterHitstun(); // 仕様: 硬直終了 → 次の攻撃処理へ（攻撃5は連鎖しない）
	}
}
