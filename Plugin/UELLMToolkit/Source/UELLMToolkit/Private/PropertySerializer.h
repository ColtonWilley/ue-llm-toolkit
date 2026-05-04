// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/**
 * Walk state for optional recursion into instanced UObject subobjects.
 * Default-constructed state (MaxDepth=0) means "no recursion" — preserves
 * existing single-level behavior for callers that don't opt in.
 */
struct FPropertyRecursionContext
{
	/** Caps how many levels deep to recurse into instanced object fields. 0 disables recursion. */
	int32 MaxDepth = 0;

	/** Total property-count cap across the whole walk. Prevents runaway expansion on dense graphs. */
	int32 MaxProperties = 500;

	/** When true, ignore the editable/blueprint-visible filter and emit everything (still skips transient/deprecated/delegates). */
	bool bIncludeNonEdit = false;

	// --- Mutable walk state ---
	int32 CurrentDepth = 0;
	int32 PropertyCount = 0;
	TSet<const UObject*> Visited;

	bool IsBudgetExhausted() const { return PropertyCount >= MaxProperties; }
	bool CanRecurse() const { return CurrentDepth < MaxDepth && !IsBudgetExhausted(); }
};

class FPropertySerializer
{
public:
	/** Single-level serialization (legacy). Equivalent to a default-constructed recursion context. */
	static TSharedPtr<FJsonValue> PropertyToJsonValue(FProperty* Property, const void* ValuePtr);

	/** Context-aware serialization. Recurses into instanced object fields when Ctx allows. */
	static TSharedPtr<FJsonValue> PropertyToJsonValue(FProperty* Property, const void* ValuePtr, FPropertyRecursionContext& Ctx);

	struct FPropertyPathResult
	{
		TSharedPtr<FJsonValue> Value;
		FString Error;
		bool bSuccess = false;
	};

	/** Path-walk (legacy). Splits on '.' only; leaf serialization is single-level. */
	static FPropertyPathResult GetPropertyByPath(UObject* Object, const FString& PropertyPath);

	/**
	 * Path-walk with recursion context and `[N]` array-index support.
	 * Each path segment may be `Name` or `Name[N]`. Leaf serialization respects Ctx.
	 */
	static FPropertyPathResult GetPropertyByPath(UObject* Object, const FString& PropertyPath, FPropertyRecursionContext& Ctx);

	static TSharedPtr<FJsonObject> GetCDOOverrides(UObject* CDO, UObject* ParentCDO, bool bEditableOnly = true);

	/** CDO override dump with optional recursion. */
	static TSharedPtr<FJsonObject> GetCDOOverrides(UObject* CDO, UObject* ParentCDO, bool bEditableOnly, FPropertyRecursionContext& Ctx);

	static bool ShouldSkipProperty(FProperty* Property);

private:
	static TSharedPtr<FJsonValue> StructToJsonValue(FStructProperty* StructProp, const void* ValuePtr);
};
