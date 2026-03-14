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
			"- 'adjust_track': Adjust a bone track across one or more animations.\n"
			"  Adds location_offset to every position key, pre-multiplies rotation_offset into every rotation key.\n"
			"  scale_override replaces scale on all keys (absolute, not additive — use for fixing baked negative scale).\n"
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
			"- 'set_curve_keys': Replace all keys on an existing curve.\n"
			"- 'sync_mesh_bones': Add bones from USkeleton that are missing in the mesh's FReferenceSkeleton.\n"
			"  Enables CopyPoseFromMesh for bones that exist in skeleton but not in mesh (zero-weight).\n"
			"- 'rename_bone': Rename a bone in a USkeletalMesh and optionally its USkeleton.\n"
			"  If skeleton already has new_name, renames only in mesh (syncs stale mesh to skeleton).\n"
			"  Otherwise renames in both and rebuilds animation linkup data.\n"
			"- 'set_ref_pose': Modify the reference (bind) pose of a bone.\n"
			"  Accepts USkeletalMesh or USkeleton path via mesh_path.\n"
			"  Mesh: full 'Apply Pose as Rest Pose' — retransforms vertices, rebuilds inverse bind matrices.\n"
			"  Use retransform_vertices=false to change bind pose without adjusting mesh vertices.\n"
			"  Skeleton: updates skeleton ref pose + propagates to dependent assets. No vertex work.\n"
			"- 'set_additive_type': Set additive animation type on an AnimSequence.\n"
			"  Params: asset_path (required), additive_anim_type (required: 'None', 'LocalSpaceBase', 'RotationOffsetMeshSpace'),\n"
			"  base_pose_type (optional: 'None', 'RefPose', 'AnimScaled', 'AnimFrame', 'LocalAnimFrame', default 'RefPose'),\n"
			"  ref_pose_seq (optional: asset path to base pose animation — required when base_pose_type is 'AnimScaled' or 'AnimFrame'),\n"
			"  ref_frame_index (optional: frame index for AnimFrame/LocalAnimFrame, default 0), save (optional, default true).\n"
			"  Fires PostEditChangeProperty to trigger recompression.\n"
			"- 'transform_vertices': Apply a transform (rotation, scale, translation) directly to mesh vertex positions.\n"
			"  Operates on raw MeshDescription geometry — does NOT touch bones, skin weights, or bind poses.\n"
			"  Equivalent to Blender edit-mode select-all transform. Use to fix axis/scale mismatches.\n"
			"  Params: mesh_path (required), rotation {pitch,yaw,roll}, scale {x,y,z} or float, translation {x,y,z}.\n"
			"- 'inspect_mesh': Read-only diagnostic dump of skeletal mesh internals.\n"
			"  Returns: bone hierarchy with bind poses (local + CS), inverse bind matrices,\n"
			"  per-bone vertex count + weight sum, sample vertex positions + weights.\n"
			"  Params: mesh_path (required).\n"
			"- 'extract_range': Extract a frame range from an animation into a new asset.\n"
			"  Duplicates the source, then trims to keep only [start_frame, end_frame].\n"
			"  Params: asset_path (required), start_frame (required, 0-indexed inclusive),\n"
			"  end_frame (required, 0-indexed inclusive, -1 = last frame),\n"
			"  dest_path (optional: destination folder, default same as source),\n"
			"  new_name (optional: asset name, default {Source}_F{start}_{end}), save (optional, default true)."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation name: 'adjust_track', 'inspect_track', 'resample', 'replace_skeleton', 'get_curves', 'add_curve', 'remove_curve', 'set_curve_keys', 'sync_mesh_bones', 'rename_bone', 'set_ref_pose', 'set_additive_type', 'transform_vertices', 'inspect_mesh', or 'extract_range'"), true),
			FMCPToolParameter(TEXT("asset_paths"), TEXT("array"), TEXT("Array of animation asset paths (adjust_track, resample — batch)")),
			FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Single animation asset path (inspect_track, get_curves, add_curve, remove_curve, set_curve_keys)")),
			FMCPToolParameter(TEXT("bone_name"), TEXT("string"), TEXT("Bone track name to modify/inspect (adjust_track, inspect_track)")),
			FMCPToolParameter(TEXT("location_offset"), TEXT("object"), TEXT("{x,y,z} added to every position key")),
			FMCPToolParameter(TEXT("rotation_offset"), TEXT("object"), TEXT("{pitch,yaw,roll} pre-multiplied into every rotation key")),
			FMCPToolParameter(TEXT("scale_override"), TEXT("object"), TEXT("{x,y,z} absolute scale replacement on all keys (adjust_track — not additive, replaces existing scale)")),
			FMCPToolParameter(TEXT("target_fps"), TEXT("number"), TEXT("Target frame rate for resample (e.g. 30, 60)")),
			FMCPToolParameter(TEXT("skeleton_path"), TEXT("string"), TEXT("Path to USkeleton or USkeletalMesh (replace_skeleton)")),
			FMCPToolParameter(TEXT("curve_name"), TEXT("string"), TEXT("Curve name (add_curve, remove_curve, set_curve_keys)")),
			FMCPToolParameter(TEXT("keys"), TEXT("array"), TEXT("Array of key objects {time, value, interp_mode?, tangent_mode?, arrive_tangent?, leave_tangent?} (add_curve optional, set_curve_keys required)")),
			FMCPToolParameter(TEXT("mesh_path"), TEXT("string"), TEXT("Path to USkeletalMesh (sync_mesh_bones, rename_bone)")),
			FMCPToolParameter(TEXT("old_name"), TEXT("string"), TEXT("Current bone name to rename (rename_bone)")),
			FMCPToolParameter(TEXT("new_name"), TEXT("string"), TEXT("New bone name (rename_bone)")),
			FMCPToolParameter(TEXT("bone_names"), TEXT("array"), TEXT("Optional: specific bone names to add. If omitted, adds ALL missing bones from skeleton.")),
			FMCPToolParameter(TEXT("position"), TEXT("object"), TEXT("{x,y,z} new ref pose translation for bone (set_ref_pose)")),
			FMCPToolParameter(TEXT("rotation"), TEXT("object"), TEXT("{pitch,yaw,roll} new ref pose rotation for bone (set_ref_pose)")),
			FMCPToolParameter(TEXT("retransform_vertices"), TEXT("boolean"), TEXT("Mesh mode: retransform weighted vertices to preserve shape (default true). Set false to change bind pose without adjusting mesh vertices."), false, TEXT("true")),
			FMCPToolParameter(TEXT("additive_anim_type"), TEXT("string"), TEXT("Additive type: 'None', 'LocalSpaceBase', 'RotationOffsetMeshSpace' (set_additive_type)")),
			FMCPToolParameter(TEXT("base_pose_type"), TEXT("string"), TEXT("Base pose type: 'None', 'RefPose', 'AnimScaled', 'AnimFrame', 'LocalAnimFrame' (set_additive_type)"), false, TEXT("RefPose")),
			FMCPToolParameter(TEXT("ref_pose_seq"), TEXT("string"), TEXT("Base pose animation path — required for AnimScaled/AnimFrame (set_additive_type)")),
			FMCPToolParameter(TEXT("ref_frame_index"), TEXT("number"), TEXT("Frame index for AnimFrame/LocalAnimFrame base pose (set_additive_type)"), false, TEXT("0")),
			FMCPToolParameter(TEXT("scale"), TEXT("object"), TEXT("{x,y,z} or uniform float — scale factor for transform_vertices")),
			FMCPToolParameter(TEXT("translation"), TEXT("object"), TEXT("{x,y,z} translation for transform_vertices")),
			FMCPToolParameter(TEXT("start_frame"), TEXT("number"), TEXT("First frame to keep, 0-indexed inclusive (extract_range)")),
			FMCPToolParameter(TEXT("end_frame"), TEXT("number"), TEXT("Last frame to keep, 0-indexed inclusive; -1 = last frame (extract_range)")),
			FMCPToolParameter(TEXT("dest_path"), TEXT("string"), TEXT("Destination folder for new asset (extract_range, default: same as source)")),
			FMCPToolParameter(TEXT("new_name"), TEXT("string"), TEXT("Name for new asset (extract_range, default: {Source}_F{start}_{end})")),
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
	FMCPToolResult HandleSyncMeshBones(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRenameBone(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetRefPose(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetAdditiveType(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleTransformVertices(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleInspectMesh(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleExtractRange(const TSharedRef<FJsonObject>& Params);
};
