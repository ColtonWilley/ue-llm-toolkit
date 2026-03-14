// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_BlueprintQuery.h"
#include "BlueprintUtils.h"
#include "ComponentInspector.h"
#include "BlueprintGraphReader.h"
#include "PropertySerializer.h"
#include "MCP/MCPParamValidator.h"
#include "UnrealClaudeModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"

FMCPToolResult FMCPTool_BlueprintQuery::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Get operation type
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("list"))
	{
		return ExecuteList(Params);
	}
	else if (Operation == TEXT("inspect"))
	{
		return ExecuteInspect(Params);
	}
	else if (Operation == TEXT("get_graph"))
	{
		return ExecuteGetGraph(Params);
	}
	else if (Operation == TEXT("get_components"))
	{
		return ExecuteGetComponents(Params);
	}
	else if (Operation == TEXT("get_collision"))
	{
		return ExecuteGetCollision(Params);
	}
	else if (Operation == TEXT("get_event_graph"))
	{
		return ExecuteGetEventGraph(Params);
	}
	else if (Operation == TEXT("get_anim_graph"))
	{
		return ExecuteGetAnimGraph(Params);
	}
	else if (Operation == TEXT("get_state_machine_detail"))
	{
		return ExecuteGetStateMachineDetail(Params);
	}
	else if (Operation == TEXT("get_defaults"))
	{
		return ExecuteGetDefaults(Params);
	}
	else if (Operation == TEXT("find_nodes"))
	{
		return ExecuteFindNodes(Params);
	}
	else if (Operation == TEXT("get_node"))
	{
		return ExecuteGetNode(Params);
	}
	else if (Operation == TEXT("verify_connection"))
	{
		return ExecuteVerifyConnection(Params);
	}
	else if (Operation == TEXT("get_node_connections"))
	{
		return ExecuteGetNodeConnections(Params);
	}
	else if (Operation == TEXT("get_graph_summary"))
	{
		return ExecuteGetGraphSummary(Params);
	}
	else if (Operation == TEXT("get_exec_chain"))
	{
		return ExecuteGetExecChain(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid operations: 'list', 'inspect', 'get_graph', 'get_components', 'get_collision', 'get_event_graph', 'get_anim_graph', 'get_state_machine_detail', 'get_defaults', 'find_nodes', 'get_node', 'verify_connection', 'get_node_connections', 'get_graph_summary', 'get_exec_chain'"), *Operation));
}

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteList(const TSharedRef<FJsonObject>& Params)
{
	// Extract filters
	FString PathFilter = ExtractOptionalString(Params, TEXT("path_filter"), TEXT("/Game/"));
	FString TypeFilter = ExtractOptionalString(Params, TEXT("type_filter"));
	FString NameFilter = ExtractOptionalString(Params, TEXT("name_filter"));
	int32 Limit = ExtractOptionalNumber<int32>(Params, TEXT("limit"), 25);

	// Clamp limit
	Limit = FMath::Clamp(Limit, 1, 1000);

	// Validate path filter
	FString ValidationError;
	if (!PathFilter.IsEmpty() && !FMCPParamValidator::ValidateBlueprintPath(PathFilter, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Query AssetRegistry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Build filter
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
	}

	// Get assets
	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	// Process results
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 Count = 0;
	int32 TotalMatching = 0;

	for (const FAssetData& AssetData : AssetDataList)
	{
		// Get parent class name for filtering
		FString ParentClassName;
		FAssetDataTagMapSharedView::FFindTagResult ParentClassTag = AssetData.TagsAndValues.FindTag(FName("ParentClass"));
		if (ParentClassTag.IsSet())
		{
			ParentClassName = ParentClassTag.GetValue();
		}

		// Apply type filter
		if (!TypeFilter.IsEmpty())
		{
			if (!ParentClassName.Contains(TypeFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		// Apply name filter
		if (!NameFilter.IsEmpty())
		{
			if (!AssetData.AssetName.ToString().Contains(NameFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		TotalMatching++;

		// Check limit
		if (Count >= Limit)
		{
			continue;
		}

		// Get Blueprint type
		FString BlueprintType = TEXT("Normal");
		FAssetDataTagMapSharedView::FFindTagResult TypeTag = AssetData.TagsAndValues.FindTag(FName("BlueprintType"));
		if (TypeTag.IsSet())
		{
			BlueprintType = TypeTag.GetValue();
		}

		// Build result object
		TSharedPtr<FJsonObject> BPJson = MakeShared<FJsonObject>();
		BPJson->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		BPJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		BPJson->SetStringField(TEXT("blueprint_type"), BlueprintType);

		// Clean up parent class name (remove prefix)
		if (!ParentClassName.IsEmpty())
		{
			FString CleanParentName = ParentClassName;
			int32 LastDotIndex;
			if (CleanParentName.FindLastChar(TEXT('.'), LastDotIndex))
			{
				CleanParentName = CleanParentName.Mid(LastDotIndex + 1);
			}
			// Remove trailing '_C' from generated class names
			if (CleanParentName.EndsWith(TEXT("_C")))
			{
				CleanParentName = CleanParentName.LeftChop(2);
			}
			BPJson->SetStringField(TEXT("parent_class"), CleanParentName);
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(BPJson));
		Count++;
	}

	// Build response
	TSharedPtr<FJsonObject> ResponseData = MakeShared<FJsonObject>();
	ResponseData->SetArrayField(TEXT("blueprints"), ResultsArray);
	ResponseData->SetNumberField(TEXT("count"), Count);
	ResponseData->SetNumberField(TEXT("total_matching"), TotalMatching);

	if (TotalMatching > Count)
	{
		ResponseData->SetBoolField(TEXT("truncated"), true);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d Blueprints (showing %d)"), TotalMatching, Count),
		ResponseData
	);
}

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteInspect(const TSharedRef<FJsonObject>& Params)
{
	// Get Blueprint path
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	// Validate path
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Load Blueprint
	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	// Get options
	bool bIncludeVariables = ExtractOptionalBool(Params, TEXT("include_variables"), false);
	bool bIncludeFunctions = ExtractOptionalBool(Params, TEXT("include_functions"), false);
	bool bIncludeGraphs = ExtractOptionalBool(Params, TEXT("include_graphs"), false);

	// Serialize Blueprint info
	TSharedPtr<FJsonObject> BlueprintInfo = FBlueprintUtils::SerializeBlueprintInfo(
		Blueprint,
		bIncludeVariables,
		bIncludeFunctions,
		bIncludeGraphs
	);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Blueprint info for: %s"), *Blueprint->GetName()),
		BlueprintInfo
	);
}

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetGraph(const TSharedRef<FJsonObject>& Params)
{
	// Get Blueprint path
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	// Validate path
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Load Blueprint
	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	// Get graph info
	TSharedPtr<FJsonObject> GraphInfo = FBlueprintUtils::GetGraphInfo(Blueprint);

	// Add Blueprint name for context
	GraphInfo->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	GraphInfo->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Graph info for: %s"), *Blueprint->GetName()),
		GraphInfo
	);
}

// ============================================================================
// get_components
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetComponents(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString ComponentNameFilter = ExtractOptionalString(Params, TEXT("component_name"));

	if (!ComponentNameFilter.IsEmpty())
	{
		TSharedPtr<FJsonObject> Result = FComponentInspector::SerializeSingleComponent(Blueprint, ComponentNameFilter);
		if (Result->HasField(TEXT("error")))
		{
			return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
		}
		return FMCPToolResult::Success(
			FString::Printf(TEXT("Component '%s' on %s"), *ComponentNameFilter, *Blueprint->GetName()),
			Result
		);
	}

	TSharedPtr<FJsonObject> ComponentData = FComponentInspector::SerializeComponentTree(Blueprint);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Component tree for: %s (%d components)"),
			*Blueprint->GetName(),
			static_cast<int32>(ComponentData->GetNumberField(TEXT("total_components")))),
		ComponentData
	);
}

// ============================================================================
// get_collision
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetCollision(const TSharedRef<FJsonObject>& Params)
{
	// Check if we're inspecting a level actor or a blueprint
	FString ActorLabel = ExtractOptionalString(Params, TEXT("actor_label"));

	if (!ActorLabel.IsEmpty())
	{
		// Level actor mode
		UWorld* World = nullptr;
		if (auto Error = ValidateEditorContext(World))
		{
			return Error.GetValue();
		}

		AActor* Actor = FindActorByNameOrLabel(World, ActorLabel);
		if (!Actor)
		{
			return ActorNotFoundError(ActorLabel);
		}

		TSharedPtr<FJsonObject> CollisionData = FComponentInspector::SerializeCollisionForActor(Actor);
		return FMCPToolResult::Success(
			FString::Printf(TEXT("Collision settings for level actor: %s"), *ActorLabel),
			CollisionData
		);
	}

	// Blueprint CDO mode
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TSharedPtr<FJsonObject> CollisionData = FComponentInspector::SerializeCollisionForBlueprint(Blueprint);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Collision settings for: %s"), *Blueprint->GetName()),
		CollisionData
	);
}

// ============================================================================
// get_event_graph
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetEventGraph(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"));

	FBlueprintGraphReader::FGraphReadResult ReadResult =
		FBlueprintGraphReader::ReadEventGraph(Blueprint, GraphName);

	TSharedPtr<FJsonObject> ResponseData = MakeShared<FJsonObject>();
	ResponseData->SetStringField(TEXT("summary"), ReadResult.Summary);
	ResponseData->SetObjectField(TEXT("metadata"), ReadResult.Metadata);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Event graph for: %s"), *Blueprint->GetName()),
		ResponseData
	);
}

// ============================================================================
// get_anim_graph
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetAnimGraph(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"));

	FBlueprintGraphReader::FGraphReadResult ReadResult =
		FBlueprintGraphReader::ReadAnimGraph(Blueprint, GraphName);

	TSharedPtr<FJsonObject> ResponseData = MakeShared<FJsonObject>();
	ResponseData->SetStringField(TEXT("summary"), ReadResult.Summary);
	ResponseData->SetObjectField(TEXT("metadata"), ReadResult.Metadata);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Anim graph for: %s"), *Blueprint->GetName()),
		ResponseData
	);
}

// ============================================================================
// get_state_machine_detail
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetStateMachineDetail(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine_name"));

	FBlueprintGraphReader::FGraphReadResult ReadResult =
		FBlueprintGraphReader::ReadStateMachineDetail(Blueprint, StateMachineName);

	TSharedPtr<FJsonObject> ResponseData = MakeShared<FJsonObject>();
	ResponseData->SetStringField(TEXT("summary"), ReadResult.Summary);
	ResponseData->SetObjectField(TEXT("metadata"), ReadResult.Metadata);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("State machine detail for: %s"), *Blueprint->GetName()),
		ResponseData
	);
}

// ============================================================================
// get_defaults
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetDefaults(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	if (!Blueprint->GeneratedClass)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Blueprint '%s' has no GeneratedClass — compile it first"), *Blueprint->GetName()));
	}

	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to get CDO for Blueprint '%s'"), *Blueprint->GetName()));
	}

	TSharedPtr<FJsonObject> ResponseData = MakeShared<FJsonObject>();
	ResponseData->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	ResponseData->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	ResponseData->SetStringField(TEXT("class"), Blueprint->GeneratedClass->GetName());

	FString ParentClassName = TEXT("None");
	if (Blueprint->ParentClass)
	{
		ParentClassName = Blueprint->ParentClass->GetName();
	}
	ResponseData->SetStringField(TEXT("parent_class"), ParentClassName);

	const TArray<TSharedPtr<FJsonValue>>* PropertiesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("properties"), PropertiesArray) && PropertiesArray && PropertiesArray->Num() > 0)
	{
		TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Errors = MakeShared<FJsonObject>();
		int32 SuccessCount = 0;

		for (const TSharedPtr<FJsonValue>& PropVal : *PropertiesArray)
		{
			FString PropName;
			if (!PropVal->TryGetString(PropName) || PropName.IsEmpty())
			{
				continue;
			}

			FPropertySerializer::FPropertyPathResult Result = FPropertySerializer::GetPropertyByPath(CDO, PropName);
			if (Result.bSuccess)
			{
				Values->SetField(PropName, Result.Value);
				SuccessCount++;
			}
			else
			{
				Errors->SetStringField(PropName, Result.Error);
			}
		}

		ResponseData->SetObjectField(TEXT("values"), Values);
		ResponseData->SetNumberField(TEXT("count"), SuccessCount);

		if (Errors->Values.Num() > 0)
		{
			ResponseData->SetObjectField(TEXT("errors"), Errors);
		}

		return FMCPToolResult::Success(
			FString::Printf(TEXT("Read %d CDO properties from '%s'"), SuccessCount, *Blueprint->GetName()),
			ResponseData
		);
	}
	else
	{
		UObject* ParentCDO = nullptr;
		if (Blueprint->ParentClass)
		{
			ParentCDO = Blueprint->ParentClass->GetDefaultObject();
		}

		TSharedPtr<FJsonObject> Overrides = FPropertySerializer::GetCDOOverrides(CDO, ParentCDO, true);

		ResponseData->SetObjectField(TEXT("overrides"), Overrides);
		ResponseData->SetNumberField(TEXT("count"), Overrides->Values.Num());

		return FMCPToolResult::Success(
			FString::Printf(TEXT("Found %d CDO overrides on '%s'"), Overrides->Values.Num(), *Blueprint->GetName()),
			ResponseData
		);
	}
}

// ============================================================================
// find_nodes
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteFindNodes(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString Search = ExtractOptionalString(Params, TEXT("search"));
	FString NodeClass = ExtractOptionalString(Params, TEXT("node_class"));
	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"));
	FString GraphType = ExtractOptionalString(Params, TEXT("graph_type"));

	TSharedPtr<FJsonObject> Result = FBlueprintGraphReader::FindNodes(Blueprint, Search, NodeClass, GraphName, GraphType);

	int32 Count = static_cast<int32>(Result->GetNumberField(TEXT("count")));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d nodes in '%s'"), Count, *Blueprint->GetName()),
		Result
	);
}

// ============================================================================
// get_node
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetNode(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"));

	TSharedPtr<FJsonObject> Result = FBlueprintGraphReader::GetNode(Blueprint, NodeId, GraphName);

	if (Result->HasField(TEXT("error")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Node '%s' from '%s'"), *NodeId, *Blueprint->GetName()),
		Result
	);
}

// ============================================================================
// verify_connection
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteVerifyConnection(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString SourceNodeId;
	if (!ExtractRequiredString(Params, TEXT("source_node_id"), SourceNodeId, Error))
	{
		return Error.GetValue();
	}

	FString TargetNodeId;
	if (!ExtractRequiredString(Params, TEXT("target_node_id"), TargetNodeId, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString SourcePin = ExtractOptionalString(Params, TEXT("source_pin"));
	FString TargetPin = ExtractOptionalString(Params, TEXT("target_pin"));

	TSharedPtr<FJsonObject> Result = FBlueprintGraphReader::VerifyConnection(
		Blueprint, SourceNodeId, TargetNodeId, SourcePin, TargetPin);

	if (Result->HasField(TEXT("error")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	bool bConnected = Result->GetBoolField(TEXT("connected"));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Connection %s -> %s: %s"),
			*SourceNodeId, *TargetNodeId, bConnected ? TEXT("CONNECTED") : TEXT("NOT CONNECTED")),
		Result
	);
}

// ============================================================================
// get_node_connections
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetNodeConnections(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString Direction = ExtractOptionalString(Params, TEXT("direction"));
	FString PinFilter = ExtractOptionalString(Params, TEXT("pin_filter"));

	TSharedPtr<FJsonObject> Result = FBlueprintGraphReader::GetNodeConnections(Blueprint, NodeId, Direction, PinFilter);

	if (Result->HasField(TEXT("error")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Connections for node '%s' in '%s'"), *NodeId, *Blueprint->GetName()),
		Result
	);
}

// ============================================================================
// get_graph_summary
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetGraphSummary(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"));
	FString GraphType = ExtractOptionalString(Params, TEXT("graph_type"));

	TSharedPtr<FJsonObject> Result = FBlueprintGraphReader::GetGraphSummary(Blueprint, GraphName, GraphType);

	int32 GraphCount = static_cast<int32>(Result->GetNumberField(TEXT("graph_count")));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Graph summary for '%s' (%d graphs)"), *Blueprint->GetName(), GraphCount),
		Result
	);
}

// ============================================================================
// get_exec_chain
// ============================================================================

FMCPToolResult FMCPTool_BlueprintQuery::ExecuteGetExecChain(const TSharedRef<FJsonObject>& Params)
{
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(BlueprintPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	FString LoadError;
	UBlueprint* Blueprint = FBlueprintUtils::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return FMCPToolResult::Error(LoadError);
	}

	int32 MaxDepth = ExtractOptionalNumber<int32>(Params, TEXT("max_depth"), 50);

	TSharedPtr<FJsonObject> Result = FBlueprintGraphReader::GetExecChain(Blueprint, NodeId, MaxDepth);

	if (Result->HasField(TEXT("error")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	int32 NodeCount = static_cast<int32>(Result->GetNumberField(TEXT("node_count")));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Exec chain from '%s' (%d nodes)"), *NodeId, NodeCount),
		Result
	);
}
