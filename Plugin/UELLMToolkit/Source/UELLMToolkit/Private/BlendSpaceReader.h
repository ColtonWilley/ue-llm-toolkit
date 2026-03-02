// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlendSpace;

/**
 * Utility class for read-only blend space inspection.
 * Covers: axis configuration, sample points, interpolation settings, geometry.
 *
 * All methods are static, return JSON, and contain no MCP/JSON dispatch logic.
 */
class FBlendSpaceReader
{
public:
	/** Full read of a blend space: axes, samples, interpolation, geometry. Works for both 1D and 2D. */
	static TSharedPtr<FJsonObject> InspectBlendSpace(const FString& AssetPath);

	/** Find blend space assets in a content folder. */
	static TSharedPtr<FJsonObject> ListBlendSpaces(const FString& FolderPath, bool bRecursive = false);

private:
	static TSharedPtr<FJsonObject> SuccessResult(const FString& Message);
	static TSharedPtr<FJsonObject> ErrorResult(const FString& Message);

	/** Load a blend space by path (works for both 1D and 2D since 1D inherits from 2D). */
	static UBlendSpace* LoadBlendSpace(const FString& Path, FString& OutError);

	// Enum-to-string helpers
	static FString FilterInterpolationTypeToString(uint8 Type);
	static FString NotifyTriggerModeToString(uint8 Mode);
	static FString PreferredTriangulationToString(uint8 Direction);
};
