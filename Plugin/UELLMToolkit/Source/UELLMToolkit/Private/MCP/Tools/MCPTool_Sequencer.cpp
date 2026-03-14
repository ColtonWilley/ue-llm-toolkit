// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Sequencer.h"
#include "SequencerController.h"

FMCPToolResult FMCPTool_Sequencer::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	if (Operation == TEXT("take_start")) return ExecuteTakeStart(Params);
	if (Operation == TEXT("take_stop")) return ExecuteTakeStop(Params);
	if (Operation == TEXT("take_status")) return ExecuteTakeStatus(Params);
	if (Operation == TEXT("open")) return ExecuteOpen(Params);
	if (Operation == TEXT("scrub")) return ExecuteScrub(Params);
	if (Operation == TEXT("get_state")) return ExecuteGetState(Params);
	if (Operation == TEXT("close")) return ExecuteClose(Params);

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: take_start, take_stop, take_status, open, scrub, get_state, close"),
		*Operation));
}

FMCPToolResult FMCPTool_Sequencer::ExecuteTakeStart(const TSharedRef<FJsonObject>& Params)
{
	FString SlateName = ExtractOptionalString(Params, TEXT("slate_name"), TEXT(""));

	FString Error;
	if (!FSequencerController::StartTakeRecording(SlateName, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("recording"), true);
	if (!SlateName.IsEmpty())
	{
		Data->SetStringField(TEXT("slate_name"), SlateName);
	}

	return FMCPToolResult::Success(TEXT("Take Recorder started"), Data);
}

FMCPToolResult FMCPTool_Sequencer::ExecuteTakeStop(const TSharedRef<FJsonObject>& Params)
{
	FString SequencePath;
	FString Error;
	if (!FSequencerController::StopTakeRecording(SequencePath, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("recording"), false);
	Data->SetStringField(TEXT("sequence_path"), SequencePath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Take Recorder stopped. Sequence: %s"), *SequencePath), Data);
}

FMCPToolResult FMCPTool_Sequencer::ExecuteTakeStatus(const TSharedRef<FJsonObject>& Params)
{
	bool bIsRecording = false;
	FString State;
	FString SequencePath;
	if (!FSequencerController::GetTakeRecorderStatus(bIsRecording, State, SequencePath))
	{
		return FMCPToolResult::Error(TEXT("Failed to get Take Recorder status"));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("is_recording"), bIsRecording);
	Data->SetStringField(TEXT("state"), State);
	if (!SequencePath.IsEmpty())
	{
		Data->SetStringField(TEXT("last_sequence_path"), SequencePath);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Take Recorder state: %s (recording: %s)"),
			*State, bIsRecording ? TEXT("true") : TEXT("false")), Data);
}

FMCPToolResult FMCPTool_Sequencer::ExecuteOpen(const TSharedRef<FJsonObject>& Params)
{
	FString SequencePath;
	TOptional<FMCPToolResult> OpenError;
	if (!ExtractRequiredString(Params, TEXT("sequence_path"), SequencePath, OpenError))
	{
		return OpenError.GetValue();
	}

	FString Error;
	if (!FSequencerController::OpenSequence(SequencePath, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	const TSharedPtr<FJsonObject>* CamLocObj = nullptr;
	const TSharedPtr<FJsonObject>* CamRotObj = nullptr;
	bool bSetCamera = false;
	if (Params->TryGetObjectField(TEXT("camera_location"), CamLocObj) && CamLocObj && (*CamLocObj).IsValid() &&
		Params->TryGetObjectField(TEXT("camera_rotation"), CamRotObj) && CamRotObj && (*CamRotObj).IsValid())
	{
		FVector Location(
			(*CamLocObj)->GetNumberField(TEXT("x")),
			(*CamLocObj)->GetNumberField(TEXT("y")),
			(*CamLocObj)->GetNumberField(TEXT("z")));
		FRotator Rotation(
			(*CamRotObj)->GetNumberField(TEXT("pitch")),
			(*CamRotObj)->GetNumberField(TEXT("yaw")),
			(*CamRotObj)->GetNumberField(TEXT("roll")));
		FSequencerController::SetViewportCamera(Location, Rotation);
		bSetCamera = true;
	}

	double FrameDouble = 0.0;
	bool bScrubbed = false;
	if (Params->TryGetNumberField(TEXT("frame"), FrameDouble))
	{
		FString ScrubError;
		if (FSequencerController::ScrubToFrame(static_cast<int32>(FrameDouble), ScrubError))
		{
			bScrubbed = true;
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("sequence_path"), SequencePath);
	Data->SetBoolField(TEXT("camera_set"), bSetCamera);
	if (bScrubbed)
	{
		Data->SetNumberField(TEXT("frame"), FrameDouble);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Opened sequence: %s%s%s"), *SequencePath,
			bScrubbed ? *FString::Printf(TEXT(" at frame %d"), static_cast<int32>(FrameDouble)) : TEXT(""),
			bSetCamera ? TEXT(" with camera positioned") : TEXT("")),
		Data);
}

FMCPToolResult FMCPTool_Sequencer::ExecuteScrub(const TSharedRef<FJsonObject>& Params)
{
	double FrameDouble = 0.0;
	if (!Params->TryGetNumberField(TEXT("frame"), FrameDouble))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: frame"));
	}

	int32 Frame = static_cast<int32>(FrameDouble);
	FString Error;
	if (!FSequencerController::ScrubToFrame(Frame, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("frame"), Frame);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Scrubbed to frame %d"), Frame), Data);
}

FMCPToolResult FMCPTool_Sequencer::ExecuteGetState(const TSharedRef<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> State;
	FString Error;
	if (!FSequencerController::GetSequencerState(State, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	return FMCPToolResult::Success(TEXT("Sequencer state retrieved"), State);
}

FMCPToolResult FMCPTool_Sequencer::ExecuteClose(const TSharedRef<FJsonObject>& Params)
{
	FString Error;
	if (!FSequencerController::CloseSequence(Error))
	{
		return FMCPToolResult::Error(Error);
	}

	return FMCPToolResult::Success(TEXT("Sequencer closed"));
}
