// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlendSpace;
class UAnimSequence;
class USkeleton;

/**
 * Utility class for blend space write operations.
 * Covers: create, add/remove/move samples, set axis, save, batch.
 *
 * All methods are static, return JSON, and contain no MCP/JSON dispatch logic.
 * Read operations remain in FBlendSpaceReader.
 */
class FBlendSpaceEditor
{
public:
	// ===== Individual Operations (include ResampleData) =====

	/** Create a new BlendSpace, BlendSpace1D, AimOffsetBlendSpace, or AimOffsetBlendSpace1D asset. */
	static TSharedPtr<FJsonObject> CreateBlendSpace(const FString& PackagePath, const FString& Name,
		const FString& SkeletonPath, bool bIs1D = true, bool bIsAimOffset = false);

	/** Add an animation sample at a position. Auto-expands axis range. RateScale < 0 = don't touch. */
	static TSharedPtr<FJsonObject> AddSample(UBlendSpace* BS, const FString& AnimPath, float X, float Y, float RateScale = -1.f);

	/** Remove a sample by index. Warning: uses RemoveAtSwap — indices shift. */
	static TSharedPtr<FJsonObject> RemoveSample(UBlendSpace* BS, int32 Index);

	/** Move a sample to a new position. Auto-expands axis range. RateScale < 0 = don't touch. */
	static TSharedPtr<FJsonObject> MoveSample(UBlendSpace* BS, int32 Index, float X, float Y, float RateScale = -1.f);

	/** Replace the animation on an existing sample. */
	static TSharedPtr<FJsonObject> SetSampleAnimation(UBlendSpace* BS, int32 Index, const FString& AnimPath);

	/** Configure axis parameters and interpolation settings. */
	static TSharedPtr<FJsonObject> SetAxis(UBlendSpace* BS, int32 AxisIndex, const TSharedPtr<FJsonObject>& AxisParams);

	/** Save a blend space asset to disk. */
	static TSharedPtr<FJsonObject> SaveBlendSpace(const FString& AssetPath);

	/** Execute multiple operations with a single ResampleData at the end. */
	static TSharedPtr<FJsonObject> ExecuteBatch(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>& Operations);

	// ===== Asset Loading =====

	/** Load a blend space by path (works for both 1D and 2D). */
	static UBlendSpace* LoadBlendSpace(const FString& Path, FString& OutError);

	/** Load an animation sequence by path. */
	static UAnimSequence* LoadAnimSequence(const FString& Path, FString& OutError);

private:
	static TSharedPtr<FJsonObject> SuccessResult(const FString& Message);
	static TSharedPtr<FJsonObject> ErrorResult(const FString& Message);

	// ===== Internal Methods (no ResampleData — for batch use) =====

	static TSharedPtr<FJsonObject> AddSampleInternal(UBlendSpace* BS, const FString& AnimPath, float X, float Y, float RateScale = -1.f);
	static TSharedPtr<FJsonObject> RemoveSampleInternal(UBlendSpace* BS, int32 Index);
	static TSharedPtr<FJsonObject> MoveSampleInternal(UBlendSpace* BS, int32 Index, float X, float Y, float RateScale = -1.f);
	static TSharedPtr<FJsonObject> SetSampleAnimInternal(UBlendSpace* BS, int32 Index, const FString& AnimPath);
	static TSharedPtr<FJsonObject> SetAxisInternal(UBlendSpace* BS, int32 AxisIndex, const TSharedPtr<FJsonObject>& AxisParams);

	/** Dispatch a single batch operation. */
	static TSharedPtr<FJsonObject> DispatchBatchOp(UBlendSpace* BS, const TSharedPtr<FJsonObject>& OpData);
};
