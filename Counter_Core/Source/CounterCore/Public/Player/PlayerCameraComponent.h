#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerCameraComponent.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UCharacterMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerLockStateChanged, bool, bLocked);

/**
 * 仕様書 Battle シート「カメラ制御」の C++ 実装。既存 BP のスプリングアーム / カメラ /
 * LockOnCollision コンポーネントを流用し、Tick（PostUpdateWork）で権威的に制御する。
 *
 * - ターゲットロック: 敵との距離が一定内で自動オン（ヒステリシス付き）
 * - ロック時: プレイヤーと敵の中点を注視、アーム長を距離に応じて伸縮して両者を画面に収める、
 *   プレイヤーを敵方向へ向ける（左右入力が敵中心の円運動になる）
 * - 非ロック時: 通常の背後カメラ（右スティック / マウスの視点入力に任せる）
 */
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class COUNTERCORE_API UPlayerCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerCameraComponent();

	/** false で一切介入しない（既存 BP カメラに戻す）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bTakeOverCamera = true;

	/** この距離（cm）以内に敵がいれば自動ロックオン。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn", meta = (ClampMin = "0"))
	float AutoLockRange = 1800.f;

	/** この距離（cm）を超えるとロック解除（ヒステリシス）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn", meta = (ClampMin = "0"))
	float AutoLockBreakRange = 2600.f;

	/** 注視点: 0=プレイヤー / 1=敵 / 0.5=中点（仕様: ロック時は中点）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn", meta = (ClampMin = "0", ClampMax = "1"))
	float FocusMidpointBias = 0.5f;

	/** ロック時、カメラが注視点を向く補間速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn", meta = (ClampMin = "0.1"))
	float LookInterpSpeed = 9.f;

	/** ロック時のカメラピッチのクランプ（deg）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn")
	float LockedPitchMin = -55.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn")
	float LockedPitchMax = 15.f;

	/** アーム長: プレイヤー-敵距離がこの値のとき ArmLengthMax、近いほど短く。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn", meta = (ClampMin = "1"))
	float ArmLengthDistanceRef = 1400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn", meta = (ClampMin = "0"))
	float ArmLengthMin = 320.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn", meta = (ClampMin = "0"))
	float ArmLengthMax = 650.f;

	/** ロック時のスプリングアーム SocketOffset（プレイヤー背後・上のオフセット）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn")
	FVector LockedSocketOffset = FVector(0.f, 40.f, 60.f);

	/** ロック時、プレイヤーを敵方向へ向ける補間速度（円運動移動のため）。0 で無効。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn", meta = (ClampMin = "0"))
	float FacePlayerInterpSpeed = 12.f;

	UFUNCTION(BlueprintPure, Category = "Camera|LockOn")
	bool IsLockedOn() const { return bLockedOn; }

	UFUNCTION(BlueprintPure, Category = "Camera|LockOn")
	AActor* GetLockTarget() const { return LockTarget.Get(); }

	/** ロックオン表示（点＋円）を出すワールド位置。bValid=false なら出さない。 */
	UFUNCTION(BlueprintPure, Category = "Camera|LockOn")
	FVector GetLockReticleWorldLocation(bool& bValid) const;

	/** ロックの手動トグル（入力から呼んでもよい）。 */
	UFUNCTION(BlueprintCallable, Category = "Camera|LockOn")
	void ToggleLock();

	UPROPERTY(BlueprintAssignable, Category = "Camera|LockOn")
	FPlayerLockStateChanged OnLockStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void ResolveRefs();
	AActor* FindNearestEnemy(float& OutDist) const;
	void SetLocked(bool bNew, AActor* Target);

	UPROPERTY() TObjectPtr<USpringArmComponent> SpringArm;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<UCharacterMovementComponent> Movement;
	TWeakObjectPtr<AActor> LockTarget;

	bool bLockedOn = false;
	bool bManualLockDisabled = false; // 手動でロックを切ったら距離内でも自動ロックしない

	// 元の設定（非ロック時に戻す用）
	float DefaultArmLength = 400.f;
	FVector DefaultSocketOffset = FVector::ZeroVector;
	bool bDefaultOrientToMovement = true;
	bool bDefaultUseControllerYaw = false;

	FTimerHandle ResolveTimer;
};
