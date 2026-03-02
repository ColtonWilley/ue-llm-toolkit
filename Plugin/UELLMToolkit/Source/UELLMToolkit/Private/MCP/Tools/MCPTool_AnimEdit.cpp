// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_AnimEdit.h"
#include "AnimTrackEditor.h"

static FMCPToolResult AnimEditJsonToToolResult(const TSharedPtr<FJsonObject>& Result, const FString& SuccessContext)
{
	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);

	if (bSuccess)
	{
		FString Message;
		Result->TryGetStringField(TEXT("message"), Message);
		if (Message.IsEmpty())
		{
			Message = SuccessContext;
		}
		return FMCPToolResult::Success(Message, Result);
	}
	else
	{
		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		return FMCPToolResult::Error(Error.IsEmpty() ? TEXT("Unknown error") : Error);
	}
}

FMCPToolResult FMCPTool_AnimEdit::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("adjust_track"))
	{
		return HandleAdjustTrack(Params);
	}
	else if (Operation == TEXT("inspect_track"))
	{
		return HandleInspectTrack(Params);
	}
	else if (Operation == TEXT("resample"))
	{
		return HandleResample(Params);
	}
	else if (Operation == TEXT("replace_skeleton"))
	{
		return HandleReplaceSkeleton(Params);
	}

	return FMCPToolResult::Error(FString::Printf(TEXT("Unknown operation: %s"), *Operation));
}

FMCPToolResult FMCPTool_AnimEdit::HandleAdjustTrack(const TSharedRef<FJsonObject>& Params)
{
	FString BoneName;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("bone_name"), BoneName, Error))
	{
		return Error.GetValue();
	}

	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array)"));
	}

	TArray<FString> AssetPaths;
	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString Path;
		if (Val.IsValid() && Val->TryGetString(Path) && !Path.IsEmpty())
		{
			AssetPaths.Add(Path);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("asset_paths contains no valid paths"));
	}

	FVector LocationOffset = ExtractVectorParam(Params, TEXT("location_offset"));
	FRotator RotationOffset = ExtractRotatorParam(Params, TEXT("rotation_offset"));

	if (LocationOffset.IsNearlyZero() && RotationOffset.IsNearlyZero())
	{
		return FMCPToolResult::Error(TEXT("At least one of location_offset or rotation_offset must be non-zero"));
	}

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::AdjustTrack(AssetPaths, BoneName, LocationOffset, RotationOffset, bSave);
	return AnimEditJsonToToolResult(Result, TEXT("Batch adjust complete"));
}

FMCPToolResult FMCPTool_AnimEdit::HandleInspectTrack(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	FString BoneName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("bone_name"), BoneName, Error))
	{
		return Error.GetValue();
	}

	TArray<int32> SampleFrames;
	const TArray<TSharedPtr<FJsonValue>>* FramesArray;
	if (Params->TryGetArrayField(TEXT("sample_frames"), FramesArray) && FramesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *FramesArray)
		{
			double NumVal;
			if (Val.IsValid() && Val->TryGetNumber(NumVal))
			{
				SampleFrames.Add(static_cast<int32>(NumVal));
			}
		}
	}

	if (SampleFrames.Num() == 0)
	{
		SampleFrames.Add(0);
		SampleFrames.Add(-1);
	}

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::InspectTrack(AssetPath, BoneName, SampleFrames);
	return AnimEditJsonToToolResult(Result, TEXT("Inspect complete"));
}

FMCPToolResult FMCPTool_AnimEdit::HandleResample(const TSharedRef<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array)"));
	}

	TArray<FString> AssetPaths;
	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString Path;
		if (Val.IsValid() && Val->TryGetString(Path) && !Path.IsEmpty())
		{
			AssetPaths.Add(Path);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("asset_paths contains no valid paths"));
	}

	double TargetFPSDouble = 0;
	if (!Params->TryGetNumberField(TEXT("target_fps"), TargetFPSDouble) || TargetFPSDouble <= 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: target_fps (number > 0)"));
	}
	int32 TargetFPS = static_cast<int32>(TargetFPSDouble);

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::Resample(AssetPaths, TargetFPS, bSave);
	return AnimEditJsonToToolResult(Result, TEXT("Resample complete"));
}

FMCPToolResult FMCPTool_AnimEdit::HandleReplaceSkeleton(const TSharedRef<FJsonObject>& Params)
{
	FString SkeletonPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Error))
	{
		return Error.GetValue();
	}

	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array)"));
	}

	TArray<FString> AssetPaths;
	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString Path;
		if (Val.IsValid() && Val->TryGetString(Path) && !Path.IsEmpty())
		{
			AssetPaths.Add(Path);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("asset_paths contains no valid paths"));
	}

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::ReplaceSkeleton(AssetPaths, SkeletonPath, bSave);
	return AnimEditJsonToToolResult(Result, TEXT("Replace skeleton complete"));
}
