#include "UI/CounterCoreHUD.h"
#include "Player/PlayerCombatComponent.h"
#include "Player/PlayerGuardComponent.h"
#include "Player/PlayerCameraComponent.h"
#include "Enemy/MonsterCombatComponent.h"
#include "Battle/BattleDirectorComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"

namespace
{
	static UFont* HudUIFont()
	{
		static TWeakObjectPtr<UFont> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		}
		return Cached.Get();
	}
}

void ACounterCoreHUD::BeginPlay()
{
	Super::BeginPlay();
	if (bRemoveLegacyWidgets)
	{
		// プレイヤーの BeginPlay で AddToViewport される旧 UI を、少し遅れて数回スイープして外す。
		LegacySweepsLeft = 6;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(LegacySweepTimer, this, &ACounterCoreHUD::SweepLegacyWidgets, 0.5f, true);
		}
	}
}

void ACounterCoreHUD::SweepLegacyWidgets()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* W = *It;
		if (!W || W->GetWorld() != World || !W->IsInViewport())
		{
			continue;
		}
		const FString ClassName = W->GetClass()->GetName();
		for (const FString& Needle : LegacyWidgetNameContains)
		{
			if (!Needle.IsEmpty() && ClassName.Contains(Needle))
			{
				W->RemoveFromParent();
				break;
			}
		}
	}
	if (--LegacySweepsLeft <= 0)
	{
		World->GetTimerManager().ClearTimer(LegacySweepTimer);
	}
}

AActor* ACounterCoreHUD::FindEnemy() const
{
	if (CachedEnemy.IsValid())
	{
		return CachedEnemy.Get();
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->FindComponentByClass<UMonsterCombatComponent>())
			{
				ACounterCoreHUD* Self = const_cast<ACounterCoreHUD*>(this);
				Self->CachedEnemy = *It;
				return *It;
			}
		}
	}
	return nullptr;
}

void ACounterCoreHUD::DrawBar(float X, float Y, float W, float H, float FillFrac, float DelayFrac,
	const FLinearColor& FillColor, const FLinearColor& DelayColor)
{
	FillFrac = FMath::Clamp(FillFrac, 0.f, 1.f);
	DelayFrac = FMath::Clamp(DelayFrac, FillFrac, 1.f);
	// 背景
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - 2, Y - 2, W + 4, H + 4);
	DrawRect(FLinearColor(0.12f, 0.12f, 0.12f, 0.9f), X, Y, W, H);
	// 遅延ダメージ（赤）
	if (DelayFrac > FillFrac)
	{
		DrawRect(DelayColor, X + W * FillFrac, Y, W * (DelayFrac - FillFrac), H);
	}
	// 現在値（緑）
	DrawRect(FillColor, X, Y, W * FillFrac, H);
}

void ACounterCoreHUD::DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale)
{
	// Scale 1.0 ≒ 18px。目標サイズで直接ラスタライズした Slate フォントで描く
	// （ビットマップフォントの拡大ボケを避ける）。
	if (UFont* UIFont = HudUIFont())
	{
		const int32 Px = FMath::Max(6, FMath::RoundToInt(18.f * Scale));
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text),
			FSlateFontInfo(UIFont, Px), Color);
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		if (Canvas)
		{
			Canvas->DrawItem(Item);
			return;
		}
	}
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(Text, Color, X, Y, Font, Scale);
}

void ACounterCoreHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}
	const float VW = Canvas->SizeX;
	const float VH = Canvas->SizeY;
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;

	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	UBattleDirectorComponent* BD = Player ? Player->FindComponentByClass<UBattleDirectorComponent>() : nullptr;

	// ---- バトルタイム: 画面左上、.00 秒単位 ----
	if (BD)
	{
		const float T = BD->GetElapsedTime();
		const int32 Min = FMath::FloorToInt(T / 60.f);
		const float Sec = T - Min * 60.f;
		DrawLabel(FString::Printf(TEXT("TIME  %02d:%05.2f"), Min, Sec), 28.f, 24.f, FLinearColor::White, 1.1f);
	}

	// ---- ラッシュ中の画面ティント ----
	if (Player)
	{
		if (UPlayerCombatComponent* RPC = Player->FindComponentByClass<UPlayerCombatComponent>())
		{
			if (RPC->bRushActive)
			{
				DrawRect(FLinearColor(1.f, 0.45f, 0.05f, 0.10f), 0.f, 0.f, VW, VH);
				DrawLabel(TEXT("R U S H"), (VW * 0.5f) - 70.f, 90.f, FLinearColor(1.f, 0.7f, 0.2f), 2.0f);
			}
		}
	}

	// ---- 開始演出 ----
	if (BD && BD->IsIntroPlaying())
	{
		DrawLabel(TEXT("READY..."), (VW * 0.5f) - 90.f, VH * 0.35f, FLinearColor(1.f, 1.f, 1.f), 2.6f);
	}

	// ---- 敵（ボス）HP: 緑バー + 遅延赤バー、画面上部中央 ----
	if (bShowEnemyHp)
	{
		if (AActor* Enemy = FindEnemy())
		{
			if (UMonsterCombatComponent* EC = Enemy->FindComponentByClass<UMonsterCombatComponent>())
			{
				const float Frac = EC->GetHpNormalized();
				if (EnemyHpDisplayed < 0.f)
				{
					EnemyHpDisplayed = Frac;
				}
				// 実 HP まで赤バーをゆっくり減らす（増加は即時）。
				if (Frac < EnemyHpDisplayed)
				{
					EnemyHpDisplayed = FMath::Max(Frac, EnemyHpDisplayed - DelayBarCatchupPerSec * Dt);
				}
				else
				{
					EnemyHpDisplayed = Frac;
				}

				const float BW = FMath::Min(760.f, VW * 0.6f);
				const float BX = (VW - BW) * 0.5f;
				const float BY = 46.f;
				DrawBar(BX, BY, BW, 20.f, Frac, EnemyHpDisplayed,
					FLinearColor(0.15f, 0.85f, 0.2f, 1.f), FLinearColor(0.85f, 0.15f, 0.15f, 0.9f));
				DrawLabel(FString::Printf(TEXT("BOSS  HP %d / %d"), EC->Status.Hp, EC->Status.MaxHp),
					BX, BY - 20.f, FLinearColor::White);
				// スタンゲージ（おまけ、細く）
				DrawBar(BX, BY + 24.f, BW, 6.f, EC->GetStunNormalized(), EC->GetStunNormalized(),
					FLinearColor(0.9f, 0.75f, 0.1f, 1.f), FLinearColor::Transparent);
			}
		}
	}

	if (!Player)
	{
		return;
	}
	UPlayerCombatComponent* PC = Player->FindComponentByClass<UPlayerCombatComponent>();
	UPlayerGuardComponent* PG = Player->FindComponentByClass<UPlayerGuardComponent>();

	// ---- プレイヤー HP: 緑バー + 遅延赤バー、画面下中央 ----
	if (bShowPlayerHp && PC)
	{
		const float Frac = PC->GetHpNormalized();
		if (PlayerHpDisplayed < 0.f) { PlayerHpDisplayed = Frac; }
		PlayerHpDisplayed = (Frac < PlayerHpDisplayed)
			? FMath::Max(Frac, PlayerHpDisplayed - DelayBarCatchupPerSec * Dt)
			: Frac;

		const float BW = FMath::Min(560.f, VW * 0.44f);
		const float BX = (VW - BW) * 0.5f;
		const float BY = VH - 52.f;
		DrawBar(BX, BY, BW, 18.f, Frac, PlayerHpDisplayed,
			FLinearColor(0.2f, 0.85f, 0.25f, 1.f), FLinearColor(0.85f, 0.15f, 0.15f, 0.9f));
		DrawLabel(FString::Printf(TEXT("HP %d / %d"), PC->Hp, PC->MaxHp), BX, BY - 18.f, FLinearColor::White);
	}

	// ---- プレイヤー攻撃ゲージ: 10 枠、画面下中央 ----
	if (bShowPlayerGauge && PC)
	{
		const int32 Max = FMath::Max(1, PC->MaxGauge);
		const float SegW = 26.f, SegH = 16.f, Gap = 3.f;
		const float TotalW = Max * SegW + (Max - 1) * Gap;
		const float GX = (VW - TotalW) * 0.5f;
		const float GY = VH - 116.f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), GX - 6, GY - 6, TotalW + 12, SegH + 12);
		for (int32 i = 0; i < Max; ++i)
		{
			const bool bFilled = i < PC->Gauge;
			const FLinearColor Col = bFilled
				? (PC->bRushActive ? FLinearColor(1.f, 0.55f, 0.1f, 1.f) : FLinearColor(0.2f, 0.6f, 1.f, 1.f))
				: FLinearColor(0.15f, 0.15f, 0.18f, 0.9f);
			DrawRect(Col, GX + i * (SegW + Gap), GY, SegW, SegH);
		}
		DrawLabel(FString::Printf(TEXT("GAUGE %d / %d%s"), PC->Gauge, Max, PC->bRushActive ? TEXT("  RUSH!") : TEXT("")),
			GX, GY - 20.f, FLinearColor(0.8f, 0.9f, 1.f));
	}

	// ---- ガード: 文字 + 盾ゲージ + ガード残り時間（サークルの代用バー）----
	if (bShowGuard && PG)
	{
		const float LX = 48.f;
		float LY = VH * 0.5f - 40.f;

		if (PG->IsGuarding())
		{
			DrawLabel(TEXT(">>> ガード中 <<<"), LX, LY, FLinearColor(0.3f, 0.8f, 1.f), 1.4f);
		}
		else if (PG->IsOnCooldown())
		{
			DrawLabel(TEXT("ガード クールタイム"), LX, LY, FLinearColor(1.f, 0.5f, 0.3f));
		}
		LY += 26.f;
		DrawLabel(FString::Printf(TEXT("盾 %d / %d"), FMath::RoundToInt(PG->ShieldDurability),
			FMath::RoundToInt(PG->MaxShieldDurability)), LX, LY, FLinearColor::White);
		LY += 18.f;
		DrawBar(LX, LY, 220.f, 14.f, PG->GetShieldNormalized(), PG->GetShieldNormalized(),
			FLinearColor(0.3f, 0.55f, 1.f, 1.f), FLinearColor::Transparent);
		LY += 22.f;
		DrawLabel(TEXT("ガード可能時間"), LX, LY, FLinearColor(0.7f, 0.7f, 0.7f), 0.9f);
		LY += 16.f;
		DrawBar(LX, LY, 220.f, 10.f, PG->GetGuardTimeNormalized(), PG->GetGuardTimeNormalized(),
			FLinearColor(0.9f, 0.85f, 0.3f, 1.f), FLinearColor::Transparent);
	}

	// ---- プレイヤー状態フラグ（気絶などが分かるように）----
	if (PC && PC->GetCombatState() == EPlayerCombatState::Stun)
	{
		DrawLabel(TEXT("気絶！"), (VW * 0.5f) - 40.f, VH * 0.5f, FLinearColor(1.f, 0.3f, 0.3f), 1.6f);
	}

	// ---- ジャストガード フラッシュ ----
	if (PG && GetWorld())
	{
		const float Since = GetWorld()->GetTimeSeconds() - PG->LastJustGuardTime;
		if (Since >= 0.f && Since < 0.8f)
		{
			const float A = FMath::Clamp(1.f - Since / 0.8f, 0.f, 1.f);
			DrawLabel(TEXT("JUST GUARD!"), (VW * 0.5f) - 90.f, VH * 0.38f, FLinearColor(0.4f, 0.9f, 1.f, A), 1.8f);
		}
	}

	// ---- ロックオン表示（点＋円、仕様書 UI「ターゲットロック」）----
	if (UPlayerCameraComponent* Cam = Player->FindComponentByClass<UPlayerCameraComponent>())
	{
		bool bValid = false;
		const FVector WorldLoc = Cam->GetLockReticleWorldLocation(bValid);
		if (bValid)
		{
			const FVector Proj = Project(WorldLoc);
			const FVector2D Screen(Proj.X, Proj.Y);
			if (Proj.Z > 0.f && Screen.X > 0.f && Screen.Y > 0.f && Screen.X < VW && Screen.Y < VH)
			{
				const FLinearColor Col(0.9f, 0.95f, 1.f, 0.9f);
				// 点
				DrawRect(Col, Screen.X - 3.f, Screen.Y - 3.f, 6.f, 6.f);
				// 円（線分で近似）
				const int32 Seg = 24;
				const float R = 26.f;
				for (int32 i = 0; i < Seg; ++i)
				{
					const float A0 = (2.f * PI * i) / Seg;
					const float A1 = (2.f * PI * (i + 1)) / Seg;
					DrawLine(Screen.X + R * FMath::Cos(A0), Screen.Y + R * FMath::Sin(A0),
						Screen.X + R * FMath::Cos(A1), Screen.Y + R * FMath::Sin(A1), Col, 1.5f);
				}
			}
		}
	}

	// ---- 決着表示 ----
	if (BD)
	{
		const EBattleResult R = BD->GetResult();
		if (R == EBattleResult::PlayerWin)
		{
			DrawLabel(TEXT("YOU WIN"), (VW * 0.5f) - 110.f, VH * 0.42f, FLinearColor(1.f, 0.9f, 0.3f), 3.0f);
		}
		else if (R == EBattleResult::PlayerLose)
		{
			DrawLabel(TEXT("YOU LOSE"), (VW * 0.5f) - 120.f, VH * 0.42f, FLinearColor(1.f, 0.3f, 0.3f), 3.0f);
		}
	}
}
