// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimBlueprint.h"
#include "Dom/JsonObject.h"
#include "AnimNodePinUtils.h"  // For FPinSearchConfig

// Forward declarations
class UAnimGraphNode_StateMachine;
class UAnimGraphNode_Root;
class UAnimStateNode;
class UAnimStateTransitionNode;
class UEdGraph;
class UEdGraphNode;
class UAnimSequence;
class UBlendSpace;
class UBlendSpace1D;
class UAnimMontage;

/**
 * Animation Graph Editor for Animation Blueprints
 *
 * Responsibilities:
 * - Finding animation graphs (AnimGraph, state bound graphs, transition graphs)
 * - Creating animation-specific nodes
 * - Managing node connections in animation graphs
 * - Handling transition condition nodes
 *
 * Supported Condition Node Types:
 * - TimeRemaining: Check time remaining in current state
 * - CompareFloat: Float comparison (requires "comparison" param: Greater, Less, etc.)
 * - CompareBool: Boolean equality comparison
 * - Greater, Less, GreaterEqual, LessEqual, Equal, NotEqual: Direct comparison operators
 * - And, Or, Not: Logical operators
 */
class FAnimGraphEditor
{
public:
	// ===== Graph Finding =====

	/**
	 * Find AnimGraph (main animation graph)
	 */
	static UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBP, FString& OutError);

	/**
	 * Find state bound graph by state name
	 */
	static UEdGraph* FindStateBoundGraph(
		UAnimBlueprint* AnimBP,
		const FString& StateMachineName,
		const FString& StateName,
		FString& OutError
	);

	/**
	 * Find transition graph by states
	 */
	static UEdGraph* FindTransitionGraph(
		UAnimBlueprint* AnimBP,
		const FString& StateMachineName,
		const FString& FromState,
		const FString& ToState,
		FString& OutError
	);

	// ===== Transition Condition Nodes (Level 4) =====

	/**
	 * Add condition node to transition graph
	 *
	 * Supported NodeTypes:
	 * - "TimeRemaining" - params: { threshold }
	 * - "CompareFloat" - params: { variable_name, comparison: "Greater"|"Less"|"Equal", value }
	 * - "CompareBool" - params: { variable_name, expected_value }
	 * - "And" / "Or" / "Not" - logical operators
	 *
	 * @param TransitionGraph - Transition rule graph
	 * @param NodeType - Type of condition node
	 * @param Params - Node parameters
	 * @param PosX - X position
	 * @param PosY - Y position
	 * @param OutNodeId - Generated node ID
	 * @param OutError - Error message if failed
	 * @return Created node or nullptr
	 */
	static UEdGraphNode* CreateTransitionConditionNode(
		UEdGraph* TransitionGraph,
		const FString& NodeType,
		const TSharedPtr<FJsonObject>& Params,
		int32 PosX,
		int32 PosY,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Connect transition condition nodes
	 */
	static bool ConnectTransitionNodes(
		UEdGraph* TransitionGraph,
		const FString& SourceNodeId,
		const FString& SourcePinName,
		const FString& TargetNodeId,
		const FString& TargetPinName,
		FString& OutError
	);

	/**
	 * Connect condition result to transition result
	 */
	static bool ConnectToTransitionResult(
		UEdGraph* TransitionGraph,
		const FString& ConditionNodeId,
		const FString& ConditionPinName,
		FString& OutError
	);

	// ===== Animation Asset Nodes (Level 5) =====

	/**
	 * Add animation sequence player node to state graph
	 */
	static UEdGraphNode* CreateAnimSequenceNode(
		UEdGraph* StateGraph,
		UAnimSequence* AnimSequence,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Add BlendSpace player node to state graph (2D)
	 */
	static UEdGraphNode* CreateBlendSpaceNode(
		UEdGraph* StateGraph,
		UBlendSpace* BlendSpace,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Add BlendSpace1D player node to state graph
	 */
	static UEdGraphNode* CreateBlendSpace1DNode(
		UEdGraph* StateGraph,
		UBlendSpace1D* BlendSpace,
		FVector2D Position,
		FString& OutNodeId,
		FString& OutError
	);

	/**
	 * Connect animation node to state output pose
	 */
	static bool ConnectToOutputPose(
		UEdGraph* StateGraph,
		const FString& AnimNodeId,
		FString& OutError
	);

	/**
	 * Clear all nodes from state graph (except result)
	 */
	static bool ClearStateGraph(UEdGraph* StateGraph, FString& OutError);

	// ===== AnimGraph Root Connection (Level 5) =====

	/**
	 * Connect a State Machine node's output to the AnimGraph's root result
	 * This is used to wire the State Machine into the animation output
	 *
	 * @param AnimBP - The Animation Blueprint
	 * @param StateMachineName - Name of the State Machine node to connect
	 * @param OutError - Error message if failed
	 * @return True if connection succeeded
	 */
	static bool ConnectStateMachineToAnimGraphRoot(
		UAnimBlueprint* AnimBP,
		const FString& StateMachineName,
		FString& OutError,
		const FString& TargetGraphName = FString()
	);

	/**
	 * Find the AnimGraph root node (Output Pose)
	 */
	static UAnimGraphNode_Root* FindAnimGraphRoot(UAnimBlueprint* AnimBP, FString& OutError);

	// ===== Generic Anim Node Connection =====

	/**
	 * Connect two anim nodes via their pose pins in any animation graph.
	 * Resolves graphs by name (empty = main AnimGraph, otherwise layer function graph).
	 * Handles special target names: "root", "output", "output_pose" -> result node.
	 *
	 * When SourcePinName/TargetPinName are provided, uses explicit pin lookup
	 * instead of pose-pin search. Falls back to pose-pin behavior when omitted.
	 *
	 * @param AnimBP The Animation Blueprint
	 * @param SourceNodeId Node ID or title of the source node
	 * @param TargetNodeId Node ID, title, or "root"/"output"/"output_pose" for result node
	 * @param TargetGraphName Layer function graph name (empty = AnimGraph)
	 * @param OutError Error message if operation fails
	 * @param SourcePinName Optional explicit source pin name (bypasses pose-pin search)
	 * @param TargetPinName Optional explicit target pin name (bypasses pose-pin search)
	 * @return True if connection succeeded
	 */
	static bool ConnectAnimNodes(
		UAnimBlueprint* AnimBP,
		const FString& SourceNodeId,
		const FString& TargetNodeId,
		const FString& TargetGraphName,
		FString& OutError,
		const FString& SourcePinName = FString(),
		const FString& TargetPinName = FString(),
		const FString& StateMachineName = FString(),
		const FString& StateName = FString()
	);

	/**
	 * Bind a Blueprint variable to a pin on a target node.
	 * Creates a UK2Node_VariableGet in the target graph and wires its output
	 * to the specified pin on the target node using schema-validated connection.
	 *
	 * @param AnimBP The Animation Blueprint
	 * @param VariableName Name of the variable to bind
	 * @param TargetNodeId Node ID or title of the target node
	 * @param TargetPinName Name of the input pin to wire to
	 * @param TargetGraphName Layer function graph name (empty = AnimGraph)
	 * @param OutNodeId Returns the node ID of the created variable node
	 * @param OutError Error message if operation fails
	 * @return True if binding succeeded
	 */
	static bool BindVariable(
		UAnimBlueprint* AnimBP,
		const FString& VariableName,
		const FString& TargetNodeId,
		const FString& TargetPinName,
		const FString& TargetGraphName,
		FString& OutNodeId,
		FString& OutError,
		const FString& StateMachineName = FString(),
		const FString& StateName = FString()
	);

	// ===== Node Finding =====

	/**
	 * Find node by ID in graph
	 */
	static UEdGraphNode* FindNodeById(UEdGraph* Graph, const FString& NodeId);

	/**
	 * Find result/output node in graph
	 */
	static UEdGraphNode* FindResultNode(UEdGraph* Graph);

	/**
	 * Find pin on node by name
	 */
	static UEdGraphPin* FindPinByName(
		UEdGraphNode* Node,
		const FString& PinName,
		EEdGraphPinDirection Direction = EGPD_MAX
	);

	/**
	 * Find pin using configuration with multiple fallback strategies
	 * (Delegates to FAnimNodePinUtils)
	 *
	 * @param Node - Node to search
	 * @param Config - Search configuration (see FPinSearchConfig in AnimNodePinUtils.h)
	 * @param OutError - Optional: If provided and no pin found, populated with available pins list
	 * @return Found pin or nullptr
	 */
	static UEdGraphPin* FindPinWithFallbacks(
		UEdGraphNode* Node,
		const FPinSearchConfig& Config,
		FString* OutError = nullptr
	);

	/**
	 * Build error message listing available pins on a node
	 */
	static FString BuildAvailablePinsError(
		UEdGraphNode* Node,
		EEdGraphPinDirection Direction,
		const FString& Context
	);

	// ===== Node ID System =====

	/**
	 * Generate animation-specific node ID
	 */
	static FString GenerateAnimNodeId(
		const FString& NodeType,
		const FString& Context,
		UEdGraph* Graph
	);

	/**
	 * Set node ID
	 */
	static void SetNodeId(UEdGraphNode* Node, const FString& NodeId);

	/**
	 * Get node ID
	 */
	static FString GetNodeId(UEdGraphNode* Node);

	// ===== Serialization =====

	static TSharedPtr<FJsonObject> SerializeAnimNodeInfo(UEdGraphNode* Node);

	/**
	 * Serialize detailed pin information (for inspect_node_pins)
	 * Includes pin type, sub-category, default value, connected pins, etc.
	 */
	static TSharedPtr<FJsonObject> SerializeDetailedPinInfo(UEdGraphPin* Pin);

	// ===== Transition Graph Node Operations (for new MCP tools) =====

	/**
	 * Get all nodes in a transition graph with detailed pin information
	 * Used by get_transition_nodes operation
	 */
	static TSharedPtr<FJsonObject> GetTransitionGraphNodes(
		UEdGraph* TransitionGraph,
		FString& OutError
	);

	/**
	 * Get all transition nodes for a state machine (all transitions)
	 * Used by get_transition_nodes with state_machine parameter only
	 */
	static TSharedPtr<FJsonObject> GetAllTransitionNodes(
		UAnimBlueprint* AnimBP,
		const FString& StateMachineName,
		FString& OutError
	);

	/**
	 * Get an internal FAnimNode struct property (or all properties) from an anim graph node.
	 * If PropertyName is non-empty, returns that single property's value.
	 * If PropertyName is empty, returns all FAnimNode struct properties (discovery mode).
	 *
	 * Works on any UAnimGraphNode_Base-derived node (SequencePlayer, BlendSpacePlayer,
	 * TwoBoneIK, ModifyBone, etc.)
	 */
	static bool GetAnimNodeProperty(
		UEdGraph* Graph,
		const FString& NodeId,
		const FString& PropertyName,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError
	);

	/**
	 * Set an internal FAnimNode struct property on an anim graph node.
	 * Uses UE reflection to find and set the named property on the node's
	 * embedded FAnimNode struct (e.g., bLoopAnimation, PlayRate, StartPosition).
	 *
	 * Works on any UAnimGraphNode_Base-derived node (SequencePlayer, BlendSpacePlayer, etc.)
	 */
	static bool SetAnimNodeProperty(
		UEdGraph* Graph,
		const FString& NodeId,
		const FString& PropertyName,
		const FString& Value,
		FString& OutError
	);

	/**
	 * Set pin default value with type validation
	 * Returns error if value type doesn't match pin type
	 */
	static bool SetPinDefaultValueWithValidation(
		UEdGraph* Graph,
		const FString& NodeId,
		const FString& PinName,
		const FString& Value,
		FString& OutError
	);

	/**
	 * Validate if a value is compatible with a pin type
	 */
	static bool ValidatePinValueType(
		UEdGraphPin* Pin,
		const FString& Value,
		FString& OutError
	);

	/**
	 * Create a comparison chain: GetVariable → Comparison → Result
	 * Auto-chains with AND to existing logic if present
	 */
	static TSharedPtr<FJsonObject> CreateComparisonChain(
		UAnimBlueprint* AnimBP,
		UEdGraph* TransitionGraph,
		const FString& VariableName,
		const FString& ComparisonType,
		const FString& CompareValue,
		FVector2D Position,
		FString& OutError
	);

private:
	// Thread-safe counter for unique IDs
	static volatile int32 NodeIdCounter;

	// Node ID prefix
	static const FString NodeIdPrefix;

	// Internal node creation helpers
	static UEdGraphNode* CreateTimeRemainingNode(UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& Params, FVector2D Position, FString& OutError);
	static UEdGraphNode* CreateComparisonNode(UEdGraph* Graph, const FString& ComparisonType,
		const TSharedPtr<FJsonObject>& Params, FVector2D Position, FString& OutError, bool bIsBooleanType = false);
	static UEdGraphNode* CreateLogicNode(UEdGraph* Graph, const FString& LogicType,
		FVector2D Position, FString& OutError);
	static UEdGraphNode* CreateVariableGetNode(UEdGraph* Graph, UAnimBlueprint* AnimBP,
		const FString& VariableName, FVector2D Position, FString& OutError);
};
