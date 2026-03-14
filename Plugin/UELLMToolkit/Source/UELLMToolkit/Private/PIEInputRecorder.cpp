// Copyright Natali Caggiano. All Rights Reserved.

#include "PIEInputRecorder.h"
#include "UnrealClaudeModule.h"

#include "Editor.h"
#include "InputAction.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Animation/AnimInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	UWorld* FindPIEWorldForRecorder()
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

	FString ValueTypeToString(EInputActionValueType Type)
	{
		switch (Type)
		{
		case EInputActionValueType::Boolean: return TEXT("Digital");
		case EInputActionValueType::Axis1D:  return TEXT("Axis1D");
		case EInputActionValueType::Axis2D:  return TEXT("Axis2D");
		case EInputActionValueType::Axis3D:  return TEXT("Axis3D");
		}
		return TEXT("Unknown");
	}
}

FPIEInputRecorder::FPIEInputRecorder()
{
	BeginPIEHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FPIEInputRecorder::OnBeginPIE);
	EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FPIEInputRecorder::OnEndPIE);
}

FPIEInputRecorder::~FPIEInputRecorder()
{
	FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
	FEditorDelegates::EndPIE.Remove(EndPIEHandle);

	if (OnEndFrameHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
		OnEndFrameHandle.Reset();
	}
}

void FPIEInputRecorder::OnBeginPIE(bool bIsSimulating)
{
	if (!bEnabled) return;

	Reset();

	OnEndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FPIEInputRecorder::OnEndFrameTick);
	State = EPIERecorderState::WaitingForPawn;

	UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-REC] PIE started, waiting for pawn..."));
}

void FPIEInputRecorder::OnEndPIE(bool bIsSimulating)
{
	if (State == EPIERecorderState::Idle) return;

	if (OnEndFrameHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
		OnEndFrameHandle.Reset();
	}

	if (RecordedFrames.Num() > 0)
	{
		FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
		FString OutputDir = FPaths::ProjectSavedDir() / TEXT("Recordings") / (TEXT("recording-") + Timestamp);
		IFileManager::Get().MakeDirectory(*OutputDir, true);

		FString CSVPath = OutputDir / TEXT("recording.csv");
		FString SequencePath = OutputDir / TEXT("sequence.json");

		WriteCSV(CSVPath);
		WriteSequenceJSON(SequencePath);

		double Duration = 0.0;
		if (RecordedFrames.Num() >= 2)
		{
			Duration = RecordedFrames.Last().GameTime - RecordedFrames[0].GameTime;
		}

		LastResult = FRecordingResult();
		LastResult.CSVPath = CSVPath;
		LastResult.SequencePath = SequencePath;
		LastResult.OutputDir = OutputDir;
		LastResult.TotalFrames = RecordedFrames.Num();
		LastResult.DurationSeconds = Duration;
		for (const FRecordedAction& RA : TrackedActions)
		{
			LastResult.DiscoveredActions.Add(FString::Printf(TEXT("%s (%s)"), *RA.ActionName, *ValueTypeToString(RA.ValueType)));
		}

		UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-REC] Recorded %d frames (%.1fs) -> %s"), RecordedFrames.Num(), Duration, *OutputDir);
	}
	else
	{
		UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-REC] PIE ended with no recorded frames"));
	}

	State = EPIERecorderState::Idle;
}

void FPIEInputRecorder::OnEndFrameTick()
{
	UWorld* PIEWorld = FindPIEWorldForRecorder();
	if (!PIEWorld) return;

	if (State == EPIERecorderState::WaitingForPawn)
	{
		if (AttachToPIE())
		{
			State = EPIERecorderState::Recording;
		}
		return;
	}

	if (State != EPIERecorderState::Recording) return;

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC) return;
	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP) return;
	UEnhancedInputLocalPlayerSubsystem* InputSub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSub) return;
	UEnhancedPlayerInput* PlayerInput = InputSub->GetPlayerInput();
	if (!PlayerInput) return;

	RecordFrame(PlayerInput);
}

bool FPIEInputRecorder::AttachToPIE()
{
	UWorld* PIEWorld = FindPIEWorldForRecorder();
	if (!PIEWorld) return false;

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC) return false;
	APawn* Pawn = PC->GetPawn();
	if (!Pawn || !Pawn->InputComponent) return false;

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	if (!EIC) return false;

	TrackedActions.Empty();
	TSet<const UInputAction*> Seen;

	for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : EIC->GetActionEventBindings())
	{
		const UInputAction* Action = Binding->GetAction();
		if (!Action || Seen.Contains(Action)) continue;
		Seen.Add(Action);

		FRecordedAction RA;
		RA.Action = Action;
		RA.ActionName = Action->GetName();
		RA.ActionPath = Action->GetPathName();
		RA.ValueType = Action->ValueType;
		RA.bWasActive = false;
		TrackedActions.Add(MoveTemp(RA));
	}

	if (PIEWorld && GEngine)
	{
		GEngine->Exec(PIEWorld, TEXT("t.MaxFPS 60"));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-REC] Recording started, %d actions discovered, t.MaxFPS=60"), TrackedActions.Num());
	for (const FRecordedAction& RA : TrackedActions)
	{
		UE_LOG(LogUnrealClaude, Log, TEXT("[PIE-REC]   %s (%s) -> %s"), *RA.ActionName, *ValueTypeToString(RA.ValueType), *RA.ActionPath);
	}

	return true;
}

void FPIEInputRecorder::RecordFrame(UEnhancedPlayerInput* PlayerInput)
{
	UWorld* PIEWorld = FindPIEWorldForRecorder();
	if (!PIEWorld) return;

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	if (!PC) return;
	APawn* Pawn = PC->GetPawn();
	if (!Pawn) return;

	FRecordedInputFrame Frame;
	Frame.FrameNumber = GFrameNumber;
	Frame.GameTime = PIEWorld->GetTimeSeconds();
	Frame.DeltaTime = PIEWorld->GetDeltaSeconds();
	Frame.PawnLocation = Pawn->GetActorLocation();
	Frame.PawnRotation = Pawn->GetActorRotation();

	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (Character && Character->GetCharacterMovement())
	{
		Frame.PawnVelocity = Character->GetCharacterMovement()->Velocity;
		Frame.Speed2D = Character->GetCharacterMovement()->Velocity.Size2D();
	}

	if (Character)
	{
		UAnimInstance* AnimInst = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		if (AnimInst)
		{
			UAnimMontage* Montage = AnimInst->GetCurrentActiveMontage();
			if (Montage)
			{
				FName Section = AnimInst->Montage_GetCurrentSection(Montage);
				Frame.ActiveMontage = FString::Printf(TEXT("%s:%s"), *Montage->GetName(), *Section.ToString());
			}
		}
	}

	for (FRecordedAction& RA : TrackedActions)
	{
		if (!RA.Action.IsValid()) continue;

		FInputActionValue CurrentValue = PlayerInput->GetActionValue(RA.Action.Get());

		switch (RA.ValueType)
		{
		case EInputActionValueType::Boolean:
		{
			bool bNow = CurrentValue.Get<bool>();
			Frame.ActionValues.Add(RA.ActionName, FVector(bNow ? 1.0 : 0.0, 0.0, 0.0));
			if (bNow && !RA.bWasActive)
			{
				Frame.EdgeEvents.Add(RA.ActionName + TEXT("_pressed"));
			}
			else if (!bNow && RA.bWasActive)
			{
				Frame.EdgeEvents.Add(RA.ActionName + TEXT("_released"));
			}
			RA.bWasActive = bNow;
			break;
		}
		case EInputActionValueType::Axis1D:
		{
			float Val = CurrentValue.Get<float>();
			Frame.ActionValues.Add(RA.ActionName, FVector(Val, 0.0, 0.0));
			bool bNowActive = FMath::Abs(Val) > AxisThreshold;
			if (bNowActive && !RA.bWasActive)
			{
				Frame.EdgeEvents.Add(RA.ActionName + TEXT("_pressed"));
			}
			else if (!bNowActive && RA.bWasActive)
			{
				Frame.EdgeEvents.Add(RA.ActionName + TEXT("_released"));
			}
			RA.bWasActive = bNowActive;
			break;
		}
		case EInputActionValueType::Axis2D:
		{
			FVector2D Val = CurrentValue.Get<FVector2D>();
			Frame.ActionValues.Add(RA.ActionName, FVector(Val.X, Val.Y, 0.0));
			bool bNowActive = Val.Size() > AxisThreshold;
			if (bNowActive && !RA.bWasActive)
			{
				Frame.EdgeEvents.Add(RA.ActionName + TEXT("_pressed"));
			}
			else if (!bNowActive && RA.bWasActive)
			{
				Frame.EdgeEvents.Add(RA.ActionName + TEXT("_released"));
			}
			RA.bWasActive = bNowActive;
			break;
		}
		case EInputActionValueType::Axis3D:
		{
			FVector Val = CurrentValue.Get<FVector>();
			Frame.ActionValues.Add(RA.ActionName, Val);
			bool bNowActive = Val.Size() > AxisThreshold;
			if (bNowActive && !RA.bWasActive)
			{
				Frame.EdgeEvents.Add(RA.ActionName + TEXT("_pressed"));
			}
			else if (!bNowActive && RA.bWasActive)
			{
				Frame.EdgeEvents.Add(RA.ActionName + TEXT("_released"));
			}
			RA.bWasActive = bNowActive;
			break;
		}
		}

		RA.LastValue = CurrentValue;
	}

	RecordedFrames.Add(MoveTemp(Frame));
}

void FPIEInputRecorder::WriteCSV(const FString& Path)
{
	FString CSV;

	CSV += TEXT("frame,time,dt,pos_x,pos_y,pos_z,rot_yaw,rot_pitch,rot_roll,vel_x,vel_y,vel_z,speed2d,montage");

	for (const FRecordedAction& RA : TrackedActions)
	{
		switch (RA.ValueType)
		{
		case EInputActionValueType::Boolean:
		case EInputActionValueType::Axis1D:
			CSV += FString::Printf(TEXT(",%s"), *RA.ActionName);
			break;
		case EInputActionValueType::Axis2D:
			CSV += FString::Printf(TEXT(",%s_x,%s_y"), *RA.ActionName, *RA.ActionName);
			break;
		case EInputActionValueType::Axis3D:
			CSV += FString::Printf(TEXT(",%s_x,%s_y,%s_z"), *RA.ActionName, *RA.ActionName, *RA.ActionName);
			break;
		}
	}
	CSV += TEXT(",event\n");

	for (const FRecordedInputFrame& Frame : RecordedFrames)
	{
		CSV += FString::Printf(TEXT("%llu,%.4f,%.4f,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%s"),
			Frame.FrameNumber,
			Frame.GameTime,
			Frame.DeltaTime,
			Frame.PawnLocation.X, Frame.PawnLocation.Y, Frame.PawnLocation.Z,
			Frame.PawnRotation.Yaw, Frame.PawnRotation.Pitch, Frame.PawnRotation.Roll,
			Frame.PawnVelocity.X, Frame.PawnVelocity.Y, Frame.PawnVelocity.Z,
			Frame.Speed2D,
			Frame.ActiveMontage.IsEmpty() ? TEXT("") : *Frame.ActiveMontage);

		for (const FRecordedAction& RA : TrackedActions)
		{
			const FVector* Val = Frame.ActionValues.Find(RA.ActionName);
			FVector V = Val ? *Val : FVector::ZeroVector;
			switch (RA.ValueType)
			{
			case EInputActionValueType::Boolean:
				CSV += FString::Printf(TEXT(",%.0f"), V.X);
				break;
			case EInputActionValueType::Axis1D:
				CSV += FString::Printf(TEXT(",%.3f"), V.X);
				break;
			case EInputActionValueType::Axis2D:
				CSV += FString::Printf(TEXT(",%.3f,%.3f"), V.X, V.Y);
				break;
			case EInputActionValueType::Axis3D:
				CSV += FString::Printf(TEXT(",%.3f,%.3f,%.3f"), V.X, V.Y, V.Z);
				break;
			}
		}

		FString EventStr = FString::Join(Frame.EdgeEvents, TEXT("|"));
		CSV += FString::Printf(TEXT(",%s\n"), *EventStr);
	}

	FFileHelper::SaveStringToFile(CSV, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FPIEInputRecorder::WriteSequenceJSON(const FString& Path)
{
	if (RecordedFrames.Num() == 0) return;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("settle_ms"), 500);

	TArray<TSharedPtr<FJsonValue>> Steps;

	double BaseTime = RecordedFrames[0].GameTime;
	double FrameDuration = 1.0 / 60.0;

	for (const FRecordedAction& RA : TrackedActions)
	{
		if (RA.ValueType == EInputActionValueType::Axis2D || RA.ValueType == EInputActionValueType::Axis1D || RA.ValueType == EInputActionValueType::Axis3D)
		{
			int32 FirstNonZero = -1;
			int32 LastNonZero = -1;

			for (int32 i = 0; i < RecordedFrames.Num(); ++i)
			{
				const FVector* Val = RecordedFrames[i].ActionValues.Find(RA.ActionName);
				if (Val && Val->Size() > AxisThreshold)
				{
					if (FirstNonZero < 0) FirstNonZero = i;
					LastNonZero = i;
				}
			}

			if (FirstNonZero < 0) continue;

			int32 RunStart = FirstNonZero;
			while (RunStart <= LastNonZero)
			{
				while (RunStart <= LastNonZero)
				{
					const FVector* V = RecordedFrames[RunStart].ActionValues.Find(RA.ActionName);
					if (V && V->Size() > AxisThreshold) break;
					RunStart++;
				}
				if (RunStart > LastNonZero) break;

				int32 RunEnd = RunStart;
				int32 ZeroGap = 0;
				for (int32 i = RunStart + 1; i <= LastNonZero; ++i)
				{
					const FVector* V = RecordedFrames[i].ActionValues.Find(RA.ActionName);
					bool bActive = V && V->Size() > AxisThreshold;
					if (bActive)
					{
						RunEnd = i;
						ZeroGap = 0;
					}
					else
					{
						ZeroGap++;
						if (ZeroGap > 6) break;
					}
				}

				TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
				Step->SetStringField(TEXT("type"), TEXT("input_tape"));
				Step->SetStringField(TEXT("action"), RA.ActionPath);

				double DelayMs = (RecordedFrames[RunStart].GameTime - BaseTime) * 1000.0;
				Step->SetNumberField(TEXT("delay_ms"), FMath::RoundToInt(DelayMs));

				TArray<TSharedPtr<FJsonValue>> Values;
				for (int32 i = RunStart; i <= RunEnd; ++i)
				{
					const FVector* V = RecordedFrames[i].ActionValues.Find(RA.ActionName);
					FVector Val = V ? *V : FVector::ZeroVector;

					if (RA.ValueType == EInputActionValueType::Axis2D)
					{
						TArray<TSharedPtr<FJsonValue>> Pair;
						Pair.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Val.X * 1000.0f) / 1000.0f));
						Pair.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Val.Y * 1000.0f) / 1000.0f));
						Values.Add(MakeShared<FJsonValueArray>(Pair));
					}
					else if (RA.ValueType == EInputActionValueType::Axis1D)
					{
						Values.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Val.X * 1000.0f) / 1000.0f));
					}
					else
					{
						TArray<TSharedPtr<FJsonValue>> Triple;
						Triple.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Val.X * 1000.0f) / 1000.0f));
						Triple.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Val.Y * 1000.0f) / 1000.0f));
						Triple.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Val.Z * 1000.0f) / 1000.0f));
						Values.Add(MakeShared<FJsonValueArray>(Triple));
					}
				}
				Step->SetArrayField(TEXT("values"), Values);

				Steps.Add(MakeShared<FJsonValueObject>(Step));

				RunStart = RunEnd + 1;
			}
		}
		else if (RA.ValueType == EInputActionValueType::Boolean)
		{
			for (int32 i = 0; i < RecordedFrames.Num(); ++i)
			{
				for (const FString& Event : RecordedFrames[i].EdgeEvents)
				{
					if (Event == RA.ActionName + TEXT("_pressed"))
					{
						int32 ReleaseFrame = -1;
						for (int32 j = i + 1; j < RecordedFrames.Num(); ++j)
						{
							for (const FString& RE : RecordedFrames[j].EdgeEvents)
							{
								if (RE == RA.ActionName + TEXT("_released"))
								{
									ReleaseFrame = j;
									break;
								}
							}
							if (ReleaseFrame >= 0) break;
						}

						double PressDurationMs;
						if (ReleaseFrame >= 0)
						{
							PressDurationMs = (RecordedFrames[ReleaseFrame].GameTime - RecordedFrames[i].GameTime) * 1000.0;
						}
						else
						{
							PressDurationMs = 100.0;
						}
						if (PressDurationMs < FrameDuration * 1000.0) PressDurationMs = FrameDuration * 1000.0;

						TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
						Step->SetStringField(TEXT("type"), TEXT("hold"));
						Step->SetStringField(TEXT("action"), RA.ActionPath);
						double DelayMs = (RecordedFrames[i].GameTime - BaseTime) * 1000.0;
						Step->SetNumberField(TEXT("delay_ms"), FMath::RoundToInt(DelayMs));
						Step->SetNumberField(TEXT("value_x"), 1.0);
						Step->SetNumberField(TEXT("duration_ms"), FMath::RoundToInt(PressDurationMs));

						Steps.Add(MakeShared<FJsonValueObject>(Step));
					}
				}
			}
		}
	}

	Steps.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B) -> bool
	{
		double DA = A->AsObject()->GetNumberField(TEXT("delay_ms"));
		double DB = B->AsObject()->GetNumberField(TEXT("delay_ms"));
		return DA < DB;
	});

	double PrevAbsMs = 0.0;
	for (auto& StepVal : Steps)
	{
		TSharedPtr<FJsonObject> StepObj = StepVal->AsObject();
		double AbsMs = StepObj->GetNumberField(TEXT("delay_ms"));
		StepObj->SetNumberField(TEXT("delay_ms"), FMath::Max(0.0, AbsMs - PrevAbsMs));
		PrevAbsMs = AbsMs;
	}

	Root->SetArrayField(TEXT("steps"), Steps);

	FString JSONStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JSONStr);
	FJsonSerializer::Serialize(Root, Writer);

	FFileHelper::SaveStringToFile(JSONStr, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FPIEInputRecorder::Reset()
{
	TrackedActions.Empty();
	RecordedFrames.Empty();

	if (OnEndFrameHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
		OnEndFrameHandle.Reset();
	}

	State = EPIERecorderState::Idle;
}
