#include "Enemy/MonsterCombatComponent.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

UMonsterCombatComponent::UMonsterCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMonsterCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Status.Hp = Status.MaxHp;
	Status.Stun = 0;
}

int32 UMonsterCombatComponent::CalculateIncomingDamage(int32 AttackPower) const
{
	const int32 Base = FMath::Max(0, AttackPower - Status.Defence);
	const float Scaled = bTargetInRush ? Base * RushDamageMultiplier : static_cast<float>(Base);
	return FMath::Max(0, FMath::RoundToInt(Scaled));
}

float UMonsterCombatComponent::GetStunNormalized() const
{
	return Status.MaxStun > 0 ? FMath::Clamp(static_cast<float>(Status.Stun) / Status.MaxStun, 0.f, 1.f) : 0.f;
}

FMonsterDamageResult UMonsterCombatComponent::HandleIncomingHit(int32 AttackPower, bool bGuardedByPlayer)
{
	FMonsterDamageResult Result;
	Result.bGuarded = bGuardedByPlayer;

	if (!IsAlive())
	{
		return Result;
	}

	if (bGuardedByPlayer)
	{
		// 仕様書「やられ」: HP ダメージ無し。スタン値 +10、後方ノックバック、
		// やられ State へ（実際のノックバック移動・アニメは BP 側）。
		AddStun(GuardStaggerStunGain);
		OnDamaged.Broadcast(Result);

		if (IsAlive() && !IsStunThresholdReached())
		{
			OnStateChangeRequested.Broadcast(EMonsterState::Hitstun);
		}
		return Result;
	}

	const int32 Dmg = CalculateIncomingDamage(AttackPower);
	Status.Hp = FMath::Max(0, Status.Hp - Dmg);
	Result.AppliedDamage = Dmg;

	if (HitStunGain > 0)
	{
		AddStun(HitStunGain); // AddStun 内で閾値到達なら Stun 要求
	}

	OnDamaged.Broadcast(Result);

	if (Status.Hp <= 0)
	{
		Result.bWasLethal = true;
		OnDied.Broadcast();
		OnStateChangeRequested.Broadcast(EMonsterState::Dead);
	}
	else if (IsStunThresholdReached())
	{
		Result.bReachedStun = true;
		OnStateChangeRequested.Broadcast(EMonsterState::Stun);
	}

	return Result;
}

void UMonsterCombatComponent::AddStun(int32 Amount)
{
	if (Amount == 0 || !IsAlive())
	{
		return;
	}

	const bool bWasReached = IsStunThresholdReached();
	Status.Stun = FMath::Clamp(Status.Stun + Amount, 0, Status.MaxStun);

	if (!bWasReached && IsStunThresholdReached())
	{
		OnStateChangeRequested.Broadcast(EMonsterState::Stun);
	}
}

void UMonsterCombatComponent::BeginStun()
{
	OnStunned.Broadcast();

	if (StunDuration > 0.f)
	{
		if (const UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(StunTimerHandle, this, &UMonsterCombatComponent::EndStun,
				StunDuration, false);
		}
	}
}

void UMonsterCombatComponent::EndStun()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StunTimerHandle);
	}

	Status.Stun = 0;
	OnStunRecovered.Broadcast();

	if (IsAlive())
	{
		OnStateChangeRequested.Broadcast(EMonsterState::Idle);
	}
}

void UMonsterCombatComponent::ResetStatus()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StunTimerHandle);
	}
	Status.Hp = Status.MaxHp;
	Status.Stun = 0;
}
