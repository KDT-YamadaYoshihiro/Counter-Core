#include "Player/PlayerCombatComponent.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Common/CombatFeedbackLibrary.h"

UPlayerCombatComponent::UPlayerCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPlayerCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Hp = MaxHp;
	Gauge = FMath::Clamp(InitialGauge, 0, MaxGauge);
	State = EPlayerCombatState::Normal;
	OnHpChanged.Broadcast(Hp, MaxHp);
	OnGaugeChanged.Broadcast(Gauge, MaxGauge);
}

void UPlayerCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (InvulnTimer > 0.f)
	{
		InvulnTimer = FMath::Max(0.f, InvulnTimer - DeltaTime);
	}
	if (HitReactTimer > 0.f)
	{
		HitReactTimer -= DeltaTime;
		if (HitReactTimer <= 0.f)
		{
			EndHitReact();
		}
	}
	if (StunTimer > 0.f)
	{
		StunTimer -= DeltaTime;
		if (StunTimer <= 0.f)
		{
			EndStun();
		}
	}

	// 仕様 UI:「ゲージは一定時間ごとに1枠獲得する」。
	if (PassiveGaugeInterval > 0.f && Gauge < MaxGauge && IsAlive() && State != EPlayerCombatState::Stun)
	{
		PassiveGaugeTimer += DeltaTime;
		if (PassiveGaugeTimer >= PassiveGaugeInterval)
		{
			PassiveGaugeTimer = 0.f;
			AddGauge(1);
		}
	}
}

int32 UPlayerCombatComponent::CalculateIncomingDamage(int32 AttackPower) const
{
	return FMath::Max(0, AttackPower - Defence);
}

FPlayerDamageResult UPlayerCombatComponent::TakeIncomingHit(int32 AttackPower, bool bGuarded)
{
	FPlayerDamageResult Result;
	Result.bGuarded = bGuarded;

	if (!IsAlive() || State == EPlayerCombatState::Stun)
	{
		Result.bNullified = true;
		return Result;
	}

	// 無敵（回避中 / 被弾直後）は完全無効。ガード成立時は無敵でも盾処理を通すため別扱い。
	if (!bGuarded && IsInvulnerable())
	{
		Result.bNullified = true;
		OnDamaged.Broadcast(Result);
		return Result;
	}

	if (bGuarded)
	{
		// HP ダメージは 0。盾耐久の減算・ゲージ変換は UPlayerGuardComponent が行う。
		OnDamaged.Broadcast(Result);
		return Result;
	}

	const int32 Dmg = CalculateIncomingDamage(AttackPower);
	Hp = FMath::Max(0, Hp - Dmg);
	Result.AppliedDamage = Dmg;
	Result.bWasLethal = Hp <= 0;
	OnHpChanged.Broadcast(Hp, MaxHp);
	PlayDamagedShake();
	UCombatFeedbackLibrary::PlayCombatFeedback(this, DamagedFeedback, GetOwner(), nullptr,
		GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);

	if (Result.bWasLethal)
	{
		// PG-04: HP0 → Dead。被弾モーションは出さず死亡 Montage を 1 回だけ。
		OnDamaged.Broadcast(Result);
		EnterDeath();
		return Result;
	}

	// 仕様: 被弾モーション + 被ダメージ後の無敵時間。
	InvulnTimer = FMath::Max(InvulnTimer, PostHitInvulnTime);
	HitReactTimer = HitReactTime;
	SetCombatState(EPlayerCombatState::Hit);
	PlayMontage(HitReactMontage);

	OnDamaged.Broadcast(Result);
	return Result;
}

void UPlayerCombatComponent::EnterDeath()
{
	if (State == EPlayerCombatState::Dead)
	{
		return;
	}
	Hp = 0;
	HitReactTimer = 0.f;
	StunTimer = 0.f;
	InvulnTimer = 0.f;
	bInvulnerable = false;
	SetCombatState(EPlayerCombatState::Dead);

	if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
	{
		if (bDisableMovementOnDeath)
		{
			if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
				Move->DisableMovement();
			}
		}
		if (!DeathCollisionProfile.IsNone() && Char->GetCapsuleComponent())
		{
			Char->GetCapsuleComponent()->SetCollisionProfileName(DeathCollisionProfile);
		}
	}

	// 他の Montage（攻撃・被弾）を止めてから死亡 Montage を 1 回。
	if (USkeletalMeshComponent* Mesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr)
	{
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			Anim->StopAllMontages(0.1f);
		}
	}
	PlayMontage(DeathMontage);
	UCombatFeedbackLibrary::PlayCombatFeedback(this, DeathFeedback, GetOwner(), nullptr,
		GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);

	OnDied.Broadcast(); // 敗北判定・リザルト遷移は BattleDirector（ResultTransitionDelay）
}

void UPlayerCombatComponent::SetCombatState(EPlayerCombatState NewState)
{
	if (State == NewState || State == EPlayerCombatState::Dead)
	{
		return; // 死亡は終端状態
	}
	// 気絶中は本人の解除（と死亡）以外の遷移を受け付けない。
	if (State == EPlayerCombatState::Stun && NewState != EPlayerCombatState::Normal && NewState != EPlayerCombatState::Dead)
	{
		return;
	}
	const EPlayerCombatState Old = State;
	State = NewState;
	OnStateChanged.Broadcast(Old, NewState);
}

void UPlayerCombatComponent::EndHitReact()
{
	if (State == EPlayerCombatState::Hit)
	{
		SetCombatState(EPlayerCombatState::Normal);
	}
}

void UPlayerCombatComponent::BeginStun(float Duration)
{
	if (!IsAlive())
	{
		return;
	}
	StunTimer = FMath::Max(0.01f, Duration);
	SetCombatState(EPlayerCombatState::Stun);
	PlayMontage(StunMontage);
	OnStunned.Broadcast();
}

void UPlayerCombatComponent::PlayMontage(UAnimMontage* Montage) const
{
	if (!Montage || !GetOwner())
	{
		return;
	}
	if (USkeletalMeshComponent* Mesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			Anim->Montage_Play(Montage);
		}
	}
}

void UPlayerCombatComponent::PlayDamagedShake() const
{
	if (!DamagedCameraShake || CameraShakeScale <= 0.f)
	{
		return;
	}
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraShake(DamagedCameraShake, CameraShakeScale);
			}
		}
	}
}

void UPlayerCombatComponent::EndStun()
{
	StunTimer = 0.f;
	if (State == EPlayerCombatState::Stun)
	{
		State = EPlayerCombatState::Normal;
		OnStateChanged.Broadcast(EPlayerCombatState::Stun, EPlayerCombatState::Normal);
	}
	OnStunRecovered.Broadcast();
}

void UPlayerCombatComponent::Heal(int32 Amount)
{
	if (Amount <= 0 || !IsAlive())
	{
		return;
	}
	Hp = FMath::Min(MaxHp, Hp + Amount);
	OnHpChanged.Broadcast(Hp, MaxHp);
}

void UPlayerCombatComponent::SetGauge(int32 NewGauge)
{
	const int32 Clamped = FMath::Clamp(NewGauge, 0, MaxGauge);
	if (Clamped != Gauge)
	{
		Gauge = Clamped;
		OnGaugeChanged.Broadcast(Gauge, MaxGauge);
	}
}

void UPlayerCombatComponent::AddGauge(int32 Frames)
{
	SetGauge(Gauge + Frames);
}

void UPlayerCombatComponent::AddGaugeFromGuardedDamage(int32 BlockedDamage, float Multiplier)
{
	if (BlockedDamage <= 0 || GuardDamagePerGauge <= 0)
	{
		return;
	}
	const int32 Frames = FMath::Max(1, FMath::RoundToInt((BlockedDamage * Multiplier) / GuardDamagePerGauge));
	AddGauge(Frames);
}

bool UPlayerCombatComponent::TryConsumeGauge(int32 Cost)
{
	if (Cost <= 0)
	{
		return true;
	}
	if (Gauge < Cost)
	{
		return false;
	}
	SetGauge(Gauge - Cost);
	return true;
}

void UPlayerCombatComponent::ForceGaugeMax()
{
	SetGauge(MaxGauge);
}

void UPlayerCombatComponent::SetRushActive(bool bActive)
{
	if (bRushActive == bActive)
	{
		return;
	}
	bRushActive = bActive;
	if (bActive)
	{
		// 仕様 Battle: ラッシュ突入時、攻撃ゲージを強制 MAX。
		ForceGaugeMax();
	}
}
