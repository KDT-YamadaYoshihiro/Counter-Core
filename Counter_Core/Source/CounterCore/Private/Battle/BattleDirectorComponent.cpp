#include "Battle/BattleDirectorComponent.h"
#include "Battle/BattleResultSubsystem.h"
#include "Player/PlayerCombatComponent.h"
#include "Player/PlayerGuardComponent.h"
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

	if (!PlayerCombat || !PlayerGuard)
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

	if (!PlayerCombat || !EnemyCombat)
	{
		World->GetTimerManager().SetTimer(ResolveTimer, this, &UBattleDirectorComponent::ResolveRefs, 1.0f, false);
	}
}

void UBattleDirectorComponent::SetActorsFrozen(bool bFrozen)
{
	// プレイヤー入力
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetIgnoreMoveInput(bFrozen);
		PC->SetIgnoreLookInput(bFrozen);
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
	}
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
