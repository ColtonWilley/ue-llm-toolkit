// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class USkeleton;
class USkeletalMesh;
class UIKRigDefinition;
class UIKRigController;
class UIKRetargeter;
class UIKRetargeterController;
class UAnimSequence;

/**
 * Utility class for animation retargeting operations.
 * Covers: skeleton inspection, IK Rig create/edit, IK Retargeter create/configure,
 * FBX import, batch retarget, and animation inspection.
 *
 * All methods are static, return JSON, and contain no MCP/JSON dispatch logic.
 * Pitfall auto-fixes from retargeting.md are encoded directly in the methods.
 */
class FRetargetEditor
{
public:
	// ===== Skeleton =====

	/** Inspect bone hierarchy from a skeleton or skeletal mesh path. */
	static TSharedPtr<FJsonObject> InspectSkeleton(const FString& SkeletonOrMeshPath);

	/** Inspect ref pose from a skeletal mesh or skeleton: per-bone position + rotation (euler + quat). */
	static TSharedPtr<FJsonObject> InspectRefPose(const FString& AssetPath, const TArray<FString>& BoneFilter = TArray<FString>());

	/** List skeleton assets in a content folder. */
	static TSharedPtr<FJsonObject> ListSkeletons(const FString& FolderPath);

	/** Add a bone to a skeleton's reference skeleton. */
	static TSharedPtr<FJsonObject> AddBoneToSkeleton(const FString& SkeletonPath,
		const FString& BoneName, const FString& ParentBoneName,
		const FVector& Position, const FQuat& Rotation);

	/** Copy bone tracks from source animation to target animation. */
	static TSharedPtr<FJsonObject> CopyBoneTracks(const FString& SourceAnimPath,
		const FString& TargetAnimPath, const TArray<FString>& BoneNames);

	// ===== IK Rig =====

	/** Inspect an IK Rig: chains, retarget root, mesh, goals/solvers (warns if present). */
	static TSharedPtr<FJsonObject> InspectIKRig(const FString& RigPath);

	/** Create a new IK Rig with mesh, retarget root, and chains. No goals/solvers (pitfall #15). */
	static TSharedPtr<FJsonObject> CreateIKRig(const FString& PackagePath, const FString& RigName,
		const FString& SkeletalMeshPath, const FString& RetargetRoot,
		const TArray<TSharedPtr<FJsonValue>>& Chains);

	/** Add a retarget chain to an existing IK Rig. */
	static TSharedPtr<FJsonObject> AddChain(const FString& RigPath,
		const FString& ChainName, const FString& StartBone, const FString& EndBone);

	/** Remove a retarget chain from an IK Rig. */
	static TSharedPtr<FJsonObject> RemoveChain(const FString& RigPath, const FString& ChainName);

	// ===== Retargeter =====

	/** Inspect a retargeter: ops, chain mappings, FK settings, source/target rigs. */
	static TSharedPtr<FJsonObject> InspectRetargeter(const FString& RetargeterPath);

	/** Create a retargeter with source/target IK rigs, add default ops, clean dupes, auto-map. */
	static TSharedPtr<FJsonObject> CreateRetargeter(const FString& PackagePath, const FString& Name,
		const FString& SourceRigPath, const FString& TargetRigPath);

	/** Re-run add default ops + cleanup dupes + assign IK rigs + auto-map chains. */
	static TSharedPtr<FJsonObject> SetupOps(const FString& RetargeterPath);

	/** Configure FK chain TranslationMode/RotationMode per chain. Uses reliable struct setting. */
	static TSharedPtr<FJsonObject> ConfigureFK(const FString& RetargeterPath,
		const TArray<TSharedPtr<FJsonValue>>& ChainSettings);

	// ===== FBX Import =====

	/** Import a single FBX as animation onto a skeleton. */
	static TSharedPtr<FJsonObject> ImportFBX(const FString& FbxPath, const FString& DestPath,
		const FString& SkeletonPath, bool bImportMesh = false, int32 CustomSampleRate = 0,
		bool bSnapToClosestFrameBoundary = false);

	/** Import all FBX files from a directory. */
	static TSharedPtr<FJsonObject> BatchImportFBX(const FString& FbxDirectory, const FString& DestPath,
		const FString& SkeletonPath, const FString& FilePattern = TEXT("*.fbx"), int32 CustomSampleRate = 0,
		bool bSnapToClosestFrameBoundary = false);

	// ===== Batch Retarget =====

	/** Run DuplicateAndRetarget, auto-move results, auto-set root motion (pitfalls #4, #7). */
	static TSharedPtr<FJsonObject> BatchRetarget(const FString& RetargeterPath,
		const TArray<TSharedPtr<FJsonValue>>& AnimPaths,
		const FString& SourceMeshPath, const FString& TargetMeshPath,
		const FString& Prefix = TEXT("RTG_"),
		bool bAutoRootMotion = true, const FString& RootMotionPattern = TEXT("RootMotion|Attack|Dodge"));

	/** Enable/disable root motion on one or more animation assets. */
	static TSharedPtr<FJsonObject> SetRootMotion(const FString& AnimPath, bool bEnable);

	/** List animations in a content folder. */
	static TSharedPtr<FJsonObject> FindAnims(const FString& FolderPath, bool bRecursive = false);

	/** Save an asset to disk. */
	static TSharedPtr<FJsonObject> SaveAsset(const FString& AssetPath);

	// ===== Inspection =====

	/** Get animation metadata: length, frame count, root motion, skeleton. */
	static TSharedPtr<FJsonObject> InspectAnim(const FString& AnimPath);

	/** Compare bone poses between source and retargeted anims at sample times. */
	static TSharedPtr<FJsonObject> CompareBones(const FString& SourceAnimPath,
		const FString& TargetAnimPath, const TArray<FString>& BoneNames,
		const TArray<float>& SampleTimes);

	// ===== Animation Analysis =====

	/** Sample raw bone transforms at specific frames. */
	static TSharedPtr<FJsonObject> SampleBones(const FString& AnimPath,
		const TArray<int32>& Frames, const TArray<FString>& BoneNames);

	/** Automated health check: root motion, pops, quaternion flips. */
	static TSharedPtr<FJsonObject> DiagnoseAnim(const FString& AnimPath,
		float PopThreshold = 50.0f, float RotationFlipDotThreshold = 0.0f);

private:
	static TSharedPtr<FJsonObject> SuccessResult(const FString& Message);
	static TSharedPtr<FJsonObject> ErrorResult(const FString& Message);

	// Asset loading helpers
	static USkeleton* LoadSkeletonFromPath(const FString& Path, FString& OutError);
	static USkeletalMesh* LoadSkeletalMeshFromPath(const FString& Path, FString& OutError);
	static UIKRigDefinition* LoadIKRig(const FString& Path, FString& OutError);
	static UIKRetargeter* LoadRetargeter(const FString& Path, FString& OutError);
	static UAnimSequence* LoadAnimSequence(const FString& Path, FString& OutError);

	// Pitfall #9: Remove duplicate ops created by AddDefaultOps
	static void RemoveDuplicateOps(UIKRetargeterController* Controller);

	// Pitfall #4: Check if anim name matches root motion pattern
	static bool IsRootMotionAnim(const FString& AssetName, const FString& Pattern);
};
