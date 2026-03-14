// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_GameplayDebug.h"
#include "PIESequenceRunner.h"
#include "PIEFrameGrabber.h"
#include "UnrealClaudeModule.h"
#include "MCP/MCPParamValidator.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "UnrealClient.h"
#include "InputAction.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Animation/AnimInstance.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"

namespace
{
	UWorld* FindPIEWorld()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem(UWorld* PIEWorld)
	{
		if (!PIEWorld) return nullptr;
		APlayerController* PC = PIEWorld->GetFirstPlayerController();
		if (!PC || !PC->GetLocalPlayer()) return nullptr;
		return PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	}
}

FMCPTool_GameplayDebug::~FMCPTool_GameplayDebug()
{
	if (Monitor.bActive && Monitor.TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Monitor.TickerHandle);
		Monitor.bActive = false;
	}
	ContinuousInjections.Empty();
	if (ActiveSequence.IsValid())
	{
		ActiveSequence->Cancel();
	}
}

FMCPToolResult FMCPTool_GameplayDebug::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	if (Operation == TEXT("start_pie")) return ExecuteStartPIE(Params);
	if (Operation == TEXT("stop_pie")) return ExecuteStopPIE(Params);
	if (Operation == TEXT("pie_status")) return ExecutePIEStatus(Params);
	if (Operation == TEXT("inject_input")) return ExecuteInjectInput(Params);
	if (Operation == TEXT("start_continuous")) return ExecuteStartContinuous(Params);
	if (Operation == TEXT("update_continuous")) return ExecuteUpdateContinuous(Params);
	if (Operation == TEXT("stop_continuous")) return ExecuteStopContinuous(Params);
	if (Operation == TEXT("capture_pie")) return ExecuteCapturePIE(Params);
	if (Operation == TEXT("execute_sequence")) return ExecuteSequence(Params);
	if (Operation == TEXT("sequence_status")) return ExecuteSequenceStatus(Params);
	if (Operation == TEXT("run_sequence")) return ExecuteRunSequence(Params);
	if (Operation == TEXT("start_monitor")) return ExecuteStartMonitor(Params);
	if (Operation == TEXT("stop_monitor")) return ExecuteStopMonitor(Params);
	if (Operation == TEXT("monitor_status")) return ExecuteMonitorStatus(Params);
	if (Operation == TEXT("play_montage")) return ExecutePlayMontage(Params);
	if (Operation == TEXT("montage_jump_to_section")) return ExecuteMontageJumpToSection(Params);
	if (Operation == TEXT("montage_stop")) return ExecuteMontageStop(Params);

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: %s. Valid: run_sequence, start_pie, stop_pie, pie_status, inject_input, "
			"start_continuous, update_continuous, stop_continuous, capture_pie, execute_sequence, sequence_status, "
			"start_monitor, stop_monitor, monitor_status, play_montage, montage_jump_to_section, montage_stop"),
		*Operation));
}

// ============================================================================
// PIE Lifecycle
// ============================================================================

FMCPToolResult FMCPTool_GameplayDebug::ExecuteStartPIE(const TSharedRef<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("Editor not available"));
	}

	if (GEditor->IsPlaySessionInProgress())
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("already_running"), true);
		return FMCPToolResult::Success(TEXT("PIE session already running"), Data);
	}

	FRequestPlaySessionParams SessionParams;
	SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;

	GEditor->RequestPlaySession(SessionParams);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("requested"), true);
	Data->SetStringField(TEXT("note"), TEXT("PIE start is async. Poll pie_status until running."));

	return FMCPToolResult::Success(TEXT("PIE session requested"), Data);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteStopPIE(const TSharedRef<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("Editor not available"));
	}

	if (!GEditor->IsPlaySessionInProgress())
	{
		return FMCPToolResult::Success(TEXT("No PIE session running"));
	}

	ContinuousInjections.Empty();

	if (ActiveSequence.IsValid() && ActiveSequence->IsRunning())
	{
		ActiveSequence->Cancel();
	}

	GEditor->RequestEndPlayMap();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("stop_requested"), true);

	return FMCPToolResult::Success(TEXT("PIE stop requested"), Data);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecutePIEStatus(const TSharedRef<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("Editor not available"));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	bool bRunning = GEditor->IsPlaySessionInProgress();
	Data->SetBoolField(TEXT("running"), bRunning);

	if (bRunning)
	{
		Data->SetNumberField(TEXT("frame_number"), static_cast<double>(GFrameNumber));

		UWorld* PIEWorld = FindPIEWorld();
		if (PIEWorld)
		{
			Data->SetNumberField(TEXT("game_time"), PIEWorld->GetTimeSeconds());
			Data->SetNumberField(TEXT("real_time"), PIEWorld->GetRealTimeSeconds());

			APlayerController* PC = PIEWorld->GetFirstPlayerController();
			if (PC && PC->GetPawn())
			{
				FVector Loc = PC->GetPawn()->GetActorLocation();
				TSharedPtr<FJsonObject> PawnLoc = MakeShared<FJsonObject>();
				PawnLoc->SetNumberField(TEXT("x"), Loc.X);
				PawnLoc->SetNumberField(TEXT("y"), Loc.Y);
				PawnLoc->SetNumberField(TEXT("z"), Loc.Z);
				Data->SetObjectField(TEXT("pawn_location"), PawnLoc);
			}
		}

		Data->SetNumberField(TEXT("continuous_injections"), ContinuousInjections.Num());
	}

	return FMCPToolResult::Success(
		bRunning ? TEXT("PIE is running") : TEXT("PIE is not running"),
		Data);
}

// ============================================================================
// Input Injection
// ============================================================================

FMCPToolResult FMCPTool_GameplayDebug::ExecuteInjectInput(const TSharedRef<FJsonObject>& Params)
{
	UWorld* PIEWorld = FindPIEWorld();
	if (!PIEWorld)
	{
		return FMCPToolResult::Error(TEXT("PIE is not running. Call start_pie first."));
	}

	FString ActionPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("action_path"), ActionPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UInputAction* Action = LoadInputAction(ActionPath, LoadError);
	if (!Action)
	{
		return FMCPToolResult::Error(LoadError);
	}

	UEnhancedInputLocalPlayerSubsystem* InputSub = GetInputSubsystem(PIEWorld);
	if (!InputSub)
	{
		return FMCPToolResult::Error(TEXT("Enhanced Input subsystem not available in PIE"));
	}

	FInputActionValue Value = BuildInputValue(Action, Params);
	InputSub->InjectInputForAction(Action, Value, {}, {});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("action"), Action->GetName());
	Data->SetStringField(TEXT("action_path"), Action->GetPathName());
	Data->SetNumberField(TEXT("frame_number"), static_cast<double>(GFrameNumber));

	UE_LOG(LogUnrealClaude, Log, TEXT("GameplayDebug: Injected %s at frame %llu"), *Action->GetName(), GFrameNumber);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Injected %s at frame %llu"), *Action->GetName(), GFrameNumber),
		Data);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteStartContinuous(const TSharedRef<FJsonObject>& Params)
{
	UWorld* PIEWorld = FindPIEWorld();
	if (!PIEWorld)
	{
		return FMCPToolResult::Error(TEXT("PIE is not running. Call start_pie first."));
	}

	FString ActionPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("action_path"), ActionPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UInputAction* Action = LoadInputAction(ActionPath, LoadError);
	if (!Action)
	{
		return FMCPToolResult::Error(LoadError);
	}

	UEnhancedInputLocalPlayerSubsystem* InputSub = GetInputSubsystem(PIEWorld);
	if (!InputSub)
	{
		return FMCPToolResult::Error(TEXT("Enhanced Input subsystem not available in PIE"));
	}

	FString InjectionId = FString::Printf(TEXT("ci_%d"), NextContinuousId++);

	TSharedPtr<FContinuousInjection> Injection = MakeShared<FContinuousInjection>();
	Injection->Id = InjectionId;
	Injection->Action = Action;
	Injection->Value = BuildInputValue(Action, Params);

	PIEWorld->GetTimerManager().SetTimer(
		Injection->TimerHandle,
		FTimerDelegate::CreateLambda([this, InjectionId]()
		{
			TSharedPtr<FContinuousInjection>* Found = ContinuousInjections.Find(InjectionId);
			if (!Found || !Found->IsValid()) return;

			UWorld* World = FindPIEWorld();
			if (!World) return;

			UEnhancedInputLocalPlayerSubsystem* Sub = GetInputSubsystem(World);
			if (!Sub) return;

			UInputAction* Act = (*Found)->Action.Get();
			if (!Act) return;

			Sub->InjectInputForAction(Act, (*Found)->Value, {}, {});
		}),
		1.f / 60.f, true, 0.f);

	ContinuousInjections.Add(InjectionId, Injection);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("injection_id"), InjectionId);
	Data->SetStringField(TEXT("action"), Action->GetName());

	UE_LOG(LogUnrealClaude, Log, TEXT("GameplayDebug: Started continuous injection %s for %s"), *InjectionId, *Action->GetName());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Started continuous injection %s for %s"), *InjectionId, *Action->GetName()),
		Data);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteUpdateContinuous(const TSharedRef<FJsonObject>& Params)
{
	FString InjectionId;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("injection_id"), InjectionId, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FContinuousInjection>* Found = ContinuousInjections.Find(InjectionId);
	if (!Found || !Found->IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Continuous injection not found: %s"), *InjectionId));
	}

	UInputAction* Action = (*Found)->Action.Get();
	if (!Action)
	{
		return FMCPToolResult::Error(TEXT("InputAction has been garbage collected"));
	}

	(*Found)->Value = BuildInputValue(Action, Params);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("injection_id"), InjectionId);
	Data->SetStringField(TEXT("action"), Action->GetName());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Updated continuous injection %s"), *InjectionId),
		Data);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteStopContinuous(const TSharedRef<FJsonObject>& Params)
{
	FString InjectionId;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("injection_id"), InjectionId, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FContinuousInjection>* Found = ContinuousInjections.Find(InjectionId);
	if (!Found || !Found->IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Continuous injection not found: %s"), *InjectionId));
	}

	UWorld* PIEWorld = FindPIEWorld();
	if (PIEWorld && (*Found)->TimerHandle.IsValid())
	{
		PIEWorld->GetTimerManager().ClearTimer((*Found)->TimerHandle);
	}

	FString ActionName = (*Found)->Action.IsValid() ? (*Found)->Action->GetName() : TEXT("Unknown");
	ContinuousInjections.Remove(InjectionId);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("injection_id"), InjectionId);
	Data->SetStringField(TEXT("action"), ActionName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Stopped continuous injection %s (%s)"), *InjectionId, *ActionName),
		Data);
}

// ============================================================================
// Capture
// ============================================================================

FMCPToolResult FMCPTool_GameplayDebug::ExecuteCapturePIE(const TSharedRef<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FMCPToolResult::Error(TEXT("Editor not available"));
	}

	FViewport* Viewport = GEditor->GetPIEViewport();
	if (!Viewport)
	{
		return FMCPToolResult::Error(TEXT("PIE viewport not available. Is PIE running?"));
	}

	FIntPoint ViewportSize = Viewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return FMCPToolResult::Error(TEXT("PIE viewport has invalid size"));
	}

	TArray<FColor> Pixels;
	if (!Viewport->ReadPixels(Pixels))
	{
		return FMCPToolResult::Error(TEXT("Failed to read PIE viewport pixels"));
	}

	constexpr int32 TargetWidth = 1024;
	constexpr int32 TargetHeight = 576;
	constexpr int32 JPEGQuality = 70;

	TArray<FColor> ResizedPixels;
	ResizedPixels.SetNumUninitialized(TargetWidth * TargetHeight);

	const float ScaleX = static_cast<float>(ViewportSize.X) / TargetWidth;
	const float ScaleY = static_cast<float>(ViewportSize.Y) / TargetHeight;

	for (int32 Y = 0; Y < TargetHeight; ++Y)
	{
		for (int32 X = 0; X < TargetWidth; ++X)
		{
			const int32 SrcX = FMath::Clamp(static_cast<int32>(X * ScaleX), 0, ViewportSize.X - 1);
			const int32 SrcY = FMath::Clamp(static_cast<int32>(Y * ScaleY), 0, ViewportSize.Y - 1);
			ResizedPixels[Y * TargetWidth + X] = Pixels[SrcY * ViewportSize.X + SrcX];
		}
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	if (!ImageWrapper.IsValid())
	{
		return FMCPToolResult::Error(TEXT("Failed to create image wrapper"));
	}

	if (!ImageWrapper->SetRaw(ResizedPixels.GetData(), ResizedPixels.Num() * sizeof(FColor),
		TargetWidth, TargetHeight, ERGBFormat::BGRA, 8))
	{
		return FMCPToolResult::Error(TEXT("Failed to set image data"));
	}

	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(JPEGQuality);
	if (CompressedData.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Failed to compress JPEG"));
	}

	FString Base64Image = FBase64::Encode(CompressedData.GetData(), CompressedData.Num());

	double GameTime = 0.0;
	UWorld* PIEWorld = FindPIEWorld();
	if (PIEWorld)
	{
		GameTime = PIEWorld->GetTimeSeconds();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("image_base64"), Base64Image);
	Data->SetNumberField(TEXT("width"), TargetWidth);
	Data->SetNumberField(TEXT("height"), TargetHeight);
	Data->SetStringField(TEXT("format"), TEXT("jpeg"));
	Data->SetNumberField(TEXT("frame_number"), static_cast<double>(GFrameNumber));
	Data->SetNumberField(TEXT("game_time"), GameTime);
	Data->SetStringField(TEXT("viewport_type"), TEXT("PIE"));

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Captured PIE viewport at frame %llu (t=%.3f)"), GFrameNumber, GameTime),
		Data);
}

// ============================================================================
// Sequences
// ============================================================================

FMCPToolResult FMCPTool_GameplayDebug::ExecuteSequence(const TSharedRef<FJsonObject>& Params)
{
	if (ActiveSequence.IsValid() && ActiveSequence->IsRunning())
	{
		return FMCPToolResult::Error(TEXT("A sequence is already running. Use sequence_status to check, or stop_pie to cancel."));
	}

	const TArray<TSharedPtr<FJsonValue>>* StepsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("sequence"), StepsArray) || !StepsArray || StepsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing or empty 'sequence' array parameter"));
	}

	TArray<FPIESequenceStep> Steps;
	for (int32 i = 0; i < StepsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* StepObj = nullptr;
		if (!(*StepsArray)[i]->TryGetObject(StepObj) || !StepObj || !(*StepObj).IsValid())
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Invalid sequence step at index %d"), i));
		}

		FPIESequenceStep Step;
		(*StepObj)->TryGetStringField(TEXT("action"), Step.ActionPath);

		double DelayDouble = 0.0;
		if ((*StepObj)->TryGetNumberField(TEXT("delay_ms"), DelayDouble))
		{
			Step.DelayMs = static_cast<float>(DelayDouble);
		}

		double ValX = 1.0, ValY = 0.0, ValZ = 0.0;
		(*StepObj)->TryGetNumberField(TEXT("value_x"), ValX);
		(*StepObj)->TryGetNumberField(TEXT("value_y"), ValY);
		(*StepObj)->TryGetNumberField(TEXT("value_z"), ValZ);
		Step.ValueX = static_cast<float>(ValX);
		Step.ValueY = static_cast<float>(ValY);
		Step.ValueZ = static_cast<float>(ValZ);

		Steps.Add(MoveTemp(Step));
	}

	FString CaptureMode = ExtractOptionalString(Params, TEXT("capture_mode"), TEXT("normal"));
	int32 CaptureIntervalMs = ExtractOptionalNumber<int32>(Params, TEXT("capture_interval_ms"), 200);
	int32 CaptureEveryNFrames = ExtractOptionalNumber<int32>(Params, TEXT("capture_every_n_frames"), 3);

	FString Name = ExtractOptionalString(Params, TEXT("name"), TEXT("sequence"));
	FString OutputDir = ExtractOptionalString(Params, TEXT("output_dir"), TEXT(""));
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("pie-debug"), Name));
	}

	ActiveSequence = MakeShared<FPIESequenceRunner>();
	ActiveSequence->Start(Steps, CaptureMode, CaptureIntervalMs, CaptureEveryNFrames, OutputDir);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("steps"), Steps.Num());
	Data->SetStringField(TEXT("capture_mode"), CaptureMode);
	Data->SetStringField(TEXT("output_dir"), OutputDir);
	Data->SetStringField(TEXT("note"), TEXT("Sequence is async. Poll with sequence_status."));

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Started sequence '%s' with %d steps"), *Name, Steps.Num()),
		Data);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteSequenceStatus(const TSharedRef<FJsonObject>& Params)
{
	if (!ActiveSequence.IsValid())
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("state"), TEXT("idle"));
		return FMCPToolResult::Success(TEXT("No sequence running"), Data);
	}

	FPIESequenceResult Status = ActiveSequence->GetStatus();

	FString StateStr;
	switch (Status.State)
	{
	case EPIESequenceState::Idle: StateStr = TEXT("idle"); break;
	case EPIESequenceState::WaitingForPIE: StateStr = TEXT("waiting_for_pie"); break;
	case EPIESequenceState::Running: StateStr = TEXT("running"); break;
	case EPIESequenceState::Completed: StateStr = TEXT("completed"); break;
	case EPIESequenceState::Failed: StateStr = TEXT("failed"); break;
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), StateStr);
	Data->SetNumberField(TEXT("current_step"), Status.CurrentStep);
	Data->SetNumberField(TEXT("total_steps"), Status.TotalSteps);
	Data->SetNumberField(TEXT("captured_frames"), Status.CapturedFrames);

	if (!Status.ErrorMessage.IsEmpty())
	{
		Data->SetStringField(TEXT("error"), Status.ErrorMessage);
	}

	if (Status.State == EPIESequenceState::Completed || Status.State == EPIESequenceState::Failed)
	{
		Data->SetNumberField(TEXT("duration_sec"), Status.EndTime - Status.StartTime);

		TArray<TSharedPtr<FJsonValue>> FilesArray;
		for (const FString& File : Status.CapturedFiles)
		{
			FilesArray.Add(MakeShared<FJsonValueString>(File));
		}
		Data->SetArrayField(TEXT("captured_files"), FilesArray);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Sequence %s: step %d/%d, %d frames captured"),
			*StateStr, Status.CurrentStep, Status.TotalSteps, Status.CapturedFrames),
		Data);
}

// ============================================================================
// Atomic Run Sequence
// ============================================================================

FMCPToolResult FMCPTool_GameplayDebug::ExecuteRunSequence(const TSharedRef<FJsonObject>& Params)
{
	if (ActiveSequence.IsValid() && ActiveSequence->IsRunning())
	{
		return FMCPToolResult::Error(TEXT("A sequence is already running. Use sequence_status to check, or stop_pie to cancel."));
	}

	TSharedPtr<FJsonObject> FileRoot;
	FString SequenceFilePath;
	if (Params->TryGetStringField(TEXT("sequence_file"), SequenceFilePath) && !SequenceFilePath.IsEmpty())
	{
		FString FileContents;
		if (!FFileHelper::LoadFileToString(FileContents, *SequenceFilePath))
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Failed to read sequence_file: %s"), *SequenceFilePath));
		}

		TSharedPtr<FJsonValue> ParsedValue;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContents);
		if (!FJsonSerializer::Deserialize(Reader, ParsedValue) || !ParsedValue.IsValid())
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Failed to parse JSON from sequence_file: %s"), *SequenceFilePath));
		}

		if (ParsedValue->Type == EJson::Object)
		{
			FileRoot = ParsedValue->AsObject();
		}
		else if (ParsedValue->Type == EJson::Array)
		{
			FileRoot = MakeShared<FJsonObject>();
			FileRoot->SetArrayField(TEXT("steps"), ParsedValue->AsArray());
		}
		else
		{
			return FMCPToolResult::Error(TEXT("sequence_file must contain a JSON object or array"));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* StepsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("steps"), StepsArray) || !StepsArray || StepsArray->Num() == 0)
	{
		if (FileRoot.IsValid())
		{
			if (!FileRoot->TryGetArrayField(TEXT("steps"), StepsArray) || !StepsArray || StepsArray->Num() == 0)
			{
				return FMCPToolResult::Error(TEXT("sequence_file exists but contains no 'steps' array"));
			}
			auto ApplyFileDefault = [&](const FString& Key) {
				if (!Params->HasField(Key) && FileRoot->HasField(Key))
				{
					Params->SetField(Key, FileRoot->Values.FindRef(Key));
				}
			};
			ApplyFileDefault(TEXT("name"));
			ApplyFileDefault(TEXT("settle_ms"));
			ApplyFileDefault(TEXT("output_dir"));
			ApplyFileDefault(TEXT("auto_capture_every_n_frames"));
			ApplyFileDefault(TEXT("max_duration_ms"));
		}
		else
		{
			return FMCPToolResult::Error(TEXT("Missing or empty 'steps' — provide inline 'steps' array or 'sequence_file' path"));
		}
	}

	FString Name = ExtractOptionalString(Params, TEXT("name"), TEXT("sequence"));
	int32 SettleMs = ExtractOptionalNumber<int32>(Params, TEXT("settle_ms"), 500);

	FString OutputDir = ExtractOptionalString(Params, TEXT("output_dir"), TEXT(""));
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("pie-debug"), Name));
	}

	TArray<FPIESequenceStep> Steps;
	TSet<FString> ActionPaths;
	float CumulativeDelayMs = 0.f;
	float MaxCompletionMs = 0.f;

	for (int32 i = 0; i < StepsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* StepObj = nullptr;
		if (!(*StepsArray)[i]->TryGetObject(StepObj) || !StepObj || !(*StepObj).IsValid())
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Invalid step object at index %d"), i));
		}

		FString TypeStr;
		if (!(*StepObj)->TryGetStringField(TEXT("type"), TypeStr))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Step %d missing 'type' field"), i));
		}

		FPIESequenceStep Step;

		if (TypeStr == TEXT("input"))
		{
			Step.Type = EPIEStepType::Input;
		}
		else if (TypeStr == TEXT("hold"))
		{
			Step.Type = EPIEStepType::Hold;
		}
		else if (TypeStr == TEXT("capture"))
		{
			Step.Type = EPIEStepType::Capture;
		}
		else if (TypeStr == TEXT("console"))
		{
			Step.Type = EPIEStepType::Console;
		}
		else if (TypeStr == TEXT("input_tape"))
		{
			Step.Type = EPIEStepType::InputTape;
		}
		else
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Step %d: unknown type '%s'. Valid: input, hold, capture, console, input_tape"), i, *TypeStr));
		}

		double DelayDouble = 0.0;
		(*StepObj)->TryGetNumberField(TEXT("delay_ms"), DelayDouble);
		Step.DelayMs = static_cast<float>(DelayDouble);

		if (Step.Type == EPIEStepType::Input || Step.Type == EPIEStepType::Hold || Step.Type == EPIEStepType::InputTape)
		{
			FString ActionPath;
			if (!(*StepObj)->TryGetStringField(TEXT("action"), ActionPath) || ActionPath.IsEmpty())
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Step %d (%s): missing 'action' field"), i, *TypeStr));
			}
			Step.ActionPath = ActionPath;
			ActionPaths.Add(ActionPath);

			if (Step.Type != EPIEStepType::InputTape)
			{
				double ValX = 1.0, ValY = 0.0, ValZ = 0.0;
				(*StepObj)->TryGetNumberField(TEXT("value_x"), ValX);
				(*StepObj)->TryGetNumberField(TEXT("value_y"), ValY);
				(*StepObj)->TryGetNumberField(TEXT("value_z"), ValZ);
				Step.ValueX = static_cast<float>(ValX);
				Step.ValueY = static_cast<float>(ValY);
				Step.ValueZ = static_cast<float>(ValZ);
			}
		}

		if (Step.Type == EPIEStepType::InputTape)
		{
			const TArray<TSharedPtr<FJsonValue>>* ValuesArray = nullptr;
			if (!(*StepObj)->TryGetArrayField(TEXT("values"), ValuesArray) || !ValuesArray || ValuesArray->Num() == 0)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Step %d (input_tape): missing or empty 'values' array"), i));
			}
			Step.TapeValues.Reserve(ValuesArray->Num());
			for (int32 vi = 0; vi < ValuesArray->Num(); ++vi)
			{
				const TArray<TSharedPtr<FJsonValue>>* FrameVal = nullptr;
				if ((*ValuesArray)[vi]->TryGetArray(FrameVal) && FrameVal)
				{
					float X = FrameVal->Num() > 0 ? static_cast<float>((*FrameVal)[0]->AsNumber()) : 0.f;
					float Y = FrameVal->Num() > 1 ? static_cast<float>((*FrameVal)[1]->AsNumber()) : 0.f;
					float Z = FrameVal->Num() > 2 ? static_cast<float>((*FrameVal)[2]->AsNumber()) : 0.f;
					Step.TapeValues.Add(FVector(X, Y, Z));
				}
				else
				{
					double Num = 0.0;
					if ((*ValuesArray)[vi]->TryGetNumber(Num))
					{
						Step.TapeValues.Add(FVector(static_cast<float>(Num), 0.f, 0.f));
					}
					else
					{
						return FMCPToolResult::Error(FString::Printf(
							TEXT("Step %d (input_tape): invalid value at index %d"), i, vi));
					}
				}
			}
		}

		if (Step.Type == EPIEStepType::Hold)
		{
			double DurDouble = 0.0;
			(*StepObj)->TryGetNumberField(TEXT("duration_ms"), DurDouble);
			Step.DurationMs = static_cast<float>(DurDouble);
		}

		if (Step.Type == EPIEStepType::Capture)
		{
			FString CapName;
			if (!(*StepObj)->TryGetStringField(TEXT("name"), CapName) || CapName.IsEmpty())
			{
				CapName = FString::Printf(TEXT("capture_%d"), i);
			}
			Step.CaptureName = CapName;
		}

		if (Step.Type == EPIEStepType::Console)
		{
			FString Cmd;
			if (!(*StepObj)->TryGetStringField(TEXT("command"), Cmd) || Cmd.IsEmpty())
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Step %d (console): missing 'command' field"), i));
			}
			Step.Command = Cmd;
		}

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

		Steps.Add(MoveTemp(Step));
	}

	TMap<FString, TWeakObjectPtr<UInputAction>> PreLoadedActions;
	for (const FString& Path : ActionPaths)
	{
		FString LoadError;
		UInputAction* Action = LoadInputAction(Path, LoadError);
		if (!Action)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Pre-validation failed: %s"), *LoadError));
		}
		PreLoadedActions.Add(Path, Action);
	}

	int32 AutoCapEveryNFrames = 0;
	double AutoCapDouble = 0.0;
	if (Params->TryGetNumberField(TEXT("auto_capture_every_n_frames"), AutoCapDouble))
	{
		AutoCapEveryNFrames = static_cast<int32>(AutoCapDouble);
	}

	int32 MaxDurationMs = 120000;
	double MaxDurDouble = 0.0;
	if (Params->TryGetNumberField(TEXT("max_duration_ms"), MaxDurDouble))
	{
		MaxDurationMs = static_cast<int32>(MaxDurDouble);
	}

	bool bTakeRecord = false;
	Params->TryGetBoolField(TEXT("take_record"), bTakeRecord);
	FString TakeSlate = ExtractOptionalString(Params, TEXT("take_slate"), TEXT(""));

	int32 EstimatedDurationMs = SettleMs + static_cast<int32>(MaxCompletionMs) + 500;

	ActiveSequence = MakeShared<FPIESequenceRunner>();
	ActiveSequence->StartRunSequence(Steps, SettleMs, OutputDir, PreLoadedActions,
		AutoCapEveryNFrames, MaxDurationMs, bTakeRecord, TakeSlate);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Name);
	Data->SetNumberField(TEXT("total_steps"), Steps.Num());
	Data->SetStringField(TEXT("output_dir"), OutputDir);
	Data->SetNumberField(TEXT("estimated_duration_ms"), EstimatedDurationMs);
	Data->SetNumberField(TEXT("settle_ms"), SettleMs);
	Data->SetNumberField(TEXT("max_duration_ms"), MaxDurationMs);
	if (AutoCapEveryNFrames > 0)
	{
		Data->SetNumberField(TEXT("auto_capture_every_n_frames"), AutoCapEveryNFrames);
	}
	if (bTakeRecord)
	{
		Data->SetBoolField(TEXT("take_record"), true);
		if (!TakeSlate.IsEmpty())
		{
			Data->SetStringField(TEXT("take_slate"), TakeSlate);
		}
	}
	Data->SetStringField(TEXT("manifest_path"), OutputDir / TEXT("manifest.json"));
	Data->SetStringField(TEXT("note"), TEXT("Sequence running autonomously. Sleep for estimated_duration_ms + margin, then read manifest.json."));

	return FMCPToolResult::Success(
		FString::Printf(TEXT("run_sequence '%s' started: %d steps, est %dms, output=%s"), *Name, Steps.Num(), EstimatedDurationMs, *OutputDir),
		Data);
}

// ============================================================================
// Monitor Mode
// ============================================================================

FMCPToolResult FMCPTool_GameplayDebug::ExecuteStartMonitor(const TSharedRef<FJsonObject>& Params)
{
	if (Monitor.bActive)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("already_active"), true);
		Data->SetNumberField(TEXT("elapsed_ms"), MonitorGetElapsedMs());
		Data->SetNumberField(TEXT("events_logged"), Monitor.EventsLogged);
		return FMCPToolResult::Success(TEXT("Monitor already active"), Data);
	}

	bool bStartPIE = false;
	Params->TryGetBoolField(TEXT("start_pie"), bStartPIE);
	if (bStartPIE && GEditor && !GEditor->IsPlaySessionInProgress())
	{
		FRequestPlaySessionParams SessionParams;
		SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
		GEditor->RequestPlaySession(SessionParams);
	}

	Monitor = FPIEMonitor();
	Monitor.bActive = true;
	Monitor.StartTimeSeconds = FPlatformTime::Seconds();
	Monitor.LastStateLogTime = Monitor.StartTimeSeconds;
	Monitor.IntervalSeconds = ExtractOptionalNumber<float>(Params, TEXT("interval_ms"), 500.f) / 1000.f;
	Monitor.AxisThreshold = ExtractOptionalNumber<float>(Params, TEXT("axis_threshold"), 0.15f);
	Params->TryGetBoolField(TEXT("log_axes"), Monitor.bLogAxesPerTick);

	Monitor.TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FMCPTool_GameplayDebug::MonitorTick), 1.f / 60.f);

	UWorld* PIEWorld = FindPIEWorld();
	bool bPIERunning = PIEWorld != nullptr;
	if (bPIERunning)
	{
		MonitorAttachToPIE(PIEWorld);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] MONITOR_START interval=%dms threshold=%.2f pie=%s"),
		static_cast<int32>(Monitor.IntervalSeconds * 1000.f), Monitor.AxisThreshold,
		Monitor.bPIEAttached ? TEXT("attached") : TEXT("waiting"));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("monitoring"), true);
	Data->SetBoolField(TEXT("pie_running"), bPIERunning);
	Data->SetBoolField(TEXT("pie_attached"), Monitor.bPIEAttached);
	Data->SetNumberField(TEXT("interval_ms"), Monitor.IntervalSeconds * 1000.f);
	Data->SetNumberField(TEXT("axis_threshold"), Monitor.AxisThreshold);

	return FMCPToolResult::Success(
		bPIERunning ? TEXT("Monitor started (PIE attached)") : TEXT("Monitor started (waiting for PIE)"), Data);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteStopMonitor(const TSharedRef<FJsonObject>& Params)
{
	if (!Monitor.bActive)
	{
		return FMCPToolResult::Success(TEXT("Monitor not active"));
	}

	if (Monitor.TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Monitor.TickerHandle);
	}

	if (Monitor.bLogAxesPerTick)
	{
		UWorld* PIEWorld = FindPIEWorld();
		if (PIEWorld && GEngine)
		{
			GEngine->Exec(PIEWorld, TEXT("t.MaxFPS 0"));
		}
	}

	double DurationSec = FPlatformTime::Seconds() - Monitor.StartTimeSeconds;
	int32 EventCount = Monitor.EventsLogged;

	UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] MONITOR_STOP duration=%.1fs events=%d"), DurationSec, EventCount);

	Monitor = FPIEMonitor();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("duration_s"), DurationSec);
	Data->SetNumberField(TEXT("events_logged"), EventCount);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Monitor stopped: %.1fs, %d events"), DurationSec, EventCount), Data);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteMonitorStatus(const TSharedRef<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("active"), Monitor.bActive);

	if (Monitor.bActive)
	{
		Data->SetNumberField(TEXT("elapsed_ms"), MonitorGetElapsedMs());
		Data->SetNumberField(TEXT("events_logged"), Monitor.EventsLogged);
		Data->SetBoolField(TEXT("pie_attached"), Monitor.bPIEAttached);
		Data->SetNumberField(TEXT("tracked_actions"), Monitor.TrackedActions.Num());
		Data->SetNumberField(TEXT("interval_ms"), Monitor.IntervalSeconds * 1000.f);
		Data->SetNumberField(TEXT("axis_threshold"), Monitor.AxisThreshold);
	}

	return FMCPToolResult::Success(
		Monitor.bActive ? TEXT("Monitor is active") : TEXT("Monitor is inactive"), Data);
}

bool FMCPTool_GameplayDebug::MonitorTick(float DeltaTime)
{
	if (!Monitor.bActive)
	{
		return false;
	}

	UWorld* PIEWorld = FindPIEWorld();

	if (!Monitor.bPIEAttached && PIEWorld)
	{
		MonitorAttachToPIE(PIEWorld);
	}
	else if (Monitor.bPIEAttached && !PIEWorld)
	{
		MonitorDetachFromPIE();

		if (Monitor.bLogAxesPerTick)
		{
			double DurationSec = FPlatformTime::Seconds() - Monitor.StartTimeSeconds;
			UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] MONITOR_AUTO_STOP pie_ended duration=%.1fs events=%d"),
				DurationSec, Monitor.EventsLogged);
			Monitor.bActive = false;
			return false;
		}
	}

	if (!Monitor.bPIEAttached || !PIEWorld)
	{
		return true;
	}

	MonitorLogInputEdges(PIEWorld);

	double Now = FPlatformTime::Seconds();
	if ((Now - Monitor.LastStateLogTime) >= Monitor.IntervalSeconds)
	{
		MonitorLogStateSnapshot(PIEWorld);
		Monitor.LastStateLogTime = Now;
	}

	return true;
}

void FMCPTool_GameplayDebug::MonitorAttachToPIE(UWorld* PIEWorld)
{
	if (!PIEWorld) return;

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC) return;
	APawn* Pawn = PC->GetPawn();
	if (!Pawn || !Pawn->InputComponent) return;

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	if (!EIC) return;

	Monitor.TrackedActions.Empty();
	TSet<const UInputAction*> Seen;

	for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : EIC->GetActionEventBindings())
	{
		const UInputAction* Action = Binding->GetAction();
		if (!Action || Seen.Contains(Action)) continue;
		Seen.Add(Action);

		FMonitoredActionState State;
		State.Action = Action;
		State.ActionName = Action->GetName();
		State.ValueType = Action->ValueType;
		State.bWasActive = false;
		Monitor.TrackedActions.Add(MoveTemp(State));
	}

	Monitor.bPIEAttached = true;

	if (Monitor.bLogAxesPerTick && PIEWorld && GEngine)
	{
		GEngine->Exec(PIEWorld, TEXT("t.MaxFPS 60"));
		UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] Set t.MaxFPS 60 for axis recording"));
	}

	int64 ElapsedMs = MonitorGetElapsedMs();
	UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld ATTACHED actions=%d"), ElapsedMs, Monitor.TrackedActions.Num());
	Monitor.EventsLogged++;

	for (const FMonitoredActionState& AS : Monitor.TrackedActions)
	{
		FString TypeStr;
		switch (AS.ValueType)
		{
		case EInputActionValueType::Boolean: TypeStr = TEXT("Digital"); break;
		case EInputActionValueType::Axis1D:  TypeStr = TEXT("Axis1D"); break;
		case EInputActionValueType::Axis2D:  TypeStr = TEXT("Axis2D"); break;
		case EInputActionValueType::Axis3D:  TypeStr = TEXT("Axis3D"); break;
		}
		UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld ACTION_DISCOVERED %s type=%s"), ElapsedMs, *AS.ActionName, *TypeStr);
		Monitor.EventsLogged++;
	}
}

void FMCPTool_GameplayDebug::MonitorDetachFromPIE()
{
	int64 ElapsedMs = MonitorGetElapsedMs();
	UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld DETACHED"), ElapsedMs);
	Monitor.EventsLogged++;
	Monitor.bPIEAttached = false;
	Monitor.TrackedActions.Empty();
}

void FMCPTool_GameplayDebug::MonitorLogInputEdges(UWorld* PIEWorld)
{
	UEnhancedInputLocalPlayerSubsystem* InputSub = GetInputSubsystem(PIEWorld);
	if (!InputSub) return;
	UEnhancedPlayerInput* PlayerInput = InputSub->GetPlayerInput();
	if (!PlayerInput) return;

	int64 ElapsedMs = MonitorGetElapsedMs();

	for (FMonitoredActionState& AS : Monitor.TrackedActions)
	{
		if (!AS.Action.IsValid()) continue;

		FInputActionValue CurrentValue = PlayerInput->GetActionValue(AS.Action.Get());

		switch (AS.ValueType)
		{
		case EInputActionValueType::Boolean:
		{
			bool bNow = CurrentValue.Get<bool>();
			if (bNow && !AS.bWasActive)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s triggered"), ElapsedMs, *AS.ActionName);
				Monitor.EventsLogged++;
			}
			else if (!bNow && AS.bWasActive)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s released"), ElapsedMs, *AS.ActionName);
				Monitor.EventsLogged++;
			}
			AS.bWasActive = bNow;
			break;
		}
		case EInputActionValueType::Axis1D:
		{
			float Val = CurrentValue.Get<float>();
			float LastVal = AS.LastValue.Get<float>();
			bool bNowActive = FMath::Abs(Val) > Monitor.AxisThreshold;
			if (bNowActive && !AS.bWasActive)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s started val=%.2f"), ElapsedMs, *AS.ActionName, Val);
				Monitor.EventsLogged++;
			}
			else if (!bNowActive && AS.bWasActive)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s released"), ElapsedMs, *AS.ActionName);
				Monitor.EventsLogged++;
			}
			else if (bNowActive && AS.bWasActive && FMath::Abs(Val - LastVal) > Monitor.AxisThreshold)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s changed val=%.2f"), ElapsedMs, *AS.ActionName, Val);
				Monitor.EventsLogged++;
			}
			AS.bWasActive = bNowActive;
			break;
		}
		case EInputActionValueType::Axis2D:
		{
			FVector2D Val = CurrentValue.Get<FVector2D>();
			FVector2D LastVal = AS.LastValue.Get<FVector2D>();
			float Mag = Val.Size();
			bool bNowActive = Mag > Monitor.AxisThreshold;
			if (bNowActive && !AS.bWasActive)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s started val=(%.2f,%.2f)"), ElapsedMs, *AS.ActionName, Val.X, Val.Y);
				Monitor.EventsLogged++;
			}
			else if (!bNowActive && AS.bWasActive)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s released"), ElapsedMs, *AS.ActionName);
				Monitor.EventsLogged++;
			}
			else if (bNowActive && AS.bWasActive && FVector2D::Distance(Val, LastVal) > Monitor.AxisThreshold)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s changed val=(%.2f,%.2f)"), ElapsedMs, *AS.ActionName, Val.X, Val.Y);
				Monitor.EventsLogged++;
			}
			AS.bWasActive = bNowActive;
			break;
		}
		case EInputActionValueType::Axis3D:
		{
			FVector Val = CurrentValue.Get<FVector>();
			FVector LastVal = AS.LastValue.Get<FVector>();
			float Mag = Val.Size();
			bool bNowActive = Mag > Monitor.AxisThreshold;
			if (bNowActive && !AS.bWasActive)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s started val=(%.2f,%.2f,%.2f)"), ElapsedMs, *AS.ActionName, Val.X, Val.Y, Val.Z);
				Monitor.EventsLogged++;
			}
			else if (!bNowActive && AS.bWasActive)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s released"), ElapsedMs, *AS.ActionName);
				Monitor.EventsLogged++;
			}
			else if (bNowActive && AS.bWasActive && FVector::Distance(Val, LastVal) > Monitor.AxisThreshold)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld INPUT %s changed val=(%.2f,%.2f,%.2f)"), ElapsedMs, *AS.ActionName, Val.X, Val.Y, Val.Z);
				Monitor.EventsLogged++;
			}
			AS.bWasActive = bNowActive;
			break;
		}
		}

		AS.LastValue = CurrentValue;
	}

	if (Monitor.bLogAxesPerTick)
	{
		FString AxisLine;
		for (const FMonitoredActionState& AS : Monitor.TrackedActions)
		{
			if (!AS.Action.IsValid() || !AS.bWasActive) continue;
			switch (AS.ValueType)
			{
			case EInputActionValueType::Boolean:
				AxisLine += FString::Printf(TEXT(" %s=1"), *AS.ActionName);
				break;
			case EInputActionValueType::Axis1D:
				AxisLine += FString::Printf(TEXT(" %s=%.3f"), *AS.ActionName, AS.LastValue.Get<float>());
				break;
			case EInputActionValueType::Axis2D:
			{
				FVector2D v = AS.LastValue.Get<FVector2D>();
				AxisLine += FString::Printf(TEXT(" %s=(%.3f,%.3f)"), *AS.ActionName, v.X, v.Y);
				break;
			}
			case EInputActionValueType::Axis3D:
			{
				FVector v = AS.LastValue.Get<FVector>();
				AxisLine += FString::Printf(TEXT(" %s=(%.3f,%.3f,%.3f)"), *AS.ActionName, v.X, v.Y, v.Z);
				break;
			}
			}
		}
		if (!AxisLine.IsEmpty())
		{
			UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld AXIS%s"), ElapsedMs, *AxisLine);
			Monitor.EventsLogged++;
		}
	}
}

void FMCPTool_GameplayDebug::MonitorLogStateSnapshot(UWorld* PIEWorld)
{
	APlayerController* PC = PIEWorld ? PIEWorld->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn) return;

	FVector Loc = Pawn->GetActorLocation();
	FRotator Rot = Pawn->GetActorRotation();

	FVector Vel = FVector::ZeroVector;
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (Character && Character->GetCharacterMovement())
	{
		Vel = Character->GetCharacterMovement()->Velocity;
	}

	FString MontageStr = TEXT("None");
	if (Character)
	{
		UAnimInstance* AnimInst = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		if (AnimInst)
		{
			UAnimMontage* Montage = AnimInst->GetCurrentActiveMontage();
			if (Montage)
			{
				FName Section = AnimInst->Montage_GetCurrentSection(Montage);
				MontageStr = FString::Printf(TEXT("%s:%s"), *Montage->GetName(), *Section.ToString());
			}
		}
	}

	FString InputStr;
	UEnhancedInputLocalPlayerSubsystem* InputSub = GetInputSubsystem(PIEWorld);
	UEnhancedPlayerInput* PlayerInput = InputSub ? InputSub->GetPlayerInput() : nullptr;
	if (PlayerInput)
	{
		for (const FMonitoredActionState& AS : Monitor.TrackedActions)
		{
			if (!AS.Action.IsValid()) continue;
			FInputActionValue Val = PlayerInput->GetActionValue(AS.Action.Get());
			switch (AS.ValueType)
			{
			case EInputActionValueType::Boolean:
			{
				bool b = Val.Get<bool>();
				if (b) InputStr += FString::Printf(TEXT(" %s=1"), *AS.ActionName);
				break;
			}
			case EInputActionValueType::Axis1D:
			{
				float f = Val.Get<float>();
				if (FMath::Abs(f) > 0.001f)
					InputStr += FString::Printf(TEXT(" %s=%.3f"), *AS.ActionName, f);
				break;
			}
			case EInputActionValueType::Axis2D:
			{
				FVector2D v = Val.Get<FVector2D>();
				if (v.SizeSquared() > 0.000001f)
					InputStr += FString::Printf(TEXT(" %s=(%.3f,%.3f)"), *AS.ActionName, v.X, v.Y);
				break;
			}
			case EInputActionValueType::Axis3D:
			{
				FVector v = Val.Get<FVector>();
				if (v.SizeSquared() > 0.000001f)
					InputStr += FString::Printf(TEXT(" %s=(%.3f,%.3f,%.3f)"), *AS.ActionName, v.X, v.Y, v.Z);
				break;
			}
			}
		}
	}

	int64 ElapsedMs = MonitorGetElapsedMs();
	UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-DBG] t=%lld STATE pos=(%.1f,%.1f,%.1f) rot=(%.1f,%.1f,%.1f) vel=(%.1f,%.1f,%.1f) montage=%s%s"),
		ElapsedMs, Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll, Vel.X, Vel.Y, Vel.Z, *MontageStr, *InputStr);
	Monitor.EventsLogged++;
}

int64 FMCPTool_GameplayDebug::MonitorGetElapsedMs() const
{
	return static_cast<int64>((FPlatformTime::Seconds() - Monitor.StartTimeSeconds) * 1000.0);
}

// ============================================================================
// Montage Control
// ============================================================================

namespace
{
	USkeletalMeshComponent* FindSkeletalMeshComponent(UWorld* PIEWorld, const FString& ComponentName)
	{
		if (!PIEWorld) return nullptr;
		APlayerController* PC = PIEWorld->GetFirstPlayerController();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn) return nullptr;

		if (ComponentName.IsEmpty())
		{
			// Default: ACharacter::GetMesh() or first SkeletalMeshComponent
			ACharacter* Character = Cast<ACharacter>(Pawn);
			if (Character && Character->GetMesh())
			{
				return Character->GetMesh();
			}
			return Pawn->FindComponentByClass<USkeletalMeshComponent>();
		}

		// Named: search all SkeletalMeshComponents by GetName()
		TArray<USkeletalMeshComponent*> Components;
		Pawn->GetComponents<USkeletalMeshComponent>(Components);
		for (USkeletalMeshComponent* Comp : Components)
		{
			if (Comp && Comp->GetName() == ComponentName)
			{
				return Comp;
			}
		}
		return nullptr;
	}
}

FMCPToolResult FMCPTool_GameplayDebug::ExecutePlayMontage(const TSharedRef<FJsonObject>& Params)
{
	UWorld* PIEWorld = FindPIEWorld();
	if (!PIEWorld)
	{
		return FMCPToolResult::Error(TEXT("No PIE session running. Call start_pie first."));
	}

	FString MontagePath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Error))
	{
		return Error.GetValue();
	}

	FString ComponentName = ExtractOptionalString(Params, TEXT("component_name"));
	float PlayRate = ExtractOptionalNumber<float>(Params, TEXT("play_rate"), 1.0f);
	FString StartSection = ExtractOptionalString(Params, TEXT("start_section"));

	// Load the montage
	FString AdjustedPath = MontagePath;
	if (!AdjustedPath.Contains(TEXT(".")))
	{
		AdjustedPath += TEXT(".") + FPaths::GetBaseFilename(MontagePath);
	}
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AdjustedPath);
	if (!Montage)
	{
		Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
	}
	if (!Montage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimMontage: %s"), *MontagePath));
	}

	USkeletalMeshComponent* MeshComp = FindSkeletalMeshComponent(PIEWorld, ComponentName);
	if (!MeshComp)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("SkeletalMeshComponent '%s' not found on pawn"), *ComponentName));
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("No AnimInstance on component '%s'"), *MeshComp->GetName()));
	}

	float Duration = AnimInstance->Montage_Play(Montage, PlayRate);
	if (Duration <= 0.f)
	{
		return FMCPToolResult::Error(TEXT("Montage_Play returned 0 — montage may be incompatible with skeleton"));
	}

	if (!StartSection.IsEmpty())
	{
		AnimInstance->Montage_JumpToSection(FName(*StartSection), Montage);
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("montage"), Montage->GetName());
	ResultData->SetStringField(TEXT("component"), MeshComp->GetName());
	ResultData->SetNumberField(TEXT("duration"), Duration);
	ResultData->SetNumberField(TEXT("play_rate"), PlayRate);
	if (!StartSection.IsEmpty())
	{
		ResultData->SetStringField(TEXT("start_section"), StartSection);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Playing montage '%s' on '%s' (duration=%.2fs, rate=%.1f)"),
			*Montage->GetName(), *MeshComp->GetName(), Duration, PlayRate),
		ResultData);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteMontageJumpToSection(const TSharedRef<FJsonObject>& Params)
{
	UWorld* PIEWorld = FindPIEWorld();
	if (!PIEWorld)
	{
		return FMCPToolResult::Error(TEXT("No PIE session running."));
	}

	FString SectionName;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("section_name"), SectionName, Error))
	{
		return Error.GetValue();
	}

	FString ComponentName = ExtractOptionalString(Params, TEXT("component_name"));
	FString MontagePath = ExtractOptionalString(Params, TEXT("montage_path"));

	USkeletalMeshComponent* MeshComp = FindSkeletalMeshComponent(PIEWorld, ComponentName);
	if (!MeshComp)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("SkeletalMeshComponent '%s' not found on pawn"), *ComponentName));
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("No AnimInstance on component '%s'"), *MeshComp->GetName()));
	}

	// If montage_path specified, load it; otherwise use current active montage
	UAnimMontage* Montage = nullptr;
	if (!MontagePath.IsEmpty())
	{
		FString AdjustedPath = MontagePath;
		if (!AdjustedPath.Contains(TEXT(".")))
		{
			AdjustedPath += TEXT(".") + FPaths::GetBaseFilename(MontagePath);
		}
		Montage = LoadObject<UAnimMontage>(nullptr, *AdjustedPath);
		if (!Montage)
		{
			Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
		}
		if (!Montage)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimMontage: %s"), *MontagePath));
		}
	}
	else
	{
		Montage = AnimInstance->GetCurrentActiveMontage();
		if (!Montage)
		{
			return FMCPToolResult::Error(TEXT("No active montage and no montage_path specified"));
		}
	}

	AnimInstance->Montage_JumpToSection(FName(*SectionName), Montage);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("section"), SectionName);
	ResultData->SetStringField(TEXT("montage"), Montage->GetName());
	ResultData->SetStringField(TEXT("component"), MeshComp->GetName());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Jumped to section '%s' in montage '%s' on '%s'"),
			*SectionName, *Montage->GetName(), *MeshComp->GetName()),
		ResultData);
}

FMCPToolResult FMCPTool_GameplayDebug::ExecuteMontageStop(const TSharedRef<FJsonObject>& Params)
{
	UWorld* PIEWorld = FindPIEWorld();
	if (!PIEWorld)
	{
		return FMCPToolResult::Error(TEXT("No PIE session running."));
	}

	FString ComponentName = ExtractOptionalString(Params, TEXT("component_name"));
	float BlendOutTime = ExtractOptionalNumber<float>(Params, TEXT("blend_out_time"), 0.25f);

	USkeletalMeshComponent* MeshComp = FindSkeletalMeshComponent(PIEWorld, ComponentName);
	if (!MeshComp)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("SkeletalMeshComponent '%s' not found on pawn"), *ComponentName));
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("No AnimInstance on component '%s'"), *MeshComp->GetName()));
	}

	UAnimMontage* ActiveMontage = AnimInstance->GetCurrentActiveMontage();
	FString MontageName = ActiveMontage ? ActiveMontage->GetName() : TEXT("None");

	AnimInstance->Montage_Stop(BlendOutTime);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("stopped_montage"), MontageName);
	ResultData->SetStringField(TEXT("component"), MeshComp->GetName());
	ResultData->SetNumberField(TEXT("blend_out_time"), BlendOutTime);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Stopped montage '%s' on '%s' (blend=%.2fs)"),
			*MontageName, *MeshComp->GetName(), BlendOutTime),
		ResultData);
}

// ============================================================================
// Helpers
// ============================================================================

UInputAction* FMCPTool_GameplayDebug::LoadInputAction(const FString& Path, FString& OutError)
{
	FString AdjustedPath = Path;
	if (!AdjustedPath.Contains(TEXT(".")))
	{
		AdjustedPath += TEXT(".") + FPaths::GetBaseFilename(Path);
	}

	UInputAction* Action = LoadObject<UInputAction>(nullptr, *AdjustedPath);
	if (!Action)
	{
		Action = LoadObject<UInputAction>(nullptr, *Path);
	}

	if (!Action)
	{
		OutError = FString::Printf(TEXT("Failed to load InputAction: %s"), *Path);
	}

	return Action;
}

FInputActionValue FMCPTool_GameplayDebug::BuildInputValue(UInputAction* Action, const TSharedRef<FJsonObject>& Params)
{
	if (!Action)
	{
		return FInputActionValue(true);
	}

	float ValX = ExtractOptionalNumber<float>(Params, TEXT("value_x"), 1.f);
	float ValY = ExtractOptionalNumber<float>(Params, TEXT("value_y"), 0.f);
	float ValZ = ExtractOptionalNumber<float>(Params, TEXT("value_z"), 0.f);

	switch (Action->ValueType)
	{
	case EInputActionValueType::Boolean:
		return FInputActionValue(ValX != 0.f);
	case EInputActionValueType::Axis1D:
		return FInputActionValue(ValX);
	case EInputActionValueType::Axis2D:
		return FInputActionValue(FVector2D(ValX, ValY));
	case EInputActionValueType::Axis3D:
		return FInputActionValue(FVector(ValX, ValY, ValZ));
	default:
		return FInputActionValue(true);
	}
}
