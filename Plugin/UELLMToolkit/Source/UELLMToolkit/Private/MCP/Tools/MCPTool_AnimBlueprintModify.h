// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Animation Blueprint Modification
 *
 * Provides comprehensive control over Animation Blueprints including:
 * - State machine management (create, query)
 * - State operations (add, remove, list)
 * - Transition operations (add, remove, configure duration/priority)
 * - Transition condition graphs (add condition nodes, connect, delete nodes)
 * - Animation assignment (AnimSequence, BlendSpace, BlendSpace1D, Montage)
 *
 * Operations:
 * - get_info: Get AnimBlueprint structure overview
 * - get_state_machine: Get detailed state machine info
 * - create_state_machine: Create new state machine
 * - add_state: Add state to state machine
 * - remove_state: Remove state from state machine
 * - set_entry_state: Set entry state for state machine
 * - add_transition: Create transition between states
 * - remove_transition: Remove transition
 * - set_transition_duration: Set blend duration
 * - set_transition_priority: Set transition priority
 * - add_condition_node: Add node to transition graph (fully supported)
 * - delete_condition_node: Remove node from transition graph
 * - connect_condition_nodes: Connect nodes in transition graph
 * - connect_to_result: Connect condition to transition result (for transitions)
 * - connect_state_machine_to_output: Connect State Machine to AnimGraph Output Pose
 * - set_state_animation: Assign animation to state
 * - add_anim_node: Create animation player node in any graph (AnimGraph, layer, state)
 * - delete_anim_node: Delete animation node from any graph by node ID
 * - find_animations: Search for compatible animation assets
 * - batch: Execute multiple operations atomically
 *
 * NEW Operations (Enhanced Pin/Node Introspection):
 * - get_transition_nodes: List all nodes in transition graph(s) with their pins
 * - inspect_node_pins: Get detailed pin info for a specific node (types, connections)
 * - set_pin_default_value: Set pin default value with type validation
 * - add_comparison_chain: Add GetVariable → Comparison → Result chain (auto-ANDs with existing)
 * - validate_blueprint: Return compile errors with full diagnostics
 *
 * Animation Layer Interface Operations:
 * - list_layer_interfaces: List ALIs implemented by the AnimBP
 * - list_layers: List available layer functions (from compiled class)
 * - list_linked_layer_nodes: List LinkedAnimLayer nodes in AnimGraph
 * - add_layer_interface: Add an ALI to the AnimBP
 * - add_linked_layer_node: Create a LinkedAnimLayer node in AnimGraph
 * - set_layer_instance: Set instance class on a LinkedAnimLayer node
 * - connect_anim_nodes: Connect two anim nodes via pose pins (works in AnimGraph or layer graphs)
 * - inspect_layer_graph: List all nodes in a layer function graph with pins
 *
 * Supported Condition Node Types (add_condition_node):
 * - TimeRemaining: Gets time remaining in current animation (output: float)
 * - Greater: Float comparison A > B (inputs: A, B; output: bool)
 * - Less: Float comparison A < B
 * - GreaterEqual: Float comparison A >= B
 * - LessEqual: Float comparison A <= B
 * - Equal: Float comparison A == B
 * - NotEqual: Float comparison A != B
 * - And: Boolean AND (inputs: A, B; output: bool)
 * - Or: Boolean OR (inputs: A, B; output: bool)
 * - Not: Boolean NOT (input: A; output: bool)
 * - GetVariable: Get blueprint variable (node_params: {variable_name})
 */
class FMCPTool_AnimBlueprintModify : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("anim_blueprint_modify");
		Info.Description = TEXT(
			"Comprehensive Animation Blueprint modification tool.\n\n"
			"State Machine Operations:\n"
			"- 'get_info': Overview of AnimBlueprint structure\n"
			"- 'get_state_machine': Detailed state machine info\n"
			"- 'create_state_machine': Create new state machine\n"
			"- 'add_state', 'remove_state': Manage states\n"
			"- 'set_entry_state': Set entry state for state machine\n"
			"- 'add_transition', 'remove_transition': Manage transitions\n\n"
			"Transition Configuration:\n"
			"- 'set_transition_duration': Set blend duration\n"
			"- 'set_transition_priority': Set evaluation priority\n\n"
			"Condition Graph (transition logic):\n"
			"- 'add_condition_node': Add logic node (TimeRemaining, Greater, Less, And, Or, Not, GetVariable)\n"
			"- 'delete_condition_node', 'connect_condition_nodes', 'connect_to_result'\n\n"
			"Node/Pin Introspection (NEW):\n"
			"- 'get_transition_nodes': List all nodes in transition graph(s) with pins\n"
			"- 'inspect_node_pins': Get detailed pin info for a node (types, values, connections)\n"
			"- 'set_pin_default_value': Set pin value with type validation\n"
			"- 'add_comparison_chain': Add GetVariable->Comparison->Result (auto-ANDs with existing)\n"
			"- 'validate_blueprint': Return compile errors with full diagnostics\n\n"
			"Visualization (NEW):\n"
			"- 'get_state_machine_diagram': Generate ASCII diagram and enhanced JSON for state machine\n\n"
			"Bulk Operations:\n"
			"- 'setup_transition_conditions': Setup conditions for multiple transitions using pattern matching\n\n"
			"AnimGraph Connection:\n"
			"- 'connect_state_machine_to_output': Connect State Machine to AnimGraph Output Pose\n\n"
			"Animation Assignment:\n"
			"- 'set_state_animation': Assign AnimSequence, BlendSpace, BlendSpace1D, or Montage to state\n"
			"- 'get_anim_node_property': Get internal FAnimNode struct property (single or all). Omit property_name to dump all properties (discovery mode)\n"
			"- 'set_anim_node_property': Set internal FAnimNode struct property on an anim graph node (bLoopAnimation, PlayRate, StartPosition, etc.)\n"
			"- 'add_anim_node': Create animation/slot/blending node in any graph (AnimGraph, layer, state bound). Types: sequence, blendspace, blendspace1d, slot, inertialization, dead_blending, copy_pose_from_mesh, modify_bone, control_rig, layered_blend_per_bone, aim_offset\n"
			"- 'delete_anim_node': Delete animation node from any graph by node ID\n"
			"- 'find_animations': Search compatible animation assets\n\n"
			"Animation Layer Interfaces:\n"
			"- 'list_layer_interfaces': List ALIs implemented by the AnimBP\n"
			"- 'list_layers': List available layer functions\n"
			"- 'list_linked_layer_nodes': List LinkedAnimLayer nodes in AnimGraph\n"
			"- 'add_layer_interface': Add ALI to AnimBP (interface_path required)\n"
			"- 'add_linked_layer_node': Create LinkedAnimLayer node (layer_name required)\n"
			"- 'set_layer_instance': Set instance class on LinkedAnimLayer (node_id, instance_class required)\n"
			"- 'connect_anim_nodes': Connect pins (pose by default, or explicit source_pin/target_pin). Supports state_machine+state_name for state-bound graphs\n"
			"- 'bind_variable': Bind a BP variable to a node pin (variable_name, target_node_id, target_pin). Supports state_machine+state_name for state-bound graphs\n"
			"- 'inspect_layer_graph': List nodes in layer function graph (target_graph required)\n\n"
			"Layout:\n"
			"- 'layout_graph': Auto-arrange nodes into readable BFS grid layout (AnimGraph or state-bound graph)\n\n"
			"- 'batch': Execute multiple operations atomically"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("blueprint_path"), TEXT("string"), TEXT("Path to the Animation Blueprint (e.g., '/Game/Characters/ABP_Character')"), true),
			FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation: get_info, get_state_machine, get_state_machine_diagram, create_state_machine, add_state, remove_state, set_entry_state, add_transition, remove_transition, set_transition_duration, set_transition_priority, add_condition_node, delete_condition_node, connect_condition_nodes, connect_to_result, connect_state_machine_to_output, set_state_animation, get_anim_node_property, set_anim_node_property, add_anim_node, delete_anim_node, find_animations, batch, get_transition_nodes, inspect_node_pins, set_pin_default_value, add_comparison_chain, validate_blueprint, list_layer_interfaces, list_layers, list_linked_layer_nodes, add_layer_interface, add_linked_layer_node, set_layer_instance, connect_anim_nodes, bind_variable, inspect_layer_graph, layout_graph"), true),
			FMCPToolParameter(TEXT("state_machine"), TEXT("string"), TEXT("State machine name (for state/transition operations)"), false),
			FMCPToolParameter(TEXT("state_name"), TEXT("string"), TEXT("State name (for state operations)"), false),
			FMCPToolParameter(TEXT("from_state"), TEXT("string"), TEXT("Source state name (for transitions)"), false),
			FMCPToolParameter(TEXT("to_state"), TEXT("string"), TEXT("Target state name (for transitions)"), false),
			FMCPToolParameter(TEXT("position"), TEXT("object"), TEXT("Node position {x, y}"), false, TEXT("{\"x\":0,\"y\":0}")),
			FMCPToolParameter(TEXT("is_entry_state"), TEXT("boolean"), TEXT("Whether this state is the entry state"), false, TEXT("false")),
			FMCPToolParameter(TEXT("duration"), TEXT("number"), TEXT("Transition blend duration in seconds"), false),
			FMCPToolParameter(TEXT("priority"), TEXT("number"), TEXT("Transition priority (higher = checked first)"), false),
			FMCPToolParameter(TEXT("node_type"), TEXT("string"), TEXT("Condition node type: TimeRemaining, Greater, Less, GreaterEqual, LessEqual, Equal, NotEqual, And, Or, Not, GetVariable"), false),
			FMCPToolParameter(TEXT("node_params"), TEXT("object"), TEXT("Condition node parameters (e.g., {variable_name} for GetVariable)"), false),
			FMCPToolParameter(TEXT("node_id"), TEXT("string"), TEXT("Node ID for delete_condition_node operation"), false),
			FMCPToolParameter(TEXT("source_node_id"), TEXT("string"), TEXT("Source node ID for connection"), false),
			FMCPToolParameter(TEXT("source_pin"), TEXT("string"), TEXT("Source pin name"), false),
			FMCPToolParameter(TEXT("target_node_id"), TEXT("string"), TEXT("Target node ID for connection"), false),
			FMCPToolParameter(TEXT("target_pin"), TEXT("string"), TEXT("Target pin name"), false),
			FMCPToolParameter(TEXT("animation_type"), TEXT("string"), TEXT("Animation type: sequence, blendspace, blendspace1d, slot, inertialization, dead_blending, copy_pose_from_mesh, modify_bone, two_bone_ik, control_rig, layered_blend_per_bone, aim_offset, local_to_component, component_to_local"), false),
			FMCPToolParameter(TEXT("bone_name"), TEXT("string"), TEXT("Bone name for modify_bone, two_bone_ik, or layered_blend_per_bone (e.g., 'spine_01')"), false),
			FMCPToolParameter(TEXT("effector_bone"), TEXT("string"), TEXT("Effector target bone for two_bone_ik (e.g., 'Bow_String')"), false),
			FMCPToolParameter(TEXT("effector_space"), TEXT("string"), TEXT("Effector coordinate space for two_bone_ik: BoneSpace (default), ComponentSpace, WorldSpace, ParentBoneSpace"), false, TEXT("BoneSpace")),
			FMCPToolParameter(TEXT("joint_target_bone"), TEXT("string"), TEXT("Joint (pole) target bone for two_bone_ik (e.g., 'lowerarm_r')"), false),
			FMCPToolParameter(TEXT("allow_stretching"), TEXT("boolean"), TEXT("Allow bone stretching for two_bone_ik (default: false)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("blend_depth"), TEXT("number"), TEXT("Blend depth for layered_blend_per_bone (0 = all descendants, default: 0)"), false, TEXT("0")),
			FMCPToolParameter(TEXT("mesh_space_rotation_blend"), TEXT("boolean"), TEXT("Use mesh-space rotation blending for layered_blend_per_bone (default: false)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("rotation"), TEXT("object"), TEXT("Rotation for modify_bone: {pitch, yaw, roll}"), false),
			FMCPToolParameter(TEXT("translation"), TEXT("object"), TEXT("Translation for modify_bone: {x, y, z}"), false),
			FMCPToolParameter(TEXT("rotation_mode"), TEXT("string"), TEXT("Rotation mode for modify_bone: additive (default), replace, ignore"), false, TEXT("additive")),
			FMCPToolParameter(TEXT("translation_mode"), TEXT("string"), TEXT("Translation mode for modify_bone: additive (default), replace, ignore"), false, TEXT("additive")),
			FMCPToolParameter(TEXT("rotation_space"), TEXT("string"), TEXT("Rotation space for modify_bone: bone (default), component, parent, world"), false, TEXT("bone")),
			FMCPToolParameter(TEXT("translation_space"), TEXT("string"), TEXT("Translation space for modify_bone: bone (default), component, parent, world"), false, TEXT("bone")),
			FMCPToolParameter(TEXT("animation_path"), TEXT("string"), TEXT("Path to animation asset"), false),
			FMCPToolParameter(TEXT("control_rig_class"), TEXT("string"), TEXT("Control Rig Blueprint class path for control_rig type (e.g., '/Game/CR_MyRig.CR_MyRig_C')"), false),
			FMCPToolParameter(TEXT("parameter_bindings"), TEXT("object"), TEXT("BlendSpace parameter bindings {\"X\": \"Speed\", \"Y\": \"Direction\"}"), false),
			FMCPToolParameter(TEXT("search_pattern"), TEXT("string"), TEXT("Animation search pattern (for find_animations)"), false),
			FMCPToolParameter(TEXT("asset_type"), TEXT("string"), TEXT("Asset type filter: AnimSequence, BlendSpace, BlendSpace1D, Montage, All"), false, TEXT("All")),
			FMCPToolParameter(TEXT("operations"), TEXT("array"), TEXT("Array of operations for batch mode"), false),
			// New parameters for enhanced operations
			FMCPToolParameter(TEXT("variable_name"), TEXT("string"), TEXT("Blueprint variable name (for add_comparison_chain)"), false),
			FMCPToolParameter(TEXT("comparison_type"), TEXT("string"), TEXT("Comparison type: Greater, Less, GreaterEqual, LessEqual, Equal, NotEqual (for add_comparison_chain)"), false),
			FMCPToolParameter(TEXT("compare_value"), TEXT("string"), TEXT("Value to compare against (for add_comparison_chain)"), false),
			FMCPToolParameter(TEXT("pin_value"), TEXT("string"), TEXT("Default value for the pin (for set_pin_default_value)"), false),
			FMCPToolParameter(TEXT("pin_name"), TEXT("string"), TEXT("Pin name to set value (for set_pin_default_value)"), false),
			// Bulk operation parameters
			FMCPToolParameter(TEXT("rules"), TEXT("array"), TEXT("Array of condition rules for setup_transition_conditions. Each rule: {match: {from, to}, conditions: [...], logic: 'AND'|'OR'}"), false),
			// Animation Layer Interface parameters
			FMCPToolParameter(TEXT("interface_path"), TEXT("string"), TEXT("Path to Animation Layer Interface asset (for add_layer_interface)"), false),
			FMCPToolParameter(TEXT("layer_name"), TEXT("string"), TEXT("Name of the layer function (for add_linked_layer_node)"), false),
			FMCPToolParameter(TEXT("instance_class"), TEXT("string"), TEXT("Path to AnimBP to use as linked instance (for add_linked_layer_node, set_layer_instance)"), false),
			FMCPToolParameter(TEXT("target_graph"), TEXT("string"), TEXT("Layer function graph name (for create_state_machine, connect_anim_nodes, add_anim_node in layer graphs; omit for AnimGraph)"), false),
			FMCPToolParameter(TEXT("property_name"), TEXT("string"), TEXT("FAnimNode property name (e.g., bLoopAnimation, PlayRate, StartPosition)"), false),
			FMCPToolParameter(TEXT("property_value"), TEXT("string"), TEXT("Value to set (e.g., 'true', '1.5', 'None')"), false),
			FMCPToolParameter(TEXT("auto_connect"), TEXT("boolean"), TEXT("If true, connect created node's pose output to graph result/root (for add_anim_node)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("slot_name"), TEXT("string"), TEXT("Slot name for slot-type nodes (default: DefaultSlot)"), false, TEXT("DefaultSlot")),
			// For layout_graph operation
			FMCPToolParameter(TEXT("spacing_x"), TEXT("number"), TEXT("Horizontal spacing between depth levels (default: 400)"), false, TEXT("400")),
			FMCPToolParameter(TEXT("spacing_y"), TEXT("number"), TEXT("Vertical spacing between nodes at same depth (default: 200)"), false, TEXT("200")),
			FMCPToolParameter(TEXT("preserve_existing"), TEXT("boolean"), TEXT("Skip nodes with non-zero positions (default: false)"), false, TEXT("false"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// Operation handlers
	FMCPToolResult HandleGetInfo(const FString& BlueprintPath);
	FMCPToolResult HandleGetStateMachine(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleCreateStateMachine(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddState(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveState(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetEntryState(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddTransition(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveTransition(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetTransitionDuration(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetTransitionPriority(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddConditionNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleDeleteConditionNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleConnectConditionNodes(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleConnectToResult(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleConnectStateMachineToOutput(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetStateAnimation(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleFindAnimations(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleBatch(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);

	// NEW handlers for enhanced operations
	FMCPToolResult HandleGetTransitionNodes(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleInspectNodePins(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetPinDefaultValue(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddComparisonChain(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleValidateBlueprint(const FString& BlueprintPath);
	FMCPToolResult HandleGetStateMachineDiagram(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);

	// Anim node property getter/setter
	FMCPToolResult HandleGetAnimNodeProperty(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetAnimNodeProperty(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);

	// Bulk operation handler
	FMCPToolResult HandleSetupTransitionConditions(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);

	// Animation Layer Interface handlers
	FMCPToolResult HandleListLayerInterfaces(const FString& BlueprintPath);
	FMCPToolResult HandleListLayers(const FString& BlueprintPath);
	FMCPToolResult HandleListLinkedLayerNodes(const FString& BlueprintPath);
	FMCPToolResult HandleAddLayerInterface(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddLinkedLayerNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetLayerInstance(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleConnectAnimNodes(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleBindVariable(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleInspectLayerGraph(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddAnimNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleDeleteAnimNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleLayoutGraph(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params);

	// Helper to extract position
	FVector2D ExtractPosition(const TSharedRef<FJsonObject>& Params);

	/**
	 * Load animation blueprint or return error result.
	 * Reduces code duplication across 23+ handler methods.
	 *
	 * @param Path Blueprint path to load
	 * @param OutBP Output parameter for loaded blueprint
	 * @return Empty optional on success, error result on failure
	 */
	TOptional<FMCPToolResult> LoadAnimBlueprintOrError(
		const FString& Path,
		UAnimBlueprint*& OutBP);
};
