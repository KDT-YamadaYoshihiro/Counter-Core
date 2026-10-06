#include "Player/PlayerActionComponent.h"
#include "Player/PlayerCombatComponent.h"
#include "Player/PlayerGuardComponent.h"
#include "Enemy/MonsterCombatComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BoxComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "CounterCoreDebug.h"
#include "Common/CombatFeedbackLibrary.h"
#include "Player/PlayerCameraComponent.h"
#include "Engine/DataTable.h"

UPlayerActionComponent::UPlayerActionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UPlayerCombatComponent* UPlayerActionComponent::GetCombat() const
{
	return Combat;
}
UPlayerGuardComponent* UPlayerActionComponent::GetGuard() const
{
	return Guard;
}

void UPlayerActionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		Combat = Owner->FindComponentByClass<UPlayerCombatComponent>();
		Guard = Owner->FindComponentByClass<UPlayerGuardComponent>();

		// 旧 BP の近接コンポーネント（RightHand）は常時 NoCollision にして旧処理を止める。
		TArray<UPrimitiveComponent*> Prims;
		Owner->GetComponents<UPrimitiveComponent>(Prims);
		USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
		for (UPrimitiveComponent* Prim : Prims)
		{
			if (Prim && Prim->GetFName() == LegacyMeleeComponentName)
			{
				Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}

		USceneComponent* MeshOrRoot = Mesh ? (USceneComponent*)Mesh : Owner->GetRootComponent();

		// 剣を手に生成（敵と同じ BP_Weapon）。判定も敵側（ResolveAttackHitbox）と同じく、
		// 武器内のシェイプをそのまま攻撃判定として使う（剣の位置 / モーションに追従させるため）。
		UShapeComponent* WeaponShape = nullptr;
		if (WeaponClass)
		{
			WeaponActor = NewObject<UChildActorComponent>(Owner, TEXT("PlayerWeapon"));
			WeaponActor->SetupAttachment(MeshOrRoot, WeaponSocket);
			WeaponActor->RegisterComponent();
			WeaponActor->SetChildActorClass(WeaponClass);
			WeaponActor->CreateChildActor();

			if (AActor* W = WeaponActor->GetChildActor())
			{
				WeaponShape = W->FindComponentByClass<UShapeComponent>();
			}
		}

		if (WeaponShape)
		{
			// 武器のシェイプをそのまま判定に使う。ソケットにアタッチされているので
			// 剣の位置とアニメーション（モンタージュでのボーン移動）にそのまま追従する。
			MeleeHitbox = WeaponShape;
		}
		else
		{
			// 武器未設定時のみのフォールバック。WeaponSocket にアタッチし、剣がなくても
			// 手の位置基準で判定できるようにする（プレイヤー前方固定オフセットにはしない）。
			UBoxComponent* Box = NewObject<UBoxComponent>(Owner, TEXT("PlayerMeleeHitbox"));
			Box->SetupAttachment(MeshOrRoot, WeaponSocket);
			Box->RegisterComponent();
			Box->SetRelativeLocation(MeleeHitboxOffset);
			Box->SetBoxExtent(MeleeHitboxExtent);
			MeleeHitbox = Box;
		}

		if (MeleeHitbox)
		{
			MeleeHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeleeHitbox->SetCollisionObjectType(ECC_WorldDynamic);
			MeleeHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
			MeleeHitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
			MeleeHitbox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
			MeleeHitbox->SetGenerateOverlapEvents(true);
			// カメラ判定を貫通させる（近接時のスプリングアーム寄り対策）。
			MeleeHitbox->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			MeleeHitbox->OnComponentBeginOverlap.AddDynamic(this, &UPlayerActionComponent::OnMeleeOverlap);
		}
	}

	if (Combat)
	{
		Combat->OnStateChanged.AddDynamic(this, &UPlayerActionComponent::HandleCombatStateChanged);
	}

	PotionCount = FMath::Clamp(InitialPotionCount, 0, MaxPotionCount);
	OnPotionCountChanged.Broadcast(PotionCount, MaxPotionCount);

	BindInput();
}

UAnimInstance* UPlayerActionComponent::GetAnimInstance() const
{
	if (USkeletalMeshComponent* Mesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr)
	{
		return Mesh->GetAnimInstance();
	}
	return nullptr;
}

void UPlayerActionComponent::AddPotion(int32 Delta)
{
	const int32 New = FMath::Clamp(PotionCount + Delta, 0, MaxPotionCount);
	if (New != PotionCount)
	{
		PotionCount = New;
		OnPotionCountChanged.Broadcast(PotionCount, MaxPotionCount);
	}
}

void UPlayerActionComponent::SetMoveInputIgnored(bool bIgnore)
{
	if (bMoveInputIgnored == bIgnore)
	{
		return;
	}
	bMoveInputIgnored = bIgnore;
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (AController* C = Pawn->GetController())
		{
			C->SetIgnoreMoveInput(bIgnore); // カウンタ式なのでガード等と衝突しない
		}
	}
}

void UPlayerActionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Combat)
	{
		Combat->OnStateChanged.RemoveDynamic(this, &UPlayerActionComponent::HandleCombatStateChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void UPlayerActionComponent::BindInput()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (InputMapping)
			{
				Subsys->AddMappingContext(InputMapping, InputMappingPriority);
			}
		}
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	if (!EIC)
	{
		// まだ Possess されていない等。少し待って再試行。
		if (UWorld* World = GetWorld())
		{
			FTimerHandle Retry;
			World->GetTimerManager().SetTimer(Retry, this, &UPlayerActionComponent::BindInput, 0.2f, false);
		}
		return;
	}

	if (IA_AttackSmall)  EIC->BindAction(IA_AttackSmall, ETriggerEvent::Started, this, &UPlayerActionComponent::OnAttackSmall);
	if (IA_AttackMedium) EIC->BindAction(IA_AttackMedium, ETriggerEvent::Started, this, &UPlayerActionComponent::OnAttackMedium);
	if (IA_AttackHeavy)  EIC->BindAction(IA_AttackHeavy, ETriggerEvent::Started, this, &UPlayerActionComponent::OnAttackHeavy);
	if (IA_Dodge)        EIC->BindAction(IA_Dodge, ETriggerEvent::Started, this, &UPlayerActionComponent::OnDodgeInput);
	if (IA_Heal)         EIC->BindAction(IA_Heal, ETriggerEvent::Started, this, &UPlayerActionComponent::OnHealInput);
	if (IA_Guard)
	{
		EIC->BindAction(IA_Guard, ETriggerEvent::Started, this, &UPlayerActionComponent::OnGuardStarted);
		EIC->BindAction(IA_Guard, ETriggerEvent::Completed, this, &UPlayerActionComponent::OnGuardCompleted);
		EIC->BindAction(IA_Guard, ETriggerEvent::Canceled, this, &UPlayerActionComponent::OnGuardCompleted);
	}

	// フォールバックキーは TickComponent の PollFallbackInput でポーリング（Enhanced Input では
	// BindKey が使えないため）。
}

void UPlayerActionComponent::OnGuardStarted(const FInputActionValue&)
{
	if (Guard && CanStartAction(EPlayerActionType::Guard))
	{
		Guard->StartGuard();
	}
}
void UPlayerActionComponent::OnGuardCompleted(const FInputActionValue&)
{
	if (Guard)
	{
		Guard->StopGuard();
	}
}
void UPlayerActionComponent::OnMoveInput(const FInputActionValue&)
{
}

void UPlayerActionComponent::PollFallbackInput()
{
	if (!bBindFallbackKeys)
	{
		return;
	}
	APlayerController* PC = GetOwner() ? Cast<APlayerController>(Cast<APawn>(GetOwner())->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}

	auto Pressed = [PC](FKey A, FKey B) { return PC->WasInputKeyJustPressed(A) || PC->WasInputKeyJustPressed(B); };

	// 左クリックは小攻撃（旧 BP の IA_MyAttack は IMC から外し、こちらに一本化する）。
	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)) { TryAttack(EPlayerAttackTier::Small); }
	if (Pressed(EKeys::Gamepad_FaceButton_Left, EKeys::J))   { TryAttack(EPlayerAttackTier::Small); }
	if (Pressed(EKeys::Gamepad_FaceButton_Top, EKeys::K))    { TryAttack(EPlayerAttackTier::Medium); }
	if (Pressed(EKeys::Gamepad_RightShoulder, EKeys::L))     { TryAttack(EPlayerAttackTier::Heavy); }
	if (Pressed(EKeys::Gamepad_FaceButton_Bottom, EKeys::SpaceBar)) { TryDodge(); }
	if (Pressed(EKeys::Gamepad_FaceButton_Right, EKeys::H))  { TryHeal(); }

	const bool bGuardDown = PC->IsInputKeyDown(EKeys::Gamepad_RightTrigger) || PC->IsInputKeyDown(EKeys::RightMouseButton);
	if (Guard)
	{
		if (bGuardDown && !Guard->IsGuarding() && CanStartAction(EPlayerActionType::Guard))
		{
			Guard->StartGuard();
		}
		else if (!bGuardDown && Guard->IsGuarding())
		{
			Guard->StopGuard();
		}
	}
}

// --------------------------------------------------------------------------

bool UPlayerActionComponent::CanStartAction(EPlayerActionType Action) const
{
	if (Combat)
	{
		const EPlayerCombatState S = Combat->GetCombatState();
		if (S == EPlayerCombatState::Stun || !Combat->IsAlive())
		{
			return false;
		}
		if (S == EPlayerCombatState::Hit)
		{
			return false; // のけぞり中は何もできない
		}
	}
	if (Guard && Guard->IsGuarding() && Action != EPlayerActionType::Dodge && Action != EPlayerActionType::Attack)
	{
		return false;
	}

	// PG-05: 回復中は許可フラグの操作だけ。
	if (CurrentAction == EPlayerActionType::Heal)
	{
		switch (Action)
		{
		case EPlayerActionType::Dodge:  return bAllowDodgeWhileHealing;
		case EPlayerActionType::Guard:  return bAllowGuardWhileHealing;
		case EPlayerActionType::Attack: return bAllowAttackWhileHealing;
		case EPlayerActionType::Move:   return bAllowMoveWhileHealing;
		default:                        return false;
		}
	}

	// 優先度: 移動 < ガード < 攻撃 < 回避。上位は下位を割り込める。
	auto Prio = [](EPlayerActionType A) -> int32
	{
		switch (A)
		{
		case EPlayerActionType::Dodge:  return 4;
		case EPlayerActionType::Attack: return 3;
		case EPlayerActionType::Heal:   return 3;
		case EPlayerActionType::Guard:  return 2;
		case EPlayerActionType::Move:   return 1;
		default:                        return 0;
		}
	};

	switch (CurrentAction)
	{
	case EPlayerActionType::None:
	case EPlayerActionType::Move:
		return true;
	case EPlayerActionType::Guard:
		return Prio(Action) > Prio(EPlayerActionType::Guard);
	case EPlayerActionType::Attack:
		return Action == EPlayerActionType::Dodge; // 攻撃中は回避のみ割り込める
	case EPlayerActionType::Dodge:
		return false;
	default:
		return true;
	}
}

FName UPlayerActionComponent::StartIdForTier(EPlayerAttackTier Tier) const
{
	switch (Tier)
	{
	case EPlayerAttackTier::Medium: return MediumStartId;
	case EPlayerAttackTier::Heavy:  return HeavyStartId;
	default:                        return SmallStartId;
	}
}

bool UPlayerActionComponent::GetAttackRow(FName AttackId, FPlayerAttackRow& OutRow) const
{
	if (AttackDataTable)
	{
		if (const FPlayerAttackRow* Row = AttackDataTable->FindRow<FPlayerAttackRow>(AttackId, TEXT("GetAttackRow"), false))
		{
			OutRow = *Row;
			return true;
		}
	}
	return false;
}

void UPlayerActionComponent::TryAttack(EPlayerAttackTier Tier)
{
	if (CurrentAction == EPlayerActionType::Attack)
	{
		// コンボ受付: ウィンドウ中に同段の入力があれば次の派生を予約。
		if (Tier == CurrentAttackRow.Tier && AttackElapsed >= CurrentAttackRow.ComboWindowStart
			&& CurrentAttackRow.NextComboId != NAME_None)
		{
			bComboQueued = true;
			QueuedTier = Tier;
		}
		return;
	}

	if (ComboCooldownTimer > 0.f)
	{
		return; // コンボ直後の連打は無視（痙攣防止）
	}
	if (!CanStartAction(EPlayerActionType::Attack))
	{
		return;
	}

	const FName StartId = StartIdForTier(Tier);
	FPlayerAttackRow Row;
	if (!GetAttackRow(StartId, Row))
	{
		PrintAction(FString::Printf(TEXT("攻撃データ未定義: %s"), *StartId.ToString()), FColor::Silver);
		return;
	}

	// ゲージ消費（仕様: 小1 / 中2 / 大4 枠）。足りなければ発動しない。
	if (Row.GaugeCost > 0 && Combat && !Combat->TryConsumeGauge(Row.GaugeCost))
	{
		PrintAction(FString::Printf(TEXT("%s: ゲージ不足（%d 枠）"), *StartId.ToString(), Row.GaugeCost), FColor(255, 140, 0));
		return;
	}

	StartAttackRow(StartId);
}

void UPlayerActionComponent::StartAttackRow(FName AttackId)
{
	if (!GetAttackRow(AttackId, CurrentAttackRow))
	{
		FinishAttack();
		return;
	}
	CurrentAttackId = AttackId;
	AttackElapsed = 0.f;
	bComboQueued = false;
	bMeleeActive = false;
	bAttackStepActive = true;
	CurrentPlayRate = 1.f;
	HitActorsThisSwing.Reset();

	SetCurrentAction(EPlayerActionType::Attack);
	if (Combat)
	{
		Combat->SetCombatState(EPlayerCombatState::Attack);
	}

	if (CurrentAttackRow.Montage)
	{
		if (UAnimInstance* Anim = GetAnimInstance())
		{
			// PlayRate > 0 なら DT 指定（PG-09 の 1:1 同期）、0 なら従来どおり EndTime に収まるよう自動調整。
			const float MontageLen = CurrentAttackRow.Montage->GetPlayLength();
			if (CurrentAttackRow.PlayRate > 0.f)
			{
				CurrentPlayRate = CurrentAttackRow.PlayRate;
			}
			else
			{
				CurrentPlayRate = (CurrentAttackRow.EndTime > 0.05f && MontageLen > 0.05f)
					? FMath::Clamp(MontageLen / CurrentAttackRow.EndTime, 0.2f, 3.f)
					: 1.f;
			}
			Anim->Montage_Play(CurrentAttackRow.Montage, CurrentPlayRate);
		}
	}

	AActor* Owner = GetOwner();
	UCombatFeedbackLibrary::PlayCombatFeedback(this, CurrentAttackRow.SwingFeedback, Owner, MeleeHitbox,
		Owner ? Owner->GetActorLocation() : FVector::ZeroVector);

	OnAttackStarted.Broadcast(AttackId);
	PrintAction(FString::Printf(TEXT("攻撃 %s（威力%d / スタン%d）"), *AttackId.ToString(), CurrentAttackRow.Power, CurrentAttackRow.StunValue), FColor::Cyan);
}

void UPlayerActionComponent::TickAttack(float Dt)
{
	AttackElapsed += Dt;

	// PG-03: Notify 駆動のときは判定 ON/OFF を ReceiveCombatEvent に任せる（Montage がある行のみ）。
	const bool bNotifyDriven = bUseNotifyHitWindow && CurrentAttackRow.Montage != nullptr;
	if (!bNotifyDriven)
	{
		const bool bShouldHit = AttackElapsed >= CurrentAttackRow.HitActiveStart && AttackElapsed < CurrentAttackRow.HitActiveEnd;
		if (bShouldHit != bMeleeActive)
		{
			bMeleeActive = bShouldHit;
			SetMeleeHitboxActive(bMeleeActive);
		}
	}

	// PG-08: InterruptibleStartFrame 到達後、受付済みの入力があれば即次段。
	if (bComboQueued && CurrentAttackRow.NextComboId != NAME_None && CurrentAttackRow.InterruptibleStartFrame >= 0 && FrameRate > 0.f)
	{
		const float MontageTime = AttackElapsed * (CurrentAttackRow.Montage ? CurrentPlayRate : 1.f);
		if (MontageTime >= CurrentAttackRow.InterruptibleStartFrame / FrameRate)
		{
			const FName Next = CurrentAttackRow.NextComboId;
			EndCurrentAttackStep(/*bStopMontage*/ false); // 次段の Montage_Play がブレンドで差し替える
			StartAttackRow(Next);
			return;
		}
	}

	if (!bNotifyDriven && AttackElapsed >= CurrentAttackRow.EndTime)
	{
		if (bComboQueued && CurrentAttackRow.NextComboId != NAME_None)
		{
			const FName Next = CurrentAttackRow.NextComboId;
			EndCurrentAttackStep(false);
			StartAttackRow(Next); // 派生（ゲージ消費なし）
		}
		else
		{
			FinishAttack();
		}
	}
	else if (bNotifyDriven && AttackElapsed >= CurrentAttackRow.EndTime + 1.f)
	{
		// 安全弁: AttackEnd Notify が無い Montage でも EndTime + 1 秒で必ず終える。
		FinishAttack();
	}
}

void UPlayerActionComponent::EndCurrentAttackStep(bool bStopMontage)
{
	if (!bAttackStepActive)
	{
		return; // 1 回だけ
	}
	bAttackStepActive = false;

	SetMeleeHitboxActive(false);
	bMeleeActive = false;
	bComboQueued = false;
	HitActorsThisSwing.Reset();

	// トレイルは攻撃段の終了で必ず OFF。
	UCombatFeedbackLibrary::SetTrailActive(GetOwner(), CurrentAttackRow.SwingFeedback.TrailComponentTag, false);

	// Montage を止めれば RootMotion も止まる。
	if (bStopMontage && CurrentAttackRow.Montage)
	{
		if (UAnimInstance* Anim = GetAnimInstance())
		{
			if (Anim->Montage_IsPlaying(CurrentAttackRow.Montage))
			{
				Anim->Montage_Stop(AttackBlendOutTime, CurrentAttackRow.Montage);
			}
		}
	}
}

void UPlayerActionComponent::FinishAttack()
{
	EndCurrentAttackStep(true);
	CurrentAttackId = NAME_None;
	ComboCooldownTimer = PostComboCooldown; // 連打での即リスタート痙攣を防ぐ
	SetCurrentAction(EPlayerActionType::None);
	if (Combat && Combat->GetCombatState() == EPlayerCombatState::Attack)
	{
		Combat->SetCombatState(EPlayerCombatState::Normal);
	}
}

void UPlayerActionComponent::CancelAttack()
{
	if (CurrentAction != EPlayerActionType::Attack)
	{
		return;
	}
	PrintAction(TEXT("攻撃中断（相打ち / 割り込み）"), FColor::Yellow);
	EndCurrentAttackStep(true);
	CurrentAttackId = NAME_None;
	SetCurrentAction(EPlayerActionType::None);
	// Combat 状態は呼び出し側（被弾処理）が Hit にしている想定なので触らない。
}

void UPlayerActionComponent::ReceiveCombatEvent_Implementation(FName EventName)
{
	if (CurrentAction != EPlayerActionType::Attack || !bUseNotifyHitWindow || !CurrentAttackRow.Montage)
	{
		return;
	}
	if (EventName == CombatEventNames.HitStart)
	{
		if (!bMeleeActive)
		{
			bMeleeActive = true;
			SetMeleeHitboxActive(true);
		}
	}
	else if (EventName == CombatEventNames.HitEnd)
	{
		if (bMeleeActive)
		{
			bMeleeActive = false;
			SetMeleeHitboxActive(false);
		}
	}
	else if (EventName == CombatEventNames.AttackEnd)
	{
		if (bComboQueued && CurrentAttackRow.NextComboId != NAME_None)
		{
			const FName Next = CurrentAttackRow.NextComboId;
			EndCurrentAttackStep(false);
			StartAttackRow(Next);
		}
		else
		{
			FinishAttack();
		}
	}
}

void UPlayerActionComponent::SyncAttackTableToMontages()
{
	if (!AttackDataTable || FrameRate <= 0.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlayerAction] SyncAttackTableToMontages: AttackDataTable / FrameRate 未設定"));
		return;
	}
	AttackDataTable->Modify();
	int32 Updated = 0;
	for (const TPair<FName, uint8*>& Pair : AttackDataTable->GetRowMap())
	{
		FPlayerAttackRow* Row = reinterpret_cast<FPlayerAttackRow*>(Pair.Value);
		if (!Row || !Row->Montage)
		{
			continue;
		}
		if (Row->PlayRate <= 0.f)
		{
			Row->PlayRate = SyncPlayRate;
		}
		const float Len = Row->Montage->GetPlayLength();
		Row->EndTime = Len / Row->PlayRate;
		const float RawFrame = Len * FrameRate * AutoInterruptibleRatio;
		switch (InterruptibleFrameRounding)
		{
		case EFrameRounding::Ceil:  Row->InterruptibleStartFrame = FMath::CeilToInt(RawFrame); break;
		case EFrameRounding::Round: Row->InterruptibleStartFrame = FMath::RoundToInt(RawFrame); break;
		default:                    Row->InterruptibleStartFrame = FMath::FloorToInt(RawFrame); break;
		}
		++Updated;
	}
	AttackDataTable->MarkPackageDirty();
#if WITH_EDITOR
	AttackDataTable->OnDataTableChanged().Broadcast();
#endif
	UE_LOG(LogTemp, Log, TEXT("[PlayerAction] SyncAttackTableToMontages: %d 行を更新（保存は手動）"), Updated);
}

// --------------------------------------------------------------------------
// 回避
// --------------------------------------------------------------------------

void UPlayerActionComponent::TryDodge()
{
	if (!CanStartAction(EPlayerActionType::Dodge))
	{
		return;
	}

	if (CurrentAction == EPlayerActionType::Attack)
	{
		CancelAttack();
	}
	if (CurrentAction == EPlayerActionType::Heal)
	{
		EndHeal(/*bInterrupted*/ true);
	}
	if (Guard && Guard->IsGuarding())
	{
		Guard->StopGuard();
	}

	// PG-20: 方向決め。BP から SetMoveInput されていればそれを基準で回し、無ければ Pawn の最終移動入力（ワールド）。
	AActor* Owner = GetOwner();
	APawn* Pawn = Cast<APawn>(Owner);
	FVector WorldInput = FVector::ZeroVector;
	if (Owner && LastMoveInput.Size() > DodgeInputDeadZone)
	{
		float Yaw = Owner->GetActorRotation().Yaw;
		if (DodgeInputBasis == EDodgeInputBasis::Camera && Pawn && Pawn->GetController())
		{
			Yaw = Pawn->GetController()->GetControlRotation().Yaw;
		}
		const FRotator Basis(0.f, Yaw, 0.f);
		const FRotationMatrix M(Basis);
		WorldInput = M.GetUnitAxis(EAxis::X) * LastMoveInput.Y + M.GetUnitAxis(EAxis::Y) * LastMoveInput.X;
	}
	else if (Pawn)
	{
		const FVector V = Pawn->GetLastMovementInputVector();
		if (V.Size2D() > DodgeInputDeadZone)
		{
			WorldInput = V;
		}
	}

	const bool bHasInput = !WorldInput.IsNearlyZero();
	FVector Dir = FVector::ZeroVector;
	if (bHasInput)
	{
		Dir = WorldInput.GetSafeNormal2D();
	}
	else if (Owner)
	{
		Dir = DodgeNoInputDirection == EDodgeNoInputDirection::Forward ? Owner->GetActorForwardVector() : -Owner->GetActorForwardVector();
	}
	DodgeDir = Dir.GetSafeNormal2D();
	DodgeElapsed = 0.f;
	bDodgeIFrame = false;

	// 入力方向へ即回転。ロックオン中は設定次第でターゲットを向いたまま。
	if (Owner && bFaceDodgeDirection && bHasInput)
	{
		const UPlayerCameraComponent* Cam = Owner->FindComponentByClass<UPlayerCameraComponent>();
		const bool bKeepTarget = Cam && Cam->IsLockedOn() && Cam->GetLockTarget()
			&& LockOnDodgeFacing == ELockOnDodgeFacing::KeepFacingTarget;
		if (!bKeepTarget)
		{
			Owner->SetActorRotation(FRotator(0.f, DodgeDir.Rotation().Yaw, 0.f));
		}
	}

	SetCurrentAction(EPlayerActionType::Dodge);

	if (DodgeMontage)
	{
		if (UAnimInstance* Anim = GetAnimInstance())
		{
			// 回避モーションを DodgeDuration に収まるよう再生レートを合わせる。
			const float MontageLen = DodgeMontage->GetPlayLength();
			const float Rate = (DodgeDuration > 0.05f && MontageLen > 0.05f)
				? FMath::Clamp(MontageLen / DodgeDuration, 0.2f, 4.f)
				: 1.f;
			Anim->Montage_Play(DodgeMontage, Rate);
		}
	}

	UCombatFeedbackLibrary::PlayCombatFeedback(this, DodgeFeedback, Owner, nullptr,
		Owner ? Owner->GetActorLocation() : FVector::ZeroVector);
	OnDodgeStarted.Broadcast();
	PrintAction(TEXT("回避（ローリング）"), FColor::Green);
}

void UPlayerActionComponent::TickDodge(float Dt)
{
	DodgeElapsed += Dt;

	const bool bIFrame = DodgeElapsed >= DodgeIFrameStart && DodgeElapsed < DodgeIFrameEnd;
	if (bIFrame != bDodgeIFrame)
	{
		bDodgeIFrame = bIFrame;
		if (Combat)
		{
			Combat->SetInvulnerable(bDodgeIFrame);
		}
	}

	if (AActor* Owner = GetOwner())
	{
		const float Speed = DodgeDuration > 0.f ? DodgeDistance / DodgeDuration : 0.f;
		Owner->AddActorWorldOffset(DodgeDir * Speed * Dt, true);
	}

	if (DodgeElapsed >= DodgeDuration)
	{
		if (Combat)
		{
			Combat->SetInvulnerable(false);
		}
		bDodgeIFrame = false;
		// 回避終了時にモーションも明示的に止める（残り再生を持ち越さない）。
		if (DodgeMontage)
		{
			if (UAnimInstance* Anim = GetAnimInstance())
			{
				if (Anim->Montage_IsPlaying(DodgeMontage))
				{
					Anim->Montage_Stop(0.15f, DodgeMontage);
				}
			}
		}
		SetCurrentAction(EPlayerActionType::None);
	}
}

// --------------------------------------------------------------------------
// 回復（PG-05 / PG-22）
// --------------------------------------------------------------------------

void UPlayerActionComponent::TryHeal()
{
	if (CurrentAction == EPlayerActionType::Heal || !CanStartAction(EPlayerActionType::Heal))
	{
		return;
	}
	if (Combat && Combat->Hp >= Combat->MaxHp)
	{
		return; // 満タンでは使わない
	}

	const bool bUsePotion = HealCostMode != EHealCostMode::Gauge;
	const bool bUseGauge = HealCostMode != EHealCostMode::Potion;
	if (bUsePotion && PotionCount <= 0)
	{
		PrintAction(TEXT("回復薬がない"), FColor(255, 140, 0));
		return;
	}
	if (bUseGauge && HealGaugeCost > 0 && Combat && Combat->Gauge < HealGaugeCost)
	{
		return;
	}
	if (bUseGauge && Combat)
	{
		Combat->TryConsumeGauge(HealGaugeCost);
	}
	if (bUsePotion)
	{
		AddPotion(-1);
	}

	HealElapsed = 0.f;
	bHealApplied = false;
	HealTotal = HealDuration;
	if (HealMontage)
	{
		if (UAnimInstance* Anim = GetAnimInstance())
		{
			const float Len = Anim->Montage_Play(HealMontage);
			if (HealTotal <= 0.f)
			{
				HealTotal = Len;
			}
		}
	}

	SetCurrentAction(EPlayerActionType::Heal);
	if (!bAllowMoveWhileHealing)
	{
		SetMoveInputIgnored(true);
	}

	AActor* Owner = GetOwner();
	UCombatFeedbackLibrary::PlayCombatFeedback(this, HealFeedback, Owner, nullptr,
		Owner ? Owner->GetActorLocation() : FVector::ZeroVector);
	PrintAction(FString::Printf(TEXT("回復開始（回復薬 残り %d）"), PotionCount), FColor::Green);

	if (HealTotal <= 0.f)
	{
		TickHeal(0.f); // 尺 0 なら即時完了
	}
}

void UPlayerActionComponent::TickHeal(float Dt)
{
	HealElapsed += Dt;
	if (!bHealApplied && HealElapsed >= FMath::Min(HealApplyTime, HealTotal))
	{
		bHealApplied = true;
		if (Combat)
		{
			Combat->Heal(HealAmount);
		}
	}
	if (HealElapsed >= HealTotal)
	{
		EndHeal(/*bInterrupted*/ false);
	}
}

void UPlayerActionComponent::EndHeal(bool bInterrupted)
{
	if (CurrentAction != EPlayerActionType::Heal)
	{
		return;
	}
	SetMoveInputIgnored(false);
	if (bInterrupted && HealMontage)
	{
		if (UAnimInstance* Anim = GetAnimInstance())
		{
			if (Anim->Montage_IsPlaying(HealMontage))
			{
				Anim->Montage_Stop(0.1f, HealMontage);
			}
		}
	}
	SetCurrentAction(EPlayerActionType::None);
	PrintAction(bInterrupted ? TEXT("回復中断") : TEXT("回復完了"), FColor::Green);
}

// --------------------------------------------------------------------------
// 命中
// --------------------------------------------------------------------------

void UPlayerActionComponent::SetMeleeHitboxActive(bool bActive)
{
	if (!MeleeHitbox)
	{
		return;
	}
	if (bActive)
	{
		HitActorsThisSwing.Reset();
		MeleeHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeleeHitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		MeleeHitbox->SetHiddenInGame(!CounterCoreDebug::IsOnScreenDebugEnabled()); // 判定中のワイヤーフレーム表示
		MeleeHitbox->UpdateOverlaps();
		SweepMeleeOverlaps();
	}
	else
	{
		MeleeHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeleeHitbox->SetHiddenInGame(true);
	}
}

void UPlayerActionComponent::SweepMeleeOverlaps()
{
	if (!MeleeHitbox)
	{
		return;
	}
	TArray<AActor*> Overlapping;
	MeleeHitbox->GetOverlappingActors(Overlapping);
	for (AActor* Other : Overlapping)
	{
		OnMeleeOverlap(MeleeHitbox, Other, nullptr, 0, false, FHitResult());
	}
}

void UPlayerActionComponent::OnMeleeOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!bMeleeActive || !OtherActor || OtherActor == GetOwner())
	{
		return;
	}
	if (HitActorsThisSwing.Contains(OtherActor))
	{
		return;
	}

	UMonsterCombatComponent* EnemyCombat = OtherActor->FindComponentByClass<UMonsterCombatComponent>();
	if (!EnemyCombat)
	{
		// 敵以外に重なった場合もデバッグには出す（判定が動いているかの確認用）。
		PrintAction(FString::Printf(TEXT("近接判定オーバーラップ（非敵）: %s"), *OtherActor->GetName()), FColor::Silver);
		return;
	}
	HitActorsThisSwing.Add(OtherActor);

	// 敵の HandleIncomingHit が「攻撃力 - 防御力」とラッシュ倍率（敵側 bTargetInRush）を処理する。
	const FMonsterDamageResult DmgResult = EnemyCombat->HandleIncomingHit(CurrentAttackRow.Power, /*bGuardedByPlayer*/ false);
	if (CurrentAttackRow.StunValue > 0)
	{
		EnemyCombat->AddStun(CurrentAttackRow.StunValue);
	}

	// PG-19: 命中時のみ。多重発生はサブシステム側で延長のみ。
	if (CurrentAttackRow.HitStop.IsActive())
	{
		UCombatFeedbackLibrary::ApplyHitStop(GetOwner(), CurrentAttackRow.HitStop);
		if (bHitStopAlsoOnTarget)
		{
			UCombatFeedbackLibrary::ApplyHitStop(OtherActor, CurrentAttackRow.HitStop);
		}
	}
	UCombatFeedbackLibrary::PlayCombatFeedback(this, CurrentAttackRow.HitFeedback, GetOwner(), nullptr,
		MeleeHitbox ? MeleeHitbox->GetComponentLocation() : OtherActor->GetActorLocation());
	PlayAttackHitShake();
	PrintAction(FString::Printf(TEXT("命中 %s → %s に %d ダメージ（残HP %d / %d）"),
		*CurrentAttackId.ToString(), *OtherActor->GetName(),
		DmgResult.AppliedDamage, EnemyCombat->Status.Hp, EnemyCombat->Status.MaxHp), FColor::Red);
}

void UPlayerActionComponent::PlayAttackHitShake() const
{
	if (!AttackHitCameraShake || CameraShakeScale <= 0.f)
	{
		return;
	}
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraShake(AttackHitCameraShake, CameraShakeScale);
			}
		}
	}
}

// --------------------------------------------------------------------------

void UPlayerActionComponent::HandleCombatStateChanged(EPlayerCombatState /*OldState*/, EPlayerCombatState NewState)
{
	if (NewState == EPlayerCombatState::Hit)
	{
		// 仕様: 敵と相打ち → プレイヤー側の攻撃を強制中断（大攻撃は「発動後キャンセル不可」）。
		if (CurrentAction == EPlayerActionType::Attack && CurrentAttackRow.bCancelable)
		{
			CancelAttack();
		}
		EndHeal(/*bInterrupted*/ true); // PG-05: 被弾で回復中断
	}
	else if (NewState == EPlayerCombatState::Stun || NewState == EPlayerCombatState::Dead)
	{
		// PG-04 / PG-05: 気絶・死亡で攻撃・回復・回避・ガードをすべて止める。
		if (CurrentAction == EPlayerActionType::Attack)
		{
			CancelAttack();
		}
		EndHeal(true);
		EndCurrentAttackStep(true);
		if (NewState == EPlayerCombatState::Dead)
		{
			bComboQueued = false;
			LastMoveInput = FVector2D::ZeroVector;
			if (DodgeMontage)
			{
				if (UAnimInstance* Anim = GetAnimInstance())
				{
					Anim->Montage_Stop(0.f, DodgeMontage);
				}
			}
		}
		if (CurrentAction == EPlayerActionType::Dodge && Combat)
		{
			Combat->SetInvulnerable(false);
		}
		SetCurrentAction(EPlayerActionType::None);
		if (Guard)
		{
			Guard->StopGuard();
		}
	}
}

void UPlayerActionComponent::SetCurrentAction(EPlayerActionType New)
{
	if (CurrentAction == New)
	{
		return;
	}
	CurrentAction = New;
	OnActionChanged.Broadcast(New);
}

void UPlayerActionComponent::PrintAction(const FString& Msg, const FColor& Color) const
{
	if (!bPrintActionEvents)
	{
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[PlayerAction] %s"), *Msg);
#if !UE_BUILD_SHIPPING
	if (GEngine && CounterCoreDebug::IsOnScreenDebugEnabled())
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, Color, FString::Printf(TEXT("[P] %s"), *Msg));
	}
#endif
}

void UPlayerActionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Combat && Combat->IsDead())
	{
		return; // PG-04: 死亡後は入力・行動・判定すべて停止
	}

	PollFallbackInput();

	if (ComboCooldownTimer > 0.f)
	{
		ComboCooldownTimer = FMath::Max(0.f, ComboCooldownTimer - DeltaTime);
	}

	switch (CurrentAction)
	{
	case EPlayerActionType::Attack: TickAttack(DeltaTime); break;
	case EPlayerActionType::Dodge:  TickDodge(DeltaTime); break;
	case EPlayerActionType::Heal:   TickHeal(DeltaTime); break;
	default: break;
	}

	// 判定アクティブ中は毎フレーム重なりを拾う（歩いて入ってきた敵も確実に当てる）。
	if (bMeleeActive)
	{
		SweepMeleeOverlaps();
	}
}
