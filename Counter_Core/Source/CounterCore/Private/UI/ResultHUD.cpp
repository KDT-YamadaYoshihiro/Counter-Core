#include "UI/ResultHUD.h"
#include "Battle/BattleResultSubsystem.h"

#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	// UE 既定の UI フォント（Runtime。日本語フォールバックあり）。実サイズで直接
	// ラスタライズして描くとキャンバスでも綺麗に出る（ビットマップ拡大にならない）。
	static UFont* GetUIFont()
	{
		static TWeakObjectPtr<UFont> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		}
		return Cached.Get();
	}

	static FSlateFontInfo MakeFont(int32 PixelSize, bool bBold)
	{
		return FSlateFontInfo(GetUIFont(), FMath::Max(1, PixelSize), bBold ? TEXT("Bold") : TEXT("Regular"));
	}
}
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
	if (SweepCount >= 20 && GetWorld())
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

// --------------------------------------------------------------------------

void AResultHUD::UpdateState()
{
	Elapsed += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;

	const UBattleResultSubsystem* R = GetResultSys();
	const bool bWon = R && R->bWon;
	const float Delay = bWon ? WinInputDelaySeconds : LossInputDelaySeconds;
	if (!bInputArmed && Elapsed >= Delay)
	{
		bInputArmed = true;
	}

	if (bExecuting || !bInputArmed)
	{
		return;
	}
	HandleInput();
}

void AResultHUD::HandleInput()
{
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}

	if (bDialogOpen)
	{
		if (PC->WasInputKeyJustPressed(EKeys::Left) || PC->WasInputKeyJustPressed(EKeys::Right)
			|| PC->WasInputKeyJustPressed(EKeys::A) || PC->WasInputKeyJustPressed(EKeys::D)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Right))
		{
			bDialogYes = !bDialogYes;
		}
		if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right) || PC->WasInputKeyJustPressed(EKeys::Escape))
		{
			bDialogOpen = false;
			return;
		}
		if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)
			|| PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar))
		{
			if (bDialogYes) { Execute(); }
			else { bDialogOpen = false; }
		}
		return;
	}

	// 仕様: A = 再挑戦(バトル)、B = タイトル、X = ゲーム終了。押すと確認ダイアログ。
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || PC->WasInputKeyJustPressed(EKeys::Enter))
	{
		OpenDialog(EResultChoice::Retry);
	}
	else if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right) || PC->WasInputKeyJustPressed(EKeys::BackSpace))
	{
		OpenDialog(EResultChoice::Title);
	}
	else if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left) || PC->WasInputKeyJustPressed(EKeys::X))
	{
		OpenDialog(EResultChoice::Quit);
	}
}

void AResultHUD::OpenDialog(EResultChoice Choice)
{
	DialogChoice = Choice;
	bDialogOpen = true;
	bDialogYes = false; // 既定「いいえ」（誤爆防止）
}

void AResultHUD::Execute()
{
	if (bExecuting)
	{
		return;
	}
	bExecuting = true;

	if (UBattleResultSubsystem* R = GetResultSys())
	{
		if (DialogChoice != EResultChoice::Quit) { R->Clear(); }
	}

	switch (DialogChoice)
	{
	case EResultChoice::Retry: UGameplayStatics::OpenLevel(this, RetryLevelName); break;
	case EResultChoice::Title: UGameplayStatics::OpenLevel(this, TitleLevelName); break;
	case EResultChoice::Quit:  UKismetSystemLibrary::QuitGame(this, GetPC(), EQuitPreference::Quit, false); break;
	default: bExecuting = false; break;
	}
}

// --------------------------------------------------------------------------
// 描画
// --------------------------------------------------------------------------

float AResultHUD::MeasureWidth(const FString& Text, int32 PixelSize, bool bBold) const
{
	const FSlateFontInfo Font = MakeFont(PixelSize, bBold);
	if (FSlateApplication::IsInitialized())
	{
		return (float)FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
	}
	return Text.Len() * PixelSize * 0.5f;
}

void AResultHUD::DrawStr(const FString& Text, float X, float Y, int32 PixelSize, const FLinearColor& Color,
	bool bCenterX, bool bCenterY, bool bBold)
{
	if (!Canvas)
	{
		return;
	}
	PixelSize = FMath::Clamp(PixelSize, 6, 200); // Slate フォントアトラスの上限を考慮
	UFont* UIFont = GetUIFont();

	if (UIFont)
	{
		const FSlateFontInfo Font = MakeFont(PixelSize, bBold);
		float W = 0.f, H = (float)PixelSize;
		if (FSlateApplication::IsInitialized())
		{
			const FVector2D Sz = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
			W = (float)Sz.X; H = (float)Sz.Y;
		}
		FCanvasTextItem Item(FVector2D(bCenterX ? X - W * 0.5f : X, bCenterY ? Y - H * 0.5f : Y),
			FText::FromString(Text), Font, Color);
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.85f));
		Canvas->DrawItem(Item);
		return;
	}

	// フォールバック: 従来のビットマップフォント + スケール。
	UFont* Fallback = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Fallback)
	{
		return;
	}
	const float Scale = PixelSize / 14.f;
	float W = 0.f, H = 0.f;
	Canvas->TextSize(Fallback, Text, W, H, Scale, Scale);
	FCanvasTextItem Item(FVector2D(bCenterX ? X - W * 0.5f : X, bCenterY ? Y - H * 0.5f : Y),
		FText::FromString(Text), Fallback, Color);
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

	// 背景うっすら暗幕（不透明にはしない＝奥のシーンが見える）。
	if (DimAlpha > 0.f)
	{
		FCanvasTileItem Dim(FVector2D(0, 0), FVector2D(VW, VH), FLinearColor(0.f, 0.f, 0.f, DimAlpha));
		Dim.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Dim);
	}

	auto Px = [VH](float Frac) { return FMath::RoundToInt(VH * Frac); };

	const UBattleResultSubsystem* R = GetResultSys();
	const bool bHas = R && R->bHasResult;
	const bool bWon = R && R->bWon;

	if (bWon)
	{
		// "VICTORY" 上中央（白）
		DrawStr(TEXT("VICTORY"), VW * 0.5f, VH * 0.10f, Px(0.085f), FLinearColor::White, true, false, true);

		// スコア（ランク）: 左側に "スコア" 小ラベル + 大きな黄色い文字
		DrawStr(TEXT("スコア"), VW * 0.09f, VH * 0.26f, Px(0.038f), FLinearColor::White, false, false);
		const FString RankStr = bHas ? UBattleResultSubsystem::RankToString(R->Rank) : TEXT("-");
		DrawStr(RankStr, VW * 0.16f, VH * 0.34f, Px(0.30f), FLinearColor(1.f, 0.86f, 0.f, 1.f), true, false, true);

		// タイム: 中央右に "タイム" ラベル + MM:SS（.cc は小さめ）
		DrawStr(TEXT("タイム"), VW * 0.52f, VH * 0.42f, Px(0.05f), FLinearColor::White, false, false);
		if (bHas)
		{
			const FString T = R->GetTimeText();              // "MM:SS.cc"
			FString MMSS = T, Centis = TEXT("");
			int32 Dot;
			if (T.FindChar('.', Dot)) { MMSS = T.Left(Dot); Centis = T.Mid(Dot); }
			const int32 BigPx = Px(0.08f);
			DrawStr(MMSS, VW * 0.55f, VH * 0.50f, BigPx, FLinearColor::White, false, false, true);
			const float MW = MeasureWidth(MMSS, BigPx, true);
			DrawStr(Centis, VW * 0.55f + MW + 6.f, VH * 0.50f + BigPx * 0.42f, Px(0.038f), FLinearColor::White, false, false);

			// ガード成功回数（仕様書テキストに記載。小さめに添える）
			DrawStr(FString::Printf(TEXT("ガード成功  %d"), R->GuardSuccessCount),
				VW * 0.55f, VH * 0.63f, Px(0.028f), FLinearColor(0.85f, 0.85f, 0.85f, 1.f), false, false);
		}
	}
	else
	{
		// "LOSS" 中央（暗い赤）
		DrawStr(TEXT("LOSS"), VW * 0.5f, VH * 0.45f, Px(0.13f), FLinearColor(0.72f, 0.06f, 0.06f, 1.f), true, true, true);

		// 仕様: 「スコアとタイムは勝っても負けても表示」。操作受付後に控えめに出す。
		if (bInputArmed && bHas)
		{
			DrawStr(FString::Printf(TEXT("タイム  %s      スコア  D"), *R->GetTimeText()),
				VW * 0.5f, VH * 0.58f, Px(0.03f), FLinearColor(0.85f, 0.85f, 0.85f, 1.f), true, false);
		}
	}

	// 右下: ボタンヒント（操作受付後）
	if (bInputArmed && !bDialogOpen)
	{
		DrawStr(TEXT("A 再挑戦     B タイトルへ     X ゲーム終了"),
			VW * 0.5f, VH * 0.92f, Px(0.032f), FLinearColor::White, true, false);
	}

	// 確認ダイアログ
	if (bDialogOpen)
	{
		FCanvasTileItem D2(FVector2D(0, 0), FVector2D(VW, VH), FLinearColor(0.f, 0.f, 0.f, 0.5f));
		D2.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(D2);

		const float BW = VW * 0.5f, BH = VH * 0.22f;
		FCanvasTileItem Box(FVector2D((VW - BW) * 0.5f, (VH - BH) * 0.5f), FVector2D(BW, BH), FLinearColor(0.1f, 0.1f, 0.13f, 0.96f));
		Box.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Box);

		const TCHAR* Q =
			DialogChoice == EResultChoice::Retry ? TEXT("再挑戦しますか？") :
			DialogChoice == EResultChoice::Title ? TEXT("タイトルへ戻りますか？") :
			TEXT("ゲームを終了しますか？");
		DrawStr(Q, VW * 0.5f, VH * 0.44f, Px(0.04f), FLinearColor::White, true, false);
		DrawStr(bDialogYes ? TEXT("＞ はい") : TEXT("  はい"), VW * 0.43f, VH * 0.54f, Px(0.038f),
			bDialogYes ? FLinearColor(1.f, 0.86f, 0.f, 1.f) : FLinearColor::White, true, false);
		DrawStr(!bDialogYes ? TEXT("＞ いいえ") : TEXT("  いいえ"), VW * 0.57f, VH * 0.54f, Px(0.038f),
			!bDialogYes ? FLinearColor(1.f, 0.86f, 0.f, 1.f) : FLinearColor::White, true, false);
	}
}
