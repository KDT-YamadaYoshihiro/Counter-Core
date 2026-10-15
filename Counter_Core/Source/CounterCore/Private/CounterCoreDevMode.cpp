#include "CounterCoreDevMode.h"

#if !UE_BUILD_SHIPPING
#include "Player/PlayerCombatComponent.h"
#include "Enemy/MonsterCharacterBase.h"
#include "Enemy/MonsterAttackComponent.h"
#include "Enemy/MonsterCombatComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	bool bDevActive = false;
	bool bPlayerInvincible = false;
	bool bMonsterFrozen = false;
	bool bSlowMotion = false;
	FString LastAction;

	constexpr float SlowTimeScale = 0.25f;

	AMonsterCharacterBase* FindMonster(UWorld* World)
	{
		for (TActorIterator<AMonsterCharacterBase> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	void SetMonsterFrozen(AMonsterCharacterBase* Mon, bool bFrozen)
	{
		if (!Mon)
		{
			return;
		}
		Mon->SetActorTickEnabled(!bFrozen);
		if (UMonsterAttackComponent* Atk = Mon->FindComponentByClass<UMonsterAttackComponent>())
		{
			if (bFrozen)
			{
				Atk->CancelAttack();
			}
			Atk->SetComponentTickEnabled(!bFrozen);
		}
		if (bFrozen)
		{
			if (UCharacterMovementComponent* Move = Mon->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
			}
		}
	}

	void ApplySlow(UWorld* World)
	{
		UGameplayStatics::SetGlobalTimeDilation(World, bSlowMotion ? SlowTimeScale : 1.f);
	}

	void ResetAll(UWorld* World, UPlayerCombatComponent* PC, AMonsterCharacterBase* Mon)
	{
		if (bPlayerInvincible && PC)
		{
			PC->SetInvulnerable(false);
		}
		if (bMonsterFrozen)
		{
			SetMonsterFrozen(Mon, false);
		}
		if (bSlowMotion)
		{
			bSlowMotion = false;
			ApplySlow(World);
		}
		bPlayerInvincible = false;
		bMonsterFrozen = false;
	}
}

namespace CounterCoreDevMode
{
	bool IsActive()
	{
		return bDevActive;
	}

	void Tick(AHUD* Hud)
	{
		UWorld* World = Hud ? Hud->GetWorld() : nullptr;
		APlayerController* PC = Hud ? Hud->GetOwningPlayerController() : nullptr;
		if (!World || !PC)
		{
			return;
		}
		APawn* Pawn = PC->GetPawn();
		UPlayerCombatComponent* Player = Pawn ? Pawn->FindComponentByClass<UPlayerCombatComponent>() : nullptr;
		AMonsterCharacterBase* Mon = FindMonster(World);

		if (PC->WasInputKeyJustPressed(EKeys::F1))
		{
			bDevActive = !bDevActive;
			if (!bDevActive)
			{
				ResetAll(World, Player, Mon);
			}
			LastAction = bDevActive ? TEXT("開発者モード ON") : TEXT("開発者モード OFF（設定を元に戻しました）");
		}
		if (!bDevActive)
		{
			return;
		}

		// 継続効果（回避などで無敵が外れても毎フレーム戻す / 敵の再起動を抑える）。
		if (bPlayerInvincible && Player)
		{
			Player->SetInvulnerable(true);
		}

		auto Pressed = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };

		if (Pressed(EKeys::One) && Player && !Player->IsDead())
		{
			Player->DevForceDeath();
			LastAction = TEXT("1: プレイヤー死亡");
		}
		if (Pressed(EKeys::Two) && Mon)
		{
			if (bMonsterFrozen)
			{
				bMonsterFrozen = false;
				SetMonsterFrozen(Mon, false);
			}
			Mon->DebugTriggerDead();
			LastAction = TEXT("2: モンスター死亡");
		}
		if (Pressed(EKeys::Three) && Player)
		{
			bPlayerInvincible = !bPlayerInvincible;
			Player->SetInvulnerable(bPlayerInvincible);
			LastAction = FString::Printf(TEXT("3: プレイヤー無敵 %s"), bPlayerInvincible ? TEXT("ON") : TEXT("OFF"));
		}
		if (Pressed(EKeys::Four) && Mon)
		{
			bMonsterFrozen = !bMonsterFrozen;
			SetMonsterFrozen(Mon, bMonsterFrozen);
			LastAction = FString::Printf(TEXT("4: モンスター停止 %s"), bMonsterFrozen ? TEXT("ON") : TEXT("OFF"));
		}
		if (Pressed(EKeys::Five) && Player && !Player->IsDead())
		{
			Player->Heal(Player->MaxHp);
			Player->AddGauge(Player->MaxGauge);
			LastAction = TEXT("5: HP 全回復 + ゲージ MAX");
		}
		if (Pressed(EKeys::Six) && Player && !Player->IsDead())
		{
			const bool bWasInv = bPlayerInvincible;
			Player->SetInvulnerable(false);
			Player->TakeIncomingHit(Player->Defence + 5, /*bGuarded*/ false);
			Player->SetInvulnerable(bWasInv);
			LastAction = TEXT("6: プレイヤー被弾");
		}
		if (Pressed(EKeys::Seven) && Player && !Player->IsDead())
		{
			Player->BeginStun(2.f);
			LastAction = TEXT("7: プレイヤースタン（2 秒）");
		}
		if (Pressed(EKeys::Eight))
		{
			bSlowMotion = !bSlowMotion;
			ApplySlow(World);
			LastAction = FString::Printf(TEXT("8: スロー x%.2f %s"), SlowTimeScale, bSlowMotion ? TEXT("ON") : TEXT("OFF"));
		}
		if (Pressed(EKeys::Nine))
		{
			bPlayerInvincible = false;
			bMonsterFrozen = false;
			bSlowMotion = false;
			LastAction = TEXT("9: リスタート");
			UGameplayStatics::OpenLevel(World, FName(*UGameplayStatics::GetCurrentLevelName(World)));
		}
	}

	void Draw(AHUD* Hud, UCanvas* Canvas)
	{
		if (!bDevActive || !Hud || !Canvas || !GEngine)
		{
			return;
		}
		UFont* Font = GEngine->GetSmallFont();
		const TCHAR* Lines[] = {
			TEXT("[開発者モード]  F1: 閉じる"),
			TEXT("1: プレイヤー死亡   2: モンスター死亡"),
			TEXT("3: プレイヤー無敵   4: モンスター停止"),
			TEXT("5: HP全回復+ゲージMAX   6: プレイヤー被弾"),
			TEXT("7: プレイヤースタン   8: スロー   9: リスタート"),
			TEXT("敵: U=スタン I=やられ O=死亡 N=ガード被弾 M=通常被弾"),
		};
		const int32 N = UE_ARRAY_COUNT(Lines);
		const float LineH = 18.f;
		const float W = 460.f;
		const float H = (N + 2) * LineH + 12.f;
		const float X = Canvas->SizeX - W - 16.f;
		const float Y = 70.f;
		Hud->DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f), X, Y, W, H);
		Hud->DrawRect(FLinearColor(1.f, 0.3f, 0.3f, 0.9f), X, Y, 4.f, H);
		for (int32 i = 0; i < N; ++i)
		{
			Hud->DrawText(Lines[i], i == 0 ? FLinearColor(1.f, 0.4f, 0.4f) : FLinearColor::White, X + 12.f, Y + 6.f + i * LineH, Font);
		}
		const FString Status = FString::Printf(TEXT("無敵:%s  敵停止:%s  スロー:%s"),
			bPlayerInvincible ? TEXT("ON") : TEXT("off"), bMonsterFrozen ? TEXT("ON") : TEXT("off"), bSlowMotion ? TEXT("ON") : TEXT("off"));
		Hud->DrawText(Status, FLinearColor(0.6f, 1.f, 0.6f), X + 12.f, Y + 6.f + N * LineH, Font);
		if (!LastAction.IsEmpty())
		{
			Hud->DrawText(TEXT("> ") + LastAction, FLinearColor(1.f, 0.9f, 0.4f), X + 12.f, Y + 6.f + (N + 1) * LineH, Font);
		}
	}
}

#else // UE_BUILD_SHIPPING: 製品版では開発者モードを一切持たない。

namespace CounterCoreDevMode
{
	bool IsActive() { return false; }
	void Tick(AHUD*) {}
	void Draw(AHUD*, UCanvas*) {}
}

#endif
