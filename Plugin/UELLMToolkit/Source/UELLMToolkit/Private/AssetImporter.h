// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Utility class for general-purpose FBX import/export/reimport.
 * Covers: static mesh, skeletal mesh, animation import; asset export; reimport.
 *
 * All methods are static, return JSON, and contain no MCP/JSON dispatch logic.
 * Uses UAssetImportTask + UFbxImportUI (same proven API as RetargetEditor).
 */
class FAssetImporter
{
public:
	// ===== Import =====

	/** Import a single FBX file. mesh_type: "auto", "static", "skeletal", "animation". */
	static TSharedPtr<FJsonObject> ImportFBX(
		const FString& FbxPath, const FString& DestPath,
		const FString& MeshType, const FString& SkeletonPath,
		bool bImportMaterials, bool bImportTextures,
		bool bGenerateCollision, bool bCombineMeshes,
		bool bImportAnimations, const FString& NormalImportMethod,
		bool bReplaceExisting, bool bSave,
		int32 CustomSampleRate = 0,
		bool bSnapToClosestFrameBoundary = false);

	/** Import all FBX files from a directory. */
	static TSharedPtr<FJsonObject> BatchImportFBX(
		const FString& FbxDirectory, const FString& DestPath,
		const FString& MeshType, const FString& SkeletonPath,
		bool bImportMaterials, bool bImportTextures,
		bool bGenerateCollision, bool bCombineMeshes,
		bool bImportAnimations, const FString& NormalImportMethod,
		const FString& FilePattern,
		bool bReplaceExisting, bool bSave,
		int32 CustomSampleRate = 0,
		bool bSnapToClosestFrameBoundary = false);

	// ===== Export =====

	/** Export an existing asset to FBX on disk. */
	static TSharedPtr<FJsonObject> ExportAsset(
		const FString& AssetPath, const FString& OutputFilePath);

	// ===== Reimport =====

	/** Reimport an asset from its source FBX (or a new source path). */
	static TSharedPtr<FJsonObject> ReimportAsset(
		const FString& AssetPath, const FString& NewSourcePath,
		bool bOverrideRotation = false, const FRotator& ImportRotation = FRotator::ZeroRotator,
		bool bOverrideTranslation = false, const FVector& ImportTranslation = FVector::ZeroVector,
		float ImportUniformScale = 0.0f);

	// ===== Info =====

	/** Get import source info for an asset (source files, timestamps, can_reimport). */
	static TSharedPtr<FJsonObject> GetSourceInfo(const FString& AssetPath);

private:
	static TSharedPtr<FJsonObject> SuccessResult(const FString& Message);
	static TSharedPtr<FJsonObject> ErrorResult(const FString& Message);
	static UObject* LoadAssetFromPath(const FString& Path, FString& OutError);
};
