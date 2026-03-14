// Copyright Natali Caggiano. All Rights Reserved.

#include "AnimTrackEditor.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimSequenceHelpers.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

TSharedPtr<FJsonObject> FAnimTrackEditor::SuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FAnimTrackEditor::ErrorResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Message);
	return Result;
}

UAnimSequence* FAnimTrackEditor::LoadAnimSequence(const FString& Path, FString& OutError)
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

TSharedPtr<FJsonObject> FAnimTrackEditor::AdjustTrackSingle(
	const FString& AssetPath,
	const FName& BoneName,
	const FVector& LocationOffset,
	const FQuat& RotationOffsetQuat,
	const FVector& ScaleOverride,
	bool bHasScaleOverride,
	bool bSave)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AssetPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

	const IAnimationDataModel* DataModel = Anim->GetDataModel();
	if (!DataModel)
	{
		return ErrorResult(FString::Printf(TEXT("No data model: %s"), *AssetPath));
	}

	if (!DataModel->IsValidBoneTrackName(BoneName))
	{
		return ErrorResult(FString::Printf(TEXT("Bone track '%s' not found in %s"), *BoneName.ToString(), *AssetPath));
	}

	TArray<FTransform> Transforms;
	DataModel->GetBoneTrackTransforms(BoneName, Transforms);

	if (Transforms.Num() == 0)
	{
		return ErrorResult(FString::Printf(TEXT("Bone track '%s' has 0 keys in %s"), *BoneName.ToString(), *AssetPath));
	}

	bool bHasLocationOffset = !LocationOffset.IsNearlyZero();
	bool bHasRotationOffset = !RotationOffsetQuat.Equals(FQuat::Identity, KINDA_SMALL_NUMBER);

	TArray<FVector3f> PosKeys;
	TArray<FQuat4f> RotKeys;
	TArray<FVector3f> ScaleKeys;
	PosKeys.Reserve(Transforms.Num());
	RotKeys.Reserve(Transforms.Num());
	ScaleKeys.Reserve(Transforms.Num());

	for (const FTransform& T : Transforms)
	{
		FVector Pos = T.GetLocation();
		FQuat Rot = T.GetRotation();
		FVector Scale = T.GetScale3D();

		if (bHasLocationOffset)
		{
			Pos += LocationOffset;
		}
		if (bHasRotationOffset)
		{
			Rot = RotationOffsetQuat * Rot;
		}
		if (bHasScaleOverride)
		{
			Scale = ScaleOverride;
		}

		PosKeys.Add(FVector3f(Pos));
		RotKeys.Add(FQuat4f(Rot));
		ScaleKeys.Add(FVector3f(Scale));
	}

	Anim->Modify();

	IAnimationDataController& Controller = Anim->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Adjust Track")));
	bool bSet = Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys);
	Controller.CloseBracket();

	if (!bSet)
	{
		return ErrorResult(FString::Printf(TEXT("SetBoneTrackKeys failed for %s"), *AssetPath));
	}

	Anim->MarkPackageDirty();

	if (bSave)
	{
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Adjusted '%s' in %s (%d keys)"), *BoneName.ToString(), *Anim->GetName(), Transforms.Num()));
	Result->SetNumberField(TEXT("num_keys"), Transforms.Num());
	return Result;
}

TSharedPtr<FJsonObject> FAnimTrackEditor::AdjustTrack(
	const TArray<FString>& AssetPaths,
	const FString& BoneName,
	const FVector& LocationOffset,
	const FRotator& RotationOffset,
	const FVector& ScaleOverride,
	bool bHasScaleOverride,
	bool bSave)
{
	FName BoneFName(*BoneName);
	FQuat RotationOffsetQuat = RotationOffset.Quaternion();

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (const FString& Path : AssetPaths)
	{
		TSharedPtr<FJsonObject> SingleResult = AdjustTrackSingle(Path, BoneFName, LocationOffset, RotationOffsetQuat, ScaleOverride, bHasScaleOverride, bSave);

		bool bSuccess = false;
		SingleResult->TryGetBoolField(TEXT("success"), bSuccess);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_path"), Path);
		Entry->SetBoolField(TEXT("success"), bSuccess);

		if (bSuccess)
		{
			FString Msg;
			SingleResult->TryGetStringField(TEXT("message"), Msg);
			Entry->SetStringField(TEXT("message"), Msg);
			SuccessCount++;
		}
		else
		{
			FString Err;
			SingleResult->TryGetStringField(TEXT("error"), Err);
			Entry->SetStringField(TEXT("error"), Err);
			FailCount++;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	FString Summary = FString::Printf(
		TEXT("Batch adjust: %d succeeded, %d failed out of %d"), SuccessCount, FailCount, AssetPaths.Num());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), SuccessCount > 0);
	Result->SetStringField(TEXT("message"), Summary);
	if (SuccessCount == 0)
	{
		Result->SetStringField(TEXT("error"), Summary);
	}
	Result->SetNumberField(TEXT("success_count"), SuccessCount);
	Result->SetNumberField(TEXT("fail_count"), FailCount);
	Result->SetNumberField(TEXT("total"), AssetPaths.Num());
	Result->SetArrayField(TEXT("results"), ResultsArray);
	return Result;
}

TSharedPtr<FJsonObject> FAnimTrackEditor::ResampleSingle(
	const FString& AssetPath,
	int32 TargetFPS,
	bool bSave)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AssetPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

	const IAnimationDataModel* DataModel = Anim->GetDataModel();
	if (!DataModel)
	{
		return ErrorResult(FString::Printf(TEXT("No data model: %s"), *AssetPath));
	}

	FFrameRate OldFrameRate = DataModel->GetFrameRate();
	int32 OldNumKeys = DataModel->GetNumberOfKeys();
	double PlayLength = Anim->GetPlayLength();

	if (OldFrameRate.Numerator == TargetFPS && OldFrameRate.Denominator == 1)
	{
		return SuccessResult(FString::Printf(
			TEXT("Skipped %s — already at %d FPS"), *Anim->GetName(), TargetFPS));
	}

	int32 NewNumFrames = FMath::RoundToInt32(PlayLength * TargetFPS);
	if (NewNumFrames < 1)
	{
		return ErrorResult(FString::Printf(TEXT("Target FPS %d produces 0 frames for %s (length %.4fs)"), TargetFPS, *AssetPath, PlayLength));
	}
	int32 NewNumKeys = NewNumFrames + 1;

	TArray<FName> BoneTrackNames;
	DataModel->GetBoneTrackNames(BoneTrackNames);

	if (BoneTrackNames.Num() == 0)
	{
		return ErrorResult(FString::Printf(TEXT("No bone tracks in %s"), *AssetPath));
	}

	struct FResampledTrack
	{
		FName BoneName;
		TArray<FVector> PosKeys;
		TArray<FQuat> RotKeys;
		TArray<FVector> ScaleKeys;
	};
	TArray<FResampledTrack> ResampledTracks;
	ResampledTracks.Reserve(BoneTrackNames.Num());

	for (const FName& BoneName : BoneTrackNames)
	{
		FResampledTrack Track;
		Track.BoneName = BoneName;
		Track.PosKeys.Reserve(NewNumKeys);
		Track.RotKeys.Reserve(NewNumKeys);
		Track.ScaleKeys.Reserve(NewNumKeys);

		for (int32 i = 0; i < NewNumKeys; ++i)
		{
			double Time = (NewNumFrames > 0) ? (static_cast<double>(i) * PlayLength / static_cast<double>(NewNumFrames)) : 0.0;
			FFrameTime FrameTime = OldFrameRate.AsFrameTime(Time);
			FTransform T = DataModel->EvaluateBoneTrackTransform(BoneName, FrameTime, EAnimInterpolationType::Linear);

			Track.PosKeys.Add(T.GetLocation());
			Track.RotKeys.Add(T.GetRotation());
			Track.ScaleKeys.Add(T.GetScale3D());
		}

		ResampledTracks.Add(MoveTemp(Track));
	}

	Anim->Modify();

	IAnimationDataController& Controller = Anim->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Resample Animation")));
	Controller.SetNumberOfFrames(FFrameNumber(NewNumFrames));

	bool bAllSet = true;
	for (const FResampledTrack& Track : ResampledTracks)
	{
		if (!Controller.SetBoneTrackKeys(Track.BoneName, Track.PosKeys, Track.RotKeys, Track.ScaleKeys))
		{
			bAllSet = false;
		}
	}

	Controller.CloseBracket();

	Anim->MarkPackageDirty();

	if (bSave)
	{
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Resampled %s: %d/%d FPS → %d FPS, %d → %d keys (%d bones, length %.4fs)"),
		*Anim->GetName(),
		OldFrameRate.Numerator, OldFrameRate.Denominator,
		TargetFPS,
		OldNumKeys, NewNumKeys,
		BoneTrackNames.Num(),
		PlayLength));
	Result->SetNumberField(TEXT("old_fps"), static_cast<double>(OldFrameRate.Numerator) / OldFrameRate.Denominator);
	Result->SetNumberField(TEXT("new_fps"), TargetFPS);
	Result->SetNumberField(TEXT("old_keys"), OldNumKeys);
	Result->SetNumberField(TEXT("new_keys"), NewNumKeys);
	Result->SetNumberField(TEXT("bone_count"), BoneTrackNames.Num());
	Result->SetNumberField(TEXT("play_length"), PlayLength);
	return Result;
}

TSharedPtr<FJsonObject> FAnimTrackEditor::Resample(
	const TArray<FString>& AssetPaths,
	int32 TargetFPS,
	bool bSave)
{
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (const FString& Path : AssetPaths)
	{
		TSharedPtr<FJsonObject> SingleResult = ResampleSingle(Path, TargetFPS, bSave);

		bool bSuccess = false;
		SingleResult->TryGetBoolField(TEXT("success"), bSuccess);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_path"), Path);
		Entry->SetBoolField(TEXT("success"), bSuccess);

		if (bSuccess)
		{
			FString Msg;
			SingleResult->TryGetStringField(TEXT("message"), Msg);
			Entry->SetStringField(TEXT("message"), Msg);
			if (SingleResult->HasField(TEXT("old_fps")))
			{
				Entry->SetNumberField(TEXT("old_fps"), SingleResult->GetNumberField(TEXT("old_fps")));
				Entry->SetNumberField(TEXT("new_fps"), SingleResult->GetNumberField(TEXT("new_fps")));
				Entry->SetNumberField(TEXT("old_keys"), SingleResult->GetNumberField(TEXT("old_keys")));
				Entry->SetNumberField(TEXT("new_keys"), SingleResult->GetNumberField(TEXT("new_keys")));
			}
			SuccessCount++;
		}
		else
		{
			FString Err;
			SingleResult->TryGetStringField(TEXT("error"), Err);
			Entry->SetStringField(TEXT("error"), Err);
			FailCount++;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	FString Summary = FString::Printf(
		TEXT("Resample to %d FPS: %d succeeded, %d failed out of %d"), TargetFPS, SuccessCount, FailCount, AssetPaths.Num());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), SuccessCount > 0);
	Result->SetStringField(TEXT("message"), Summary);
	if (SuccessCount == 0)
	{
		Result->SetStringField(TEXT("error"), Summary);
	}
	Result->SetNumberField(TEXT("success_count"), SuccessCount);
	Result->SetNumberField(TEXT("fail_count"), FailCount);
	Result->SetNumberField(TEXT("total"), AssetPaths.Num());
	Result->SetArrayField(TEXT("results"), ResultsArray);
	return Result;
}

TSharedPtr<FJsonObject> FAnimTrackEditor::InspectTrack(
	const FString& AssetPath,
	const FString& BoneName,
	const TArray<int32>& SampleFrames)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AssetPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

	const IAnimationDataModel* DataModel = Anim->GetDataModel();
	if (!DataModel)
	{
		return ErrorResult(TEXT("Animation has no data model"));
	}

	FName BoneFName(*BoneName);
	if (!DataModel->IsValidBoneTrackName(BoneFName))
	{
		return ErrorResult(FString::Printf(TEXT("Bone track '%s' not found"), *BoneName));
	}

	int32 NumFrames = DataModel->GetNumberOfKeys();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animation"), Anim->GetName());
	Result->SetStringField(TEXT("bone"), BoneName);
	Result->SetNumberField(TEXT("total_frames"), NumFrames);

	TArray<TSharedPtr<FJsonValue>> FramesArray;

	for (int32 RequestedFrame : SampleFrames)
	{
		int32 ActualFrame = RequestedFrame;
		if (ActualFrame < 0)
		{
			ActualFrame = NumFrames + ActualFrame;
		}

		if (ActualFrame < 0 || ActualFrame >= NumFrames)
		{
			continue;
		}

		FTransform T = DataModel->GetBoneTrackTransform(BoneFName, FFrameNumber(ActualFrame));

		FVector Pos = T.GetLocation();
		FQuat Rot = T.GetRotation();
		FVector Scale = T.GetScale3D();

		TSharedPtr<FJsonObject> FrameJson = MakeShared<FJsonObject>();
		FrameJson->SetNumberField(TEXT("frame"), ActualFrame);

		TSharedPtr<FJsonObject> PosJson = MakeShared<FJsonObject>();
		PosJson->SetNumberField(TEXT("x"), Pos.X);
		PosJson->SetNumberField(TEXT("y"), Pos.Y);
		PosJson->SetNumberField(TEXT("z"), Pos.Z);
		FrameJson->SetObjectField(TEXT("position"), PosJson);

		TSharedPtr<FJsonObject> RotJson = MakeShared<FJsonObject>();
		FRotator RotEuler = Rot.Rotator();
		RotJson->SetNumberField(TEXT("pitch"), RotEuler.Pitch);
		RotJson->SetNumberField(TEXT("yaw"), RotEuler.Yaw);
		RotJson->SetNumberField(TEXT("roll"), RotEuler.Roll);
		FrameJson->SetObjectField(TEXT("rotation"), RotJson);

		TSharedPtr<FJsonObject> RotQuatJson = MakeShared<FJsonObject>();
		RotQuatJson->SetNumberField(TEXT("x"), Rot.X);
		RotQuatJson->SetNumberField(TEXT("y"), Rot.Y);
		RotQuatJson->SetNumberField(TEXT("z"), Rot.Z);
		RotQuatJson->SetNumberField(TEXT("w"), Rot.W);
		FrameJson->SetObjectField(TEXT("rotation_quat"), RotQuatJson);

		TSharedPtr<FJsonObject> ScaleJson = MakeShared<FJsonObject>();
		ScaleJson->SetNumberField(TEXT("x"), Scale.X);
		ScaleJson->SetNumberField(TEXT("y"), Scale.Y);
		ScaleJson->SetNumberField(TEXT("z"), Scale.Z);
		FrameJson->SetObjectField(TEXT("scale"), ScaleJson);

		FramesArray.Add(MakeShared<FJsonValueObject>(FrameJson));
	}

	Result->SetArrayField(TEXT("frames"), FramesArray);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Inspected %d frames of '%s' in %s"), FramesArray.Num(), *BoneName, *Anim->GetName()));
	return Result;
}

TSharedPtr<FJsonObject> FAnimTrackEditor::ReplaceSkeletonSingle(
	const FString& AssetPath,
	USkeleton* NewSkeleton,
	bool bSave)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AssetPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

	FString OldSkeletonName = Anim->GetSkeleton()
		? Anim->GetSkeleton()->GetPathName()
		: TEXT("None");

	Anim->SetSkeleton(NewSkeleton);
	Anim->MarkPackageDirty();

	if (bSave)
	{
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Replaced skeleton on %s: %s -> %s"),
		*Anim->GetName(), *OldSkeletonName, *NewSkeleton->GetPathName()));
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("old_skeleton"), OldSkeletonName);
	Result->SetStringField(TEXT("new_skeleton"), NewSkeleton->GetPathName());
	return Result;
}

TSharedPtr<FJsonObject> FAnimTrackEditor::ReplaceSkeleton(
	const TArray<FString>& AssetPaths,
	const FString& SkeletonPath,
	bool bSave)
{
	USkeleton* NewSkeleton = Cast<USkeleton>(StaticLoadObject(USkeleton::StaticClass(), nullptr, *SkeletonPath));

	if (!NewSkeleton)
	{
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(StaticLoadObject(USkeletalMesh::StaticClass(), nullptr, *SkeletonPath));
		if (SkelMesh)
		{
			NewSkeleton = SkelMesh->GetSkeleton();
		}
	}

	if (!NewSkeleton)
	{
		return ErrorResult(FString::Printf(
			TEXT("Failed to load skeleton from path: %s (tried as USkeleton and USkeletalMesh)"), *SkeletonPath));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (const FString& Path : AssetPaths)
	{
		TSharedPtr<FJsonObject> SingleResult = ReplaceSkeletonSingle(Path, NewSkeleton, bSave);

		bool bSuccess = false;
		SingleResult->TryGetBoolField(TEXT("success"), bSuccess);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);
		Entry->SetBoolField(TEXT("success"), bSuccess);

		if (bSuccess)
		{
			FString OldSkel, NewSkel;
			SingleResult->TryGetStringField(TEXT("old_skeleton"), OldSkel);
			SingleResult->TryGetStringField(TEXT("new_skeleton"), NewSkel);
			Entry->SetStringField(TEXT("old_skeleton"), OldSkel);
			Entry->SetStringField(TEXT("new_skeleton"), NewSkel);
			SuccessCount++;
		}
		else
		{
			FString Err;
			SingleResult->TryGetStringField(TEXT("error"), Err);
			Entry->SetStringField(TEXT("error"), Err);
			FailCount++;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	FString Summary = FString::Printf(
		TEXT("Replace skeleton: %d succeeded, %d failed out of %d"), SuccessCount, FailCount, AssetPaths.Num());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), SuccessCount > 0);
	Result->SetStringField(TEXT("message"), Summary);
	if (SuccessCount == 0)
	{
		Result->SetStringField(TEXT("error"), Summary);
	}
	Result->SetNumberField(TEXT("success_count"), SuccessCount);
	Result->SetNumberField(TEXT("fail_count"), FailCount);
	Result->SetNumberField(TEXT("total"), AssetPaths.Num());
	Result->SetArrayField(TEXT("results"), ResultsArray);
	return Result;
}

// ===== Extract Range =====

TSharedPtr<FJsonObject> FAnimTrackEditor::ExtractRange(
	const FString& AssetPath,
	int32 StartFrame,
	int32 EndFrame,
	const FString& DestPath,
	const FString& NewName,
	bool bSave)
{
	FString LoadError;
	UAnimSequence* SourceAnim = LoadAnimSequence(AssetPath, LoadError);
	if (!SourceAnim)
	{
		return ErrorResult(LoadError);
	}

	const IAnimationDataModel* DataModel = SourceAnim->GetDataModel();
	if (!DataModel)
	{
		return ErrorResult(FString::Printf(TEXT("No data model: %s"), *AssetPath));
	}

	int32 TotalKeys = DataModel->GetNumberOfKeys();
	if (TotalKeys <= 0)
	{
		return ErrorResult(FString::Printf(TEXT("Animation has 0 keys: %s"), *AssetPath));
	}

	if (EndFrame < 0)
	{
		EndFrame = TotalKeys - 1;
	}

	if (StartFrame < 0 || StartFrame >= TotalKeys)
	{
		return ErrorResult(FString::Printf(TEXT("start_frame %d out of range [0, %d]"), StartFrame, TotalKeys - 1));
	}
	if (EndFrame < StartFrame || EndFrame >= TotalKeys)
	{
		return ErrorResult(FString::Printf(TEXT("end_frame %d out of range [%d, %d]"), EndFrame, StartFrame, TotalKeys - 1));
	}

	FString ActualDestPath = DestPath;
	if (ActualDestPath.IsEmpty())
	{
		ActualDestPath = FPackageName::GetLongPackagePath(AssetPath);
	}

	FString ActualNewName = NewName;
	if (ActualNewName.IsEmpty())
	{
		ActualNewName = FString::Printf(TEXT("%s_F%d_%d"), *SourceAnim->GetName(), StartFrame, EndFrame);
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* DuplicatedObj = AssetTools.DuplicateAsset(ActualNewName, ActualDestPath, SourceAnim);
	if (!DuplicatedObj)
	{
		return ErrorResult(FString::Printf(TEXT("DuplicateAsset failed: %s/%s"), *ActualDestPath, *ActualNewName));
	}

	UAnimSequence* NewAnim = Cast<UAnimSequence>(DuplicatedObj);
	if (!NewAnim)
	{
		return ErrorResult(TEXT("Duplicated asset is not an AnimSequence"));
	}

	if (EndFrame < TotalKeys - 1)
	{
		bool bTrimmed = UE::Anim::AnimationData::Trim(NewAnim,
			TRange<FFrameNumber>(FFrameNumber(EndFrame + 1), FFrameNumber(TotalKeys)));
		if (!bTrimmed)
		{
			return ErrorResult(TEXT("Trim tail failed"));
		}
	}

	if (StartFrame > 0)
	{
		bool bTrimmed = UE::Anim::AnimationData::Trim(NewAnim,
			TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(StartFrame)));
		if (!bTrimmed)
		{
			return ErrorResult(TEXT("Trim head failed"));
		}
	}

	NewAnim->MarkPackageDirty();

	FString NewAssetPath = FString::Printf(TEXT("%s/%s"), *ActualDestPath, *ActualNewName);
	if (bSave)
	{
		UEditorAssetLibrary::SaveAsset(NewAssetPath, false);
	}

	FFrameRate FrameRate = DataModel->GetFrameRate();
	int32 ExtractedFrameCount = EndFrame - StartFrame + 1;

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Extracted frames %d-%d from %s -> %s (%d frames)"),
		StartFrame, EndFrame, *SourceAnim->GetName(), *ActualNewName, ExtractedFrameCount));
	Result->SetStringField(TEXT("source_path"), AssetPath);
	Result->SetStringField(TEXT("new_asset_path"), NewAssetPath);
	Result->SetNumberField(TEXT("source_total_frames"), TotalKeys);
	Result->SetNumberField(TEXT("extracted_start_frame"), StartFrame);
	Result->SetNumberField(TEXT("extracted_end_frame"), EndFrame);
	Result->SetNumberField(TEXT("extracted_frame_count"), ExtractedFrameCount);
	Result->SetStringField(TEXT("frame_rate"), FString::Printf(TEXT("%dfps"), FrameRate.Numerator / FMath::Max(1, FrameRate.Denominator)));
	return Result;
}

// ===== Curve Operations =====

static FString InterpModeToString(ERichCurveInterpMode Mode)
{
	switch (Mode)
	{
	case RCIM_Linear:   return TEXT("Linear");
	case RCIM_Constant: return TEXT("Constant");
	case RCIM_Cubic:    return TEXT("Cubic");
	case RCIM_None:     return TEXT("None");
	default:            return TEXT("Linear");
	}
}

static FString TangentModeToString(ERichCurveTangentMode Mode)
{
	switch (Mode)
	{
	case RCTM_Auto:      return TEXT("Auto");
	case RCTM_User:      return TEXT("User");
	case RCTM_Break:     return TEXT("Break");
	case RCTM_SmartAuto: return TEXT("SmartAuto");
	case RCTM_None:      return TEXT("None");
	default:             return TEXT("Auto");
	}
}

TSharedPtr<FJsonObject> FAnimTrackEditor::GetCurves(const FString& AssetPath)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AssetPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const TArray<FFloatCurve>& FloatCurves = Anim->GetCurveData().FloatCurves;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	TArray<TSharedPtr<FJsonValue>> CurvesJson;
	for (const FFloatCurve& Curve : FloatCurves)
	{
		TSharedPtr<FJsonObject> CurveJson = MakeShared<FJsonObject>();
		CurveJson->SetStringField(TEXT("name"), Curve.GetName().ToString());

		TArray<TSharedPtr<FJsonValue>> KeysJson;
		for (const FRichCurveKey& Key : Curve.FloatCurve.GetConstRefOfKeys())
		{
			TSharedPtr<FJsonObject> KeyJson = MakeShared<FJsonObject>();
			KeyJson->SetNumberField(TEXT("time"), Key.Time);
			KeyJson->SetNumberField(TEXT("value"), Key.Value);
			KeyJson->SetStringField(TEXT("interp_mode"), InterpModeToString(Key.InterpMode));
			KeyJson->SetStringField(TEXT("tangent_mode"), TangentModeToString(Key.TangentMode));
			KeyJson->SetNumberField(TEXT("arrive_tangent"), Key.ArriveTangent);
			KeyJson->SetNumberField(TEXT("leave_tangent"), Key.LeaveTangent);
			KeysJson.Add(MakeShared<FJsonValueObject>(KeyJson));
		}
		CurveJson->SetArrayField(TEXT("keys"), KeysJson);
		CurvesJson.Add(MakeShared<FJsonValueObject>(CurveJson));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(
		TEXT("Found %d curves in %s"), CurvesJson.Num(), *Anim->GetName()));
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetArrayField(TEXT("curves"), CurvesJson);
	return Result;
}

TSharedPtr<FJsonObject> FAnimTrackEditor::AddCurve(const FString& AssetPath, const FName& CurveName,
	const TArray<FRichCurveKey>* InitialKeys, bool bSave)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AssetPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const FAnimCurveBase* Existing = Anim->GetCurveData().GetCurveData(CurveName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (Existing)
	{
		return ErrorResult(FString::Printf(TEXT("Curve '%s' already exists"), *CurveName.ToString()));
	}

	IAnimationDataController& Controller = Anim->GetController();
	FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
	Controller.AddCurve(CurveId, AACF_DefaultCurve, false);

	if (InitialKeys && InitialKeys->Num() > 0)
	{
		Controller.SetCurveKeys(CurveId, *InitialKeys, false);
	}

	Anim->PostEditChange();
	Anim->MarkPackageDirty();

	if (bSave)
	{
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
	}

	return SuccessResult(FString::Printf(TEXT("Added curve '%s' to %s"), *CurveName.ToString(), *Anim->GetName()));
}

TSharedPtr<FJsonObject> FAnimTrackEditor::RemoveCurve(const FString& AssetPath, const FName& CurveName, bool bSave)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AssetPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const FAnimCurveBase* Existing = Anim->GetCurveData().GetCurveData(CurveName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!Existing)
	{
		return ErrorResult(FString::Printf(TEXT("Curve '%s' not found"), *CurveName.ToString()));
	}

	IAnimationDataController& Controller = Anim->GetController();
	FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
	Controller.RemoveCurve(CurveId, false);

	Anim->PostEditChange();
	Anim->MarkPackageDirty();

	if (bSave)
	{
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
	}

	return SuccessResult(FString::Printf(TEXT("Removed curve '%s' from %s"), *CurveName.ToString(), *Anim->GetName()));
}

TSharedPtr<FJsonObject> FAnimTrackEditor::SetCurveKeys(const FString& AssetPath, const FName& CurveName,
	const TArray<FRichCurveKey>& Keys, bool bSave)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AssetPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const FAnimCurveBase* CurveBase = Anim->GetCurveData().GetCurveData(CurveName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!CurveBase)
	{
		return ErrorResult(FString::Printf(TEXT("Curve '%s' not found"), *CurveName.ToString()));
	}

	TArray<FRichCurveKey> SortedKeys = Keys;
	SortedKeys.Sort([](const FRichCurveKey& A, const FRichCurveKey& B) { return A.Time < B.Time; });

	IAnimationDataController& Controller = Anim->GetController();
	FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
	Controller.SetCurveKeys(CurveId, SortedKeys, false);

	Anim->PostEditChange();
	Anim->MarkPackageDirty();

	if (bSave)
	{
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
	}

	return SuccessResult(FString::Printf(TEXT("Set %d keys on curve '%s' in %s"),
		SortedKeys.Num(), *CurveName.ToString(), *Anim->GetName()));
}
