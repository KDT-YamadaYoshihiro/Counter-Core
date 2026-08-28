#include "Player/PlayerCameraComponent.h"
#include "Enemy/MonsterCombatComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"

UPlayerCameraComponent::UPlayerCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 移動 / BP カメラの後に権威的に上書きする。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPlayerCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ResolveTimer, this, &UPlayerCameraComponent::ResolveRefs, 0.2f, false);
	}
}

void UPlayerCameraComponent::ResolveRefs()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	SpringArm = Owner->FindComponentByClass<USpringArmComponent>();
	Camera = Owner->FindComponentByClass<UCameraComponent>();
	if (const ACharacter* Char = Cast<ACharacter>(Owner))
	{
		Movement = Char->GetCharacterMovement();
	}

	if (SpringArm)
	{
		DefaultArmLength = SpringArm->TargetArmLength;
		DefaultSocketOffset = SpringArm->SocketOffset;
	}
	if (Movement)
	{
		bDefaultOrientToMovement = Movement->bOrientRotationToMovement;
	}
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		bDefaultUseControllerYaw = Pawn->bUseControllerRotationYaw;
	}

	if (!SpringArm || !Camera)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(ResolveTimer, this, &UPlayerCameraComponent::ResolveRefs, 0.5f, false);
		}
		return;
	}

	// 参照が揃った最初のタイミングで、いったん敵の方へカメラを即スナップ
	// （開始時に毎回同じ画にするため。以降は Tick が維持する）。
	if (bAimAtEnemyOnStart && !bDidStartSnap)
	{
		SnapAimToEnemy();
	}
}

void UPlayerCameraComponent::SnapAimToEnemy()
{
	AActor* Owner = GetOwner();
	APlayerController* PC = Owner ? Cast<APlayerController>(Cast<APawn>(Owner)->GetController()) : nullptr;
	if (!Owner || !PC)
	{
		return;
	}
	float Dist = 0.f;
	AActor* Enemy = FindNearestEnemy(Dist); // 距離制限なし
	if (!Enemy)
	{
		return;
	}
	const FVector Pivot = Owner->GetActorLocation() + FVector(0.f, 0.f, LockedSocketOffset.Z);
	FRotator Desired = (Enemy->GetActorLocation() - Pivot).Rotation();
	Desired.Pitch = FMath::Clamp(Desired.Pitch, LockedPitchMin, LockedPitchMax);
	Desired.Roll = 0.f;
	PC->SetControlRotation(Desired);

	// キャラも敵の方へ向ける。
	FRotator Face = (Enemy->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D().Rotation();
	Face.Pitch = 0.f; Face.Roll = 0.f;
	Owner->SetActorRotation(Face);

	bDidStartSnap = true;
}

AActor* UPlayerCameraComponent::FindNearestEnemy(float& OutDist) const
{
	OutDist = TNumericLimits<float>::Max();
	AActor* Best = nullptr;
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}
	const FVector From = Owner->GetActorLocation();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->FindComponentByClass<UMonsterCombatComponent>())
		{
			continue;
		}
		const float D = FVector::Dist(From, It->GetActorLocation());
		if (D < OutDist)
		{
			OutDist = D;
			Best = *It;
		}
	}
	return Best;
}

void UPlayerCameraComponent::SetLocked(bool bNew, AActor* Target)
{
	if (bLockedOn == bNew)
	{
		if (bNew)
		{
			LockTarget = Target;
		}
		return;
	}
	bLockedOn = bNew;
	LockTarget = bNew ? Target : nullptr;

	// 円運動移動のため、ロック中は「コントローラー Yaw で向く」、非ロックは元に戻す。
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->bUseControllerRotationYaw = bNew ? true : bDefaultUseControllerYaw;
	}
	if (Movement)
	{
		Movement->bOrientRotationToMovement = bNew ? false : bDefaultOrientToMovement;
	}

	OnLockStateChanged.Broadcast(bLockedOn);
}

void UPlayerCameraComponent::ToggleLock()
{
	if (bLockedOn)
	{
		bManualLockDisabled = true;
		SetLocked(false, nullptr);
	}
	else
	{
		float D = 0.f;
		AActor* E = FindNearestEnemy(D);
		if (E && D <= AutoLockBreakRange)
		{
			bManualLockDisabled = false;
			SetLocked(true, E);
		}
	}
}

void UPlayerCameraComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(Dt, TickType, ThisTickFunction);

	if (!bTakeOverCamera || !SpringArm)
	{
		return;
	}

	AActor* Owner = GetOwner();
	APlayerController* PC = Owner ? Cast<APlayerController>(Cast<APawn>(Owner)->GetController()) : nullptr;
	if (!Owner || !PC)
	{
		return;
	}

	TimeSinceBegin += Dt;

	// --- ロック状態の更新（ヒステリシス）---
	float Dist = 0.f;
	AActor* Nearest = FindNearestEnemy(Dist);

	// 開始直後は距離に関係なく敵ロック（毎回同じ画で戦闘に入るため）。
	const bool bStartWindow = bAimAtEnemyOnStart && !bManualLockDisabled
		&& TimeSinceBegin < StartAimHoldSeconds && Nearest != nullptr;

	if (bStartWindow)
	{
		if (!bLockedOn)
		{
			SetLocked(true, Nearest);
		}
		LockTarget = Nearest;
	}
	else if (bLockedOn)
	{
		if (!Nearest || Dist > AutoLockBreakRange)
		{
			SetLocked(false, nullptr);
		}
		else
		{
			LockTarget = Nearest;
		}
	}
	else if (!bManualLockDisabled && Nearest && Dist <= AutoLockRange)
	{
		SetLocked(true, Nearest);
	}
	// 距離内に戻ったら手動解除フラグはリセット（次に近づいたら再ロック可）。
	if (!bStartWindow && (!Nearest || Dist > AutoLockBreakRange))
	{
		bManualLockDisabled = false;
	}

	const FVector PlayerLoc = Owner->GetActorLocation();

	if (bLockedOn && LockTarget.IsValid())
	{
		const FVector EnemyLoc = LockTarget->GetActorLocation();
		const FVector Focus = FMath::Lerp(PlayerLoc, EnemyLoc, FocusMidpointBias);
		const FVector Pivot = PlayerLoc + FVector(0.f, 0.f, LockedSocketOffset.Z);

		FRotator Desired = (Focus - Pivot).Rotation();
		Desired.Pitch = FMath::Clamp(Desired.Pitch, LockedPitchMin, LockedPitchMax);
		Desired.Roll = 0.f;

		const FRotator NewRot = FMath::RInterpTo(PC->GetControlRotation(), Desired, Dt, LookInterpSpeed);
		PC->SetControlRotation(NewRot);

		// 両者が収まるようアーム長を距離に応じて伸縮。
		const float Flat = FVector::Dist2D(PlayerLoc, EnemyLoc);
		const float TargetArm = FMath::GetMappedRangeValueClamped(
			FVector2D(200.f, ArmLengthDistanceRef), FVector2D(ArmLengthMin, ArmLengthMax), Flat);
		SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetArm, Dt, 5.f);
		SpringArm->SocketOffset = FMath::VInterpTo(SpringArm->SocketOffset, LockedSocketOffset, Dt, 6.f);

		// プレイヤーを敵方向へ（左右入力＝敵中心の円運動）。
		if (FacePlayerInterpSpeed > 0.f)
		{
			FRotator Face = (EnemyLoc - PlayerLoc).GetSafeNormal2D().Rotation();
			Face.Pitch = 0.f;
			Face.Roll = 0.f;
			Owner->SetActorRotation(FMath::RInterpTo(Owner->GetActorRotation(), Face, Dt, FacePlayerInterpSpeed));
		}
	}
	else
	{
		// 非ロック: アーム長 / オフセットを既定へ。視点はプレイヤーの入力に任せる。
		SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, DefaultArmLength, Dt, 5.f);
		SpringArm->SocketOffset = FMath::VInterpTo(SpringArm->SocketOffset, DefaultSocketOffset, Dt, 6.f);
	}
}

FVector UPlayerCameraComponent::GetLockReticleWorldLocation(bool& bValid) const
{
	bValid = bLockedOn && LockTarget.IsValid();
	return bValid ? LockTarget->GetActorLocation() + FVector(0.f, 0.f, 40.f) : FVector::ZeroVector;
}
