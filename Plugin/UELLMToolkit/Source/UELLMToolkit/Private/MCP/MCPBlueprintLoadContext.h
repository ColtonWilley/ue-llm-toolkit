// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPToolRegistry.h"
#include "MCPParamValidator.h"
#include "BlueprintUtils.h"
#include "BlueprintLoader.h"
#include "Engine/Blueprint.h"

/**
 * Context helper for Blueprint load-validate-modify operations
 * Eliminates ~220 lines of duplicate boilerplate across MCP tools
 *
 * Usage:
 *   FMCPBlueprintLoadContext Context;
 *   if (auto Error = Context.LoadAndValidate(Params))
 *       return Error.GetValue();
 *
 *   // Use Context.Blueprint for operations
 *
 *   if (auto Error = Context.CompileAndFinalize())
 *       return Error.GetValue();
 */
class FMCPBlueprintLoadContext
{
public:
	/** The loaded Blueprint (valid after successful LoadAndValidate) */
	UBlueprint* Blueprint = nullptr;

	/** The Blueprint path that was loaded */
	FString BlueprintPath;

	/** Last error message (for custom error handling) */
	FString LastError;

	/** Detailed compile result (populated after CompileAndFinalize) */
	FBlueprintCompileResult CompileResult;

	/**
	 * Load and validate a Blueprint from JSON parameters
	 * Handles: path extraction, path validation, loading, and editability check
	 *
	 * @param Params - JSON parameters containing "blueprint_path"
	 * @param PathParamName - Name of the path parameter (default: "blueprint_path")
	 * @return Error result if any step fails, empty optional on success
	 */
	TOptional<FMCPToolResult> LoadAndValidate(
		const TSharedRef<FJsonObject>& Params,
		const FString& PathParamName = TEXT("blueprint_path"))
	{
		// Extract blueprint path
		if (!Params->TryGetStringField(PathParamName, BlueprintPath) || BlueprintPath.IsEmpty())
		{
			LastError = FString::Printf(TEXT("Missing required parameter: %s"), *PathParamName);
			return FMCPToolResult::Error(LastError);
		}

		// Validate path (security check)
		if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, LastError))
		{
			return FMCPToolResult::Error(LastError);
		}

		// Load Blueprint
		Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LastError);
		if (!Blueprint)
		{
			return FMCPToolResult::Error(LastError);
		}

		// Check if editable
		if (!FBlueprintUtils::IsBlueprintEditable(Blueprint, LastError))
		{
			return FMCPToolResult::Error(LastError);
		}

		return TOptional<FMCPToolResult>();
	}

	/**
	 * Load a Blueprint without editability check (for query operations)
	 *
	 * @param Params - JSON parameters containing the path
	 * @param PathParamName - Name of the path parameter
	 * @return Error result if loading fails, empty optional on success
	 */
	TOptional<FMCPToolResult> LoadForQuery(
		const TSharedRef<FJsonObject>& Params,
		const FString& PathParamName = TEXT("blueprint_path"))
	{
		// Extract blueprint path
		if (!Params->TryGetStringField(PathParamName, BlueprintPath) || BlueprintPath.IsEmpty())
		{
			LastError = FString::Printf(TEXT("Missing required parameter: %s"), *PathParamName);
			return FMCPToolResult::Error(LastError);
		}

		// Validate path (security check)
		if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, LastError))
		{
			return FMCPToolResult::Error(LastError);
		}

		// Load Blueprint
		Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LastError);
		if (!Blueprint)
		{
			return FMCPToolResult::Error(LastError);
		}

		return TOptional<FMCPToolResult>();
	}

	/**
	 * Compile the Blueprint and mark it dirty
	 * Call this after making modifications
	 * Stores detailed compile result in CompileResult member
	 *
	 * @param OperationName - Name of the operation for error messages
	 * @return Error result if compilation fails, empty optional on success
	 */
	TOptional<FMCPToolResult> CompileAndFinalize(const FString& OperationName = TEXT("Operation"))
	{
		if (!Blueprint)
		{
			LastError = TEXT("No Blueprint loaded");
			return FMCPToolResult::Error(LastError);
		}

		// Compile with detailed result capture
		CompileResult = FBlueprintLoader::CompileBlueprintWithResult(Blueprint);

		if (!CompileResult.bSuccess)
		{
			LastError = CompileResult.VerboseOutput;
			return FMCPToolResult::Error(FString::Printf(
				TEXT("%s succeeded but compilation failed:\n%s"), *OperationName, *CompileResult.VerboseOutput));
		}

		// Mark dirty
		FBlueprintUtils::MarkBlueprintDirty(Blueprint);

		return TOptional<FMCPToolResult>();
	}

	/**
	 * Build standard result JSON with blueprint info and compile details
	 * Includes compile messages (errors/warnings) when present
	 * @return JSON object with blueprint_path, compiled, and compile_output fields
	 */
	TSharedPtr<FJsonObject> BuildResultJson() const
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		if (Blueprint)
		{
			ResultData->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
			ResultData->SetBoolField(TEXT("compiled"), CompileResult.bSuccess);
			ResultData->SetStringField(TEXT("compile_status"), CompileResult.StatusString);

			// Always include compile info when there are messages (errors OR warnings)
			if (CompileResult.HasIssues() || !CompileResult.bSuccess)
			{
				ResultData->SetNumberField(TEXT("error_count"), CompileResult.ErrorCount);
				ResultData->SetNumberField(TEXT("warning_count"), CompileResult.WarningCount);
				ResultData->SetStringField(TEXT("compile_output"), CompileResult.VerboseOutput);

				// Include structured messages array
				TArray<TSharedPtr<FJsonValue>> MessagesArray;
				for (const FBlueprintCompileMessage& Msg : CompileResult.Messages)
				{
					TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
					MsgObj->SetStringField(TEXT("severity"), Msg.Severity);
					MsgObj->SetStringField(TEXT("message"), Msg.Message);
					if (!Msg.NodeName.IsEmpty())
					{
						MsgObj->SetStringField(TEXT("node"), Msg.NodeName);
					}
					if (!Msg.ObjectPath.IsEmpty())
					{
						MsgObj->SetStringField(TEXT("object_path"), Msg.ObjectPath);
					}
					MessagesArray.Add(MakeShared<FJsonValueObject>(MsgObj));
				}
				ResultData->SetArrayField(TEXT("compile_messages"), MessagesArray);
			}
		}
		return ResultData;
	}

	/**
	 * Check if Blueprint is valid
	 */
	bool IsValid() const { return Blueprint != nullptr; }

	/**
	 * Get the Blueprint (convenience accessor)
	 */
	UBlueprint* Get() const { return Blueprint; }
};
