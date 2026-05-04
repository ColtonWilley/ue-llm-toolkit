// Copyright Natali Caggiano. All Rights Reserved.

/**
 * Unit tests for FPropertyPathParser — the canonical "Foo[N].Bar" path syntax used by
 * blueprint_query get_defaults, blueprint_modify set_cdo_default / set_component_default,
 * and set_property.
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PropertyPathParser.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== ParseSegment =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPropertyPathParser_ParseSegment_Plain,
	"UnrealClaude.PropertyPathParser.ParseSegment.Plain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FPropertyPathParser_ParseSegment_Plain::RunTest(const FString& Parameters)
{
	FString Name; int32 Index = 99; FString Err;

	TestTrue("Plain name parses", FPropertyPathParser::ParseSegment(TEXT("Foo"), Name, Index, Err));
	TestEqual("Plain name preserved", Name, TEXT("Foo"));
	TestEqual("Plain name has no index", Index, (int32)INDEX_NONE);
	TestTrue("Plain name has no error", Err.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPropertyPathParser_ParseSegment_Indexed,
	"UnrealClaude.PropertyPathParser.ParseSegment.Indexed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FPropertyPathParser_ParseSegment_Indexed::RunTest(const FString& Parameters)
{
	FString Name; int32 Index; FString Err;

	TestTrue("Indexed segment parses", FPropertyPathParser::ParseSegment(TEXT("Foo[3]"), Name, Index, Err));
	TestEqual("Name extracted", Name, TEXT("Foo"));
	TestEqual("Index extracted", Index, 3);

	TestTrue("Multi-digit index parses", FPropertyPathParser::ParseSegment(TEXT("Bar[123]"), Name, Index, Err));
	TestEqual("Multi-digit name", Name, TEXT("Bar"));
	TestEqual("Multi-digit value", Index, 123);

	TestTrue("Zero index parses", FPropertyPathParser::ParseSegment(TEXT("Items[0]"), Name, Index, Err));
	TestEqual("Zero index value", Index, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPropertyPathParser_ParseSegment_Malformed,
	"UnrealClaude.PropertyPathParser.ParseSegment.Malformed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FPropertyPathParser_ParseSegment_Malformed::RunTest(const FString& Parameters)
{
	FString Name; int32 Index; FString Err;

	TestFalse("Unclosed bracket rejected",       FPropertyPathParser::ParseSegment(TEXT("Foo["),       Name, Index, Err));
	TestFalse("Empty brackets rejected",         FPropertyPathParser::ParseSegment(TEXT("Foo[]"),      Name, Index, Err));
	TestFalse("Non-numeric index rejected",      FPropertyPathParser::ParseSegment(TEXT("Foo[abc]"),   Name, Index, Err));
	TestFalse("Trailing chars after ] rejected", FPropertyPathParser::ParseSegment(TEXT("Foo[3]bar"),  Name, Index, Err));
	TestFalse("Bracket without name rejected",   FPropertyPathParser::ParseSegment(TEXT("[3]"),        Name, Index, Err));
	TestFalse("Empty segment rejected",          FPropertyPathParser::ParseSegment(TEXT(""),           Name, Index, Err));

	return true;
}

// ===== SplitPath =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPropertyPathParser_SplitPath_Mixed,
	"UnrealClaude.PropertyPathParser.SplitPath.Mixed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FPropertyPathParser_SplitPath_Mixed::RunTest(const FString& Parameters)
{
	TArray<FString> Segments; FString Err;

	TestTrue("Mixed path splits", FPropertyPathParser::SplitPath(TEXT("AIPerception.SensesConfig[0].SightRadius"), Segments, Err));
	if (Segments.Num() == 3)
	{
		TestEqual("Segment 0", Segments[0], TEXT("AIPerception"));
		TestEqual("Segment 1", Segments[1], TEXT("SensesConfig[0]"));
		TestEqual("Segment 2", Segments[2], TEXT("SightRadius"));

		// Each segment must round-trip through ParseSegment.
		FString Name; int32 Index; FString PErr;
		TestTrue("Seg0 round-trips",  FPropertyPathParser::ParseSegment(Segments[0], Name, Index, PErr));
		TestEqual("Seg0 name", Name, TEXT("AIPerception"));
		TestEqual("Seg0 has no index", Index, (int32)INDEX_NONE);

		TestTrue("Seg1 round-trips",  FPropertyPathParser::ParseSegment(Segments[1], Name, Index, PErr));
		TestEqual("Seg1 name", Name, TEXT("SensesConfig"));
		TestEqual("Seg1 index", Index, 0);

		TestTrue("Seg2 round-trips",  FPropertyPathParser::ParseSegment(Segments[2], Name, Index, PErr));
		TestEqual("Seg2 name", Name, TEXT("SightRadius"));
		TestEqual("Seg2 has no index", Index, (int32)INDEX_NONE);
	}
	else
	{
		AddError(FString::Printf(TEXT("Expected 3 segments, got %d"), Segments.Num()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPropertyPathParser_SplitPath_RejectsEmpty,
	"UnrealClaude.PropertyPathParser.SplitPath.RejectsEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FPropertyPathParser_SplitPath_RejectsEmpty::RunTest(const FString& Parameters)
{
	TArray<FString> Segments; FString Err;

	TestFalse("Empty path rejected",        FPropertyPathParser::SplitPath(TEXT(""),         Segments, Err));
	TestFalse("Leading dot rejected",       FPropertyPathParser::SplitPath(TEXT(".Foo"),     Segments, Err));
	TestFalse("Trailing dot rejected",      FPropertyPathParser::SplitPath(TEXT("Foo."),     Segments, Err));
	TestFalse("Consecutive dots rejected",  FPropertyPathParser::SplitPath(TEXT("Foo..Bar"), Segments, Err));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPropertyPathParser_SplitPath_SingleSegment,
	"UnrealClaude.PropertyPathParser.SplitPath.SingleSegment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FPropertyPathParser_SplitPath_SingleSegment::RunTest(const FString& Parameters)
{
	TArray<FString> Segments; FString Err;

	TestTrue("Single name path splits", FPropertyPathParser::SplitPath(TEXT("MaxWalkSpeed"), Segments, Err));
	TestEqual("Single segment count", Segments.Num(), 1);
	if (Segments.Num() >= 1)
	{
		TestEqual("Single segment value", Segments[0], TEXT("MaxWalkSpeed"));
	}

	TestTrue("Single bracketed segment splits", FPropertyPathParser::SplitPath(TEXT("Items[0]"), Segments, Err));
	TestEqual("Single bracket segment count", Segments.Num(), 1);
	if (Segments.Num() >= 1)
	{
		TestEqual("Single bracket segment value", Segments[0], TEXT("Items[0]"));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
