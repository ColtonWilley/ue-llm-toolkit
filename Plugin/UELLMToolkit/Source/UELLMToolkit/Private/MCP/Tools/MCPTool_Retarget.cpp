// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Retarget.h"
#include "RetargetEditor.h"

// Helper: convert utility result JSON to FMCPToolResult
static FMCPToolResult RetargetJsonToToolResult(const TSharedPtr<FJsonObject>& Result, const FString& SuccessContext)
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

FMCPToolResult FMCPTool_Retarget::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	// Skeleton
	if (Operation == TEXT("inspect_skeleton"))
	{
		return HandleInspectSkeleton(Params);
	}
	else if (Operation == TEXT("inspect_ref_pose"))
	{
		return HandleInspectRefPose(Params);
	}
	else if (Operation == TEXT("list_skeletons"))
	{
		return HandleListSkeletons(Params);
	}
	// IK Rig
	else if (Operation == TEXT("inspect_ik_rig"))
	{
		return HandleInspectIKRig(Params);
	}
	else if (Operation == TEXT("create_ik_rig"))
	{
		return HandleCreateIKRig(Params);
	}
	else if (Operation == TEXT("add_chain"))
	{
		return HandleAddChain(Params);
	}
	else if (Operation == TEXT("remove_chain"))
	{
		return HandleRemoveChain(Params);
	}
	// Retargeter
	else if (Operation == TEXT("inspect_retargeter"))
	{
		return HandleInspectRetargeter(Params);
	}
	else if (Operation == TEXT("create_retargeter"))
	{
		return HandleCreateRetargeter(Params);
	}
	else if (Operation == TEXT("setup_ops"))
	{
		return HandleSetupOps(Params);
	}
	else if (Operation == TEXT("configure_fk"))
	{
		return HandleConfigureFK(Params);
	}
	// FBX Import
	else if (Operation == TEXT("import_fbx"))
	{
		return HandleImportFBX(Params);
	}
	else if (Operation == TEXT("batch_import_fbx"))
	{
		return HandleBatchImportFBX(Params);
	}
	// Batch Retarget
	else if (Operation == TEXT("batch_retarget"))
	{
		return HandleBatchRetarget(Params);
	}
	else if (Operation == TEXT("set_root_motion"))
	{
		return HandleSetRootMotion(Params);
	}
	else if (Operation == TEXT("find_anims"))
	{
		return HandleFindAnims(Params);
	}
	else if (Operation == TEXT("save"))
	{
		return HandleSave(Params);
	}
	// Inspection
	else if (Operation == TEXT("inspect_anim"))
	{
		return HandleInspectAnim(Params);
	}
	else if (Operation == TEXT("compare_bones"))
	{
		return HandleCompareBones(Params);
	}
	// Animation Analysis
	else if (Operation == TEXT("sample_bones"))
	{
		return HandleSampleBones(Params);
	}
	else if (Operation == TEXT("diagnose_anim"))
	{
		return HandleDiagnoseAnim(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid: inspect_skeleton, inspect_ref_pose, list_skeletons, inspect_ik_rig, create_ik_rig, add_chain, remove_chain, inspect_retargeter, create_retargeter, setup_ops, configure_fk, import_fbx, batch_import_fbx, batch_retarget, set_root_motion, find_anims, save, inspect_anim, compare_bones, sample_bones, diagnose_anim"),
		*Operation));
}

// ============================================================================
// Skeleton Operations
// ============================================================================

FMCPToolResult FMCPTool_Retarget::HandleInspectSkeleton(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FRetargetEditor::InspectSkeleton(AssetPath);
	return RetargetJsonToToolResult(Result, TEXT("Skeleton inspected"));
}

FMCPToolResult FMCPTool_Retarget::HandleInspectRefPose(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FRetargetEditor::InspectRefPose(AssetPath);
	return RetargetJsonToToolResult(Result, TEXT("Ref pose inspected"));
}

FMCPToolResult FMCPTool_Retarget::HandleListSkeletons(const TSharedRef<FJsonObject>& Params)
{
	FString FolderPath = ExtractOptionalString(Params, TEXT("folder_path"), TEXT("/Game"));

	TSharedPtr<FJsonObject> Result = FRetargetEditor::ListSkeletons(FolderPath);
	return RetargetJsonToToolResult(Result, TEXT("Skeletons listed"));
}

// ============================================================================
// IK Rig Operations
// ============================================================================

FMCPToolResult FMCPTool_Retarget::HandleInspectIKRig(const TSharedRef<FJsonObject>& Params)
{
	// Try asset_path first, then rig_path
	FString RigPath = ExtractOptionalString(Params, TEXT("asset_path"));
	if (RigPath.IsEmpty())
	{
		RigPath = ExtractOptionalString(Params, TEXT("rig_path"));
	}
	if (RigPath.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_path or rig_path"));
	}

	TSharedPtr<FJsonObject> Result = FRetargetEditor::InspectIKRig(RigPath);
	return RetargetJsonToToolResult(Result, TEXT("IK Rig inspected"));
}

FMCPToolResult FMCPTool_Retarget::HandleCreateIKRig(const TSharedRef<FJsonObject>& Params)
{
	FString PackagePath, Name, MeshPath, RetargetRoot;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Error))
		return Error.GetValue();

	RetargetRoot = ExtractOptionalString(Params, TEXT("retarget_root"));

	// Get chains array
	const TArray<TSharedPtr<FJsonValue>>* ChainsArray = nullptr;
	Params->TryGetArrayField(TEXT("chains"), ChainsArray);
	TArray<TSharedPtr<FJsonValue>> Chains;
	if (ChainsArray)
	{
		Chains = *ChainsArray;
	}

	TSharedPtr<FJsonObject> Result = FRetargetEditor::CreateIKRig(
		PackagePath, Name, MeshPath, RetargetRoot, Chains);
	return RetargetJsonToToolResult(Result, TEXT("IK Rig created"));
}

FMCPToolResult FMCPTool_Retarget::HandleAddChain(const TSharedRef<FJsonObject>& Params)
{
	FString RigPath, ChainName, StartBone, EndBone;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("rig_path"), RigPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("chain_name"), ChainName, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("start_bone"), StartBone, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("end_bone"), EndBone, Error))
		return Error.GetValue();

	TSharedPtr<FJsonObject> Result = FRetargetEditor::AddChain(RigPath, ChainName, StartBone, EndBone);
	return RetargetJsonToToolResult(Result, TEXT("Chain added"));
}

FMCPToolResult FMCPTool_Retarget::HandleRemoveChain(const TSharedRef<FJsonObject>& Params)
{
	FString RigPath, ChainName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("rig_path"), RigPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("chain_name"), ChainName, Error))
		return Error.GetValue();

	TSharedPtr<FJsonObject> Result = FRetargetEditor::RemoveChain(RigPath, ChainName);
	return RetargetJsonToToolResult(Result, TEXT("Chain removed"));
}

// ============================================================================
// Retargeter Operations
// ============================================================================

FMCPToolResult FMCPTool_Retarget::HandleInspectRetargeter(const TSharedRef<FJsonObject>& Params)
{
	FString RetargeterPath = ExtractOptionalString(Params, TEXT("retargeter_path"));
	if (RetargeterPath.IsEmpty())
	{
		RetargeterPath = ExtractOptionalString(Params, TEXT("asset_path"));
	}
	if (RetargeterPath.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: retargeter_path or asset_path"));
	}

	TSharedPtr<FJsonObject> Result = FRetargetEditor::InspectRetargeter(RetargeterPath);
	return RetargetJsonToToolResult(Result, TEXT("Retargeter inspected"));
}

FMCPToolResult FMCPTool_Retarget::HandleCreateRetargeter(const TSharedRef<FJsonObject>& Params)
{
	FString PackagePath, Name, SourceRigPath, TargetRigPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("source_rig_path"), SourceRigPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("target_rig_path"), TargetRigPath, Error))
		return Error.GetValue();

	TSharedPtr<FJsonObject> Result = FRetargetEditor::CreateRetargeter(
		PackagePath, Name, SourceRigPath, TargetRigPath);
	return RetargetJsonToToolResult(Result, TEXT("Retargeter created"));
}

FMCPToolResult FMCPTool_Retarget::HandleSetupOps(const TSharedRef<FJsonObject>& Params)
{
	FString RetargeterPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("retargeter_path"), RetargeterPath, Error))
		return Error.GetValue();

	TSharedPtr<FJsonObject> Result = FRetargetEditor::SetupOps(RetargeterPath);
	return RetargetJsonToToolResult(Result, TEXT("Ops setup complete"));
}

FMCPToolResult FMCPTool_Retarget::HandleConfigureFK(const TSharedRef<FJsonObject>& Params)
{
	FString RetargeterPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("retargeter_path"), RetargeterPath, Error))
		return Error.GetValue();

	const TArray<TSharedPtr<FJsonValue>>* SettingsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("chain_settings"), SettingsArray) || !SettingsArray)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: chain_settings (JSON array)"));
	}

	TSharedPtr<FJsonObject> Result = FRetargetEditor::ConfigureFK(RetargeterPath, *SettingsArray);
	return RetargetJsonToToolResult(Result, TEXT("FK configured"));
}

// ============================================================================
// FBX Import Operations
// ============================================================================

FMCPToolResult FMCPTool_Retarget::HandleImportFBX(const TSharedRef<FJsonObject>& Params)
{
	FString FbxPath, DestPath, SkeletonPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("fbx_path"), FbxPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("dest_path"), DestPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Error))
		return Error.GetValue();

	bool bImportMesh = ExtractOptionalBool(Params, TEXT("import_mesh"), false);
	int32 CustomSampleRate = ExtractOptionalNumber<int32>(Params, TEXT("custom_sample_rate"), 0);
	bool bSnapToFrameBoundary = ExtractOptionalBool(Params, TEXT("snap_to_frame_boundary"), false);

	TSharedPtr<FJsonObject> Result = FRetargetEditor::ImportFBX(FbxPath, DestPath, SkeletonPath, bImportMesh, CustomSampleRate, bSnapToFrameBoundary);
	return RetargetJsonToToolResult(Result, TEXT("FBX imported"));
}

FMCPToolResult FMCPTool_Retarget::HandleBatchImportFBX(const TSharedRef<FJsonObject>& Params)
{
	FString FbxDirectory, DestPath, SkeletonPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("fbx_directory"), FbxDirectory, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("dest_path"), DestPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Error))
		return Error.GetValue();

	FString FilePattern = ExtractOptionalString(Params, TEXT("file_pattern"), TEXT("*.fbx"));
	int32 CustomSampleRate = ExtractOptionalNumber<int32>(Params, TEXT("custom_sample_rate"), 0);
	bool bSnapToFrameBoundary = ExtractOptionalBool(Params, TEXT("snap_to_frame_boundary"), false);

	TSharedPtr<FJsonObject> Result = FRetargetEditor::BatchImportFBX(
		FbxDirectory, DestPath, SkeletonPath, FilePattern, CustomSampleRate, bSnapToFrameBoundary);
	return RetargetJsonToToolResult(Result, TEXT("Batch FBX import complete"));
}

// ============================================================================
// Batch Retarget Operations
// ============================================================================

FMCPToolResult FMCPTool_Retarget::HandleBatchRetarget(const TSharedRef<FJsonObject>& Params)
{
	FString RetargeterPath, SourceMeshPath, TargetMeshPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("retargeter_path"), RetargeterPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("source_mesh_path"), SourceMeshPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("target_mesh_path"), TargetMeshPath, Error))
		return Error.GetValue();

	// Get animation paths array
	const TArray<TSharedPtr<FJsonValue>>* AnimPathsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("anim_paths"), AnimPathsArray) || !AnimPathsArray)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: anim_paths (JSON array of animation asset paths)"));
	}

	FString Prefix = ExtractOptionalString(Params, TEXT("prefix"), TEXT("RTG_"));
	bool bAutoRootMotion = ExtractOptionalBool(Params, TEXT("auto_root_motion"), true);
	FString RootMotionPattern = ExtractOptionalString(Params, TEXT("root_motion_pattern"), TEXT("RootMotion|Attack|Dodge"));

	TSharedPtr<FJsonObject> Result = FRetargetEditor::BatchRetarget(
		RetargeterPath, *AnimPathsArray, SourceMeshPath, TargetMeshPath,
		Prefix, bAutoRootMotion, RootMotionPattern);
	return RetargetJsonToToolResult(Result, TEXT("Batch retarget complete"));
}

FMCPToolResult FMCPTool_Retarget::HandleSetRootMotion(const TSharedRef<FJsonObject>& Params)
{
	FString AnimPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AnimPath, Error))
		return Error.GetValue();

	bool bEnable = ExtractOptionalBool(Params, TEXT("enable"), true);

	TSharedPtr<FJsonObject> Result = FRetargetEditor::SetRootMotion(AnimPath, bEnable);
	return RetargetJsonToToolResult(Result, TEXT("Root motion set"));
}

FMCPToolResult FMCPTool_Retarget::HandleFindAnims(const TSharedRef<FJsonObject>& Params)
{
	FString FolderPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("folder_path"), FolderPath, Error))
		return Error.GetValue();

	bool bRecursive = ExtractOptionalBool(Params, TEXT("recursive"), false);

	TSharedPtr<FJsonObject> Result = FRetargetEditor::FindAnims(FolderPath, bRecursive);
	return RetargetJsonToToolResult(Result, TEXT("Animations found"));
}

FMCPToolResult FMCPTool_Retarget::HandleSave(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
		return Error.GetValue();

	TSharedPtr<FJsonObject> Result = FRetargetEditor::SaveAsset(AssetPath);
	return RetargetJsonToToolResult(Result, TEXT("Asset saved"));
}

// ============================================================================
// Inspection Operations
// ============================================================================

FMCPToolResult FMCPTool_Retarget::HandleInspectAnim(const TSharedRef<FJsonObject>& Params)
{
	FString AnimPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AnimPath, Error))
		return Error.GetValue();

	TSharedPtr<FJsonObject> Result = FRetargetEditor::InspectAnim(AnimPath);
	return RetargetJsonToToolResult(Result, TEXT("Animation inspected"));
}

FMCPToolResult FMCPTool_Retarget::HandleCompareBones(const TSharedRef<FJsonObject>& Params)
{
	FString SourceAnimPath, TargetAnimPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("source_anim_path"), SourceAnimPath, Error))
		return Error.GetValue();
	if (!ExtractRequiredString(Params, TEXT("target_anim_path"), TargetAnimPath, Error))
		return Error.GetValue();

	// Get bone names array
	const TArray<TSharedPtr<FJsonValue>>* BoneNamesArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("bone_names"), BoneNamesArray) || !BoneNamesArray)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: bone_names (JSON array)"));
	}

	TArray<FString> BoneNames;
	for (const TSharedPtr<FJsonValue>& Val : *BoneNamesArray)
	{
		BoneNames.Add(Val->AsString());
	}

	// Get sample times
	const TArray<TSharedPtr<FJsonValue>>* SampleTimesArray = nullptr;
	TArray<float> SampleTimes;
	if (Params->TryGetArrayField(TEXT("sample_times"), SampleTimesArray) && SampleTimesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *SampleTimesArray)
		{
			SampleTimes.Add(static_cast<float>(Val->AsNumber()));
		}
	}
	else
	{
		// Default sample times
		SampleTimes = {0.0f, 0.5f};
	}

	TSharedPtr<FJsonObject> Result = FRetargetEditor::CompareBones(
		SourceAnimPath, TargetAnimPath, BoneNames, SampleTimes);
	return RetargetJsonToToolResult(Result, TEXT("Bones compared"));
}

// ============================================================================
// Animation Analysis Operations
// ============================================================================

FMCPToolResult FMCPTool_Retarget::HandleSampleBones(const TSharedRef<FJsonObject>& Params)
{
	FString AnimPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AnimPath, Error))
		return Error.GetValue();

	// Get frames array (required)
	const TArray<TSharedPtr<FJsonValue>>* FramesArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("frames"), FramesArray) || !FramesArray)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: frames (JSON array of 0-based frame indices)"));
	}

	TArray<int32> Frames;
	for (const TSharedPtr<FJsonValue>& Val : *FramesArray)
	{
		Frames.Add(static_cast<int32>(Val->AsNumber()));
	}

	// Get bones array (optional)
	TArray<FString> BoneNames;
	const TArray<TSharedPtr<FJsonValue>>* BonesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("bones"), BonesArray) && BonesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *BonesArray)
		{
			BoneNames.Add(Val->AsString());
		}
	}

	TSharedPtr<FJsonObject> Result = FRetargetEditor::SampleBones(AnimPath, Frames, BoneNames);
	return RetargetJsonToToolResult(Result, TEXT("Bones sampled"));
}

FMCPToolResult FMCPTool_Retarget::HandleDiagnoseAnim(const TSharedRef<FJsonObject>& Params)
{
	FString AnimPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AnimPath, Error))
		return Error.GetValue();

	float PopThreshold = ExtractOptionalNumber(Params, TEXT("pop_threshold"), 50.0f);
	float RotationFlipThreshold = ExtractOptionalNumber(Params, TEXT("rotation_flip_threshold"), 0.0f);

	TSharedPtr<FJsonObject> Result = FRetargetEditor::DiagnoseAnim(AnimPath, PopThreshold, RotationFlipThreshold);
	return RetargetJsonToToolResult(Result, TEXT("Animation diagnosed"));
}
