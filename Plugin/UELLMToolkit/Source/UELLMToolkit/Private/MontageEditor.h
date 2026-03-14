// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Curves/RichCurve.h"

class UAnimMontage;
class UAnimSequence;
class USkeleton;

class FMontageEditor
{
public:
	// ===== Creation & Persistence =====

	static UAnimMontage* CreateMontage(
		const FString& SkeletonPath,
		const FString& PackagePath,
		const FString& MontageName,
		FString& OutError);

	static bool SaveMontage(UAnimMontage* Montage, FString& OutError);

	// ===== Read / Inspect =====

	static TSharedPtr<FJsonObject> SerializeMontageInfo(UAnimMontage* Montage);

	// ===== Section Operations =====

	static bool AddSection(UAnimMontage* Montage, const FName& SectionName, float StartTime, FString& OutError);
	static bool RemoveSection(UAnimMontage* Montage, const FName& SectionName, FString& OutError);
	static bool SetSectionLink(UAnimMontage* Montage, const FName& SectionName, const FName& NextSectionName, FString& OutError);

	// ===== Segment Operations =====

	static bool AddSegment(
		UAnimMontage* Montage,
		int32 SlotIndex,
		const FString& AnimSequencePath,
		float StartPos,
		FString& OutError);

	static bool RemoveSegment(UAnimMontage* Montage, int32 SlotIndex, int32 SegmentIndex, FString& OutError);

	static bool SetSegmentProperties(
		UAnimMontage* Montage,
		int32 SlotIndex,
		int32 SegmentIndex,
		TOptional<float> PlayRate,
		TOptional<float> AnimStartTime,
		TOptional<float> AnimEndTime,
		TOptional<float> StartPos,
		const FString& AnimSequencePath,
		FString& OutError);

	// ===== Slot Operations =====

	static bool AddSlot(UAnimMontage* Montage, const FName& SlotName, FString& OutError);
	static bool RemoveSlot(UAnimMontage* Montage, int32 SlotIndex, FString& OutError);

	// ===== Notify Operations =====

	static bool AddNotify(
		UAnimMontage* Montage,
		const FString& NotifyName,
		float TriggerTime,
		int32 TrackIndex,
		const FString& NotifyClassPath,
		FString& OutError);

	static bool AddNotifyState(
		UAnimMontage* Montage,
		const FString& NotifyStateName,
		float StartTime,
		float Duration,
		int32 TrackIndex,
		const FString& NotifyStateClassPath,
		FString& OutError);

	static bool RemoveNotify(UAnimMontage* Montage, const FString& NotifyName, FString& OutError);
	static bool MoveNotify(UAnimMontage* Montage, int32 NotifyIndex, int32 NewTrackIndex, FString& OutError);

	static bool SetNotifyProperties(UAnimMontage* Montage, int32 NotifyIndex,
		const TSharedPtr<FJsonObject>& Properties, FString& OutError);
	static bool SetPropertiesOnObject(UObject* Object,
		const TSharedPtr<FJsonObject>& Properties, FString& OutError);

	// ===== Track Operations =====

	static bool RenameNotifyTrack(UAnimMontage* Montage, int32 TrackIndex, const FString& NewTrackName, FString& OutError);
	static int32 CleanupNotifyTracks(UAnimMontage* Montage);

	// ===== Blend Settings =====

	static bool SetBlendIn(UAnimMontage* Montage, float BlendTime, const FString& BlendOption, FString& OutError);
	static bool SetBlendOut(UAnimMontage* Montage, float BlendTime, const FString& BlendOption, FString& OutError);

	// ===== Curve Operations =====

	static TSharedPtr<FJsonObject> SerializeCurves(UAnimMontage* Montage);
	static bool AddCurve(UAnimMontage* Montage, const FName& CurveName,
		const TArray<FRichCurveKey>* InitialKeys, FString& OutError);
	static bool RemoveCurve(UAnimMontage* Montage, const FName& CurveName, FString& OutError);
	static bool SetCurveKeys(UAnimMontage* Montage, const FName& CurveName,
		const TArray<FRichCurveKey>& Keys, FString& OutError);

	static ERichCurveInterpMode ParseInterpMode(const FString& ModeStr);
	static ERichCurveTangentMode ParseTangentMode(const FString& ModeStr);

private:
	static void RecalculateSequenceLength(UAnimMontage* Montage);
	static EAlphaBlendOption ParseBlendOption(const FString& OptionStr);

	template<typename T>
	static UClass* ResolveNotifyClass(const FString& ClassPath, FString& OutError);
};
