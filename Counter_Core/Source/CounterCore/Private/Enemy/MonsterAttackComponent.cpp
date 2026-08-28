#include "Enemy/MonsterAttackComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"

UMonsterAttackComponent::UMonsterAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

FMonsterAttackFrameData UMonsterAttackComponent::GetAttackData(FName AttackId, bool& bFound) const
{
	bFound = false;
	if (AttackDataTable)
	{
		if (const FMonsterAttackFrameData* Row =
			AttackDataTable->FindRow<FMonsterAttackFrameData>(AttackId, TEXT("GetAttackData"), false))
		{
			bFound = true;
			return *Row;
		}
	}
	return FMonsterAttackFrameData();
}

TArray<FName> UMonsterAttackComponent::GetComboAttacks(FName ComboId) const
{
	if (ComboDataTable)
	{
		if (const FMonsterComboData* Row =
			ComboDataTable->FindRow<FMonsterComboData>(ComboId, TEXT("GetComboAttacks"), false))
		{
			return Row->AttackSequence;
		}
	}
	return {};
}

FName UMonsterAttackComponent::SelectCombo(float DistanceToTargetM, float SignedAngleToTargetDeg) const
{
	if (!ComboDataTable)
	{
		return NAME_None;
	}

	const float AbsAngle = FMath::Abs(SignedAngleToTargetDeg);

	TArray<FMonsterComboData*> Rows;
	ComboDataTable->GetAllRows<FMonsterComboData>(TEXT("SelectCombo"), Rows);
	for (const FMonsterComboData* Row : Rows)
	{
		if (!Row)
		{
			continue;
		}

		const bool bDistanceOk = Row->MaxDistanceM <= 0.f || DistanceToTargetM < Row->MaxDistanceM;
		const bool bBehind = AbsAngle > 100.f; // 仕様: ±100度外 = 背後
		const bool bAngleOk = Row->bRequireTargetBehind ? bBehind : (AbsAngle <= Row->MaxAngleDeg);

		if (!bDistanceOk || !bAngleOk)
		{
			continue;
		}

		// 条件達成 → 発生確率ロール。成功で採用、失敗なら「次の攻撃へ遷移」＝次コンボ評価。
		const float Roll = FMath::FRandRange(0.f, 100.f);
		if (Roll <= Row->TriggerChancePercent)
		{
			return Row->ComboId;
		}
	}
	return NAME_None;
}

bool UMonsterAttackComponent::IsHitstunAllowed() const
{
	if (CurrentPhase == EMonsterAttackPhase::None || CurrentPhase == EMonsterAttackPhase::Finished)
	{
		return true;
	}
	// 攻撃5の1段目など「やられ判定無効」区間。
	if (ActiveData.bImmuneToHitstun && CurrentPhase == EMonsterAttackPhase::Anticipation)
	{
		return false;
	}
	return true;
}

void UMonsterAttackComponent::StartAttack(FName AttackId)
{
	bool bFound = false;
	ActiveData = GetAttackData(AttackId, bFound);
	if (!bFound)
	{
		UE_LOG(LogTemp, Warning, TEXT("MonsterAttackComponent: unknown attack '%s'"), *AttackId.ToString());
		OnAttackFinished.Broadcast();
		return;
	}

	ElapsedTime = 0.f;
	bHitboxOn = false;
	SetComponentTickEnabled(true);
	SetPhase(EMonsterAttackPhase::Anticipation);
}

void UMonsterAttackComponent::CancelAttack()
{
	if (bHitboxOn)
	{
		bHitboxOn = false;
		OnToggleHitbox.Broadcast(false);
	}
	SetComponentTickEnabled(false);
	SetPhase(EMonsterAttackPhase::Finished);
	CurrentPhase = EMonsterAttackPhase::None;
}

void UMonsterAttackComponent::SetPhase(EMonsterAttackPhase NewPhase)
{
	if (CurrentPhase == NewPhase)
	{
		return;
	}
	CurrentPhase = NewPhase;
	OnPhaseChanged.Broadcast(NewPhase);

	if (NewPhase == EMonsterAttackPhase::HitActive)
	{
		OnPlayAttackAnim.Broadcast(ActiveData.AttackId);
	}
}

void UMonsterAttackComponent::RotateTowardTarget(float DeltaTime, float RateDegPerSec)
{
	AActor* Rotator = RotationActor ? RotationActor.Get() : GetOwner();
	if (!Rotator || !TargetActor || RateDegPerSec <= 0.f)
	{
		return;
	}

	const FRotator Look = UKismetMathLibrary::FindLookAtRotation(
		Rotator->GetActorLocation(), TargetActor->GetActorLocation());
	const FRotator Cur = Rotator->GetActorRotation();
	const float MaxStep = RateDegPerSec * DeltaTime;
	const float NewYaw = FMath::FixedTurn(Cur.Yaw, Look.Yaw, MaxStep);
	Rotator->SetActorRotation(FRotator(Cur.Pitch, NewYaw, Cur.Roll));
}

void UMonsterAttackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CurrentPhase == EMonsterAttackPhase::None || CurrentPhase == EMonsterAttackPhase::Finished)
	{
		SetComponentTickEnabled(false);
		return;
	}

	ElapsedTime += DeltaTime;

	// 予兆中は軸合わせ。
	if (ElapsedTime < ActiveData.TurnStopTime)
	{
		RotateTowardTarget(DeltaTime, ActiveData.TurnRateDegPerSec);
	}

	// フェーズ進行（時刻ベース）。
	if (ElapsedTime < ActiveData.TurnStopTime)
	{
		SetPhase(EMonsterAttackPhase::Anticipation);
	}
	else if (ElapsedTime < ActiveData.HitActiveStart)
	{
		SetPhase(EMonsterAttackPhase::Committed);
	}
	else if (ElapsedTime < ActiveData.HitActiveEnd)
	{
		SetPhase(EMonsterAttackPhase::HitActive);
	}
	else if (ElapsedTime < ActiveData.EndTime)
	{
		SetPhase(EMonsterAttackPhase::Recovery);
	}
	else
	{
		SetComponentTickEnabled(false);
		SetPhase(EMonsterAttackPhase::Finished);
		CurrentPhase = EMonsterAttackPhase::None;
		OnAttackFinished.Broadcast();
		return;
	}

	// Hitbox の ON/OFF は判定ウィンドウで厳密に。
	const bool bShouldHit =
		ElapsedTime >= ActiveData.HitActiveStart && ElapsedTime < ActiveData.HitActiveEnd;
	if (bShouldHit != bHitboxOn)
	{
		bHitboxOn = bShouldHit;
		OnToggleHitbox.Broadcast(bHitboxOn);
	}
}
