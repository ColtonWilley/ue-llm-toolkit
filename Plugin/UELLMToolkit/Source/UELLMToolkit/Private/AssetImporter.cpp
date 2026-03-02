// Copyright Natali Caggiano. All Rights Reserved.

#include "AssetImporter.h"

// Engine
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

// FBX import
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxAnimSequenceImportData.h"

// Export
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"

// Reimport
#include "Misc/FeedbackContext.h"
#include "EditorReimportHandler.h"
#include "Factories/FbxAssetImportData.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeGenericAssetsPipeline.h"

// ============================================================================
// Private Helpers
// ============================================================================

TSharedPtr<FJsonObject> FAssetImporter::SuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FAssetImporter::ErrorResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Message);
	return Result;
}

UObject* FAssetImporter::LoadAssetFromPath(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *Path);
	}
	return Loaded;
}

// ============================================================================
// Import
// ============================================================================

TSharedPtr<FJsonObject> FAssetImporter::ImportFBX(
	const FString& FbxPath, const FString& DestPath,
	const FString& MeshType, const FString& SkeletonPath,
	bool bImportMaterials, bool bImportTextures,
	bool bGenerateCollision, bool bCombineMeshes,
	bool bImportAnimations, const FString& NormalImportMethod,
	bool bReplaceExisting, bool bSave,
	int32 CustomSampleRate, bool bSnapToClosestFrameBoundary)
{
	// Validate FBX file exists
	if (!FPaths::FileExists(FbxPath))
	{
		return ErrorResult(FString::Printf(TEXT("FBX file not found: %s"), *FbxPath));
	}

	// Determine mesh type
	FString TypeLower = MeshType.ToLower();
	bool bIsAuto = (TypeLower == TEXT("auto") || TypeLower.IsEmpty());
	bool bIsStatic = (TypeLower == TEXT("static"));
	bool bIsSkeletal = (TypeLower == TEXT("skeletal"));
	bool bIsAnimation = (TypeLower == TEXT("animation"));

	if (!bIsAuto && !bIsStatic && !bIsSkeletal && !bIsAnimation)
	{
		return ErrorResult(FString::Printf(
			TEXT("Invalid mesh_type: '%s'. Valid: auto, static, skeletal, animation"), *MeshType));
	}

	// Load skeleton if needed
	USkeleton* Skeleton = nullptr;
	if (bIsSkeletal || bIsAnimation)
	{
		if (SkeletonPath.IsEmpty())
		{
			return ErrorResult(FString::Printf(
				TEXT("skeleton_path is required for mesh_type '%s'"), *MeshType));
		}

		FString SkelError;
		UObject* SkelObj = LoadAssetFromPath(SkeletonPath, SkelError);
		if (!SkelObj)
		{
			return ErrorResult(SkelError);
		}

		// Support both USkeleton and USkeletalMesh paths
		Skeleton = Cast<USkeleton>(SkelObj);
		if (!Skeleton)
		{
			if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(SkelObj))
			{
				Skeleton = SkelMesh->GetSkeleton();
			}
		}
		if (!Skeleton)
		{
			return ErrorResult(FString::Printf(
				TEXT("Could not get skeleton from: %s"), *SkeletonPath));
		}
	}

	// Setup import task
	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = FbxPath;
	Task->DestinationPath = DestPath;
	Task->bAutomated = true;
	Task->bReplaceExisting = bReplaceExisting;
	Task->bSave = bSave;

	// FBX import options
	UFbxImportUI* Options = NewObject<UFbxImportUI>();
	Options->bImportMaterials = bImportMaterials;
	Options->bImportTextures = bImportTextures;

	if (bIsAuto)
	{
		// Default to static mesh; FBX factory will override if file contains skeletal data
		Options->MeshTypeToImport = FBXIT_StaticMesh;
		Options->bImportMesh = true;
		Options->bImportAnimations = bImportAnimations;
		if (Skeleton)
		{
			Options->Skeleton = Skeleton;
		}
	}
	else if (bIsStatic)
	{
		Options->MeshTypeToImport = FBXIT_StaticMesh;
		Options->bImportMesh = true;
		Options->bImportAnimations = false;
	}
	else if (bIsSkeletal)
	{
		Options->MeshTypeToImport = FBXIT_SkeletalMesh;
		Options->Skeleton = Skeleton;
		Options->bImportMesh = true;
		Options->bImportAnimations = bImportAnimations;
	}
	else if (bIsAnimation)
	{
		Options->MeshTypeToImport = FBXIT_Animation;
		Options->Skeleton = Skeleton;
		Options->bImportMesh = false;
		Options->bImportAnimations = true;
	}

	// Static mesh specific options
	if (Options->StaticMeshImportData)
	{
		Options->StaticMeshImportData->bAutoGenerateCollision = bGenerateCollision;
		Options->StaticMeshImportData->bCombineMeshes = bCombineMeshes;

		// Normal import method
		if (NormalImportMethod.Equals(TEXT("compute"), ESearchCase::IgnoreCase))
		{
			Options->StaticMeshImportData->NormalImportMethod = FBXNIM_ComputeNormals;
		}
		else if (NormalImportMethod.Equals(TEXT("mikk"), ESearchCase::IgnoreCase))
		{
			Options->StaticMeshImportData->NormalImportMethod = FBXNIM_ImportNormalsAndTangents;
		}
		else
		{
			// Default: import normals from FBX
			Options->StaticMeshImportData->NormalImportMethod = FBXNIM_ImportNormals;
		}
	}

	// Skeletal mesh specific options
	if (Options->SkeletalMeshImportData)
	{
		Options->SkeletalMeshImportData->bImportMeshesInBoneHierarchy = true;
	}

	// Animation specific options
	if (Options->AnimSequenceImportData)
	{
		Options->AnimSequenceImportData->bImportBoneTracks = true;
		Options->AnimSequenceImportData->bConvertScene = true;
		if (CustomSampleRate > 0)
		{
			Options->AnimSequenceImportData->bUseDefaultSampleRate = false;
			Options->AnimSequenceImportData->CustomSampleRate = CustomSampleRate;
		}
		if (bSnapToClosestFrameBoundary)
		{
			Options->AnimSequenceImportData->bSnapToClosestFrameBoundary = true;
		}
	}

	Task->Options = Options;

	// Run import
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.ImportAssetTasks({Task});

	// Check result
	TArray<UObject*> ImportedObjects = Task->GetObjects();
	if (ImportedObjects.Num() == 0)
	{
		return ErrorResult(FString::Printf(TEXT("Import failed for: %s"), *FbxPath));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Imported %d asset(s) from %s"), ImportedObjects.Num(), *FPaths::GetBaseFilename(FbxPath)));

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (UObject* Obj : ImportedObjects)
	{
		if (Obj)
		{
			TSharedPtr<FJsonObject> AssetJson = MakeShared<FJsonObject>();
			AssetJson->SetStringField(TEXT("name"), Obj->GetName());
			AssetJson->SetStringField(TEXT("path"), Obj->GetPathName());
			AssetJson->SetStringField(TEXT("class"), Obj->GetClass()->GetName());
			AssetsArray.Add(MakeShared<FJsonValueObject>(AssetJson));
		}
	}
	Result->SetArrayField(TEXT("imported_assets"), AssetsArray);

	return Result;
}

// ============================================================================
// Batch Import
// ============================================================================

TSharedPtr<FJsonObject> FAssetImporter::BatchImportFBX(
	const FString& FbxDirectory, const FString& DestPath,
	const FString& MeshType, const FString& SkeletonPath,
	bool bImportMaterials, bool bImportTextures,
	bool bGenerateCollision, bool bCombineMeshes,
	bool bImportAnimations, const FString& NormalImportMethod,
	const FString& FilePattern,
	bool bReplaceExisting, bool bSave,
	int32 CustomSampleRate, bool bSnapToClosestFrameBoundary)
{
	// Find FBX files in directory
	FString Extension = TEXT("fbx");
	if (!FilePattern.IsEmpty() && FilePattern != TEXT("*.fbx"))
	{
		// Extract extension from pattern like "*.fbx"
		int32 DotIdx;
		if (FilePattern.FindLastChar('.', DotIdx))
		{
			Extension = FilePattern.Mid(DotIdx + 1);
		}
	}

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *FbxDirectory, *Extension);

	if (FoundFiles.Num() == 0)
	{
		return ErrorResult(FString::Printf(TEXT("No .%s files found in: %s"), *Extension, *FbxDirectory));
	}

	int32 SuccessCount = 0;
	int32 FailCount = 0;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (const FString& FileName : FoundFiles)
	{
		FString FullPath = FPaths::Combine(FbxDirectory, FileName);
		TSharedPtr<FJsonObject> ImportResult = ImportFBX(
			FullPath, DestPath, MeshType, SkeletonPath,
			bImportMaterials, bImportTextures,
			bGenerateCollision, bCombineMeshes,
			bImportAnimations, NormalImportMethod,
			bReplaceExisting, bSave,
			CustomSampleRate, bSnapToClosestFrameBoundary);

		bool bSuccess = false;
		ImportResult->TryGetBoolField(TEXT("success"), bSuccess);
		if (bSuccess) SuccessCount++;
		else FailCount++;

		ImportResult->SetStringField(TEXT("file"), FileName);
		ResultsArray.Add(MakeShared<FJsonValueObject>(ImportResult));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), FailCount == 0);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Batch import: %d succeeded, %d failed, %d total"),
		SuccessCount, FailCount, FoundFiles.Num()));
	Result->SetNumberField(TEXT("success_count"), SuccessCount);
	Result->SetNumberField(TEXT("fail_count"), FailCount);
	Result->SetNumberField(TEXT("total"), FoundFiles.Num());
	Result->SetArrayField(TEXT("results"), ResultsArray);

	return Result;
}

// ============================================================================
// Export
// ============================================================================

TSharedPtr<FJsonObject> FAssetImporter::ExportAsset(
	const FString& AssetPath, const FString& OutputFilePath)
{
	// Load the asset
	FString LoadError;
	UObject* Asset = LoadAssetFromPath(AssetPath, LoadError);
	if (!Asset)
	{
		return ErrorResult(LoadError);
	}

	// Ensure output directory exists
	FString OutputDir = FPaths::GetPath(OutputFilePath);
	if (!OutputDir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*OutputDir, true);
	}

	// Create export task
	UAssetExportTask* ExportTask = NewObject<UAssetExportTask>();
	ExportTask->Object = Asset;
	ExportTask->Filename = OutputFilePath;
	ExportTask->bAutomated = true;
	ExportTask->bPrompt = false;
	ExportTask->bReplaceIdentical = true;
	ExportTask->bSelected = false;

	// Find appropriate exporter
	UExporter* Exporter = UExporter::FindExporter(Asset, *FPaths::GetExtension(OutputFilePath));
	if (Exporter)
	{
		ExportTask->Exporter = Exporter;
	}

	bool bSuccess = UExporter::RunAssetExportTask(ExportTask);

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
			TEXT("Exported %s to %s"), *Asset->GetName(), *OutputFilePath));
		Result->SetStringField(TEXT("asset_path"), AssetPath);
		Result->SetStringField(TEXT("output_path"), OutputFilePath);
		Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
		return Result;
	}
	else
	{
		return ErrorResult(FString::Printf(
			TEXT("Export failed for %s. Check that the asset type supports FBX export."), *AssetPath));
	}
}

// ============================================================================
// Reimport
// ============================================================================

TSharedPtr<FJsonObject> FAssetImporter::ReimportAsset(
	const FString& AssetPath, const FString& NewSourcePath,
	bool bOverrideRotation, const FRotator& ImportRotation,
	bool bOverrideTranslation, const FVector& ImportTranslation,
	float ImportUniformScale)
{
	// Load the asset
	FString LoadError;
	UObject* Asset = LoadAssetFromPath(AssetPath, LoadError);
	if (!Asset)
	{
		return ErrorResult(LoadError);
	}

	// Get AssetImportData from the asset (needed for source path and transform overrides)
	UAssetImportData* ImportData = nullptr;

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		ImportData = SM->GetAssetImportData();
	}
	else if (USkeletalMesh* SK = Cast<USkeletalMesh>(Asset))
	{
		ImportData = SK->GetAssetImportData();
	}
	else if (UAnimSequence* Anim = Cast<UAnimSequence>(Asset))
	{
		ImportData = Anim->AssetImportData;
	}

	// If a new source path is specified, update the asset import data
	if (!NewSourcePath.IsEmpty())
	{
		if (!FPaths::FileExists(NewSourcePath))
		{
			return ErrorResult(FString::Printf(TEXT("New source file not found: %s"), *NewSourcePath));
		}

		if (ImportData)
		{
			ImportData->UpdateFilenameOnly(NewSourcePath);
		}
		else
		{
			return ErrorResult(FString::Printf(
				TEXT("Cannot update source path — asset %s has no AssetImportData"), *AssetPath));
		}
	}

	// Apply transform overrides (supports both FBX and Interchange pipelines)
	if (bOverrideRotation || bOverrideTranslation || ImportUniformScale > 0.0f)
	{
		bool bApplied = false;

		// Try legacy FBX pipeline first
		if (UFbxAssetImportData* FbxImportData = Cast<UFbxAssetImportData>(ImportData))
		{
			if (bOverrideRotation)
			{
				FbxImportData->ImportRotation = ImportRotation;
			}
			if (bOverrideTranslation)
			{
				FbxImportData->ImportTranslation = ImportTranslation;
			}
			if (ImportUniformScale > 0.0f)
			{
				FbxImportData->ImportUniformScale = ImportUniformScale;
			}
			bApplied = true;
		}
		// Try Interchange pipeline
		else if (UInterchangeAssetImportData* InterchangeImportData = Cast<UInterchangeAssetImportData>(ImportData))
		{
			TArray<UObject*> Pipelines = InterchangeImportData->GetPipelines();
			UInterchangeGenericAssetsPipeline* GenericPipeline = nullptr;
			for (UObject* Pipeline : Pipelines)
			{
				GenericPipeline = Cast<UInterchangeGenericAssetsPipeline>(Pipeline);
				if (GenericPipeline)
				{
					break;
				}
			}

			if (!GenericPipeline)
			{
				return ErrorResult(FString::Printf(
					TEXT("Cannot apply transform overrides — asset %s has no GenericAssetsPipeline in its Interchange data"),
					*AssetPath));
			}

			if (bOverrideRotation)
			{
				GenericPipeline->ImportOffsetRotation = ImportRotation;
			}
			if (bOverrideTranslation)
			{
				GenericPipeline->ImportOffsetTranslation = ImportTranslation;
			}
			if (ImportUniformScale > 0.0f)
			{
				GenericPipeline->ImportOffsetUniformScale = ImportUniformScale;
			}

			InterchangeImportData->SetPipelines(Pipelines);
			bApplied = true;
		}

		if (!bApplied)
		{
			FString ImportDataClass = ImportData ? ImportData->GetClass()->GetName() : TEXT("null");
			return ErrorResult(FString::Printf(
				TEXT("Cannot apply transform overrides — asset %s import data type %s is not supported"),
				*AssetPath, *ImportDataClass));
		}
	}

	// Perform reimport
	FReimportManager* ReimportManager = FReimportManager::Instance();
	if (!ReimportManager)
	{
		return ErrorResult(TEXT("FReimportManager not available"));
	}

	bool bCanReimport = ReimportManager->CanReimport(Asset);
	if (!bCanReimport)
	{
		return ErrorResult(FString::Printf(
			TEXT("Asset cannot be reimported: %s. No source file found."), *AssetPath));
	}

	// bAskForNewFileIfMissing = false, bShowNotification = false
	bool bReimportSuccess = ReimportManager->Reimport(Asset, false, false);

	if (bReimportSuccess)
	{
		TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
			TEXT("Reimported %s successfully"), *Asset->GetName()));
		Result->SetStringField(TEXT("asset_path"), AssetPath);
		Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
		if (!NewSourcePath.IsEmpty())
		{
			Result->SetStringField(TEXT("new_source_path"), NewSourcePath);
		}
		return Result;
	}
	else
	{
		return ErrorResult(FString::Printf(TEXT("Reimport failed for: %s"), *AssetPath));
	}
}

// ============================================================================
// Get Source Info
// ============================================================================

TSharedPtr<FJsonObject> FAssetImporter::GetSourceInfo(const FString& AssetPath)
{
	// Load the asset
	FString LoadError;
	UObject* Asset = LoadAssetFromPath(AssetPath, LoadError);
	if (!Asset)
	{
		return ErrorResult(LoadError);
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Source info for %s"), *Asset->GetName()));
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());

	// Try to find AssetImportData
	UAssetImportData* ImportData = nullptr;

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		ImportData = SM->GetAssetImportData();
	}
	else if (USkeletalMesh* SK = Cast<USkeletalMesh>(Asset))
	{
		ImportData = SK->GetAssetImportData();
	}
	else if (UAnimSequence* Anim = Cast<UAnimSequence>(Asset))
	{
		ImportData = Anim->AssetImportData;
	}

	if (ImportData)
	{
		Result->SetBoolField(TEXT("has_import_data"), true);

		TArray<FString> SourceFiles;
		ImportData->ExtractFilenames(SourceFiles);

		TArray<TSharedPtr<FJsonValue>> SourceArray;
		for (const FString& SourceFile : SourceFiles)
		{
			TSharedPtr<FJsonObject> FileJson = MakeShared<FJsonObject>();
			FileJson->SetStringField(TEXT("path"), SourceFile);
			FileJson->SetBoolField(TEXT("exists"), FPaths::FileExists(SourceFile));
			SourceArray.Add(MakeShared<FJsonValueObject>(FileJson));
		}
		Result->SetArrayField(TEXT("source_files"), SourceArray);
	}
	else
	{
		Result->SetBoolField(TEXT("has_import_data"), false);
		Result->SetArrayField(TEXT("source_files"), TArray<TSharedPtr<FJsonValue>>());
	}

	// Check if reimport is possible
	FReimportManager* ReimportManager = FReimportManager::Instance();
	bool bCanReimport = ReimportManager ? ReimportManager->CanReimport(Asset) : false;
	Result->SetBoolField(TEXT("can_reimport"), bCanReimport);

	return Result;
}
