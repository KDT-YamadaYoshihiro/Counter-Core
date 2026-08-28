#include "Battle/BattleDirectorComponent.h"
#include "Player/PlayerCombatComponent.h"
#include "Enemy/MonsterCombatComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"

UBattleDirectorComponent::UBattleDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBattleDirectorComponent::BeginPlay()
{
	Super::BeginPlay();
	// キャラのスポーンを少し待ってから参照解決。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ResolveTimer, this, &UBattleDirectorComponent::ResolveRefs, 0.5f, false);
	}
}

void UBattleDirectorComponent::ResolveRefs()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!PlayerCombat)
	{
		if (APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			PlayerCombat = P->FindComponentByClass<UPlayerCombatComponent>();
		}
	}
	if (!EnemyCombat)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (UMonsterCombatComponent* MC = It->FindComponentByClass<UMonsterCombatComponent>())
			{
				EnemyCombat = MC;
				break;
			}
		}
	}

	if (PlayerCombat)
	{
		PlayerCombat->OnDied.AddDynamic(this, &UBattleDirectorComponent::HandlePlayerDied);
	}
	if (EnemyCombat)
	{
		EnemyCombat->OnDied.AddDynamic(this, &UBattleDirectorComponent::HandleEnemyDied);
	}

	// どちらか取れなかったらもう一度だけ後で試す。
	if ((!PlayerCombat || !EnemyCombat))
	{
		World->GetTimerManager().SetTimer(ResolveTimer, this, &UBattleDirectorComponent::ResolveRefs, 1.0f, false);
	}
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
		// グローバル time dilation の影響を受けるので、実時間換算で長めに設定。
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
}
