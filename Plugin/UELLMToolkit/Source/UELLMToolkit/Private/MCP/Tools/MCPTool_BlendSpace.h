// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Blend space read + write operations.
 *
 * Read Operations:
 *   - 'inspect': Full read of a blend space — axes, samples, interpolation, geometry
 *   - 'list': Find blend space assets in a content folder
 *
 * Write Operations:
 *   - 'create': Create a new BlendSpace or BlendSpace1D
 *   - 'add_sample': Add animation at a position
 *   - 'remove_sample': Remove sample by index
 *   - 'move_sample': Move sample to new position
 *   - 'set_sample_animation': Replace animation on existing sample
 *   - 'set_axis': Configure axis parameters and interpolation
 *   - 'save': Save blend space to disk
 *   - 'batch': Multiple operations with single ResampleData
 */
class FMCPTool_BlendSpace : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("blend_space");
		Info.Description = TEXT(
			"Blend space read + write tool.\n\n"
			"READ OPERATIONS:\n"
			"- 'inspect': Full read — axes, samples, interpolation, geometry.\n"
			"  Params: asset_path (required)\n\n"
			"- 'list': Find blend spaces in a content folder.\n"
			"  Params: folder_path (required), recursive (optional, default false)\n\n"
			"WRITE OPERATIONS:\n"
			"- 'create': Create a new BlendSpace or BlendSpace1D.\n"
			"  Params: package_path, name, skeleton_path (required); type (optional: 'BlendSpace1D' or 'BlendSpace', default 'BlendSpace1D')\n\n"
			"- 'add_sample': Add animation at a position. Auto-expands axis range.\n"
			"  Params: asset_path, animation_path (required); x, y, rate_scale (optional)\n\n"
			"- 'remove_sample': Remove sample by index. Warning: uses RemoveAtSwap — indices shift.\n"
			"  Params: asset_path, sample_index (required)\n\n"
			"- 'move_sample': Move sample to new position. Auto-expands axis range.\n"
			"  Params: asset_path, sample_index (required); x, y, rate_scale (optional)\n\n"
			"- 'set_sample_animation': Replace animation on existing sample.\n"
			"  Params: asset_path, sample_index, animation_path (required)\n\n"
			"- 'set_axis': Configure axis parameters + interpolation.\n"
			"  Params: asset_path, axis_index (required); axis_name, min, max, grid_divisions, snap_to_grid, wrap_input, interp_time, interp_type, damping_ratio, max_speed (all optional)\n"
			"  interp_type values: Average, Linear, Cubic, EaseInOut, ExponentialDecay, SpringDamper\n\n"
			"- 'save': Save blend space to disk.\n"
			"  Params: asset_path (required)\n\n"
			"- 'batch': Multiple ops with single ResampleData at end. More efficient for multi-step setup.\n"
			"  Params: asset_path (required), operations (array of {op, ...params})\n"
			"  Valid batch ops: add_sample, remove_sample, move_sample, set_sample_animation, set_axis"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation: inspect, list, create, add_sample, remove_sample, move_sample, set_sample_animation, set_axis, save, batch"), true),
			FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Blend space asset path")),
			FMCPToolParameter(TEXT("folder_path"), TEXT("string"), TEXT("Content folder path (for list)")),
			FMCPToolParameter(TEXT("recursive"), TEXT("boolean"), TEXT("Search subfolders (for list)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("package_path"), TEXT("string"), TEXT("Package path for new asset (for create)")),
			FMCPToolParameter(TEXT("name"), TEXT("string"), TEXT("Asset name (for create)")),
			FMCPToolParameter(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton asset path (for create)")),
			FMCPToolParameter(TEXT("type"), TEXT("string"), TEXT("'BlendSpace1D' or 'BlendSpace' (for create)"), false, TEXT("BlendSpace1D")),
			FMCPToolParameter(TEXT("animation_path"), TEXT("string"), TEXT("Animation sequence path")),
			FMCPToolParameter(TEXT("sample_index"), TEXT("number"), TEXT("Sample index")),
			FMCPToolParameter(TEXT("x"), TEXT("number"), TEXT("X position"), false, TEXT("0")),
			FMCPToolParameter(TEXT("y"), TEXT("number"), TEXT("Y position"), false, TEXT("0")),
			FMCPToolParameter(TEXT("rate_scale"), TEXT("number"), TEXT("Per-sample playback speed multiplier (0.01-64.0, default 1.0)")),
			FMCPToolParameter(TEXT("axis_index"), TEXT("number"), TEXT("Axis index (0 or 1)")),
			FMCPToolParameter(TEXT("axis_name"), TEXT("string"), TEXT("Axis display name")),
			FMCPToolParameter(TEXT("min"), TEXT("number"), TEXT("Axis minimum value")),
			FMCPToolParameter(TEXT("max"), TEXT("number"), TEXT("Axis maximum value")),
			FMCPToolParameter(TEXT("grid_divisions"), TEXT("number"), TEXT("Number of grid divisions")),
			FMCPToolParameter(TEXT("snap_to_grid"), TEXT("boolean"), TEXT("Snap samples to grid")),
			FMCPToolParameter(TEXT("wrap_input"), TEXT("boolean"), TEXT("Wrap input around axis range")),
			FMCPToolParameter(TEXT("interp_time"), TEXT("number"), TEXT("Interpolation time in seconds")),
			FMCPToolParameter(TEXT("interp_type"), TEXT("string"), TEXT("Interpolation type: Average, Linear, Cubic, EaseInOut, ExponentialDecay, SpringDamper")),
			FMCPToolParameter(TEXT("damping_ratio"), TEXT("number"), TEXT("Damping ratio for SpringDamper interpolation")),
			FMCPToolParameter(TEXT("max_speed"), TEXT("number"), TEXT("Max interpolation speed")),
			FMCPToolParameter(TEXT("operations"), TEXT("array"), TEXT("Batch operations array [{op, ...params}]"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// Read operations
	FMCPToolResult HandleInspect(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleList(const TSharedRef<FJsonObject>& Params);

	// Write operations
	FMCPToolResult HandleCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddSample(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveSample(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleMoveSample(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetSampleAnimation(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetAxis(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSave(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleBatch(const TSharedRef<FJsonObject>& Params);
};
