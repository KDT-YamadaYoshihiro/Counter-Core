#include "Title/TitleSceneController.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
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
}

void ATitleSceneController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	UpdateOrbit(DeltaSeconds);
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
