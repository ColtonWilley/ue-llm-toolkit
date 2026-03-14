// Copyright Natali Caggiano. All Rights Reserved.

#include "PIESequenceRunner.h"
#include "PIEFrameGrabber.h"
#include "SequencerController.h"
#include "UnrealClaudeModule.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "UnrealClient.h"
#include "InputAction.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "RenderingThread.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "GenericPlatform/GenericWindow.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

static TSharedPtr<SWindow> GetPIEWindow()
{
	if (!GEditor) return nullptr;

	for (auto& Pair : GEditor->SlatePlayInEditorMap)
	{
		TSharedPtr<SWindow> PIEWin = Pair.Value.SlatePlayInEditorWindow.Pin();
		if (PIEWin.IsValid())
			return PIEWin;
	}

	if (FSlateApplication::IsInitialized())
	{
		TArray<TSharedRef<SWindow>> AllWindows;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(AllWindows);
		for (const TSharedRef<SWindow>& Win : AllWindows)
		{
			if (Win->GetTitle().ToString().Contains(TEXT("Unreal Editor")))
				return Win;
		}
	}
	return nullptr;
}

static void ForceWindowToForeground(TSharedRef<SWindow> Window)
{
	Window->HACK_ForceToFront();

#if PLATFORM_WINDOWS
	TSharedPtr<FGenericWindow> NativeWindow = Window->GetNativeWindow();
	if (!NativeWindow.IsValid()) return;

	HWND TargetHwnd = reinterpret_cast<HWND>(NativeWindow->GetOSWindowHandle());
	if (!TargetHwnd) return;

	HWND ForegroundHwnd = ::GetForegroundWindow();
	if (ForegroundHwnd != TargetHwnd && ForegroundHwnd != nullptr)
	{
		DWORD ForegroundThread = ::GetWindowThreadProcessId(ForegroundHwnd, nullptr);
		DWORD OurThread = ::GetCurrentThreadId();
		if (ForegroundThread != OurThread)
		{
			::AttachThreadInput(OurThread, ForegroundThread, 1);
			::SetForegroundWindow(TargetHwnd);
			::SetFocus(TargetHwnd);
			::AttachThreadInput(OurThread, ForegroundThread, 0);
		}
	}
#endif
}

FPIESequenceRunner::FPIESequenceRunner()
{
}

FPIESequenceRunner::~FPIESequenceRunner()
{
	UnbindEndPIEDelegate();
	StopAllTapes();
	Cancel();
}

void FPIESequenceRunner::BindEndPIEDelegate()
{
	if (!EndPIEDelegateHandle.IsValid())
	{
		EndPIEDelegateHandle = FEditorDelegates::EndPIE.AddRaw(this, &FPIESequenceRunner::OnPIEEnded);
	}
}

void FPIESequenceRunner::UnbindEndPIEDelegate()
{
	if (EndPIEDelegateHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(EndPIEDelegateHandle);
		EndPIEDelegateHandle.Reset();
	}
}

void FPIESequenceRunner::OnPIEEnded(bool bIsSimulating)
{
	if (!IsRunning())
	{
		UnbindEndPIEDelegate();
		return;
	}

	int32 StepAtInterrupt = StepsCompleted;
	int32 TotalStepCount = Steps.Num();

	UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: PIE ended externally while sequence was at step %d/%d"),
		StepAtInterrupt, TotalStepCount);

	if (PIEPollTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PIEPollTickerHandle);
		PIEPollTickerHandle.Reset();
	}

	StopAllHolds();
	StopAllTapes();
	MaxDurationTimerHandle.Invalidate();

	if (FrameGrabber.IsValid())
	{
		FrameGrabber->StopCapture();
		CapturedFiles = FrameGrabber->GetCapturedFilePaths();
		FrameGrabber.Reset();
	}

	State = EPIESequenceState::Failed;
	ErrorMessage = FString::Printf(TEXT("PIE ended externally (step %d/%d)"), StepAtInterrupt, TotalStepCount);
	EndTime = FPlatformTime::Seconds();

	AddDiagEntry(TEXT("pie_ended_externally"));

	if (bAutoStopPIE && !OutputDir.IsEmpty())
	{
		WriteManifest();
	}

	UnbindEndPIEDelegate();
}

void FPIESequenceRunner::Start(
	const TArray<FPIESequenceStep>& InSteps,
	const FString& InCaptureMode,
	int32 InCaptureIntervalMs,
	int32 InCaptureEveryNFrames,
	const FString& InOutputDir)
{
	if (IsRunning())
	{
		Cancel();
	}

	Steps = InSteps;
	CaptureMode = InCaptureMode;
	CaptureIntervalMs = FMath::Max(50, InCaptureIntervalMs);
	CaptureEveryNFrames = FMath::Max(1, InCaptureEveryNFrames);
	OutputDir = InOutputDir;
	CurrentStepIndex = 0;
	StepsCompleted = 0;
	CapturedFiles.Empty();
	CaptureEntries.Empty();
	NormalCaptureCount = 0;
	ErrorMessage.Empty();
	SettleMs = 0;
	bAutoStartPIE = false;
	bAutoStopPIE = false;
	PreLoadedActions.Empty();
	ActiveHolds.Empty();
	ScheduledStepHandles.Empty();
	DiagLog.Empty();
	StartTime = FPlatformTime::Seconds();
	EndTime = 0.0;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*OutputDir))
	{
		PlatformFile.CreateDirectoryTree(*OutputDir);
	}

	if (!GEditor)
	{
		OnSequenceFailed(TEXT("Editor not available"));
		return;
	}

	BindEndPIEDelegate();

	if (GEditor->IsPlaySessionInProgress())
	{
		State = EPIESequenceState::Running;

		if (CaptureMode == TEXT("high_speed"))
		{
			TSharedPtr<FSceneViewport> SceneVP = GetPIESceneViewport();
			if (SceneVP.IsValid())
			{
				FrameGrabber = MakeShared<FPIEFrameGrabber>();
				FrameGrabber->StartCapture(SceneVP.ToSharedRef(), CaptureEveryNFrames, OutputDir);
			}
		}
		else
		{
			StartNormalCapture();
		}

		ExecuteNextStep();
	}
	else
	{
		State = EPIESequenceState::WaitingForPIE;

		TWeakPtr<FPIESequenceRunner> WeakSelf = AsShared();
		PIEPollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakSelf](float DeltaTime) -> bool
			{
				TSharedPtr<FPIESequenceRunner> Self = WeakSelf.Pin();
				if (!Self.IsValid()) return false;
				Self->PollPIEReady();
				return Self->State == EPIESequenceState::WaitingForPIE;
			}),
			0.1f);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Started with %d steps, capture_mode=%s"),
		Steps.Num(), *CaptureMode);
}

void FPIESequenceRunner::StartRunSequence(
	const TArray<FPIESequenceStep>& InSteps,
	int32 InSettleMs,
	const FString& InOutputDir,
	const TMap<FString, TWeakObjectPtr<UInputAction>>& InPreLoadedActions,
	int32 InAutoCapEveryNFrames,
	int32 InMaxDurationMs,
	bool bInTakeRecord,
	const FString& InTakeSlate)
{
	if (IsRunning())
	{
		Cancel();
	}

	Steps = InSteps;
	SettleMs = FMath::Max(0, InSettleMs);
	OutputDir = InOutputDir;
	PreLoadedActions = InPreLoadedActions;
	AutoCaptureEveryNFrames = FMath::Max(0, InAutoCapEveryNFrames);
	MaxDurationMs = FMath::Max(0, InMaxDurationMs);
	CaptureMode = TEXT("none");
	CaptureIntervalMs = 0;
	CaptureEveryNFrames = 0;
	CurrentStepIndex = 0;
	StepsCompleted = 0;
	CapturedFiles.Empty();
	CaptureEntries.Empty();
	NormalCaptureCount = 0;
	ErrorMessage.Empty();
	bAutoStartPIE = true;
	bAutoStopPIE = true;
	ActiveHolds.Empty();
	StopAllTapes();
	ScheduledStepHandles.Empty();
	DiagLog.Empty();
	StartTime = FPlatformTime::Seconds();
	EndTime = 0.0;
	bTakeRecord = bInTakeRecord;
	TakeSlate = InTakeSlate;
	TakeSequencePath.Empty();

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*OutputDir))
	{
		PlatformFile.CreateDirectoryTree(*OutputDir);
	}

	if (!GEditor)
	{
		OnSequenceFailed(TEXT("Editor not available"));
		return;
	}

	BindEndPIEDelegate();

	if (GEditor->IsPlaySessionInProgress())
	{
		GEditor->RequestEndPlayMap();
		FPlatformProcess::Sleep(0.5f);
	}

	FRequestPlaySessionParams SessionParams;
	SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
	GEditor->RequestPlaySession(SessionParams);

	State = EPIESequenceState::WaitingForPIE;

	TWeakPtr<FPIESequenceRunner> WeakSelf = AsShared();
	PIEPollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakSelf](float DeltaTime) -> bool
		{
			TSharedPtr<FPIESequenceRunner> Self = WeakSelf.Pin();
			if (!Self.IsValid()) return false;
			Self->PollPIEReady();
			return Self->State == EPIESequenceState::WaitingForPIE;
		}),
		0.1f);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: run_sequence started with %d steps, settle=%dms, output=%s"),
		Steps.Num(), SettleMs, *OutputDir);
}

void FPIESequenceRunner::Cancel()
{
	if (State == EPIESequenceState::Idle || State == EPIESequenceState::Completed || State == EPIESequenceState::Failed)
	{
		return;
	}

	if (PIEPollTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PIEPollTickerHandle);
		PIEPollTickerHandle.Reset();
	}

	FTimerManager* TimerMgr = GetPIETimerManager();
	if (TimerMgr)
	{
		if (StepTimerHandle.IsValid())
		{
			TimerMgr->ClearTimer(StepTimerHandle);
		}
		if (NormalCaptureHandle.IsValid())
		{
			TimerMgr->ClearTimer(NormalCaptureHandle);
		}
		if (SettleTimerHandle.IsValid())
		{
			TimerMgr->ClearTimer(SettleTimerHandle);
		}
		if (CompletionCheckHandle.IsValid())
		{
			TimerMgr->ClearTimer(CompletionCheckHandle);
		}
		if (MaxDurationTimerHandle.IsValid())
		{
			TimerMgr->ClearTimer(MaxDurationTimerHandle);
		}
		for (FTimerHandle& Handle : ScheduledStepHandles)
		{
			if (Handle.IsValid())
			{
				TimerMgr->ClearTimer(Handle);
			}
		}
	}
	ScheduledStepHandles.Empty();

	StopAllHolds();
	StopAllTapes();

	if (FrameGrabber.IsValid())
	{
		FrameGrabber->StopCapture();
		CapturedFiles = FrameGrabber->GetCapturedFilePaths();
		FrameGrabber.Reset();
	}

	State = EPIESequenceState::Failed;
	ErrorMessage = TEXT("Cancelled");
	EndTime = FPlatformTime::Seconds();

	UnbindEndPIEDelegate();

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Cancelled"));
}

FPIESequenceResult FPIESequenceRunner::GetStatus() const
{
	FPIESequenceResult Result;
	Result.State = State;
	Result.CurrentStep = bAutoStopPIE ? StepsCompleted : CurrentStepIndex;
	Result.TotalSteps = Steps.Num();
	Result.ErrorMessage = ErrorMessage;
	Result.StartTime = StartTime;
	Result.EndTime = EndTime;
	Result.Captures = CaptureEntries;

	if (FrameGrabber.IsValid())
	{
		Result.CapturedFrames = FrameGrabber->GetCapturedFrameCount();
		Result.CapturedFiles = FrameGrabber->GetCapturedFilePaths();
	}
	else
	{
		Result.CapturedFrames = CaptureEntries.Num() > 0 ? CaptureEntries.Num() : NormalCaptureCount;
		Result.CapturedFiles = CapturedFiles;
	}

	return Result;
}

void FPIESequenceRunner::PollPIEReady()
{
	if (!GEditor || State != EPIESequenceState::WaitingForPIE)
	{
		return;
	}

	if (GEditor->IsPlaySessionInProgress())
	{
		PIEPollTickerHandle.Reset();

		if (bAutoStartPIE)
		{
			BeginSettle();
		}
		else
		{
			State = EPIESequenceState::Running;

			if (CaptureMode == TEXT("high_speed"))
			{
				TSharedPtr<FSceneViewport> SceneVP = GetPIESceneViewport();
				if (SceneVP.IsValid())
				{
					FrameGrabber = MakeShared<FPIEFrameGrabber>();
					FrameGrabber->StartCapture(SceneVP.ToSharedRef(), CaptureEveryNFrames, OutputDir);
				}
			}
			else
			{
				StartNormalCapture();
			}

			ExecuteNextStep();
		}
	}
}

void FPIESequenceRunner::BeginSettle()
{
	State = EPIESequenceState::Settling;

	TSharedPtr<SWindow> PIEWin = GetPIEWindow();
	if (PIEWin.IsValid())
	{
		ForceWindowToForeground(PIEWin.ToSharedRef());
		FSlateApplication::Get().SetAllUserFocus(PIEWin.ToSharedRef(), EFocusCause::SetDirectly);
		UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Forced editor window to foreground"));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (PIEWorld)
	{
		GEngine->Exec(PIEWorld, TEXT("t.MaxFPS 60"));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: PIE ready, settling for %dms"), SettleMs);
	LogPlayerState(TEXT("PIE_READY"));
	AddDiagEntry(FString::Printf(TEXT("pie_ready settle=%dms"), SettleMs));

	if (bTakeRecord)
	{
		FString TakeError;
		if (FSequencerController::StartTakeRecordingForPIE(GetPIEWorld(), TakeSlate, TakeError))
		{
			AddDiagEntry(TEXT("take_recorder_started"));
			UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Take Recorder started (during settle)"));
		}
		else
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: Failed to start Take Recorder: %s"), *TakeError);
			AddDiagEntry(FString::Printf(TEXT("take_recorder_start_failed: %s"), *TakeError));
		}
	}

	if (SettleMs <= 0)
	{
		OnSettleComplete();
		return;
	}

	FTimerManager* TimerMgr = GetPIETimerManager();
	if (TimerMgr)
	{
		float SettleSec = SettleMs / 1000.f;
		TimerMgr->SetTimer(
			SettleTimerHandle,
			FTimerDelegate::CreateSP(this, &FPIESequenceRunner::OnSettleComplete),
			SettleSec, false);
	}
	else
	{
		OnSequenceFailed(TEXT("PIE timer manager not available for settle"));
	}
}

void FPIESequenceRunner::OnSettleComplete()
{
	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Settle complete, scheduling %d steps"), Steps.Num());
	LogPlayerState(TEXT("SETTLE_DONE"));
	AddDiagEntry(TEXT("settle_complete"));

	State = EPIESequenceState::Running;

	if (AutoCaptureEveryNFrames > 0)
	{
		TSharedPtr<FSceneViewport> SceneVP = GetPIESceneViewport();
		if (SceneVP.IsValid())
		{
			FrameGrabber = MakeShared<FPIEFrameGrabber>();
			FrameGrabber->StartCapture(SceneVP.ToSharedRef(), AutoCaptureEveryNFrames, OutputDir);
			UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Async auto-capture started (every %d frames via FFrameGrabber)"),
				AutoCaptureEveryNFrames);
			AddDiagEntry(FString::Printf(TEXT("auto_capture_start every=%d frames (async)"), AutoCaptureEveryNFrames));
		}
		else
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: Could not get FSceneViewport for auto-capture"));
		}
	}

	if (Steps.Num() == 0)
	{
		OnSequenceComplete();
		return;
	}

	ScheduleAllSteps();

	if (MaxDurationMs > 0)
	{
		FTimerManager* TM = GetPIETimerManager();
		if (TM)
		{
			TM->SetTimer(MaxDurationTimerHandle,
				FTimerDelegate::CreateSP(AsShared(), &FPIESequenceRunner::OnMaxDurationTimeout),
				MaxDurationMs / 1000.f, false);
			AddDiagEntry(FString::Printf(TEXT("safety_timeout_armed %dms"), MaxDurationMs));
		}
	}
}

void FPIESequenceRunner::ScheduleAllSteps()
{
	FTimerManager* TimerMgr = GetPIETimerManager();
	if (!TimerMgr)
	{
		OnSequenceFailed(TEXT("PIE timer manager not available"));
		return;
	}

	float CumulativeDelayMs = 0.f;
	float MaxCompletionMs = 0.f;

	for (int32 i = 0; i < Steps.Num(); ++i)
	{
		const FPIESequenceStep& Step = Steps[i];
		CumulativeDelayMs += Step.DelayMs;

		float StepCompletionMs = CumulativeDelayMs;
		if (Step.Type == EPIEStepType::Hold)
		{
			StepCompletionMs += Step.DurationMs;
		}
		else if (Step.Type == EPIEStepType::InputTape)
		{
			StepCompletionMs += Step.TapeValues.Num() * (1000.f / 60.f);
		}
		MaxCompletionMs = FMath::Max(MaxCompletionMs, StepCompletionMs);

		if (CumulativeDelayMs <= 0.f)
		{
			CurrentStepIndex = i;
			ExecuteStep(Step);
			StepsCompleted++;
		}
		else
		{
			float DelaySec = CumulativeDelayMs / 1000.f;
			int32 StepIdx = i;

			FTimerHandle Handle;
			TimerMgr->SetTimer(
				Handle,
				FTimerDelegate::CreateLambda([this, StepIdx]()
				{
					if (State != EPIESequenceState::Running) return;
					if (StepIdx >= Steps.Num()) return;
					CurrentStepIndex = StepIdx;
					ExecuteStep(Steps[StepIdx]);
					StepsCompleted++;
					if (StepsCompleted >= Steps.Num())
					{
						FTimerManager* TM = GetPIETimerManager();
						if (TM)
						{
							float GraceSec = 0.1f;
							for (auto& Pair : ActiveHolds)
							{
								if (Pair.Value.StopHandle.IsValid())
								{
									float Remaining = TM->GetTimerRemaining(Pair.Value.StopHandle);
									GraceSec = FMath::Max(GraceSec, Remaining + 0.1f);
								}
							}
							for (const FActiveTape& Tape : ActiveTapes)
							{
								int32 RemainingFrames = Tape.TotalFrames - Tape.CurrentIndex;
								float RemainingSec = RemainingFrames * (1.f / 60.f);
								GraceSec = FMath::Max(GraceSec, RemainingSec + 0.1f);
							}
							TM->SetTimer(
								CompletionCheckHandle,
								FTimerDelegate::CreateSP(AsShared(), &FPIESequenceRunner::OnSequenceComplete),
								GraceSec, false);
						}
						else
						{
							OnSequenceComplete();
						}
					}
				}),
				DelaySec, false);

			ScheduledStepHandles.Add(Handle);
		}
	}

	if (StepsCompleted >= Steps.Num())
	{
		float GraceSec = 0.1f;
		for (auto& Pair : ActiveHolds)
		{
			if (Pair.Value.StopHandle.IsValid())
			{
				float Remaining = TimerMgr->GetTimerRemaining(Pair.Value.StopHandle);
				GraceSec = FMath::Max(GraceSec, Remaining + 0.1f);
			}
		}
		for (const FActiveTape& Tape : ActiveTapes)
		{
			int32 RemainingFrames = Tape.TotalFrames - Tape.CurrentIndex;
			float RemainingSec = RemainingFrames * (1.f / 60.f);
			GraceSec = FMath::Max(GraceSec, RemainingSec + 0.1f);
		}
		FTimerHandle Handle;
		TimerMgr->SetTimer(
			Handle,
			FTimerDelegate::CreateSP(AsShared(), &FPIESequenceRunner::OnSequenceComplete),
			GraceSec, false);
		ScheduledStepHandles.Add(Handle);
	}
}

void FPIESequenceRunner::ExecuteNextStep()
{
	if (State != EPIESequenceState::Running)
	{
		return;
	}

	if (CurrentStepIndex >= Steps.Num())
	{
		OnSequenceComplete();
		return;
	}

	const FPIESequenceStep& Step = Steps[CurrentStepIndex];

	if (Step.DelayMs > 0.f)
	{
		FTimerManager* TimerMgr = GetPIETimerManager();
		if (TimerMgr)
		{
			float DelaySec = Step.DelayMs / 1000.f;
			TimerMgr->SetTimer(
				StepTimerHandle,
				FTimerDelegate::CreateLambda([this, Step]()
				{
					ExecuteStep(Step);
					CurrentStepIndex++;
					ExecuteNextStep();
				}),
				DelaySec, false);
		}
		else
		{
			OnSequenceFailed(TEXT("PIE timer manager not available"));
		}
	}
	else
	{
		ExecuteStep(Step);
		CurrentStepIndex++;
		ExecuteNextStep();
	}
}

void FPIESequenceRunner::ExecuteStep(const FPIESequenceStep& Step)
{
	switch (Step.Type)
	{
	case EPIEStepType::Input:
		ExecuteInputStep(Step);
		break;
	case EPIEStepType::Hold:
		ExecuteHoldStep(Step, CurrentStepIndex);
		break;
	case EPIEStepType::Capture:
		ExecuteCaptureStep(Step);
		break;
	case EPIEStepType::Console:
		ExecuteConsoleStep(Step);
		break;
	case EPIEStepType::InputTape:
		ExecuteInputTapeStep(Step, CurrentStepIndex);
		break;
	}
}

void FPIESequenceRunner::ExecuteInputStep(const FPIESequenceStep& Step)
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: PIE world not available for input step"));
		return;
	}

	UInputAction* Action = nullptr;

	TWeakObjectPtr<UInputAction>* PreLoaded = PreLoadedActions.Find(Step.ActionPath);
	if (PreLoaded && PreLoaded->IsValid())
	{
		Action = PreLoaded->Get();
	}

	if (!Action)
	{
		FString AdjustedPath = Step.ActionPath;
		if (!AdjustedPath.Contains(TEXT(".")))
		{
			AdjustedPath += TEXT(".") + FPaths::GetBaseFilename(Step.ActionPath);
		}

		Action = LoadObject<UInputAction>(nullptr, *AdjustedPath);
		if (!Action)
		{
			Action = LoadObject<UInputAction>(nullptr, *Step.ActionPath);
		}
	}

	if (!Action)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: Failed to load InputAction: %s"), *Step.ActionPath);
		return;
	}

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC || !PC->GetLocalPlayer())
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: No player controller in PIE"));
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: No EnhancedInput subsystem"));
		return;
	}

	FInputActionValue Value;
	switch (Action->ValueType)
	{
	case EInputActionValueType::Boolean:
		Value = FInputActionValue(true);
		break;
	case EInputActionValueType::Axis1D:
		Value = FInputActionValue(Step.ValueX);
		break;
	case EInputActionValueType::Axis2D:
		Value = FInputActionValue(FVector2D(Step.ValueX, Step.ValueY));
		break;
	case EInputActionValueType::Axis3D:
		Value = FInputActionValue(FVector(Step.ValueX, Step.ValueY, Step.ValueZ));
		break;
	}

	InputSubsystem->InjectInputForAction(Action, Value, {}, {});

	FString ValueStr;
	switch (Action->ValueType)
	{
	case EInputActionValueType::Boolean: ValueStr = TEXT("bool=true"); break;
	case EInputActionValueType::Axis1D: ValueStr = FString::Printf(TEXT("1D=%.2f"), Step.ValueX); break;
	case EInputActionValueType::Axis2D: ValueStr = FString::Printf(TEXT("2D=(%.2f, %.2f)"), Step.ValueX, Step.ValueY); break;
	case EInputActionValueType::Axis3D: ValueStr = FString::Printf(TEXT("3D=(%.2f, %.2f, %.2f)"), Step.ValueX, Step.ValueY, Step.ValueZ); break;
	}

	FString DiagMsg = FString::Printf(TEXT("input:%s val=%s"), *Action->GetName(), *ValueStr);
	LogPlayerState(*FString::Printf(TEXT("INPUT %s"), *Action->GetName()));
	AddDiagEntry(DiagMsg);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Input step - Injected %s %s (frame %llu)"),
		*Action->GetName(), *ValueStr, GFrameNumber);
}

void FPIESequenceRunner::ExecuteHoldStep(const FPIESequenceStep& Step, int32 StepIndex)
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: PIE world not available for hold step"));
		return;
	}

	UInputAction* Action = nullptr;

	TWeakObjectPtr<UInputAction>* PreLoaded = PreLoadedActions.Find(Step.ActionPath);
	if (PreLoaded && PreLoaded->IsValid())
	{
		Action = PreLoaded->Get();
	}

	if (!Action)
	{
		FString AdjustedPath = Step.ActionPath;
		if (!AdjustedPath.Contains(TEXT(".")))
		{
			AdjustedPath += TEXT(".") + FPaths::GetBaseFilename(Step.ActionPath);
		}

		Action = LoadObject<UInputAction>(nullptr, *AdjustedPath);
		if (!Action)
		{
			Action = LoadObject<UInputAction>(nullptr, *Step.ActionPath);
		}
	}

	if (!Action)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: Failed to load InputAction for hold: %s"), *Step.ActionPath);
		return;
	}

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC || !PC->GetLocalPlayer())
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: No player controller for hold step"));
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: No EnhancedInput subsystem for hold"));
		return;
	}

	FInputActionValue Value;
	switch (Action->ValueType)
	{
	case EInputActionValueType::Boolean:
		Value = FInputActionValue(true);
		break;
	case EInputActionValueType::Axis1D:
		Value = FInputActionValue(Step.ValueX);
		break;
	case EInputActionValueType::Axis2D:
		Value = FInputActionValue(FVector2D(Step.ValueX, Step.ValueY));
		break;
	case EInputActionValueType::Axis3D:
		Value = FInputActionValue(FVector(Step.ValueX, Step.ValueY, Step.ValueZ));
		break;
	}

	FActiveHold Hold;
	Hold.Action = Action;
	Hold.TickCount = 0;

	FTimerManager* TimerMgr = GetPIETimerManager();
	if (!TimerMgr)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: No timer manager for hold"));
		return;
	}

	FString ValueStr;
	switch (Action->ValueType)
	{
	case EInputActionValueType::Boolean: ValueStr = TEXT("bool=true"); break;
	case EInputActionValueType::Axis1D: ValueStr = FString::Printf(TEXT("1D=%.2f"), Step.ValueX); break;
	case EInputActionValueType::Axis2D: ValueStr = FString::Printf(TEXT("2D=(%.2f, %.2f)"), Step.ValueX, Step.ValueY); break;
	case EInputActionValueType::Axis3D: ValueStr = FString::Printf(TEXT("3D=(%.2f, %.2f, %.2f)"), Step.ValueX, Step.ValueY, Step.ValueZ); break;
	}

	LogPlayerState(*FString::Printf(TEXT("HOLD_START %s %s dur=%dms"), *Action->GetName(), *ValueStr, static_cast<int32>(Step.DurationMs)));
	AddDiagEntry(FString::Printf(TEXT("hold_start:%s val=%s dur=%dms"), *Action->GetName(), *ValueStr, static_cast<int32>(Step.DurationMs)));

	TimerMgr->SetTimer(
		Hold.TimerHandle,
		FTimerDelegate::CreateLambda([this, InputSubsystem, Action, Value, StepIndex]()
		{
			if (InputSubsystem && Action)
			{
				InputSubsystem->InjectInputForAction(Action, Value, {}, {});

				FActiveHold* H = ActiveHolds.Find(StepIndex);
				if (H)
				{
					H->TickCount++;
					if (H->TickCount == 1 || H->TickCount % 30 == 0)
					{
						UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] HOLD_TICK step=%d tick=%d action=%s frame=%llu"),
							StepIndex, H->TickCount, *Action->GetName(), GFrameNumber);
						LogPlayerState(*FString::Printf(TEXT("HOLD_TICK_%d step=%d"), H->TickCount, StepIndex));
					}
				}
			}
		}),
		1.f / 60.f, true, 0.f);

	if (Step.DurationMs > 0.f)
	{
		float DurationSec = Step.DurationMs / 1000.f;
		TimerMgr->SetTimer(
			Hold.StopHandle,
			FTimerDelegate::CreateLambda([this, StepIndex, TimerMgr, Action]()
			{
				FActiveHold* Found = ActiveHolds.Find(StepIndex);
				if (Found)
				{
					int32 FinalTicks = Found->TickCount;
					if (TimerMgr && Found->TimerHandle.IsValid())
					{
						TimerMgr->ClearTimer(Found->TimerHandle);
					}
					UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] HOLD_END step=%d ticks=%d action=%s frame=%llu"),
						StepIndex, FinalTicks, *Action->GetName(), GFrameNumber);
					LogPlayerState(*FString::Printf(TEXT("HOLD_END step=%d ticks=%d"), StepIndex, FinalTicks));
					AddDiagEntry(FString::Printf(TEXT("hold_end:%s ticks=%d"), *Action->GetName(), FinalTicks));
					ActiveHolds.Remove(StepIndex);
				}
			}),
			DurationSec, false);
	}

	ActiveHolds.Add(StepIndex, MoveTemp(Hold));

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Hold step %d - %s %s for %dms (frame %llu)"),
		StepIndex, *Action->GetName(), *ValueStr, static_cast<int32>(Step.DurationMs), GFrameNumber);
}

void FPIESequenceRunner::ExecuteCaptureStep(const FPIESequenceStep& Step)
{
	LogPlayerState(*FString::Printf(TEXT("CAPTURE %s"), *Step.CaptureName));
	AddDiagEntry(FString::Printf(TEXT("capture:%s"), *Step.CaptureName));
	DoNamedCapture(Step.CaptureName);
}

void FPIESequenceRunner::ExecuteConsoleStep(const FPIESequenceStep& Step)
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld || !GEngine)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: PIE world not available for console step"));
		return;
	}

	GEngine->Exec(PIEWorld, *Step.Command);

	LogPlayerState(*FString::Printf(TEXT("CONSOLE %s"), *Step.Command));
	AddDiagEntry(FString::Printf(TEXT("console:%s"), *Step.Command));

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Console step - '%s' (frame %llu)"),
		*Step.Command, GFrameNumber);
}

void FPIESequenceRunner::OnSequenceComplete()
{
	StopNormalCapture();
	StopAllHolds();
	StopAllTapes();

	FTimerManager* TimerMgr = GetPIETimerManager();
	if (TimerMgr && MaxDurationTimerHandle.IsValid())
	{
		TimerMgr->ClearTimer(MaxDurationTimerHandle);
	}

	if (FrameGrabber.IsValid())
	{
		FrameGrabber->StopCapture();
		CapturedFiles = FrameGrabber->GetCapturedFilePaths();
		FrameGrabber.Reset();
	}

	if (bTakeRecord)
	{
		FString TakeError;
		FString TakePath;
		if (FSequencerController::StopTakeRecordingForPIE(TakePath, TakeError))
		{
			TakeSequencePath = TakePath;
			AddDiagEntry(FString::Printf(TEXT("take_recorder_stopped: %s"), *TakePath));
			UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Take Recorder stopped, sequence: %s"), *TakePath);
		}
		else
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: Failed to stop Take Recorder: %s"), *TakeError);
			AddDiagEntry(FString::Printf(TEXT("take_recorder_stop_failed: %s"), *TakeError));
		}
	}

	State = EPIESequenceState::Completed;
	EndTime = FPlatformTime::Seconds();

	LogPlayerState(TEXT("SEQUENCE_DONE"));
	AddDiagEntry(TEXT("sequence_complete"));

	UWorld* PIEWorld = GetPIEWorld();
	if (PIEWorld && GEngine)
	{
		GEngine->Exec(PIEWorld, TEXT("t.MaxFPS 0"));
	}

	UnbindEndPIEDelegate();

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Completed %d steps, %d captures (%.1fs)"),
		Steps.Num(), CaptureEntries.Num() + CapturedFiles.Num(), EndTime - StartTime);

	if (bAutoStopPIE)
	{
		WriteManifest();

		if (GEditor && GEditor->IsPlaySessionInProgress())
		{
			GEditor->RequestEndPlayMap();
			UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Auto-stopping PIE"));
		}
	}
}

void FPIESequenceRunner::OnSequenceFailed(const FString& Error)
{
	StopNormalCapture();
	StopAllHolds();
	StopAllTapes();

	if (FrameGrabber.IsValid())
	{
		FrameGrabber->StopCapture();
		CapturedFiles = FrameGrabber->GetCapturedFilePaths();
		FrameGrabber.Reset();
	}

	State = EPIESequenceState::Failed;
	ErrorMessage = Error;
	EndTime = FPlatformTime::Seconds();

	UnbindEndPIEDelegate();

	UE_LOG(LogUnrealClaude, Error, TEXT("PIESequenceRunner: Failed - %s"), *Error);

	if (bAutoStopPIE)
	{
		WriteManifest();

		if (GEditor && GEditor->IsPlaySessionInProgress())
		{
			GEditor->RequestEndPlayMap();
		}
	}
}

void FPIESequenceRunner::OnMaxDurationTimeout()
{
	if (!IsRunning())
	{
		return;
	}

	UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: Safety timeout reached (%dms) — force-stopping sequence at step %d/%d"),
		MaxDurationMs, StepsCompleted, Steps.Num());
	AddDiagEntry(FString::Printf(TEXT("safety_timeout_fired %dms step=%d/%d"), MaxDurationMs, StepsCompleted, Steps.Num()));

	OnSequenceFailed(FString::Printf(TEXT("Safety timeout: sequence exceeded max_duration_ms=%d (completed %d/%d steps)"),
		MaxDurationMs, StepsCompleted, Steps.Num()));
}

void FPIESequenceRunner::WriteManifest()
{
	TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();

	FString Name = FPaths::GetBaseFilename(OutputDir);
	Manifest->SetStringField(TEXT("name"), Name);
	Manifest->SetStringField(TEXT("status"),
		State == EPIESequenceState::Completed ? TEXT("completed") : TEXT("failed"));
	Manifest->SetNumberField(TEXT("steps_executed"), StepsCompleted);
	Manifest->SetNumberField(TEXT("total_steps"), Steps.Num());
	Manifest->SetNumberField(TEXT("duration_s"), EndTime - StartTime);

	TArray<TSharedPtr<FJsonValue>> CapturesArray;
	for (const FPIECaptureEntry& Entry : CaptureEntries)
	{
		TSharedPtr<FJsonObject> CaptureObj = MakeShared<FJsonObject>();
		CaptureObj->SetStringField(TEXT("name"), Entry.Name);
		CaptureObj->SetStringField(TEXT("file"), Entry.File);
		CaptureObj->SetNumberField(TEXT("frame_number"), static_cast<double>(Entry.FrameNumber));
		CaptureObj->SetNumberField(TEXT("game_time"), Entry.GameTime);
		CapturesArray.Add(MakeShared<FJsonValueObject>(CaptureObj));
	}
	Manifest->SetArrayField(TEXT("captures"), CapturesArray);

	if (!TakeSequencePath.IsEmpty())
	{
		Manifest->SetStringField(TEXT("take_sequence_path"), TakeSequencePath);
	}

	if (CapturedFiles.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> AutoCapArray;
		for (const FString& FilePath : CapturedFiles)
		{
			AutoCapArray.Add(MakeShared<FJsonValueString>(FPaths::GetCleanFilename(FilePath)));
		}
		Manifest->SetNumberField(TEXT("auto_capture_count"), CapturedFiles.Num());
		Manifest->SetArrayField(TEXT("auto_capture_files"), AutoCapArray);
	}

	TArray<TSharedPtr<FJsonValue>> DiagArray;
	for (const FDiagLogEntry& D : DiagLog)
	{
		TSharedPtr<FJsonObject> DObj = MakeShared<FJsonObject>();
		DObj->SetNumberField(TEXT("t"), FMath::RoundToDouble(D.GameTime * 1000.0) / 1000.0);
		DObj->SetNumberField(TEXT("frame"), static_cast<double>(D.Frame));
		DObj->SetStringField(TEXT("event"), D.Event);
		DObj->SetStringField(TEXT("pos"), FString::Printf(TEXT("%.1f, %.1f, %.1f"), D.PlayerLoc.X, D.PlayerLoc.Y, D.PlayerLoc.Z));
		DObj->SetStringField(TEXT("rot"), FString::Printf(TEXT("P=%.1f Y=%.1f R=%.1f"), D.PlayerRot.Pitch, D.PlayerRot.Yaw, D.PlayerRot.Roll));
		DiagArray.Add(MakeShared<FJsonValueObject>(DObj));
	}
	Manifest->SetArrayField(TEXT("diag"), DiagArray);

	if (!ErrorMessage.IsEmpty())
	{
		Manifest->SetStringField(TEXT("error"), ErrorMessage);
	}
	else
	{
		Manifest->SetField(TEXT("error"), MakeShared<FJsonValueNull>());
	}

	FString ManifestJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ManifestJson);
	FJsonSerializer::Serialize(Manifest.ToSharedRef(), Writer);

	FString ManifestPath = OutputDir / TEXT("manifest.json");
	FFileHelper::SaveStringToFile(ManifestJson, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Wrote manifest to %s"), *ManifestPath);
}

void FPIESequenceRunner::StopAllHolds()
{
	FTimerManager* TimerMgr = GetPIETimerManager();

	for (auto& Pair : ActiveHolds)
	{
		if (TimerMgr)
		{
			if (Pair.Value.TimerHandle.IsValid())
			{
				TimerMgr->ClearTimer(Pair.Value.TimerHandle);
			}
			if (Pair.Value.StopHandle.IsValid())
			{
				TimerMgr->ClearTimer(Pair.Value.StopHandle);
			}
		}
	}
	ActiveHolds.Empty();
}

void FPIESequenceRunner::ExecuteInputTapeStep(const FPIESequenceStep& Step, int32 StepIndex)
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: PIE world not available for input_tape step"));
		return;
	}

	UInputAction* Action = nullptr;

	TWeakObjectPtr<UInputAction>* PreLoaded = PreLoadedActions.Find(Step.ActionPath);
	if (PreLoaded && PreLoaded->IsValid())
	{
		Action = PreLoaded->Get();
	}

	if (!Action)
	{
		FString AdjustedPath = Step.ActionPath;
		if (!AdjustedPath.Contains(TEXT(".")))
		{
			AdjustedPath += TEXT(".") + FPaths::GetBaseFilename(Step.ActionPath);
		}
		Action = LoadObject<UInputAction>(nullptr, *AdjustedPath);
		if (!Action)
		{
			Action = LoadObject<UInputAction>(nullptr, *Step.ActionPath);
		}
	}

	if (!Action)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: Failed to load InputAction for tape: %s"), *Step.ActionPath);
		return;
	}

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC || !PC->GetLocalPlayer())
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: No player controller for tape step"));
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: No EnhancedInput subsystem for tape"));
		return;
	}

	FActiveTape Tape;
	Tape.Action = Action;
	Tape.CurrentIndex = 0;
	Tape.TotalFrames = Step.TapeValues.Num();
	Tape.StepIndex = StepIndex;
	ActiveTapes.Add(MoveTemp(Tape));

	if (!OnEndFrameHandle.IsValid())
	{
		OnEndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FPIESequenceRunner::OnEndFrameTick);
	}

	LogPlayerState(*FString::Printf(TEXT("TAPE_START %s frames=%d"), *Action->GetName(), Step.TapeValues.Num()));
	AddDiagEntry(FString::Printf(TEXT("tape_start:%s frames=%d"), *Action->GetName(), Step.TapeValues.Num()));

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Tape step %d - %s, %d frames (frame %llu)"),
		StepIndex, *Action->GetName(), Step.TapeValues.Num(), GFrameNumber);
}

void FPIESequenceRunner::OnEndFrameTick()
{
	if (State != EPIESequenceState::Running || ActiveTapes.Num() == 0)
	{
		return;
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld) return;

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC || !PC->GetLocalPlayer()) return;

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem) return;

	if (APawn* Pawn = PC->GetPawn())
	{
		FVector Loc = Pawn->GetActorLocation();
		FRotator Rot = Pawn->GetActorRotation();
		FRotator CtrlRot = PC->GetControlRotation();
		UE_LOG(LogUnrealClaude, Log, TEXT("[REPLAY-POS] frame=%llu loc=(%.2f,%.2f,%.2f) rot=(%.2f,%.2f,%.2f) ctrl=(%.2f,%.2f,%.2f)"),
			GFrameNumber, Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll, CtrlRot.Pitch, CtrlRot.Yaw, CtrlRot.Roll);
	}

	for (int32 i = ActiveTapes.Num() - 1; i >= 0; --i)
	{
		FActiveTape& Tape = ActiveTapes[i];

		if (Tape.CurrentIndex >= Tape.TotalFrames)
		{
			UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] TAPE_END step=%d ticks=%d action=%s frame=%llu"),
				Tape.StepIndex, Tape.TotalFrames, *Tape.Action->GetName(), GFrameNumber);
			LogPlayerState(*FString::Printf(TEXT("TAPE_END step=%d ticks=%d"), Tape.StepIndex, Tape.TotalFrames));
			AddDiagEntry(FString::Printf(TEXT("tape_end:%s ticks=%d"), *Tape.Action->GetName(), Tape.TotalFrames));
			ActiveTapes.RemoveAt(i);
			continue;
		}

		UInputAction* Action = Tape.Action.Get();
		if (!Action)
		{
			ActiveTapes.RemoveAt(i);
			continue;
		}

		const FVector& Val = Steps[Tape.StepIndex].TapeValues[Tape.CurrentIndex];

		FInputActionValue Value;
		switch (Action->ValueType)
		{
		case EInputActionValueType::Boolean:
			Value = FInputActionValue(Val.X != 0.f);
			break;
		case EInputActionValueType::Axis1D:
			Value = FInputActionValue(static_cast<float>(Val.X));
			break;
		case EInputActionValueType::Axis2D:
			Value = FInputActionValue(FVector2D(Val.X, Val.Y));
			break;
		case EInputActionValueType::Axis3D:
			Value = FInputActionValue(Val);
			break;
		}

		InputSubsystem->InjectInputForAction(Action, Value, {}, {});

		if (Tape.CurrentIndex == 0 || (Tape.CurrentIndex + 1) == Tape.TotalFrames || Tape.CurrentIndex % 60 == 0)
		{
			UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] TAPE_TICK step=%d tick=%d/%d action=%s val=(%.2f,%.2f) frame=%llu"),
				Tape.StepIndex, Tape.CurrentIndex + 1, Tape.TotalFrames, *Action->GetName(),
				Val.X, Val.Y, GFrameNumber);
		}

		Tape.CurrentIndex++;
	}

	if (ActiveTapes.Num() == 0)
	{
		FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
		OnEndFrameHandle.Reset();
	}
}

void FPIESequenceRunner::StopAllTapes()
{
	ActiveTapes.Empty();
	if (OnEndFrameHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
		OnEndFrameHandle.Reset();
	}
}

void FPIESequenceRunner::StartNormalCapture()
{
	FTimerManager* TimerMgr = GetPIETimerManager();
	if (!TimerMgr)
	{
		return;
	}

	float IntervalSec = CaptureIntervalMs / 1000.f;
	TimerMgr->SetTimer(
		NormalCaptureHandle,
		FTimerDelegate::CreateSP(this, &FPIESequenceRunner::DoNormalCapture),
		IntervalSec, true, 0.f);
}

void FPIESequenceRunner::StopNormalCapture()
{
	if (NormalCaptureHandle.IsValid())
	{
		FTimerManager* TimerMgr = GetPIETimerManager();
		if (TimerMgr)
		{
			TimerMgr->ClearTimer(NormalCaptureHandle);
		}
	}
}

void FPIESequenceRunner::DoNormalCapture()
{
	FViewport* PIEViewport = GetPIEViewport();
	if (!PIEViewport)
	{
		return;
	}

	FIntPoint Size = PIEViewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return;
	}

	TArray<FColor> Pixels;
	if (!PIEViewport->ReadPixels(Pixels))
	{
		return;
	}

	constexpr int32 TargetWidth = 1024;
	constexpr int32 TargetHeight = 576;
	constexpr int32 JPEGQuality = 70;

	TArray<FColor> ResizedPixels;
	ResizedPixels.SetNumUninitialized(TargetWidth * TargetHeight);

	const float ScaleX = static_cast<float>(Size.X) / TargetWidth;
	const float ScaleY = static_cast<float>(Size.Y) / TargetHeight;

	for (int32 Y = 0; Y < TargetHeight; ++Y)
	{
		for (int32 X = 0; X < TargetWidth; ++X)
		{
			const int32 SrcX = FMath::Clamp(static_cast<int32>(X * ScaleX), 0, Size.X - 1);
			const int32 SrcY = FMath::Clamp(static_cast<int32>(Y * ScaleY), 0, Size.Y - 1);
			ResizedPixels[Y * TargetWidth + X] = Pixels[SrcY * Size.X + SrcX];
		}
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	if (!ImageWrapper.IsValid())
	{
		return;
	}

	if (!ImageWrapper->SetRaw(ResizedPixels.GetData(), ResizedPixels.Num() * sizeof(FColor),
		TargetWidth, TargetHeight, ERGBFormat::BGRA, 8))
	{
		return;
	}

	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(JPEGQuality);
	if (CompressedData.Num() == 0)
	{
		return;
	}

	double GameTime = 0.0;
	UWorld* PIEWorld = GetPIEWorld();
	if (PIEWorld)
	{
		GameTime = PIEWorld->GetTimeSeconds();
	}

	FString Filename = FString::Printf(TEXT("f%06llu_t%.3f.jpg"), GFrameNumber, GameTime);
	FString FullPath = OutputDir / Filename;

	FFileHelper::SaveArrayToFile(
		TArrayView64<const uint8>(CompressedData.GetData(), CompressedData.Num()),
		*FullPath);

	CapturedFiles.Add(FullPath);
	NormalCaptureCount++;
}

void FPIESequenceRunner::DoNamedCapture(const FString& CaptureName)
{
	FViewport* PIEViewport = GetPIEViewport();
	if (!PIEViewport)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: No PIE viewport for capture '%s'"), *CaptureName);
		return;
	}

	FIntPoint Size = PIEViewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: Invalid viewport size for capture '%s'"), *CaptureName);
		return;
	}

	TArray<FColor> Pixels;
	if (!PIEViewport->ReadPixels(Pixels))
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("PIESequenceRunner: ReadPixels failed for capture '%s'"), *CaptureName);
		return;
	}

	constexpr int32 TargetWidth = 1024;
	constexpr int32 TargetHeight = 576;
	constexpr int32 JPEGQuality = 70;

	TArray<FColor> ResizedPixels;
	ResizedPixels.SetNumUninitialized(TargetWidth * TargetHeight);

	const float ScaleX = static_cast<float>(Size.X) / TargetWidth;
	const float ScaleY = static_cast<float>(Size.Y) / TargetHeight;

	for (int32 Y = 0; Y < TargetHeight; ++Y)
	{
		for (int32 X = 0; X < TargetWidth; ++X)
		{
			const int32 SrcX = FMath::Clamp(static_cast<int32>(X * ScaleX), 0, Size.X - 1);
			const int32 SrcY = FMath::Clamp(static_cast<int32>(Y * ScaleY), 0, Size.Y - 1);
			ResizedPixels[Y * TargetWidth + X] = Pixels[SrcY * Size.X + SrcX];
		}
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	if (!ImageWrapper.IsValid())
	{
		return;
	}

	if (!ImageWrapper->SetRaw(ResizedPixels.GetData(), ResizedPixels.Num() * sizeof(FColor),
		TargetWidth, TargetHeight, ERGBFormat::BGRA, 8))
	{
		return;
	}

	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(JPEGQuality);
	if (CompressedData.Num() == 0)
	{
		return;
	}

	double GameTime = 0.0;
	UWorld* PIEWorld = GetPIEWorld();
	if (PIEWorld)
	{
		GameTime = PIEWorld->GetTimeSeconds();
	}

	FString Filename = CaptureName + TEXT(".jpg");
	FString FullPath = OutputDir / Filename;

	FFileHelper::SaveArrayToFile(
		TArrayView64<const uint8>(CompressedData.GetData(), CompressedData.Num()),
		*FullPath);

	FPIECaptureEntry Entry;
	Entry.Name = CaptureName;
	Entry.File = Filename;
	Entry.FrameNumber = GFrameNumber;
	Entry.GameTime = GameTime;
	CaptureEntries.Add(MoveTemp(Entry));

	CapturedFiles.Add(FullPath);

	UE_LOG(LogUnrealClaude, Log, TEXT("PIESequenceRunner: Captured '%s' at frame %llu (t=%.3f) -> %s"),
		*CaptureName, GFrameNumber, GameTime, *FullPath);
}

void FPIESequenceRunner::LogPlayerState(const TCHAR* Context) const
{
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld) return;

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC) return;

	APawn* Pawn = PC->GetPawn();
	if (!Pawn) return;

	FVector Loc = Pawn->GetActorLocation();
	FRotator Rot = Pawn->GetActorRotation();
	FVector Vel = FVector::ZeroVector;

	ACharacter* Char = Cast<ACharacter>(Pawn);
	if (Char && Char->GetCharacterMovement())
	{
		Vel = Char->GetCharacterMovement()->Velocity;
	}

	UE_LOG(LogUnrealClaude, Log,
		TEXT("[PIE-DBG] %s | frame=%llu t=%.3f | pos=(%.1f, %.1f, %.1f) rot=(P=%.1f Y=%.1f R=%.1f) vel=(%.1f, %.1f, %.1f) speed=%.1f"),
		Context, GFrameNumber, PIEWorld->GetTimeSeconds(),
		Loc.X, Loc.Y, Loc.Z,
		Rot.Pitch, Rot.Yaw, Rot.Roll,
		Vel.X, Vel.Y, Vel.Z, Vel.Size());
}

void FPIESequenceRunner::AddDiagEntry(const FString& Event)
{
	FDiagLogEntry Entry;
	Entry.Event = Event;
	Entry.Frame = GFrameNumber;

	UWorld* PIEWorld = GetPIEWorld();
	if (PIEWorld)
	{
		Entry.GameTime = PIEWorld->GetTimeSeconds();

		APlayerController* PC = PIEWorld->GetFirstPlayerController();
		if (PC && PC->GetPawn())
		{
			Entry.PlayerLoc = PC->GetPawn()->GetActorLocation();
			Entry.PlayerRot = PC->GetPawn()->GetActorRotation();
		}
	}

	DiagLog.Add(MoveTemp(Entry));
}

UWorld* FPIESequenceRunner::GetPIEWorld() const
{
	if (!GEngine)
	{
		return nullptr;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			return Context.World();
		}
	}
	return nullptr;
}

FTimerManager* FPIESequenceRunner::GetPIETimerManager() const
{
	UWorld* World = GetPIEWorld();
	return World ? &World->GetTimerManager() : nullptr;
}

FViewport* FPIESequenceRunner::GetPIEViewport() const
{
	if (!GEditor)
	{
		return nullptr;
	}
	return GEditor->GetPIEViewport();
}

TSharedPtr<FSceneViewport> FPIESequenceRunner::GetPIESceneViewport() const
{
	if (!GEditor)
	{
		return nullptr;
	}
	for (auto& Pair : GEditor->SlatePlayInEditorMap)
	{
		if (Pair.Value.SlatePlayInEditorWindowViewport.IsValid())
		{
			return Pair.Value.SlatePlayInEditorWindowViewport;
		}
	}
	return nullptr;
}
