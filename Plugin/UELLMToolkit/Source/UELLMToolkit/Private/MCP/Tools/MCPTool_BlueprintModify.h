// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Modify Blueprints (write operations)
 *
 * Level 2 Operations (Variables/Functions):
 *   - create: Create a new Blueprint
 *   - reparent: Change a Blueprint's parent class
 *   - add_variable: Add a variable to a Blueprint
 *   - remove_variable: Remove a variable from a Blueprint
 *   - add_function: Add an empty function to a Blueprint
 *   - remove_function: Remove a function from a Blueprint
 *
 * Level 3 Operations (Nodes):
 *   - add_node: Add a single node to a graph
 *   - add_nodes: Batch add multiple nodes with connections
 *   - delete_node: Remove a node from a graph
 *
 * Level 4 Operations (Connections):
 *   - connect_pins: Connect two pins
 *   - disconnect_pins: Disconnect two pins
 *   - set_pin_value: Set default value for an input pin
 *
 * Level 5 Operations (Defaults):
 *   - set_component_default: Set a default property on a Blueprint component template
 *   - set_cdo_default: Set a top-level CDO property on a Blueprint (e.g., bUseControllerRotationYaw)
 *
 * Level 6 Operations (Debug):
 *   - add_debug_print: Add labeled debug print subgraph to event graph (idempotent)
 *   - remove_debug_print: Remove debug print subgraph by label
 *
 * Level 7 Operations (Layout):
 *   - layout_graph: Auto-arrange graph nodes into a readable BFS-ordered grid layout
 *
 * Validation Operations:
 *   - compile: Compile a single Blueprint and return full diagnostics
 *   - compile_all: Compile all Blueprints under a path and return per-blueprint diagnostics
 *
 * All modification operations auto-compile the Blueprint after changes.
 */
class FMCPTool_BlueprintModify : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("blueprint_modify");
		Info.Description = TEXT(
			"Create and modify Blueprints programmatically. Auto-compiles after changes.\n\n"
			"Complexity Levels:\n"
			"Level 2 (Structure): 'create', 'reparent', 'add_variable', 'remove_variable', 'add_function', 'remove_function'\n"
			"Level 3 (Nodes): 'add_node', 'add_nodes' (batch), 'delete_node'\n"
			"Level 4 (Wiring): 'connect_pins', 'disconnect_pins', 'set_pin_value'\n"
			"Level 5 (Defaults): 'set_component_default' - set default property on BP component, 'set_cdo_default' - set top-level CDO property\n"
			"Level 6 (Debug): 'add_debug_print' - add labeled debug print subgraph, 'remove_debug_print' - remove by label\n"
			"Level 7 (Layout): 'layout_graph' - auto-arrange nodes into readable BFS grid layout\n"
			"Validation: 'compile' - compile single BP with diagnostics, 'compile_all' - batch compile under path\n\n"
			"Workflow: Use blueprint_query first to understand existing structure, then modify.\n\n"
			"Node types: CallFunction, Branch, Event, VariableGet, VariableSet, Sequence, "
			"PrintString, Add, Subtract, Multiply, Divide\n\n"
			"Variable types: bool, int32, float, FString, FVector, FRotator, AActor*, UObject*, etc.\n\n"
			"Returns: Operation result with created node IDs (for subsequent connections)."
		);
		Info.Parameters = {
			// Operation selector
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation to perform (see description for full list)"), true),

			// Common parameters
			FMCPToolParameter(TEXT("blueprint_path"), TEXT("string"),
				TEXT("Blueprint to modify"), false),

			// For 'create' operation
			FMCPToolParameter(TEXT("package_path"), TEXT("string"),
				TEXT("Package path for new Blueprint (e.g., '/Game/Blueprints')"), false),
			FMCPToolParameter(TEXT("blueprint_name"), TEXT("string"),
				TEXT("Name for new Blueprint"), false),
			FMCPToolParameter(TEXT("parent_class"), TEXT("string"),
				TEXT("Parent class (e.g., 'Actor', 'Pawn')"), false),
			FMCPToolParameter(TEXT("blueprint_type"), TEXT("string"),
				TEXT("Type: 'Normal', 'FunctionLibrary', 'Interface', 'MacroLibrary'"), false, TEXT("Normal")),

			// For variable operations
			FMCPToolParameter(TEXT("variable_name"), TEXT("string"),
				TEXT("Variable name"), false),
			FMCPToolParameter(TEXT("variable_type"), TEXT("string"),
				TEXT("Variable type: 'bool', 'int32', 'float', 'FString', 'FVector', 'AActor*', etc."), false),

			// For function operations
			FMCPToolParameter(TEXT("function_name"), TEXT("string"),
				TEXT("Function name"), false),

			// For node operations (Level 3)
			FMCPToolParameter(TEXT("graph_name"), TEXT("string"),
				TEXT("Graph name (empty for default EventGraph)"), false),
			FMCPToolParameter(TEXT("is_function_graph"), TEXT("boolean"),
				TEXT("True to target function graphs, false for event graphs"), false, TEXT("false")),
			FMCPToolParameter(TEXT("node_type"), TEXT("string"),
				TEXT("Node type: 'CallFunction', 'Branch', 'Event', 'VariableGet', 'VariableSet', 'Sequence', 'PrintString', 'Add', 'Subtract', 'Multiply', 'Divide'"), false),
			FMCPToolParameter(TEXT("node_params"), TEXT("object"),
				TEXT("Node parameters: {function, target_class, event, variable, num_outputs}"), false),
			FMCPToolParameter(TEXT("pos_x"), TEXT("number"),
				TEXT("Node X position"), false, TEXT("0")),
			FMCPToolParameter(TEXT("pos_y"), TEXT("number"),
				TEXT("Node Y position"), false, TEXT("0")),
			FMCPToolParameter(TEXT("node_id"), TEXT("string"),
				TEXT("Node ID (for delete/connect operations)"), false),

			// For batch add_nodes operation
			FMCPToolParameter(TEXT("nodes"), TEXT("array"),
				TEXT("Array of node specs: [{type, params, pos_x, pos_y, pin_values}]"), false),
			FMCPToolParameter(TEXT("connections"), TEXT("array"),
				TEXT("Array of connections: [{from_node, from_pin, to_node, to_pin}] (use indices or node IDs)"), false),

			// For connection operations (Level 4)
			FMCPToolParameter(TEXT("source_node_id"), TEXT("string"),
				TEXT("Source node ID"), false),
			FMCPToolParameter(TEXT("source_pin"), TEXT("string"),
				TEXT("Source pin name (empty for auto exec)"), false),
			FMCPToolParameter(TEXT("target_node_id"), TEXT("string"),
				TEXT("Target node ID"), false),
			FMCPToolParameter(TEXT("target_pin"), TEXT("string"),
				TEXT("Target pin name (empty for auto exec)"), false),

			// For set_pin_value operation
			FMCPToolParameter(TEXT("pin_name"), TEXT("string"),
				TEXT("Pin name to set value"), false),
			FMCPToolParameter(TEXT("pin_value"), TEXT("string"),
				TEXT("Default value to set"), false),

			// For set_component_default operation
			FMCPToolParameter(TEXT("component_name"), TEXT("string"),
				TEXT("Component variable name (e.g., 'CameraBoom', 'CharacterMovement0', 'Mesh')"), false),
			FMCPToolParameter(TEXT("property"), TEXT("string"),
				TEXT("Property path with dot notation (e.g., 'RelativeRotation', 'RelativeRotation.Yaw', 'MaxWalkSpeed')"), false),
			FMCPToolParameter(TEXT("value"), TEXT("any"),
				TEXT("Value to set: number, bool, string, or object (e.g., {\"pitch\":0,\"yaw\":-90,\"roll\":0})"), false),

			// For compile_all operation
			FMCPToolParameter(TEXT("path"), TEXT("string"),
				TEXT("Content path to scan for compile_all (default: '/Game')"), false, TEXT("/Game")),

			// For layout_graph operation
			FMCPToolParameter(TEXT("spacing_x"), TEXT("number"),
				TEXT("Horizontal spacing between depth levels (default: 400)"), false, TEXT("400")),
			FMCPToolParameter(TEXT("spacing_y"), TEXT("number"),
				TEXT("Vertical spacing between nodes at same depth (default: 200)"), false, TEXT("200")),
			FMCPToolParameter(TEXT("preserve_existing"), TEXT("boolean"),
				TEXT("Skip nodes with non-zero positions (default: false)"), false, TEXT("false")),

			// For add_debug_print / remove_debug_print
			FMCPToolParameter(TEXT("label"), TEXT("string"),
				TEXT("Debug print group label (used for identification and removal)"), false),
			FMCPToolParameter(TEXT("variables"), TEXT("array"),
				TEXT("Array of variable names to print (e.g., ['Speed', 'Direction'])"), false),
			FMCPToolParameter(TEXT("functions"), TEXT("array"),
				TEXT("Array of function calls: [{function, target_class, params, label}]"), false),
			FMCPToolParameter(TEXT("print_to_screen"), TEXT("boolean"),
				TEXT("Print to screen (default: false)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("print_to_log"), TEXT("boolean"),
				TEXT("Print to log (default: true)"), false, TEXT("true")),
			FMCPToolParameter(TEXT("event"), TEXT("string"),
				TEXT("Event to hook into (default: 'BlueprintUpdateAnimation')"), false, TEXT("BlueprintUpdateAnimation"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// Level 2 Operations
	FMCPToolResult ExecuteCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteReparent(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteAddVariable(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteRemoveVariable(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteAddFunction(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteRemoveFunction(const TSharedRef<FJsonObject>& Params);

	// Level 3 Operations (Nodes)
	FMCPToolResult ExecuteAddNode(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteAddNodes(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteDeleteNode(const TSharedRef<FJsonObject>& Params);

	// Level 4 Operations (Connections)
	FMCPToolResult ExecuteConnectPins(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteDisconnectPins(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteSetPinValue(const TSharedRef<FJsonObject>& Params);

	// Level 5 Operations (Defaults)
	FMCPToolResult ExecuteSetComponentDefault(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteSetCDODefault(const TSharedRef<FJsonObject>& Params);

	// Level 6 Operations (Debug)
	FMCPToolResult ExecuteAddDebugPrint(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteRemoveDebugPrint(const TSharedRef<FJsonObject>& Params);

	// Level 7 Operations (Layout)
	FMCPToolResult ExecuteLayoutGraph(const TSharedRef<FJsonObject>& Params);

	// Validation Operations
	FMCPToolResult ExecuteCompile(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteCompileAll(const TSharedRef<FJsonObject>& Params);

	// Property reflection helpers for set_component_default
	bool SetComponentPropertyFromJson(UObject* Template, const FString& PropertyPath, const TSharedPtr<FJsonValue>& Value, FString& OutError);
	bool SetNumericValue(FNumericProperty* NumProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value);
	bool SetStructValue(FStructProperty* StructProp, void* ValuePtr, const TSharedPtr<FJsonValue>& Value);

	// Helpers
	EBlueprintType ParseBlueprintType(const FString& TypeString);

	// ExecuteAddNodes helper functions (reduces function complexity)
	bool CreateNodesFromSpec(
		UEdGraph* Graph,
		const TArray<TSharedPtr<FJsonValue>>& NodesArray,
		TArray<FString>& OutCreatedNodeIds,
		TArray<TSharedPtr<FJsonValue>>& OutCreatedNodes,
		FString& OutError
	);

	void ProcessNodeConnections(
		UEdGraph* Graph,
		const TArray<TSharedPtr<FJsonValue>>& ConnectionsArray,
		const TArray<FString>& CreatedNodeIds
	);
};
