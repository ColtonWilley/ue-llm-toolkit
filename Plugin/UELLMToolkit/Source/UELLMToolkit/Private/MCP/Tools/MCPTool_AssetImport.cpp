// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_AssetImport.h"
#include "AssetImporter.h"

// Helper: convert utility result JSON to FMCPToolResult
static FMCPToolResult AssetImportJsonToToolResult(const TSharedPtr<FJsonObject>& Result, const FString& SuccessContext)
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

FMCPToolResult FMCPTool_AssetImport::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("import"))
	{
		return HandleImport(Params);
	}
	else if (Operation == TEXT("batch_import"))
	{
		return HandleBatchImport(Params);
	}
	else if (Operation == TEXT("export"))
	{
		return HandleExport(Params);
	}
	else if (Operation == TEXT("reimport"))
	{
		return HandleReimport(Params);
	}
	else if (Operation == TEXT("get_source"))
	{
		return HandleGetSource(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid: import, batch_import, export, reimport, get_source"),
		*Operation));
}

// ============================================================================
// Handlers
// ============================================================================

FMCPToolResult FMCPTool_AssetImport::HandleImport(const TSharedRef<FJsonObject>& Params)
{
	FString FbxPath, DestPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("fbx_path"), FbxPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("dest_path"), DestPath, Error))
	{
		return Error.GetValue();
	}

	FString MeshType = ExtractOptionalString(Params, TEXT("mesh_type"), TEXT("auto"));
	FString SkeletonPath = ExtractOptionalString(Params, TEXT("skeleton_path"));
	bool bImportMaterials = ExtractOptionalBool(Params, TEXT("import_materials"), true);
	bool bImportTextures = ExtractOptionalBool(Params, TEXT("import_textures"), true);
	bool bGenerateCollision = ExtractOptionalBool(Params, TEXT("generate_collision"), true);
	bool bCombineMeshes = ExtractOptionalBool(Params, TEXT("combine_meshes"), false);
	bool bImportAnimations = ExtractOptionalBool(Params, TEXT("import_animations"), false);
	FString NormalImport = ExtractOptionalString(Params, TEXT("normal_import"), TEXT("import"));
	bool bReplaceExisting = ExtractOptionalBool(Params, TEXT("replace_existing"), true);
	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);
	int32 CustomSampleRate = ExtractOptionalNumber<int32>(Params, TEXT("custom_sample_rate"), 0);
	bool bSnapToFrameBoundary = ExtractOptionalBool(Params, TEXT("snap_to_frame_boundary"), false);

	TSharedPtr<FJsonObject> Result = FAssetImporter::ImportFBX(
		FbxPath, DestPath, MeshType, SkeletonPath,
		bImportMaterials, bImportTextures,
		bGenerateCollision, bCombineMeshes,
		bImportAnimations, NormalImport,
		bReplaceExisting, bSave,
		CustomSampleRate, bSnapToFrameBoundary);

	return AssetImportJsonToToolResult(Result, TEXT("FBX imported"));
}

FMCPToolResult FMCPTool_AssetImport::HandleBatchImport(const TSharedRef<FJsonObject>& Params)
{
	FString FbxDirectory, DestPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("fbx_directory"), FbxDirectory, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("dest_path"), DestPath, Error))
	{
		return Error.GetValue();
	}

	FString MeshType = ExtractOptionalString(Params, TEXT("mesh_type"), TEXT("auto"));
	FString SkeletonPath = ExtractOptionalString(Params, TEXT("skeleton_path"));
	bool bImportMaterials = ExtractOptionalBool(Params, TEXT("import_materials"), true);
	bool bImportTextures = ExtractOptionalBool(Params, TEXT("import_textures"), true);
	bool bGenerateCollision = ExtractOptionalBool(Params, TEXT("generate_collision"), true);
	bool bCombineMeshes = ExtractOptionalBool(Params, TEXT("combine_meshes"), false);
	bool bImportAnimations = ExtractOptionalBool(Params, TEXT("import_animations"), false);
	FString NormalImport = ExtractOptionalString(Params, TEXT("normal_import"), TEXT("import"));
	FString FilePattern = ExtractOptionalString(Params, TEXT("file_pattern"), TEXT("*.fbx"));
	bool bReplaceExisting = ExtractOptionalBool(Params, TEXT("replace_existing"), true);
	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);
	int32 CustomSampleRate = ExtractOptionalNumber<int32>(Params, TEXT("custom_sample_rate"), 0);
	bool bSnapToFrameBoundary = ExtractOptionalBool(Params, TEXT("snap_to_frame_boundary"), false);

	TSharedPtr<FJsonObject> Result = FAssetImporter::BatchImportFBX(
		FbxDirectory, DestPath, MeshType, SkeletonPath,
		bImportMaterials, bImportTextures,
		bGenerateCollision, bCombineMeshes,
		bImportAnimations, NormalImport,
		FilePattern, bReplaceExisting, bSave,
		CustomSampleRate, bSnapToFrameBoundary);

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);

	FString Message;
	Result->TryGetStringField(TEXT("message"), Message);

	if (bSuccess)
	{
		return FMCPToolResult::Success(Message, Result);
	}
	else
	{
		// Partial success — still return data
		FMCPToolResult PartialResult;
		PartialResult.bSuccess = false;
		PartialResult.Message = Message;
		PartialResult.Data = Result;
		return PartialResult;
	}
}

FMCPToolResult FMCPTool_AssetImport::HandleExport(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, OutputPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("output_path"), OutputPath, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FAssetImporter::ExportAsset(AssetPath, OutputPath);
	return AssetImportJsonToToolResult(Result, TEXT("Asset exported"));
}

FMCPToolResult FMCPTool_AssetImport::HandleReimport(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString NewSourcePath = ExtractOptionalString(Params, TEXT("new_source_path"));

	// Parse optional transform overrides
	bool bOverrideRotation = false;
	FRotator ImportRotation = FRotator::ZeroRotator;
	if (Params->HasField(TEXT("import_rotation")))
	{
		const TSharedPtr<FJsonObject>* RotObj = nullptr;
		if (Params->TryGetObjectField(TEXT("import_rotation"), RotObj) && RotObj)
		{
			bOverrideRotation = true;
			ImportRotation.Pitch = (*RotObj)->GetNumberField(TEXT("pitch"));
			ImportRotation.Yaw = (*RotObj)->GetNumberField(TEXT("yaw"));
			ImportRotation.Roll = (*RotObj)->GetNumberField(TEXT("roll"));
		}
	}

	bool bOverrideTranslation = false;
	FVector ImportTranslation = FVector::ZeroVector;
	if (Params->HasField(TEXT("import_translation")))
	{
		const TSharedPtr<FJsonObject>* TransObj = nullptr;
		if (Params->TryGetObjectField(TEXT("import_translation"), TransObj) && TransObj)
		{
			bOverrideTranslation = true;
			ImportTranslation.X = (*TransObj)->GetNumberField(TEXT("x"));
			ImportTranslation.Y = (*TransObj)->GetNumberField(TEXT("y"));
			ImportTranslation.Z = (*TransObj)->GetNumberField(TEXT("z"));
		}
	}

	float ImportUniformScale = ExtractOptionalNumber<float>(Params, TEXT("import_uniform_scale"), 0.0f);

	TSharedPtr<FJsonObject> Result = FAssetImporter::ReimportAsset(
		AssetPath, NewSourcePath,
		bOverrideRotation, ImportRotation,
		bOverrideTranslation, ImportTranslation,
		ImportUniformScale);
	return AssetImportJsonToToolResult(Result, TEXT("Asset reimported"));
}

FMCPToolResult FMCPTool_AssetImport::HandleGetSource(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FAssetImporter::GetSourceInfo(AssetPath);
	return AssetImportJsonToToolResult(Result, TEXT("Source info retrieved"));
}
