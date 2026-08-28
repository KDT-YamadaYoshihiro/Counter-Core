#include "Title/TitleSceneController.h"
#include "Title/TitleHUD.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Blueprint/UserWidget.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATitleSceneController::ATitleSceneController()
{
	PrimaryActorTick.bCanEverTick = true;
}

APlayerController* ATitleSceneController::GetPC() const
{
	return GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
}

void ATitleSceneController::BeginPlay()
{
	Super::BeginPlay();
	PivotWorld = GetActorLocation();
	Angle = StartAngleDegrees;
	SetupCamera();
}

void ATitleSceneController::SetupCamera()
{
	APlayerController* PC = GetPC();
	if (!PC)
	{
		// Possess 前などは少し待って再試行。
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(SetupRetryTimer, this, &ATitleSceneController::SetupCamera, 0.15f, false);
		}
		return;
	}

	if (!OrbitCam)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		OrbitCam = GetWorld()->SpawnActor<ACameraActor>(PivotWorld, FRotator::ZeroRotator, Params);
		if (OrbitCam)
		{
#if WITH_EDITOR
			OrbitCam->SetActorLabel(TEXT("TitleOrbitCamera"));
#endif
			if (UCameraComponent* Cam = OrbitCam->GetCameraComponent())
			{
				Cam->SetFieldOfView(CameraFOV);
				Cam->bConstrainAspectRatio = false;
			}
		}
	}

	UpdateOrbit(0.f);

	if (OrbitCam)
	{
		PC->SetViewTargetWithBlend(OrbitCam, 0.f);
	}

	// GM の HUDClass に依存せず、確実に TitleHUD を出す（☰ / 終了ダイアログ描画用）。
	if (!PC->GetHUD() || !PC->GetHUD()->IsA(ATitleHUD::StaticClass()))
	{
		PC->ClientSetHUD(ATitleHUD::StaticClass());
	}
}

void ATitleSceneController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	UpdateOrbit(DeltaSeconds);
	PollQuitInput();
}

void ATitleSceneController::SetTitleWidgetHidden(bool bWantHidden)
{
	// 終了確認中は WBP_Title をビューポートから外す。
	// （隠すだけだと WBP_Title 側の A ボタン InputKey がまだ発火してしまうため。）
	if (bWantHidden)
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* W = *It;
			if (W && W->IsInViewport() && W->GetClass()->GetName().Contains(TEXT("Title")))
			{
				CachedTitleWidget = W;
				W->RemoveFromParent();
				break;
			}
		}
	}
	else if (CachedTitleWidget)
	{
		CachedTitleWidget->AddToViewport();
		CachedTitleWidget = nullptr;
	}
}

void ATitleSceneController::PollQuitInput()
{
	if (!bAllowQuit)
	{
		return;
	}
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}

	if (bQuitPromptOpen)
	{
		if (PC->WasInputKeyJustPressed(EKeys::Left) || PC->WasInputKeyJustPressed(EKeys::Right)
			|| PC->WasInputKeyJustPressed(EKeys::A) || PC->WasInputKeyJustPressed(EKeys::D)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Right))
		{
			bQuitPromptYes = !bQuitPromptYes;
		}

		const bool bConfirm = PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar)
			|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
		const bool bCancel = PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right);

		if (bCancel)
		{
			bQuitPromptOpen = false;
			SetTitleWidgetHidden(false);
		}
		else if (bConfirm)
		{
			if (bQuitPromptYes)
			{
				UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
			}
			else
			{
				bQuitPromptOpen = false;
				SetTitleWidgetHidden(false);
			}
		}
		return;
	}

	// ☰ / Esc / Start で終了確認を開く。
	if (PC->WasInputKeyJustPressed(EKeys::Escape)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left))
	{
		bQuitPromptOpen = true;
		bQuitPromptYes = false;
		SetTitleWidgetHidden(true);
	}
}

void ATitleSceneController::UpdateOrbit(float DeltaSeconds)
{
	Angle += OrbitDegreesPerSecond * DeltaSeconds;

	const float Rad = FMath::DegreesToRadians(Angle);
	const float Bob = (BobAmplitude > 0.f)
		? FMath::Sin(Elapsed / FMath::Max(0.1f, BobPeriod) * 2.f * PI) * BobAmplitude
		: 0.f;

	const FVector CamLoc = PivotWorld + FVector(
		FMath::Cos(Rad) * OrbitRadius,
		FMath::Sin(Rad) * OrbitRadius,
		OrbitHeight + Bob);
	const FVector LookAt = PivotWorld + FVector(0.f, 0.f, LookAtHeightOffset);
	const FRotator CamRot = (LookAt - CamLoc).Rotation();

	if (OrbitCam)
	{
		OrbitCam->SetActorLocationAndRotation(CamLoc, CamRot);
	}
}
