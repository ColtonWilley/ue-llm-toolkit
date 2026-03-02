// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Asset.h"
#include "MCP/MCPParamValidator.h"
#include "UnrealClaudeModule.h"
#include "Editor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "UObject/PropertyAccessUtil.h"
#include "Engine/SkeletalMesh.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ObjectTools.h"
#include "FileHelpers.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"

FMCPToolInfo FMCPTool_Asset::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("asset");
	Info.Description = TEXT("Generic asset operations: set properties, save, and query assets in Content Browser");

	// Parameters
	Info.Parameters.Add(FMCPToolParameter(TEXT("operation"), TEXT("string"),
		TEXT("Operation: set_asset_property, save_asset, get_asset_info, list_assets, rename, duplicate, delete, migrate, create_folder, rename_folder, delete_folder, get_hashes, save_all, open_editor, set_preview_mesh"), true));

	// Common params
	Info.Parameters.Add(FMCPToolParameter(TEXT("asset_path"), TEXT("string"),
		TEXT("Asset path (e.g., /Game/Characters/MyMesh)"), false));

	// set_asset_property params
	Info.Parameters.Add(FMCPToolParameter(TEXT("property"), TEXT("string"),
		TEXT("Property path (e.g., Materials.0.MaterialInterface, bEnableGravity)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("value"), TEXT("any"),
		TEXT("Value to set (type must match property type)"), false));

	// save_asset params
	Info.Parameters.Add(FMCPToolParameter(TEXT("save"), TEXT("boolean"),
		TEXT("Actually save to disk (default: true)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("mark_dirty"), TEXT("boolean"),
		TEXT("Mark the asset as dirty (default: true if save is false)"), false));

	// get_asset_info params
	Info.Parameters.Add(FMCPToolParameter(TEXT("include_properties"), TEXT("boolean"),
		TEXT("Include editable property list (default: false)"), false));

	// list_assets params
	Info.Parameters.Add(FMCPToolParameter(TEXT("directory"), TEXT("string"),
		TEXT("Directory to list (e.g., /Game/Characters/)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("class_filter"), TEXT("string"),
		TEXT("Filter by class name (e.g., SkeletalMesh, StaticMesh, Material)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("recursive"), TEXT("boolean"),
		TEXT("Search recursively (default: false)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("limit"), TEXT("integer"),
		TEXT("Maximum results (1-1000, default: 25)"), false));

	// rename params
	Info.Parameters.Add(FMCPToolParameter(TEXT("new_name"), TEXT("string"),
		TEXT("New asset name (rename op). At least one of new_name or new_path required."), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("new_path"), TEXT("string"),
		TEXT("New package folder (rename op, e.g. /Game/NewFolder). At least one of new_name or new_path required."), false));

	// duplicate params
	Info.Parameters.Add(FMCPToolParameter(TEXT("dest_path"), TEXT("string"),
		TEXT("Destination package folder for duplicate (e.g. /Game/Copy)"), false));

	// delete / migrate params
	Info.Parameters.Add(FMCPToolParameter(TEXT("asset_paths"), TEXT("array"),
		TEXT("Array of asset paths (delete: paths to delete; migrate: package paths to migrate)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("force"), TEXT("boolean"),
		TEXT("Force delete even if referenced (default: false)"), false));

	// folder operation params
	Info.Parameters.Add(FMCPToolParameter(TEXT("folder_path"), TEXT("string"),
		TEXT("Folder path for create/rename/delete_folder (e.g. /Game/Animations/NewFolder)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("new_folder_path"), TEXT("string"),
		TEXT("Destination path for rename_folder (e.g. /Game/Animations/Renamed)"), false));

	// migrate params
	Info.Parameters.Add(FMCPToolParameter(TEXT("target_content_dir"), TEXT("string"),
		TEXT("Absolute disk path to target Content/ folder (migrate op)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("include_dependencies"), TEXT("boolean"),
		TEXT("Resolve full dependency tree (migrate op, default: true)"), false));
	Info.Parameters.Add(FMCPToolParameter(TEXT("overwrite"), TEXT("boolean"),
		TEXT("Overwrite existing assets at destination (migrate op, default: true)"), false));

	// set_preview_mesh params
	Info.Parameters.Add(FMCPToolParameter(TEXT("mesh_path"), TEXT("string"),
		TEXT("Skeletal mesh path for set_preview_mesh (e.g. /Game/Characters/Meshes/SKM_Hero)"), false));

	Info.Annotations = FMCPToolAnnotations::Modifying();

	return Info;
}

FMCPToolResult FMCPTool_Asset::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("set_asset_property"))
	{
		return ExecuteSetAssetProperty(Params);
	}
	else if (Operation == TEXT("save_asset"))
	{
		return ExecuteSaveAsset(Params);
	}
	else if (Operation == TEXT("get_asset_info"))
	{
		return ExecuteGetAssetInfo(Params);
	}
	else if (Operation == TEXT("list_assets"))
	{
		return ExecuteListAssets(Params);
	}
	else if (Operation == TEXT("rename"))
	{
		return ExecuteRename(Params);
	}
	else if (Operation == TEXT("duplicate"))
	{
		return ExecuteDuplicate(Params);
	}
	else if (Operation == TEXT("delete"))
	{
		return ExecuteDelete(Params);
	}
	else if (Operation == TEXT("migrate"))
	{
		return ExecuteMigrate(Params);
	}
	else if (Operation == TEXT("create_folder"))
	{
		return ExecuteCreateFolder(Params);
	}
	else if (Operation == TEXT("rename_folder"))
	{
		return ExecuteRenameFolder(Params);
	}
	else if (Operation == TEXT("delete_folder"))
	{
		return ExecuteDeleteFolder(Params);
	}
	else if (Operation == TEXT("get_hashes"))
	{
		return ExecuteGetHashes(Params);
	}
	else if (Operation == TEXT("save_all"))
	{
		return ExecuteSaveAll(Params);
	}
	else if (Operation == TEXT("open_editor"))
	{
		return ExecuteOpenEditor(Params);
	}
	else if (Operation == TEXT("set_preview_mesh"))
	{
		return ExecuteSetPreviewMesh(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: %s. Valid: set_asset_property, save_asset, get_asset_info, list_assets, rename, duplicate, delete, migrate, create_folder, rename_folder, delete_folder, get_hashes, save_all, open_editor, set_preview_mesh"),
		*Operation));
}

FMCPToolResult FMCPTool_Asset::ExecuteSetAssetProperty(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	FString PropertyPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("property"), PropertyPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidatePropertyPathParam(PropertyPath, Error))
	{
		return Error.GetValue();
	}

	if (!Params->HasField(TEXT("value")))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: value"));
	}
	TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));

	// Load the asset
	FString LoadError;
	UObject* Asset = LoadAssetByPath(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	// Set the property — try on asset first, then Blueprint CDO if applicable
	FString PropertyError;
	UObject* TargetObject = Asset;
	bool bUsedCDO = false;

	if (!SetPropertyFromJson(Asset, PropertyPath, Value, PropertyError))
	{
		// If property not found on Blueprint asset, try its generated class CDO
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (BP && BP->GeneratedClass && PropertyError.Contains(TEXT("not found")))
		{
			UObject* CDO = BP->GeneratedClass->GetDefaultObject();
			if (CDO)
			{
				FString CDOError;
				if (SetPropertyFromJson(CDO, PropertyPath, Value, CDOError))
				{
					TargetObject = CDO;
					bUsedCDO = true;
				}
				else
				{
					return FMCPToolResult::Error(CDOError);
				}
			}
			else
			{
				return FMCPToolResult::Error(PropertyError);
			}
		}
		else
		{
			return FMCPToolResult::Error(PropertyError);
		}
	}

	// Mark dirty and notify
	TargetObject->PostEditChange();
	Asset->MarkPackageDirty();

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetStringField(TEXT("property"), PropertyPath);
	ResultData->SetBoolField(TEXT("modified"), true);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set property '%s' on asset '%s'"), *PropertyPath, *Asset->GetName()),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteSaveAsset(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(AssetPath, Error))
	{
		return Error.GetValue();
	}

	// Get options
	bool bSave = true;
	if (Params->HasField(TEXT("save")))
	{
		bSave = Params->GetBoolField(TEXT("save"));
	}

	bool bMarkDirty = !bSave; // Default to marking dirty if not saving
	if (Params->HasField(TEXT("mark_dirty")))
	{
		bMarkDirty = Params->GetBoolField(TEXT("mark_dirty"));
	}

	// Load the asset
	FString LoadError;
	UObject* Asset = LoadAssetByPath(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	bool bWasSaved = false;
	bool bWasMarkedDirty = false;

	// Mark dirty if requested
	if (bMarkDirty)
	{
		Asset->MarkPackageDirty();
		bWasMarkedDirty = true;
	}

	// Save if requested
	if (bSave)
	{
		UPackage* Package = Asset->GetOutermost();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension()
		);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		FSavePackageResultStruct SaveResult = UPackage::Save(Package, Asset, *PackageFileName, SaveArgs);

		bWasSaved = SaveResult.IsSuccessful();
		if (!bWasSaved)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath));
		}
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetBoolField(TEXT("saved"), bWasSaved);
	ResultData->SetBoolField(TEXT("marked_dirty"), bWasMarkedDirty);

	FString Message = bWasSaved
		? FString::Printf(TEXT("Saved asset: %s"), *AssetPath)
		: FString::Printf(TEXT("Marked asset dirty: %s"), *AssetPath);

	return FMCPToolResult::Success(Message, ResultData);
}

FMCPToolResult FMCPTool_Asset::ExecuteGetAssetInfo(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(AssetPath, Error))
	{
		return Error.GetValue();
	}

	bool bIncludeProperties = false;
	if (Params->HasField(TEXT("include_properties")))
	{
		bIncludeProperties = Params->GetBoolField(TEXT("include_properties"));
	}

	// Load the asset
	FString LoadError;
	UObject* Asset = LoadAssetByPath(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TSharedPtr<FJsonObject> ResultData = BuildAssetInfoJson(Asset);

	// Add properties if requested
	if (bIncludeProperties)
	{
		ResultData->SetArrayField(TEXT("properties"), GetAssetProperties(Asset, true));
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Asset info: %s"), *Asset->GetName()),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteListAssets(const TSharedRef<FJsonObject>& Params)
{
	FString Directory = TEXT("/Game/");
	if (Params->HasField(TEXT("directory")))
	{
		Directory = Params->GetStringField(TEXT("directory"));
	}

	TOptional<FMCPToolResult> Error;
	if (!ValidateBlueprintPathParam(Directory, Error))
	{
		return Error.GetValue();
	}

	FString ClassFilter;
	if (Params->HasField(TEXT("class_filter")))
	{
		ClassFilter = Params->GetStringField(TEXT("class_filter"));
	}

	bool bRecursive = false;
	if (Params->HasField(TEXT("recursive")))
	{
		bRecursive = Params->GetBoolField(TEXT("recursive"));
	}

	int32 Limit = 25;
	if (Params->HasField(TEXT("limit")))
	{
		Limit = FMath::Clamp(Params->GetIntegerField(TEXT("limit")), 1, 1000);
	}

	// Query asset registry
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPath(FName(*Directory), Assets, bRecursive);

	// Filter and build results
	TArray<TSharedPtr<FJsonValue>> ResultArray;
	int32 Count = 0;

	for (const FAssetData& AssetData : Assets)
	{
		if (Count >= Limit)
		{
			break;
		}

		// Apply class filter if specified
		if (!ClassFilter.IsEmpty())
		{
			FString AssetClassName = AssetData.AssetClassPath.GetAssetName().ToString();
			if (!AssetClassName.Contains(ClassFilter))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		AssetObj->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

		ResultArray.Add(MakeShared<FJsonValueObject>(AssetObj));
		Count++;
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("directory"), Directory);
	ResultData->SetNumberField(TEXT("count"), Count);
	ResultData->SetNumberField(TEXT("total_found"), Assets.Num());
	ResultData->SetArrayField(TEXT("assets"), ResultArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d assets in %s"), Count, *Directory),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteRename(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString NewName = ExtractOptionalString(Params, TEXT("new_name"));
	FString NewPath = ExtractOptionalString(Params, TEXT("new_path"));

	if (NewName.IsEmpty() && NewPath.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("At least one of new_name or new_path is required"));
	}

	FString LoadError;
	UObject* Asset = LoadAssetByPath(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString CurrentPackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());
	FString CurrentAssetName = Asset->GetName();

	FString TargetPackagePath = NewPath.IsEmpty() ? CurrentPackagePath : NewPath;
	FString TargetAssetName = NewName.IsEmpty() ? CurrentAssetName : NewName;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TArray<FAssetRenameData> RenameData;
	RenameData.Emplace(Asset, TargetPackagePath, TargetAssetName);
	bool bSuccess = AssetTools.RenameAssets(RenameData);

	if (!bSuccess)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to rename asset %s to %s/%s"), *AssetPath, *TargetPackagePath, *TargetAssetName));
	}

	FString NewFullPath = TargetPackagePath / TargetAssetName;

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("old_path"), AssetPath);
	ResultData->SetStringField(TEXT("new_path"), NewFullPath);
	ResultData->SetBoolField(TEXT("success"), true);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Renamed %s -> %s"), *AssetPath, *NewFullPath),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteDuplicate(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, DestPath, NewName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("dest_path"), DestPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("new_name"), NewName, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UObject* Asset = LoadAssetByPath(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.DuplicateAsset(NewName, DestPath, Asset);

	if (!NewAsset)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to duplicate asset %s to %s/%s"), *AssetPath, *DestPath, *NewName));
	}

	FString NewAssetPath = NewAsset->GetPathName();

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("success"), true);
	ResultData->SetStringField(TEXT("source_path"), AssetPath);
	ResultData->SetStringField(TEXT("new_asset_path"), NewAssetPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Duplicated %s -> %s"), *AssetPath, *NewAssetPath),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteDelete(const TSharedRef<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array)"));
	}

	bool bForce = ExtractOptionalBool(Params, TEXT("force"), false);

	TArray<UObject*> AssetsToDelete;
	TArray<FString> FailedPaths;
	TArray<FString> SucceededPaths;

	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString Path;
		if (!Val.IsValid() || !Val->TryGetString(Path) || Path.IsEmpty())
		{
			continue;
		}

		FString LoadError;
		UObject* Asset = LoadAssetByPath(Path, LoadError);
		if (Asset)
		{
			AssetsToDelete.Add(Asset);
			SucceededPaths.Add(Path);
		}
		else
		{
			FailedPaths.Add(Path);
		}
	}

	if (AssetsToDelete.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("No valid assets found to delete"));
	}

	int32 DeletedCount = 0;
	if (bForce)
	{
		DeletedCount = ObjectTools::ForceDeleteObjects(AssetsToDelete, false);
	}
	else
	{
		DeletedCount = ObjectTools::DeleteObjects(AssetsToDelete, false);
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("success"), DeletedCount > 0);
	ResultData->SetNumberField(TEXT("deleted_count"), DeletedCount);
	ResultData->SetNumberField(TEXT("requested_count"), AssetsToDelete.Num());

	TArray<TSharedPtr<FJsonValue>> DeletedArr;
	for (const FString& Path : SucceededPaths)
	{
		DeletedArr.Add(MakeShared<FJsonValueString>(Path));
	}
	ResultData->SetArrayField(TEXT("requested_paths"), DeletedArr);

	if (FailedPaths.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailedArr;
		for (const FString& Path : FailedPaths)
		{
			FailedArr.Add(MakeShared<FJsonValueString>(Path));
		}
		ResultData->SetArrayField(TEXT("not_found"), FailedArr);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Deleted %d of %d assets"), DeletedCount, AssetsToDelete.Num()),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteMigrate(const TSharedRef<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array of package paths)"));
	}

	FString TargetContentDir;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("target_content_dir"), TargetContentDir, Error))
	{
		return Error.GetValue();
	}

	if (!FPaths::DirectoryExists(TargetContentDir))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Target directory does not exist: %s"), *TargetContentDir));
	}

	bool bIncludeDependencies = ExtractOptionalBool(Params, TEXT("include_dependencies"), true);
	bool bOverwrite = ExtractOptionalBool(Params, TEXT("overwrite"), true);

	TArray<FName> PackageNames;
	TArray<FString> RequestedPaths;
	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString Path;
		if (Val.IsValid() && Val->TryGetString(Path) && !Path.IsEmpty())
		{
			PackageNames.Add(FName(*Path));
			RequestedPaths.Add(Path);
		}
	}

	if (PackageNames.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("No valid package paths found in asset_paths array"));
	}

	FMigrationOptions Options;
	Options.bPrompt = false;
	Options.bIgnoreDependencies = !bIncludeDependencies;
	Options.AssetConflict = bOverwrite ? EAssetMigrationConflict::Overwrite : EAssetMigrationConflict::Skip;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.MigratePackages(PackageNames, TargetContentDir, Options);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("success"), true);
	ResultData->SetNumberField(TEXT("package_count"), PackageNames.Num());
	ResultData->SetStringField(TEXT("target_content_dir"), TargetContentDir);
	ResultData->SetBoolField(TEXT("include_dependencies"), bIncludeDependencies);
	ResultData->SetBoolField(TEXT("overwrite"), bOverwrite);

	TArray<TSharedPtr<FJsonValue>> PathsArr;
	for (const FString& Path : RequestedPaths)
	{
		PathsArr.Add(MakeShared<FJsonValueString>(Path));
	}
	ResultData->SetArrayField(TEXT("migrated_packages"), PathsArr);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Migrated %d packages to %s"), PackageNames.Num(), *TargetContentDir),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteCreateFolder(const TSharedRef<FJsonObject>& Params)
{
	FString FolderPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("folder_path"), FolderPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(FolderPath, Error))
	{
		return Error.GetValue();
	}

	if (UEditorAssetLibrary::DoesDirectoryExist(FolderPath))
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("folder_path"), FolderPath);
		ResultData->SetBoolField(TEXT("already_existed"), true);
		return FMCPToolResult::Success(
			FString::Printf(TEXT("Folder already exists: %s"), *FolderPath),
			ResultData
		);
	}

	bool bSuccess = UEditorAssetLibrary::MakeDirectory(FolderPath);
	if (!bSuccess)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create folder: %s"), *FolderPath));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("folder_path"), FolderPath);
	ResultData->SetBoolField(TEXT("already_existed"), false);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created folder: %s"), *FolderPath),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteRenameFolder(const TSharedRef<FJsonObject>& Params)
{
	FString FolderPath, NewFolderPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("folder_path"), FolderPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(FolderPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("new_folder_path"), NewFolderPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(NewFolderPath, Error))
	{
		return Error.GetValue();
	}

	if (!UEditorAssetLibrary::DoesDirectoryExist(FolderPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Source folder does not exist: %s"), *FolderPath));
	}

	bool bSuccess = UEditorAssetLibrary::RenameDirectory(FolderPath, NewFolderPath);
	if (!bSuccess)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to rename folder %s to %s"), *FolderPath, *NewFolderPath));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("old_path"), FolderPath);
	ResultData->SetStringField(TEXT("new_path"), NewFolderPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Renamed folder %s -> %s"), *FolderPath, *NewFolderPath),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteDeleteFolder(const TSharedRef<FJsonObject>& Params)
{
	FString FolderPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("folder_path"), FolderPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(FolderPath, Error))
	{
		return Error.GetValue();
	}

	if (!UEditorAssetLibrary::DoesDirectoryExist(FolderPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Folder does not exist: %s"), *FolderPath));
	}

	bool bSuccess = UEditorAssetLibrary::DeleteDirectory(FolderPath);
	if (!bSuccess)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to delete folder: %s (must be empty — delete contained assets first)"), *FolderPath));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("folder_path"), FolderPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Deleted folder: %s"), *FolderPath),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteSaveAll(const TSharedRef<FJsonObject>& Params)
{
	bool bResult = FEditorFileUtils::SaveDirtyPackages(
		false,
		true,
		true);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetBoolField(TEXT("success"), bResult);

	if (bResult)
	{
		return FMCPToolResult::Success(TEXT("Saved all dirty packages"), ResultData);
	}

	return FMCPToolResult::Error(TEXT("SaveDirtyPackages failed"));
}

FMCPToolResult FMCPTool_Asset::ExecuteOpenEditor(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ValidateBlueprintPathParam(AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UObject* Asset = LoadAssetByPath(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return FMCPToolResult::Error(TEXT("AssetEditorSubsystem not available."));
	}

	bool bOpened = EditorSubsystem->OpenEditorForAsset(Asset);
	if (!bOpened)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to open editor for: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	ResultData->SetBoolField(TEXT("opened"), true);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Opened editor for: %s"), *AssetPath),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteSetPreviewMesh(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, MeshPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UObject* Asset = LoadAssetByPath(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	UAnimationAsset* AnimAsset = Cast<UAnimationAsset>(Asset);
	if (!AnimAsset)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Asset is not an animation: %s (%s)"), *AssetPath, *Asset->GetClass()->GetName()));
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Mesh)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load skeletal mesh: %s"), *MeshPath));
	}

	AnimAsset->SetPreviewMesh(Mesh, true);

	UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	bool bReopened = false;
	if (EditorSubsystem)
	{
		IAssetEditorInstance* OpenEditor = EditorSubsystem->FindEditorForAsset(AnimAsset, false);
		if (OpenEditor)
		{
			EditorSubsystem->CloseAllEditorsForAsset(AnimAsset);
			EditorSubsystem->OpenEditorForAsset(AnimAsset);
			bReopened = true;
		}
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("asset_path"), AssetPath);
	ResultData->SetStringField(TEXT("mesh_path"), MeshPath);
	ResultData->SetStringField(TEXT("mesh_name"), Mesh->GetName());
	ResultData->SetBoolField(TEXT("editor_reopened"), bReopened);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set preview mesh on %s to %s"), *AnimAsset->GetName(), *Mesh->GetName()),
		ResultData
	);
}

FMCPToolResult FMCPTool_Asset::ExecuteGetHashes(const TSharedRef<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array)"));
	}

	TArray<TSharedPtr<FJsonValue>> ResultArray;

	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString AssetPath;
		if (!Val.IsValid() || !Val->TryGetString(AssetPath) || AssetPath.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);

		FString DiskPath = FPackageName::LongPackageNameToFilename(
			AssetPath, FPackageName::GetAssetPackageExtension());

		IFileManager& FM = IFileManager::Get();
		FDateTime MTime = FM.GetTimeStamp(*DiskPath);

		if (MTime == FDateTime::MinValue())
		{
			Entry->SetStringField(TEXT("error"), TEXT("file_not_found"));
			ResultArray.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		int64 FileSize = FM.FileSize(*DiskPath);
		bool bIsDirty = false;

		UPackage* Pkg = FindPackage(nullptr, *AssetPath);
		if (Pkg)
		{
			bIsDirty = Pkg->IsDirty();
		}

		FString RawStr = FString::Printf(TEXT("%lld_%lld_%d"), FileSize, MTime.ToUnixTimestamp(), bIsDirty ? 1 : 0);
		uint32 CRC = FCrc::StrCrc32(*RawStr);
		FString HashStr = FString::Printf(TEXT("%08x"), CRC);

		Entry->SetStringField(TEXT("hash"), HashStr);
		Entry->SetNumberField(TEXT("file_size"), static_cast<double>(FileSize));
		Entry->SetStringField(TEXT("mtime"), MTime.ToIso8601());
		Entry->SetBoolField(TEXT("is_dirty"), bIsDirty);

		ResultArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetNumberField(TEXT("count"), ResultArray.Num());
	ResultData->SetArrayField(TEXT("assets"), ResultArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Hashed %d assets"), ResultArray.Num()),
		ResultData
	);
}

UObject* FMCPTool_Asset::LoadAssetByPath(const FString& AssetPath, FString& OutError)
{
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		// Try with _C suffix for Blueprint classes
		if (!AssetPath.EndsWith(TEXT("_C")))
		{
			Asset = LoadObject<UObject>(nullptr, *(AssetPath + TEXT("_C")));
		}
	}

	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
	}

	return Asset;
}

bool FMCPTool_Asset::NavigateToProperty(
	UObject* StartObject,
	const TArray<FString>& PathParts,
	UObject*& OutObject,
	FProperty*& OutProperty,
	FString& OutError)
{
	OutObject = StartObject;
	OutProperty = nullptr;

	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		const FString& PartName = PathParts[i];
		const bool bIsLastPart = (i == PathParts.Num() - 1);

		// Check for array index notation (e.g., "Materials.0")
		int32 ArrayIndex = INDEX_NONE;
		FString PropertyName = PartName;

		// Check if this part is a numeric index
		if (PartName.IsNumeric())
		{
			ArrayIndex = FCString::Atoi(*PartName);
			// The previous property should be an array
			if (!OutProperty)
			{
				OutError = FString::Printf(TEXT("Cannot index without preceding array property"));
				return false;
			}

			FArrayProperty* ArrayProp = CastField<FArrayProperty>(OutProperty);
			if (!ArrayProp)
			{
				OutError = FString::Printf(TEXT("Property is not an array, cannot use index"));
				return false;
			}

			// Navigate into array element
			FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(OutObject));
			if (ArrayIndex < 0 || ArrayIndex >= ArrayHelper.Num())
			{
				OutError = FString::Printf(TEXT("Array index %d out of bounds (size: %d)"), ArrayIndex, ArrayHelper.Num());
				return false;
			}

			// Get the inner property
			FProperty* InnerProp = ArrayProp->Inner;
			if (FObjectProperty* ObjProp = CastField<FObjectProperty>(InnerProp))
			{
				void* ElementPtr = ArrayHelper.GetRawPtr(ArrayIndex);
				UObject* ElementObj = ObjProp->GetObjectPropertyValue(ElementPtr);
				if (ElementObj && !bIsLastPart)
				{
					OutObject = ElementObj;
					OutProperty = nullptr;
					continue;
				}
			}

			if (bIsLastPart)
			{
				// Return the inner property for setting
				OutProperty = InnerProp;
				return true;
			}
			continue;
		}

		// Find the property
		OutProperty = OutObject->GetClass()->FindPropertyByName(FName(*PropertyName));

		if (!OutProperty)
		{
			OutError = FString::Printf(TEXT("Property not found: %s on %s"), *PropertyName, *OutObject->GetClass()->GetName());
			return false;
		}

		// If not the last part, navigate into nested object
		if (!bIsLastPart)
		{
			if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(OutProperty))
			{
				// Keep the array property for next iteration (index access)
				continue;
			}

			if (FObjectProperty* ObjProp = CastField<FObjectProperty>(OutProperty))
			{
				UObject* NestedObj = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(OutObject));
				if (!NestedObj)
				{
					OutError = FString::Printf(TEXT("Nested object is null: %s"), *PropertyName);
					return false;
				}
				OutObject = NestedObj;
				OutProperty = nullptr;
			}
			else if (FStructProperty* StructProp = CastField<FStructProperty>(OutProperty))
			{
				// For structs, we need to handle differently - keep the struct property
				// This is a limitation: we can only set entire structs, not nested struct members
				OutError = FString::Printf(TEXT("Cannot navigate into struct property: %s. Set the entire struct instead."), *PropertyName);
				return false;
			}
			else
			{
				OutError = FString::Printf(TEXT("Cannot navigate into property type: %s"), *PropertyName);
				return false;
			}
		}
	}

	return OutProperty != nullptr;
}

bool FMCPTool_Asset::SetPropertyFromJson(UObject* Object, const FString& PropertyPath, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Object || !Value.IsValid())
	{
		OutError = TEXT("Invalid object or value");
		return false;
	}

	// Parse property path
	TArray<FString> PathParts;
	PropertyPath.ParseIntoArray(PathParts, TEXT("."), true);

	UObject* TargetObject = nullptr;
	FProperty* Property = nullptr;

	if (!NavigateToProperty(Object, PathParts, TargetObject, Property, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Property not found: %s"), *PropertyPath);
		}
		return false;
	}

	// Get property address
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);

	// Handle object property (for setting references like materials)
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		return SetObjectPropertyValue(ObjProp, ValuePtr, Value, OutError);
	}

	// Try numeric property
	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		if (SetNumericPropertyValue(NumProp, ValuePtr, Value))
		{
			return true;
		}
	}
	// Try bool property
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool BoolVal = false;
		if (Value->TryGetBool(BoolVal))
		{
			BoolProp->SetPropertyValue(ValuePtr, BoolVal);
			return true;
		}
	}
	// Try string property
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			StrProp->SetPropertyValue(ValuePtr, StrVal);
			return true;
		}
	}
	// Try name property
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*StrVal));
			return true;
		}
	}
	// Try struct property
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (SetStructPropertyValue(StructProp, ValuePtr, Value))
		{
			return true;
		}
	}
	// Try map property (TMap)
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return SetMapPropertyFromJson(MapProp, ValuePtr, Value, PropertyPath, OutError);
	}
	// FClassProperty before FObjectPropertyBase (FClassProperty IS-A FObjectPropertyBase)
	else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			if (StrVal.IsEmpty() || StrVal == TEXT("None"))
			{
				ClassProp->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			UClass* LoadedClass = LoadClass<UObject>(nullptr, *StrVal);
			if (LoadedClass)
			{
				ClassProp->SetObjectPropertyValue(ValuePtr, LoadedClass);
				return true;
			}
		}
	}
	// Try object reference property (TObjectPtr, raw pointer)
	else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			if (StrVal.IsEmpty() || StrVal == TEXT("None"))
			{
				ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			UObject* LoadedObj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *StrVal);
			if (LoadedObj)
			{
				ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
				return true;
			}
		}
	}
	// FSoftClassProperty before FSoftObjectProperty (FSoftClassProperty IS-A FSoftObjectProperty)
	else if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
			*SoftPtr = FSoftObjectPath(StrVal);
			return true;
		}
	}
	else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
			*SoftPtr = FSoftObjectPath(StrVal);
			return true;
		}
	}

	OutError = FString::Printf(TEXT("Unsupported property type for: %s"), *PropertyPath);
	return false;
}

bool FMCPTool_Asset::SetNumericPropertyValue(FNumericProperty* NumProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value)
{
	if (NumProp->IsFloatingPoint())
	{
		double DoubleVal = 0.0;
		if (Value->TryGetNumber(DoubleVal))
		{
			NumProp->SetFloatingPointPropertyValue(ValuePtr, DoubleVal);
			return true;
		}
	}
	else if (NumProp->IsInteger())
	{
		int64 IntVal = 0;
		if (Value->TryGetNumber(IntVal))
		{
			NumProp->SetIntPropertyValue(ValuePtr, IntVal);
			return true;
		}
	}
	return false;
}

bool FMCPTool_Asset::SetStructPropertyValue(FStructProperty* StructProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value)
{
	const TSharedPtr<FJsonObject>* ObjVal;
	if (!Value->TryGetObject(ObjVal))
	{
		return false;
	}

	if (StructProp->Struct == TBaseStructure<FVector>::Get())
	{
		FVector Vec;
		(*ObjVal)->TryGetNumberField(TEXT("x"), Vec.X);
		(*ObjVal)->TryGetNumberField(TEXT("y"), Vec.Y);
		(*ObjVal)->TryGetNumberField(TEXT("z"), Vec.Z);
		*reinterpret_cast<FVector*>(ValuePtr) = Vec;
		return true;
	}

	if (StructProp->Struct == TBaseStructure<FRotator>::Get())
	{
		FRotator Rot;
		(*ObjVal)->TryGetNumberField(TEXT("pitch"), Rot.Pitch);
		(*ObjVal)->TryGetNumberField(TEXT("yaw"), Rot.Yaw);
		(*ObjVal)->TryGetNumberField(TEXT("roll"), Rot.Roll);
		*reinterpret_cast<FRotator*>(ValuePtr) = Rot;
		return true;
	}

	if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
	{
		FLinearColor Color;
		(*ObjVal)->TryGetNumberField(TEXT("r"), Color.R);
		(*ObjVal)->TryGetNumberField(TEXT("g"), Color.G);
		(*ObjVal)->TryGetNumberField(TEXT("b"), Color.B);
		(*ObjVal)->TryGetNumberField(TEXT("a"), Color.A);
		*reinterpret_cast<FLinearColor*>(ValuePtr) = Color;
		return true;
	}

	return false;
}

bool FMCPTool_Asset::SetObjectPropertyValue(FObjectProperty* ObjProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	// Value should be a string path to the object
	FString ObjectPath;
	if (!Value->TryGetString(ObjectPath))
	{
		OutError = TEXT("Object property value must be a string path");
		return false;
	}

	// Handle "None" or empty as null
	if (ObjectPath.IsEmpty() || ObjectPath.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
		return true;
	}

	// Load the referenced object
	UObject* ReferencedObject = LoadObject<UObject>(nullptr, *ObjectPath);
	if (!ReferencedObject)
	{
		OutError = FString::Printf(TEXT("Failed to load object: %s"), *ObjectPath);
		return false;
	}

	// Verify type compatibility
	if (!ReferencedObject->IsA(ObjProp->PropertyClass))
	{
		OutError = FString::Printf(TEXT("Object type mismatch. Expected %s, got %s"),
			*ObjProp->PropertyClass->GetName(), *ReferencedObject->GetClass()->GetName());
		return false;
	}

	ObjProp->SetObjectPropertyValue(ValuePtr, ReferencedObject);
	return true;
}

TSharedPtr<FJsonObject> FMCPTool_Asset::BuildAssetInfoJson(UObject* Asset)
{
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

	Info->SetStringField(TEXT("name"), Asset->GetName());
	Info->SetStringField(TEXT("path"), Asset->GetPathName());
	Info->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
	Info->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());

	// Check dirty state
	UPackage* Package = Asset->GetOutermost();
	Info->SetBoolField(TEXT("is_dirty"), Package->IsDirty());

	// Add class-specific info
	if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Asset))
	{
		TArray<TSharedPtr<FJsonValue>> MaterialsArr;
		const TArray<FSkeletalMaterial>& Materials = SkelMesh->GetMaterials();
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			TSharedPtr<FJsonObject> MatObj = MakeShared<FJsonObject>();
			MatObj->SetNumberField(TEXT("slot"), i);
			MatObj->SetStringField(TEXT("slot_name"), Materials[i].MaterialSlotName.ToString());
			MatObj->SetStringField(TEXT("material"),
				Materials[i].MaterialInterface ? Materials[i].MaterialInterface->GetPathName() : TEXT("None"));
			MaterialsArr.Add(MakeShared<FJsonValueObject>(MatObj));
		}
		Info->SetArrayField(TEXT("materials"), MaterialsArr);
	}

	return Info;
}

TArray<TSharedPtr<FJsonValue>> FMCPTool_Asset::GetAssetProperties(UObject* Asset, bool bEditableOnly)
{
	TArray<TSharedPtr<FJsonValue>> PropsArray;

	for (TFieldIterator<FProperty> PropIt(Asset->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Skip non-editable if requested
		if (bEditableOnly && !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Property->GetName());

		// Determine type string
		FString TypeStr;
		if (CastField<FNumericProperty>(Property))
		{
			TypeStr = Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>()
				? TEXT("float") : TEXT("integer");
		}
		else if (CastField<FBoolProperty>(Property))
		{
			TypeStr = TEXT("bool");
		}
		else if (CastField<FStrProperty>(Property))
		{
			TypeStr = TEXT("string");
		}
		else if (CastField<FNameProperty>(Property))
		{
			TypeStr = TEXT("name");
		}
		else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			TypeStr = FString::Printf(TEXT("struct:%s"), *StructProp->Struct->GetName());
		}
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
		{
			TypeStr = FString::Printf(TEXT("object:%s"), *ObjProp->PropertyClass->GetName());
		}
		else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
		{
			TypeStr = TEXT("array");
		}
		else
		{
			TypeStr = TEXT("other");
		}

		PropObj->SetStringField(TEXT("type"), TypeStr);
		PropObj->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));

		PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	return PropsArray;
}
