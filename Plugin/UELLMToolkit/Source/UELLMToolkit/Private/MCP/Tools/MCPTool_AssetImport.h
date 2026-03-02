// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: General-purpose FBX import/export/reimport.
 *
 * Operations:
 *   - 'import': Import a single FBX file (static mesh, skeletal mesh, or animation)
 *   - 'batch_import': Import all FBX files from a directory
 *   - 'export': Export an existing asset to FBX on disk
 *   - 'reimport': Reimport an asset from its source FBX (or a new source path)
 *   - 'get_source': Get import source info for an asset
 */
class FMCPTool_AssetImport : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("asset_import");
		Info.Description = TEXT(
			"General-purpose FBX import/export/reimport tool.\n\n"
			"OPERATIONS:\n"
			"- 'import': Import a single FBX file.\n"
			"  Required: fbx_path, dest_path\n"
			"  Optional: mesh_type ('auto'|'static'|'skeletal'|'animation', default 'auto'),\n"
			"    skeleton_path (required for skeletal/animation),\n"
			"    import_materials (default true), import_textures (default true),\n"
			"    generate_collision (default true, static only), combine_meshes (default false),\n"
			"    import_animations (default false), normal_import ('compute'|'import'|'mikk', default 'import'),\n"
			"    replace_existing (default true), save (default true)\n\n"
			"- 'batch_import': Import all FBX from a directory.\n"
			"  Required: fbx_directory, dest_path\n"
			"  Optional: same as import, plus file_pattern (default '*.fbx')\n\n"
			"- 'export': Export an asset to FBX.\n"
			"  Required: asset_path, output_path\n\n"
			"- 'reimport': Reimport from source FBX.\n"
			"  Required: asset_path\n"
			"  Optional: new_source_path (update source before reimport),\n"
			"    import_rotation ({pitch,yaw,roll} in degrees),\n"
			"    import_translation ({x,y,z}),\n"
			"    import_uniform_scale (number, must be >0)\n\n"
			"- 'get_source': Get import source info.\n"
			"  Required: asset_path\n"
			"  Returns: source files, timestamps, can_reimport"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation: import, batch_import, export, reimport, get_source"), true),
			FMCPToolParameter(TEXT("fbx_path"), TEXT("string"), TEXT("Disk path to FBX file (for import)")),
			FMCPToolParameter(TEXT("fbx_directory"), TEXT("string"), TEXT("Directory containing FBX files (for batch_import)")),
			FMCPToolParameter(TEXT("dest_path"), TEXT("string"), TEXT("Content destination path, e.g. /Game/Meshes")),
			FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Asset path for export/reimport/get_source")),
			FMCPToolParameter(TEXT("output_path"), TEXT("string"), TEXT("Output file path (for export)")),
			FMCPToolParameter(TEXT("mesh_type"), TEXT("string"), TEXT("'auto', 'static', 'skeletal', or 'animation'"), false, TEXT("auto")),
			FMCPToolParameter(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton or skeletal mesh path (required for skeletal/animation)")),
			FMCPToolParameter(TEXT("import_materials"), TEXT("boolean"), TEXT("Import materials from FBX"), false, TEXT("true")),
			FMCPToolParameter(TEXT("import_textures"), TEXT("boolean"), TEXT("Import textures from FBX"), false, TEXT("true")),
			FMCPToolParameter(TEXT("generate_collision"), TEXT("boolean"), TEXT("Auto-generate collision (static mesh only)"), false, TEXT("true")),
			FMCPToolParameter(TEXT("combine_meshes"), TEXT("boolean"), TEXT("Combine all meshes into one asset"), false, TEXT("false")),
			FMCPToolParameter(TEXT("import_animations"), TEXT("boolean"), TEXT("Import animations from FBX"), false, TEXT("false")),
			FMCPToolParameter(TEXT("normal_import"), TEXT("string"), TEXT("'compute', 'import', or 'mikk'"), false, TEXT("import")),
			FMCPToolParameter(TEXT("replace_existing"), TEXT("boolean"), TEXT("Overwrite existing assets"), false, TEXT("true")),
			FMCPToolParameter(TEXT("save"), TEXT("boolean"), TEXT("Save imported assets to disk"), false, TEXT("true")),
			FMCPToolParameter(TEXT("file_pattern"), TEXT("string"), TEXT("File pattern for batch_import"), false, TEXT("*.fbx")),
			FMCPToolParameter(TEXT("new_source_path"), TEXT("string"), TEXT("New source FBX path (for reimport)")),
			FMCPToolParameter(TEXT("import_rotation"), TEXT("object"), TEXT("{pitch,yaw,roll} in degrees — FBX import rotation override (reimport)")),
			FMCPToolParameter(TEXT("import_translation"), TEXT("object"), TEXT("{x,y,z} — FBX import translation override (reimport)")),
			FMCPToolParameter(TEXT("import_uniform_scale"), TEXT("number"), TEXT("FBX import uniform scale override, must be >0 (reimport)")),
			FMCPToolParameter(TEXT("custom_sample_rate"), TEXT("number"), TEXT("Custom sample rate for anim import (0=use default)"), false, TEXT("0")),
			FMCPToolParameter(TEXT("snap_to_frame_boundary"), TEXT("boolean"),
				TEXT("Snap anim length to closest frame boundary on import (for sub-frame-aligned FBX)"),
				false, TEXT("false"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult HandleImport(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleBatchImport(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleExport(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleReimport(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleGetSource(const TSharedRef<FJsonObject>& Params);
};
