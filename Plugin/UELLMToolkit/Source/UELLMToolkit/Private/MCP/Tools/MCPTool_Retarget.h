// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Animation retargeting — IK Rig, IK Retargeter, FBX import, batch retarget.
 *
 * Skeleton:
 *   - inspect_skeleton: Bone hierarchy from skeleton or skeletal mesh path
 *   - inspect_ref_pose: Per-bone position + rotation (euler + quat) from skeletal mesh bind pose
 *   - list_skeletons: Find skeleton assets in a folder
 *   - add_bone: Add a bone to a skeleton (name, parent, position, rotation)
 *
 * IK Rig:
 *   - inspect_ik_rig: Chains, retarget root, mesh, goals/solvers
 *   - create_ik_rig: Create new IK rig with mesh, root, and chains
 *   - add_chain: Add a retarget chain
 *   - remove_chain: Remove a retarget chain
 *
 * Retargeter:
 *   - inspect_retargeter: Ops, chain mappings, FK settings
 *   - create_retargeter: Create retargeter with source/target IK rigs
 *   - setup_ops: Add default ops, clean duplicates, assign rigs, auto-map
 *   - configure_fk: Set FK chain TranslationMode/RotationMode
 *
 * FBX Import:
 *   - import_fbx: Import single FBX animation
 *   - batch_import_fbx: Import all FBX files from a directory
 *
 * Batch Retarget:
 *   - batch_retarget: Retarget anims, auto-set root motion
 *   - set_root_motion: Enable/disable root motion on anim(s)
 *   - find_anims: List animations in a content folder
 *   - save: Save asset to disk
 *
 * Inspection:
 *   - inspect_anim: Animation metadata
 *   - compare_bones: Compare bone presence between source and target skeletons
 *
 * Animation Analysis:
 *   - sample_bones: Raw per-frame bone transforms at specific frames
 *   - diagnose_anim: Automated health check (root motion, pops, quaternion flips)
 */
class FMCPTool_Retarget : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("retarget");
		Info.Description = TEXT(
			"Animation retargeting tool — IK Rig, IK Retargeter, FBX import, batch retarget.\n\n"
			"Skeleton:\n"
			"- 'inspect_skeleton': Bone hierarchy from skeleton or skeletal mesh path\n"
			"- 'inspect_ref_pose': Per-bone position + rotation (euler + quat) from skeletal mesh bind pose (asset_path = skeletal mesh)\n"
			"- 'list_skeletons': Find skeleton assets in a folder\n"
			"- 'add_bone': Add a bone to a skeleton (skeleton_path, bone_name, parent_bone, position {x,y,z}, rotation {x,y,z,w} quat)\n"
			"- 'copy_bone_tracks': Copy bone animation tracks from source to target anim (source_anim_path, target_anim_path, bone_names[])\n\n"
			"IK Rig:\n"
			"- 'inspect_ik_rig': Chains, retarget root, mesh, goals/solvers\n"
			"- 'create_ik_rig': Create new IK rig with mesh, root, and chains\n"
			"- 'add_chain': Add a retarget chain\n"
			"- 'remove_chain': Remove a retarget chain\n\n"
			"Retargeter:\n"
			"- 'inspect_retargeter': Ops, chain mappings, FK settings\n"
			"- 'create_retargeter': Create retargeter with source/target IK rigs\n"
			"- 'setup_ops': Add default ops, clean duplicates, assign rigs, auto-map\n"
			"- 'configure_fk': Set FK chain TranslationMode/RotationMode\n\n"
			"FBX Import:\n"
			"- 'import_fbx': Import single FBX animation\n"
			"- 'batch_import_fbx': Import all FBX files from a directory\n\n"
			"Batch Retarget:\n"
			"- 'batch_retarget': Retarget anims, auto-set root motion\n"
			"- 'set_root_motion': Enable/disable root motion on anim(s)\n"
			"- 'find_anims': List animations in a content folder\n"
			"- 'save': Save asset to disk\n\n"
			"Inspection:\n"
			"- 'inspect_anim': Animation metadata\n"
			"- 'compare_bones': Compare bone presence in source/target skeletons\n\n"
			"Animation Analysis:\n"
			"- 'sample_bones': Raw per-frame bone transforms at specific frames\n"
			"- 'diagnose_anim': Automated health check (root motion, pops, quaternion flips)"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation name"), true),
			// Paths
			FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Asset path (skeleton, IK rig, retargeter, or anim)")),
			FMCPToolParameter(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton asset path")),
			FMCPToolParameter(TEXT("mesh_path"), TEXT("string"), TEXT("Skeletal mesh path")),
			FMCPToolParameter(TEXT("rig_path"), TEXT("string"), TEXT("IK Rig asset path")),
			FMCPToolParameter(TEXT("source_rig_path"), TEXT("string"), TEXT("Source IK Rig path")),
			FMCPToolParameter(TEXT("target_rig_path"), TEXT("string"), TEXT("Target IK Rig path")),
			FMCPToolParameter(TEXT("retargeter_path"), TEXT("string"), TEXT("IK Retargeter asset path")),
			// Creation
			FMCPToolParameter(TEXT("package_path"), TEXT("string"), TEXT("Package path for asset creation")),
			FMCPToolParameter(TEXT("name"), TEXT("string"), TEXT("Asset name for creation")),
			FMCPToolParameter(TEXT("retarget_root"), TEXT("string"), TEXT("Retarget root bone name")),
			// Chains
			FMCPToolParameter(TEXT("chains"), TEXT("array"), TEXT("Array of {name, start_bone, end_bone} for create_ik_rig")),
			FMCPToolParameter(TEXT("bone_name"), TEXT("string"), TEXT("Bone name for add_bone")),
			FMCPToolParameter(TEXT("parent_bone"), TEXT("string"), TEXT("Parent bone name for add_bone")),
			FMCPToolParameter(TEXT("position"), TEXT("object"), TEXT("{x,y,z} local position for add_bone")),
			FMCPToolParameter(TEXT("rotation"), TEXT("object"), TEXT("{x,y,z,w} quaternion rotation for add_bone")),
			FMCPToolParameter(TEXT("chain_name"), TEXT("string"), TEXT("Chain name for add/remove")),
			FMCPToolParameter(TEXT("start_bone"), TEXT("string"), TEXT("Start bone for chain")),
			FMCPToolParameter(TEXT("end_bone"), TEXT("string"), TEXT("End bone for chain")),
			// FK config
			FMCPToolParameter(TEXT("chain_settings"), TEXT("array"),
				TEXT("Array of {chain_name, translation_mode, rotation_mode} for configure_fk")),
			// FBX import
			FMCPToolParameter(TEXT("fbx_path"), TEXT("string"), TEXT("FBX file path on disk")),
			FMCPToolParameter(TEXT("fbx_directory"), TEXT("string"), TEXT("Directory of FBX files")),
			FMCPToolParameter(TEXT("dest_path"), TEXT("string"), TEXT("Content destination path")),
			FMCPToolParameter(TEXT("file_pattern"), TEXT("string"), TEXT("File filter pattern"), false, TEXT("*.fbx")),
			FMCPToolParameter(TEXT("import_mesh"), TEXT("boolean"), TEXT("Import mesh from FBX"), false, TEXT("false")),
			FMCPToolParameter(TEXT("custom_sample_rate"), TEXT("number"), TEXT("Custom sample rate for FBX anim import (0=use default)"), false, TEXT("0")),
			FMCPToolParameter(TEXT("snap_to_frame_boundary"), TEXT("boolean"),
				TEXT("Snap anim length to closest frame boundary on import (for sub-frame-aligned FBX)"),
				false, TEXT("false")),
			// Batch retarget
			FMCPToolParameter(TEXT("anim_paths"), TEXT("array"), TEXT("Array of animation asset paths to retarget")),
			FMCPToolParameter(TEXT("source_mesh_path"), TEXT("string"), TEXT("Source skeletal mesh path")),
			FMCPToolParameter(TEXT("target_mesh_path"), TEXT("string"), TEXT("Target skeletal mesh path")),
			FMCPToolParameter(TEXT("prefix"), TEXT("string"), TEXT("Prefix for retargeted assets"), false, TEXT("RTG_")),
			FMCPToolParameter(TEXT("auto_root_motion"), TEXT("boolean"), TEXT("Auto-enable root motion by filename"), false, TEXT("true")),
			FMCPToolParameter(TEXT("root_motion_pattern"), TEXT("string"),
				TEXT("Pipe-separated patterns for auto root motion detection"), false, TEXT("RootMotion|Attack|Dodge")),
			// Root motion
			FMCPToolParameter(TEXT("enable"), TEXT("boolean"), TEXT("Enable/disable root motion")),
			// Folder
			FMCPToolParameter(TEXT("folder_path"), TEXT("string"), TEXT("Content folder path")),
			FMCPToolParameter(TEXT("recursive"), TEXT("boolean"), TEXT("Search recursively"), false, TEXT("false")),
			// Bone comparison
			FMCPToolParameter(TEXT("source_anim_path"), TEXT("string"), TEXT("Source anim for comparison")),
			FMCPToolParameter(TEXT("target_anim_path"), TEXT("string"), TEXT("Target anim for comparison")),
			FMCPToolParameter(TEXT("bone_names"), TEXT("array"), TEXT("Array of bone name strings to compare")),
			FMCPToolParameter(TEXT("sample_times"), TEXT("array"), TEXT("Array of times to sample"), false, TEXT("[0.0, 0.5]")),
			// Animation analysis
			FMCPToolParameter(TEXT("frames"), TEXT("array"), TEXT("Array of 0-based frame indices for sample_bones")),
			FMCPToolParameter(TEXT("bones"), TEXT("array"), TEXT("Array of bone names to filter (omit for all bones)")),
			FMCPToolParameter(TEXT("pop_threshold"), TEXT("number"), TEXT("Position pop threshold in cm for diagnose_anim"), false, TEXT("50.0")),
			FMCPToolParameter(TEXT("rotation_flip_threshold"), TEXT("number"), TEXT("Quaternion dot product threshold for flip detection"), false, TEXT("0.0"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// Dispatch handlers per operation
	FMCPToolResult HandleInspectSkeleton(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleInspectRefPose(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleListSkeletons(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddBone(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleCopyBoneTracks(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleInspectIKRig(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleCreateIKRig(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddChain(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveChain(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleInspectRetargeter(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleCreateRetargeter(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetupOps(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleConfigureFK(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleImportFBX(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleBatchImportFBX(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleBatchRetarget(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetRootMotion(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleFindAnims(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSave(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleInspectAnim(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleCompareBones(const TSharedRef<FJsonObject>& Params);
	// Animation analysis
	FMCPToolResult HandleSampleBones(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleDiagnoseAnim(const TSharedRef<FJsonObject>& Params);
};
