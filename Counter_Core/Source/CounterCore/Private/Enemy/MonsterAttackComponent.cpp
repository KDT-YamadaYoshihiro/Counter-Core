#include "Enemy/MonsterAttackComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "CounterCoreDebug.h"

UMonsterAttackComponent::UMonsterAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 条件可視化のため常時 Tick。攻撃タイムラインは CurrentPhase で判定する。
	PrimaryComponentTick.bStartWithTickEnabled = true;
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

FMonsterComboData UMonsterAttackComponent::GetComboData(FName ComboId, bool& bFound) const
{
	bFound = false;
	if (ComboDataTable)
	{
		if (const FMonsterComboData* Row =
			ComboDataTable->FindRow<FMonsterComboData>(ComboId, TEXT("GetComboData"), false))
		{
			bFound = true;
			return *Row;
		}
	}
	return FMonsterComboData();
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
	PrintAttackEvent(TEXT("攻撃開始"), FColor::Cyan);
	SetPhase(EMonsterAttackPhase::Anticipation);
}

void UMonsterAttackComponent::CancelAttack()
{
	const bool bWasActive = CurrentPhase != EMonsterAttackPhase::None && CurrentPhase != EMonsterAttackPhase::Finished;
	if (bHitboxOn)
	{
		bHitboxOn = false;
		OnToggleHitbox.Broadcast(false);
	}
	if (bWasActive)
	{
		PrintAttackEvent(TEXT("攻撃中断"), FColor::Yellow);
	}
	SetPhase(EMonsterAttackPhase::Finished);
	CurrentPhase = EMonsterAttackPhase::None;
}

void UMonsterAttackComponent::PrintAttackEvent(const TCHAR* Label, const FColor& Color) const
{
	if (!bPrintAttackEvents)
	{
		return;
	}
	const FString Msg = FString::Printf(TEXT("%s : %s  (t=%.2f)"), *ActiveData.AttackId.ToString(), Label, ElapsedTime);
	UE_LOG(LogTemp, Log, TEXT("[MonsterAttack] %s"), *Msg);
#if !UE_BUILD_SHIPPING
	if (GEngine && CounterCoreDebug::IsOnScreenDebugEnabled())
	{
		GEngine->AddOnScreenDebugMessage(-1, AttackEventPrintDuration, Color, Msg);
	}
#endif
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

	if (bDrawDebug && CounterCoreDebug::IsOnScreenDebugEnabled())
	{
		DrawDebugVisualization();
	}

	if (CurrentPhase == EMonsterAttackPhase::None || CurrentPhase == EMonsterAttackPhase::Finished)
	{
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
		PrintAttackEvent(TEXT("攻撃終了"), FColor::Green);
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
		PrintAttackEvent(bHitboxOn ? TEXT("判定ON") : TEXT("判定OFF"),
			bHitboxOn ? FColor::Red : FColor(255, 140, 0));
	}
}

void UMonsterAttackComponent::DrawDebugVisualization() const
{
#if ENABLE_DRAW_DEBUG
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	const FVector Base = Owner->GetActorLocation();
	const FVector Fwd = Owner->GetActorForwardVector().GetSafeNormal2D();
	const FVector Up = FVector::UpVector;
	const FVector AxisX(1.f, 0.f, 0.f);
	const FVector AxisY(0.f, 1.f, 0.f);

	// ターゲットまでの距離(m)・符号付き角度(deg)
	float DistM = -1.f;
	float AngleDeg = 0.f;
	if (TargetActor)
	{
		const FVector To = TargetActor->GetActorLocation() - Base;
		DistM = To.Size2D() / 100.f;
		const FVector To2D = To.GetSafeNormal2D();
		AngleDeg = FMath::RadiansToDegrees(
			FMath::Atan2(FVector::CrossProduct(Fwd, To2D).Z, FVector::DotProduct(Fwd, To2D)));
		DrawDebugLine(World, Base, TargetActor->GetActorLocation(), FColor::White, false, -1.f, 0, 1.5f);
		DrawDebugString(World, Base + FVector(0, 0, 140.f),
			FString::Printf(TEXT("dist %.2fm  ang %.0f"), DistM, AngleDeg), nullptr, FColor::White, 0.f);
	}

	// 各コンボの発動条件: 最大距離リング + 角度ウェッジ（条件成立=緑 / 不成立=灰）
	if (ComboDataTable)
	{
		TArray<FMonsterComboData*> Rows;
		ComboDataTable->GetAllRows<FMonsterComboData>(TEXT("DebugViz"), Rows);
		int32 i = 0;
		for (const FMonsterComboData* Row : Rows)
		{
			if (!Row)
			{
				continue;
			}
			const float Radius = (Row->MaxDistanceM > 0.f ? Row->MaxDistanceM : 8.f) * 100.f;
			const bool bDistOk = Row->MaxDistanceM <= 0.f || (DistM >= 0.f && DistM < Row->MaxDistanceM);
			const bool bBehind = FMath::Abs(AngleDeg) > 100.f;
			const bool bAngleOk = Row->bRequireTargetBehind ? bBehind : (FMath::Abs(AngleDeg) <= Row->MaxAngleDeg);
			const bool bMet = TargetActor && bDistOk && bAngleOk;
			const FColor Col = bMet ? FColor::Green : FColor(110, 110, 110);
			const FVector RingCenter = Base + FVector(0, 0, 4.f + i * 1.5f);

			DrawDebugCircle(World, RingCenter, Radius, 48, Col, false, -1.f, 0, 1.5f, AxisX, AxisY, false);

			const float Half = FMath::Clamp(Row->MaxAngleDeg, 0.f, 180.f);
			const FVector WedgeCenter = Row->bRequireTargetBehind ? -Fwd : Fwd;
			const FVector L = WedgeCenter.RotateAngleAxis(-Half, Up);
			const FVector R = WedgeCenter.RotateAngleAxis(Half, Up);
			DrawDebugLine(World, Base, Base + L * Radius, Col, false, -1.f, 0, 1.f);
			DrawDebugLine(World, Base, Base + R * Radius, Col, false, -1.f, 0, 1.f);
			DrawDebugString(World, RingCenter + WedgeCenter * Radius,
				Row->ComboId.ToString(), nullptr, Col, 0.f);
			++i;
		}
	}

	// 進行中の攻撃: 接触距離リング + 接触角度ウェッジ + フェーズ表示（黄 / 判定中は赤）
	if (CurrentPhase != EMonsterAttackPhase::None && CurrentPhase != EMonsterAttackPhase::Finished)
	{
		const FColor Col = bHitboxOn ? FColor::Red : FColor::Yellow;
		const float Radius = FMath::Max(ActiveData.ContactDistanceM, 0.1f) * 100.f;
		DrawDebugCircle(World, Base + FVector(0, 0, 2.f), Radius, 48, Col, false, -1.f, 0, 3.f, AxisX, AxisY, false);

		const float Half = FMath::Clamp(ActiveData.ContactAngleDeg, 0.f, 180.f);
		const FVector L = Fwd.RotateAngleAxis(-Half, Up);
		const FVector R = Fwd.RotateAngleAxis(Half, Up);
		DrawDebugLine(World, Base, Base + L * Radius, Col, false, -1.f, 0, 2.5f);
		DrawDebugLine(World, Base, Base + R * Radius, Col, false, -1.f, 0, 2.5f);

		const TCHAR* PhaseStr = TEXT("");
		switch (CurrentPhase)
		{
		case EMonsterAttackPhase::Anticipation: PhaseStr = TEXT("予兆"); break;
		case EMonsterAttackPhase::Committed:    PhaseStr = TEXT("発生前"); break;
		case EMonsterAttackPhase::HitActive:    PhaseStr = TEXT("判定"); break;
		case EMonsterAttackPhase::Recovery:     PhaseStr = TEXT("硬直"); break;
		default: break;
		}
		DrawDebugString(World, Base + FVector(0, 0, 165.f),
			FString::Printf(TEXT("%s  %s  t=%.2f  dmg=%d"),
				*ActiveData.AttackId.ToString(), PhaseStr, ElapsedTime, ActiveData.Damage),
			nullptr, Col, 0.f);
	}
#endif
}
