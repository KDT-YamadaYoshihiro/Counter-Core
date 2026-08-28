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

		USceneComponent* AttachTo = Mesh ? (USceneComponent*)Mesh : Owner->GetRootComponent();

		// 剣を手に生成（敵と同じ BP_Weapon）。
		if (WeaponClass)
		{
			WeaponActor = NewObject<UChildActorComponent>(Owner, TEXT("PlayerWeapon"));
			WeaponActor->SetupAttachment(AttachTo, WeaponSocket);
			WeaponActor->RegisterComponent();
			WeaponActor->SetChildActorClass(WeaponClass);
			WeaponActor->CreateChildActor();

			if (AActor* W = WeaponActor->GetChildActor())
			{
				if (UShapeComponent* Shape = W->FindComponentByClass<UShapeComponent>())
				{
					MeleeHitbox = Shape;
				}
			}
		}

		// 武器が無ければフォールバックの近接判定ボックスを手に生成。
		if (!MeleeHitbox)
		{
			UBoxComponent* Box = NewObject<UBoxComponent>(Owner, TEXT("PlayerMeleeHitbox"));
			Box->SetupAttachment(AttachTo, WeaponSocket);
			Box->RegisterComponent();
			Box->SetBoxExtent(MeleeHitboxExtent);
			MeleeHitbox = Box;
		}

		if (MeleeHitbox)
		{
			MeleeHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeleeHitbox->SetCollisionObjectType(ECC_WorldDynamic);
			MeleeHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
			MeleeHitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
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

	BindInput();
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

	// 優先度: 移動 < ガード < 攻撃 < 回避。上位は下位を割り込める。
	auto Prio = [](EPlayerActionType A) -> int32
	{
		switch (A)
		{
		case EPlayerActionType::Dodge:  return 4;
		case EPlayerActionType::Attack: return 3;
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
	HitActorsThisSwing.Reset();

	SetCurrentAction(EPlayerActionType::Attack);
	if (Combat)
	{
		Combat->SetCombatState(EPlayerCombatState::Attack);
	}

	if (CurrentAttackRow.Montage)
	{
		if (USkeletalMeshComponent* Mesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr)
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				Anim->Montage_Play(CurrentAttackRow.Montage);
			}
		}
	}

	OnAttackStarted.Broadcast(AttackId);
	PrintAction(FString::Printf(TEXT("攻撃 %s（威力%d / スタン%d）"), *AttackId.ToString(), CurrentAttackRow.Power, CurrentAttackRow.StunValue), FColor::Cyan);
}

void UPlayerActionComponent::TickAttack(float Dt)
{
	AttackElapsed += Dt;

	const bool bShouldHit = AttackElapsed >= CurrentAttackRow.HitActiveStart && AttackElapsed < CurrentAttackRow.HitActiveEnd;
	if (bShouldHit != bMeleeActive)
	{
		bMeleeActive = bShouldHit;
		SetMeleeHitboxActive(bMeleeActive);
		if (bMeleeActive)
		{
			HitActorsThisSwing.Reset();
		}
	}

	if (AttackElapsed >= CurrentAttackRow.EndTime)
	{
		if (bComboQueued && CurrentAttackRow.NextComboId != NAME_None)
		{
			StartAttackRow(CurrentAttackRow.NextComboId); // 派生（ゲージ消費なし）
		}
		else
		{
			FinishAttack();
		}
	}
}

void UPlayerActionComponent::FinishAttack()
{
	SetMeleeHitboxActive(false);
	bMeleeActive = false;
	bComboQueued = false;
	CurrentAttackId = NAME_None;
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
	SetMeleeHitboxActive(false);
	bMeleeActive = false;
	bComboQueued = false;
	CurrentAttackId = NAME_None;
	SetCurrentAction(EPlayerActionType::None);
	// Combat 状態は呼び出し側（被弾処理）が Hit にしている想定なので触らない。
	if (USkeletalMeshComponent* Mesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr)
	{
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			Anim->Montage_Stop(0.1f);
		}
	}
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
	if (Guard && Guard->IsGuarding())
	{
		Guard->StopGuard();
	}

	// 方向: 移動入力があればその方向、無ければ後方。
	const AActor* Owner = GetOwner();
	FVector Dir = Owner ? -Owner->GetActorForwardVector() : FVector::ZeroVector;
	if (Owner && !LastMoveInput.IsNearlyZero())
	{
		const FVector Fwd = Owner->GetActorForwardVector();
		const FVector Right = Owner->GetActorRightVector();
		Dir = (Fwd * LastMoveInput.Y + Right * LastMoveInput.X).GetSafeNormal();
	}
	DodgeDir = Dir.GetSafeNormal2D();
	DodgeElapsed = 0.f;
	bDodgeIFrame = false;

	SetCurrentAction(EPlayerActionType::Dodge);

	if (DodgeMontage)
	{
		if (USkeletalMeshComponent* Mesh = Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr)
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				Anim->Montage_Play(DodgeMontage);
			}
		}
	}

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
		SetCurrentAction(EPlayerActionType::None);
	}
}

// --------------------------------------------------------------------------

void UPlayerActionComponent::TryHeal()
{
	if (!CanStartAction(EPlayerActionType::Attack)) // 攻撃と同格の割り込み条件
	{
		return;
	}
	if (Combat && HealGaugeCost > 0 && !Combat->TryConsumeGauge(HealGaugeCost))
	{
		return;
	}
	if (Combat)
	{
		Combat->Heal(HealAmount);
	}
	if (HealMontage)
	{
		if (USkeletalMeshComponent* Mesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr)
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				Anim->Montage_Play(HealMontage);
			}
		}
	}
	PrintAction(TEXT("回復"), FColor::Green);
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
		MeleeHitbox->SetHiddenInGame(false);
		TArray<AActor*> Overlapping;
		MeleeHitbox->GetOverlappingActors(Overlapping, APawn::StaticClass());
		for (AActor* Other : Overlapping)
		{
			OnMeleeOverlap(MeleeHitbox, Other, nullptr, 0, false, FHitResult());
		}
	}
	else
	{
		MeleeHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeleeHitbox->SetHiddenInGame(true);
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
		return;
	}
	HitActorsThisSwing.Add(OtherActor);

	// 敵の HandleIncomingHit が「攻撃力 - 防御力」とラッシュ倍率（敵側 bTargetInRush）を処理する。
	EnemyCombat->HandleIncomingHit(CurrentAttackRow.Power, /*bGuardedByPlayer*/ false);
	if (CurrentAttackRow.StunValue > 0)
	{
		EnemyCombat->AddStun(CurrentAttackRow.StunValue);
	}

	if (CurrentAttackRow.bHitStop)
	{
		ApplyHitStop(HitStopDuration);
	}
	PlayAttackHitShake();
	PrintAction(FString::Printf(TEXT("命中 %s → 威力%d / スタン+%d"), *CurrentAttackId.ToString(), CurrentAttackRow.Power, CurrentAttackRow.StunValue), FColor::Red);
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

void UPlayerActionComponent::ApplyHitStop(float Duration)
{
	AActor* Owner = GetOwner();
	if (!Owner || Duration <= 0.f)
	{
		return;
	}
	Owner->CustomTimeDilation = 0.02f;
	if (USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>())
	{
		Mesh->GlobalAnimRateScale = 0.02f;
	}
	TWeakObjectPtr<AActor> WeakOwner(Owner);
	FTimerDelegate Del;
	Del.BindLambda([WeakOwner]()
	{
		if (WeakOwner.IsValid())
		{
			WeakOwner->CustomTimeDilation = 1.f;
			if (USkeletalMeshComponent* M = WeakOwner->FindComponentByClass<USkeletalMeshComponent>())
			{
				M->GlobalAnimRateScale = 1.f;
			}
		}
	});
	Owner->GetWorldTimerManager().SetTimer(HitStopTimerHandle, Del, Duration, false);
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
	}
	else if (NewState == EPlayerCombatState::Stun)
	{
		if (CurrentAction == EPlayerActionType::Attack)
		{
			CancelAttack();
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
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, Color, FString::Printf(TEXT("[P] %s"), *Msg));
	}
#endif
}

void UPlayerActionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	PollFallbackInput();

	switch (CurrentAction)
	{
	case EPlayerActionType::Attack: TickAttack(DeltaTime); break;
	case EPlayerActionType::Dodge:  TickDodge(DeltaTime); break;
	default: break;
	}
}
