#include "Title/TitleHUD.h"
#include "Title/TitleSceneController.h"

#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	static UFont* TitleUIFont()
	{
		static TWeakObjectPtr<UFont> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		}
		return Cached.Get();
	}
}

ATitleSceneController* ATitleHUD::GetController()
{
	if (!Cached)
	{
		for (TActorIterator<ATitleSceneController> It(GetWorld()); It; ++It)
		{
			Cached = *It;
			break;
		}
	}
	return Cached;
}

void ATitleHUD::DrawStr(const FString& Text, float X, float Y, int32 PixelSize, const FLinearColor& Color, bool bCenterX)
{
	if (!Canvas)
	{
		return;
	}
	UFont* F = TitleUIFont();
	if (F)
	{
		const FSlateFontInfo Font(F, FMath::Clamp(PixelSize, 6, 200));
		float W = 0.f;
		if (FSlateApplication::IsInitialized())
		{
			W = (float)FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
		}
		FCanvasTextItem Item(FVector2D(bCenterX ? X - W * 0.5f : X, Y), FText::FromString(Text), Font, Color);
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.85f));
		Canvas->DrawItem(Item);
		return;
	}
	DrawText(Text, Color, X, Y, GEngine ? GEngine->GetLargeFont() : nullptr, PixelSize / 14.f);
}

void ATitleHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const float VW = Canvas->SizeX;
	const float VH = Canvas->SizeY;
	ATitleSceneController* Ctl = GetController();
	const bool bPrompt = Ctl && Ctl->IsQuitPromptOpen();

	// ☰ アイコン（左上）
	{
		const float IX = 20.f, IY = 18.f, IW = 34.f, IH = 30.f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, bPrompt ? 0.8f : 0.45f), IX, IY, IW, IH);
		for (int32 i = 0; i < 3; ++i)
		{
			DrawRect(FLinearColor::White, IX + 7.f, IY + 7.f + i * 8.f, IW - 14.f, 3.f);
		}
		DrawStr(TEXT("Esc / Start : ゲーム終了"), IX + IW + 10.f, IY + 4.f,
			FMath::RoundToInt(VH * 0.022f), FLinearColor(0.85f, 0.85f, 0.9f, 0.9f), false);
	}

	// 終了確認ダイアログ
	if (bPrompt)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.72f), 0.f, 0.f, VW, VH);

		const float BW = VW * 0.44f, BH = VH * 0.24f;
		DrawRect(FLinearColor(0.09f, 0.09f, 0.12f, 0.98f), (VW - BW) * 0.5f, (VH - BH) * 0.5f, BW, BH);

		DrawStr(TEXT("ゲームを終了しますか？"), VW * 0.5f, VH * 0.42f, FMath::RoundToInt(VH * 0.045f), FLinearColor::White, true);

		const bool bYes = Ctl->IsQuitPromptYes();
		DrawStr(bYes ? TEXT("＞ はい") : TEXT("  はい"), VW * 0.42f, VH * 0.54f, FMath::RoundToInt(VH * 0.04f),
			bYes ? FLinearColor(1.f, 0.86f, 0.f) : FLinearColor::White, true);
		DrawStr(!bYes ? TEXT("＞ いいえ") : TEXT("  いいえ"), VW * 0.58f, VH * 0.54f, FMath::RoundToInt(VH * 0.04f),
			!bYes ? FLinearColor(1.f, 0.86f, 0.f) : FLinearColor::White, true);
	}
}
