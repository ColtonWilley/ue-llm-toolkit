// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_BlendSpace.h"
#include "BlendSpaceReader.h"
#include "BlendSpaceEditor.h"

// Helper: convert utility result JSON to FMCPToolResult
static FMCPToolResult BlendSpaceJsonToToolResult(const TSharedPtr<FJsonObject>& Result, const FString& SuccessContext)
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

// ============================================================================
// Main Dispatch
// ============================================================================

FMCPToolResult FMCPTool_BlendSpace::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	// Read operations
	if (Operation == TEXT("inspect"))
	{
		return HandleInspect(Params);
	}
	else if (Operation == TEXT("list"))
	{
		return HandleList(Params);
	}
	// Write operations
	else if (Operation == TEXT("create"))
	{
		return HandleCreate(Params);
	}
	else if (Operation == TEXT("add_sample"))
	{
		return HandleAddSample(Params);
	}
	else if (Operation == TEXT("remove_sample"))
	{
		return HandleRemoveSample(Params);
	}
	else if (Operation == TEXT("move_sample"))
	{
		return HandleMoveSample(Params);
	}
	else if (Operation == TEXT("set_sample_animation"))
	{
		return HandleSetSampleAnimation(Params);
	}
	else if (Operation == TEXT("set_axis"))
	{
		return HandleSetAxis(Params);
	}
	else if (Operation == TEXT("save"))
	{
		return HandleSave(Params);
	}
	else if (Operation == TEXT("batch"))
	{
		return HandleBatch(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid: inspect, list, create, add_sample, remove_sample, move_sample, set_sample_animation, set_axis, save, batch"),
		*Operation));
}

// ============================================================================
// Read Handlers
// ============================================================================

FMCPToolResult FMCPTool_BlendSpace::HandleInspect(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FBlendSpaceReader::InspectBlendSpace(AssetPath);
	return BlendSpaceJsonToToolResult(Result, TEXT("Blend space inspected"));
}

FMCPToolResult FMCPTool_BlendSpace::HandleList(const TSharedRef<FJsonObject>& Params)
{
	FString FolderPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("folder_path"), FolderPath, Error))
	{
		return Error.GetValue();
	}

	bool bRecursive = ExtractOptionalBool(Params, TEXT("recursive"), false);

	TSharedPtr<FJsonObject> Result = FBlendSpaceReader::ListBlendSpaces(FolderPath, bRecursive);
	return BlendSpaceJsonToToolResult(Result, TEXT("Blend spaces listed"));
}

// ============================================================================
// Write Handlers
// ============================================================================

FMCPToolResult FMCPTool_BlendSpace::HandleCreate(const TSharedRef<FJsonObject>& Params)
{
	FString PackagePath, Name, SkeletonPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Error))
	{
		return Error.GetValue();
	}

	FString TypeStr = ExtractOptionalString(Params, TEXT("type"), TEXT("BlendSpace1D"));
	bool bIsAimOffset = TypeStr.Contains(TEXT("AimOffset"), ESearchCase::IgnoreCase);
	bool bIs1D = TypeStr.Contains(TEXT("1D"), ESearchCase::IgnoreCase);
	if (!bIsAimOffset && TypeStr.Equals(TEXT("BlendSpace"), ESearchCase::IgnoreCase))
	{
		bIs1D = false;
	}

	TSharedPtr<FJsonObject> Result = FBlendSpaceEditor::CreateBlendSpace(PackagePath, Name, SkeletonPath, bIs1D, bIsAimOffset);
	return BlendSpaceJsonToToolResult(Result, TEXT("Blend space created"));
}

FMCPToolResult FMCPTool_BlendSpace::HandleAddSample(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, AnimPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("animation_path"), AnimPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UBlendSpace* BS = FBlendSpaceEditor::LoadBlendSpace(AssetPath, LoadError);
	if (!BS)
	{
		return FMCPToolResult::Error(LoadError);
	}

	float X = ExtractOptionalNumber<float>(Params, TEXT("x"), 0.f);
	float Y = ExtractOptionalNumber<float>(Params, TEXT("y"), 0.f);
	float RateScale = ExtractOptionalNumber<float>(Params, TEXT("rate_scale"), -1.f);

	TSharedPtr<FJsonObject> Result = FBlendSpaceEditor::AddSample(BS, AnimPath, X, Y, RateScale);
	return BlendSpaceJsonToToolResult(Result, TEXT("Sample added"));
}

FMCPToolResult FMCPTool_BlendSpace::HandleRemoveSample(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UBlendSpace* BS = FBlendSpaceEditor::LoadBlendSpace(AssetPath, LoadError);
	if (!BS)
	{
		return FMCPToolResult::Error(LoadError);
	}

	int32 SampleIndex = ExtractOptionalNumber<int32>(Params, TEXT("sample_index"), -1);
	if (SampleIndex < 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: sample_index"));
	}

	TSharedPtr<FJsonObject> Result = FBlendSpaceEditor::RemoveSample(BS, SampleIndex);
	return BlendSpaceJsonToToolResult(Result, TEXT("Sample removed"));
}

FMCPToolResult FMCPTool_BlendSpace::HandleMoveSample(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UBlendSpace* BS = FBlendSpaceEditor::LoadBlendSpace(AssetPath, LoadError);
	if (!BS)
	{
		return FMCPToolResult::Error(LoadError);
	}

	int32 SampleIndex = ExtractOptionalNumber<int32>(Params, TEXT("sample_index"), -1);
	if (SampleIndex < 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: sample_index"));
	}

	float X = ExtractOptionalNumber<float>(Params, TEXT("x"), 0.f);
	float Y = ExtractOptionalNumber<float>(Params, TEXT("y"), 0.f);
	float RateScale = ExtractOptionalNumber<float>(Params, TEXT("rate_scale"), -1.f);

	TSharedPtr<FJsonObject> Result = FBlendSpaceEditor::MoveSample(BS, SampleIndex, X, Y, RateScale);
	return BlendSpaceJsonToToolResult(Result, TEXT("Sample moved"));
}

FMCPToolResult FMCPTool_BlendSpace::HandleSetSampleAnimation(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, AnimPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("animation_path"), AnimPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UBlendSpace* BS = FBlendSpaceEditor::LoadBlendSpace(AssetPath, LoadError);
	if (!BS)
	{
		return FMCPToolResult::Error(LoadError);
	}

	int32 SampleIndex = ExtractOptionalNumber<int32>(Params, TEXT("sample_index"), -1);
	if (SampleIndex < 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: sample_index"));
	}

	TSharedPtr<FJsonObject> Result = FBlendSpaceEditor::SetSampleAnimation(BS, SampleIndex, AnimPath);
	return BlendSpaceJsonToToolResult(Result, TEXT("Sample animation set"));
}

FMCPToolResult FMCPTool_BlendSpace::HandleSetAxis(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UBlendSpace* BS = FBlendSpaceEditor::LoadBlendSpace(AssetPath, LoadError);
	if (!BS)
	{
		return FMCPToolResult::Error(LoadError);
	}

	int32 AxisIndex = ExtractOptionalNumber<int32>(Params, TEXT("axis_index"), -1);
	if (AxisIndex < 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: axis_index"));
	}

	// Pass the whole Params object — SetAxis picks out the fields it needs
	TSharedPtr<FJsonObject> Result = FBlendSpaceEditor::SetAxis(BS, AxisIndex, Params);
	return BlendSpaceJsonToToolResult(Result, TEXT("Axis configured"));
}

FMCPToolResult FMCPTool_BlendSpace::HandleSave(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FBlendSpaceEditor::SaveBlendSpace(AssetPath);
	return BlendSpaceJsonToToolResult(Result, TEXT("Blend space saved"));
}

FMCPToolResult FMCPTool_BlendSpace::HandleBatch(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UBlendSpace* BS = FBlendSpaceEditor::LoadBlendSpace(AssetPath, LoadError);
	if (!BS)
	{
		return FMCPToolResult::Error(LoadError);
	}

	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("operations"), OpsArray) || !OpsArray)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: operations (JSON array)"));
	}

	TSharedPtr<FJsonObject> Result = FBlendSpaceEditor::ExecuteBatch(BS, *OpsArray);

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);

	int32 OKCount = static_cast<int32>(Result->GetNumberField(TEXT("ok_count")));
	int32 ErrCount = static_cast<int32>(Result->GetNumberField(TEXT("error_count")));
	int32 Total = static_cast<int32>(Result->GetNumberField(TEXT("total")));

	FString Message = FString::Printf(TEXT("Batch: %d OK, %d errors, %d total"), OKCount, ErrCount, Total);

	if (bSuccess)
	{
		return FMCPToolResult::Success(Message, Result);
	}
	else
	{
		// Partial success — still return data but mark as error
		FMCPToolResult PartialResult;
		PartialResult.bSuccess = false;
		PartialResult.Message = Message;
		PartialResult.Data = Result;
		return PartialResult;
	}
}
