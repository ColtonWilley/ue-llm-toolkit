// Copyright Natali Caggiano. All Rights Reserved.

#include "RetargetEditor.h"

// Engine
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

// IKRig runtime
#include "Rig/IKRigDefinition.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetSettings.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Retargeter/RetargetOps/PelvisMotionOp.h"

// IKRig editor
#include "RigEditor/IKRigController.h"
#include "RigEditor/IKRigDefinitionFactory.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"

// FBX import
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxAnimSequenceImportData.h"

// ============================================================================
// Private Helpers
// ============================================================================

TSharedPtr<FJsonObject> FRetargetEditor::SuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::ErrorResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Message);
	return Result;
}

USkeleton* FRetargetEditor::LoadSkeletonFromPath(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load: %s"), *Path);
		return nullptr;
	}

	// Could be a skeleton directly
	if (USkeleton* Skel = Cast<USkeleton>(Loaded))
	{
		return Skel;
	}

	// Or a skeletal mesh — get its skeleton
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Loaded))
	{
		USkeleton* Skel = Mesh->GetSkeleton();
		if (Skel)
		{
			return Skel;
		}
		OutError = FString::Printf(TEXT("Skeletal mesh has no skeleton: %s"), *Path);
		return nullptr;
	}

	OutError = FString::Printf(TEXT("Asset is not a Skeleton or SkeletalMesh: %s (is %s)"),
		*Path, *Loaded->GetClass()->GetName());
	return nullptr;
}

USkeletalMesh* FRetargetEditor::LoadSkeletalMeshFromPath(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(USkeletalMesh::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load skeletal mesh: %s"), *Path);
		return nullptr;
	}
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(Loaded);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Asset is not a SkeletalMesh: %s"), *Path);
		return nullptr;
	}
	return Mesh;
}

UIKRigDefinition* FRetargetEditor::LoadIKRig(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UIKRigDefinition::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load IK Rig: %s"), *Path);
		return nullptr;
	}
	UIKRigDefinition* Rig = Cast<UIKRigDefinition>(Loaded);
	if (!Rig)
	{
		OutError = FString::Printf(TEXT("Asset is not an IKRigDefinition: %s"), *Path);
		return nullptr;
	}
	return Rig;
}

UIKRetargeter* FRetargetEditor::LoadRetargeter(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UIKRetargeter::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load IK Retargeter: %s"), *Path);
		return nullptr;
	}
	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(Loaded);
	if (!Retargeter)
	{
		OutError = FString::Printf(TEXT("Asset is not an IKRetargeter: %s"), *Path);
		return nullptr;
	}
	return Retargeter;
}

UAnimSequence* FRetargetEditor::LoadAnimSequence(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load anim sequence: %s"), *Path);
		return nullptr;
	}
	UAnimSequence* Anim = Cast<UAnimSequence>(Loaded);
	if (!Anim)
	{
		OutError = FString::Printf(TEXT("Asset is not an AnimSequence: %s"), *Path);
		return nullptr;
	}
	return Anim;
}

// Pitfall #9: AddDefaultOps can create duplicates with _0 suffix
void FRetargetEditor::RemoveDuplicateOps(UIKRetargeterController* Controller)
{
	if (!Controller) return;

	// Collect op names, then remove any ending with _0, _1 etc that duplicate a base name
	TSet<FString> BaseNames;
	TArray<int32> IndicesToRemove;

	int32 NumOps = Controller->GetNumRetargetOps();
	for (int32 i = 0; i < NumOps; ++i)
	{
		FString OpName = Controller->GetOpName(i).ToString();
		BaseNames.Add(OpName);
	}

	// Second pass: find duplicates (names ending with _N where the base exists)
	NumOps = Controller->GetNumRetargetOps();
	for (int32 i = NumOps - 1; i >= 0; --i)
	{
		FString OpName = Controller->GetOpName(i).ToString();

		// Check if this looks like a duplicate: "BaseName_N"
		int32 LastUnderscore = INDEX_NONE;
		OpName.FindLastChar('_', LastUnderscore);
		if (LastUnderscore != INDEX_NONE && LastUnderscore > 0)
		{
			FString Suffix = OpName.Mid(LastUnderscore + 1);
			bool bIsNumeric = true;
			for (TCHAR C : Suffix)
			{
				if (!FChar::IsDigit(C))
				{
					bIsNumeric = false;
					break;
				}
			}

			if (bIsNumeric && Suffix.Len() > 0)
			{
				FString BaseName = OpName.Left(LastUnderscore);
				// If the base name exists as a separate op, this is a duplicate
				if (BaseNames.Contains(BaseName))
				{
					IndicesToRemove.Add(i);
				}
			}
		}
	}

	// Remove duplicates (iterate in reverse order so indices stay valid)
	for (int32 Idx : IndicesToRemove)
	{
		Controller->RemoveRetargetOp(Idx);
	}
}

bool FRetargetEditor::IsRootMotionAnim(const FString& AssetName, const FString& Pattern)
{
	// Simple pattern matching using | as OR separator
	TArray<FString> Patterns;
	Pattern.ParseIntoArray(Patterns, TEXT("|"), true);

	for (const FString& Pat : Patterns)
	{
		if (AssetName.Contains(Pat, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

// ============================================================================
// Skeleton
// ============================================================================

TSharedPtr<FJsonObject> FRetargetEditor::InspectSkeleton(const FString& SkeletonOrMeshPath)
{
	FString LoadError;
	USkeleton* Skeleton = LoadSkeletonFromPath(SkeletonOrMeshPath, LoadError);
	if (!Skeleton)
	{
		return ErrorResult(LoadError);
	}

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	int32 NumBones = RefSkel.GetNum();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("skeleton_name"), Skeleton->GetName());
	Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
	Result->SetNumberField(TEXT("bone_count"), NumBones);

	TArray<TSharedPtr<FJsonValue>> BonesArray;
	FString Summary = FString::Printf(TEXT("=== Skeleton: %s (%d bones) ===\n"), *Skeleton->GetName(), NumBones);

	for (int32 i = 0; i < NumBones; ++i)
	{
		FName BoneName = RefSkel.GetBoneName(i);
		int32 ParentIdx = RefSkel.GetParentIndex(i);
		FString ParentName = (ParentIdx >= 0) ? RefSkel.GetBoneName(ParentIdx).ToString() : TEXT("ROOT");

		TSharedPtr<FJsonObject> BoneJson = MakeShared<FJsonObject>();
		BoneJson->SetNumberField(TEXT("index"), i);
		BoneJson->SetStringField(TEXT("name"), BoneName.ToString());
		BoneJson->SetNumberField(TEXT("parent_index"), ParentIdx);
		BoneJson->SetStringField(TEXT("parent_name"), ParentName);

		// Ref pose transform
		FTransform RefPose = RefSkel.GetRefBonePose()[i];
		TSharedPtr<FJsonObject> TransformJson = MakeShared<FJsonObject>();
		FVector Loc = RefPose.GetLocation();
		TransformJson->SetNumberField(TEXT("x"), Loc.X);
		TransformJson->SetNumberField(TEXT("y"), Loc.Y);
		TransformJson->SetNumberField(TEXT("z"), Loc.Z);
		BoneJson->SetObjectField(TEXT("ref_location"), TransformJson);

		BonesArray.Add(MakeShared<FJsonValueObject>(BoneJson));

		// Indent based on depth
		int32 Depth = 0;
		int32 P = ParentIdx;
		while (P >= 0 && Depth < 20)
		{
			P = RefSkel.GetParentIndex(P);
			Depth++;
		}
		FString Indent;
		for (int32 d = 0; d < Depth; ++d) Indent += TEXT("  ");
		Summary += FString::Printf(TEXT("%s[%d] %s (parent: %s)\n"), *Indent, i, *BoneName.ToString(), *ParentName);
	}

	Result->SetArrayField(TEXT("bones"), BonesArray);

	// === Sockets ===
	auto SocketToJson = [](USkeletalMeshSocket* Socket, const TCHAR* Source) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> SockJson = MakeShared<FJsonObject>();
		SockJson->SetStringField(TEXT("name"), Socket->SocketName.ToString());
		SockJson->SetStringField(TEXT("bone_name"), Socket->BoneName.ToString());
		SockJson->SetStringField(TEXT("source"), Source);

		TSharedPtr<FJsonObject> LocJson = MakeShared<FJsonObject>();
		LocJson->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
		LocJson->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
		LocJson->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
		SockJson->SetObjectField(TEXT("relative_location"), LocJson);

		TSharedPtr<FJsonObject> RotJson = MakeShared<FJsonObject>();
		RotJson->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
		RotJson->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
		RotJson->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
		SockJson->SetObjectField(TEXT("relative_rotation"), RotJson);

		TSharedPtr<FJsonObject> ScaleJson = MakeShared<FJsonObject>();
		ScaleJson->SetNumberField(TEXT("x"), Socket->RelativeScale.X);
		ScaleJson->SetNumberField(TEXT("y"), Socket->RelativeScale.Y);
		ScaleJson->SetNumberField(TEXT("z"), Socket->RelativeScale.Z);
		SockJson->SetObjectField(TEXT("relative_scale"), ScaleJson);

		return SockJson;
	};

	TArray<TSharedPtr<FJsonValue>> SocketsArray;

	for (USkeletalMeshSocket* Socket : Skeleton->Sockets)
	{
		if (!Socket) continue;
		SocketsArray.Add(MakeShared<FJsonValueObject>(SocketToJson(Socket, TEXT("skeleton"))));
	}

	UObject* OriginalAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *SkeletonOrMeshPath);
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(OriginalAsset))
	{
		for (USkeletalMeshSocket* Socket : Mesh->GetMeshOnlySocketList())
		{
			if (!Socket) continue;
			SocketsArray.Add(MakeShared<FJsonValueObject>(SocketToJson(Socket, TEXT("mesh"))));
		}
	}

	Result->SetNumberField(TEXT("socket_count"), SocketsArray.Num());
	Result->SetArrayField(TEXT("sockets"), SocketsArray);

	if (SocketsArray.Num() > 0)
	{
		Summary += FString::Printf(TEXT("\n=== Sockets (%d) ===\n"), SocketsArray.Num());
		for (USkeletalMeshSocket* Socket : Skeleton->Sockets)
		{
			if (!Socket) continue;
			Summary += FString::Printf(TEXT("  [skeleton] %s on bone '%s' loc=(%g,%g,%g) rot=(%g,%g,%g)\n"),
				*Socket->SocketName.ToString(), *Socket->BoneName.ToString(),
				Socket->RelativeLocation.X, Socket->RelativeLocation.Y, Socket->RelativeLocation.Z,
				Socket->RelativeRotation.Pitch, Socket->RelativeRotation.Yaw, Socket->RelativeRotation.Roll);
		}
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(OriginalAsset))
		{
			for (USkeletalMeshSocket* Socket : Mesh->GetMeshOnlySocketList())
			{
				if (!Socket) continue;
				Summary += FString::Printf(TEXT("  [mesh] %s on bone '%s' loc=(%g,%g,%g) rot=(%g,%g,%g)\n"),
					*Socket->SocketName.ToString(), *Socket->BoneName.ToString(),
					Socket->RelativeLocation.X, Socket->RelativeLocation.Y, Socket->RelativeLocation.Z,
					Socket->RelativeRotation.Pitch, Socket->RelativeRotation.Yaw, Socket->RelativeRotation.Roll);
			}
		}
	}

	Result->SetStringField(TEXT("summary"), Summary);

	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::InspectRefPose(const FString& AssetPath, const TArray<FString>& BoneFilter)
{
	FString LoadError;
	USkeletalMesh* Mesh = LoadSkeletalMeshFromPath(AssetPath, LoadError);
	if (Mesh)
	{
		const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
		int32 NumBones = RefSkel.GetRawBoneNum();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("asset"), Mesh->GetName());
		Result->SetStringField(TEXT("path"), Mesh->GetPathName());
		Result->SetNumberField(TEXT("bone_count"), NumBones);
		Result->SetStringField(TEXT("source"), TEXT("skeletal_mesh"));

		TArray<TSharedPtr<FJsonValue>> BonesArray;

		for (int32 i = 0; i < NumBones; ++i)
		{
			const FMeshBoneInfo& BoneInfo = RefSkel.GetRawRefBoneInfo()[i];
			const FTransform& BonePose = RefSkel.GetRawRefBonePose()[i];

			FVector Loc = BonePose.GetLocation();
			FQuat Quat = BonePose.GetRotation();
			FRotator Rot = Quat.Rotator();

			FString ParentName = (BoneInfo.ParentIndex >= 0)
				? RefSkel.GetRawRefBoneInfo()[BoneInfo.ParentIndex].Name.ToString()
				: TEXT("ROOT");

			TSharedPtr<FJsonObject> PosJson = MakeShared<FJsonObject>();
			PosJson->SetNumberField(TEXT("x"), Loc.X);
			PosJson->SetNumberField(TEXT("y"), Loc.Y);
			PosJson->SetNumberField(TEXT("z"), Loc.Z);

			TSharedPtr<FJsonObject> RotJson = MakeShared<FJsonObject>();
			RotJson->SetNumberField(TEXT("pitch"), Rot.Pitch);
			RotJson->SetNumberField(TEXT("yaw"), Rot.Yaw);
			RotJson->SetNumberField(TEXT("roll"), Rot.Roll);

			TSharedPtr<FJsonObject> QuatJson = MakeShared<FJsonObject>();
			QuatJson->SetNumberField(TEXT("x"), Quat.X);
			QuatJson->SetNumberField(TEXT("y"), Quat.Y);
			QuatJson->SetNumberField(TEXT("z"), Quat.Z);
			QuatJson->SetNumberField(TEXT("w"), Quat.W);

			TSharedPtr<FJsonObject> BoneJson = MakeShared<FJsonObject>();
			BoneJson->SetNumberField(TEXT("index"), i);
			BoneJson->SetStringField(TEXT("name"), BoneInfo.Name.ToString());
			BoneJson->SetNumberField(TEXT("parent_index"), BoneInfo.ParentIndex);
			BoneJson->SetStringField(TEXT("parent_name"), ParentName);
			BoneJson->SetObjectField(TEXT("position"), PosJson);
			BoneJson->SetObjectField(TEXT("rotation"), RotJson);
			BoneJson->SetObjectField(TEXT("rotation_quat"), QuatJson);

			BonesArray.Add(MakeShared<FJsonValueObject>(BoneJson));
		}

		Result->SetArrayField(TEXT("bones"), BonesArray);
		return Result;
	}

	// Fallback: try USkeleton
	FString SkeletonError;
	USkeleton* Skeleton = LoadSkeletonFromPath(AssetPath, SkeletonError);
	if (!Skeleton)
	{
		return ErrorResult(FString::Printf(TEXT("Not a skeletal mesh or skeleton: %s — %s / %s"),
			*AssetPath, *LoadError, *SkeletonError));
	}

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	int32 NumBones = RefSkel.GetNum();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), Skeleton->GetName());
	Result->SetStringField(TEXT("path"), Skeleton->GetPathName());
	Result->SetNumberField(TEXT("bone_count"), NumBones);
	Result->SetStringField(TEXT("source"), TEXT("skeleton"));

	TArray<TSharedPtr<FJsonValue>> BonesArray;

	for (int32 i = 0; i < NumBones; ++i)
	{
		FName BoneName = RefSkel.GetBoneName(i);
		int32 ParentIdx = RefSkel.GetParentIndex(i);
		FString ParentName = (ParentIdx >= 0) ? RefSkel.GetBoneName(ParentIdx).ToString() : TEXT("ROOT");
		const FTransform& BonePose = RefSkel.GetRefBonePose()[i];

		FVector Loc = BonePose.GetLocation();
		FQuat Quat = BonePose.GetRotation();
		FRotator Rot = Quat.Rotator();

		TSharedPtr<FJsonObject> PosJson = MakeShared<FJsonObject>();
		PosJson->SetNumberField(TEXT("x"), Loc.X);
		PosJson->SetNumberField(TEXT("y"), Loc.Y);
		PosJson->SetNumberField(TEXT("z"), Loc.Z);

		TSharedPtr<FJsonObject> RotJson = MakeShared<FJsonObject>();
		RotJson->SetNumberField(TEXT("pitch"), Rot.Pitch);
		RotJson->SetNumberField(TEXT("yaw"), Rot.Yaw);
		RotJson->SetNumberField(TEXT("roll"), Rot.Roll);

		TSharedPtr<FJsonObject> QuatJson = MakeShared<FJsonObject>();
		QuatJson->SetNumberField(TEXT("x"), Quat.X);
		QuatJson->SetNumberField(TEXT("y"), Quat.Y);
		QuatJson->SetNumberField(TEXT("z"), Quat.Z);
		QuatJson->SetNumberField(TEXT("w"), Quat.W);

		TSharedPtr<FJsonObject> BoneJson = MakeShared<FJsonObject>();
		BoneJson->SetNumberField(TEXT("index"), i);
		BoneJson->SetStringField(TEXT("name"), BoneName.ToString());
		BoneJson->SetNumberField(TEXT("parent_index"), ParentIdx);
		BoneJson->SetStringField(TEXT("parent_name"), ParentName);
		BoneJson->SetObjectField(TEXT("position"), PosJson);
		BoneJson->SetObjectField(TEXT("rotation"), RotJson);
		BoneJson->SetObjectField(TEXT("rotation_quat"), QuatJson);

		BonesArray.Add(MakeShared<FJsonValueObject>(BoneJson));
	}

	Result->SetArrayField(TEXT("bones"), BonesArray);
	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::ListSkeletons(const FString& FolderPath)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*FolderPath));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Assets.Num());

	TArray<TSharedPtr<FJsonValue>> SkeletonsArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> SkelJson = MakeShared<FJsonObject>();
		SkelJson->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		SkelJson->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		SkeletonsArray.Add(MakeShared<FJsonValueObject>(SkelJson));
	}

	Result->SetArrayField(TEXT("skeletons"), SkeletonsArray);
	return Result;
}

namespace SkeletonAccess
{
	struct FHelper : public USkeleton
	{
		using USkeleton::BoneTree;
		using USkeleton::HandleSkeletonHierarchyChange;
	};
}

TSharedPtr<FJsonObject> FRetargetEditor::AddBoneToSkeleton(const FString& SkeletonPath,
	const FString& BoneName, const FString& ParentBoneName,
	const FVector& Position, const FQuat& Rotation)
{
	FString LoadError;
	USkeleton* Skeleton = LoadSkeletonFromPath(SkeletonPath, LoadError);
	if (!Skeleton)
	{
		return ErrorResult(LoadError);
	}

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();

	if (RefSkel.FindBoneIndex(FName(*BoneName)) != INDEX_NONE)
	{
		return ErrorResult(FString::Printf(TEXT("Bone '%s' already exists in skeleton"), *BoneName));
	}

	int32 ParentIndex = RefSkel.FindBoneIndex(FName(*ParentBoneName));
	if (ParentIndex == INDEX_NONE)
	{
		return ErrorResult(FString::Printf(TEXT("Parent bone '%s' not found in skeleton"), *ParentBoneName));
	}

	FMeshBoneInfo BoneInfo(FName(*BoneName), BoneName, ParentIndex);
	FTransform BonePose(Rotation, Position, FVector::OneVector);

	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(BoneInfo, BonePose);
	}

	auto* Helper = static_cast<SkeletonAccess::FHelper*>(Skeleton);
	Helper->BoneTree.AddDefaulted(1);
	Helper->HandleSkeletonHierarchyChange();

	int32 NewIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(FName(*BoneName));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Added bone '%s' (index %d) as child of '%s' (index %d)"),
		*BoneName, NewIndex, *ParentBoneName, ParentIndex));
	Result->SetNumberField(TEXT("bone_index"), NewIndex);
	Result->SetNumberField(TEXT("total_bones"), Skeleton->GetReferenceSkeleton().GetNum());
	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::CopyBoneTracks(const FString& SourceAnimPath,
	const FString& TargetAnimPath, const TArray<FString>& BoneNames)
{
	FString LoadError;
	UAnimSequence* SourceAnim = LoadAnimSequence(SourceAnimPath, LoadError);
	if (!SourceAnim) return ErrorResult(LoadError);

	UAnimSequence* TargetAnim = LoadAnimSequence(TargetAnimPath, LoadError);
	if (!TargetAnim) return ErrorResult(LoadError);

	const IAnimationDataModel* SourceModel = SourceAnim->GetDataModel();
	if (!SourceModel) return ErrorResult(TEXT("Source animation has no data model"));

	IAnimationDataController& TargetController = TargetAnim->GetController();
	const IAnimationDataModel* TargetModel = TargetController.GetModel();
	if (!TargetModel) return ErrorResult(TEXT("Target animation has no data model"));

	int32 TargetNumKeys = TargetModel->GetNumberOfKeys();
	int32 SourceNumKeys = SourceModel->GetNumberOfKeys();

	TArray<FName> SourceTrackNames;
	SourceModel->GetBoneTrackNames(SourceTrackNames);
	TSet<FName> SourceTrackSet(SourceTrackNames);

	TargetController.OpenBracket(FText::FromString(TEXT("CopyBoneTracks")), false);

	int32 CopiedCount = 0;
	TArray<FString> CopiedNames;

	for (const FString& BoneName : BoneNames)
	{
		FName BoneFName(*BoneName);

		if (!SourceTrackSet.Contains(BoneFName))
		{
			continue;
		}

		TargetController.AddBoneCurve(BoneFName, false);

		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;
		Positions.Reserve(TargetNumKeys);
		Rotations.Reserve(TargetNumKeys);
		Scales.Reserve(TargetNumKeys);

		for (int32 KeyIdx = 0; KeyIdx < TargetNumKeys; ++KeyIdx)
		{
			float Alpha = (TargetNumKeys > 1) ? (float)KeyIdx / (float)(TargetNumKeys - 1) : 0.f;
			float SourceFloat = Alpha * (float)(SourceNumKeys - 1);
			int32 SourceKeyA = FMath::FloorToInt32(SourceFloat);
			int32 SourceKeyB = FMath::Min(SourceKeyA + 1, SourceNumKeys - 1);
			float Frac = SourceFloat - (float)SourceKeyA;

			FTransform TA = SourceModel->GetBoneTrackTransform(BoneFName, FFrameNumber(SourceKeyA));
			if (Frac < KINDA_SMALL_NUMBER)
			{
				Positions.Add(TA.GetLocation());
				Rotations.Add(TA.GetRotation());
				Scales.Add(TA.GetScale3D());
			}
			else
			{
				FTransform TB = SourceModel->GetBoneTrackTransform(BoneFName, FFrameNumber(SourceKeyB));
				FTransform Blended;
				Blended.SetLocation(FMath::Lerp(TA.GetLocation(), TB.GetLocation(), Frac));
				Blended.SetRotation(FQuat::Slerp(TA.GetRotation(), TB.GetRotation(), Frac));
				Blended.SetScale3D(FMath::Lerp(TA.GetScale3D(), TB.GetScale3D(), Frac));
				Positions.Add(Blended.GetLocation());
				Rotations.Add(Blended.GetRotation());
				Scales.Add(Blended.GetScale3D());
			}
		}

		TargetController.SetBoneTrackKeys(BoneFName, Positions, Rotations, Scales, false);
		CopiedCount++;
		CopiedNames.Add(BoneName);
	}

	TargetController.CloseBracket(false);
	TargetAnim->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Copied %d bone tracks from '%s' to '%s': %s"),
		CopiedCount, *SourceAnim->GetName(), *TargetAnim->GetName(),
		*FString::Join(CopiedNames, TEXT(", "))));
	Result->SetNumberField(TEXT("copied_count"), CopiedCount);
	Result->SetNumberField(TEXT("source_keys"), SourceNumKeys);
	Result->SetNumberField(TEXT("target_keys"), TargetNumKeys);
	Result->SetNumberField(TEXT("target_total_tracks"), TargetModel->GetNumBoneTracks());
	return Result;
}

// ============================================================================
// IK Rig
// ============================================================================

TSharedPtr<FJsonObject> FRetargetEditor::InspectIKRig(const FString& RigPath)
{
	FString LoadError;
	UIKRigDefinition* RigDef = LoadIKRig(RigPath, LoadError);
	if (!RigDef)
	{
		return ErrorResult(LoadError);
	}

	UIKRigController* Controller = UIKRigController::GetController(RigDef);
	if (!Controller)
	{
		return ErrorResult(TEXT("Failed to get IK Rig controller"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), RigDef->GetName());
	Result->SetStringField(TEXT("path"), RigDef->GetPathName());

	// Skeletal mesh
	USkeletalMesh* Mesh = Controller->GetSkeletalMesh();
	if (Mesh)
	{
		Result->SetStringField(TEXT("skeletal_mesh"), Mesh->GetPathName());
	}

	// Retarget root
	FName RetargetRoot = Controller->GetRetargetRoot();
	Result->SetStringField(TEXT("retarget_root"), RetargetRoot.ToString());

	// Chains
	const TArray<FBoneChain>& Chains = Controller->GetRetargetChains();
	TArray<TSharedPtr<FJsonValue>> ChainsArray;
	for (const FBoneChain& Chain : Chains)
	{
		TSharedPtr<FJsonObject> ChainJson = MakeShared<FJsonObject>();
		ChainJson->SetStringField(TEXT("name"), Chain.ChainName.ToString());
		ChainJson->SetStringField(TEXT("start_bone"), Chain.StartBone.BoneName.ToString());
		ChainJson->SetStringField(TEXT("end_bone"), Chain.EndBone.BoneName.ToString());
		if (!Chain.IKGoalName.IsNone())
		{
			ChainJson->SetStringField(TEXT("goal"), Chain.IKGoalName.ToString());
		}
		ChainsArray.Add(MakeShared<FJsonValueObject>(ChainJson));
	}
	Result->SetArrayField(TEXT("chains"), ChainsArray);
	Result->SetNumberField(TEXT("chain_count"), Chains.Num());

	// Goals (pitfall #15: warn if present)
	const TArray<UIKRigEffectorGoal*>& Goals = Controller->GetAllGoals();
	Result->SetNumberField(TEXT("goal_count"), Goals.Num());
	if (Goals.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> GoalsArray;
		for (UIKRigEffectorGoal* Goal : Goals)
		{
			if (Goal)
			{
				GoalsArray.Add(MakeShared<FJsonValueString>(Goal->GoalName.ToString()));
			}
		}
		Result->SetArrayField(TEXT("goals"), GoalsArray);
	}

	// Solvers (pitfall #15: warn if present)
	int32 NumSolvers = Controller->GetNumSolvers();
	Result->SetNumberField(TEXT("solver_count"), NumSolvers);

	// Warnings
	TArray<TSharedPtr<FJsonValue>> Warnings;
	if (Goals.Num() > 0)
	{
		Warnings.Add(MakeShared<FJsonValueString>(
			TEXT("PITFALL #15: IK goals present — batch retarget will fail. Remove all goals for pure FK retargeting.")));
	}
	if (NumSolvers > 0)
	{
		Warnings.Add(MakeShared<FJsonValueString>(
			TEXT("PITFALL #15: IK solvers present — batch retarget will fail. Remove all solvers for pure FK retargeting.")));
	}
	if (Warnings.Num() > 0)
	{
		Result->SetArrayField(TEXT("warnings"), Warnings);
	}

	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::CreateIKRig(const FString& PackagePath, const FString& RigName,
	const FString& SkeletalMeshPath, const FString& RetargetRoot,
	const TArray<TSharedPtr<FJsonValue>>& Chains)
{
	// Load the skeletal mesh first
	FString MeshError;
	USkeletalMesh* Mesh = LoadSkeletalMeshFromPath(SkeletalMeshPath, MeshError);
	if (!Mesh)
	{
		return ErrorResult(MeshError);
	}

	// Create IK Rig asset via factory
	UIKRigDefinition* NewRig = UIKRigDefinitionFactory::CreateNewIKRigAsset(PackagePath, RigName);
	if (!NewRig)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to create IK Rig: %s/%s"), *PackagePath, *RigName));
	}

	UIKRigController* Controller = UIKRigController::GetController(NewRig);
	if (!Controller)
	{
		return ErrorResult(TEXT("Failed to get controller for new IK Rig"));
	}

	// Set skeletal mesh
	if (!Controller->SetSkeletalMesh(Mesh))
	{
		return ErrorResult(FString::Printf(TEXT("Failed to set skeletal mesh: %s"), *SkeletalMeshPath));
	}

	// Set retarget root
	if (!RetargetRoot.IsEmpty())
	{
		Controller->SetRetargetRoot(FName(*RetargetRoot));
	}

	// Add chains — NO goals, NO solvers (pitfall #15)
	int32 ChainsAdded = 0;
	for (const TSharedPtr<FJsonValue>& ChainVal : Chains)
	{
		const TSharedPtr<FJsonObject>* ChainObj;
		if (!ChainVal.IsValid() || !ChainVal->TryGetObject(ChainObj) || !ChainObj)
		{
			continue;
		}

		FString ChainName, StartBone, EndBone;
		(*ChainObj)->TryGetStringField(TEXT("name"), ChainName);
		(*ChainObj)->TryGetStringField(TEXT("start_bone"), StartBone);
		(*ChainObj)->TryGetStringField(TEXT("end_bone"), EndBone);

		if (!ChainName.IsEmpty() && !StartBone.IsEmpty() && !EndBone.IsEmpty())
		{
			FName Result = Controller->AddRetargetChain(FName(*ChainName), FName(*StartBone), FName(*EndBone), NAME_None);
			if (!Result.IsNone())
			{
				ChainsAdded++;
			}
		}
	}

	// Mark dirty and save
	NewRig->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Created IK Rig '%s' with %d chains, retarget root: %s"),
		*RigName, ChainsAdded, *RetargetRoot));
	Result->SetStringField(TEXT("asset_path"), NewRig->GetPathName());
	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::AddChain(const FString& RigPath,
	const FString& ChainName, const FString& StartBone, const FString& EndBone)
{
	FString LoadError;
	UIKRigDefinition* RigDef = LoadIKRig(RigPath, LoadError);
	if (!RigDef)
	{
		return ErrorResult(LoadError);
	}

	UIKRigController* Controller = UIKRigController::GetController(RigDef);
	if (!Controller)
	{
		return ErrorResult(TEXT("Failed to get IK Rig controller"));
	}

	FName Result = Controller->AddRetargetChain(FName(*ChainName), FName(*StartBone), FName(*EndBone), NAME_None);
	if (Result.IsNone())
	{
		return ErrorResult(FString::Printf(TEXT("Failed to add chain '%s' (%s -> %s)"),
			*ChainName, *StartBone, *EndBone));
	}

	RigDef->MarkPackageDirty();
	return SuccessResult(FString::Printf(TEXT("Added chain '%s' (%s -> %s)"),
		*Result.ToString(), *StartBone, *EndBone));
}

TSharedPtr<FJsonObject> FRetargetEditor::RemoveChain(const FString& RigPath, const FString& ChainName)
{
	FString LoadError;
	UIKRigDefinition* RigDef = LoadIKRig(RigPath, LoadError);
	if (!RigDef)
	{
		return ErrorResult(LoadError);
	}

	UIKRigController* Controller = UIKRigController::GetController(RigDef);
	if (!Controller)
	{
		return ErrorResult(TEXT("Failed to get IK Rig controller"));
	}

	bool bRemoved = Controller->RemoveRetargetChain(FName(*ChainName));
	if (!bRemoved)
	{
		return ErrorResult(FString::Printf(TEXT("Chain '%s' not found or could not be removed"), *ChainName));
	}

	RigDef->MarkPackageDirty();
	return SuccessResult(FString::Printf(TEXT("Removed chain '%s'"), *ChainName));
}

// ============================================================================
// Retargeter
// ============================================================================

TSharedPtr<FJsonObject> FRetargetEditor::InspectRetargeter(const FString& RetargeterPath)
{
	FString LoadError;
	UIKRetargeter* Retargeter = LoadRetargeter(RetargeterPath, LoadError);
	if (!Retargeter)
	{
		return ErrorResult(LoadError);
	}

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		return ErrorResult(TEXT("Failed to get retargeter controller"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Retargeter->GetName());
	Result->SetStringField(TEXT("path"), Retargeter->GetPathName());

	// Source/Target IK Rigs
	const UIKRigDefinition* SourceRig = Controller->GetIKRig(ERetargetSourceOrTarget::Source);
	const UIKRigDefinition* TargetRig = Controller->GetIKRig(ERetargetSourceOrTarget::Target);
	Result->SetStringField(TEXT("source_rig"), SourceRig ? SourceRig->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("target_rig"), TargetRig ? TargetRig->GetPathName() : TEXT("None"));

	// Preview meshes
	USkeletalMesh* SourceMesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Source);
	USkeletalMesh* TargetMesh = Controller->GetPreviewMesh(ERetargetSourceOrTarget::Target);
	Result->SetStringField(TEXT("source_mesh"), SourceMesh ? SourceMesh->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("target_mesh"), TargetMesh ? TargetMesh->GetPathName() : TEXT("None"));

	// Ops stack
	int32 NumOps = Controller->GetNumRetargetOps();
	Result->SetNumberField(TEXT("op_count"), NumOps);

	TArray<TSharedPtr<FJsonValue>> OpsArray;
	for (int32 i = 0; i < NumOps; ++i)
	{
		TSharedPtr<FJsonObject> OpJson = MakeShared<FJsonObject>();
		FName OpName = Controller->GetOpName(i);
		OpJson->SetNumberField(TEXT("index"), i);
		OpJson->SetStringField(TEXT("name"), OpName.ToString());
		OpJson->SetBoolField(TEXT("enabled"), Controller->GetRetargetOpEnabled(i));

		// Try to identify op type and get FK settings
		FIKRetargetOpBase* OpBase = Controller->GetRetargetOpByIndex(i);
		if (OpBase)
		{
			const UScriptStruct* OpType = OpBase->GetType();
			if (OpType)
			{
				OpJson->SetStringField(TEXT("type"), OpType->GetName());
			}

			// If this is an FK Chains op, extract per-chain settings
			if (OpType && OpType == FIKRetargetFKChainsOp::StaticStruct())
			{
				FIKRetargetOpSettingsBase* SettingsBase = OpBase->GetSettings();
				FIKRetargetFKChainsOpSettings* FKSettings = static_cast<FIKRetargetFKChainsOpSettings*>(SettingsBase);
				if (FKSettings)
				{
					TArray<TSharedPtr<FJsonValue>> FKArray;
					for (const FRetargetFKChainSettings& ChainSetting : FKSettings->ChainsToRetarget)
					{
						TSharedPtr<FJsonObject> FKJson = MakeShared<FJsonObject>();
						FKJson->SetStringField(TEXT("chain"), ChainSetting.TargetChainName.ToString());
						FKJson->SetBoolField(TEXT("enable_fk"), ChainSetting.EnableFK);

						// Translation mode
						switch (ChainSetting.TranslationMode)
						{
						case EFKChainTranslationMode::None: FKJson->SetStringField(TEXT("translation"), TEXT("None")); break;
						case EFKChainTranslationMode::GloballyScaled: FKJson->SetStringField(TEXT("translation"), TEXT("GloballyScaled")); break;
						case EFKChainTranslationMode::Absolute: FKJson->SetStringField(TEXT("translation"), TEXT("Absolute")); break;
						default: FKJson->SetStringField(TEXT("translation"), TEXT("Other")); break;
						}

						// Rotation mode
						switch (ChainSetting.RotationMode)
						{
						case EFKChainRotationMode::None: FKJson->SetStringField(TEXT("rotation"), TEXT("None")); break;
						case EFKChainRotationMode::Interpolated: FKJson->SetStringField(TEXT("rotation"), TEXT("Interpolated")); break;
						case EFKChainRotationMode::OneToOne: FKJson->SetStringField(TEXT("rotation"), TEXT("OneToOne")); break;
						case EFKChainRotationMode::OneToOneReversed: FKJson->SetStringField(TEXT("rotation"), TEXT("OneToOneReversed")); break;
						case EFKChainRotationMode::MatchChain: FKJson->SetStringField(TEXT("rotation"), TEXT("MatchChain")); break;
						case EFKChainRotationMode::MatchScaledChain: FKJson->SetStringField(TEXT("rotation"), TEXT("MatchScaledChain")); break;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
						case EFKChainRotationMode::CopyLocal: FKJson->SetStringField(TEXT("rotation"), TEXT("CopyLocal")); break;
#endif
						default: FKJson->SetStringField(TEXT("rotation"), TEXT("Other")); break;
						}

						FKJson->SetNumberField(TEXT("rotation_alpha"), ChainSetting.RotationAlpha);
						FKJson->SetNumberField(TEXT("translation_alpha"), ChainSetting.TranslationAlpha);

						FKArray.Add(MakeShared<FJsonValueObject>(FKJson));
					}
					OpJson->SetArrayField(TEXT("fk_chains"), FKArray);

					// Chain mapping
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
					const FRetargetChainMapping& Mapping = FKSettings->ChainMapping;
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 6
					const FRetargetChainMapping& Mapping = Retargeter->GetChainMapping();
#endif
					const TArray<FRetargetChainPair>& Pairs = Mapping.GetChainPairs();
					TArray<TSharedPtr<FJsonValue>> MappingArray;
					for (const FRetargetChainPair& Pair : Pairs)
					{
						TSharedPtr<FJsonObject> PairJson = MakeShared<FJsonObject>();
						PairJson->SetStringField(TEXT("target"), Pair.TargetChainName.ToString());
						PairJson->SetStringField(TEXT("source"), Pair.SourceChainName.ToString());
						MappingArray.Add(MakeShared<FJsonValueObject>(PairJson));
					}
					OpJson->SetArrayField(TEXT("chain_mapping"), MappingArray);
				}
			}
		}

		OpsArray.Add(MakeShared<FJsonValueObject>(OpJson));
	}
	Result->SetArrayField(TEXT("ops"), OpsArray);

	// Warnings
	TArray<TSharedPtr<FJsonValue>> Warnings;
	// Pitfall #11: Check for Pelvis Motion op
	bool bHasPelvisMotion = false;
	for (int32 i = 0; i < NumOps; ++i)
	{
		FIKRetargetOpBase* OpBase = Controller->GetRetargetOpByIndex(i);
		if (OpBase && OpBase->GetType() == FIKRetargetPelvisMotionOp::StaticStruct())
		{
			bHasPelvisMotion = true;
			break;
		}
	}
	if (bHasPelvisMotion)
	{
		Warnings.Add(MakeShared<FJsonValueString>(
			TEXT("PITFALL #11: Pelvis Motion op is present. DuplicateAndRetarget ignores this op — pelvis translation must come through FK chain with TranslationMode=GloballyScaled.")));
	}
	if (Warnings.Num() > 0)
	{
		Result->SetArrayField(TEXT("warnings"), Warnings);
	}

	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::CreateRetargeter(const FString& PackagePath, const FString& Name,
	const FString& SourceRigPath, const FString& TargetRigPath)
{
	// Load IK Rigs
	FString SrcError, TgtError;
	UIKRigDefinition* SourceRig = LoadIKRig(SourceRigPath, SrcError);
	if (!SourceRig) return ErrorResult(SrcError);
	UIKRigDefinition* TargetRig = LoadIKRig(TargetRigPath, TgtError);
	if (!TargetRig) return ErrorResult(TgtError);

	// Create the package
	FString FullPath = PackagePath / Name;
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	// Create retargeter using factory
	UIKRetargetFactory* Factory = NewObject<UIKRetargetFactory>();
	UObject* NewObj = Factory->FactoryCreateNew(
		UIKRetargeter::StaticClass(), Package, FName(*Name),
		RF_Public | RF_Standalone, nullptr, GWarn);

	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(NewObj);
	if (!Retargeter)
	{
		return ErrorResult(TEXT("Factory failed to create IKRetargeter"));
	}

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		return ErrorResult(TEXT("Failed to get retargeter controller"));
	}

	// Set source/target IK rigs
	Controller->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
	Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);

	// Add default ops, clean duplicates, assign, auto-map
	Controller->AddDefaultOps();
	RemoveDuplicateOps(Controller);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
	Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
#endif
	Controller->AutoMapChains(EAutoMapChainType::Fuzzy, true);

	// Pitfall #12: Auto-set Pelvis FK chain to GloballyScaled
	// Find the FK Chains op and set the Pelvis chain translation mode
	int32 NumOps = Controller->GetNumRetargetOps();
	for (int32 i = 0; i < NumOps; ++i)
	{
		FIKRetargetOpBase* OpBase = Controller->GetRetargetOpByIndex(i);
		if (OpBase && OpBase->GetType() == FIKRetargetFKChainsOp::StaticStruct())
		{
			FIKRetargetOpSettingsBase* SettingsBase = OpBase->GetSettings();
			FIKRetargetFKChainsOpSettings* FKSettings = static_cast<FIKRetargetFKChainsOpSettings*>(SettingsBase);
			if (FKSettings)
			{
				for (FRetargetFKChainSettings& ChainSetting : FKSettings->ChainsToRetarget)
				{
					// Auto-fix Pelvis/Hips chain to GloballyScaled (pitfall #12)
					FString ChainNameStr = ChainSetting.TargetChainName.ToString();
					if (ChainNameStr.Contains(TEXT("Pelvis"), ESearchCase::IgnoreCase) ||
						ChainNameStr.Contains(TEXT("Hips"), ESearchCase::IgnoreCase) ||
						ChainNameStr.Contains(TEXT("Root"), ESearchCase::IgnoreCase))
					{
						ChainSetting.TranslationMode = EFKChainTranslationMode::GloballyScaled;
					}
				}
			}
			break;
		}
	}

	Retargeter->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Created retargeter '%s' with %d ops, auto-mapped chains"),
		*Name, Controller->GetNumRetargetOps()));
	Result->SetStringField(TEXT("asset_path"), Retargeter->GetPathName());
	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::SetupOps(const FString& RetargeterPath)
{
	FString LoadError;
	UIKRetargeter* Retargeter = LoadRetargeter(RetargeterPath, LoadError);
	if (!Retargeter) return ErrorResult(LoadError);

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller) return ErrorResult(TEXT("Failed to get retargeter controller"));

	// Get rigs for re-assignment
	UIKRigDefinition* SourceRig = Controller->GetIKRigWriteable(ERetargetSourceOrTarget::Source);
	UIKRigDefinition* TargetRig = Controller->GetIKRigWriteable(ERetargetSourceOrTarget::Target);

	// Add defaults, clean dupes, assign, map (pitfalls #9, #10)
	Controller->AddDefaultOps();
	RemoveDuplicateOps(Controller);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (SourceRig)
	{
		Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
	}
	if (TargetRig)
	{
		Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
	}
#endif
	Controller->AutoMapChains(EAutoMapChainType::Fuzzy, true);

	Retargeter->MarkPackageDirty();

	return SuccessResult(FString::Printf(TEXT("SetupOps complete: %d ops after cleanup"), Controller->GetNumRetargetOps()));
}

TSharedPtr<FJsonObject> FRetargetEditor::ConfigureFK(const FString& RetargeterPath,
	const TArray<TSharedPtr<FJsonValue>>& ChainSettings)
{
	FString LoadError;
	UIKRetargeter* Retargeter = LoadRetargeter(RetargeterPath, LoadError);
	if (!Retargeter) return ErrorResult(LoadError);

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller) return ErrorResult(TEXT("Failed to get retargeter controller"));

	// Find the FK Chains op
	int32 NumOps = Controller->GetNumRetargetOps();
	FIKRetargetFKChainsOp* FKOp = nullptr;
	int32 FKOpIndex = INDEX_NONE;
	for (int32 i = 0; i < NumOps; ++i)
	{
		FIKRetargetOpBase* OpBase = Controller->GetRetargetOpByIndex(i);
		if (OpBase && OpBase->GetType() == FIKRetargetFKChainsOp::StaticStruct())
		{
			FKOp = static_cast<FIKRetargetFKChainsOp*>(OpBase);
			FKOpIndex = i;
			break;
		}
	}

	if (!FKOp)
	{
		return ErrorResult(TEXT("No FK Chains op found in retargeter stack"));
	}

	// Get the FK controller for structured access
	UIKRetargetOpControllerBase* OpCtrlBase = Controller->GetOpController(FKOpIndex);
	UIKRetargetFKChainsController* FKController = Cast<UIKRetargetFKChainsController>(OpCtrlBase);

	// Get current settings
	FIKRetargetOpSettingsBase* SettingsBase = FKOp->GetSettings();
	FIKRetargetFKChainsOpSettings* FKSettings = static_cast<FIKRetargetFKChainsOpSettings*>(SettingsBase);
	if (!FKSettings)
	{
		return ErrorResult(TEXT("Failed to access FK chain settings"));
	}

	// Build a map of chain name -> index for quick lookup
	TMap<FString, int32> ChainNameToIndex;
	for (int32 i = 0; i < FKSettings->ChainsToRetarget.Num(); ++i)
	{
		ChainNameToIndex.Add(FKSettings->ChainsToRetarget[i].TargetChainName.ToString().ToLower(), i);
	}

	// Apply settings from JSON
	int32 Modified = 0;
	for (const TSharedPtr<FJsonValue>& SettingVal : ChainSettings)
	{
		const TSharedPtr<FJsonObject>* SettingObj;
		if (!SettingVal.IsValid() || !SettingVal->TryGetObject(SettingObj) || !SettingObj)
		{
			continue;
		}

		FString ChainName;
		(*SettingObj)->TryGetStringField(TEXT("chain_name"), ChainName);
		if (ChainName.IsEmpty()) continue;

		int32* IndexPtr = ChainNameToIndex.Find(ChainName.ToLower());
		if (!IndexPtr) continue;

		FRetargetFKChainSettings& Setting = FKSettings->ChainsToRetarget[*IndexPtr];

		// Translation mode
		FString TransMode;
		if ((*SettingObj)->TryGetStringField(TEXT("translation_mode"), TransMode))
		{
			TransMode = TransMode.ToLower();
			if (TransMode == TEXT("none")) Setting.TranslationMode = EFKChainTranslationMode::None;
			else if (TransMode == TEXT("globallyscaled")) Setting.TranslationMode = EFKChainTranslationMode::GloballyScaled;
			else if (TransMode == TEXT("absolute")) Setting.TranslationMode = EFKChainTranslationMode::Absolute;
		}

		// Rotation mode
		FString RotMode;
		if ((*SettingObj)->TryGetStringField(TEXT("rotation_mode"), RotMode))
		{
			RotMode = RotMode.ToLower();
			if (RotMode == TEXT("none")) Setting.RotationMode = EFKChainRotationMode::None;
			else if (RotMode == TEXT("interpolated")) Setting.RotationMode = EFKChainRotationMode::Interpolated;
			else if (RotMode == TEXT("onetoone")) Setting.RotationMode = EFKChainRotationMode::OneToOne;
			else if (RotMode == TEXT("onetoonereversed")) Setting.RotationMode = EFKChainRotationMode::OneToOneReversed;
			else if (RotMode == TEXT("matchchain")) Setting.RotationMode = EFKChainRotationMode::MatchChain;
			else if (RotMode == TEXT("matchscaledchain")) Setting.RotationMode = EFKChainRotationMode::MatchScaledChain;
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		else if (RotMode == TEXT("copylocal")) Setting.RotationMode = EFKChainRotationMode::CopyLocal;
#endif
		}

		// Alpha values
		double Alpha;
		if ((*SettingObj)->TryGetNumberField(TEXT("rotation_alpha"), Alpha))
		{
			Setting.RotationAlpha = Alpha;
		}
		if ((*SettingObj)->TryGetNumberField(TEXT("translation_alpha"), Alpha))
		{
			Setting.TranslationAlpha = Alpha;
		}

		// EnableFK
		bool bEnableFK;
		if ((*SettingObj)->TryGetBoolField(TEXT("enable_fk"), bEnableFK))
		{
			Setting.EnableFK = bEnableFK;
		}

		Modified++;
	}

	// Apply settings back through the controller for proper persistence
	if (FKController)
	{
		FKController->SetSettings(*FKSettings);
	}

	Retargeter->MarkPackageDirty();

	return SuccessResult(FString::Printf(TEXT("Configured FK settings for %d chains"), Modified));
}

// ============================================================================
// FBX Import
// ============================================================================

TSharedPtr<FJsonObject> FRetargetEditor::ImportFBX(const FString& FbxPath, const FString& DestPath,
	const FString& SkeletonPath, bool bImportMesh, int32 CustomSampleRate, bool bSnapToClosestFrameBoundary)
{
	// Verify FBX file exists
	if (!FPaths::FileExists(FbxPath))
	{
		return ErrorResult(FString::Printf(TEXT("FBX file not found: %s"), *FbxPath));
	}

	// Load skeleton
	FString SkelError;
	USkeleton* Skeleton = LoadSkeletonFromPath(SkeletonPath, SkelError);
	if (!Skeleton)
	{
		return ErrorResult(SkelError);
	}

	// Setup import task
	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = FbxPath;
	Task->DestinationPath = DestPath;
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = true;

	// FBX import options
	UFbxImportUI* Options = NewObject<UFbxImportUI>();
	Options->Skeleton = Skeleton;
	Options->bImportMesh = bImportMesh;
	Options->bImportAnimations = true;
	Options->bImportMaterials = false;
	Options->bImportTextures = false;
	Options->MeshTypeToImport = bImportMesh ? FBXIT_SkeletalMesh : FBXIT_Animation;

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

TSharedPtr<FJsonObject> FRetargetEditor::BatchImportFBX(const FString& FbxDirectory, const FString& DestPath,
	const FString& SkeletonPath, const FString& FilePattern, int32 CustomSampleRate, bool bSnapToClosestFrameBoundary)
{
	// Find FBX files
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *FbxDirectory, *FString(TEXT("fbx")));

	if (FoundFiles.Num() == 0)
	{
		return ErrorResult(FString::Printf(TEXT("No FBX files found in: %s"), *FbxDirectory));
	}

	int32 SuccessCount = 0;
	int32 FailCount = 0;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (const FString& FileName : FoundFiles)
	{
		FString FullPath = FPaths::Combine(FbxDirectory, FileName);
		TSharedPtr<FJsonObject> ImportResult = ImportFBX(FullPath, DestPath, SkeletonPath, false, CustomSampleRate, bSnapToClosestFrameBoundary);

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
// Batch Retarget
// ============================================================================

TSharedPtr<FJsonObject> FRetargetEditor::BatchRetarget(const FString& RetargeterPath,
	const TArray<TSharedPtr<FJsonValue>>& AnimPaths,
	const FString& SourceMeshPath, const FString& TargetMeshPath,
	const FString& Prefix,
	bool bAutoRootMotion, const FString& RootMotionPattern)
{
	// Load retargeter
	FString LoadError;
	UIKRetargeter* Retargeter = LoadRetargeter(RetargeterPath, LoadError);
	if (!Retargeter) return ErrorResult(LoadError);

	// Load meshes
	FString MeshError;
	USkeletalMesh* SourceMesh = LoadSkeletalMeshFromPath(SourceMeshPath, MeshError);
	if (!SourceMesh) return ErrorResult(MeshError);
	USkeletalMesh* TargetMesh = LoadSkeletalMeshFromPath(TargetMeshPath, MeshError);
	if (!TargetMesh) return ErrorResult(MeshError);

	// Build asset list
	TArray<FAssetData> AssetsToRetarget;
	for (const TSharedPtr<FJsonValue>& PathVal : AnimPaths)
	{
		if (!PathVal.IsValid()) continue;
		FString AnimPath = PathVal->AsString();
		if (AnimPath.IsEmpty()) continue;

		FString AnimError;
		UAnimSequence* Anim = LoadAnimSequence(AnimPath, AnimError);
		if (Anim)
		{
			AssetsToRetarget.Add(FAssetData(Anim));
		}
	}

	if (AssetsToRetarget.Num() == 0)
	{
		return ErrorResult(TEXT("No valid animation assets found to retarget"));
	}

	// Run DuplicateAndRetarget
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	TArray<FAssetData> NewAssets = UIKRetargetBatchOperation::DuplicateAndRetarget(
		AssetsToRetarget,
		SourceMesh,
		TargetMesh,
		Retargeter,
		TEXT(""),       // Search
		TEXT(""),       // Replace
		Prefix,         // Prefix
		TEXT(""),       // Suffix
		false,          // bIncludeReferencedAssets
		true            // bOverwriteExistingFiles
	);
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 6
	TArray<FAssetData> NewAssets = UIKRetargetBatchOperation::DuplicateAndRetarget(
		AssetsToRetarget,
		SourceMesh,
		TargetMesh,
		Retargeter,
		TEXT(""),       // Search
		TEXT(""),       // Replace
		Prefix,         // Prefix
		TEXT(""),       // Suffix
		false           // bIncludeReferencedAssets
	);
#endif

	// Process results
	int32 RootMotionCount = 0;
	TArray<TSharedPtr<FJsonValue>> NewAssetsArray;
	for (const FAssetData& NewAsset : NewAssets)
	{
		TSharedPtr<FJsonObject> AssetJson = MakeShared<FJsonObject>();
		AssetJson->SetStringField(TEXT("name"), NewAsset.AssetName.ToString());
		AssetJson->SetStringField(TEXT("path"), NewAsset.GetObjectPathString());

		// Pitfall #4: Auto-enable root motion based on filename pattern
		if (bAutoRootMotion && IsRootMotionAnim(NewAsset.AssetName.ToString(), RootMotionPattern))
		{
			UAnimSequence* NewAnim = Cast<UAnimSequence>(NewAsset.GetAsset());
			if (NewAnim)
			{
				NewAnim->bEnableRootMotion = true;
				NewAnim->MarkPackageDirty();
				AssetJson->SetBoolField(TEXT("root_motion_enabled"), true);
				RootMotionCount++;
			}
		}

		NewAssetsArray.Add(MakeShared<FJsonValueObject>(AssetJson));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Retargeted %d animations (%d with root motion enabled)"),
		NewAssets.Num(), RootMotionCount));
	Result->SetNumberField(TEXT("retargeted_count"), NewAssets.Num());
	Result->SetNumberField(TEXT("root_motion_count"), RootMotionCount);
	Result->SetArrayField(TEXT("new_assets"), NewAssetsArray);

	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::SetRootMotion(const FString& AnimPath, bool bEnable)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AnimPath, LoadError);
	if (!Anim) return ErrorResult(LoadError);

	Anim->Modify();
	Anim->bEnableRootMotion = bEnable;
	Anim->bForceRootLock = !bEnable;
	FPropertyChangedEvent Evt(nullptr);
	Anim->PostEditChangeProperty(Evt);
	Anim->MarkPackageDirty();

	FString State = bEnable ? TEXT("enabled") : TEXT("disabled");
	return SuccessResult(FString::Printf(TEXT("Root motion %s on %s"), *State, *Anim->GetName()));
}

TSharedPtr<FJsonObject> FRetargetEditor::FindAnims(const FString& FolderPath, bool bRecursive)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*FolderPath));
	Filter.bRecursivePaths = bRecursive;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Assets.Num());

	TArray<TSharedPtr<FJsonValue>> AnimsArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AnimJson = MakeShared<FJsonObject>();
		AnimJson->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AnimJson->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AnimsArray.Add(MakeShared<FJsonValueObject>(AnimJson));
	}

	Result->SetArrayField(TEXT("animations"), AnimsArray);
	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::SaveAsset(const FString& AssetPath)
{
	bool bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, false);
	if (bSaved)
	{
		return SuccessResult(FString::Printf(TEXT("Saved: %s"), *AssetPath));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to save: %s"), *AssetPath));
}

// ============================================================================
// Inspection
// ============================================================================

TSharedPtr<FJsonObject> FRetargetEditor::InspectAnim(const FString& AnimPath)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AnimPath, LoadError);
	if (!Anim) return ErrorResult(LoadError);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Anim->GetName());
	Result->SetStringField(TEXT("path"), Anim->GetPathName());
	Result->SetNumberField(TEXT("length"), Anim->GetPlayLength());
	Result->SetNumberField(TEXT("num_frames"), Anim->GetNumberOfSampledKeys());
	Result->SetNumberField(TEXT("rate_scale"), Anim->RateScale);
	Result->SetBoolField(TEXT("root_motion"), Anim->bEnableRootMotion);
	Result->SetBoolField(TEXT("force_root_lock"), Anim->bForceRootLock);

	USkeleton* Skeleton = Anim->GetSkeleton();
	if (Skeleton)
	{
		Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
		Result->SetNumberField(TEXT("bone_count"), Skeleton->GetReferenceSkeleton().GetNum());
	}

	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::CompareBones(const FString& SourceAnimPath,
	const FString& TargetAnimPath, const TArray<FString>& BoneNames,
	const TArray<float>& SampleTimes)
{
	FString SrcError, TgtError;
	UAnimSequence* SourceAnim = LoadAnimSequence(SourceAnimPath, SrcError);
	if (!SourceAnim) return ErrorResult(SrcError);
	UAnimSequence* TargetAnim = LoadAnimSequence(TargetAnimPath, TgtError);
	if (!TargetAnim) return ErrorResult(TgtError);

	USkeleton* SourceSkel = SourceAnim->GetSkeleton();
	USkeleton* TargetSkel = TargetAnim->GetSkeleton();
	if (!SourceSkel || !TargetSkel)
	{
		return ErrorResult(TEXT("Could not get skeletons from animations"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("source_anim"), SourceAnim->GetName());
	Result->SetStringField(TEXT("target_anim"), TargetAnim->GetName());

	TArray<TSharedPtr<FJsonValue>> ComparisonsArray;

	for (const FString& BoneName : BoneNames)
	{
		TSharedPtr<FJsonObject> BoneResult = MakeShared<FJsonObject>();
		BoneResult->SetStringField(TEXT("bone"), BoneName);

		int32 SourceIdx = SourceSkel->GetReferenceSkeleton().FindBoneIndex(FName(*BoneName));
		int32 TargetIdx = TargetSkel->GetReferenceSkeleton().FindBoneIndex(FName(*BoneName));

		BoneResult->SetBoolField(TEXT("found_in_source"), SourceIdx != INDEX_NONE);
		BoneResult->SetBoolField(TEXT("found_in_target"), TargetIdx != INDEX_NONE);

		ComparisonsArray.Add(MakeShared<FJsonValueObject>(BoneResult));
	}

	Result->SetArrayField(TEXT("bones"), ComparisonsArray);
	return Result;
}

// ============================================================================
// Animation Analysis
// ============================================================================

TSharedPtr<FJsonObject> FRetargetEditor::SampleBones(const FString& AnimPath,
	const TArray<int32>& Frames, const TArray<FString>& BoneNames)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AnimPath, LoadError);
	if (!Anim) return ErrorResult(LoadError);

	const IAnimationDataModel* DataModel = Anim->GetDataModel();
	if (!DataModel)
	{
		return ErrorResult(TEXT("Animation has no data model"));
	}

	int32 NumBoneTracks = DataModel->GetNumBoneTracks();
	if (NumBoneTracks == 0)
	{
		return ErrorResult(TEXT("Animation has no bone tracks"));
	}

	// Get all bone track names via modern API
	TArray<FName> AllTrackNames;
	DataModel->GetBoneTrackNames(AllTrackNames);

	// Build bone name filter set (case-insensitive)
	TSet<FString> BoneFilter;
	for (const FString& Name : BoneNames)
	{
		BoneFilter.Add(Name.ToLower());
	}
	bool bFilterBones = BoneFilter.Num() > 0;

	// Get skeleton for bone indices
	USkeleton* Skeleton = Anim->GetSkeleton();
	const FReferenceSkeleton* RefSkel = Skeleton ? &Skeleton->GetReferenceSkeleton() : nullptr;

	int32 NumFrames = Anim->GetNumberOfSampledKeys();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animation"), Anim->GetName());
	Result->SetNumberField(TEXT("total_frames"), NumFrames);
	Result->SetNumberField(TEXT("total_tracks"), NumBoneTracks);

	TArray<TSharedPtr<FJsonValue>> FramesArray;

	for (int32 RequestedFrame : Frames)
	{
		if (RequestedFrame < 0 || RequestedFrame >= NumFrames)
		{
			continue;
		}

		TSharedPtr<FJsonObject> FrameJson = MakeShared<FJsonObject>();
		FrameJson->SetNumberField(TEXT("frame"), RequestedFrame);

		TArray<TSharedPtr<FJsonValue>> BonesArray;
		FFrameNumber FrameNumber(RequestedFrame);

		for (const FName& TrackName : AllTrackNames)
		{
			if (bFilterBones && !BoneFilter.Contains(TrackName.ToString().ToLower()))
			{
				continue;
			}

			FTransform BoneTransform = DataModel->GetBoneTrackTransform(TrackName, FrameNumber);

			TSharedPtr<FJsonObject> BoneJson = MakeShared<FJsonObject>();
			BoneJson->SetStringField(TEXT("bone"), TrackName.ToString());

			if (RefSkel)
			{
				BoneJson->SetNumberField(TEXT("bone_index"), RefSkel->FindBoneIndex(TrackName));
			}

			// Position
			FVector Pos = BoneTransform.GetLocation();
			TSharedPtr<FJsonObject> PosJson = MakeShared<FJsonObject>();
			PosJson->SetNumberField(TEXT("x"), Pos.X);
			PosJson->SetNumberField(TEXT("y"), Pos.Y);
			PosJson->SetNumberField(TEXT("z"), Pos.Z);
			BoneJson->SetObjectField(TEXT("position"), PosJson);

			// Rotation (quaternion)
			FQuat Rot = BoneTransform.GetRotation();
			TSharedPtr<FJsonObject> RotJson = MakeShared<FJsonObject>();
			RotJson->SetNumberField(TEXT("x"), Rot.X);
			RotJson->SetNumberField(TEXT("y"), Rot.Y);
			RotJson->SetNumberField(TEXT("z"), Rot.Z);
			RotJson->SetNumberField(TEXT("w"), Rot.W);
			BoneJson->SetObjectField(TEXT("rotation"), RotJson);

			// Scale
			FVector Scale = BoneTransform.GetScale3D();
			TSharedPtr<FJsonObject> ScaleJson = MakeShared<FJsonObject>();
			ScaleJson->SetNumberField(TEXT("x"), Scale.X);
			ScaleJson->SetNumberField(TEXT("y"), Scale.Y);
			ScaleJson->SetNumberField(TEXT("z"), Scale.Z);
			BoneJson->SetObjectField(TEXT("scale"), ScaleJson);

			BonesArray.Add(MakeShared<FJsonValueObject>(BoneJson));
		}

		FrameJson->SetArrayField(TEXT("bones"), BonesArray);
		FramesArray.Add(MakeShared<FJsonValueObject>(FrameJson));
	}

	Result->SetArrayField(TEXT("frames"), FramesArray);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Sampled %d frames, %d tracks"), FramesArray.Num(), NumBoneTracks));
	return Result;
}

TSharedPtr<FJsonObject> FRetargetEditor::DiagnoseAnim(const FString& AnimPath,
	float PopThreshold, float RotationFlipDotThreshold)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AnimPath, LoadError);
	if (!Anim) return ErrorResult(LoadError);

	const IAnimationDataModel* DataModel = Anim->GetDataModel();
	if (!DataModel)
	{
		return ErrorResult(TEXT("Animation has no data model"));
	}

	int32 NumBoneTracks = DataModel->GetNumBoneTracks();
	if (NumBoneTracks == 0)
	{
		return ErrorResult(TEXT("Animation has no bone tracks"));
	}

	// Get all bone track names
	TArray<FName> AllTrackNames;
	DataModel->GetBoneTrackNames(AllTrackNames);

	// Get root bone name from skeleton (index 0)
	USkeleton* Skeleton = Anim->GetSkeleton();
	FName RootBoneName = NAME_None;
	if (Skeleton)
	{
		RootBoneName = Skeleton->GetReferenceSkeleton().GetBoneName(0);
	}

	constexpr int32 MaxIssuesPerCategory = 50;
	constexpr float RootMotionMovementThresholdCm = 5.0f;

	TArray<TSharedPtr<FJsonValue>> Issues;
	int32 BonePopCount = 0;
	int32 QuatFlipCount = 0;
	int32 RootMotionMismatchCount = 0;

	// ===== Check 1: Root Motion Analysis =====
	TSharedPtr<FJsonObject> RootMotionJson = MakeShared<FJsonObject>();

	if (!RootBoneName.IsNone() && DataModel->IsValidBoneTrackName(RootBoneName))
	{
		RootMotionJson->SetStringField(TEXT("root_bone"), RootBoneName.ToString());
		RootMotionJson->SetBoolField(TEXT("root_motion_flag"), Anim->bEnableRootMotion);

		// Collect root bone position keys via IterateBoneKeys
		TArray<FVector3f> RootPosKeys;
		DataModel->IterateBoneKeys(RootBoneName,
			[&RootPosKeys](const FVector3f& Pos, const FQuat4f& /*Rot*/, const FVector3f /*Scale*/, const FFrameNumber& /*Frame*/)
			{
				RootPosKeys.Add(Pos);
				return true;
			});

		int32 NumPosKeys = RootPosKeys.Num();
		if (NumPosKeys > 0)
		{
			// Compute XY range
			float MinX = RootPosKeys[0].X, MaxX = RootPosKeys[0].X;
			float MinY = RootPosKeys[0].Y, MaxY = RootPosKeys[0].Y;

			for (int32 i = 1; i < NumPosKeys; ++i)
			{
				MinX = FMath::Min(MinX, RootPosKeys[i].X);
				MaxX = FMath::Max(MaxX, RootPosKeys[i].X);
				MinY = FMath::Min(MinY, RootPosKeys[i].Y);
				MaxY = FMath::Max(MaxY, RootPosKeys[i].Y);
			}

			float RangeX = MaxX - MinX;
			float RangeY = MaxY - MinY;
			float RangeXY = FMath::Sqrt(RangeX * RangeX + RangeY * RangeY);

			// Total displacement first->last
			const FVector3f& First = RootPosKeys[0];
			const FVector3f& Last = RootPosKeys[NumPosKeys - 1];
			float TotalDispXY = FMath::Sqrt(
				FMath::Square(Last.X - First.X) + FMath::Square(Last.Y - First.Y));

			RootMotionJson->SetNumberField(TEXT("total_displacement_xy"), TotalDispXY);
			RootMotionJson->SetNumberField(TEXT("range_xy"), RangeXY);
			RootMotionJson->SetNumberField(TEXT("pos_key_count"), NumPosKeys);

			// Trajectory samples (~10 evenly spaced including first and last)
			TArray<TSharedPtr<FJsonValue>> TrajSamples;
			int32 NumSamples = FMath::Min(10, NumPosKeys);
			for (int32 s = 0; s < NumSamples; ++s)
			{
				int32 Idx = (NumPosKeys > 1) ? (s * (NumPosKeys - 1)) / (NumSamples - 1) : 0;
				const FVector3f& P = RootPosKeys[Idx];
				TSharedPtr<FJsonObject> Sample = MakeShared<FJsonObject>();
				Sample->SetNumberField(TEXT("frame"), Idx);
				Sample->SetNumberField(TEXT("x"), P.X);
				Sample->SetNumberField(TEXT("y"), P.Y);
				Sample->SetNumberField(TEXT("z"), P.Z);
				TrajSamples.Add(MakeShared<FJsonValueObject>(Sample));
			}
			RootMotionJson->SetArrayField(TEXT("trajectory_samples"), TrajSamples);

			// Check for mismatch
			bool bHasMovement = RangeXY > RootMotionMovementThresholdCm;
			bool bFlagOn = Anim->bEnableRootMotion;

			if (bHasMovement && !bFlagOn)
			{
				TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
				Issue->SetStringField(TEXT("type"), TEXT("root_motion_mismatch"));
				Issue->SetStringField(TEXT("detail"),
					TEXT("Root bone has significant XY movement but bEnableRootMotion is OFF — character will slide"));
				Issue->SetNumberField(TEXT("range_xy"), RangeXY);
				Issues.Add(MakeShared<FJsonValueObject>(Issue));
				RootMotionMismatchCount++;
			}
			else if (!bHasMovement && bFlagOn)
			{
				TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
				Issue->SetStringField(TEXT("type"), TEXT("root_motion_mismatch"));
				Issue->SetStringField(TEXT("detail"),
					TEXT("bEnableRootMotion is ON but root bone has no significant XY movement"));
				Issue->SetNumberField(TEXT("range_xy"), RangeXY);
				Issues.Add(MakeShared<FJsonValueObject>(Issue));
				RootMotionMismatchCount++;
			}
		}
	}
	else
	{
		RootMotionJson->SetStringField(TEXT("root_bone"), TEXT("NOT_FOUND"));
		RootMotionJson->SetBoolField(TEXT("root_motion_flag"), Anim->bEnableRootMotion);
	}

	// ===== Check 2 & 3: Bone Pop + Quaternion Flip Detection =====
	for (const FName& TrackName : AllTrackNames)
	{
		// Collect all keys for this bone via IterateBoneKeys
		TArray<FVector3f> PosKeys;
		TArray<FQuat4f> RotKeys;

		DataModel->IterateBoneKeys(TrackName,
			[&PosKeys, &RotKeys](const FVector3f& Pos, const FQuat4f& Rot, const FVector3f /*Scale*/, const FFrameNumber& /*Frame*/)
			{
				PosKeys.Add(Pos);
				RotKeys.Add(Rot);
				return true;
			});

		FString BoneNameStr = TrackName.ToString();

		// Pop detection
		if (PosKeys.Num() > 1)
		{
			for (int32 i = 1; i < PosKeys.Num(); ++i)
			{
				float DeltaX = PosKeys[i].X - PosKeys[i - 1].X;
				float DeltaY = PosKeys[i].Y - PosKeys[i - 1].Y;
				float DeltaZ = PosKeys[i].Z - PosKeys[i - 1].Z;
				float Dist = FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);

				if (Dist > PopThreshold)
				{
					if (BonePopCount < MaxIssuesPerCategory)
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("type"), TEXT("bone_pop"));
						Issue->SetStringField(TEXT("bone"), BoneNameStr);
						Issue->SetNumberField(TEXT("frame"), i);
						Issue->SetNumberField(TEXT("prev_frame"), i - 1);
						Issue->SetNumberField(TEXT("distance"), Dist);
						Issues.Add(MakeShared<FJsonValueObject>(Issue));
					}
					BonePopCount++;
				}
			}
		}

		// Quaternion flip detection
		if (RotKeys.Num() > 1)
		{
			for (int32 i = 1; i < RotKeys.Num(); ++i)
			{
				float Dot = RotKeys[i].X * RotKeys[i - 1].X
				          + RotKeys[i].Y * RotKeys[i - 1].Y
				          + RotKeys[i].Z * RotKeys[i - 1].Z
				          + RotKeys[i].W * RotKeys[i - 1].W;

				if (Dot < RotationFlipDotThreshold)
				{
					if (QuatFlipCount < MaxIssuesPerCategory)
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("type"), TEXT("quaternion_flip"));
						Issue->SetStringField(TEXT("bone"), BoneNameStr);
						Issue->SetNumberField(TEXT("frame"), i);
						Issue->SetNumberField(TEXT("prev_frame"), i - 1);
						Issue->SetNumberField(TEXT("dot_product"), Dot);
						Issues.Add(MakeShared<FJsonValueObject>(Issue));
					}
					QuatFlipCount++;
				}
			}
		}
	}

	// ===== Build Result =====
	int32 TotalIssues = BonePopCount + QuatFlipCount + RootMotionMismatchCount;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animation"), Anim->GetName());

	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("total_issues"), TotalIssues);
	Summary->SetNumberField(TEXT("bone_pops"), BonePopCount);
	Summary->SetNumberField(TEXT("quaternion_flips"), QuatFlipCount);
	Summary->SetNumberField(TEXT("root_motion_mismatches"), RootMotionMismatchCount);
	Result->SetObjectField(TEXT("summary"), Summary);

	TSharedPtr<FJsonObject> Thresholds = MakeShared<FJsonObject>();
	Thresholds->SetNumberField(TEXT("pop_threshold_cm"), PopThreshold);
	Thresholds->SetNumberField(TEXT("rotation_flip_dot_threshold"), RotationFlipDotThreshold);
	Thresholds->SetNumberField(TEXT("root_motion_movement_threshold_cm"), RootMotionMovementThresholdCm);
	Result->SetObjectField(TEXT("thresholds"), Thresholds);

	Result->SetObjectField(TEXT("root_motion"), RootMotionJson);
	Result->SetArrayField(TEXT("issues"), Issues);

	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Diagnosis complete: %d issues (%d pops, %d quat flips, %d root motion mismatches)"),
		TotalIssues, BonePopCount, QuatFlipCount, RootMotionMismatchCount));

	return Result;
}
