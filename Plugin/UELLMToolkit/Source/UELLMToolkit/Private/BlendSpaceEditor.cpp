// Copyright Natali Caggiano. All Rights Reserved.

#include "BlendSpaceEditor.h"

#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "EditorAssetLibrary.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Factories/AimOffsetBlendSpaceFactoryNew.h"
#include "Factories/AimOffsetBlendSpaceFactory1D.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

// ============================================================================
// Private Helpers
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::SuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FBlendSpaceEditor::ErrorResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Message);
	return Result;
}

UBlendSpace* FBlendSpaceEditor::LoadBlendSpace(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UBlendSpace::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load blend space: %s"), *Path);
		return nullptr;
	}

	UBlendSpace* BS = Cast<UBlendSpace>(Loaded);
	if (!BS)
	{
		OutError = FString::Printf(TEXT("Asset is not a BlendSpace: %s (is %s)"),
			*Path, *Loaded->GetClass()->GetName());
		return nullptr;
	}

	return BS;
}

UAnimSequence* FBlendSpaceEditor::LoadAnimSequence(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load animation: %s"), *Path);
		return nullptr;
	}

	UAnimSequence* Anim = Cast<UAnimSequence>(Loaded);
	if (!Anim)
	{
		OutError = FString::Printf(TEXT("Asset is not an AnimSequence: %s (is %s)"),
			*Path, *Loaded->GetClass()->GetName());
		return nullptr;
	}

	return Anim;
}

// ============================================================================
// CreateBlendSpace
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::CreateBlendSpace(const FString& PackagePath, const FString& Name,
	const FString& SkeletonPath, bool bIs1D, bool bIsAimOffset)
{
	// Load skeleton
	UObject* SkelObj = StaticLoadObject(USkeleton::StaticClass(), nullptr, *SkeletonPath);
	if (!SkelObj)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to load skeleton: %s"), *SkeletonPath));
	}
	USkeleton* Skeleton = Cast<USkeleton>(SkelObj);
	if (!Skeleton)
	{
		return ErrorResult(FString::Printf(TEXT("Asset is not a Skeleton: %s"), *SkeletonPath));
	}

	// Create via factory — 4-way: BlendSpace1D, BlendSpace, AimOffsetBlendSpace1D, AimOffsetBlendSpace
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UFactory* Factory = nullptr;

	if (bIsAimOffset)
	{
		if (bIs1D)
		{
			auto* F = NewObject<UAimOffsetBlendSpaceFactory1D>();
			F->TargetSkeleton = Skeleton;
			Factory = F;
		}
		else
		{
			auto* F = NewObject<UAimOffsetBlendSpaceFactoryNew>();
			F->TargetSkeleton = Skeleton;
			Factory = F;
		}
	}
	else
	{
		if (bIs1D)
		{
			auto* F = NewObject<UBlendSpaceFactory1D>();
			F->TargetSkeleton = Skeleton;
			Factory = F;
		}
		else
		{
			auto* F = NewObject<UBlendSpaceFactoryNew>();
			F->TargetSkeleton = Skeleton;
			Factory = F;
		}
	}

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, nullptr, Factory);
	if (!NewAsset)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to create blend space: %s/%s"), *PackagePath, *Name));
	}

	NewAsset->MarkPackageDirty();

	const TCHAR* TypeName =
		bIsAimOffset ? (bIs1D ? TEXT("AimOffsetBlendSpace1D") : TEXT("AimOffsetBlendSpace"))
		             : (bIs1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace"));

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Created %s: %s"), TypeName, *NewAsset->GetPathName()));
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("type"), TypeName);
	return Result;
}

// ============================================================================
// AddSample
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::AddSampleInternal(UBlendSpace* BS, const FString& AnimPath, float X, float Y, float RateScale)
{
	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AnimPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

	FVector SampleValue(X, Y, 0.f);

	// AddSample calls ExpandRangeForSample before ValidateSampleValue — do NOT pre-validate
	int32 NewIndex = BS->AddSample(Anim, SampleValue);
	if (NewIndex == INDEX_NONE)
	{
		return ErrorResult(FString::Printf(TEXT("AddSample failed for %s at (%f, %f)"), *AnimPath, X, Y));
	}

	if (RateScale > 0.f)
	{
		TArray<FBlendSample>& Samples = const_cast<TArray<FBlendSample>&>(BS->GetBlendSamples());
		Samples[NewIndex].RateScale = FMath::Clamp(RateScale, 0.01f, 64.0f);
	}

	BS->MarkPackageDirty();

	FString Msg = FString::Printf(TEXT("Added sample %d: %s at (%g, %g)"), NewIndex, *Anim->GetName(), X, Y);
	if (RateScale > 0.f)
	{
		Msg += FString::Printf(TEXT(", rate_scale=%g"), BS->GetBlendSamples()[NewIndex].RateScale);
	}
	TSharedPtr<FJsonObject> Result = SuccessResult(Msg);
	Result->SetNumberField(TEXT("sample_index"), NewIndex);
	return Result;
}

TSharedPtr<FJsonObject> FBlendSpaceEditor::AddSample(UBlendSpace* BS, const FString& AnimPath, float X, float Y, float RateScale)
{
	TSharedPtr<FJsonObject> Result = AddSampleInternal(BS, AnimPath, X, Y, RateScale);

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);
	if (bSuccess)
	{
		BS->ValidateSampleData();
		BS->ResampleData();
	}

	return Result;
}

// ============================================================================
// RemoveSample
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::RemoveSampleInternal(UBlendSpace* BS, int32 Index)
{
	const TArray<FBlendSample>& Samples = BS->GetBlendSamples();
	if (Index < 0 || Index >= Samples.Num())
	{
		return ErrorResult(FString::Printf(TEXT("Sample index %d out of range (0-%d)"), Index, Samples.Num() - 1));
	}

	FString AnimName = Samples[Index].Animation ? Samples[Index].Animation->GetName() : TEXT("None");

	// DeleteSample uses RemoveAtSwap — last element moves into deleted slot
	bool bRemoved = BS->DeleteSample(Index);
	if (!bRemoved)
	{
		return ErrorResult(FString::Printf(TEXT("DeleteSample failed for index %d"), Index));
	}

	BS->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Removed sample %d (%s). Warning: RemoveAtSwap — remaining indices may have shifted."),
			Index, *AnimName));
	Result->SetNumberField(TEXT("remaining_samples"), BS->GetBlendSamples().Num());
	return Result;
}

TSharedPtr<FJsonObject> FBlendSpaceEditor::RemoveSample(UBlendSpace* BS, int32 Index)
{
	TSharedPtr<FJsonObject> Result = RemoveSampleInternal(BS, Index);

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);
	if (bSuccess)
	{
		BS->ValidateSampleData();
		BS->ResampleData();
	}

	return Result;
}

// ============================================================================
// MoveSample
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::MoveSampleInternal(UBlendSpace* BS, int32 Index, float X, float Y, float RateScale)
{
	const TArray<FBlendSample>& Samples = BS->GetBlendSamples();
	if (Index < 0 || Index >= Samples.Num())
	{
		return ErrorResult(FString::Printf(TEXT("Sample index %d out of range (0-%d)"), Index, Samples.Num() - 1));
	}

	FVector NewValue(X, Y, 0.f);

	// EditSampleValue calls ExpandRangeForSample internally
	bool bMoved = BS->EditSampleValue(Index, NewValue);
	if (!bMoved)
	{
		return ErrorResult(FString::Printf(TEXT("EditSampleValue failed for index %d"), Index));
	}

	if (RateScale > 0.f)
	{
		TArray<FBlendSample>& MutableSamples = const_cast<TArray<FBlendSample>&>(BS->GetBlendSamples());
		MutableSamples[Index].RateScale = FMath::Clamp(RateScale, 0.01f, 64.0f);
	}

	BS->MarkPackageDirty();

	FString Msg = FString::Printf(TEXT("Moved sample %d to (%g, %g)"), Index, X, Y);
	if (RateScale > 0.f)
	{
		Msg += FString::Printf(TEXT(", rate_scale=%g"), BS->GetBlendSamples()[Index].RateScale);
	}
	return SuccessResult(Msg);
}

TSharedPtr<FJsonObject> FBlendSpaceEditor::MoveSample(UBlendSpace* BS, int32 Index, float X, float Y, float RateScale)
{
	TSharedPtr<FJsonObject> Result = MoveSampleInternal(BS, Index, X, Y, RateScale);

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);
	if (bSuccess)
	{
		BS->ValidateSampleData();
		BS->ResampleData();
	}

	return Result;
}

// ============================================================================
// SetSampleAnimation
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::SetSampleAnimInternal(UBlendSpace* BS, int32 Index, const FString& AnimPath)
{
	const TArray<FBlendSample>& Samples = BS->GetBlendSamples();
	if (Index < 0 || Index >= Samples.Num())
	{
		return ErrorResult(FString::Printf(TEXT("Sample index %d out of range (0-%d)"), Index, Samples.Num() - 1));
	}

	FString LoadError;
	UAnimSequence* Anim = LoadAnimSequence(AnimPath, LoadError);
	if (!Anim)
	{
		return ErrorResult(LoadError);
	}

	// Validate animation is compatible with this blend space's skeleton
	if (!BS->ValidateAnimationSequence(Anim))
	{
		return ErrorResult(FString::Printf(TEXT("Animation %s is not compatible with this blend space's skeleton"), *AnimPath));
	}

	BS->ReplaceSampleAnimation(Index, Anim);
	BS->MarkPackageDirty();

	return SuccessResult(
		FString::Printf(TEXT("Set sample %d animation to %s"), Index, *Anim->GetName()));
}

TSharedPtr<FJsonObject> FBlendSpaceEditor::SetSampleAnimation(UBlendSpace* BS, int32 Index, const FString& AnimPath)
{
	// No ResampleData needed — geometry doesn't change
	return SetSampleAnimInternal(BS, Index, AnimPath);
}

// ============================================================================
// SetAxis
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::SetAxisInternal(UBlendSpace* BS, int32 AxisIndex, const TSharedPtr<FJsonObject>& AxisParams)
{
	bool bIs1D = Cast<UBlendSpace1D>(BS) != nullptr;
	int32 MaxAxis = bIs1D ? 1 : 2;

	if (AxisIndex < 0 || AxisIndex >= MaxAxis)
	{
		return ErrorResult(FString::Printf(TEXT("Axis index %d out of range (0-%d for %s)"),
			AxisIndex, MaxAxis - 1, bIs1D ? TEXT("1D") : TEXT("2D")));
	}

	// GetBlendParameter returns const ref — const_cast is safe because underlying BlendParameters[] is mutable
	FBlendParameter& Param = const_cast<FBlendParameter&>(BS->GetBlendParameter(AxisIndex));
	FInterpolationParameter& Interp = BS->InterpolationParam[AxisIndex];

	// Apply axis params (only those provided)
	FString StrVal;
	double NumVal;
	bool BoolVal;

	if (AxisParams->TryGetStringField(TEXT("axis_name"), StrVal))
	{
		Param.DisplayName = StrVal;
	}
	// Also accept "name" for batch convenience
	if (AxisParams->TryGetStringField(TEXT("name"), StrVal))
	{
		Param.DisplayName = StrVal;
	}
	if (AxisParams->TryGetNumberField(TEXT("min"), NumVal))
	{
		Param.Min = static_cast<float>(NumVal);
	}
	if (AxisParams->TryGetNumberField(TEXT("max"), NumVal))
	{
		Param.Max = static_cast<float>(NumVal);
	}
	if (AxisParams->TryGetNumberField(TEXT("grid_divisions"), NumVal))
	{
		Param.GridNum = static_cast<int32>(NumVal);
	}
	if (AxisParams->TryGetBoolField(TEXT("snap_to_grid"), BoolVal))
	{
		Param.bSnapToGrid = BoolVal;
	}
	if (AxisParams->TryGetBoolField(TEXT("wrap_input"), BoolVal))
	{
		Param.bWrapInput = BoolVal;
	}

	// Interpolation settings
	if (AxisParams->TryGetNumberField(TEXT("interp_time"), NumVal))
	{
		Interp.InterpolationTime = static_cast<float>(NumVal);
	}
	if (AxisParams->TryGetStringField(TEXT("interp_type"), StrVal))
	{
		StrVal = StrVal.ToLower();
		if (StrVal == TEXT("average"))            Interp.InterpolationType = BSIT_Average;
		else if (StrVal == TEXT("linear"))        Interp.InterpolationType = BSIT_Linear;
		else if (StrVal == TEXT("cubic"))         Interp.InterpolationType = BSIT_Cubic;
		else if (StrVal == TEXT("easeinout"))     Interp.InterpolationType = BSIT_EaseInOut;
		else if (StrVal == TEXT("exponentialdecay")) Interp.InterpolationType = BSIT_ExponentialDecay;
		else if (StrVal == TEXT("springdamper"))  Interp.InterpolationType = BSIT_SpringDamper;
	}
	if (AxisParams->TryGetNumberField(TEXT("damping_ratio"), NumVal))
	{
		Interp.DampingRatio = static_cast<float>(NumVal);
	}
	if (AxisParams->TryGetNumberField(TEXT("max_speed"), NumVal))
	{
		Interp.MaxSpeed = static_cast<float>(NumVal);
	}

	BS->MarkPackageDirty();

	return SuccessResult(
		FString::Printf(TEXT("Configured axis %d ('%s': %g to %g, %d divisions)"),
			AxisIndex, *Param.DisplayName, Param.Min, Param.Max, Param.GridNum));
}

TSharedPtr<FJsonObject> FBlendSpaceEditor::SetAxis(UBlendSpace* BS, int32 AxisIndex, const TSharedPtr<FJsonObject>& AxisParams)
{
	TSharedPtr<FJsonObject> Result = SetAxisInternal(BS, AxisIndex, AxisParams);

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);
	if (bSuccess)
	{
		BS->ValidateSampleData();
		BS->ResampleData();
	}

	return Result;
}

// ============================================================================
// Save
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::SaveBlendSpace(const FString& AssetPath)
{
	bool bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, false);
	if (bSaved)
	{
		return SuccessResult(FString::Printf(TEXT("Saved: %s"), *AssetPath));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to save: %s"), *AssetPath));
}

// ============================================================================
// Batch
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceEditor::DispatchBatchOp(UBlendSpace* BS, const TSharedPtr<FJsonObject>& OpData)
{
	FString OpName;
	OpData->TryGetStringField(TEXT("op"), OpName);
	OpName = OpName.ToLower();

	if (OpName == TEXT("add_sample"))
	{
		FString AnimPath;
		if (!OpData->TryGetStringField(TEXT("animation_path"), AnimPath) || AnimPath.IsEmpty())
		{
			return ErrorResult(TEXT("add_sample: missing animation_path"));
		}
		double X = 0, Y = 0;
		OpData->TryGetNumberField(TEXT("x"), X);
		OpData->TryGetNumberField(TEXT("y"), Y);
		double RateScaleVal = -1.0;
		OpData->TryGetNumberField(TEXT("rate_scale"), RateScaleVal);
		return AddSampleInternal(BS, AnimPath, static_cast<float>(X), static_cast<float>(Y), static_cast<float>(RateScaleVal));
	}
	else if (OpName == TEXT("remove_sample"))
	{
		double Index = -1;
		if (!OpData->TryGetNumberField(TEXT("sample_index"), Index))
		{
			return ErrorResult(TEXT("remove_sample: missing sample_index"));
		}
		return RemoveSampleInternal(BS, static_cast<int32>(Index));
	}
	else if (OpName == TEXT("move_sample"))
	{
		double Index = -1;
		if (!OpData->TryGetNumberField(TEXT("sample_index"), Index))
		{
			return ErrorResult(TEXT("move_sample: missing sample_index"));
		}
		double X = 0, Y = 0;
		OpData->TryGetNumberField(TEXT("x"), X);
		OpData->TryGetNumberField(TEXT("y"), Y);
		double RateScaleVal = -1.0;
		OpData->TryGetNumberField(TEXT("rate_scale"), RateScaleVal);
		return MoveSampleInternal(BS, static_cast<int32>(Index), static_cast<float>(X), static_cast<float>(Y), static_cast<float>(RateScaleVal));
	}
	else if (OpName == TEXT("set_sample_animation"))
	{
		double Index = -1;
		if (!OpData->TryGetNumberField(TEXT("sample_index"), Index))
		{
			return ErrorResult(TEXT("set_sample_animation: missing sample_index"));
		}
		FString AnimPath;
		if (!OpData->TryGetStringField(TEXT("animation_path"), AnimPath) || AnimPath.IsEmpty())
		{
			return ErrorResult(TEXT("set_sample_animation: missing animation_path"));
		}
		return SetSampleAnimInternal(BS, static_cast<int32>(Index), AnimPath);
	}
	else if (OpName == TEXT("set_axis"))
	{
		double AxisIndex = 0;
		if (!OpData->TryGetNumberField(TEXT("axis_index"), AxisIndex))
		{
			return ErrorResult(TEXT("set_axis: missing axis_index"));
		}
		// Pass the whole OpData as axis params — SetAxisInternal picks out the fields it needs
		return SetAxisInternal(BS, static_cast<int32>(AxisIndex), OpData);
	}

	return ErrorResult(FString::Printf(TEXT("Unknown batch op: '%s'. Valid: add_sample, remove_sample, move_sample, set_sample_animation, set_axis"), *OpName));
}

TSharedPtr<FJsonObject> FBlendSpaceEditor::ExecuteBatch(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>& Operations)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!BS)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Null blend space"));
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 OKCount = 0;
	int32 ErrCount = 0;

	for (int32 i = 0; i < Operations.Num(); ++i)
	{
		if (!Operations[i].IsValid() || Operations[i]->Type != EJson::Object)
		{
			TSharedPtr<FJsonObject> SkipResult = ErrorResult(
				FString::Printf(TEXT("[%d] not a JSON object"), i));
			ResultsArray.Add(MakeShared<FJsonValueObject>(SkipResult));
			ErrCount++;
			continue;
		}

		TSharedPtr<FJsonObject> OpData = Operations[i]->AsObject();
		FString OpName;
		if (!OpData->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
		{
			TSharedPtr<FJsonObject> SkipResult = ErrorResult(
				FString::Printf(TEXT("[%d] missing 'op' field"), i));
			ResultsArray.Add(MakeShared<FJsonValueObject>(SkipResult));
			ErrCount++;
			continue;
		}

		TSharedPtr<FJsonObject> OpResult = DispatchBatchOp(BS, OpData);
		ResultsArray.Add(MakeShared<FJsonValueObject>(OpResult));

		bool bSuccess = false;
		OpResult->TryGetBoolField(TEXT("success"), bSuccess);
		if (bSuccess)
		{
			OKCount++;
		}
		else
		{
			ErrCount++;
		}
	}

	// Single ResampleData at the end
	BS->ValidateSampleData();
	BS->ResampleData();

	Result->SetBoolField(TEXT("success"), ErrCount == 0);
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("ok_count"), OKCount);
	Result->SetNumberField(TEXT("error_count"), ErrCount);
	Result->SetNumberField(TEXT("total"), Operations.Num());

	return Result;
}
