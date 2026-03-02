// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Get the Unreal Engine output log
 * Supports structured parsing, filtering by category/verbosity, cursor-based incremental reads,
 * duplicate collapsing, and timestamp stripping.
 */
class FMCPTool_GetOutputLog : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_output_log");
		Info.Description = TEXT(
			"Retrieve recent entries from the Unreal Engine output log with structured filtering.\n\n"
			"Supports cursor-based incremental reads (since), category/verbosity filtering, "
			"duplicate collapsing (compact), and timestamp stripping to minimize token usage.\n\n"
			"Filter examples:\n"
			"- categories: [\"LogTemp\"] — only LogTemp lines\n"
			"- exclude_categories: [\"LogStreaming\",\"LogLinker\"] — hide noisy categories\n"
			"- min_verbosity: \"Warning\" — warnings, errors, and fatals only\n"
			"- since: true — only lines added since last call (biggest token saver)\n"
			"- compact: true — collapse consecutive duplicate messages\n\n"
			"Note: If both categories and exclude_categories are provided, exclude_categories is ignored."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("lines"), TEXT("number"), TEXT("Number of recent lines to return (default: 100, max: 1000)"), false, TEXT("100")),
			FMCPToolParameter(TEXT("filter"), TEXT("string"), TEXT("Substring text filter applied to raw lines (case-insensitive)"), false),
			FMCPToolParameter(TEXT("since"), TEXT("boolean"), TEXT("Only return lines added since last call (cursor-based). Resets on log rotation."), false, TEXT("false")),
			FMCPToolParameter(TEXT("categories"), TEXT("array"), TEXT("Include only lines from these log categories (case-insensitive). Array of strings."), false),
			FMCPToolParameter(TEXT("exclude_categories"), TEXT("array"), TEXT("Exclude lines from these categories. Ignored if 'categories' is set. Array of strings."), false),
			FMCPToolParameter(TEXT("min_verbosity"), TEXT("string"), TEXT("Minimum verbosity level: Display, Warning, Error, Fatal"), false),
			FMCPToolParameter(TEXT("compact"), TEXT("boolean"), TEXT("Collapse consecutive duplicate messages with (xN) count"), false, TEXT("false")),
			FMCPToolParameter(TEXT("strip_timestamps"), TEXT("boolean"), TEXT("Remove [timestamp][frame] prefix from output"), false, TEXT("false"))
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult ResolveLogFilePath(FString& OutPath) const;

	int64 LastReadOffset = 0;
	FString LastLogFilePath;
};
