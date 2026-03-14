// Copyright Natali Caggiano. All Rights Reserved.

#include "SequencerController.h"
#include "UnrealClaudeModule.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "LevelSequence.h"
#include "ISequencer.h"
#include "ILevelSequenceEditorToolkit.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"

#include "Recorder/TakeRecorderSubsystem.h"
#include "Recorder/TakeRecorder.h"
#include "TakeRecorderActorSource.h"
#include "TakeRecorderSettings.h"

#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FrameRate.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "TakesUtils.h"

// ============================================================================
// Take Recorder
// ============================================================================

bool FSequencerController::StartTakeRecording(const FString& SlateName, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor not available");
		return false;
	}

	UTakeRecorderSubsystem* Subsystem = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>();
	if (!Subsystem)
	{
		OutError = TEXT("TakeRecorderSubsystem not available");
		return false;
	}

	if (Subsystem->IsRecording())
	{
		OutError = TEXT("Take Recorder is already recording");
		return false;
	}

	Subsystem->ClearSources();

	if (!SlateName.IsEmpty())
	{
		Subsystem->SetSlateName(SlateName);
	}

	auto* UserSettings = GetMutableDefault<UTakeRecorderUserSettings>();
	UserSettings->Settings.bSaveRecordedAssets = true;
	UserSettings->Settings.CountdownSeconds = 0.f;

	if (!Subsystem->StartRecording(false))
	{
		OutError = TEXT("Failed to start Take Recorder");
		return false;
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Take recording started (slate: %s)"),
		SlateName.IsEmpty() ? TEXT("default") : *SlateName);
	return true;
}

bool FSequencerController::StopTakeRecording(FString& OutSequencePath, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor not available");
		return false;
	}

	UTakeRecorderSubsystem* Subsystem = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>();
	if (!Subsystem)
	{
		OutError = TEXT("TakeRecorderSubsystem not available");
		return false;
	}

	if (!Subsystem->IsRecording())
	{
		OutError = TEXT("Take Recorder is not recording");
		return false;
	}

	ULevelSequence* PreStopSeq = Subsystem->GetRecordingLevelSequence();
	FString PreStopPath;
	if (PreStopSeq)
	{
		PreStopPath = PreStopSeq->GetPackage()->GetPathName();
	}

	Subsystem->StopRecording();

	ULevelSequence* RecordedSeq = Subsystem->GetLastRecordedLevelSequence();
	if (RecordedSeq)
	{
		OutSequencePath = RecordedSeq->GetPackage()->GetPathName();
	}
	else if (!PreStopPath.IsEmpty())
	{
		OutSequencePath = PreStopPath;
	}
	else
	{
		OutSequencePath = TEXT("");
		UE_LOG(LogUnrealClaude, Warning, TEXT("SequencerController: Take recording stopped but no sequence was produced"));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Take recording stopped, sequence: %s"),
		OutSequencePath.IsEmpty() ? TEXT("(none)") : *OutSequencePath);
	return true;
}

bool FSequencerController::GetTakeRecorderStatus(bool& bIsRecording, FString& OutState, FString& OutSequencePath)
{
	if (!GEditor) return false;

	UTakeRecorderSubsystem* Subsystem = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>();
	if (!Subsystem) return false;

	bIsRecording = Subsystem->IsRecording();
	ETakeRecorderState State = Subsystem->GetState();

	switch (State)
	{
	case ETakeRecorderState::PreInitialization: OutState = TEXT("pre_initialization"); break;
	case ETakeRecorderState::CountingDown: OutState = TEXT("counting_down"); break;
	case ETakeRecorderState::PreRecord: OutState = TEXT("pre_record"); break;
	case ETakeRecorderState::TickingAfterPre: OutState = TEXT("ticking_after_pre"); break;
	case ETakeRecorderState::Started: OutState = TEXT("started"); break;
	case ETakeRecorderState::Stopped: OutState = TEXT("stopped"); break;
	case ETakeRecorderState::Cancelled: OutState = TEXT("cancelled"); break;
	default: OutState = TEXT("unknown"); break;
	}

	ULevelSequence* Seq = Subsystem->GetLastRecordedLevelSequence();
	OutSequencePath = Seq ? Seq->GetPathName() : TEXT("");

	return true;
}

// ============================================================================
// Take Recorder for PIE
// ============================================================================

bool FSequencerController::StartTakeRecordingForPIE(UWorld* PIEWorld, const FString& SlateName, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor not available");
		return false;
	}

	UTakeRecorderSubsystem* Subsystem = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>();
	if (!Subsystem)
	{
		OutError = TEXT("TakeRecorderSubsystem not available");
		return false;
	}

	if (Subsystem->IsRecording())
	{
		OutError = TEXT("Take Recorder is already recording");
		return false;
	}

	Subsystem->ClearSources();

	if (PIEWorld)
	{
		APlayerController* PC = PIEWorld->GetFirstPlayerController();
		if (PC)
		{
			APawn* Pawn = PC->GetPawn();
			if (Pawn)
			{
				Subsystem->AddSourceForActor(Pawn, true, false);
				UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Added PIE pawn as recording source: %s"), *Pawn->GetName());
			}
		}
	}

	if (!SlateName.IsEmpty())
	{
		Subsystem->SetSlateName(SlateName);
	}

	auto* UserSettings = GetMutableDefault<UTakeRecorderUserSettings>();
	UserSettings->Settings.bSaveRecordedAssets = true;
	UserSettings->Settings.CountdownSeconds = 0.f;

	if (!Subsystem->StartRecording(false))
	{
		OutError = TEXT("Failed to start Take Recorder for PIE");
		return false;
	}

	Subsystem->SetSequenceCountdown(0.f);

	ETakeRecorderState PostStartState = Subsystem->GetState();
	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Take recording started for PIE (state=%d)"), (int32)PostStartState);
	return true;
}

bool FSequencerController::StopTakeRecordingForPIE(FString& OutSequencePath, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor not available");
		return false;
	}

	UTakeRecorderSubsystem* Subsystem = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>();
	if (!Subsystem)
	{
		OutError = TEXT("TakeRecorderSubsystem not available");
		return false;
	}

	if (!Subsystem->IsRecording())
	{
		OutError = TEXT("Take Recorder is not recording");
		return false;
	}

	ULevelSequence* RecordingSeq = Subsystem->GetRecordingLevelSequence();
	FString PreStopPath;
	if (RecordingSeq)
	{
		PreStopPath = RecordingSeq->GetPackage()->GetPathName();
		UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Pre-stop sequence: %s"), *PreStopPath);
	}

	ETakeRecorderState PreStopState = Subsystem->GetState();
	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Pre-stop state: %d (4=Started)"), (int32)PreStopState);

	Subsystem->StopRecording();

	ULevelSequence* LastSeq = Subsystem->GetLastRecordedLevelSequence();
	if (LastSeq)
	{
		OutSequencePath = LastSeq->GetPackage()->GetPathName();
		UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: LastRecordedLevelSequence: %s"), *OutSequencePath);

		TakesUtils::SaveAsset(LastSeq);
		UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: TakesUtils::SaveAsset completed for %s"), *OutSequencePath);
	}
	else if (!PreStopPath.IsEmpty())
	{
		OutSequencePath = PreStopPath;
		UE_LOG(LogUnrealClaude, Warning, TEXT("SequencerController: LastRecordedLevelSequence null (state was %d), using pre-stop path: %s"),
			(int32)PreStopState, *PreStopPath);
	}
	else
	{
		OutSequencePath = TEXT("");
		UE_LOG(LogUnrealClaude, Warning, TEXT("SequencerController: No sequence path available after stop"));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: PIE Take recording stopped, sequence: %s"),
		OutSequencePath.IsEmpty() ? TEXT("(none)") : *OutSequencePath);
	return true;
}

// ============================================================================
// Sequencer
// ============================================================================

bool FSequencerController::OpenSequence(const FString& AssetPath, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor not available");
		return false;
	}

	FString LoadPath = AssetPath;
	if (!LoadPath.Contains(TEXT(".")))
	{
		LoadPath = LoadPath + TEXT(".") + FPaths::GetBaseFilename(LoadPath);
	}

	ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *LoadPath);
	if (!Sequence)
	{
		Sequence = LoadObject<ULevelSequence>(nullptr, *AssetPath);
	}
	if (!Sequence)
	{
		OutError = FString::Printf(TEXT("Failed to load Level Sequence: %s"), *AssetPath);
		return false;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		OutError = TEXT("AssetEditorSubsystem not available");
		return false;
	}

	if (!AssetEditorSubsystem->OpenEditorForAsset(Sequence))
	{
		OutError = FString::Printf(TEXT("Failed to open editor for: %s"), *AssetPath);
		return false;
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Opened sequence: %s"), *AssetPath);
	return true;
}

bool FSequencerController::CloseSequence(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor not available");
		return false;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		OutError = TEXT("AssetEditorSubsystem not available");
		return false;
	}

	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
	int32 ClosedCount = 0;
	for (UObject* Asset : EditedAssets)
	{
		if (Cast<ULevelSequence>(Asset))
		{
			ClosedCount += AssetEditorSubsystem->CloseAllEditorsForAsset(Asset);
		}
	}

	if (ClosedCount == 0)
	{
		OutError = TEXT("No open Level Sequence editors found");
		return false;
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Closed %d sequencer editor(s)"), ClosedCount);
	return true;
}

bool FSequencerController::ScrubToFrame(int32 Frame, FString& OutError)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (!Sequencer.IsValid())
	{
		OutError = TEXT("No active Sequencer found");
		return false;
	}

	FFrameRate DisplayRate = Sequencer->GetFocusedDisplayRate();
	FFrameRate TickRes = Sequencer->GetFocusedTickResolution();
	FFrameTime TickTime = FFrameRate::TransformTime(FFrameTime(FFrameNumber(Frame)), DisplayRate, TickRes);
	Sequencer->SetLocalTimeDirectly(TickTime, true);

	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Scrubbed to display frame %d"), Frame);
	return true;
}

TSharedPtr<ISequencer> FSequencerController::GetActiveSequencer()
{
	if (!GEditor) return nullptr;

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem) return nullptr;

	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
	for (UObject* Asset : EditedAssets)
	{
		ULevelSequence* LevelSequence = Cast<ULevelSequence>(Asset);
		if (!LevelSequence) continue;

		IAssetEditorInstance* Editor = AssetEditorSubsystem->FindEditorForAsset(LevelSequence, false);
		if (!Editor) continue;

		ILevelSequenceEditorToolkit* Toolkit = static_cast<ILevelSequenceEditorToolkit*>(Editor);
		if (!Toolkit) continue;

		TSharedPtr<ISequencer> Sequencer = Toolkit->GetSequencer();
		if (Sequencer.IsValid())
		{
			return Sequencer;
		}
	}

	return nullptr;
}

// ============================================================================
// State
// ============================================================================

bool FSequencerController::GetSequencerState(TSharedPtr<FJsonObject>& OutState, FString& OutError)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (!Sequencer.IsValid())
	{
		OutError = TEXT("No active Sequencer found");
		return false;
	}

	OutState = MakeShared<FJsonObject>();

	FQualifiedFrameTime CurrentTime = Sequencer->GetLocalTime();
	FFrameRate DisplayRate = Sequencer->GetFocusedDisplayRate();
	FFrameRate TickRes = Sequencer->GetFocusedTickResolution();

	FFrameTime DisplayFrameTime = FFrameRate::TransformTime(CurrentTime.Time, TickRes, DisplayRate);
	int32 DisplayFrame = DisplayFrameTime.FloorToFrame().Value;

	OutState->SetNumberField(TEXT("current_frame"), DisplayFrame);
	OutState->SetStringField(TEXT("display_rate"), FString::Printf(TEXT("%d/%d"), DisplayRate.Numerator, DisplayRate.Denominator));
	OutState->SetNumberField(TEXT("playback_speed"), Sequencer->GetPlaybackSpeed());

	TRange<FFrameNumber> PlaybackRange = Sequencer->GetPlaybackRange();
	if (PlaybackRange.HasLowerBound() && PlaybackRange.HasUpperBound())
	{
		FFrameTime StartDisplay = FFrameRate::TransformTime(FFrameTime(PlaybackRange.GetLowerBoundValue()), TickRes, DisplayRate);
		FFrameTime EndDisplay = FFrameRate::TransformTime(FFrameTime(PlaybackRange.GetUpperBoundValue()), TickRes, DisplayRate);
		OutState->SetNumberField(TEXT("playback_start_frame"), StartDisplay.FloorToFrame().Value);
		OutState->SetNumberField(TEXT("playback_end_frame"), EndDisplay.FloorToFrame().Value);
	}

	UMovieSceneSequence* FocusedSeq = Sequencer->GetFocusedMovieSceneSequence();
	if (FocusedSeq)
	{
		OutState->SetStringField(TEXT("sequence_name"), FocusedSeq->GetName());
		OutState->SetStringField(TEXT("sequence_path"), FocusedSeq->GetPathName());
	}

	FVector CamLoc;
	FRotator CamRot;
	if (GetViewportCamera(CamLoc, CamRot))
	{
		TSharedPtr<FJsonObject> CamLocObj = MakeShared<FJsonObject>();
		CamLocObj->SetNumberField(TEXT("x"), CamLoc.X);
		CamLocObj->SetNumberField(TEXT("y"), CamLoc.Y);
		CamLocObj->SetNumberField(TEXT("z"), CamLoc.Z);
		OutState->SetObjectField(TEXT("camera_location"), CamLocObj);

		TSharedPtr<FJsonObject> CamRotObj = MakeShared<FJsonObject>();
		CamRotObj->SetNumberField(TEXT("pitch"), CamRot.Pitch);
		CamRotObj->SetNumberField(TEXT("yaw"), CamRot.Yaw);
		CamRotObj->SetNumberField(TEXT("roll"), CamRot.Roll);
		OutState->SetObjectField(TEXT("camera_rotation"), CamRotObj);
	}

	return true;
}

// ============================================================================
// Viewport Camera
// ============================================================================

bool FSequencerController::GetViewportCamera(FVector& OutLocation, FRotator& OutRotation)
{
	if (!GEditor) return false;

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditorModule.GetFirstActiveLevelViewport();
	if (!ActiveViewport.IsValid()) return false;

	FLevelEditorViewportClient& ViewportClient = ActiveViewport->GetLevelViewportClient();
	OutLocation = ViewportClient.GetViewLocation();
	OutRotation = ViewportClient.GetViewRotation();
	return true;
}

bool FSequencerController::SetViewportCamera(const FVector& Location, const FRotator& Rotation)
{
	if (!GEditor) return false;

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditorModule.GetFirstActiveLevelViewport();
	if (!ActiveViewport.IsValid()) return false;

	FLevelEditorViewportClient& ViewportClient = ActiveViewport->GetLevelViewportClient();
	ViewportClient.SetViewLocation(Location);
	ViewportClient.SetViewRotation(Rotation);
	ViewportClient.Invalidate();

	UE_LOG(LogUnrealClaude, Log, TEXT("SequencerController: Set viewport camera to (%.1f, %.1f, %.1f) P=%.1f Y=%.1f R=%.1f"),
		Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
	return true;
}
