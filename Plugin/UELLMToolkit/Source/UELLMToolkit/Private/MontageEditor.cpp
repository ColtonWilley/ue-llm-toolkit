// Copyright Natali Caggiano. All Rights Reserved.

#include "MontageEditor.h"
#include "AnimAssetManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/EnumProperty.h"
#include "Misc/PackageName.h"
#include "Factories/AnimMontageFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"

UAnimMontage* FMontageEditor::CreateMontage(
	const FString& SkeletonPath,
	const FString& PackagePath,
	const FString& MontageName,
	FString& OutError)
{
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("Failed to load skeleton: %s"), *SkeletonPath);
		return nullptr;
	}

	FString FullPackagePath = PackagePath / MontageName;
	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Failed to create package: %s"), *FullPackagePath);
		return nullptr;
	}

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = Skeleton;

	UAnimMontage* Montage = Cast<UAnimMontage>(
		Factory->FactoryCreateNew(
			UAnimMontage::StaticClass(),
			Package,
			FName(*MontageName),
			RF_Public | RF_Standalone,
			nullptr,
			GWarn));

	if (!Montage)
	{
		OutError = TEXT("FactoryCreateNew failed to create montage");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Montage);
	Package->MarkPackageDirty();

	return Montage;
}

bool FMontageEditor::SaveMontage(UAnimMontage* Montage, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	UPackage* Package = Montage->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Montage has no package");
		return false;
	}

	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		OutError = FString::Printf(TEXT("Failed to resolve filename for package: %s"), *Package->GetName());
		return false;
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	bool bSaved = UPackage::SavePackage(Package, Montage, *PackageFileName, SaveArgs);

	if (!bSaved)
	{
		OutError = FString::Printf(TEXT("SavePackage failed for: %s"), *PackageFileName);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FMontageEditor::SerializeMontageInfo(UAnimMontage* Montage)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	if (!Montage)
	{
		return Root;
	}

	Root->SetStringField(TEXT("name"), Montage->GetName());
	Root->SetStringField(TEXT("path"), Montage->GetPathName());
	Root->SetNumberField(TEXT("sequence_length"), Montage->GetPlayLength());

	if (USkeleton* Skel = Montage->GetSkeleton())
	{
		Root->SetStringField(TEXT("skeleton"), Skel->GetPathName());
	}

	Root->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
	Root->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());

	// Slot tracks
	TArray<TSharedPtr<FJsonValue>> SlotsJson;
	for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); ++SlotIdx)
	{
		const FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[SlotIdx];
		TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
		SlotJson->SetNumberField(TEXT("index"), SlotIdx);
		SlotJson->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());

		TArray<TSharedPtr<FJsonValue>> SegmentsJson;
		for (int32 SegIdx = 0; SegIdx < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegIdx)
		{
			const FAnimSegment& Seg = SlotTrack.AnimTrack.AnimSegments[SegIdx];
			TSharedPtr<FJsonObject> SegJson = MakeShared<FJsonObject>();
			SegJson->SetNumberField(TEXT("index"), SegIdx);
			if (Seg.GetAnimReference())
			{
				SegJson->SetStringField(TEXT("animation"), Seg.GetAnimReference()->GetPathName());
				SegJson->SetStringField(TEXT("animation_name"), Seg.GetAnimReference()->GetName());
			}
			SegJson->SetNumberField(TEXT("start_pos"), Seg.StartPos);
			SegJson->SetNumberField(TEXT("anim_start_time"), Seg.AnimStartTime);
			SegJson->SetNumberField(TEXT("anim_end_time"), Seg.AnimEndTime);
			SegJson->SetNumberField(TEXT("anim_play_rate"), Seg.AnimPlayRate);
			SegmentsJson.Add(MakeShared<FJsonValueObject>(SegJson));
		}
		SlotJson->SetArrayField(TEXT("segments"), SegmentsJson);
		SlotsJson.Add(MakeShared<FJsonValueObject>(SlotJson));
	}
	Root->SetArrayField(TEXT("slot_tracks"), SlotsJson);

	// Composite sections
	TArray<TSharedPtr<FJsonValue>> SectionsJson;
	for (int32 SecIdx = 0; SecIdx < Montage->CompositeSections.Num(); ++SecIdx)
	{
		const FCompositeSection& Section = Montage->CompositeSections[SecIdx];
		TSharedPtr<FJsonObject> SecJson = MakeShared<FJsonObject>();
		SecJson->SetNumberField(TEXT("index"), SecIdx);
		SecJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
		float SectionStart = Section.GetTime();
		SecJson->SetNumberField(TEXT("start_time"), SectionStart);

		float SectionEnd = Montage->GetPlayLength();
		if (SecIdx + 1 < Montage->CompositeSections.Num())
		{
			SectionEnd = Montage->CompositeSections[SecIdx + 1].GetTime();
		}
		SecJson->SetNumberField(TEXT("length"), SectionEnd - SectionStart);
		SecJson->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());

		SectionsJson.Add(MakeShared<FJsonValueObject>(SecJson));
	}
	Root->SetArrayField(TEXT("sections"), SectionsJson);

	// Notifies
	TArray<TSharedPtr<FJsonValue>> NotifiesJson;
	for (int32 NotifyIdx = 0; NotifyIdx < Montage->Notifies.Num(); ++NotifyIdx)
	{
		const FAnimNotifyEvent& Notify = Montage->Notifies[NotifyIdx];
		TSharedPtr<FJsonObject> NotifyJson = MakeShared<FJsonObject>();
		NotifyJson->SetNumberField(TEXT("index"), NotifyIdx);
		NotifyJson->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
		NotifyJson->SetNumberField(TEXT("trigger_time"), Notify.GetTriggerTime());
		NotifyJson->SetNumberField(TEXT("duration"), Notify.GetDuration());
		NotifyJson->SetNumberField(TEXT("track_index"), Notify.TrackIndex);

		if (Notify.Notify)
		{
			NotifyJson->SetStringField(TEXT("notify_class"), Notify.Notify->GetClass()->GetName());
		}
		else if (Notify.NotifyStateClass)
		{
			NotifyJson->SetStringField(TEXT("notify_state_class"), Notify.NotifyStateClass->GetClass()->GetName());
			NotifyJson->SetBoolField(TEXT("is_state"), true);
		}

		NotifiesJson.Add(MakeShared<FJsonValueObject>(NotifyJson));
	}
	Root->SetArrayField(TEXT("notifies"), NotifiesJson);

	// Notify tracks
	TArray<TSharedPtr<FJsonValue>> TracksJson;
	for (int32 TrackIdx = 0; TrackIdx < Montage->AnimNotifyTracks.Num(); ++TrackIdx)
	{
		TSharedPtr<FJsonObject> TrackJson = MakeShared<FJsonObject>();
		TrackJson->SetNumberField(TEXT("index"), TrackIdx);
		TrackJson->SetStringField(TEXT("track_name"), Montage->AnimNotifyTracks[TrackIdx].TrackName.ToString());
		TracksJson.Add(MakeShared<FJsonValueObject>(TrackJson));
	}
	Root->SetArrayField(TEXT("notify_tracks"), TracksJson);

	// Lightweight curve summary
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TArray<TSharedPtr<FJsonValue>> CurveSummaryJson;
	for (const FFloatCurve& Curve : Montage->GetCurveData().FloatCurves)
	{
		TSharedPtr<FJsonObject> CurveJson = MakeShared<FJsonObject>();
		CurveJson->SetStringField(TEXT("name"), Curve.GetName().ToString());
		CurveJson->SetNumberField(TEXT("key_count"), Curve.FloatCurve.GetNumKeys());
		CurveSummaryJson.Add(MakeShared<FJsonValueObject>(CurveJson));
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	Root->SetArrayField(TEXT("curves"), CurveSummaryJson);

	return Root;
}

bool FMontageEditor::AddSection(UAnimMontage* Montage, const FName& SectionName, float StartTime, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	int32 ExistingIdx = Montage->GetSectionIndex(SectionName);
	if (ExistingIdx != INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Section '%s' already exists at index %d"), *SectionName.ToString(), ExistingIdx);
		return false;
	}

	int32 NewIdx = Montage->AddAnimCompositeSection(SectionName, StartTime);
	if (NewIdx == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Failed to add section '%s'"), *SectionName.ToString());
		return false;
	}

	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::RemoveSection(UAnimMontage* Montage, const FName& SectionName, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	int32 SectionIdx = Montage->GetSectionIndex(SectionName);
	if (SectionIdx == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Section '%s' not found"), *SectionName.ToString());
		return false;
	}

	if (SectionIdx == 0)
	{
		OutError = TEXT("Cannot remove the first section (Default)");
		return false;
	}

	Montage->CompositeSections.RemoveAt(SectionIdx);
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::SetSectionLink(UAnimMontage* Montage, const FName& SectionName, const FName& NextSectionName, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	int32 SectionIdx = Montage->GetSectionIndex(SectionName);
	if (SectionIdx == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Section '%s' not found"), *SectionName.ToString());
		return false;
	}

	if (NextSectionName != NAME_None)
	{
		int32 NextIdx = Montage->GetSectionIndex(NextSectionName);
		if (NextIdx == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Next section '%s' not found"), *NextSectionName.ToString());
			return false;
		}
	}

	Montage->CompositeSections[SectionIdx].NextSectionName = NextSectionName;
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::AddSegment(
	UAnimMontage* Montage,
	int32 SlotIndex,
	const FString& AnimSequencePath,
	float StartPos,
	FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	if (SlotIndex < 0 || SlotIndex >= Montage->SlotAnimTracks.Num())
	{
		OutError = FString::Printf(TEXT("Slot index %d out of range (0-%d)"), SlotIndex, Montage->SlotAnimTracks.Num() - 1);
		return false;
	}

	FString LoadError;
	UAnimSequence* AnimSeq = FAnimAssetManager::LoadAnimSequence(AnimSequencePath, LoadError);
	if (!AnimSeq)
	{
		OutError = LoadError;
		return false;
	}

	USkeleton* MontageSkel = Montage->GetSkeleton();
	USkeleton* AnimSkel = AnimSeq->GetSkeleton();
	if (MontageSkel && AnimSkel && !MontageSkel->IsCompatibleForEditor(AnimSkel))
	{
		OutError = FString::Printf(TEXT("Animation skeleton '%s' is not compatible with montage skeleton '%s'"),
			*AnimSkel->GetName(), *MontageSkel->GetName());
		return false;
	}

	FAnimSegment NewSeg;
	NewSeg.SetAnimReference(AnimSeq);
	NewSeg.AnimStartTime = 0.0f;
	NewSeg.AnimEndTime = AnimSeq->GetPlayLength();
	NewSeg.AnimPlayRate = 1.0f;
	NewSeg.StartPos = StartPos;

	Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments.Add(NewSeg);

	RecalculateSequenceLength(Montage);
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::RemoveSegment(UAnimMontage* Montage, int32 SlotIndex, int32 SegmentIndex, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	if (SlotIndex < 0 || SlotIndex >= Montage->SlotAnimTracks.Num())
	{
		OutError = FString::Printf(TEXT("Slot index %d out of range"), SlotIndex);
		return false;
	}

	TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments;
	if (SegmentIndex < 0 || SegmentIndex >= Segments.Num())
	{
		OutError = FString::Printf(TEXT("Segment index %d out of range (0-%d)"), SegmentIndex, Segments.Num() - 1);
		return false;
	}

	Segments.RemoveAt(SegmentIndex);

	RecalculateSequenceLength(Montage);
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::SetSegmentProperties(
	UAnimMontage* Montage,
	int32 SlotIndex,
	int32 SegmentIndex,
	TOptional<float> PlayRate,
	TOptional<float> AnimStartTime,
	TOptional<float> AnimEndTime,
	TOptional<float> StartPos,
	const FString& AnimSequencePath,
	FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	if (SlotIndex < 0 || SlotIndex >= Montage->SlotAnimTracks.Num())
	{
		OutError = FString::Printf(TEXT("Slot index %d out of range"), SlotIndex);
		return false;
	}

	TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments;
	if (SegmentIndex < 0 || SegmentIndex >= Segments.Num())
	{
		OutError = FString::Printf(TEXT("Segment index %d out of range"), SegmentIndex);
		return false;
	}

	FAnimSegment& Seg = Segments[SegmentIndex];

	if (!AnimSequencePath.IsEmpty())
	{
		FString LoadError;
		UAnimSequence* AnimSeq = FAnimAssetManager::LoadAnimSequence(AnimSequencePath, LoadError);
		if (!AnimSeq)
		{
			OutError = LoadError;
			return false;
		}
		Seg.SetAnimReference(AnimSeq);
	}

	if (PlayRate.IsSet())
	{
		Seg.AnimPlayRate = PlayRate.GetValue();
	}
	if (AnimStartTime.IsSet())
	{
		Seg.AnimStartTime = AnimStartTime.GetValue();
	}
	if (AnimEndTime.IsSet())
	{
		Seg.AnimEndTime = AnimEndTime.GetValue();
	}
	if (StartPos.IsSet())
	{
		Seg.StartPos = StartPos.GetValue();
	}

	RecalculateSequenceLength(Montage);
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::AddSlot(UAnimMontage* Montage, const FName& SlotName, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (Track.SlotName == SlotName)
		{
			OutError = FString::Printf(TEXT("Slot '%s' already exists"), *SlotName.ToString());
			return false;
		}
	}

	FSlotAnimationTrack NewTrack;
	NewTrack.SlotName = SlotName;
	Montage->SlotAnimTracks.Add(NewTrack);

	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::RemoveSlot(UAnimMontage* Montage, int32 SlotIndex, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	if (Montage->SlotAnimTracks.Num() <= 1)
	{
		OutError = TEXT("Cannot remove the last slot track");
		return false;
	}

	if (SlotIndex < 0 || SlotIndex >= Montage->SlotAnimTracks.Num())
	{
		OutError = FString::Printf(TEXT("Slot index %d out of range"), SlotIndex);
		return false;
	}

	Montage->SlotAnimTracks.RemoveAt(SlotIndex);

	RecalculateSequenceLength(Montage);
	Montage->MarkPackageDirty();
	return true;
}

template<typename T>
UClass* FMontageEditor::ResolveNotifyClass(const FString& ClassPath, FString& OutError)
{
	UClass* FoundClass = LoadClass<T>(nullptr, *ClassPath);

	if (!FoundClass)
	{
		FoundClass = LoadClass<T>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassPath));
	}
	if (!FoundClass)
	{
		FoundClass = FindObject<UClass>(nullptr, *ClassPath);
		if (FoundClass && !FoundClass->IsChildOf(T::StaticClass()))
		{
			FoundClass = nullptr;
		}
	}

	if (!FoundClass)
	{
		OutError = FString::Printf(TEXT("Failed to load notify class: %s"), *ClassPath);
	}
	return FoundClass;
}

bool FMontageEditor::AddNotify(
	UAnimMontage* Montage,
	const FString& NotifyName,
	float TriggerTime,
	int32 TrackIndex,
	const FString& NotifyClassPath,
	FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	if (TriggerTime < 0.0f || TriggerTime > Montage->GetPlayLength())
	{
		OutError = FString::Printf(TEXT("Trigger time %.3f out of range (0-%.3f)"), TriggerTime, Montage->GetPlayLength());
		return false;
	}

	UAnimNotify* NotifyInstance = nullptr;
	if (!NotifyClassPath.IsEmpty())
	{
		UClass* NotifyClass = ResolveNotifyClass<UAnimNotify>(NotifyClassPath, OutError);
		if (!NotifyClass)
		{
			return false;
		}
		NotifyInstance = NewObject<UAnimNotify>(Montage, NotifyClass, NAME_None, RF_Transactional);
	}

	while (Montage->AnimNotifyTracks.Num() <= TrackIndex)
	{
		FAnimNotifyTrack NewTrack;
		NewTrack.TrackName = FName(*FString::Printf(TEXT("NotifyTrack_%d"), Montage->AnimNotifyTracks.Num()));
		Montage->AnimNotifyTracks.Add(NewTrack);
	}

	int32 NewNotifyIndex = Montage->Notifies.Add(FAnimNotifyEvent());
	FAnimNotifyEvent& NewNotify = Montage->Notifies[NewNotifyIndex];

	NewNotify.NotifyName = FName(*NotifyName);
	NewNotify.Guid = FGuid::NewGuid();
	NewNotify.Link(Montage, TriggerTime);
	NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(
		Montage->CalculateOffsetForNotify(TriggerTime));
	NewNotify.TrackIndex = TrackIndex;

	if (NotifyInstance)
	{
		NewNotify.Notify = NotifyInstance;
		NewNotify.TriggerWeightThreshold = NotifyInstance->GetDefaultTriggerWeightThreshold();
		NotifyInstance->OnAnimNotifyCreatedInEditor(NewNotify);
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::AddNotifyState(
	UAnimMontage* Montage,
	const FString& NotifyStateName,
	float StartTime,
	float Duration,
	int32 TrackIndex,
	const FString& NotifyStateClassPath,
	FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	float EndTime = StartTime + Duration;
	if (StartTime < 0.0f || EndTime > Montage->GetPlayLength())
	{
		OutError = FString::Printf(TEXT("Notify state range (%.3f-%.3f) exceeds montage length %.3f"),
			StartTime, EndTime, Montage->GetPlayLength());
		return false;
	}

	UAnimNotifyState* StateInstance = nullptr;
	if (!NotifyStateClassPath.IsEmpty())
	{
		UClass* StateClass = ResolveNotifyClass<UAnimNotifyState>(NotifyStateClassPath, OutError);
		if (!StateClass)
		{
			return false;
		}
		StateInstance = NewObject<UAnimNotifyState>(Montage, StateClass, NAME_None, RF_Transactional);
	}

	while (Montage->AnimNotifyTracks.Num() <= TrackIndex)
	{
		FAnimNotifyTrack NewTrack;
		NewTrack.TrackName = FName(*FString::Printf(TEXT("NotifyTrack_%d"), Montage->AnimNotifyTracks.Num()));
		Montage->AnimNotifyTracks.Add(NewTrack);
	}

	int32 NewNotifyIndex = Montage->Notifies.Add(FAnimNotifyEvent());
	FAnimNotifyEvent& NewNotify = Montage->Notifies[NewNotifyIndex];

	NewNotify.NotifyName = FName(*NotifyStateName);
	NewNotify.Guid = FGuid::NewGuid();
	NewNotify.Link(Montage, StartTime);
	NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(
		Montage->CalculateOffsetForNotify(StartTime));
	NewNotify.TrackIndex = TrackIndex;

	if (StateInstance)
	{
		NewNotify.NotifyStateClass = StateInstance;
		NewNotify.SetDuration(Duration);
		NewNotify.EndLink.Link(Montage, NewNotify.EndLink.GetTime());
		NewNotify.RefreshEndTriggerOffset(
			Montage->CalculateOffsetForNotify(NewNotify.EndLink.GetTime()));
		NewNotify.TriggerWeightThreshold = StateInstance->GetDefaultTriggerWeightThreshold();
		StateInstance->OnAnimNotifyCreatedInEditor(NewNotify);
	}
	else
	{
		NewNotify.SetDuration(Duration);
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::RemoveNotify(UAnimMontage* Montage, const FString& NotifyName, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	FName SearchName(*NotifyName);
	int32 FoundIdx = INDEX_NONE;
	for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
	{
		if (Montage->Notifies[i].NotifyName == SearchName)
		{
			FoundIdx = i;
			break;
		}
	}

	if (FoundIdx == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Notify '%s' not found"), *NotifyName);
		return false;
	}

	Montage->Notifies.RemoveAt(FoundIdx);
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::MoveNotify(UAnimMontage* Montage, int32 NotifyIndex, int32 NewTrackIndex, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	if (NotifyIndex < 0 || NotifyIndex >= Montage->Notifies.Num())
	{
		OutError = FString::Printf(TEXT("Notify index %d out of range (0-%d)"), NotifyIndex, Montage->Notifies.Num() - 1);
		return false;
	}

	if (NewTrackIndex < 0)
	{
		OutError = FString::Printf(TEXT("Track index %d is invalid"), NewTrackIndex);
		return false;
	}

	while (Montage->AnimNotifyTracks.Num() <= NewTrackIndex)
	{
		FAnimNotifyTrack NewTrack;
		NewTrack.TrackName = FName(*FString::Printf(TEXT("NotifyTrack_%d"), Montage->AnimNotifyTracks.Num()));
		Montage->AnimNotifyTracks.Add(NewTrack);
	}

	Montage->Notifies[NotifyIndex].TrackIndex = NewTrackIndex;
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::SetNotifyProperties(UAnimMontage* Montage, int32 NotifyIndex,
	const TSharedPtr<FJsonObject>& Properties, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	if (NotifyIndex < 0 || NotifyIndex >= Montage->Notifies.Num())
	{
		OutError = FString::Printf(TEXT("Notify index %d out of range (0-%d)"),
			NotifyIndex, Montage->Notifies.Num() - 1);
		return false;
	}

	FAnimNotifyEvent& Notify = Montage->Notifies[NotifyIndex];
	UObject* Target = Notify.NotifyStateClass
		? static_cast<UObject*>(Notify.NotifyStateClass)
		: static_cast<UObject*>(Notify.Notify);

	if (!Target)
	{
		OutError = FString::Printf(TEXT("Notify at index %d has no UObject (untyped notify)"), NotifyIndex);
		return false;
	}

	if (!SetPropertiesOnObject(Target, Properties, OutError))
	{
		return false;
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::SetPropertiesOnObject(UObject* Object,
	const TSharedPtr<FJsonObject>& Properties, FString& OutError)
{
	if (!Object || !Properties.IsValid())
	{
		OutError = TEXT("Null object or properties");
		return false;
	}

	UClass* ObjClass = Object->GetClass();

	for (const auto& Pair : Properties->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		if (Key == TEXT("_class"))
		{
			continue;
		}

		FProperty* Prop = ObjClass->FindPropertyByName(FName(*Key));
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *Key, *ObjClass->GetName());
			return false;
		}

		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Object);

		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			bool bVal = false;
			if (!Value->TryGetBool(bVal))
			{
				OutError = FString::Printf(TEXT("Property '%s' expects bool"), *Key);
				return false;
			}
			BoolProp->SetPropertyValue(PropAddr, bVal);
		}
		else if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			double NumVal = 0.0;
			if (!Value->TryGetNumber(NumVal))
			{
				OutError = FString::Printf(TEXT("Property '%s' expects number"), *Key);
				return false;
			}
			if (NumProp->IsFloatingPoint())
			{
				NumProp->SetFloatingPointPropertyValue(PropAddr, NumVal);
			}
			else
			{
				NumProp->SetIntPropertyValue(PropAddr, static_cast<int64>(NumVal));
			}
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			FString StrVal;
			if (!Value->TryGetString(StrVal))
			{
				OutError = FString::Printf(TEXT("Property '%s' expects string"), *Key);
				return false;
			}
			NameProp->SetPropertyValue(PropAddr, FName(*StrVal));
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			FString StrVal;
			if (!Value->TryGetString(StrVal))
			{
				OutError = FString::Printf(TEXT("Property '%s' expects string"), *Key);
				return false;
			}
			StrProp->SetPropertyValue(PropAddr, StrVal);
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			FString StrVal;
			if (!Value->TryGetString(StrVal))
			{
				OutError = FString::Printf(TEXT("Property '%s' expects string (enum name)"), *Key);
				return false;
			}
			UEnum* Enum = EnumProp->GetEnum();
			int64 EnumVal = Enum->GetValueByNameString(StrVal);
			if (EnumVal == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Enum value '%s' not found in %s"), *StrVal, *Enum->GetName());
				return false;
			}
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropAddr, EnumVal);
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				FString StrVal;
				if (!Value->TryGetString(StrVal))
				{
					OutError = FString::Printf(TEXT("Property '%s' expects string (enum name)"), *Key);
					return false;
				}
				int64 EnumVal = ByteProp->Enum->GetValueByNameString(StrVal);
				if (EnumVal == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Enum value '%s' not found in %s"), *StrVal, *ByteProp->Enum->GetName());
					return false;
				}
				ByteProp->SetIntPropertyValue(PropAddr, EnumVal);
			}
			else
			{
				double NumVal = 0.0;
				if (!Value->TryGetNumber(NumVal))
				{
					OutError = FString::Printf(TEXT("Property '%s' expects number"), *Key);
					return false;
				}
				ByteProp->SetIntPropertyValue(PropAddr, static_cast<int64>(NumVal));
			}
		}
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			const TSharedPtr<FJsonObject>* SubObj = nullptr;
			if (!Value->TryGetObject(SubObj) || !SubObj || !SubObj->IsValid())
			{
				OutError = FString::Printf(TEXT("Property '%s' expects object with _class"), *Key);
				return false;
			}

			FString ClassPath;
			if (!(*SubObj)->TryGetStringField(TEXT("_class"), ClassPath))
			{
				OutError = FString::Printf(TEXT("Property '%s' object missing _class"), *Key);
				return false;
			}

			UClass* SubClass = LoadClass<UObject>(nullptr, *ClassPath);
			if (!SubClass)
			{
				SubClass = FindObject<UClass>(nullptr, *ClassPath);
			}
			if (!SubClass)
			{
				OutError = FString::Printf(TEXT("Failed to load class: %s"), *ClassPath);
				return false;
			}

			if (!SubClass->IsChildOf(ObjProp->PropertyClass))
			{
				OutError = FString::Printf(TEXT("Class %s is not a subclass of %s"),
					*SubClass->GetName(), *ObjProp->PropertyClass->GetName());
				return false;
			}

			UObject* SubObject = NewObject<UObject>(Object, SubClass, NAME_None, RF_Transactional);
			ObjProp->SetObjectPropertyValue(PropAddr, SubObject);

			if (!SetPropertiesOnObject(SubObject, *SubObj, OutError))
			{
				return false;
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported property type '%s' for '%s'"),
				*Prop->GetCPPType(), *Key);
			return false;
		}
	}

	return true;
}

bool FMontageEditor::RenameNotifyTrack(UAnimMontage* Montage, int32 TrackIndex, const FString& NewTrackName, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	if (TrackIndex < 0 || TrackIndex >= Montage->AnimNotifyTracks.Num())
	{
		OutError = FString::Printf(TEXT("Track index %d out of range (0-%d)"), TrackIndex, Montage->AnimNotifyTracks.Num() - 1);
		return false;
	}

	Montage->AnimNotifyTracks[TrackIndex].TrackName = FName(*NewTrackName);
	Montage->MarkPackageDirty();
	return true;
}

int32 FMontageEditor::CleanupNotifyTracks(UAnimMontage* Montage)
{
	if (!Montage) return 0;

	int32 MaxUsedTrack = -1;
	for (const FAnimNotifyEvent& Notify : Montage->Notifies)
	{
		if (Notify.TrackIndex > MaxUsedTrack)
		{
			MaxUsedTrack = Notify.TrackIndex;
		}
	}

	int32 DesiredCount = MaxUsedTrack + 1;
	if (DesiredCount < 1) DesiredCount = 1;

	int32 OldCount = Montage->AnimNotifyTracks.Num();
	if (OldCount <= DesiredCount) return 0;

	int32 Removed = OldCount - DesiredCount;
	Montage->AnimNotifyTracks.SetNum(DesiredCount);
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return Removed;
}

bool FMontageEditor::SetBlendIn(UAnimMontage* Montage, float BlendTime, const FString& BlendOption, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	Montage->BlendIn.SetBlendTime(BlendTime);
	Montage->BlendIn.SetBlendOption(ParseBlendOption(BlendOption));
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::SetBlendOut(UAnimMontage* Montage, float BlendTime, const FString& BlendOption, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

	Montage->BlendOut.SetBlendTime(BlendTime);
	Montage->BlendOut.SetBlendOption(ParseBlendOption(BlendOption));
	Montage->MarkPackageDirty();
	return true;
}

void FMontageEditor::RecalculateSequenceLength(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}

	// Compute max end time across all segments
	float MaxEndTime = 0.0f;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Seg : Track.AnimTrack.AnimSegments)
		{
			const float PlayRate = FMath::Max(FMath::Abs(Seg.AnimPlayRate), KINDA_SMALL_NUMBER);
			const float SegEnd = Seg.StartPos + (Seg.AnimEndTime - Seg.AnimStartTime) / PlayRate;
			MaxEndTime = FMath::Max(MaxEndTime, SegEnd);
		}
	}

	// PostEditChange may recompute SequenceLength internally; override it after.
	// SequenceLength is protected, so use property reflection to set it.
	Montage->PostEditChange();
	if (FFloatProperty* LengthProp = CastField<FFloatProperty>(
		UAnimSequenceBase::StaticClass()->FindPropertyByName(TEXT("SequenceLength"))))
	{
		LengthProp->SetPropertyValue_InContainer(Montage, MaxEndTime);
	}
}

EAlphaBlendOption FMontageEditor::ParseBlendOption(const FString& OptionStr)
{
	if (OptionStr.Equals(TEXT("Linear"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::Linear;
	if (OptionStr.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::Cubic;
	if (OptionStr.Equals(TEXT("HermiteCubic"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::HermiteCubic;
	if (OptionStr.Equals(TEXT("Sinusoidal"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::Sinusoidal;
	if (OptionStr.Equals(TEXT("QuadraticInOut"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::QuadraticInOut;
	if (OptionStr.Equals(TEXT("CubicInOut"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::CubicInOut;
	if (OptionStr.Equals(TEXT("QuarticInOut"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::QuarticInOut;
	if (OptionStr.Equals(TEXT("QuinticInOut"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::QuinticInOut;
	if (OptionStr.Equals(TEXT("CircularIn"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::CircularIn;
	if (OptionStr.Equals(TEXT("CircularOut"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::CircularOut;
	if (OptionStr.Equals(TEXT("CircularInOut"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::CircularInOut;
	if (OptionStr.Equals(TEXT("ExpIn"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::ExpIn;
	if (OptionStr.Equals(TEXT("ExpOut"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::ExpOut;
	if (OptionStr.Equals(TEXT("ExpInOut"), ESearchCase::IgnoreCase)) return EAlphaBlendOption::ExpInOut;
	return EAlphaBlendOption::Linear;
}

ERichCurveInterpMode FMontageEditor::ParseInterpMode(const FString& ModeStr)
{
	if (ModeStr.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) return RCIM_Constant;
	if (ModeStr.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase)) return RCIM_Cubic;
	if (ModeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return RCIM_None;
	return RCIM_Linear;
}

ERichCurveTangentMode FMontageEditor::ParseTangentMode(const FString& ModeStr)
{
	if (ModeStr.Equals(TEXT("User"), ESearchCase::IgnoreCase)) return RCTM_User;
	if (ModeStr.Equals(TEXT("Break"), ESearchCase::IgnoreCase)) return RCTM_Break;
	if (ModeStr.Equals(TEXT("SmartAuto"), ESearchCase::IgnoreCase)) return RCTM_SmartAuto;
	if (ModeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return RCTM_None;
	return RCTM_Auto;
}

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

TSharedPtr<FJsonObject> FMontageEditor::SerializeCurves(UAnimMontage* Montage)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	if (!Montage)
	{
		return Root;
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const TArray<FFloatCurve>& FloatCurves = Montage->GetCurveData().FloatCurves;
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

	Root->SetStringField(TEXT("montage"), Montage->GetPathName());
	Root->SetArrayField(TEXT("curves"), CurvesJson);
	return Root;
}

bool FMontageEditor::AddCurve(UAnimMontage* Montage, const FName& CurveName,
	const TArray<FRichCurveKey>* InitialKeys, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const FAnimCurveBase* Existing = Montage->GetCurveData().GetCurveData(CurveName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (Existing)
	{
		OutError = FString::Printf(TEXT("Curve '%s' already exists"), *CurveName.ToString());
		return false;
	}

	IAnimationDataController& Controller = Montage->GetController();
	FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
	Controller.AddCurve(CurveId, AACF_DefaultCurve, false);

	if (InitialKeys && InitialKeys->Num() > 0)
	{
		Controller.SetCurveKeys(CurveId, *InitialKeys, false);
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::RemoveCurve(UAnimMontage* Montage, const FName& CurveName, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const FAnimCurveBase* Existing = Montage->GetCurveData().GetCurveData(CurveName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!Existing)
	{
		OutError = FString::Printf(TEXT("Curve '%s' not found"), *CurveName.ToString());
		return false;
	}

	IAnimationDataController& Controller = Montage->GetController();
	FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
	Controller.RemoveCurve(CurveId, false);

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}

bool FMontageEditor::SetCurveKeys(UAnimMontage* Montage, const FName& CurveName,
	const TArray<FRichCurveKey>& Keys, FString& OutError)
{
	if (!Montage)
	{
		OutError = TEXT("Montage is null");
		return false;
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const FAnimCurveBase* CurveBase = Montage->GetCurveData().GetCurveData(CurveName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!CurveBase)
	{
		OutError = FString::Printf(TEXT("Curve '%s' not found"), *CurveName.ToString());
		return false;
	}

	TArray<FRichCurveKey> SortedKeys = Keys;
	SortedKeys.Sort([](const FRichCurveKey& A, const FRichCurveKey& B) { return A.Time < B.Time; });

	IAnimationDataController& Controller = Montage->GetController();
	FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
	Controller.SetCurveKeys(CurveId, SortedKeys, false);

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}
