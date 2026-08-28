#include "Player/PlayerGuardComponent.h"
#include "Player/PlayerCombatComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

UPlayerGuardComponent::UPlayerGuardComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPlayerGuardComponent::BeginPlay()
{
	Super::BeginPlay();
	ShieldDurability = MaxShieldDurability;
	GuardTimeRemaining = MaxGuardTime;
	OnShieldChanged.Broadcast(ShieldDurability, MaxShieldDurability);
	OnGuardTimeChanged.Broadcast(GuardTimeRemaining, MaxGuardTime);
}

UPlayerCombatComponent* UPlayerGuardComponent::GetCombat() const
{
	if (CachedCombat.IsValid())
	{
		return CachedCombat.Get();
	}
	if (const AActor* Owner = GetOwner())
	{
		UPlayerGuardComponent* Self = const_cast<UPlayerGuardComponent*>(this);
		Self->CachedCombat = Owner->FindComponentByClass<UPlayerCombatComponent>();
		return Self->CachedCombat.Get();
	}
	return nullptr;
}

bool UPlayerGuardComponent::CanStartGuard() const
{
	if (CooldownTimer > 0.f || ShieldDurability <= 0.f || GuardTimeRemaining <= 0.f)
	{
		return false;
	}
	if (const UPlayerCombatComponent* Combat = GetCombat())
	{
		if (!Combat->IsAlive() || Combat->GetCombatState() == EPlayerCombatState::Stun)
		{
			return false;
		}
	}
	return true;
}

void UPlayerGuardComponent::StartGuard()
{
	bGuardHeld = true;
	if (!bGuarding && CanStartGuard())
	{
		SetGuarding(true);
	}
}

void UPlayerGuardComponent::StopGuard()
{
	bGuardHeld = false;
	if (bGuarding)
	{
		SetGuarding(false);
	}
}

void UPlayerGuardComponent::SetGuarding(bool bNewGuarding)
{
	if (bGuarding == bNewGuarding)
	{
		return;
	}
	bGuarding = bNewGuarding;

	// 仕様: 移動しながらのガード不可。ガード中は MaxWalkSpeed を 0 に。
	if (bLockMovementWhileGuarding)
	{
		if (const ACharacter* Char = Cast<ACharacter>(GetOwner()))
		{
			if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
			{
				if (bGuarding)
				{
					if (SavedMaxWalkSpeed < 0.f)
					{
						SavedMaxWalkSpeed = Move->MaxWalkSpeed;
					}
					Move->MaxWalkSpeed = 0.f;
					Move->StopMovementImmediately();
				}
				else if (SavedMaxWalkSpeed >= 0.f)
				{
					Move->MaxWalkSpeed = SavedMaxWalkSpeed;
					SavedMaxWalkSpeed = -1.f;
				}
			}
		}
	}

	OnGuardStateChanged.Broadcast(bGuarding);
}

void UPlayerGuardComponent::ForceReleaseWithCooldown()
{
	SetGuarding(false);
	CooldownTimer = GuardCooldown; // 仕様: ガード可能時間を使い切った場合のみクールタイム
}

void UPlayerGuardComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CooldownTimer > 0.f)
	{
		CooldownTimer = FMath::Max(0.f, CooldownTimer - DeltaTime);
	}

	if (bGuarding)
	{
		// ガード可能時間を消費。使い切ったら強制解除＋クールタイム。
		GuardTimeRemaining = FMath::Max(0.f, GuardTimeRemaining - DeltaTime);
		OnGuardTimeChanged.Broadcast(GuardTimeRemaining, MaxGuardTime);
		if (GuardTimeRemaining <= 0.f)
		{
			ForceReleaseWithCooldown();
		}
	}
	else
	{
		// 非ガード時: ガード可能時間が回復。
		if (GuardTimeRemaining < MaxGuardTime)
		{
			GuardTimeRemaining = FMath::Min(MaxGuardTime, GuardTimeRemaining + GuardTimeRegenPerSec * DeltaTime);
			OnGuardTimeChanged.Broadcast(GuardTimeRemaining, MaxGuardTime);
		}
		// 入力が押されたままでクールタイムが明けたら再ガード。
		if (bGuardHeld && CanStartGuard())
		{
			SetGuarding(true);
		}
	}

	// 盾耐久の自然回復（仕様: ガード成功1秒後から）。
	if (ShieldRegenDelayTimer > 0.f)
	{
		ShieldRegenDelayTimer = FMath::Max(0.f, ShieldRegenDelayTimer - DeltaTime);
	}
	else if (ShieldDurability < MaxShieldDurability)
	{
		const float Before = ShieldDurability;
		ShieldDurability = FMath::Min(MaxShieldDurability, ShieldDurability + ShieldRegenPerSec * DeltaTime);
		if (!FMath::IsNearlyEqual(Before, ShieldDurability))
		{
			OnShieldChanged.Broadcast(ShieldDurability, MaxShieldDurability);
		}
	}
}

bool UPlayerGuardComponent::HandleGuardedHit(int32 EnemyAttackPower, int32 ShieldChipValue, bool bJustGuard)
{
	if (!bGuarding)
	{
		return false;
	}

	UPlayerCombatComponent* Combat = GetCombat();

	// 仕様 Battle 分岐A: HP ダメージ 0 / 盾耐久 -= 盾削り値 / ゲージ変換 / ヒットストップ。
	const int32 Chip = ShieldChipValue > 0 ? ShieldChipValue : EnemyAttackPower;
	ShieldDurability = FMath::Max(0.f, ShieldDurability - Chip);
	OnShieldChanged.Broadcast(ShieldDurability, MaxShieldDurability);

	if (Combat)
	{
		Combat->TakeIncomingHit(EnemyAttackPower, /*bGuarded*/ true);
		// ジャストガードはゲージ上昇値が増える（仕様「ジャストガード」）。倍率は将来の調整用。
		Combat->AddGaugeFromGuardedDamage(EnemyAttackPower, bJustGuard ? 2.f : 1.f);
	}

	// 盾耐久の自然回復を一旦止める（成功1秒後から再開）。
	ShieldRegenDelayTimer = ShieldRegenDelay;

	ApplyGuardHitStop();
	OnGuardSuccess.Broadcast();

	// 仕様 Player「気絶」: 盾耐久 0 → 10 秒行動不能。
	if (ShieldDurability <= 0.f)
	{
		SetGuarding(false);
		bGuardHeld = false;
		CooldownTimer = FMath::Max(CooldownTimer, GuardCooldown);
		OnGuardBroken.Broadcast();
		if (Combat)
		{
			Combat->BeginStun(StunDuration);
		}
	}
	return true;
}

void UPlayerGuardComponent::ApplyGuardHitStop()
{
	if (GuardHitStopDuration <= 0.f)
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	Owner->CustomTimeDilation = 0.02f;
	FTimerDelegate Del;
	TWeakObjectPtr<AActor> WeakOwner(Owner);
	Del.BindLambda([WeakOwner]()
	{
		if (WeakOwner.IsValid())
		{
			WeakOwner->CustomTimeDilation = 1.f;
		}
	});
	Owner->GetWorldTimerManager().SetTimer(HitStopTimerHandle, Del, GuardHitStopDuration, false);
}
