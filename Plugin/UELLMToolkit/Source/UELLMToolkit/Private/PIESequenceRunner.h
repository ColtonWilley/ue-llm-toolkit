// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "Containers/Ticker.h"

class FPIEFrameGrabber;
class UInputAction;
class FViewport;
class FSceneViewport;

enum class EPIEStepType : uint8
{
	Input,
	Hold,
	Capture,
	Console,
	InputTape
};

struct FPIESequenceStep
{
	EPIEStepType Type = EPIEStepType::Input;
	FString ActionPath;
	float DelayMs = 0.f;
	float ValueX = 1.f;
	float ValueY = 0.f;
	float ValueZ = 0.f;
	float DurationMs = 0.f;
	FString CaptureName;
	FString Command;
	TArray<FVector> TapeValues;
};

enum class EPIESequenceState : uint8
{
	Idle,
	WaitingForPIE,
	Settling,
	Running,
	Completed,
	Failed
};

struct FPIECaptureEntry
{
	FString Name;
	FString File;
	uint64 FrameNumber = 0;
	double GameTime = 0.0;
};

struct FPIESequenceResult
{
	EPIESequenceState State = EPIESequenceState::Idle;
	int32 CurrentStep = 0;
	int32 TotalSteps = 0;
	int32 CapturedFrames = 0;
	FString ErrorMessage;
	TArray<FString> CapturedFiles;
	TArray<FPIECaptureEntry> Captures;
	double StartTime = 0.0;
	double EndTime = 0.0;
};

class FPIESequenceRunner : public TSharedFromThis<FPIESequenceRunner>
{
public:
	FPIESequenceRunner();
	~FPIESequenceRunner();

	void Start(
		const TArray<FPIESequenceStep>& InSteps,
		const FString& InCaptureMode,
		int32 InCaptureIntervalMs,
		int32 InCaptureEveryNFrames,
		const FString& InOutputDir);

	void StartRunSequence(
		const TArray<FPIESequenceStep>& InSteps,
		int32 InSettleMs,
		const FString& InOutputDir,
		const TMap<FString, TWeakObjectPtr<UInputAction>>& InPreLoadedActions,
		int32 InAutoCapEveryNFrames = 0,
		int32 InMaxDurationMs = 0,
		bool bInTakeRecord = false,
		const FString& InTakeSlate = TEXT(""));

	void Cancel();

	FPIESequenceResult GetStatus() const;

	bool IsRunning() const
	{
		return State == EPIESequenceState::WaitingForPIE
			|| State == EPIESequenceState::Settling
			|| State == EPIESequenceState::Running;
	}

private:
	void OnPIEEnded(bool bIsSimulating);
	void BindEndPIEDelegate();
	void UnbindEndPIEDelegate();

	void PollPIEReady();
	void BeginSettle();
	void OnSettleComplete();
	void ScheduleAllSteps();
	void ExecuteNextStep();
	void ExecuteStep(const FPIESequenceStep& Step);
	void ExecuteInputStep(const FPIESequenceStep& Step);
	void ExecuteHoldStep(const FPIESequenceStep& Step, int32 StepIndex);
	void ExecuteCaptureStep(const FPIESequenceStep& Step);
	void ExecuteConsoleStep(const FPIESequenceStep& Step);
	void ExecuteInputTapeStep(const FPIESequenceStep& Step, int32 StepIndex);
	void OnEndFrameTick();
	void StopAllTapes();
	void OnSequenceComplete();
	void OnSequenceFailed(const FString& Error);
	void OnMaxDurationTimeout();
	void WriteManifest();
	void StopAllHolds();

	void StartNormalCapture();
	void StopNormalCapture();
	void DoNormalCapture();
	void DoNamedCapture(const FString& CaptureName);

	void LogPlayerState(const TCHAR* Context) const;

	TArray<FPIESequenceStep> Steps;
	int32 CurrentStepIndex = 0;
	int32 StepsCompleted = 0;
	EPIESequenceState State = EPIESequenceState::Idle;
	FString ErrorMessage;

	FString CaptureMode;
	int32 CaptureIntervalMs = 200;
	int32 CaptureEveryNFrames = 3;
	FString OutputDir;
	int32 SettleMs = 0;
	bool bAutoStartPIE = false;
	bool bAutoStopPIE = false;
	int32 AutoCaptureEveryNFrames = 0;

	TMap<FString, TWeakObjectPtr<UInputAction>> PreLoadedActions;

	struct FActiveHold
	{
		FTimerHandle TimerHandle;
		FTimerHandle StopHandle;
		TWeakObjectPtr<UInputAction> Action;
		int32 TickCount = 0;
	};
	TMap<int32, FActiveHold> ActiveHolds;

	struct FActiveTape
	{
		TWeakObjectPtr<UInputAction> Action;
		int32 CurrentIndex = 0;
		int32 TotalFrames = 0;
		int32 StepIndex = -1;
	};
	TArray<FActiveTape> ActiveTapes;
	FDelegateHandle OnEndFrameHandle;

	struct FDiagLogEntry
	{
		double GameTime = 0.0;
		uint64 Frame = 0;
		FString Event;
		FVector PlayerLoc = FVector::ZeroVector;
		FRotator PlayerRot = FRotator::ZeroRotator;
	};
	TArray<FDiagLogEntry> DiagLog;
	void AddDiagEntry(const FString& Event);

	FTSTicker::FDelegateHandle PIEPollTickerHandle;
	FTimerHandle SettleTimerHandle;
	FTimerHandle StepTimerHandle;
	FTimerHandle NormalCaptureHandle;
	FTimerHandle CompletionCheckHandle;
	FTimerHandle MaxDurationTimerHandle;
	TArray<FTimerHandle> ScheduledStepHandles;

	int32 MaxDurationMs = 0;

	FDelegateHandle EndPIEDelegateHandle;

	TSharedPtr<FPIEFrameGrabber> FrameGrabber;

	TArray<FString> CapturedFiles;
	TArray<FPIECaptureEntry> CaptureEntries;
	int32 NormalCaptureCount = 0;
	double StartTime = 0.0;
	double EndTime = 0.0;

	bool bTakeRecord = false;
	FString TakeSlate;
	FString TakeSequencePath;

	UWorld* GetPIEWorld() const;
	FTimerManager* GetPIETimerManager() const;
	FViewport* GetPIEViewport() const;
	TSharedPtr<FSceneViewport> GetPIESceneViewport() const;
};
