// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Shared parser for the canonical property-path syntax used by both read
 * (FPropertySerializer::GetPropertyByPath) and write
 * (FMCPTool_BlueprintModify::SetComponentPropertyFromJson, FMCPTool_SetProperty::NavigateToProperty)
 * code paths.
 *
 * Canonical syntax:
 *   - Dot separates segments:        "Foo.Bar.Baz"
 *   - Brackets index arrays:         "Foo[3]"
 *   - Combined for nested arrays:    "AIPerception.SensesConfig[0].SightRadius"
 *
 * Canonical error messages (kept consistent across read and write so LLMs can pattern-match):
 *   - "Malformed path segment 'X' (expected Name[N])"
 *   - "Non-numeric index in 'X'"
 *   - "Negative index in 'X'"
 *   - "Property path cannot contain consecutive dots"
 *   - "Property path cannot start or end with a dot"
 */
class FPropertyPathParser
{
public:
	/**
	 * Parse a single segment like "Foo" or "Foo[3]".
	 * On success: OutName="Foo", OutIndex=3 (or INDEX_NONE for unbracketed names).
	 * On failure: OutErr is set to a canonical error message.
	 */
	static bool ParseSegment(const FString& Raw, FString& OutName, int32& OutIndex, FString& OutErr);

	/**
	 * Split a full path into segments on '.', validating overall shape (no leading/trailing dot,
	 * no consecutive dots, no empty segments). Each returned segment may carry a "[N]" suffix and
	 * must be passed through ParseSegment by the caller before lookup.
	 */
	static bool SplitPath(const FString& Path, TArray<FString>& OutSegments, FString& OutErr);
};
