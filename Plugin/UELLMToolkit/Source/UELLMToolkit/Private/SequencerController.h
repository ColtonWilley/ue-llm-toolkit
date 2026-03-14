// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class ULevelSequence;
class ISequencer;

class FSequencerController
{
public:
	// === Take Recorder ===

	static bool StartTakeRecording(const FString& SlateName, FString& OutError);

	static bool StopTakeRecording(FString& OutSequencePath, FString& OutError);

	static bool GetTakeRecorderStatus(bool& bIsRecording, FString& OutState, FString& OutSequencePath);

	// === Sequencer ===

	static bool OpenSequence(const FString& AssetPath, FString& OutError);

	static bool CloseSequence(FString& OutError);

	static bool ScrubToFrame(int32 Frame, FString& OutError);

	static TSharedPtr<ISequencer> GetActiveSequencer();

	// === State ===

	static bool GetSequencerState(TSharedPtr<FJsonObject>& OutState, FString& OutError);

	// === Viewport Camera ===

	static bool GetViewportCamera(FVector& OutLocation, FRotator& OutRotation);

	static bool SetViewportCamera(const FVector& Location, const FRotator& Rotation);

	// === Take Recorder for PIESequenceRunner ===

	static bool StartTakeRecordingForPIE(UWorld* PIEWorld, const FString& SlateName, FString& OutError);

	static bool StopTakeRecordingForPIE(FString& OutSequencePath, FString& OutError);
};
