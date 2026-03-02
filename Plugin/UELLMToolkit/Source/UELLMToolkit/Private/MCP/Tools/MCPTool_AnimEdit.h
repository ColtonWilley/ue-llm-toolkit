// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Animation track editing — batch adjust bone track keys, inspect tracks, resample.
 *
 * Operations:
 *   - adjust_track: Offset position/rotation of a bone track across one or more animations
 *   - inspect_track: Read position/rotation/scale at sampled frames for a bone track
 *   - resample: Resample animations to a target frame rate, preserving length
 *   - replace_skeleton: Set a new skeleton on one or more animation assets
 */
class FMCPTool_AnimEdit : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("anim_edit");
		Info.Description = TEXT(
			"Animation track editing — batch adjust bone track keys, inspect tracks, resample, replace skeleton, curve operations.\n\n"
			"Operations:\n"
			"- 'adjust_track': Offset position/rotation of a bone track across one or more animations.\n"
			"  Adds location_offset to every position key, pre-multiplies rotation_offset into every rotation key.\n"
			"- 'inspect_track': Read position/rotation/scale at sampled frames for a bone track.\n"
			"  Returns euler and quaternion rotation. Frame index -1 = last frame.\n"
			"- 'resample': Resample animations to a target frame rate. Samples all bone tracks at new\n"
			"  frame positions using engine interpolation, overwrites tracks, fixes metadata.\n"
			"  Preserves animation length exactly — only key count changes.\n"
			"- 'replace_skeleton': Set a new skeleton on one or more animation assets.\n"
			"  Accepts USkeleton or USkeletalMesh path (extracts skeleton from mesh).\n"
			"- 'get_curves': Read all float curves with full key data from an animation.\n"
			"- 'add_curve': Add a new float curve, optionally with initial keys.\n"
			"- 'remove_curve': Remove an existing float curve.\n"
			"- 'set_curve_keys': Replace all keys on an existing curve."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation name: 'adjust_track', 'inspect_track', 'resample', 'replace_skeleton', 'get_curves', 'add_curve', 'remove_curve', or 'set_curve_keys'"), true),
			FMCPToolParameter(TEXT("asset_paths"), TEXT("array"), TEXT("Array of animation asset paths (adjust_track, resample — batch)")),
			FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Single animation asset path (inspect_track, get_curves, add_curve, remove_curve, set_curve_keys)")),
			FMCPToolParameter(TEXT("bone_name"), TEXT("string"), TEXT("Bone track name to modify/inspect (adjust_track, inspect_track)")),
			FMCPToolParameter(TEXT("location_offset"), TEXT("object"), TEXT("{x,y,z} added to every position key")),
			FMCPToolParameter(TEXT("rotation_offset"), TEXT("object"), TEXT("{pitch,yaw,roll} pre-multiplied into every rotation key")),
			FMCPToolParameter(TEXT("target_fps"), TEXT("number"), TEXT("Target frame rate for resample (e.g. 30, 60)")),
			FMCPToolParameter(TEXT("skeleton_path"), TEXT("string"), TEXT("Path to USkeleton or USkeletalMesh (replace_skeleton)")),
			FMCPToolParameter(TEXT("curve_name"), TEXT("string"), TEXT("Curve name (add_curve, remove_curve, set_curve_keys)")),
			FMCPToolParameter(TEXT("keys"), TEXT("array"), TEXT("Array of key objects {time, value, interp_mode?, tangent_mode?, arrive_tangent?, leave_tangent?} (add_curve optional, set_curve_keys required)")),
			FMCPToolParameter(TEXT("save"), TEXT("boolean"), TEXT("Save after modification"), false, TEXT("true")),
			FMCPToolParameter(TEXT("sample_frames"), TEXT("array"), TEXT("Frame indices to inspect (-1 = last frame)"), false, TEXT("[0, -1]"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult HandleAdjustTrack(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleInspectTrack(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleResample(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleReplaceSkeleton(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleGetCurves(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddCurve(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveCurve(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetCurveKeys(const TSharedRef<FJsonObject>& Params);
};
