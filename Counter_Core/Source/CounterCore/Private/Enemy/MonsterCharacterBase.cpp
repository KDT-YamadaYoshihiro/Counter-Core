#include "Enemy/MonsterCharacterBase.h"
#include "Enemy/MonsterCombatComponent.h"
#include "Enemy/MonsterAttackComponent.h"
#include "Components/BoxComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Engine/DamageEvents.h"

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
			WeaponActor->AttachToComponent(GetMesh(),
				FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponSocket);
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

	if (Combat)
	{
		Combat->OnStateChangeRequested.AddDynamic(this, &AMonsterCharacterBase::HandleCombatStateRequest);
	}
	if (Attack)
	{
		Attack->OnAttackFinished.AddDynamic(this, &AMonsterCharacterBase::HandleAttackFinished);
		Attack->OnToggleHitbox.AddDynamic(this, &AMonsterCharacterBase::HandleToggleHitbox);
		Attack->OnPlayAttackAnim.AddDynamic(this, &AMonsterCharacterBase::HandlePlayAttackAnim);
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
			WeaponActor->AttachToComponent(GetMesh(),
				FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponSocket);
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
	}

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = ChaseSpeed;
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
	if (Old == NewState && Old != EMonsterState::Idle)
	{
		return;
	}

	// 攻撃を抜けるときはタイムライン中断。
	if (Old == EMonsterState::Attack && NewState != EMonsterState::Attack && Attack && Attack->IsAttacking())
	{
		Attack->CancelAttack();
	}

	State = NewState;

	switch (NewState)
	{
	case EMonsterState::Hitstun:
	{
		HitstunTimer = Combat ? Combat->HitstunDuration : 0.4f;
		if (Combat)
		{
			Combat->AddStun(Combat->GuardStaggerStunGain);
		}
		if (TargetActor)
		{
			HitstunKnockbackDir = (GetActorLocation() - TargetActor->GetActorLocation()).GetSafeNormal2D();
		}
		break;
	}
	case EMonsterState::Stun:
		if (Combat)
		{
			Combat->BeginStun();
		}
		break;
	case EMonsterState::Dead:
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
		break;
	case EMonsterState::Attack:
		AdvanceCombo();
		break;
	default:
		break;
	}

	PlayReaction(NewState);
	OnStateChanged.Broadcast(Old, NewState);
}

void AMonsterCharacterBase::AdvanceCombo()
{
	if (!Attack)
	{
		RequestState(EMonsterState::Idle);
		return;
	}

	// 新規コンボの開始か、進行中コンボの次の一手か。
	if (CurrentComboAttacks.Num() == 0 || ComboIndex >= CurrentComboAttacks.Num())
	{
		FName ComboId = NAME_None;
		if (ActionLoop.Num() > 0)
		{
			ComboId = ActionLoop[ActionLoopIndex % ActionLoop.Num()];
			++ActionLoopIndex;
			// 行動ループの条件付き項目（発生確率など）は SelectCombo にも通す。
			const FName Rolled = Attack->SelectCombo(GetDistanceToTargetCm() / 100.f, GetSignedAngleToTargetDeg());
			if (Rolled == NAME_None)
			{
				// 条件未達 → この項目はスキップして通常の移動へ。
				CurrentComboAttacks.Reset();
				ComboIndex = 0;
				RequestState(EMonsterState::Run);
				return;
			}
			ComboId = Rolled;
		}
		else
		{
			ComboId = Attack->SelectCombo(GetDistanceToTargetCm() / 100.f, GetSignedAngleToTargetDeg());
		}

		if (ComboId == NAME_None)
		{
			CurrentComboAttacks.Reset();
			ComboIndex = 0;
			RequestState(EMonsterState::Run);
			return;
		}

		CurrentComboAttacks = Attack->GetComboAttacks(ComboId);
		ComboIndex = 0;
	}

	if (CurrentComboAttacks.IsValidIndex(ComboIndex))
	{
		Attack->StartAttack(CurrentComboAttacks[ComboIndex]);
		++ComboIndex;
	}
	else
	{
		RequestState(EMonsterState::Idle);
	}
}

void AMonsterCharacterBase::HandleAttackFinished()
{
	if (State != EMonsterState::Attack)
	{
		return;
	}
	// コンボ継続 or 終了。仕様: コンボ内の以降の攻撃は条件無視で続行。
	if (ComboIndex < CurrentComboAttacks.Num())
	{
		Attack->StartAttack(CurrentComboAttacks[ComboIndex]);
		++ComboIndex;
	}
	else
	{
		CurrentComboAttacks.Reset();
		ComboIndex = 0;
		RequestState(EMonsterState::Run); // 仕様: コンボ後は移動を挟んで次へ
	}
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
		HitActorsThisSwing.Reset();
		ActiveHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ActiveHitbox->SetHiddenInGame(false); // 判定中はワイヤーフレーム表示
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
		ActiveHitbox->SetHiddenInGame(true);
		HitActorsThisSwing.Reset();
	}
}

void AMonsterCharacterBase::HandlePlayAttackAnim(FName AttackId)
{
	PlayAttackMontage(AttackId);
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

	const int32 Power = CurrentAttackPower();
	if (Power > 0)
	{
		UGameplayStatics::ApplyDamage(OtherActor, static_cast<float>(Power), GetController(), this,
			UDamageType::StaticClass());
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
	if (TObjectPtr<UAnimMontage>* Found = AttackMontages.Find(AttackId))
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

void AMonsterCharacterBase::PlayReaction_Implementation(EMonsterState NewState)
{
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
	}
	return Actual;
}

void AMonsterCharacterBase::Tick(float Dt)
{
	Super::Tick(Dt);

	switch (State)
	{
	case EMonsterState::Idle:    TickIdle(Dt); break;
	case EMonsterState::Run:     TickRun(Dt); break;
	case EMonsterState::Attack:  TickAttack(Dt); break;
	case EMonsterState::Hitstun: TickHitstun(Dt); break;
	default: break;
	}
}

void AMonsterCharacterBase::TickIdle(float)
{
	if (TargetActor && GetDistanceToTargetCm() <= DetectionRange)
	{
		RequestState(EMonsterState::Run);
	}
}

void AMonsterCharacterBase::TickRun(float Dt)
{
	if (!TargetActor)
	{
		RequestState(EMonsterState::Idle);
		return;
	}

	const float Dist = GetDistanceToTargetCm();

	// 向きをターゲットへ。
	const FRotator Look = UKismetMathLibrary::FindLookAtRotation(GetActorLocation(), TargetActor->GetActorLocation());
	const FRotator Cur = GetActorRotation();
	SetActorRotation(FRotator(Cur.Pitch, FMath::FInterpTo(Cur.Yaw, Look.Yaw, Dt, 8.f), Cur.Roll));

	if (Dist <= EngageRange)
	{
		RequestState(EMonsterState::Attack);
		return;
	}

	// 前進（CharacterMovement 前提）。
	AddMovementInput(GetActorForwardVector(), 1.f);
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
	// 仕様: 後方 0.2M ノックバック → [0.4s] 硬直終了 → 次の攻撃処理へ。
	if (!HitstunKnockbackDir.IsNearlyZero())
	{
		const float SpeedCmPerSec = (Combat ? Combat->HitstunKnockbackM : 0.2f) * 100.f /
			FMath::Max(0.01f, (Combat ? Combat->HitstunDuration : 0.4f));
		AddActorWorldOffset(HitstunKnockbackDir * SpeedCmPerSec * Dt, true);
	}

	HitstunTimer -= Dt;
	if (HitstunTimer <= 0.f)
	{
		HitstunKnockbackDir = FVector::ZeroVector;
		RequestState(EMonsterState::Attack); // 仕様: 硬直終了 → 次の攻撃処理へ
	}
}
