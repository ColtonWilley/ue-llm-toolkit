// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Query level actors with noise filtering.
 * Coexists with get_level_actors (no breaking changes).
 *
 * Operations:
 *   - list: All gameplay-relevant actors grouped by class (noise filtered)
 *   - find: Case-insensitive substring search against label and class
 *   - info: Detailed dump of one actor (components + collision)
 *
 * Replaces: level_query.py
 */
class FMCPTool_LevelQuery : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("level_query");
		Info.Description = TEXT(
			"Query level actors with intelligent noise filtering.\n\n"
			"Operations:\n"
			"- 'list': All gameplay actors grouped by class (filters Landscape, Foliage, NavMesh, HLOD, etc.)\n"
			"- 'find': Case-insensitive substring search against actor label and class name\n"
			"- 'info': Detailed dump of one actor: transform, all components, collision settings\n\n"
			"Prefer this over get_level_actors for gameplay-focused queries.\n"
			"'list' excludes lights; 'find' includes them."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation: 'list', 'find', or 'info'"), true),
			FMCPToolParameter(TEXT("pattern"), TEXT("string"),
				TEXT("Search pattern for 'find' operation (case-insensitive substring)"), false),
			FMCPToolParameter(TEXT("actor_label"), TEXT("string"),
				TEXT("Exact actor label for 'info' operation"), false)
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult ExecuteList(UWorld* World);
	FMCPToolResult ExecuteFind(UWorld* World, const FString& Pattern);
	FMCPToolResult ExecuteInfo(UWorld* World, const FString& ActorLabel);
};
