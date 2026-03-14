// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_AnimEdit.h"
#include "AnimTrackEditor.h"
#include "SkeletonModifier.h"
#include "SkeletalMeshOperations.h"
#include "StaticMeshOperations.h"
#include "Engine/SkeletalMesh.h"
#include "EditorAssetLibrary.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

namespace AnimEditSkeletonAccess
{
	struct FHelper : public USkeleton
	{
		using USkeleton::HandleSkeletonHierarchyChange;
	};
}

static FMCPToolResult AnimEditJsonToToolResult(const TSharedPtr<FJsonObject>& Result, const FString& SuccessContext)
{
	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);

	if (bSuccess)
	{
		FString Message;
		Result->TryGetStringField(TEXT("message"), Message);
		if (Message.IsEmpty())
		{
			Message = SuccessContext;
		}
		return FMCPToolResult::Success(Message, Result);
	}
	else
	{
		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		return FMCPToolResult::Error(Error.IsEmpty() ? TEXT("Unknown error") : Error);
	}
}

FMCPToolResult FMCPTool_AnimEdit::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("adjust_track"))
	{
		return HandleAdjustTrack(Params);
	}
	else if (Operation == TEXT("inspect_track"))
	{
		return HandleInspectTrack(Params);
	}
	else if (Operation == TEXT("resample"))
	{
		return HandleResample(Params);
	}
	else if (Operation == TEXT("replace_skeleton"))
	{
		return HandleReplaceSkeleton(Params);
	}
	else if (Operation == TEXT("sync_mesh_bones"))
	{
		return HandleSyncMeshBones(Params);
	}
	else if (Operation == TEXT("rename_bone"))
	{
		return HandleRenameBone(Params);
	}
	else if (Operation == TEXT("set_ref_pose"))
	{
		return HandleSetRefPose(Params);
	}
	else if (Operation == TEXT("set_additive_type"))
	{
		return HandleSetAdditiveType(Params);
	}
	else if (Operation == TEXT("transform_vertices"))
	{
		return HandleTransformVertices(Params);
	}
	else if (Operation == TEXT("inspect_mesh"))
	{
		return HandleInspectMesh(Params);
	}
	else if (Operation == TEXT("extract_range"))
	{
		return HandleExtractRange(Params);
	}

	return FMCPToolResult::Error(FString::Printf(TEXT("Unknown operation: %s"), *Operation));
}

FMCPToolResult FMCPTool_AnimEdit::HandleAdjustTrack(const TSharedRef<FJsonObject>& Params)
{
	FString BoneName;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("bone_name"), BoneName, Error))
	{
		return Error.GetValue();
	}

	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array)"));
	}

	TArray<FString> AssetPaths;
	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString Path;
		if (Val.IsValid() && Val->TryGetString(Path) && !Path.IsEmpty())
		{
			AssetPaths.Add(Path);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("asset_paths contains no valid paths"));
	}

	FVector LocationOffset = ExtractVectorParam(Params, TEXT("location_offset"));
	FRotator RotationOffset = ExtractRotatorParam(Params, TEXT("rotation_offset"));
	bool bHasScaleOverride = Params->HasField(TEXT("scale_override"));
	FVector ScaleOverride = bHasScaleOverride ? ExtractVectorParam(Params, TEXT("scale_override")) : FVector::OneVector;

	if (LocationOffset.IsNearlyZero() && RotationOffset.IsNearlyZero() && !bHasScaleOverride)
	{
		return FMCPToolResult::Error(TEXT("At least one of location_offset, rotation_offset, or scale_override must be provided"));
	}

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::AdjustTrack(AssetPaths, BoneName, LocationOffset, RotationOffset, ScaleOverride, bHasScaleOverride, bSave);
	return AnimEditJsonToToolResult(Result, TEXT("Batch adjust complete"));
}

FMCPToolResult FMCPTool_AnimEdit::HandleInspectTrack(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	FString BoneName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("bone_name"), BoneName, Error))
	{
		return Error.GetValue();
	}

	TArray<int32> SampleFrames;
	const TArray<TSharedPtr<FJsonValue>>* FramesArray;
	if (Params->TryGetArrayField(TEXT("sample_frames"), FramesArray) && FramesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *FramesArray)
		{
			double NumVal;
			if (Val.IsValid() && Val->TryGetNumber(NumVal))
			{
				SampleFrames.Add(static_cast<int32>(NumVal));
			}
		}
	}

	if (SampleFrames.Num() == 0)
	{
		SampleFrames.Add(0);
		SampleFrames.Add(-1);
	}

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::InspectTrack(AssetPath, BoneName, SampleFrames);
	return AnimEditJsonToToolResult(Result, TEXT("Inspect complete"));
}

FMCPToolResult FMCPTool_AnimEdit::HandleResample(const TSharedRef<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array)"));
	}

	TArray<FString> AssetPaths;
	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString Path;
		if (Val.IsValid() && Val->TryGetString(Path) && !Path.IsEmpty())
		{
			AssetPaths.Add(Path);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("asset_paths contains no valid paths"));
	}

	double TargetFPSDouble = 0;
	if (!Params->TryGetNumberField(TEXT("target_fps"), TargetFPSDouble) || TargetFPSDouble <= 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: target_fps (number > 0)"));
	}
	int32 TargetFPS = static_cast<int32>(TargetFPSDouble);

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::Resample(AssetPaths, TargetFPS, bSave);
	return AnimEditJsonToToolResult(Result, TEXT("Resample complete"));
}

FMCPToolResult FMCPTool_AnimEdit::HandleReplaceSkeleton(const TSharedRef<FJsonObject>& Params)
{
	FString SkeletonPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Error))
	{
		return Error.GetValue();
	}

	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray) || !AssetPathsArray || AssetPathsArray->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: asset_paths (non-empty array)"));
	}

	TArray<FString> AssetPaths;
	for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
	{
		FString Path;
		if (Val.IsValid() && Val->TryGetString(Path) && !Path.IsEmpty())
		{
			AssetPaths.Add(Path);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("asset_paths contains no valid paths"));
	}

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::ReplaceSkeleton(AssetPaths, SkeletonPath, bSave);
	return AnimEditJsonToToolResult(Result, TEXT("Replace skeleton complete"));
}

FMCPToolResult FMCPTool_AnimEdit::HandleSyncMeshBones(const TSharedRef<FJsonObject>& Params)
{
	FString MeshPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Error))
	{
		return Error.GetValue();
	}

	// Load the skeletal mesh
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Mesh)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load USkeletalMesh: %s"), *MeshPath));
	}

	USkeleton* Skeleton = Mesh->GetSkeleton();
	if (!Skeleton)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Mesh has no USkeleton: %s"), *MeshPath));
	}

	const FReferenceSkeleton& SkelRefSkel = Skeleton->GetReferenceSkeleton();
	const FReferenceSkeleton& MeshRefSkel = Mesh->GetRefSkeleton();

	// Collect optional bone_names filter
	TSet<FName> FilterBones;
	const TArray<TSharedPtr<FJsonValue>>* BoneNamesArray;
	if (Params->TryGetArrayField(TEXT("bone_names"), BoneNamesArray) && BoneNamesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *BoneNamesArray)
		{
			FString Name;
			if (Val.IsValid() && Val->TryGetString(Name) && !Name.IsEmpty())
			{
				FilterBones.Add(FName(*Name));
			}
		}
	}

	// Find missing bones — sorted by skeleton index (parents before children)
	TArray<int32> MissingBoneIndices;
	for (int32 i = 0; i < SkelRefSkel.GetRawBoneNum(); ++i)
	{
		FName BoneName = SkelRefSkel.GetBoneName(i);
		if (MeshRefSkel.FindBoneIndex(BoneName) == INDEX_NONE)
		{
			if (FilterBones.Num() == 0 || FilterBones.Contains(BoneName))
			{
				MissingBoneIndices.Add(i);
			}
		}
	}

	if (MissingBoneIndices.Num() == 0)
	{
		return FMCPToolResult::Success(TEXT("All requested bones already exist in mesh — nothing to add."));
	}

	// Verify filtered bones exist in skeleton
	if (FilterBones.Num() > 0)
	{
		for (const FName& Name : FilterBones)
		{
			if (SkelRefSkel.FindBoneIndex(Name) == INDEX_NONE)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Bone '%s' not found in USkeleton. Cannot add a bone that doesn't exist in the skeleton."),
					*Name.ToString()));
			}
		}
	}

	// Use USkeletonModifier to add missing bones
	USkeletonModifier* Modifier = NewObject<USkeletonModifier>();
	Modifier->SetSkeletalMesh(Mesh);

	TArray<FString> AddedBones;
	for (int32 SkelBoneIdx : MissingBoneIndices)
	{
		FName BoneName = SkelRefSkel.GetBoneName(SkelBoneIdx);
		FTransform RefPose = SkelRefSkel.GetRefBonePose()[SkelBoneIdx];

		FName ParentName = NAME_None;
		int32 ParentIdx = SkelRefSkel.GetParentIndex(SkelBoneIdx);
		if (ParentIdx != INDEX_NONE)
		{
			ParentName = SkelRefSkel.GetBoneName(ParentIdx);
		}

		bool bAdded = Modifier->AddBone(BoneName, ParentName, RefPose);
		if (!bAdded)
		{
			UE_LOG(LogTemp, Warning, TEXT("SyncMeshBones: AddBone failed for '%s' (parent='%s')"),
				*BoneName.ToString(), *ParentName.ToString());
			continue;
		}
		AddedBones.Add(BoneName.ToString());
	}

	if (AddedBones.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("All AddBone calls failed — no bones were added."));
	}

	// Commit changes to the skeletal mesh
	bool bCommitted = Modifier->CommitSkeletonToSkeletalMesh();
	if (!bCommitted)
	{
		return FMCPToolResult::Error(TEXT("CommitSkeletonToSkeletalMesh failed."));
	}

	// Save
	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);
	if (bSave)
	{
		Mesh->MarkPackageDirty();
		UEditorAssetLibrary::SaveLoadedAsset(Mesh);
	}

	// Build result
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetNumberField(TEXT("bones_added"), AddedBones.Num());

	TArray<TSharedPtr<FJsonValue>> BoneArray;
	for (const FString& Name : AddedBones)
	{
		BoneArray.Add(MakeShared<FJsonValueString>(Name));
	}
	ResultObj->SetArrayField(TEXT("added_bones"), BoneArray);
	ResultObj->SetNumberField(TEXT("mesh_bone_count_after"), Mesh->GetRefSkeleton().GetRawBoneNum());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added %d bones to %s"), AddedBones.Num(), *MeshPath),
		ResultObj);
}

FMCPToolResult FMCPTool_AnimEdit::HandleRenameBone(const TSharedRef<FJsonObject>& Params)
{
	FString MeshPath, OldName, NewName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("old_name"), OldName, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("new_name"), NewName, Error))
	{
		return Error.GetValue();
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Mesh)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load USkeletalMesh: %s"), *MeshPath));
	}

	USkeleton* Skeleton = Mesh->GetSkeleton();
	if (!Skeleton)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Mesh has no USkeleton: %s"), *MeshPath));
	}

	FName OldFName(*OldName);
	FName NewFName(*NewName);

	const FReferenceSkeleton& SkelRefSkel = Skeleton->GetReferenceSkeleton();
	int32 SkelOldIdx = SkelRefSkel.FindBoneIndex(OldFName);
	int32 SkelNewIdx = SkelRefSkel.FindBoneIndex(NewFName);

	bool bMeshOnly = false;
	if (SkelOldIdx != INDEX_NONE)
	{
		if (SkelNewIdx != INDEX_NONE)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Both '%s' and '%s' already exist in skeleton"), *OldName, *NewName));
		}
	}
	else if (SkelNewIdx != INDEX_NONE)
	{
		bMeshOnly = true;
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Bone '%s' not found in skeleton (under old or new name)"), *OldName));
	}

	FReferenceSkeleton& MeshRefSkel = Mesh->GetRefSkeleton();
	if (MeshRefSkel.FindBoneIndex(OldFName) == INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Bone '%s' not found in mesh"), *OldName));
	}
	if (MeshRefSkel.FindBoneIndex(NewFName) != INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Bone '%s' already exists in mesh"), *NewName));
	}

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	if (bMeshOnly)
	{
		{
			FReferenceSkeletonModifier Modifier(MeshRefSkel, Skeleton);
			Modifier.Rename(OldFName, NewFName);
		}
		Skeleton->ClearCacheData();
		Mesh->PostEditChange();

		if (bSave)
		{
			Mesh->MarkPackageDirty();
			UEditorAssetLibrary::SaveLoadedAsset(Mesh);
		}
	}
	else
	{
		{
			FReferenceSkeletonModifier Modifier(Skeleton);
			Modifier.Rename(OldFName, NewFName);
		}
		{
			FReferenceSkeletonModifier Modifier(MeshRefSkel, Skeleton);
			Modifier.Rename(OldFName, NewFName);
		}
		auto* Helper = static_cast<AnimEditSkeletonAccess::FHelper*>(Skeleton);
		Helper->HandleSkeletonHierarchyChange();

		if (bSave)
		{
			Skeleton->MarkPackageDirty();
			Mesh->MarkPackageDirty();
			UEditorAssetLibrary::SaveLoadedAsset(Skeleton);
			UEditorAssetLibrary::SaveLoadedAsset(Mesh);
		}
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("mode"), bMeshOnly ? TEXT("mesh_only") : TEXT("full"));
	ResultObj->SetStringField(TEXT("old_name"), OldName);
	ResultObj->SetStringField(TEXT("new_name"), NewName);
	ResultObj->SetStringField(TEXT("mesh"), MeshPath);
	ResultObj->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Renamed bone '%s' -> '%s' (%s)"), *OldName, *NewName, bMeshOnly ? TEXT("mesh only — skeleton already had new name") : TEXT("skeleton and mesh")),
		ResultObj);
}

FMCPToolResult FMCPTool_AnimEdit::HandleSetRefPose(const TSharedRef<FJsonObject>& Params)
{
	FString MeshPath, BoneName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("bone_name"), BoneName, Error))
	{
		return Error.GetValue();
	}

	bool bHasPosition = HasVectorParam(Params, TEXT("position"));
	bool bHasRotation = HasVectorParam(Params, TEXT("rotation"));

	if (!bHasPosition && !bHasRotation)
	{
		return FMCPToolResult::Error(TEXT("At least one of 'position' {x,y,z} or 'rotation' {pitch,yaw,roll} must be provided"));
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	USkeleton* Skeleton = nullptr;
	bool bSkeletonOnly = false;

	if (Mesh)
	{
		Skeleton = Mesh->GetSkeleton();
		if (!Skeleton)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Mesh has no USkeleton: %s"), *MeshPath));
		}
	}
	else
	{
		Skeleton = LoadObject<USkeleton>(nullptr, *MeshPath);
		if (!Skeleton)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load as USkeletalMesh or USkeleton: %s"), *MeshPath));
		}
		bSkeletonOnly = true;
	}

	FReferenceSkeleton& RefSkel = bSkeletonOnly
		? const_cast<FReferenceSkeleton&>(Skeleton->GetReferenceSkeleton())
		: Mesh->GetRefSkeleton();
	FName BoneFName(*BoneName);
	int32 BoneIndex = RefSkel.FindBoneIndex(BoneFName);
	if (BoneIndex == INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Bone '%s' not found in %s"), *BoneName, bSkeletonOnly ? TEXT("skeleton") : TEXT("mesh")));
	}

	const FTransform OldTransform = RefSkel.GetRefBonePose()[BoneIndex];

	// Build new transform — preserve whatever wasn't provided
	FTransform NewTransform = OldTransform;
	if (bHasPosition)
	{
		NewTransform.SetTranslation(ExtractVectorParam(Params, TEXT("position")));
	}
	if (bHasRotation)
	{
		FRotator NewRot = ExtractRotatorParam(Params, TEXT("rotation"));
		NewTransform.SetRotation(FQuat(NewRot));
	}

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);
	bool bRetransformVertices = ExtractOptionalBool(Params, TEXT("retransform_vertices"), true);

	if (bSkeletonOnly)
	{
		// Skeleton-only path: update transform, propagate to dependent assets, save
		{
			FReferenceSkeletonModifier Modifier(Skeleton);
			Modifier.UpdateRefPoseTransform(BoneIndex, NewTransform);
		}

		auto* Helper = static_cast<AnimEditSkeletonAccess::FHelper*>(Skeleton);
		Helper->HandleSkeletonHierarchyChange();

		if (bSave)
		{
			Skeleton->MarkPackageDirty();
			UEditorAssetLibrary::SaveLoadedAsset(Skeleton);
		}
	}
	else
	{
		// Mesh path: full "Apply Pose as Rest Pose" — retransform vertices + rebuild inverse bind matrices
		{
			FReferenceSkeletonModifier Modifier(RefSkel, Skeleton);
			Modifier.UpdateRefPoseTransform(BoneIndex, NewTransform);
		}

		if (bRetransformVertices)
		{
			TArray<FTransform> NewRefPoseCS;
			RefSkel.GetBoneAbsoluteTransforms(NewRefPoseCS);

			int32 NumLODs = Mesh->GetLODNum();
			int32 LODsRetransformed = 0;

			for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
			{
				if (!Mesh->HasMeshDescription(LODIndex))
				{
					continue;
				}

				Mesh->ModifyMeshDescription(LODIndex, false);
				FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LODIndex);
				if (!MeshDesc)
				{
					continue;
				}

				bool bOk = FSkeletalMeshOperations::GetPosedMeshInPlace(
					*MeshDesc,
					NewRefPoseCS,
					NAME_None,
					{},
					false,
					true
				);

				if (bOk)
				{
					Mesh->CommitMeshDescription(LODIndex, USkeletalMesh::FCommitMeshDescriptionParams{});
					++LODsRetransformed;
				}
			}

			UE_LOG(LogTemp, Log, TEXT("set_ref_pose: Retransformed vertices in %d/%d LODs"), LODsRetransformed, NumLODs);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("set_ref_pose: Skipped vertex retransformation (retransform_vertices=false)"));
		}

		// Force-rebuild inverse bind matrices
		Mesh->GetRefBasesInvMatrix().Empty();
		Mesh->CalculateInvRefMatrices();

		Mesh->PostEditChange();

		if (bSave)
		{
			Mesh->MarkPackageDirty();
			UEditorAssetLibrary::SaveLoadedAsset(Mesh);
		}
	}

	// Build result JSON
	auto TransformToJson = [](const FTransform& T) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		FVector Pos = T.GetTranslation();
		FRotator Rot = T.Rotator();
		TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
		PosObj->SetNumberField(TEXT("x"), FMath::RoundToFloat(Pos.X * 1000.0f) / 1000.0f);
		PosObj->SetNumberField(TEXT("y"), FMath::RoundToFloat(Pos.Y * 1000.0f) / 1000.0f);
		PosObj->SetNumberField(TEXT("z"), FMath::RoundToFloat(Pos.Z * 1000.0f) / 1000.0f);
		TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("pitch"), FMath::RoundToFloat(Rot.Pitch * 1000.0f) / 1000.0f);
		RotObj->SetNumberField(TEXT("yaw"), FMath::RoundToFloat(Rot.Yaw * 1000.0f) / 1000.0f);
		RotObj->SetNumberField(TEXT("roll"), FMath::RoundToFloat(Rot.Roll * 1000.0f) / 1000.0f);
		Obj->SetObjectField(TEXT("position"), PosObj);
		Obj->SetObjectField(TEXT("rotation"), RotObj);
		return Obj;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("mode"), bSkeletonOnly ? TEXT("skeleton_only") : TEXT("mesh"));
	ResultObj->SetStringField(bSkeletonOnly ? TEXT("skeleton_path") : TEXT("mesh_path"), MeshPath);
	ResultObj->SetStringField(TEXT("bone_name"), BoneName);
	if (!bSkeletonOnly)
	{
		ResultObj->SetBoolField(TEXT("retransform_vertices"), bRetransformVertices);
	}
	ResultObj->SetObjectField(TEXT("old_transform"), TransformToJson(OldTransform));
	ResultObj->SetObjectField(TEXT("new_transform"), TransformToJson(NewTransform));

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Updated ref pose for bone '%s' in %s (%s)"), *BoneName, *MeshPath, bSkeletonOnly ? TEXT("skeleton only") : TEXT("mesh")),
		ResultObj);
}

FMCPToolResult FMCPTool_AnimEdit::HandleSetAdditiveType(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString AdditiveTypeStr;
	if (!ExtractRequiredString(Params, TEXT("additive_anim_type"), AdditiveTypeStr, Error))
	{
		return Error.GetValue();
	}

	UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *AssetPath);
	if (!Anim)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimSequence: %s"), *AssetPath));
	}

	EAdditiveAnimationType NewAdditiveType;
	FString AdditiveTypeLower = AdditiveTypeStr.ToLower();
	if (AdditiveTypeLower == TEXT("none"))
	{
		NewAdditiveType = AAT_None;
	}
	else if (AdditiveTypeLower == TEXT("localspacebase"))
	{
		NewAdditiveType = AAT_LocalSpaceBase;
	}
	else if (AdditiveTypeLower == TEXT("rotationoffsetmeshspace"))
	{
		NewAdditiveType = AAT_RotationOffsetMeshSpace;
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown additive_anim_type: '%s'. Valid: None, LocalSpaceBase, RotationOffsetMeshSpace"), *AdditiveTypeStr));
	}

	FString BasePoseTypeStr = ExtractOptionalString(Params, TEXT("base_pose_type"), TEXT("RefPose"));
	EAdditiveBasePoseType NewBasePoseType;
	FString BasePoseLower = BasePoseTypeStr.ToLower();
	if (BasePoseLower == TEXT("none"))
	{
		NewBasePoseType = ABPT_None;
	}
	else if (BasePoseLower == TEXT("refpose"))
	{
		NewBasePoseType = ABPT_RefPose;
	}
	else if (BasePoseLower == TEXT("animscaled"))
	{
		NewBasePoseType = ABPT_AnimScaled;
	}
	else if (BasePoseLower == TEXT("animframe"))
	{
		NewBasePoseType = ABPT_AnimFrame;
	}
	else if (BasePoseLower == TEXT("localanimframe"))
	{
		NewBasePoseType = ABPT_LocalAnimFrame;
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown base_pose_type: '%s'. Valid: None, RefPose, AnimScaled, AnimFrame, LocalAnimFrame"), *BasePoseTypeStr));
	}

	UAnimSequence* RefPoseSeq = nullptr;
	FString RefPoseSeqPath;
	Params->TryGetStringField(TEXT("ref_pose_seq"), RefPoseSeqPath);

	if (NewBasePoseType == ABPT_AnimScaled || NewBasePoseType == ABPT_AnimFrame)
	{
		if (RefPoseSeqPath.IsEmpty())
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("base_pose_type '%s' requires ref_pose_seq (base pose animation path)"), *BasePoseTypeStr));
		}
	}

	if (!RefPoseSeqPath.IsEmpty())
	{
		RefPoseSeq = LoadObject<UAnimSequence>(nullptr, *RefPoseSeqPath);
		if (!RefPoseSeq)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load ref_pose_seq: %s"), *RefPoseSeqPath));
		}
		if (Anim->GetSkeleton() && RefPoseSeq->GetSkeleton() && Anim->GetSkeleton() != RefPoseSeq->GetSkeleton())
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Skeleton mismatch: anim uses '%s' but ref_pose_seq uses '%s'"),
				*Anim->GetSkeleton()->GetPathName(), *RefPoseSeq->GetSkeleton()->GetPathName()));
		}
	}

	int32 RefFrameIndex = ExtractOptionalNumber<int32>(Params, TEXT("ref_frame_index"), 0);

	auto AdditiveTypeToString = [](EAdditiveAnimationType T) -> FString
	{
		switch (T)
		{
		case AAT_None: return TEXT("None");
		case AAT_LocalSpaceBase: return TEXT("LocalSpaceBase");
		case AAT_RotationOffsetMeshSpace: return TEXT("RotationOffsetMeshSpace");
		default: return TEXT("Unknown");
		}
	};
	auto BasePoseTypeToString = [](EAdditiveBasePoseType T) -> FString
	{
		switch (T)
		{
		case ABPT_None: return TEXT("None");
		case ABPT_RefPose: return TEXT("RefPose");
		case ABPT_AnimScaled: return TEXT("AnimScaled");
		case ABPT_AnimFrame: return TEXT("AnimFrame");
		case ABPT_LocalAnimFrame: return TEXT("LocalAnimFrame");
		default: return TEXT("Unknown");
		}
	};

	TSharedPtr<FJsonObject> OldValues = MakeShared<FJsonObject>();
	OldValues->SetStringField(TEXT("additive_anim_type"), AdditiveTypeToString(Anim->AdditiveAnimType));
	OldValues->SetStringField(TEXT("base_pose_type"), BasePoseTypeToString(Anim->RefPoseType));
	if (Anim->RefPoseSeq)
	{
		OldValues->SetStringField(TEXT("ref_pose_seq"), Anim->RefPoseSeq->GetPathName());
	}
	OldValues->SetNumberField(TEXT("ref_frame_index"), Anim->RefFrameIndex);

	Anim->AdditiveAnimType = NewAdditiveType;
	Anim->RefPoseType = NewBasePoseType;
	Anim->RefPoseSeq = RefPoseSeq;
	Anim->RefFrameIndex = RefFrameIndex;

	FProperty* Prop = UAnimSequence::StaticClass()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType));
	FPropertyChangedEvent Evt(Prop);
	Anim->PostEditChangeProperty(Evt);

	Anim->MarkPackageDirty();

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);
	if (bSave)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Anim);
	}

	TSharedPtr<FJsonObject> NewValues = MakeShared<FJsonObject>();
	NewValues->SetStringField(TEXT("additive_anim_type"), AdditiveTypeToString(NewAdditiveType));
	NewValues->SetStringField(TEXT("base_pose_type"), BasePoseTypeToString(NewBasePoseType));
	if (RefPoseSeq)
	{
		NewValues->SetStringField(TEXT("ref_pose_seq"), RefPoseSeq->GetPathName());
	}
	NewValues->SetNumberField(TEXT("ref_frame_index"), RefFrameIndex);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("asset_path"), AssetPath);
	ResultObj->SetObjectField(TEXT("old"), OldValues);
	ResultObj->SetObjectField(TEXT("new"), NewValues);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set additive type on %s: %s (base: %s)"),
			*Anim->GetName(), *AdditiveTypeToString(NewAdditiveType), *BasePoseTypeToString(NewBasePoseType)),
		ResultObj);
}

FMCPToolResult FMCPTool_AnimEdit::HandleTransformVertices(const TSharedRef<FJsonObject>& Params)
{
	FString MeshPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Error))
	{
		return Error.GetValue();
	}

	bool bHasRotation = HasVectorParam(Params, TEXT("rotation"));
	bool bHasScale = Params->HasField(TEXT("scale"));
	bool bHasTranslation = HasVectorParam(Params, TEXT("translation"));

	if (!bHasRotation && !bHasScale && !bHasTranslation)
	{
		return FMCPToolResult::Error(TEXT("At least one of 'rotation' {pitch,yaw,roll}, 'scale' {x,y,z}/float, or 'translation' {x,y,z} must be provided"));
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Mesh)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load USkeletalMesh: %s"), *MeshPath));
	}

	FTransform UserTransform;

	if (bHasRotation)
	{
		FRotator Rot = ExtractRotatorParam(Params, TEXT("rotation"));
		UserTransform.SetRotation(FQuat(Rot));
	}

	if (bHasScale)
	{
		const TSharedPtr<FJsonObject>* ScaleObj;
		if (Params->TryGetObjectField(TEXT("scale"), ScaleObj))
		{
			FVector S;
			S.X = (*ScaleObj)->GetNumberField(TEXT("x"));
			S.Y = (*ScaleObj)->GetNumberField(TEXT("y"));
			S.Z = (*ScaleObj)->GetNumberField(TEXT("z"));
			UserTransform.SetScale3D(S);
		}
		else
		{
			double Uniform = Params->GetNumberField(TEXT("scale"));
			UserTransform.SetScale3D(FVector(Uniform));
		}
	}

	if (bHasTranslation)
	{
		UserTransform.SetTranslation(ExtractVectorParam(Params, TEXT("translation")));
	}

	int32 NumLODs = Mesh->GetLODNum();
	int32 LODsTransformed = 0;

	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		if (!Mesh->HasMeshDescription(LODIndex))
		{
			continue;
		}

		Mesh->ModifyMeshDescription(LODIndex, false);
		FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LODIndex);
		if (!MeshDesc)
		{
			continue;
		}

		FStaticMeshOperations::ApplyTransform(*MeshDesc, UserTransform, true);
		Mesh->CommitMeshDescription(LODIndex, USkeletalMesh::FCommitMeshDescriptionParams{});
		++LODsTransformed;
	}

	if (LODsTransformed == 0)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("No LODs with MeshDescription found in %s — cannot transform vertices"), *MeshPath));
	}

	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);
	if (bSave)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Mesh);
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("mesh_path"), MeshPath);
	ResultObj->SetNumberField(TEXT("lods_transformed"), LODsTransformed);
	ResultObj->SetNumberField(TEXT("total_lods"), NumLODs);

	TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();
	FRotator R = UserTransform.Rotator();
	FVector S = UserTransform.GetScale3D();
	FVector T = UserTransform.GetTranslation();
	TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
	RotObj->SetNumberField(TEXT("pitch"), R.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), R.Yaw);
	RotObj->SetNumberField(TEXT("roll"), R.Roll);
	TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
	ScaleObj->SetNumberField(TEXT("x"), S.X);
	ScaleObj->SetNumberField(TEXT("y"), S.Y);
	ScaleObj->SetNumberField(TEXT("z"), S.Z);
	TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
	TransObj->SetNumberField(TEXT("x"), T.X);
	TransObj->SetNumberField(TEXT("y"), T.Y);
	TransObj->SetNumberField(TEXT("z"), T.Z);
	TransformObj->SetObjectField(TEXT("rotation"), RotObj);
	TransformObj->SetObjectField(TEXT("scale"), ScaleObj);
	TransformObj->SetObjectField(TEXT("translation"), TransObj);
	ResultObj->SetObjectField(TEXT("applied_transform"), TransformObj);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Transformed vertices in %d/%d LODs of %s"), LODsTransformed, NumLODs, *MeshPath),
		ResultObj);
}

FMCPToolResult FMCPTool_AnimEdit::HandleInspectMesh(const TSharedRef<FJsonObject>& Params)
{
	FString MeshPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Error))
	{
		return Error.GetValue();
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Mesh)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load USkeletalMesh: %s"), *MeshPath));
	}

	USkeleton* Skeleton = Mesh->GetSkeleton();
	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
	int32 NumBones = RefSkel.GetNum();

	auto R3 = [](double V) { return FMath::RoundToFloat(V * 1000.0f) / 1000.0f; };

	TArray<FTransform> CSTransforms;
	RefSkel.GetBoneAbsoluteTransforms(CSTransforms);

	const auto& InvBindMatrices = Mesh->GetRefBasesInvMatrix();

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("mesh_path"), MeshPath);
	ResultObj->SetStringField(TEXT("skeleton"), Skeleton ? Skeleton->GetPathName() : TEXT("none"));
	ResultObj->SetNumberField(TEXT("num_bones"), NumBones);

	TArray<TSharedPtr<FJsonValue>> BonesArray;
	for (int32 i = 0; i < NumBones; ++i)
	{
		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetStringField(TEXT("name"), RefSkel.GetBoneName(i).ToString());
		BoneObj->SetNumberField(TEXT("index"), i);
		int32 ParentIdx = RefSkel.GetParentIndex(i);
		BoneObj->SetNumberField(TEXT("parent_index"), ParentIdx);
		if (ParentIdx != INDEX_NONE)
		{
			BoneObj->SetStringField(TEXT("parent_name"), RefSkel.GetBoneName(ParentIdx).ToString());
		}

		const FTransform& Local = RefSkel.GetRefBonePose()[i];
		TSharedPtr<FJsonObject> LocalObj = MakeShared<FJsonObject>();
		FVector LP = Local.GetTranslation();
		FRotator LR = Local.Rotator();
		FVector LS = Local.GetScale3D();
		LocalObj->SetStringField(TEXT("pos"), FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), LP.X, LP.Y, LP.Z));
		LocalObj->SetStringField(TEXT("rot"), FString::Printf(TEXT("(P=%.3f, Y=%.3f, R=%.3f)"), LR.Pitch, LR.Yaw, LR.Roll));
		LocalObj->SetStringField(TEXT("scale"), FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), LS.X, LS.Y, LS.Z));
		BoneObj->SetObjectField(TEXT("bind_local"), LocalObj);

		if (i < CSTransforms.Num())
		{
			const FTransform& CS = CSTransforms[i];
			TSharedPtr<FJsonObject> CSObj = MakeShared<FJsonObject>();
			FVector CP = CS.GetTranslation();
			FRotator CR = CS.Rotator();
			CSObj->SetStringField(TEXT("pos"), FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), CP.X, CP.Y, CP.Z));
			CSObj->SetStringField(TEXT("rot"), FString::Printf(TEXT("(P=%.3f, Y=%.3f, R=%.3f)"), CR.Pitch, CR.Yaw, CR.Roll));
			BoneObj->SetObjectField(TEXT("bind_cs"), CSObj);
		}

		if (i < InvBindMatrices.Num())
		{
			const auto& M = InvBindMatrices[i];
			TSharedPtr<FJsonObject> InvObj = MakeShared<FJsonObject>();
			InvObj->SetStringField(TEXT("row0"), FString::Printf(TEXT("(%.4f, %.4f, %.4f, %.4f)"), M.M[0][0], M.M[0][1], M.M[0][2], M.M[0][3]));
			InvObj->SetStringField(TEXT("row1"), FString::Printf(TEXT("(%.4f, %.4f, %.4f, %.4f)"), M.M[1][0], M.M[1][1], M.M[1][2], M.M[1][3]));
			InvObj->SetStringField(TEXT("row2"), FString::Printf(TEXT("(%.4f, %.4f, %.4f, %.4f)"), M.M[2][0], M.M[2][1], M.M[2][2], M.M[2][3]));
			InvObj->SetStringField(TEXT("row3"), FString::Printf(TEXT("(%.4f, %.4f, %.4f, %.4f)"), M.M[3][0], M.M[3][1], M.M[3][2], M.M[3][3]));
			BoneObj->SetObjectField(TEXT("inv_bind_matrix"), InvObj);
		}

		BonesArray.Add(MakeShared<FJsonValueObject>(BoneObj));
	}
	ResultObj->SetArrayField(TEXT("bones"), BonesArray);

	FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
	if (RenderData && RenderData->LODRenderData.Num() > 0)
	{
		FSkeletalMeshLODRenderData& LOD0 = RenderData->LODRenderData[0];
		uint32 NumVerts = LOD0.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
		ResultObj->SetNumberField(TEXT("lod0_vertex_count"), NumVerts);

		const FSkinWeightVertexBuffer* SkinBuf = LOD0.GetSkinWeightVertexBuffer();

		TArray<int32> PerBoneVertCount;
		TArray<float> PerBoneWeightSum;
		PerBoneVertCount.SetNumZeroed(NumBones);
		PerBoneWeightSum.SetNumZeroed(NumBones);

		uint32 MaxInfluences = SkinBuf->GetMaxBoneInfluences();
		ResultObj->SetNumberField(TEXT("max_bone_influences"), MaxInfluences);

		for (uint32 v = 0; v < NumVerts; ++v)
		{
			for (uint32 inf = 0; inf < MaxInfluences; ++inf)
			{
				uint32 BIdx = SkinBuf->GetBoneIndex(v, inf);
				uint16 BWeight = SkinBuf->GetBoneWeight(v, inf);
				if (BWeight > 0 && (int32)BIdx < NumBones)
				{
					PerBoneVertCount[BIdx]++;
					PerBoneWeightSum[BIdx] += BWeight / 65535.0f;
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> WeightSummary;
		for (int32 b = 0; b < NumBones; ++b)
		{
			TSharedPtr<FJsonObject> WObj = MakeShared<FJsonObject>();
			WObj->SetStringField(TEXT("bone"), RefSkel.GetBoneName(b).ToString());
			WObj->SetNumberField(TEXT("vertices_influenced"), PerBoneVertCount[b]);
			WObj->SetNumberField(TEXT("weight_sum"), R3(PerBoneWeightSum[b]));
			WeightSummary.Add(MakeShared<FJsonValueObject>(WObj));
		}
		ResultObj->SetArrayField(TEXT("skin_weight_summary"), WeightSummary);

		int32 SampleCount = FMath::Min(5u, NumVerts);
		uint32 Step = NumVerts > 5 ? NumVerts / 5 : 1;
		TArray<TSharedPtr<FJsonValue>> SampleVerts;
		for (int32 s = 0; s < SampleCount; ++s)
		{
			uint32 vi = s * Step;
			TSharedPtr<FJsonObject> SV = MakeShared<FJsonObject>();
			SV->SetNumberField(TEXT("vertex_index"), vi);
			FVector3f Pos = LOD0.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(vi);
			SV->SetStringField(TEXT("position"), FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), Pos.X, Pos.Y, Pos.Z));

			TArray<TSharedPtr<FJsonValue>> Weights;
			for (uint32 inf = 0; inf < MaxInfluences; ++inf)
			{
				uint32 BIdx = SkinBuf->GetBoneIndex(vi, inf);
				uint16 BWeight = SkinBuf->GetBoneWeight(vi, inf);
				if (BWeight > 0)
				{
					TSharedPtr<FJsonObject> WI = MakeShared<FJsonObject>();
					WI->SetStringField(TEXT("bone"), (int32)BIdx < NumBones ? RefSkel.GetBoneName(BIdx).ToString() : FString::Printf(TEXT("idx_%d"), BIdx));
					WI->SetNumberField(TEXT("weight"), R3(BWeight / 65535.0f));
					Weights.Add(MakeShared<FJsonValueObject>(WI));
				}
			}
			SV->SetArrayField(TEXT("weights"), Weights);
			SampleVerts.Add(MakeShared<FJsonValueObject>(SV));
		}
		ResultObj->SetArrayField(TEXT("sample_vertices"), SampleVerts);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Mesh inspection: %s (%d bones)"), *MeshPath, NumBones),
		ResultObj);
}

FMCPToolResult FMCPTool_AnimEdit::HandleExtractRange(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	double StartFrameD = 0, EndFrameD = 0;
	if (!Params->TryGetNumberField(TEXT("start_frame"), StartFrameD))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: start_frame"));
	}
	if (!Params->TryGetNumberField(TEXT("end_frame"), EndFrameD))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: end_frame"));
	}

	int32 StartFrame = static_cast<int32>(StartFrameD);
	int32 EndFrame = static_cast<int32>(EndFrameD);

	FString DestPath = ExtractOptionalString(Params, TEXT("dest_path"), TEXT(""));
	FString NewName = ExtractOptionalString(Params, TEXT("new_name"), TEXT(""));
	bool bSave = ExtractOptionalBool(Params, TEXT("save"), true);

	TSharedPtr<FJsonObject> Result = FAnimTrackEditor::ExtractRange(AssetPath, StartFrame, EndFrame, DestPath, NewName, bSave);
	return AnimEditJsonToToolResult(Result, TEXT("Extract range complete"));
}
