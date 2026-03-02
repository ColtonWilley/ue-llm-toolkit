// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_MontageModify.h"
#include "MontageEditor.h"
#include "AnimAssetManager.h"
#include "Animation/AnimMontage.h"
#include "Curves/RichCurve.h"
#include "Serialization/JsonSerializer.h"

TOptional<FMCPToolResult> FMCPTool_MontageModify::LoadMontageOrError(
	const FString& Path,
	UAnimMontage*& OutMontage)
{
	FString LoadError;
	OutMontage = FAnimAssetManager::LoadMontage(Path, LoadError);
	if (!OutMontage)
	{
		return FMCPToolResult::Error(LoadError);
	}
	return {};
}

FMCPToolResult FMCPTool_MontageModify::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	if (Operation == TEXT("create"))
	{
		return HandleCreate(Params);
	}

	FString MontagePath;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Error))
	{
		return Error.GetValue();
	}

	if (!ValidateBlueprintPathParam(MontagePath, Error))
	{
		return Error.GetValue();
	}

	if (Operation == TEXT("get_info"))
	{
		return HandleGetInfo(MontagePath);
	}
	else if (Operation == TEXT("save"))
	{
		return HandleSave(MontagePath);
	}
	else if (Operation == TEXT("add_section"))
	{
		return HandleAddSection(MontagePath, Params);
	}
	else if (Operation == TEXT("remove_section"))
	{
		return HandleRemoveSection(MontagePath, Params);
	}
	else if (Operation == TEXT("link_sections"))
	{
		return HandleLinkSections(MontagePath, Params);
	}
	else if (Operation == TEXT("add_segment"))
	{
		return HandleAddSegment(MontagePath, Params);
	}
	else if (Operation == TEXT("remove_segment"))
	{
		return HandleRemoveSegment(MontagePath, Params);
	}
	else if (Operation == TEXT("set_segment"))
	{
		return HandleSetSegment(MontagePath, Params);
	}
	else if (Operation == TEXT("add_slot"))
	{
		return HandleAddSlot(MontagePath, Params);
	}
	else if (Operation == TEXT("remove_slot"))
	{
		return HandleRemoveSlot(MontagePath, Params);
	}
	else if (Operation == TEXT("add_notify"))
	{
		return HandleAddNotify(MontagePath, Params);
	}
	else if (Operation == TEXT("add_notify_state"))
	{
		return HandleAddNotifyState(MontagePath, Params);
	}
	else if (Operation == TEXT("remove_notify"))
	{
		return HandleRemoveNotify(MontagePath, Params);
	}
	else if (Operation == TEXT("move_notify"))
	{
		return HandleMoveNotify(MontagePath, Params);
	}
	else if (Operation == TEXT("rename_track"))
	{
		return HandleRenameTrack(MontagePath, Params);
	}
	else if (Operation == TEXT("set_blend_in"))
	{
		return HandleSetBlendIn(MontagePath, Params);
	}
	else if (Operation == TEXT("set_blend_out"))
	{
		return HandleSetBlendOut(MontagePath, Params);
	}
	else if (Operation == TEXT("get_curves"))
	{
		return HandleGetCurves(MontagePath);
	}
	else if (Operation == TEXT("add_curve"))
	{
		return HandleAddCurve(MontagePath, Params);
	}
	else if (Operation == TEXT("remove_curve"))
	{
		return HandleRemoveCurve(MontagePath, Params);
	}
	else if (Operation == TEXT("set_curve_keys"))
	{
		return HandleSetCurveKeys(MontagePath, Params);
	}

	return FMCPToolResult::Error(FString::Printf(TEXT("Unknown operation: '%s'"), *Operation));
}

FMCPToolResult FMCPTool_MontageModify::HandleGetInfo(const FString& MontagePath)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Info = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(TEXT("Montage info retrieved"), Info);
}

FMCPToolResult FMCPTool_MontageModify::HandleCreate(const TSharedRef<FJsonObject>& Params)
{
	TOptional<FMCPToolResult> Error;

	FString SkeletonPath;
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Error))
	{
		return Error.GetValue();
	}

	FString PackagePath;
	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Error))
	{
		return Error.GetValue();
	}

	FString MontageName;
	if (!ExtractRequiredString(Params, TEXT("montage_name"), MontageName, Error))
	{
		return Error.GetValue();
	}

	FString CreateError;
	UAnimMontage* Montage = FMontageEditor::CreateMontage(SkeletonPath, PackagePath, MontageName, CreateError);
	if (!Montage)
	{
		return FMCPToolResult::Error(CreateError);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), Montage->GetPathName());
	Result->SetStringField(TEXT("name"), Montage->GetName());
	return FMCPToolResult::Success(FString::Printf(TEXT("Created montage: %s"), *Montage->GetPathName()), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleSave(const FString& MontagePath)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	FString SaveError;
	if (!FMontageEditor::SaveMontage(Montage, SaveError))
	{
		return FMCPToolResult::Error(SaveError);
	}

	return FMCPToolResult::Success(FString::Printf(TEXT("Saved montage: %s"), *MontagePath));
}

FMCPToolResult FMCPTool_MontageModify::HandleAddSection(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString SectionName;
	if (!ExtractRequiredString(Params, TEXT("section_name"), SectionName, Error))
	{
		return Error.GetValue();
	}

	float StartTime = ExtractOptionalNumber<float>(Params, TEXT("start_time"), 0.0f);

	FString OpError;
	if (!FMontageEditor::AddSection(Montage, FName(*SectionName), StartTime, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Added section '%s' at %.3fs"), *SectionName, StartTime), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleRemoveSection(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString SectionName;
	if (!ExtractRequiredString(Params, TEXT("section_name"), SectionName, Error))
	{
		return Error.GetValue();
	}

	FString OpError;
	if (!FMontageEditor::RemoveSection(Montage, FName(*SectionName), OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed section '%s'"), *SectionName), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleLinkSections(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString SectionName;
	if (!ExtractRequiredString(Params, TEXT("section_name"), SectionName, Error))
	{
		return Error.GetValue();
	}

	FString NextSection = ExtractOptionalString(Params, TEXT("next_section"));
	FName NextSectionName = NextSection.IsEmpty() ? NAME_None : FName(*NextSection);

	FString OpError;
	if (!FMontageEditor::SetSectionLink(Montage, FName(*SectionName), NextSectionName, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	FString Msg = NextSection.IsEmpty()
		? FString::Printf(TEXT("Unlinked section '%s'"), *SectionName)
		: FString::Printf(TEXT("Linked section '%s' -> '%s'"), *SectionName, *NextSection);
	return FMCPToolResult::Success(Msg, Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleAddSegment(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString AnimPath;
	if (!ExtractRequiredString(Params, TEXT("animation_path"), AnimPath, Error))
	{
		return Error.GetValue();
	}

	int32 SlotIndex = ExtractOptionalNumber<int32>(Params, TEXT("slot_index"), 0);
	float StartPos = ExtractOptionalNumber<float>(Params, TEXT("start_pos"), -1.0f);

	if (StartPos < 0.0f)
	{
		float MaxEnd = 0.0f;
		if (SlotIndex >= 0 && SlotIndex < Montage->SlotAnimTracks.Num())
		{
			for (const FAnimSegment& Seg : Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments)
			{
				float SegEnd = Seg.StartPos + (Seg.AnimEndTime - Seg.AnimStartTime) / FMath::Max(Seg.AnimPlayRate, 0.01f);
				MaxEnd = FMath::Max(MaxEnd, SegEnd);
			}
		}
		StartPos = MaxEnd;
	}

	FString OpError;
	if (!FMontageEditor::AddSegment(Montage, SlotIndex, AnimPath, StartPos, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Added segment at slot %d, pos %.3f"), SlotIndex, StartPos), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleRemoveSegment(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	int32 SlotIndex = ExtractOptionalNumber<int32>(Params, TEXT("slot_index"), 0);

	TOptional<FMCPToolResult> Error;
	double SegIdx;
	if (!Params->TryGetNumberField(TEXT("segment_index"), SegIdx))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: segment_index"));
	}

	FString OpError;
	if (!FMontageEditor::RemoveSegment(Montage, SlotIndex, static_cast<int32>(SegIdx), OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed segment %d from slot %d"), static_cast<int32>(SegIdx), SlotIndex), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleSetSegment(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	int32 SlotIndex = ExtractOptionalNumber<int32>(Params, TEXT("slot_index"), 0);

	double SegIdx;
	if (!Params->TryGetNumberField(TEXT("segment_index"), SegIdx))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: segment_index"));
	}

	TOptional<float> PlayRate;
	TOptional<float> AnimStartTime;
	TOptional<float> AnimEndTime;
	TOptional<float> StartPos;

	double Val;
	if (Params->TryGetNumberField(TEXT("play_rate"), Val))
	{
		PlayRate = static_cast<float>(Val);
	}
	if (Params->TryGetNumberField(TEXT("anim_start_time"), Val))
	{
		AnimStartTime = static_cast<float>(Val);
	}
	if (Params->TryGetNumberField(TEXT("anim_end_time"), Val))
	{
		AnimEndTime = static_cast<float>(Val);
	}
	if (Params->TryGetNumberField(TEXT("start_pos"), Val))
	{
		StartPos = static_cast<float>(Val);
	}

	FString AnimPath;
	Params->TryGetStringField(TEXT("animation_path"), AnimPath);

	FString OpError;
	if (!FMontageEditor::SetSegmentProperties(Montage, SlotIndex, static_cast<int32>(SegIdx),
		PlayRate, AnimStartTime, AnimEndTime, StartPos, AnimPath, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Updated segment %d in slot %d"), static_cast<int32>(SegIdx), SlotIndex), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleAddSlot(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString SlotName;
	if (!ExtractRequiredString(Params, TEXT("slot_name"), SlotName, Error))
	{
		return Error.GetValue();
	}

	FString OpError;
	if (!FMontageEditor::AddSlot(Montage, FName(*SlotName), OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Added slot '%s'"), *SlotName), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleRemoveSlot(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	double SlotIdx;
	if (!Params->TryGetNumberField(TEXT("slot_index"), SlotIdx))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: slot_index"));
	}

	FString OpError;
	if (!FMontageEditor::RemoveSlot(Montage, static_cast<int32>(SlotIdx), OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed slot %d"), static_cast<int32>(SlotIdx)), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleAddNotify(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString NotifyName;
	if (!ExtractRequiredString(Params, TEXT("notify_name"), NotifyName, Error))
	{
		return Error.GetValue();
	}

	double TrigTime;
	if (!Params->TryGetNumberField(TEXT("trigger_time"), TrigTime))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: trigger_time"));
	}

	int32 TrackIndex = ExtractOptionalNumber<int32>(Params, TEXT("track_index"), 0);
	FString NotifyClassPath = ExtractOptionalString(Params, TEXT("notify_class"));

	FString OpError;
	if (!FMontageEditor::AddNotify(Montage, NotifyName, static_cast<float>(TrigTime), TrackIndex, NotifyClassPath, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Added notify '%s' at %.3fs"), *NotifyName, TrigTime), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleAddNotifyState(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString NotifyName;
	if (!ExtractRequiredString(Params, TEXT("notify_name"), NotifyName, Error))
	{
		return Error.GetValue();
	}

	double StartTime;
	if (!Params->TryGetNumberField(TEXT("start_time"), StartTime))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: start_time"));
	}

	double Duration;
	if (!Params->TryGetNumberField(TEXT("duration"), Duration))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: duration"));
	}

	int32 TrackIndex = ExtractOptionalNumber<int32>(Params, TEXT("track_index"), 0);
	FString NotifyClassPath = ExtractOptionalString(Params, TEXT("notify_class"));

	FString OpError;
	if (!FMontageEditor::AddNotifyState(Montage, NotifyName, static_cast<float>(StartTime), static_cast<float>(Duration), TrackIndex, NotifyClassPath, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Added notify state '%s' at %.3fs for %.3fs"), *NotifyName, StartTime, Duration), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleRemoveNotify(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString NotifyName;
	if (!ExtractRequiredString(Params, TEXT("notify_name"), NotifyName, Error))
	{
		return Error.GetValue();
	}

	FString OpError;
	if (!FMontageEditor::RemoveNotify(Montage, NotifyName, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed notify '%s'"), *NotifyName), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleMoveNotify(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	double NotifyIdx;
	if (!Params->TryGetNumberField(TEXT("notify_index"), NotifyIdx))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: notify_index"));
	}

	double TrackIdx;
	if (!Params->TryGetNumberField(TEXT("track_index"), TrackIdx))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: track_index"));
	}

	FString OpError;
	if (!FMontageEditor::MoveNotify(Montage, static_cast<int32>(NotifyIdx), static_cast<int32>(TrackIdx), OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Moved notify %d to track %d"), static_cast<int32>(NotifyIdx), static_cast<int32>(TrackIdx)), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleRenameTrack(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	double TrackIdx;
	if (!Params->TryGetNumberField(TEXT("track_index"), TrackIdx))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: track_index"));
	}

	TOptional<FMCPToolResult> Error;
	FString TrackName;
	if (!ExtractRequiredString(Params, TEXT("track_name"), TrackName, Error))
	{
		return Error.GetValue();
	}

	FString OpError;
	if (!FMontageEditor::RenameNotifyTrack(Montage, static_cast<int32>(TrackIdx), TrackName, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeMontageInfo(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Renamed track %d to '%s'"), static_cast<int32>(TrackIdx), *TrackName), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleSetBlendIn(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	double BlendTime;
	if (!Params->TryGetNumberField(TEXT("blend_time"), BlendTime))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: blend_time"));
	}

	FString BlendOption = ExtractOptionalString(Params, TEXT("blend_option"), TEXT("Linear"));

	FString OpError;
	if (!FMontageEditor::SetBlendIn(Montage, static_cast<float>(BlendTime), BlendOption, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("blend_in_time"), BlendTime);
	Result->SetStringField(TEXT("blend_option"), BlendOption);
	return FMCPToolResult::Success(FString::Printf(TEXT("Set blend-in: %.3fs %s"), BlendTime, *BlendOption), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleSetBlendOut(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	double BlendTime;
	if (!Params->TryGetNumberField(TEXT("blend_time"), BlendTime))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: blend_time"));
	}

	FString BlendOption = ExtractOptionalString(Params, TEXT("blend_option"), TEXT("Linear"));

	FString OpError;
	if (!FMontageEditor::SetBlendOut(Montage, static_cast<float>(BlendTime), BlendOption, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("blend_out_time"), BlendTime);
	Result->SetStringField(TEXT("blend_option"), BlendOption);
	return FMCPToolResult::Success(FString::Printf(TEXT("Set blend-out: %.3fs %s"), BlendTime, *BlendOption), Result);
}

// ===== Curve Handlers =====

static TArray<FRichCurveKey> ParseKeysFromJson(const TArray<TSharedPtr<FJsonValue>>& KeysArray)
{
	TArray<FRichCurveKey> Keys;
	for (const TSharedPtr<FJsonValue>& KeyVal : KeysArray)
	{
		const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
		if (!KeyVal || !KeyVal->TryGetObject(KeyObjPtr) || !KeyObjPtr)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& KeyObj = *KeyObjPtr;

		double Time = 0.0, Value = 0.0;
		if (!KeyObj->TryGetNumberField(TEXT("time"), Time) || !KeyObj->TryGetNumberField(TEXT("value"), Value))
		{
			continue;
		}

		FRichCurveKey Key;
		Key.Time = static_cast<float>(Time);
		Key.Value = static_cast<float>(Value);

		FString InterpStr;
		if (KeyObj->TryGetStringField(TEXT("interp_mode"), InterpStr))
		{
			Key.InterpMode = FMontageEditor::ParseInterpMode(InterpStr);
		}

		FString TangentStr;
		if (KeyObj->TryGetStringField(TEXT("tangent_mode"), TangentStr))
		{
			Key.TangentMode = FMontageEditor::ParseTangentMode(TangentStr);
		}

		double TangentVal;
		if (KeyObj->TryGetNumberField(TEXT("arrive_tangent"), TangentVal))
		{
			Key.ArriveTangent = static_cast<float>(TangentVal);
		}
		if (KeyObj->TryGetNumberField(TEXT("leave_tangent"), TangentVal))
		{
			Key.LeaveTangent = static_cast<float>(TangentVal);
		}

		Keys.Add(Key);
	}
	return Keys;
}

FMCPToolResult FMCPTool_MontageModify::HandleGetCurves(const FString& MontagePath)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeCurves(Montage);
	return FMCPToolResult::Success(TEXT("Curves retrieved"), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleAddCurve(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString CurveName;
	if (!ExtractRequiredString(Params, TEXT("curve_name"), CurveName, Error))
	{
		return Error.GetValue();
	}

	TArray<FRichCurveKey> InitialKeys;
	const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
	if (Params->TryGetArrayField(TEXT("keys"), KeysArray) && KeysArray)
	{
		InitialKeys = ParseKeysFromJson(*KeysArray);
	}

	FString OpError;
	if (!FMontageEditor::AddCurve(Montage, FName(*CurveName),
		InitialKeys.Num() > 0 ? &InitialKeys : nullptr, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeCurves(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Added curve '%s'"), *CurveName), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleRemoveCurve(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString CurveName;
	if (!ExtractRequiredString(Params, TEXT("curve_name"), CurveName, Error))
	{
		return Error.GetValue();
	}

	FString OpError;
	if (!FMontageEditor::RemoveCurve(Montage, FName(*CurveName), OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeCurves(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed curve '%s'"), *CurveName), Result);
}

FMCPToolResult FMCPTool_MontageModify::HandleSetCurveKeys(const FString& MontagePath, const TSharedRef<FJsonObject>& Params)
{
	UAnimMontage* Montage = nullptr;
	if (auto Error = LoadMontageOrError(MontagePath, Montage))
	{
		return Error.GetValue();
	}

	TOptional<FMCPToolResult> Error;
	FString CurveName;
	if (!ExtractRequiredString(Params, TEXT("curve_name"), CurveName, Error))
	{
		return Error.GetValue();
	}

	const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("keys"), KeysArray) || !KeysArray)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: keys"));
	}

	TArray<FRichCurveKey> Keys = ParseKeysFromJson(*KeysArray);

	FString OpError;
	if (!FMontageEditor::SetCurveKeys(Montage, FName(*CurveName), Keys, OpError))
	{
		return FMCPToolResult::Error(OpError);
	}

	TSharedPtr<FJsonObject> Result = FMontageEditor::SerializeCurves(Montage);
	return FMCPToolResult::Success(FString::Printf(TEXT("Set %d keys on curve '%s'"), Keys.Num(), *CurveName), Result);
}
