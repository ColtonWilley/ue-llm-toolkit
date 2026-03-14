// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"

class UInputAction;
class UEnhancedPlayerInput;

struct FRecordedAction
{
	TWeakObjectPtr<const UInputAction> Action;
	FString ActionName;
	FString ActionPath;
	EInputActionValueType ValueType = EInputActionValueType::Boolean;
	FInputActionValue LastValue;
	bool bWasActive = false;
};

struct FRecordedInputFrame
{
	uint64 FrameNumber = 0;
	double GameTime = 0.0;
	double DeltaTime = 0.0;

	FVector PawnLocation = FVector::ZeroVector;
	FRotator PawnRotation = FRotator::ZeroRotator;
	FVector PawnVelocity = FVector::ZeroVector;
	float Speed2D = 0.f;

	FString ActiveMontage;

	TMap<FString, FVector> ActionValues;
	TArray<FString> EdgeEvents;
};

struct FRecordingResult
{
	FString CSVPath;
	FString SequencePath;
	FString OutputDir;
	int32 TotalFrames = 0;
	double DurationSeconds = 0.0;
	TArray<FString> DiscoveredActions;
};

enum class EPIERecorderState : uint8
{
	Idle,
	WaitingForPawn,
	Recording
};

class FPIEInputRecorder
{
public:
	FPIEInputRecorder();
	~FPIEInputRecorder();

	FPIEInputRecorder(const FPIEInputRecorder&) = delete;
	FPIEInputRecorder& operator=(const FPIEInputRecorder&) = delete;

	const FRecordingResult& GetLastResult() const { return LastResult; }
	bool HasLastResult() const { return LastResult.TotalFrames > 0; }

	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

	float GetAxisThreshold() const { return AxisThreshold; }
	void SetAxisThreshold(float InThreshold) { AxisThreshold = InThreshold; }

	EPIERecorderState GetState() const { return State; }

private:
	void OnBeginPIE(bool bIsSimulating);
	void OnEndPIE(bool bIsSimulating);
	void OnEndFrameTick();

	bool AttachToPIE();
	void RecordFrame(UEnhancedPlayerInput* PlayerInput);

	void WriteCSV(const FString& Path);
	void WriteSequenceJSON(const FString& Path);

	void Reset();

	bool bEnabled = true;
	float AxisThreshold = 0.15f;
	EPIERecorderState State = EPIERecorderState::Idle;

	TArray<FRecordedAction> TrackedActions;
	TArray<FRecordedInputFrame> RecordedFrames;

	FDelegateHandle BeginPIEHandle;
	FDelegateHandle EndPIEHandle;
	FDelegateHandle OnEndFrameHandle;

	FRecordingResult LastResult;
};
