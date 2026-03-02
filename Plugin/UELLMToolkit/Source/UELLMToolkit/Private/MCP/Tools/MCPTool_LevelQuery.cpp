// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_LevelQuery.h"
#include "LevelQueryHelper.h"

FMCPToolResult FMCPTool_LevelQuery::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Validate editor context
	UWorld* World = nullptr;
	if (auto Error = ValidateEditorContext(World))
	{
		return Error.GetValue();
	}

	// Get operation
	FString Operation;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, ParamError))
	{
		return ParamError.GetValue();
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("list"))
	{
		return ExecuteList(World);
	}
	else if (Operation == TEXT("find"))
	{
		FString Pattern = ExtractOptionalString(Params, TEXT("pattern"));
		if (Pattern.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("'find' operation requires a 'pattern' parameter"));
		}
		return ExecuteFind(World, Pattern);
	}
	else if (Operation == TEXT("info"))
	{
		FString ActorLabel = ExtractOptionalString(Params, TEXT("actor_label"));
		if (ActorLabel.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("'info' operation requires an 'actor_label' parameter"));
		}
		return ExecuteInfo(World, ActorLabel);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid operations: 'list', 'find', 'info'"), *Operation));
}

FMCPToolResult FMCPTool_LevelQuery::ExecuteList(UWorld* World)
{
	TSharedPtr<FJsonObject> Data = FLevelQueryHelper::ListGameplayActors(World);

	int32 GameplayCount = static_cast<int32>(Data->GetNumberField(TEXT("gameplay_actors")));
	int32 FilteredCount = static_cast<int32>(Data->GetNumberField(TEXT("filtered_count")));

	return FMCPToolResult::Success(
		FString::Printf(TEXT("%d gameplay actors (%d filtered)"), GameplayCount, FilteredCount),
		Data
	);
}

FMCPToolResult FMCPTool_LevelQuery::ExecuteFind(UWorld* World, const FString& Pattern)
{
	TSharedPtr<FJsonObject> Data = FLevelQueryHelper::FindActors(World, Pattern);

	int32 MatchCount = static_cast<int32>(Data->GetNumberField(TEXT("match_count")));

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d actors matching '%s'"), MatchCount, *Pattern),
		Data
	);
}

FMCPToolResult FMCPTool_LevelQuery::ExecuteInfo(UWorld* World, const FString& ActorLabel)
{
	TSharedPtr<FJsonObject> Data = FLevelQueryHelper::InspectActor(World, ActorLabel);

	if (Data->HasField(TEXT("error")))
	{
		return FMCPToolResult::Error(Data->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Actor info: %s"), *ActorLabel),
		Data
	);
}
