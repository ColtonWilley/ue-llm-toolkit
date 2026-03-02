// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Curves/RichCurve.h"

class UAnimSequence;
class USkeleton;

/**
 * Utility class for animation track editing operations.
 * Provides batch adjust (offset position/rotation) and inspect (read keys).
 *
 * Uses UE5 IAnimationDataController API directly — no marketplace plugin dependency.
 * All methods are static, return JSON, and contain no MCP/JSON dispatch logic.
 */
class FAnimTrackEditor
{
public:
	/**
	 * Adjust a bone track across one or more animations (batch).
	 * Adds LocationOffset to every position key, pre-multiplies RotationOffset into every rotation key.
	 */
	static TSharedPtr<FJsonObject> AdjustTrack(
		const TArray<FString>& AssetPaths,
		const FString& BoneName,
		const FVector& LocationOffset,
		const FRotator& RotationOffset,
		bool bSave);

	/**
	 * Inspect a bone track: returns position/rotation/scale at sampled frames.
	 * Frame index -1 maps to last frame.
	 */
	static TSharedPtr<FJsonObject> InspectTrack(
		const FString& AssetPath,
		const FString& BoneName,
		const TArray<int32>& SampleFrames);

	/**
	 * Resample one or more animations to a target frame rate (batch).
	 * Samples all bone tracks at new frame positions using engine interpolation,
	 * then overwrites tracks and fixes metadata. Preserves animation length exactly.
	 */
	static TSharedPtr<FJsonObject> Resample(
		const TArray<FString>& AssetPaths,
		int32 TargetFPS,
		bool bSave);

	/**
	 * Replace the skeleton on one or more animation assets (batch).
	 * Accepts either a USkeleton or USkeletalMesh path — if a mesh is given,
	 * extracts its skeleton automatically.
	 */
	static TSharedPtr<FJsonObject> ReplaceSkeleton(
		const TArray<FString>& AssetPaths,
		const FString& SkeletonPath,
		bool bSave);

	// ===== Curve Operations =====

	static TSharedPtr<FJsonObject> GetCurves(const FString& AssetPath);
	static TSharedPtr<FJsonObject> AddCurve(const FString& AssetPath, const FName& CurveName,
		const TArray<FRichCurveKey>* InitialKeys, bool bSave);
	static TSharedPtr<FJsonObject> RemoveCurve(const FString& AssetPath, const FName& CurveName, bool bSave);
	static TSharedPtr<FJsonObject> SetCurveKeys(const FString& AssetPath, const FName& CurveName,
		const TArray<FRichCurveKey>& Keys, bool bSave);

private:
	static TSharedPtr<FJsonObject> AdjustTrackSingle(
		const FString& AssetPath,
		const FName& BoneName,
		const FVector& LocationOffset,
		const FQuat& RotationOffsetQuat,
		bool bSave);

	static TSharedPtr<FJsonObject> ResampleSingle(
		const FString& AssetPath,
		int32 TargetFPS,
		bool bSave);

	static TSharedPtr<FJsonObject> ReplaceSkeletonSingle(
		const FString& AssetPath,
		USkeleton* NewSkeleton,
		bool bSave);

	static TSharedPtr<FJsonObject> SuccessResult(const FString& Message);
	static TSharedPtr<FJsonObject> ErrorResult(const FString& Message);
	static UAnimSequence* LoadAnimSequence(const FString& Path, FString& OutError);
};
