#include "Battle/BattleDirectorComponent.h"
#include "Battle/BattleResultSubsystem.h"
#include "Player/PlayerCombatComponent.h"
#include "Player/PlayerGuardComponent.h"
#include "Player/PlayerActionComponent.h"
#include "Enemy/MonsterCombatComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"

UBattleDirectorComponent::UBattleDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UBattleDirectorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoIntro && IntroDuration > 0.f)
	{
		bIntroActive = true;
		IntroTimer = IntroDuration;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ResolveTimer, this, &UBattleDirectorComponent::ResolveRefs, 0.3f, false);
	}
}

void UBattleDirectorComponent::ResolveRefs()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!PlayerCombat || !PlayerGuard || !PlayerAction)
	{
		if (APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			if (!PlayerCombat)
			{
				PlayerCombat = P->FindComponentByClass<UPlayerCombatComponent>();
			}
			if (!PlayerGuard)
			{
				PlayerGuard = P->FindComponentByClass<UPlayerGuardComponent>();
			}
			if (!PlayerAction)
			{
				PlayerAction = P->FindComponentByClass<UPlayerActionComponent>();
			}
		}
	}
	if (!EnemyCombat)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (UMonsterCombatComponent* MC = It->FindComponentByClass<UMonsterCombatComponent>())
			{
				EnemyCombat = MC;
				EnemyActor = *It;
				break;
			}
		}
	}

	if (PlayerCombat && !PlayerCombat->OnDied.IsAlreadyBound(this, &UBattleDirectorComponent::HandlePlayerDied))
	{
		PlayerCombat->OnDied.AddDynamic(this, &UBattleDirectorComponent::HandlePlayerDied);
	}
	if (EnemyCombat && !EnemyCombat->OnDied.IsAlreadyBound(this, &UBattleDirectorComponent::HandleEnemyDied))
	{
		EnemyCombat->OnDied.AddDynamic(this, &UBattleDirectorComponent::HandleEnemyDied);
	}
	if (PlayerGuard && !PlayerGuard->OnGuardSuccess.IsAlreadyBound(this, &UBattleDirectorComponent::HandleGuardSuccess))
	{
		PlayerGuard->OnGuardSuccess.AddDynamic(this, &UBattleDirectorComponent::HandleGuardSuccess);
	}

	// 開始演出中は凍結。
	if (bIntroActive)
	{
		SetActorsFrozen(true);
	}

	if (!PlayerCombat || !PlayerAction || !EnemyCombat)
	{
		World->GetTimerManager().SetTimer(ResolveTimer, this, &UBattleDirectorComponent::ResolveRefs, 0.5f, false);
	}
}

void UBattleDirectorComponent::SetActorsFrozen(bool bFrozen)
{
	if (bActorsFrozen == bFrozen)
	{
		return;
	}
	bActorsFrozen = bFrozen;

	// プレイヤー入力（移動・視点 + Enhanced Input の BindAction も含めて止める）
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetIgnoreMoveInput(bFrozen);
		PC->SetIgnoreLookInput(bFrozen);
		if (APawn* Pawn = PC->GetPawn())
		{
			if (bFrozen) { Pawn->DisableInput(PC); }
			else { Pawn->EnableInput(PC); }
		}
	}

	// C++ の攻撃/ガード/回避は PollFallbackInput で生キーを直接見ているため
	// SetIgnoreMoveInput では止まらない。コンポーネントの Tick を止める。
	if (PlayerAction)
	{
		if (bFrozen)
		{
			PlayerAction->CancelAttack();
		}
		PlayerAction->SetComponentTickEnabled(!bFrozen);
	}
	if (PlayerGuard)
	{
		if (bFrozen)
		{
			PlayerGuard->StopGuard();
		}
		PlayerGuard->SetComponentTickEnabled(!bFrozen);
	}

	// 敵 AI（Tick を止める）
	if (EnemyActor)
	{
		EnemyActor->SetActorTickEnabled(!bFrozen);
	}
}

void UBattleDirectorComponent::StartBattle()
{
	if (!bIntroActive)
	{
		return;
	}
	bIntroActive = false;
	IntroTimer = 0.f;
	SetActorsFrozen(false);
	OnBattleStarted.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("[BattleDirector] バトル開始"));
}

void UBattleDirectorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bMenuOpen)
	{
		PollMenuInput();
		return;
	}

	if (bIntroActive)
	{
		IntroTimer -= DeltaTime;
		if (IntroTimer <= 0.f)
		{
			StartBattle();
		}
		return;
	}

	if (Result == EBattleResult::InProgress)
	{
		ElapsedTime += DeltaTime;

		// メニューを開く（仕様書 UI「メニュー」: ボタン押下でバトル停止・展開）。
		if (bAllowMenu)
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
			{
				if (PC->WasInputKeyJustPressed(EKeys::Escape)
					|| PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)
					|| PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left))
				{
					OpenMenu();
				}
			}
		}
	}
}

// --------------------------------------------------------------------------
// インゲームメニュー
// --------------------------------------------------------------------------

void UBattleDirectorComponent::OpenMenu()
{
	if (bMenuOpen || bIntroActive || Result != EBattleResult::InProgress)
	{
		return;
	}
	bMenuOpen = true;
	bMenuDialogOpen = false;
	bControlsPanelOpen = false;
	MenuSelection = 0;

	SetActorsFrozen(true);
	// バトル停止（時間をほぼ止める）。メニュー操作は Tick で拾うので完全 Pause はしない。
	UGameplayStatics::SetGlobalTimeDilation(this, MenuTimeDilation);
	OnMenuOpened.Broadcast();
}

void UBattleDirectorComponent::CloseMenu()
{
	if (!bMenuOpen)
	{
		return;
	}
	bMenuOpen = false;
	bMenuDialogOpen = false;
	bControlsPanelOpen = false;

	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	SetActorsFrozen(false);
	OnMenuClosed.Broadcast();
}

void UBattleDirectorComponent::PollMenuInput()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	auto Pressed = [PC](FKey A, FKey B, FKey C) {
		return PC->WasInputKeyJustPressed(A) || PC->WasInputKeyJustPressed(B) || PC->WasInputKeyJustPressed(C);
	};
	const bool bUp = Pressed(EKeys::W, EKeys::Up, EKeys::Gamepad_DPad_Up) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Up);
	const bool bDown = Pressed(EKeys::S, EKeys::Down, EKeys::Gamepad_DPad_Down) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Down);
	const bool bConfirm = Pressed(EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom);
	const bool bCancel = Pressed(EKeys::Escape, EKeys::Gamepad_FaceButton_Right, EKeys::BackSpace);

	// 操作説明パネル表示中はキャンセルでメニューへ戻るだけ。
	if (bControlsPanelOpen)
	{
		if (bCancel || bConfirm)
		{
			bControlsPanelOpen = false;
		}
		return;
	}

	// あきらめる確認ダイアログ
	if (bMenuDialogOpen)
	{
		if (PC->WasInputKeyJustPressed(EKeys::Left) || PC->WasInputKeyJustPressed(EKeys::Right)
			|| PC->WasInputKeyJustPressed(EKeys::A) || PC->WasInputKeyJustPressed(EKeys::D)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right))
		{
			bMenuDialogYes = !bMenuDialogYes;
		}
		if (bCancel)
		{
			bMenuDialogOpen = false;
		}
		else if (bConfirm)
		{
			if (bMenuDialogYes) { GiveUp(); }
			else { bMenuDialogOpen = false; }
		}
		return;
	}

	// メニュー本体
	if (bUp)   { MenuSelection = (MenuSelection + 2) % 3; }
	if (bDown) { MenuSelection = (MenuSelection + 1) % 3; }

	if (bCancel)
	{
		CloseMenu(); // Esc で閉じる = 続ける
		return;
	}
	if (bConfirm)
	{
		ConfirmMenuSelection();
	}
}

void UBattleDirectorComponent::ConfirmMenuSelection()
{
	switch (MenuSelection)
	{
	case 0: // 続ける
		CloseMenu();
		break;
	case 1: // 操作説明
		bControlsPanelOpen = true;
		break;
	case 2: // あきらめる → 確認ダイアログ
		bMenuDialogOpen = true;
		bMenuDialogYes = false;
		break;
	default:
		break;
	}
}

void UBattleDirectorComponent::GiveUp()
{
	// 仕様書 全体フロー: メニュー＞諦める＞はい → フェードアウト → リザルト（敗北）。
	bMenuOpen = false;
	bMenuDialogOpen = false;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	EndBattle(EBattleResult::PlayerLose);
}

void UBattleDirectorComponent::HandleGuardSuccess()
{
	// 仕様書 リザルト: ガード成功回数をスコア判定に使う。
	++GuardSuccessCount;
}

void UBattleDirectorComponent::HandlePlayerDied()
{
	EndBattle(EBattleResult::PlayerLose);
}

void UBattleDirectorComponent::HandleEnemyDied()
{
	EndBattle(EBattleResult::PlayerWin);
}

void UBattleDirectorComponent::EndBattle(EBattleResult NewResult)
{
	if (Result != EBattleResult::InProgress)
	{
		return;
	}
	Result = NewResult;

	UWorld* World = GetWorld();

	// 仕様: 終了演出でヒットストップを長めに。
	if (World && EndSlowMoRealDuration > 0.f)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, EndSlowMoTimeScale);
		TWeakObjectPtr<UBattleDirectorComponent> WeakThis(this);
		FTimerDelegate Del;
		Del.BindLambda([WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				UGameplayStatics::SetGlobalTimeDilation(WeakThis.Get(), 1.f);
			}
		});
		World->GetTimerManager().SetTimer(SlowMoTimer, Del, EndSlowMoRealDuration * EndSlowMoTimeScale, false);
	}

	if (bDisablePlayerInputOnEnd && World)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			PC->SetIgnoreMoveInput(true);
			PC->SetIgnoreLookInput(true);
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->DisableInput(PC);

				// C++ の攻撃/ガードは PollFallbackInput で生キーを直接見ているため
				// DisableInput では止まらない。コンポーネントの Tick を止めて確実に固める。
				if (UPlayerActionComponent* Action = Pawn->FindComponentByClass<UPlayerActionComponent>())
				{
					Action->CancelAttack();
					Action->SetComponentTickEnabled(false);
				}
				if (UPlayerGuardComponent* Guard = Pawn->FindComponentByClass<UPlayerGuardComponent>())
				{
					Guard->StopGuard();
					Guard->SetComponentTickEnabled(false);
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[BattleDirector] 決着: %s"),
		NewResult == EBattleResult::PlayerWin ? TEXT("プレイヤー勝利") : TEXT("プレイヤー敗北"));

	OnBattleEnded.Broadcast(Result);

	// 仕様書 リザルト: 勝敗 / タイム / ガード成功回数 / ランクを引き継ぐ。
	if (UGameInstance* GI = UGameplayStatics::GetGameInstance(this))
	{
		if (UBattleResultSubsystem* ResultSys = GI->GetSubsystem<UBattleResultSubsystem>())
		{
			ResultSys->SubmitResult(NewResult == EBattleResult::PlayerWin, ElapsedTime, GuardSuccessCount);
		}
	}

	// 少し待ってからリザルトを表示（終了演出のあいだ）。
	if (World)
	{
		TWeakObjectPtr<UBattleDirectorComponent> WeakThis(this);
		FTimerDelegate Del;
		Del.BindLambda([WeakThis]()
		{
			if (WeakThis.IsValid()) { WeakThis->ShowResult(); }
		});
		World->GetTimerManager().SetTimer(ResultTimer, Del, FMath::Max(0.1f, ResultTransitionDelay), false);
	}
	else
	{
		ShowResult();
	}
}

void UBattleDirectorComponent::ShowResult()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);

	// 仕様書のリザルト画は「決着した戦闘シーンの上」に出る。レベル遷移せず HUD を差し替える。
	if (bShowResultInPlace && PC && ResultHUDClass)
	{
		PC->ClientSetHUD(ResultHUDClass);
		return;
	}

	// フォールバック: リザルトレベルへ遷移。
	if (GetWorld() && !ResultLevelName.IsNone())
	{
		UGameplayStatics::OpenLevel(this, ResultLevelName);
	}
}
