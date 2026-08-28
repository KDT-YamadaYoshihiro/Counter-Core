#include "UI/ResultHUD.h"
#include "Battle/BattleResultSubsystem.h"

#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

AResultHUD::AResultHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AResultHUD::BeginPlay()
{
	Super::BeginPlay();
	Elapsed = 0.f;

	if (bRemoveLegacyResultWidget)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(SweepTimer, this, &AResultHUD::SweepLegacyWidget, 0.15f, true);
		}
	}

	// リザルトは入力優先。マウスカーソルは出しておく（任意）。
	if (APlayerController* PC = GetPC())
	{
		FInputModeGameAndUI Mode;
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = false;
	}
}

void AResultHUD::SweepLegacyWidget()
{
	++SweepCount;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* W = *It;
		if (!W || !W->IsInViewport())
		{
			continue;
		}
		const FString Name = W->GetClass()->GetName();
		for (const FString& Frag : LegacyWidgetNameContains)
		{
			if (!Frag.IsEmpty() && Name.Contains(Frag))
			{
				W->RemoveFromParent();
				break;
			}
		}
	}
	if (SweepCount >= 20)
	{
		GetWorld()->GetTimerManager().ClearTimer(SweepTimer);
	}
}

APlayerController* AResultHUD::GetPC() const
{
	return GetOwningPlayerController() ? GetOwningPlayerController()
		: (GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);
}

UBattleResultSubsystem* AResultHUD::GetResultSys() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UBattleResultSubsystem>();
		}
	}
	return nullptr;
}

float AResultHUD::MenuDelay() const
{
	const UBattleResultSubsystem* R = GetResultSys();
	const bool bWon = R && R->bWon;
	return bWon ? WinMenuDelaySeconds : LossMenuDelaySeconds;
}

void AResultHUD::UpdateState()
{
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	Elapsed += Dt;

	if (!bMenuVisible && Elapsed >= MenuDelay())
	{
		bMenuVisible = true;
	}

	if (bExecuting)
	{
		return;
	}
	if (bDialogOpen)
	{
		HandleDialogInput();
	}
	else if (bMenuVisible)
	{
		HandleMenuInput();
	}
}

void AResultHUD::HandleMenuInput()
{
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}

	const bool bDown = PC->WasInputKeyJustPressed(EKeys::S) || PC->WasInputKeyJustPressed(EKeys::Down)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Down);
	const bool bUp = PC->WasInputKeyJustPressed(EKeys::W) || PC->WasInputKeyJustPressed(EKeys::Up)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Up);

	const int32 Count = 3;
	int32 Idx = (int32)Selected;
	if (bDown) { Idx = (Idx + 1) % Count; }
	if (bUp)   { Idx = (Idx + Count - 1) % Count; }
	Selected = (EResultMenuItem)Idx;

	// 決定（A / Enter / Space）→ 現在の選択でダイアログ。
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)
		|| PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		OpenDialogFor(Selected);
		return;
	}
	// 仕様: B = タイトル、X = ゲーム終了（直接指定 → ダイアログ）。
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right) || PC->WasInputKeyJustPressed(EKeys::BackSpace))
	{
		OpenDialogFor(EResultMenuItem::Title);
	}
	else if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left) || PC->WasInputKeyJustPressed(EKeys::X))
	{
		OpenDialogFor(EResultMenuItem::Quit);
	}
}

void AResultHUD::OpenDialogFor(EResultMenuItem Item)
{
	DialogItem = Item;
	Selected = Item;
	bDialogOpen = true;
	bDialogYes = false; // 既定は「いいえ」（誤爆防止）
}

void AResultHUD::HandleDialogInput()
{
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}

	if (PC->WasInputKeyJustPressed(EKeys::A) || PC->WasInputKeyJustPressed(EKeys::Left)
		|| PC->WasInputKeyJustPressed(EKeys::D) || PC->WasInputKeyJustPressed(EKeys::Right)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Right))
	{
		bDialogYes = !bDialogYes;
	}

	// キャンセル（B / Esc）
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right) || PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		bDialogOpen = false;
		return;
	}
	// 確定（A / Enter）
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)
		|| PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		if (bDialogYes)
		{
			ExecuteSelected();
		}
		else
		{
			bDialogOpen = false;
		}
	}
}

void AResultHUD::ExecuteSelected()
{
	if (bExecuting)
	{
		return;
	}
	bExecuting = true;

	switch (DialogItem)
	{
	case EResultMenuItem::Retry:
		if (UBattleResultSubsystem* R = GetResultSys()) { R->Clear(); }
		UGameplayStatics::OpenLevel(this, RetryLevelName);
		break;
	case EResultMenuItem::Title:
		if (UBattleResultSubsystem* R = GetResultSys()) { R->Clear(); }
		UGameplayStatics::OpenLevel(this, TitleLevelName);
		break;
	case EResultMenuItem::Quit:
		UKismetSystemLibrary::QuitGame(this, GetPC(), EQuitPreference::Quit, false);
		break;
	}
}

// --------------------------------------------------------------------------
// 描画
// --------------------------------------------------------------------------

void AResultHUD::DrawTextCentered(const FString& Text, float CX, float CY, float Scale, const FLinearColor& Color)
{
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font || !Canvas)
	{
		return;
	}
	float W = 0.f, H = 0.f;
	Canvas->TextSize(Font, Text, W, H, Scale, Scale);
	FCanvasTextItem Item(FVector2D(CX - W * 0.5f, CY - H * 0.5f), FText::FromString(Text), Font, Color);
	Item.Scale = FVector2D(Scale, Scale);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	Canvas->DrawItem(Item);
}

void AResultHUD::DrawTextLeft(const FString& Text, float X, float CY, float Scale, const FLinearColor& Color)
{
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font || !Canvas)
	{
		return;
	}
	float W = 0.f, H = 0.f;
	Canvas->TextSize(Font, Text, W, H, Scale, Scale);
	FCanvasTextItem Item(FVector2D(X, CY - H * 0.5f), FText::FromString(Text), Font, Color);
	Item.Scale = FVector2D(Scale, Scale);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	Canvas->DrawItem(Item);
}

void AResultHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	UpdateState();

	const float VW = Canvas->SizeX;
	const float VH = Canvas->SizeY;

	// 背景（不透明の暗幕。裏の残ウィジェットも隠す）。
	FCanvasTileItem BG(FVector2D(0, 0), FVector2D(VW, VH), FLinearColor(0.03f, 0.03f, 0.05f, 1.f));
	BG.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(BG);

	const UBattleResultSubsystem* R = GetResultSys();
	const bool bWon = R && R->bWon;
	const bool bHas = R && R->bHasResult;

	// --- 勝敗結果 ---
	const FString Headline = !bHas ? TEXT("RESULT")
		: (bWon ? TEXT("YOU WIN") : TEXT("LOSS"));
	DrawTextCentered(Headline, VW * 0.5f, VH * 0.16f, 4.0f,
		bWon ? FLinearColor(1.f, 0.92f, 0.4f, 1.f) : FLinearColor(1.f, 0.35f, 0.35f, 1.f));

	// 敗北時、選択項目が出るまでは「LOSS」だけ（仕様）。
	const bool bShowStats = bWon || bMenuVisible || !bHas;

	if (bShowStats && bHas)
	{
		// --- スコア（ランク）---
		const FString RankStr = UBattleResultSubsystem::RankToString(R->Rank);
		DrawTextCentered(TEXT("SCORE"), VW * 0.5f, VH * 0.30f, 1.4f, FLinearColor(0.7f, 0.7f, 0.75f, 1.f));
		DrawTextCentered(RankStr, VW * 0.5f, VH * 0.40f, 6.0f, FLinearColor(1.f, 1.f, 1.f, 1.f));

		// --- タイム / ガード成功回数（勝っても負けても表示）---
		DrawTextCentered(FString::Printf(TEXT("TIME    %s"), *R->GetTimeText()),
			VW * 0.5f, VH * 0.52f, 1.6f, FLinearColor::White);
		DrawTextCentered(FString::Printf(TEXT("GUARD   %d"), R->GuardSuccessCount),
			VW * 0.5f, VH * 0.58f, 1.6f, FLinearColor::White);
	}

	// --- 選択項目 ---
	if (bMenuVisible)
	{
		const TCHAR* Labels[3] = { TEXT("さいちょうせん"), TEXT("タイトルへ"), TEXT("ゲームしゅうりょう") };
		const float BaseY = VH * 0.70f;
		const float Step = VH * 0.07f;
		for (int32 i = 0; i < 3; ++i)
		{
			const bool bSel = ((int32)Selected == i);
			const FString Line = FString::Printf(TEXT("%s %s"), bSel ? TEXT("▶") : TEXT("  "), Labels[i]);
			DrawTextCentered(Line, VW * 0.5f, BaseY + Step * i, bSel ? 1.8f : 1.4f,
				bSel ? FLinearColor(1.f, 0.95f, 0.6f, 1.f) : FLinearColor(0.75f, 0.75f, 0.8f, 1.f));
		}

		DrawTextCentered(TEXT("A: けってい    B: タイトル    X: しゅうりょう"),
			VW * 0.5f, VH * 0.95f, 1.0f, FLinearColor(0.55f, 0.55f, 0.6f, 1.f));
	}

	// --- ダイアログ ---
	if (bDialogOpen)
	{
		FCanvasTileItem Dim(FVector2D(0, 0), FVector2D(VW, VH), FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Dim.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Dim);

		const float BoxW = VW * 0.5f, BoxH = VH * 0.24f;
		const float BoxX = (VW - BoxW) * 0.5f, BoxY = (VH - BoxH) * 0.5f;
		FCanvasTileItem Box(FVector2D(BoxX, BoxY), FVector2D(BoxW, BoxH), FLinearColor(0.1f, 0.1f, 0.14f, 1.f));
		Box.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Box);

		const TCHAR* Q[3] = { TEXT("さいちょうせん しますか？"), TEXT("タイトルへ もどりますか？"), TEXT("ゲームを しゅうりょう しますか？") };
		DrawTextCentered(Q[(int32)DialogItem], VW * 0.5f, BoxY + BoxH * 0.30f, 1.4f, FLinearColor::White);

		const FString Yes = FString::Printf(TEXT("%s はい"), bDialogYes ? TEXT("▶") : TEXT("  "));
		const FString No  = FString::Printf(TEXT("%s いいえ"), !bDialogYes ? TEXT("▶") : TEXT("  "));
		DrawTextCentered(Yes, VW * 0.42f, BoxY + BoxH * 0.68f, 1.4f,
			bDialogYes ? FLinearColor(1.f, 0.95f, 0.6f, 1.f) : FLinearColor(0.7f, 0.7f, 0.75f, 1.f));
		DrawTextCentered(No, VW * 0.58f, BoxY + BoxH * 0.68f, 1.4f,
			!bDialogYes ? FLinearColor(1.f, 0.95f, 0.6f, 1.f) : FLinearColor(0.7f, 0.7f, 0.75f, 1.f));
	}
}
