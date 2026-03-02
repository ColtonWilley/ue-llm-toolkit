// Copyright Natali Caggiano. All Rights Reserved.

#include "BlendSpaceReader.h"

#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"

// ============================================================================
// Private Helpers
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceReader::SuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FBlendSpaceReader::ErrorResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Message);
	return Result;
}

UBlendSpace* FBlendSpaceReader::LoadBlendSpace(const FString& Path, FString& OutError)
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

FString FBlendSpaceReader::FilterInterpolationTypeToString(uint8 Type)
{
	switch (Type)
	{
	case BSIT_Average:          return TEXT("Average");
	case BSIT_Linear:           return TEXT("Linear");
	case BSIT_Cubic:            return TEXT("Cubic");
	case BSIT_EaseInOut:        return TEXT("EaseInOut");
	case BSIT_ExponentialDecay: return TEXT("ExponentialDecay");
	case BSIT_SpringDamper:     return TEXT("SpringDamper");
	default:                    return TEXT("Unknown");
	}
}

FString FBlendSpaceReader::NotifyTriggerModeToString(uint8 Mode)
{
	switch (Mode)
	{
	case ENotifyTriggerMode::AllAnimations:            return TEXT("AllAnimations");
	case ENotifyTriggerMode::HighestWeightedAnimation: return TEXT("HighestWeightedAnimation");
	case ENotifyTriggerMode::None:                     return TEXT("None");
	default:                                           return TEXT("Unknown");
	}
}

FString FBlendSpaceReader::PreferredTriangulationToString(uint8 Direction)
{
	switch (Direction)
	{
	case (uint8)EPreferredTriangulationDirection::None:       return TEXT("None");
	case (uint8)EPreferredTriangulationDirection::Tangential: return TEXT("Tangential");
	case (uint8)EPreferredTriangulationDirection::Radial:     return TEXT("Radial");
	default:                                                  return TEXT("Unknown");
	}
}

// ============================================================================
// InspectBlendSpace
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceReader::InspectBlendSpace(const FString& AssetPath)
{
	FString LoadError;
	UBlendSpace* BS = LoadBlendSpace(AssetPath, LoadError);
	if (!BS)
	{
		return ErrorResult(LoadError);
	}

	bool bIs1D = Cast<UBlendSpace1D>(BS) != nullptr;
	int32 Dimensions = bIs1D ? 1 : 2;

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Inspected blend space: %s"), *BS->GetName()));

	Result->SetStringField(TEXT("name"), BS->GetName());
	Result->SetStringField(TEXT("path"), BS->GetPathName());
	Result->SetStringField(TEXT("type"), bIs1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace"));
	Result->SetNumberField(TEXT("dimensions"), Dimensions);
	Result->SetNumberField(TEXT("anim_length"), BS->AnimLength);
	Result->SetBoolField(TEXT("loop"), BS->bLoop);

	// ---- Axes ----
	TArray<TSharedPtr<FJsonValue>> AxesArray;
	for (int32 i = 0; i < Dimensions; ++i)
	{
		const FBlendParameter& Param = BS->GetBlendParameter(i);
		const FInterpolationParameter& Interp = BS->InterpolationParam[i];

		TSharedPtr<FJsonObject> AxisObj = MakeShared<FJsonObject>();
		AxisObj->SetNumberField(TEXT("index"), i);
		AxisObj->SetStringField(TEXT("name"), Param.DisplayName);
		AxisObj->SetNumberField(TEXT("min"), Param.Min);
		AxisObj->SetNumberField(TEXT("max"), Param.Max);
		AxisObj->SetNumberField(TEXT("grid_divisions"), Param.GridNum);
		AxisObj->SetBoolField(TEXT("snap_to_grid"), Param.bSnapToGrid);
		AxisObj->SetBoolField(TEXT("wrap_input"), Param.bWrapInput);

		// Interpolation settings per axis
		TSharedPtr<FJsonObject> InterpObj = MakeShared<FJsonObject>();
		InterpObj->SetNumberField(TEXT("time"), Interp.InterpolationTime);
		InterpObj->SetNumberField(TEXT("damping_ratio"), Interp.DampingRatio);
		InterpObj->SetNumberField(TEXT("max_speed"), Interp.MaxSpeed);
		InterpObj->SetStringField(TEXT("type"), FilterInterpolationTypeToString(Interp.InterpolationType));
		AxisObj->SetObjectField(TEXT("interpolation"), InterpObj);

		AxesArray.Add(MakeShared<FJsonValueObject>(AxisObj));
	}
	Result->SetArrayField(TEXT("axes"), AxesArray);

	// ---- Samples ----
	const TArray<FBlendSample>& Samples = BS->GetBlendSamples();
	TArray<TSharedPtr<FJsonValue>> SamplesArray;

	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		const FBlendSample& Sample = Samples[i];

		TSharedPtr<FJsonObject> SampleObj = MakeShared<FJsonObject>();
		SampleObj->SetNumberField(TEXT("index"), i);

		if (Sample.Animation)
		{
			SampleObj->SetStringField(TEXT("animation"), Sample.Animation->GetPathName());
			SampleObj->SetStringField(TEXT("animation_name"), Sample.Animation->GetName());
		}
		else
		{
			SampleObj->SetStringField(TEXT("animation"), TEXT("None"));
			SampleObj->SetStringField(TEXT("animation_name"), TEXT("None"));
		}

		// Position
		TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
		PosObj->SetNumberField(TEXT("x"), Sample.SampleValue.X);
		if (!bIs1D)
		{
			PosObj->SetNumberField(TEXT("y"), Sample.SampleValue.Y);
		}
		SampleObj->SetObjectField(TEXT("position"), PosObj);

		SampleObj->SetNumberField(TEXT("rate_scale"), Sample.RateScale);
		SampleObj->SetBoolField(TEXT("single_frame"), Sample.bUseSingleFrameForBlending);
		if (Sample.bUseSingleFrameForBlending)
		{
			SampleObj->SetNumberField(TEXT("frame_index"), (double)Sample.FrameIndexToSample);
		}

		SamplesArray.Add(MakeShared<FJsonValueObject>(SampleObj));
	}
	Result->SetArrayField(TEXT("samples"), SamplesArray);
	Result->SetNumberField(TEXT("sample_count"), Samples.Num());

	// ---- Settings ----
	TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
	Settings->SetStringField(TEXT("notify_trigger_mode"), NotifyTriggerModeToString(BS->NotifyTriggerMode));
	Settings->SetBoolField(TEXT("interpolate_using_grid"), BS->bInterpolateUsingGrid);
	Settings->SetStringField(TEXT("preferred_triangulation"), PreferredTriangulationToString((uint8)BS->PreferredTriangulationDirection));
	Settings->SetNumberField(TEXT("target_weight_interpolation_speed"), BS->TargetWeightInterpolationSpeedPerSec);
	Settings->SetBoolField(TEXT("allow_mesh_space_blending"), BS->bAllowMeshSpaceBlending);
	Settings->SetBoolField(TEXT("allow_marker_based_sync"), BS->bAllowMarkerBasedSync);
	Result->SetObjectField(TEXT("settings"), Settings);

	// ---- Geometry ----
	const FBlendSpaceData& SpaceData = BS->GetBlendSpaceData();
	TSharedPtr<FJsonObject> Geometry = MakeShared<FJsonObject>();
	Geometry->SetNumberField(TEXT("triangles"), SpaceData.Triangles.Num());
	Geometry->SetNumberField(TEXT("segments"), SpaceData.Segments.Num());
	Result->SetObjectField(TEXT("geometry"), Geometry);

	return Result;
}

// ============================================================================
// ListBlendSpaces
// ============================================================================

TSharedPtr<FJsonObject> FBlendSpaceReader::ListBlendSpaces(const FString& FolderPath, bool bRecursive)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*FolderPath));
	Filter.bRecursivePaths = bRecursive;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UBlendSpace::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> BlendSpacesArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());

		// Determine type: check if the native class is BlendSpace1D
		FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		if (ClassName == TEXT("BlendSpace1D"))
		{
			Entry->SetStringField(TEXT("type"), TEXT("BlendSpace1D"));
		}
		else
		{
			Entry->SetStringField(TEXT("type"), TEXT("BlendSpace"));
		}

		BlendSpacesArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Found %d blend space(s) in %s"), BlendSpacesArray.Num(), *FolderPath));
	Result->SetNumberField(TEXT("count"), BlendSpacesArray.Num());
	Result->SetArrayField(TEXT("blend_spaces"), BlendSpacesArray);

	return Result;
}
