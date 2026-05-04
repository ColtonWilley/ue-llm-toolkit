// Copyright Natali Caggiano. All Rights Reserved.

#include "PropertyPathParser.h"

bool FPropertyPathParser::ParseSegment(const FString& Raw, FString& OutName, int32& OutIndex, FString& OutErr)
{
	OutIndex = INDEX_NONE;
	OutName.Empty();
	OutErr.Empty();

	if (Raw.IsEmpty())
	{
		OutErr = TEXT("Empty path segment");
		return false;
	}

	const int32 Open = Raw.Find(TEXT("["));
	if (Open == INDEX_NONE)
	{
		OutName = Raw;
		return true;
	}

	if (Open == 0)
	{
		OutErr = FString::Printf(TEXT("Malformed path segment '%s' (expected Name[N])"), *Raw);
		return false;
	}

	const int32 Close = Raw.Find(TEXT("]"));
	if (Close == INDEX_NONE || Close != Raw.Len() - 1 || Close <= Open + 1)
	{
		OutErr = FString::Printf(TEXT("Malformed path segment '%s' (expected Name[N])"), *Raw);
		return false;
	}

	const FString IndexStr = Raw.Mid(Open + 1, Close - Open - 1);
	if (!IndexStr.IsNumeric())
	{
		OutErr = FString::Printf(TEXT("Non-numeric index in '%s'"), *Raw);
		return false;
	}

	// Use Atoi64 + explicit int32-range check so overlong digit strings can't wrap silently.
	// (Validator caps bracket length to 9 digits; this is belt-and-braces for direct callers.)
	const int64 Parsed = FCString::Atoi64(*IndexStr);
	if (Parsed < 0)
	{
		OutErr = FString::Printf(TEXT("Negative index in '%s'"), *Raw);
		return false;
	}
	if (Parsed > static_cast<int64>(TNumericLimits<int32>::Max()))
	{
		OutErr = FString::Printf(TEXT("Index in '%s' exceeds int32 range"), *Raw);
		return false;
	}

	OutName = Raw.Left(Open);
	OutIndex = static_cast<int32>(Parsed);
	return true;
}

bool FPropertyPathParser::SplitPath(const FString& Path, TArray<FString>& OutSegments, FString& OutErr)
{
	OutSegments.Reset();
	OutErr.Empty();

	if (Path.IsEmpty())
	{
		OutErr = TEXT("Empty property path");
		return false;
	}

	if (Path.StartsWith(TEXT(".")) || Path.EndsWith(TEXT(".")))
	{
		OutErr = TEXT("Property path cannot start or end with a dot");
		return false;
	}

	if (Path.Contains(TEXT("..")))
	{
		OutErr = TEXT("Property path cannot contain consecutive dots");
		return false;
	}

	Path.ParseIntoArray(OutSegments, TEXT("."), true);

	if (OutSegments.Num() == 0)
	{
		OutErr = TEXT("Empty property path");
		return false;
	}

	return true;
}
