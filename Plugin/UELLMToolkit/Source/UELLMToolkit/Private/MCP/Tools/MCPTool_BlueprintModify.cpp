// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_BlueprintModify.h"
#include "BlueprintUtils.h"
#include "BlueprintGraphEditor.h"
#include "GraphLayoutHelper.h"
#include "DebugPrintBuilder.h"
#include "BlueprintLoader.h"
#include "MCP/MCPParamValidator.h"
#include "MCP/MCPBlueprintLoadContext.h"
#include "UnrealClaudeModule.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Operation name constants
namespace BlueprintModifyOps
{
	static const FString Create = TEXT("create");
	static const FString Reparent = TEXT("reparent");
	static const FString AddVariable = TEXT("add_variable");
	static const FString RemoveVariable = TEXT("remove_variable");
	static const FString AddFunction = TEXT("add_function");
	static const FString RemoveFunction = TEXT("remove_function");
	static const FString AddNode = TEXT("add_node");
	static const FString AddNodes = TEXT("add_nodes");
	static const FString DeleteNode = TEXT("delete_node");
	static const FString ConnectPins = TEXT("connect_pins");
	static const FString DisconnectPins = TEXT("disconnect_pins");
	static const FString SetPinValue = TEXT("set_pin_value");
	static const FString SetComponentDefault = TEXT("set_component_default");
	static const FString SetCDODefault = TEXT("set_cdo_default");
	static const FString AddDebugPrint = TEXT("add_debug_print");
	static const FString RemoveDebugPrint = TEXT("remove_debug_print");
	static const FString LayoutGraph = TEXT("layout_graph");
	static const FString Compile = TEXT("compile");
	static const FString CompileAll = TEXT("compile_all");
}

FMCPToolResult FMCPTool_BlueprintModify::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Get operation type
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	// Level 2: Variable/Function Operations
	if (Operation == BlueprintModifyOps::Create)
	{
		return ExecuteCreate(Params);
	}
	if (Operation == BlueprintModifyOps::Reparent)
	{
		return ExecuteReparent(Params);
	}
	if (Operation == BlueprintModifyOps::AddVariable)
	{
		return ExecuteAddVariable(Params);
	}
	if (Operation == BlueprintModifyOps::RemoveVariable)
	{
		return ExecuteRemoveVariable(Params);
	}
	if (Operation == BlueprintModifyOps::AddFunction)
	{
		return ExecuteAddFunction(Params);
	}
	if (Operation == BlueprintModifyOps::RemoveFunction)
	{
		return ExecuteRemoveFunction(Params);
	}
	// Level 3: Node Operations
	if (Operation == BlueprintModifyOps::AddNode)
	{
		return ExecuteAddNode(Params);
	}
	if (Operation == BlueprintModifyOps::AddNodes)
	{
		return ExecuteAddNodes(Params);
	}
	if (Operation == BlueprintModifyOps::DeleteNode)
	{
		return ExecuteDeleteNode(Params);
	}
	// Level 4: Connection Operations
	if (Operation == BlueprintModifyOps::ConnectPins)
	{
		return ExecuteConnectPins(Params);
	}
	if (Operation == BlueprintModifyOps::DisconnectPins)
	{
		return ExecuteDisconnectPins(Params);
	}
	if (Operation == BlueprintModifyOps::SetPinValue)
	{
		return ExecuteSetPinValue(Params);
	}
	// Level 5: Default Operations
	if (Operation == BlueprintModifyOps::SetComponentDefault)
	{
		return ExecuteSetComponentDefault(Params);
	}
	if (Operation == BlueprintModifyOps::SetCDODefault)
	{
		return ExecuteSetCDODefault(Params);
	}
	// Level 6: Debug Operations
	if (Operation == BlueprintModifyOps::AddDebugPrint)
	{
		return ExecuteAddDebugPrint(Params);
	}
	if (Operation == BlueprintModifyOps::RemoveDebugPrint)
	{
		return ExecuteRemoveDebugPrint(Params);
	}
	// Level 7: Layout Operations
	if (Operation == BlueprintModifyOps::LayoutGraph)
	{
		return ExecuteLayoutGraph(Params);
	}
	// Validation Operations
	if (Operation == BlueprintModifyOps::Compile)
	{
		return ExecuteCompile(Params);
	}
	if (Operation == BlueprintModifyOps::CompileAll)
	{
		return ExecuteCompileAll(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid: create, reparent, add_variable, remove_variable, add_function, remove_function, add_node, add_nodes, delete_node, connect_pins, disconnect_pins, set_pin_value, set_component_default, set_cdo_default, add_debug_print, remove_debug_print, layout_graph, compile, compile_all"),
		*Operation));
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteCreate(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	FString PackagePath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Error))
	{
		return Error.GetValue();
	}

	FString BlueprintName;
	if (!ExtractRequiredString(Params, TEXT("blueprint_name"), BlueprintName, Error))
	{
		return Error.GetValue();
	}

	FString ParentClassName;
	if (!ExtractRequiredString(Params, TEXT("parent_class"), ParentClassName, Error))
	{
		return Error.GetValue();
	}

	FString BlueprintTypeStr = ExtractOptionalString(Params, TEXT("blueprint_type"), TEXT("Normal"));

	// Validate package path
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(PackagePath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Validate Blueprint name
	if (!FMCPParamValidator::ValidateBlueprintVariableName(BlueprintName, ValidationError))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Invalid Blueprint name: %s"), *ValidationError));
	}

	// Find parent class
	FString ClassError;
	UClass* ParentClass = FBlueprintUtils::FindParentClass(ParentClassName, ClassError);
	if (!ParentClass)
	{
		return FMCPToolResult::Error(ClassError);
	}

	// Parse Blueprint type
	EBlueprintType BlueprintType = ParseBlueprintType(BlueprintTypeStr);

	// Create the Blueprint
	FString CreateError;
	UBlueprint* NewBlueprint = FBlueprintUtils::CreateBlueprint(
		PackagePath,
		BlueprintName,
		ParentClass,
		BlueprintType,
		CreateError
	);

	if (!NewBlueprint)
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), NewBlueprint->GetName());
	ResultData->SetStringField(TEXT("blueprint_path"), NewBlueprint->GetPathName());
	ResultData->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	ResultData->SetStringField(TEXT("blueprint_type"), FBlueprintUtils::GetBlueprintTypeString(BlueprintType));
	ResultData->SetBoolField(TEXT("compiled"), true);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created Blueprint: %s"), *NewBlueprint->GetPathName()),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteReparent(const TSharedRef<FJsonObject>& Params)
{
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	FString NewParentClassName;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("parent_class"), NewParentClassName, Error))
	{
		return Error.GetValue();
	}

	FString ClassError;
	UClass* NewParentClass = FBlueprintUtils::FindParentClass(NewParentClassName, ClassError);
	if (!NewParentClass)
	{
		return FMCPToolResult::Error(ClassError);
	}

	if (Context.Blueprint->ParentClass == NewParentClass)
	{
		return FMCPToolResult::Success("Blueprint already has this parent class");
	}

	Context.Blueprint->Modify();
	if (USimpleConstructionScript* SCS = Context.Blueprint->SimpleConstructionScript)
	{
		SCS->Modify();
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			Node->Modify();
		}
	}

	Context.Blueprint->ParentClass = NewParentClass;
	FBlueprintEditorUtils::RefreshAllNodes(Context.Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Context.Blueprint);

	if (auto CompileError = Context.CompileAndFinalize(TEXT("Reparented")))
	{
		return CompileError.GetValue();
	}

	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("new_parent_class"), NewParentClass->GetPathName());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Reparented to '%s'"), *NewParentClass->GetName()),
		ResultData);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddVariable(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString VariableName;
	if (!ExtractRequiredString(Params, TEXT("variable_name"), VariableName, Error))
	{
		return Error.GetValue();
	}

	FString VariableType;
	if (!ExtractRequiredString(Params, TEXT("variable_type"), VariableType, Error))
	{
		return Error.GetValue();
	}

	// Validate variable name
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintVariableName(VariableName, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Parse variable type
	FEdGraphPinType PinType;
	FString TypeError;
	if (!FBlueprintUtils::ParsePinType(VariableType, PinType, TypeError))
	{
		return FMCPToolResult::Error(TypeError);
	}

	// Add the variable
	FString AddError;
	if (!FBlueprintUtils::AddVariable(Context.Blueprint, VariableName, PinType, AddError))
	{
		return FMCPToolResult::Error(AddError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Variable added")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("variable_type"), VariableType);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added variable '%s' (%s) to Blueprint"), *VariableName, *VariableType),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteRemoveVariable(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString VariableName;
	if (!ExtractRequiredString(Params, TEXT("variable_name"), VariableName, Error))
	{
		return Error.GetValue();
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Remove the variable
	FString RemoveError;
	if (!FBlueprintUtils::RemoveVariable(Context.Blueprint, VariableName, RemoveError))
	{
		return FMCPToolResult::Error(RemoveError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Variable removed")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed variable '%s' from Blueprint"), *VariableName),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddFunction(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString FunctionName;
	if (!ExtractRequiredString(Params, TEXT("function_name"), FunctionName, Error))
	{
		return Error.GetValue();
	}

	// Validate function name
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintFunctionName(FunctionName, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Add the function
	FString AddError;
	if (!FBlueprintUtils::AddFunction(Context.Blueprint, FunctionName, AddError))
	{
		return FMCPToolResult::Error(AddError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Function added")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("function_name"), FunctionName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added function '%s' to Blueprint"), *FunctionName),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteRemoveFunction(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString FunctionName;
	if (!ExtractRequiredString(Params, TEXT("function_name"), FunctionName, Error))
	{
		return Error.GetValue();
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Remove the function
	FString RemoveError;
	if (!FBlueprintUtils::RemoveFunction(Context.Blueprint, FunctionName, RemoveError))
	{
		return FMCPToolResult::Error(RemoveError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Function removed")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("function_name"), FunctionName);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed function '%s' from Blueprint"), *FunctionName),
		ResultData
	);
}

EBlueprintType FMCPTool_BlueprintModify::ParseBlueprintType(const FString& TypeString)
{
	FString LowerType = TypeString.ToLower();

	if (LowerType == TEXT("normal") || LowerType == TEXT("actor") || LowerType == TEXT("object"))
	{
		return BPTYPE_Normal;
	}
	if (LowerType == TEXT("functionlibrary") || LowerType == TEXT("function_library"))
	{
		return BPTYPE_FunctionLibrary;
	}
	if (LowerType == TEXT("interface"))
	{
		return BPTYPE_Interface;
	}
	if (LowerType == TEXT("macrolibrary") || LowerType == TEXT("macro_library") || LowerType == TEXT("macro"))
	{
		return BPTYPE_MacroLibrary;
	}

	// Default to normal
	return BPTYPE_Normal;
}

// ===== Level 3: Node Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddNode(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeType;
	if (!ExtractRequiredString(Params, TEXT("node_type"), NodeType, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);
	int32 PosX = (int32)ExtractOptionalNumber(Params, TEXT("pos_x"), 0);
	int32 PosY = (int32)ExtractOptionalNumber(Params, TEXT("pos_y"), 0);

	// Get node params object
	TSharedPtr<FJsonObject> NodeParams;
	const TSharedPtr<FJsonObject>* NodeParamsPtr;
	if (Params->TryGetObjectField(TEXT("node_params"), NodeParamsPtr))
	{
		NodeParams = *NodeParamsPtr;
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Create the node
	FString NodeId;
	FString CreateError;
	UEdGraphNode* NewNode = FBlueprintUtils::CreateNode(Graph, NodeType, NodeParams, PosX, PosY, NodeId, CreateError);
	if (!NewNode)
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Apply pin default values if provided
	if (NodeParams.IsValid())
	{
		const TSharedPtr<FJsonObject>* PinValuesPtr;
		if (NodeParams->TryGetObjectField(TEXT("pin_values"), PinValuesPtr))
		{
			for (const auto& PinValue : (*PinValuesPtr)->Values)
			{
				FString PinValueStr;
				if (PinValue.Value->TryGetString(PinValueStr))
				{
					FString PinError;
					FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinValue.Key, PinValueStr, PinError);
				}
			}
		}
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Node created")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = FBlueprintUtils::SerializeNodeInfo(NewNode);
	ResultData->SetStringField(TEXT("blueprint_path"), Context.Blueprint->GetPathName());
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created node '%s' (type: %s)"), *NodeId, *NodeType),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddNodes(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Get nodes array
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	if (!Params->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		return FMCPToolResult::Error(TEXT("'nodes' array is required"));
	}

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Create all nodes using helper
	TArray<FString> CreatedNodeIds;
	TArray<TSharedPtr<FJsonValue>> CreatedNodes;
	FString CreateError;
	if (!CreateNodesFromSpec(Graph, *NodesArray, CreatedNodeIds, CreatedNodes, CreateError))
	{
		return FMCPToolResult::Error(CreateError);
	}

	// Process connections using helper
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
	if (Params->TryGetArrayField(TEXT("connections"), ConnectionsArray))
	{
		ProcessNodeConnections(Graph, *ConnectionsArray, CreatedNodeIds);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Nodes created")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
	ResultData->SetArrayField(TEXT("nodes"), CreatedNodes);
	ResultData->SetNumberField(TEXT("node_count"), CreatedNodeIds.Num());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created %d nodes"), CreatedNodeIds.Num()),
		ResultData
	);
}

bool FMCPTool_BlueprintModify::CreateNodesFromSpec(
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& NodesArray,
	TArray<FString>& OutCreatedNodeIds,
	TArray<TSharedPtr<FJsonValue>>& OutCreatedNodes,
	FString& OutError)
{
	for (int32 i = 0; i < NodesArray.Num(); i++)
	{
		const TSharedPtr<FJsonObject>* NodeSpec;
		if (!NodesArray[i]->TryGetObject(NodeSpec))
		{
			OutError = FString::Printf(TEXT("Node at index %d is not a valid object"), i);
			return false;
		}

		FString NodeType = (*NodeSpec)->GetStringField(TEXT("type"));
		if (NodeType.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Node at index %d missing 'type' field"), i);
			return false;
		}

		int32 PosX = (int32)(*NodeSpec)->GetNumberField(TEXT("pos_x"));
		int32 PosY = (int32)(*NodeSpec)->GetNumberField(TEXT("pos_y"));

		// Get params (could be inline or nested)
		TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* ParamsPtr;
		if ((*NodeSpec)->TryGetObjectField(TEXT("params"), ParamsPtr))
		{
			NodeParams = *ParamsPtr;
		}
		else
		{
			// Copy common fields to params
			if ((*NodeSpec)->HasField(TEXT("function")))
				NodeParams->SetStringField(TEXT("function"), (*NodeSpec)->GetStringField(TEXT("function")));
			if ((*NodeSpec)->HasField(TEXT("target_class")))
				NodeParams->SetStringField(TEXT("target_class"), (*NodeSpec)->GetStringField(TEXT("target_class")));
			if ((*NodeSpec)->HasField(TEXT("event")))
				NodeParams->SetStringField(TEXT("event"), (*NodeSpec)->GetStringField(TEXT("event")));
			if ((*NodeSpec)->HasField(TEXT("variable")))
				NodeParams->SetStringField(TEXT("variable"), (*NodeSpec)->GetStringField(TEXT("variable")));
			if ((*NodeSpec)->HasField(TEXT("num_outputs")))
				NodeParams->SetNumberField(TEXT("num_outputs"), (*NodeSpec)->GetNumberField(TEXT("num_outputs")));
		}

		// Create node
		FString NodeId;
		FString CreateError;
		UEdGraphNode* NewNode = FBlueprintUtils::CreateNode(Graph, NodeType, NodeParams, PosX, PosY, NodeId, CreateError);
		if (!NewNode)
		{
			OutError = FString::Printf(TEXT("Failed to create node %d: %s"), i, *CreateError);
			return false;
		}

		OutCreatedNodeIds.Add(NodeId);

		// Apply pin default values if provided
		const TSharedPtr<FJsonObject>* PinValuesPtr;
		if ((*NodeSpec)->TryGetObjectField(TEXT("pin_values"), PinValuesPtr))
		{
			for (const auto& PinValue : (*PinValuesPtr)->Values)
			{
				FString PinValueStr;
				if (PinValue.Value->TryGetString(PinValueStr))
				{
					FString PinError;
					FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinValue.Key, PinValueStr, PinError);
				}
			}
		}

		// Add to result
		TSharedPtr<FJsonObject> NodeInfo = FBlueprintUtils::SerializeNodeInfo(NewNode);
		NodeInfo->SetNumberField(TEXT("index"), i);
		OutCreatedNodes.Add(MakeShared<FJsonValueObject>(NodeInfo));
	}

	return true;
}

void FMCPTool_BlueprintModify::ProcessNodeConnections(
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& ConnectionsArray,
	const TArray<FString>& CreatedNodeIds)
{
	for (int32 i = 0; i < ConnectionsArray.Num(); i++)
	{
		const TSharedPtr<FJsonObject>* ConnSpec;
		if (!ConnectionsArray[i]->TryGetObject(ConnSpec))
		{
			continue;
		}

		// Get source - can be index or node_id
		FString SourceNodeId;
		if ((*ConnSpec)->HasTypedField<EJson::Number>(TEXT("from_node")))
		{
			int32 FromIndex = (int32)(*ConnSpec)->GetNumberField(TEXT("from_node"));
			if (FromIndex >= 0 && FromIndex < CreatedNodeIds.Num())
			{
				SourceNodeId = CreatedNodeIds[FromIndex];
			}
		}
		else if ((*ConnSpec)->HasTypedField<EJson::String>(TEXT("from_node")))
		{
			SourceNodeId = (*ConnSpec)->GetStringField(TEXT("from_node"));
		}

		// Get target - can be index or node_id
		FString TargetNodeId;
		if ((*ConnSpec)->HasTypedField<EJson::Number>(TEXT("to_node")))
		{
			int32 ToIndex = (int32)(*ConnSpec)->GetNumberField(TEXT("to_node"));
			if (ToIndex >= 0 && ToIndex < CreatedNodeIds.Num())
			{
				TargetNodeId = CreatedNodeIds[ToIndex];
			}
		}
		else if ((*ConnSpec)->HasTypedField<EJson::String>(TEXT("to_node")))
		{
			TargetNodeId = (*ConnSpec)->GetStringField(TEXT("to_node"));
		}

		FString SourcePin = (*ConnSpec)->GetStringField(TEXT("from_pin"));
		FString TargetPin = (*ConnSpec)->GetStringField(TEXT("to_pin"));

		if (!SourceNodeId.IsEmpty() && !TargetNodeId.IsEmpty())
		{
			FString ConnectError;
			FBlueprintUtils::ConnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, ConnectError);
		}
	}
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteDeleteNode(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Delete the node
	FString DeleteError;
	if (!FBlueprintUtils::DeleteNode(Graph, NodeId, DeleteError))
	{
		return FMCPToolResult::Error(DeleteError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Node deleted")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("node_id"), NodeId);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Deleted node '%s'"), *NodeId),
		ResultData
	);
}

// ===== Level 4: Connection Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteConnectPins(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
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

	FString SourcePin = ExtractOptionalString(Params, TEXT("source_pin"), TEXT(""));
	FString TargetPin = ExtractOptionalString(Params, TEXT("target_pin"), TEXT(""));
	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Connect the pins
	FString ConnectError;
	if (!FBlueprintUtils::ConnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, ConnectError))
	{
		return FMCPToolResult::Error(ConnectError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pins connected")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("source_node_id"), SourceNodeId);
	ResultData->SetStringField(TEXT("source_pin"), SourcePin.IsEmpty() ? TEXT("(auto exec)") : SourcePin);
	ResultData->SetStringField(TEXT("target_node_id"), TargetNodeId);
	ResultData->SetStringField(TEXT("target_pin"), TargetPin.IsEmpty() ? TEXT("(auto exec)") : TargetPin);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Connected '%s' -> '%s'"), *SourceNodeId, *TargetNodeId),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteDisconnectPins(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString SourceNodeId;
	if (!ExtractRequiredString(Params, TEXT("source_node_id"), SourceNodeId, Error))
	{
		return Error.GetValue();
	}

	FString SourcePin;
	if (!ExtractRequiredString(Params, TEXT("source_pin"), SourcePin, Error))
	{
		return Error.GetValue();
	}

	FString TargetNodeId;
	if (!ExtractRequiredString(Params, TEXT("target_node_id"), TargetNodeId, Error))
	{
		return Error.GetValue();
	}

	FString TargetPin;
	if (!ExtractRequiredString(Params, TEXT("target_pin"), TargetPin, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Disconnect the pins
	FString DisconnectError;
	if (!FBlueprintUtils::DisconnectPins(Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin, DisconnectError))
	{
		return FMCPToolResult::Error(DisconnectError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pins disconnected")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("source_node_id"), SourceNodeId);
	ResultData->SetStringField(TEXT("source_pin"), SourcePin);
	ResultData->SetStringField(TEXT("target_node_id"), TargetNodeId);
	ResultData->SetStringField(TEXT("target_pin"), TargetPin);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Disconnected '%s.%s' from '%s.%s'"), *SourceNodeId, *SourcePin, *TargetNodeId, *TargetPin),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteSetPinValue(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString NodeId;
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Error))
	{
		return Error.GetValue();
	}

	FString PinName;
	if (!ExtractRequiredString(Params, TEXT("pin_name"), PinName, Error))
	{
		return Error.GetValue();
	}

	FString PinValue;
	if (!ExtractRequiredString(Params, TEXT("pin_value"), PinValue, Error))
	{
		return Error.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"), TEXT(""));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find graph
	FString GraphError;
	UEdGraph* Graph = FBlueprintUtils::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, GraphError);
	if (!Graph)
	{
		return FMCPToolResult::Error(GraphError);
	}

	// Set the pin value
	FString SetError;
	if (!FBlueprintUtils::SetPinDefaultValue(Graph, NodeId, PinName, PinValue, SetError))
	{
		return FMCPToolResult::Error(SetError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("Pin value set")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("node_id"), NodeId);
	ResultData->SetStringField(TEXT("pin_name"), PinName);
	ResultData->SetStringField(TEXT("pin_value"), PinValue);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set '%s.%s' = '%s'"), *NodeId, *PinName, *PinValue),
		ResultData
	);
}

// ===== Level 5: Component Default Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteSetComponentDefault(const TSharedRef<FJsonObject>& Params)
{
	// Extract parameters
	TOptional<FMCPToolResult> Error;
	FString ComponentName;
	if (!ExtractRequiredString(Params, TEXT("component_name"), ComponentName, Error))
	{
		return Error.GetValue();
	}

	FString PropertyPath;
	if (!ExtractRequiredString(Params, TEXT("property"), PropertyPath, Error))
	{
		return Error.GetValue();
	}

	if (!Params->HasField(TEXT("value")))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: value"));
	}
	TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));

	// Load and validate Blueprint
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Find the component template
	UObject* ComponentTemplate = nullptr;

	// Strategy 1: Search SCS nodes (Blueprint-origin components)
	if (Context.Blueprint->SimpleConstructionScript)
	{
		const TArray<USCS_Node*>& AllNodes = Context.Blueprint->SimpleConstructionScript->GetAllNodes();
		for (USCS_Node* Node : AllNodes)
		{
			if (Node && Node->ComponentTemplate && Node->GetVariableName().ToString() == ComponentName)
			{
				ComponentTemplate = Node->ComponentTemplate;
				break;
			}
		}
	}

	// Strategy 2: Fall back to CDO components (C++-origin components like capsule, mesh, movement)
	if (!ComponentTemplate && Context.Blueprint->GeneratedClass)
	{
		AActor* CDO = Cast<AActor>(Context.Blueprint->GeneratedClass->GetDefaultObject());
		if (CDO)
		{
			TArray<UActorComponent*> Components;
			CDO->GetComponents(Components);
			for (UActorComponent* Comp : Components)
			{
				if (Comp && Comp->GetName() == ComponentName)
				{
					ComponentTemplate = Comp;
					break;
				}
			}
		}
	}

	if (!ComponentTemplate)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Component '%s' not found in Blueprint '%s'. Use blueprint_query get_components to see available components."),
			*ComponentName, *Context.BlueprintPath));
	}

	// Register with undo system
	ComponentTemplate->Modify();

	// Set the property via reflection
	FString SetError;
	if (!SetComponentPropertyFromJson(ComponentTemplate, PropertyPath, Value, SetError))
	{
		return FMCPToolResult::Error(SetError);
	}

	// Compile and finalize
	if (auto CompileError = Context.CompileAndFinalize(TEXT("set_component_default")))
	{
		return CompileError.GetValue();
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("component_name"), ComponentName);
	ResultData->SetStringField(TEXT("property"), PropertyPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set '%s.%s' on Blueprint '%s'"), *ComponentName, *PropertyPath, *Context.BlueprintPath),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteSetCDODefault(const TSharedRef<FJsonObject>& Params)
{
	TOptional<FMCPToolResult> Error;
	FString PropertyPath;
	if (!ExtractRequiredString(Params, TEXT("property"), PropertyPath, Error))
	{
		return Error.GetValue();
	}

	if (!Params->HasField(TEXT("value")))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: value"));
	}
	TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));

	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	if (!Context.Blueprint->GeneratedClass)
	{
		return FMCPToolResult::Error(TEXT("Blueprint has no GeneratedClass — compile it first"));
	}

	UObject* CDO = Context.Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		return FMCPToolResult::Error(TEXT("Failed to get Class Default Object"));
	}

	CDO->Modify();

	FString SetError;
	if (!SetComponentPropertyFromJson(CDO, PropertyPath, Value, SetError))
	{
		return FMCPToolResult::Error(SetError);
	}

	if (auto CompileError = Context.CompileAndFinalize(TEXT("set_cdo_default")))
	{
		return CompileError.GetValue();
	}

	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("property"), PropertyPath);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set CDO property '%s' on Blueprint '%s'"), *PropertyPath, *Context.BlueprintPath),
		ResultData
	);
}

bool FMCPTool_BlueprintModify::SetComponentPropertyFromJson(UObject* Template, const FString& PropertyPath, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Template || !Value.IsValid())
	{
		OutError = TEXT("Invalid template object or value");
		return false;
	}

	// Parse dot-separated property path
	TArray<FString> PathParts;
	PropertyPath.ParseIntoArray(PathParts, TEXT("."), true);

	if (PathParts.Num() == 0)
	{
		OutError = TEXT("Empty property path");
		return false;
	}

	// Navigate to the leaf property
	UStruct* CurrentStruct = Template->GetClass();
	void* CurrentContainer = Template;

	for (int32 i = 0; i < PathParts.Num() - 1; ++i)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *PathParts[i], *CurrentStruct->GetName());
			return false;
		}

		FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		if (StructProp)
		{
			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
			continue;
		}

		FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
		if (ObjProp)
		{
			UObject* NestedObj = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CurrentContainer));
			if (!NestedObj)
			{
				OutError = FString::Printf(TEXT("Nested object '%s' is null"), *PathParts[i]);
				return false;
			}
			CurrentContainer = NestedObj;
			CurrentStruct = NestedObj->GetClass();
			continue;
		}

		OutError = FString::Printf(TEXT("Cannot navigate through '%s' (type: %s) — not a struct or object property"), *PathParts[i], *Prop->GetCPPType());
		return false;
	}

	// Find the leaf property
	const FString& LeafName = PathParts.Last();
	FProperty* LeafProp = CurrentStruct->FindPropertyByName(FName(*LeafName));
	if (!LeafProp)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *LeafName, *CurrentStruct->GetName());
		return false;
	}

	void* ValuePtr = LeafProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	// Type dispatch
	if (FNumericProperty* NumProp = CastField<FNumericProperty>(LeafProp))
	{
		if (SetNumericValue(NumProp, ValuePtr, Value))
		{
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set numeric property '%s'"), *PropertyPath);
		return false;
	}

	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(LeafProp))
	{
		bool BoolVal = false;
		if (Value->TryGetBool(BoolVal))
		{
			BoolProp->SetPropertyValue(ValuePtr, BoolVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set bool property '%s' — value must be true/false"), *PropertyPath);
		return false;
	}

	if (FStrProperty* StrProp = CastField<FStrProperty>(LeafProp))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			StrProp->SetPropertyValue(ValuePtr, StrVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set string property '%s'"), *PropertyPath);
		return false;
	}

	if (FNameProperty* NameProp = CastField<FNameProperty>(LeafProp))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*StrVal));
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set name property '%s'"), *PropertyPath);
		return false;
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(LeafProp))
	{
		if (SetStructValue(StructProp, ValuePtr, Value))
		{
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set struct property '%s' (type: F%s). Supported: JSON object, hex color, or UE text format."),
			*PropertyPath, *StructProp->Struct->GetName());
		return false;
	}

	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(LeafProp))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			// Try by name first
			int64 EnumVal = EnumProp->GetEnum()->GetValueByNameString(StrVal);
			if (EnumVal != INDEX_NONE)
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumVal);
				return true;
			}
		}
		double NumVal = 0;
		if (Value->TryGetNumber(NumVal))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, (int64)NumVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to set enum property '%s' — use enum name string or numeric index"), *PropertyPath);
		return false;
	}

	// Try map property (TMap)
	if (FMapProperty* MapProp = CastField<FMapProperty>(LeafProp))
	{
		return SetMapPropertyFromJson(MapProp, ValuePtr, Value, PropertyPath, OutError);
	}

	// FClassProperty before FObjectPropertyBase (FClassProperty IS-A FObjectPropertyBase)
	if (FClassProperty* ClassProp = CastField<FClassProperty>(LeafProp))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			if (StrVal.IsEmpty() || StrVal == TEXT("None"))
			{
				ClassProp->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			UClass* LoadedClass = LoadClass<UObject>(nullptr, *StrVal);
			if (LoadedClass)
			{
				ClassProp->SetObjectPropertyValue(ValuePtr, LoadedClass);
				return true;
			}
			OutError = FString::Printf(TEXT("Failed to load class '%s' for property '%s'"),
				*StrVal, *PropertyPath);
			return false;
		}
		OutError = FString::Printf(TEXT("Class property '%s' requires a string class path"), *PropertyPath);
		return false;
	}

	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(LeafProp))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			if (StrVal.IsEmpty() || StrVal == TEXT("None"))
			{
				ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			UObject* LoadedObj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *StrVal);
			if (LoadedObj)
			{
				ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
				return true;
			}
			OutError = FString::Printf(TEXT("Failed to load object '%s' for property '%s' (expected class: %s)"),
				*StrVal, *PropertyPath, *ObjProp->PropertyClass->GetName());
			return false;
		}
		OutError = FString::Printf(TEXT("Object property '%s' requires a string asset path"), *PropertyPath);
		return false;
	}

	// FSoftClassProperty before FSoftObjectProperty (FSoftClassProperty IS-A FSoftObjectProperty)
	if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(LeafProp))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
			*SoftPtr = FSoftObjectPath(StrVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Soft class property '%s' requires a string class path"), *PropertyPath);
		return false;
	}

	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(LeafProp))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
			*SoftPtr = FSoftObjectPath(StrVal);
			return true;
		}
		OutError = FString::Printf(TEXT("Soft object property '%s' requires a string asset path"), *PropertyPath);
		return false;
	}

	OutError = FString::Printf(TEXT("Unsupported property type '%s' for: %s"), *LeafProp->GetCPPType(), *PropertyPath);
	return false;
}

bool FMCPTool_BlueprintModify::SetNumericValue(FNumericProperty* NumProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value)
{
	if (NumProp->IsFloatingPoint())
	{
		double DoubleVal = 0.0;
		if (Value->TryGetNumber(DoubleVal))
		{
			NumProp->SetFloatingPointPropertyValue(ValuePtr, DoubleVal);
			return true;
		}
	}
	else if (NumProp->IsInteger())
	{
		int64 IntVal = 0;
		if (Value->TryGetNumber(IntVal))
		{
			NumProp->SetIntPropertyValue(ValuePtr, IntVal);
			return true;
		}
	}
	return false;
}

bool FMCPTool_BlueprintModify::SetStructValue(FStructProperty* StructProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value)
{
	const FName StructName = StructProp->Struct->GetFName();

	const bool bIsVector = (StructProp->Struct == TBaseStructure<FVector>::Get())
		|| (StructName == FName("Vector"));
	const bool bIsRotator = (StructProp->Struct == TBaseStructure<FRotator>::Get())
		|| (StructName == FName("Rotator"));
	const bool bIsFColor = (StructProp->Struct == TBaseStructure<FColor>::Get())
		|| (StructName == FName("Color"));
	const bool bIsLinearColor = (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		|| (StructName == FName("LinearColor"));

	// String format: hex colors or UE ImportText
	FString StringValue;
	if (Value->TryGetString(StringValue))
	{
		FString HexString = StringValue;
		if (HexString.StartsWith(TEXT("#")))
		{
			HexString = HexString.RightChop(1);
		}

		if ((HexString.Len() == 6 || HexString.Len() == 8))
		{
			FColor ParsedColor = FColor::FromHex(HexString);
			if (bIsFColor)
			{
				*reinterpret_cast<FColor*>(ValuePtr) = ParsedColor;
				return true;
			}
			if (bIsLinearColor)
			{
				*reinterpret_cast<FLinearColor*>(ValuePtr) = FLinearColor(ParsedColor);
				return true;
			}
		}

		// Generic ImportText fallback
		const TCHAR* ImportResult = StructProp->ImportText_Direct(*StringValue, ValuePtr, nullptr, 0);
		if (ImportResult != nullptr)
		{
			return true;
		}
	}

	// JSON object format
	const TSharedPtr<FJsonObject>* ObjVal;
	if (!Value->TryGetObject(ObjVal))
	{
		return false;
	}

	if (bIsVector)
	{
		FVector Vec;
		(*ObjVal)->TryGetNumberField(TEXT("x"), Vec.X);
		(*ObjVal)->TryGetNumberField(TEXT("y"), Vec.Y);
		(*ObjVal)->TryGetNumberField(TEXT("z"), Vec.Z);
		*reinterpret_cast<FVector*>(ValuePtr) = Vec;
		return true;
	}

	if (bIsRotator)
	{
		FRotator Rot;
		(*ObjVal)->TryGetNumberField(TEXT("pitch"), Rot.Pitch);
		(*ObjVal)->TryGetNumberField(TEXT("yaw"), Rot.Yaw);
		(*ObjVal)->TryGetNumberField(TEXT("roll"), Rot.Roll);
		*reinterpret_cast<FRotator*>(ValuePtr) = Rot;
		return true;
	}

	if (bIsFColor)
	{
		FColor Color;
		double R = 0, G = 0, B = 0, A = 255;
		(*ObjVal)->TryGetNumberField(TEXT("r"), R);
		(*ObjVal)->TryGetNumberField(TEXT("g"), G);
		(*ObjVal)->TryGetNumberField(TEXT("b"), B);
		if (!(*ObjVal)->TryGetNumberField(TEXT("a"), A)) { A = 255.0; }
		Color.R = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(R), 0, 255));
		Color.G = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(G), 0, 255));
		Color.B = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(B), 0, 255));
		Color.A = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(A), 0, 255));
		*reinterpret_cast<FColor*>(ValuePtr) = Color;
		return true;
	}

	if (bIsLinearColor)
	{
		FLinearColor Color;
		(*ObjVal)->TryGetNumberField(TEXT("r"), Color.R);
		(*ObjVal)->TryGetNumberField(TEXT("g"), Color.G);
		(*ObjVal)->TryGetNumberField(TEXT("b"), Color.B);
		if (!(*ObjVal)->TryGetNumberField(TEXT("a"), Color.A)) { Color.A = 1.0f; }
		if (Color.R > 1.5f || Color.G > 1.5f || Color.B > 1.5f)
		{
			Color.R /= 255.0f;
			Color.G /= 255.0f;
			Color.B /= 255.0f;
			if (Color.A > 1.5f) Color.A /= 255.0f;
		}
		*reinterpret_cast<FLinearColor*>(ValuePtr) = Color;
		return true;
	}

	// Generic fallback: convert JSON object to UE text format and ImportText
	{
		FString TextRepresentation = TEXT("(");
		bool bFirst = true;
		for (const auto& Pair : (*ObjVal)->Values)
		{
			if (!bFirst) TextRepresentation += TEXT(",");
			TextRepresentation += Pair.Key.ToUpper() + TEXT("=");

			double NumVal;
			FString StrVal;
			if (Pair.Value->TryGetNumber(NumVal))
			{
				TextRepresentation += FString::SanitizeFloat(NumVal);
			}
			else if (Pair.Value->TryGetString(StrVal))
			{
				TextRepresentation += StrVal;
			}
			bFirst = false;
		}
		TextRepresentation += TEXT(")");

		const TCHAR* ImportResult = StructProp->ImportText_Direct(*TextRepresentation, ValuePtr, nullptr, 0);
		if (ImportResult != nullptr)
		{
			return true;
		}
	}

	return false;
}

// ===== Level 6: Debug Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteAddDebugPrint(const TSharedRef<FJsonObject>& Params)
{
	TOptional<FMCPToolResult> Error;
	FString Label;
	if (!ExtractRequiredString(Params, TEXT("label"), Label, Error))
	{
		return Error.GetValue();
	}

	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	FDebugPrintConfig Config;
	Config.Label = Label;
	Config.EventName = ExtractOptionalString(Params, TEXT("event"), TEXT("BlueprintUpdateAnimation"));
	Config.bPrintToScreen = ExtractOptionalBool(Params, TEXT("print_to_screen"), false);
	Config.bPrintToLog = ExtractOptionalBool(Params, TEXT("print_to_log"), true);

	const TArray<TSharedPtr<FJsonValue>>* VarsArray;
	if (Params->TryGetArrayField(TEXT("variables"), VarsArray))
	{
		for (const TSharedPtr<FJsonValue>& Val : *VarsArray)
		{
			FString VarName;
			if (Val->TryGetString(VarName))
			{
				Config.Variables.Add(VarName);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* FuncsArray;
	if (Params->TryGetArrayField(TEXT("functions"), FuncsArray))
	{
		for (const TSharedPtr<FJsonValue>& Val : *FuncsArray)
		{
			const TSharedPtr<FJsonObject>* FuncObj;
			if (!Val->TryGetObject(FuncObj))
			{
				continue;
			}

			FDebugPrintFunctionItem FuncItem;
			FuncItem.FunctionName = (*FuncObj)->GetStringField(TEXT("function"));
			FuncItem.TargetClass = (*FuncObj)->GetStringField(TEXT("target_class"));
			FuncItem.Label = (*FuncObj)->GetStringField(TEXT("label"));

			const TSharedPtr<FJsonObject>* ParamsObj;
			if ((*FuncObj)->TryGetObjectField(TEXT("params"), ParamsObj))
			{
				for (const auto& Pair : (*ParamsObj)->Values)
				{
					FString ParamVal;
					if (Pair.Value->TryGetString(ParamVal))
					{
						FuncItem.Params.Add(Pair.Key, ParamVal);
					}
					else
					{
						double NumVal;
						if (Pair.Value->TryGetNumber(NumVal))
						{
							FuncItem.Params.Add(Pair.Key, FString::FromInt((int32)NumVal));
						}
					}
				}
			}

			Config.Functions.Add(FuncItem);
		}
	}

	if (Config.Variables.Num() == 0 && Config.Functions.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("At least one variable or function is required"));
	}

	FDebugPrintResult BuildResult = FDebugPrintBuilder::AddDebugPrint(Context.Blueprint, Config);
	if (!BuildResult.bSuccess)
	{
		return FMCPToolResult::Error(BuildResult.Error);
	}

	if (auto CompileError = Context.CompileAndFinalize(TEXT("Debug print added")))
	{
		return CompileError.GetValue();
	}

	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("label"), Label);
	ResultData->SetNumberField(TEXT("nodes_created"), BuildResult.CreatedNodeIds.Num());

	TArray<TSharedPtr<FJsonValue>> NodeIdsJson;
	for (const FString& NodeId : BuildResult.CreatedNodeIds)
	{
		NodeIdsJson.Add(MakeShared<FJsonValueString>(NodeId));
	}
	ResultData->SetArrayField(TEXT("node_ids"), NodeIdsJson);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added debug print '%s' with %d nodes (%d vars, %d funcs)"),
			*Label, BuildResult.CreatedNodeIds.Num(), Config.Variables.Num(), Config.Functions.Num()),
		ResultData);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteRemoveDebugPrint(const TSharedRef<FJsonObject>& Params)
{
	TOptional<FMCPToolResult> Error;
	FString Label;
	if (!ExtractRequiredString(Params, TEXT("label"), Label, Error))
	{
		return Error.GetValue();
	}

	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	FDebugPrintResult RemoveResult = FDebugPrintBuilder::RemoveDebugPrint(Context.Blueprint, Label);
	if (!RemoveResult.bSuccess)
	{
		return FMCPToolResult::Error(RemoveResult.Error);
	}

	if (RemoveResult.RemovedCount > 0)
	{
		if (auto CompileError = Context.CompileAndFinalize(TEXT("Debug print removed")))
		{
			return CompileError.GetValue();
		}
	}

	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetStringField(TEXT("label"), Label);
	ResultData->SetNumberField(TEXT("nodes_removed"), RemoveResult.RemovedCount);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed debug print '%s': %d nodes deleted"), *Label, RemoveResult.RemovedCount),
		ResultData);
}

// ===== Level 7: Layout Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteLayoutGraph(const TSharedRef<FJsonObject>& Params)
{
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	FString GraphName = ExtractOptionalString(Params, TEXT("graph_name"));
	bool bFunctionGraph = ExtractOptionalBool(Params, TEXT("is_function_graph"), false);

	FString Error;
	UEdGraph* Graph = FBlueprintGraphEditor::FindGraph(Context.Blueprint, GraphName, bFunctionGraph, Error);
	if (!Graph)
	{
		return FMCPToolResult::Error(Error);
	}

	FGraphLayoutConfig Config;
	Config.SpacingX = ExtractOptionalNumber<float>(Params, TEXT("spacing_x"), 400.0f);
	Config.SpacingY = ExtractOptionalNumber<float>(Params, TEXT("spacing_y"), 200.0f);
	Config.bPreserveExisting = ExtractOptionalBool(Params, TEXT("preserve_existing"), false);

	FGraphLayoutResult LayoutResult = FGraphLayoutHelper::LayoutGraph(
		Graph, Config,
		FGraphLayoutHelper::MakeK2ExecPolicy(),
		FGraphLayoutHelper::MakeK2EntryFinder(),
		FGraphLayoutHelper::MakeDataConsumerFinder()
	);

	if (auto CompileError = Context.CompileAndFinalize(TEXT("layout_graph")))
	{
		return CompileError.GetValue();
	}

	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();
	ResultData->SetBoolField(TEXT("success"), true);
	ResultData->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
	ResultData->SetNumberField(TEXT("total_nodes"), LayoutResult.TotalNodes);
	ResultData->SetNumberField(TEXT("layout_nodes"), LayoutResult.LayoutNodes);
	ResultData->SetNumberField(TEXT("skipped_nodes"), LayoutResult.SkippedNodes);
	ResultData->SetNumberField(TEXT("disconnected_nodes"), LayoutResult.DisconnectedNodes);
	ResultData->SetNumberField(TEXT("data_only_nodes"), LayoutResult.DataOnlyNodes);
	ResultData->SetNumberField(TEXT("entry_points"), LayoutResult.EntryPoints);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Laid out %d/%d nodes in %s (%d entry points, %d disconnected, %d data-only)"),
			LayoutResult.LayoutNodes, LayoutResult.TotalNodes,
			GraphName.IsEmpty() ? TEXT("EventGraph") : *GraphName,
			LayoutResult.EntryPoints, LayoutResult.DisconnectedNodes, LayoutResult.DataOnlyNodes),
		ResultData);
}

// ===== Validation Operations =====

FMCPToolResult FMCPTool_BlueprintModify::ExecuteCompile(const TSharedRef<FJsonObject>& Params)
{
	// Load and validate Blueprint (using LoadAndValidate — compile is a modification-adjacent op)
	FMCPBlueprintLoadContext Context;
	if (auto LoadError = Context.LoadAndValidate(Params))
	{
		return LoadError.GetValue();
	}

	// Compile with full diagnostics — NOT CompileAndFinalize which returns error on failure
	Context.CompileResult = FBlueprintLoader::CompileBlueprintWithResult(Context.Blueprint);

	// Build result using existing infrastructure
	TSharedPtr<FJsonObject> ResultData = Context.BuildResultJson();

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Compile %s: %s (%d errors, %d warnings)"),
			*Context.BlueprintPath,
			*Context.CompileResult.StatusString,
			Context.CompileResult.ErrorCount,
			Context.CompileResult.WarningCount),
		ResultData
	);
}

FMCPToolResult FMCPTool_BlueprintModify::ExecuteCompileAll(const TSharedRef<FJsonObject>& Params)
{
	FString ScanPath = ExtractOptionalString(Params, TEXT("path"), TEXT("/Game"));

	// Validate path
	FString ValidationError;
	if (!FMCPParamValidator::ValidateBlueprintPath(ScanPath, ValidationError))
	{
		return FMCPToolResult::Error(ValidationError);
	}

	// Query asset registry for all Blueprints under the path
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*ScanPath));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	// Compile each Blueprint and collect results
	int32 Total = 0;
	int32 Succeeded = 0;
	int32 Failed = 0;
	int32 WithWarnings = 0;

	TArray<TSharedPtr<FJsonValue>> FailuresArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;

	for (const FAssetData& AssetData : AssetDataList)
	{
		UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
		if (!BP)
		{
			continue;
		}

		Total++;

		FBlueprintCompileResult Result = FBlueprintLoader::CompileBlueprintWithResult(BP);

		if (!Result.bSuccess)
		{
			Failed++;

			TSharedPtr<FJsonObject> FailEntry = MakeShared<FJsonObject>();
			FailEntry->SetStringField(TEXT("path"), BP->GetPathName());
			FailEntry->SetStringField(TEXT("status"), Result.StatusString);

			TArray<TSharedPtr<FJsonValue>> MsgArray;
			for (const FBlueprintCompileMessage& Msg : Result.Messages)
			{
				if (Msg.Severity == TEXT("Error"))
				{
					MsgArray.Add(MakeShared<FJsonValueString>(Msg.Message));
				}
			}
			FailEntry->SetArrayField(TEXT("messages"), MsgArray);
			FailuresArray.Add(MakeShared<FJsonValueObject>(FailEntry));
		}
		else if (Result.WarningCount > 0)
		{
			WithWarnings++;
			Succeeded++;

			TSharedPtr<FJsonObject> WarnEntry = MakeShared<FJsonObject>();
			WarnEntry->SetStringField(TEXT("path"), BP->GetPathName());

			TArray<TSharedPtr<FJsonValue>> MsgArray;
			for (const FBlueprintCompileMessage& Msg : Result.Messages)
			{
				if (Msg.Severity == TEXT("Warning"))
				{
					MsgArray.Add(MakeShared<FJsonValueString>(Msg.Message));
				}
			}
			WarnEntry->SetArrayField(TEXT("messages"), MsgArray);
			WarningsArray.Add(MakeShared<FJsonValueObject>(WarnEntry));
		}
		else
		{
			Succeeded++;
		}
	}

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("scan_path"), ScanPath);
	ResultData->SetNumberField(TEXT("total"), Total);
	ResultData->SetNumberField(TEXT("succeeded"), Succeeded);
	ResultData->SetNumberField(TEXT("failed"), Failed);
	ResultData->SetNumberField(TEXT("with_warnings"), WithWarnings);

	if (FailuresArray.Num() > 0)
	{
		ResultData->SetArrayField(TEXT("failures"), FailuresArray);
	}
	if (WarningsArray.Num() > 0)
	{
		ResultData->SetArrayField(TEXT("warnings"), WarningsArray);
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Compiled %d Blueprints under '%s': %d succeeded, %d failed, %d with warnings"),
			Total, *ScanPath, Succeeded, Failed, WithWarnings),
		ResultData
	);
}
