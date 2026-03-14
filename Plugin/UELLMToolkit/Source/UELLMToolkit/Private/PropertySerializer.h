// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class FPropertySerializer
{
public:
	static TSharedPtr<FJsonValue> PropertyToJsonValue(FProperty* Property, const void* ValuePtr);

	struct FPropertyPathResult
	{
		TSharedPtr<FJsonValue> Value;
		FString Error;
		bool bSuccess = false;
	};

	static FPropertyPathResult GetPropertyByPath(UObject* Object, const FString& PropertyPath);

	static TSharedPtr<FJsonObject> GetCDOOverrides(UObject* CDO, UObject* ParentCDO, bool bEditableOnly = true);

	static bool ShouldSkipProperty(FProperty* Property);

private:
	static TSharedPtr<FJsonValue> StructToJsonValue(FStructProperty* StructProp, const void* ValuePtr);
};
