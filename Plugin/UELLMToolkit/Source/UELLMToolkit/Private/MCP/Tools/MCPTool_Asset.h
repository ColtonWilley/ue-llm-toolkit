// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * MCP Tool for generic asset operations.
 *
 * Provides operations for:
 * - Setting properties on any loaded asset using reflection
 * - Saving assets to disk
 * - Getting asset information
 *
 * This tool works with assets in the Content Browser, not actors in a level.
 * Use set_property for actor properties, use this tool for asset properties.
 *
 * Operations:
 * - set_asset_property: Set a property on an asset using reflection
 * - save_asset: Save an asset to disk (mark dirty and/or save)
 * - get_asset_info: Get information about an asset
 * - list_assets: List assets in a directory with optional filtering
 * - rename: Rename and/or move an asset (creates redirector)
 * - duplicate: Copy an asset to a new location
 * - delete: Delete asset(s) from the project
 * - get_hashes: Batch staleness check via filesystem metadata (no asset loading)
 * - save_all: Save all dirty packages silently (no dialog)
 */
class FMCPTool_Asset : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// Operation handlers
	FMCPToolResult ExecuteSetAssetProperty(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteSaveAsset(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteGetAssetInfo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteListAssets(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteRename(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteDuplicate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteDelete(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteMigrate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteCreateFolder(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteRenameFolder(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteDeleteFolder(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteGetHashes(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteSaveAll(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteOpenEditor(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteSetPreviewMesh(const TSharedRef<FJsonObject>& Params);

	// Property reflection helpers (adapted from SetProperty tool)
	bool NavigateToProperty(
		UObject* StartObject,
		const TArray<FString>& PathParts,
		UObject*& OutObject,
		FProperty*& OutProperty,
		FString& OutError);

	bool SetPropertyFromJson(
		UObject* Object,
		const FString& PropertyPath,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

	bool SetNumericPropertyValue(FNumericProperty* NumProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value);
	bool SetStructPropertyValue(FStructProperty* StructProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value);
	bool SetObjectPropertyValue(FObjectProperty* ObjProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	// Asset loading
	UObject* LoadAssetByPath(const FString& AssetPath, FString& OutError);

	// Utility
	TSharedPtr<FJsonObject> BuildAssetInfoJson(UObject* Asset);
	TArray<TSharedPtr<FJsonValue>> GetAssetProperties(UObject* Asset, bool bEditableOnly);
};
