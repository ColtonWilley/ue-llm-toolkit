// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UAnimGraphNode_StateMachine;

/**
 * Utility class for deep-reading Blueprint graph structures.
 * Produces human-readable text summaries + structured metadata.
 * Uses native UEdGraph API (no T3D export hack).
 *
 * Replaces: dump_event_graph.py
 */
class FBlueprintGraphReader
{
public:
	/** Result from a graph read operation */
	struct FGraphReadResult
	{
		FString Summary;                    // Human-readable formatted text
		TSharedPtr<FJsonObject> Metadata;   // Structured graph metadata
	};

	/** Result from cross-graph node lookup */
	struct FNodeSearchResult
	{
		UEdGraphNode* Node = nullptr;
		UEdGraph* Graph = nullptr;
		FString GraphName;
		FString GraphType; // "EventGraph", "AnimGraph", "StateMachine", "Other"
	};

	// ===== Existing Full-Dump Operations =====

	/**
	 * Read event graph execution chains from a Blueprint.
	 * Walks exec pins from entry nodes (Event, CustomEvent, FunctionEntry).
	 * @param Blueprint - The Blueprint to read
	 * @param GraphName - Optional: target a specific graph by name (empty = all event graphs)
	 */
	static FGraphReadResult ReadEventGraph(UBlueprint* Blueprint, const FString& GraphName = FString());

	/**
	 * Read anim graph pose chain from an AnimBlueprint.
	 * Walks backward from Root through pose link pins.
	 * @param Blueprint - The AnimBlueprint to read
	 * @param GraphName - Optional: target a specific anim graph by name
	 */
	static FGraphReadResult ReadAnimGraph(UBlueprint* Blueprint, const FString& GraphName = FString());

	/**
	 * Read state machine detail from an AnimBlueprint.
	 * Shows states, transitions, entry state, and rule expressions.
	 * @param Blueprint - The AnimBlueprint to read
	 * @param StateMachineName - Optional: which state machine (empty = all)
	 */
	static FGraphReadResult ReadStateMachineDetail(UBlueprint* Blueprint, const FString& StateMachineName = FString());

	// ===== Targeted Query Operations =====

	/**
	 * Search for nodes by class, label substring, or type across all graphs.
	 * Returns lightweight summaries (no pin arrays).
	 * @param Blueprint - Blueprint to search
	 * @param Search - Optional label substring filter
	 * @param NodeClass - Optional node class filter (e.g. "K2Node_CallFunction")
	 * @param GraphName - Optional graph name filter
	 * @param GraphType - Optional graph type filter: "EventGraph", "AnimGraph", "All"
	 */
	static TSharedPtr<FJsonObject> FindNodes(UBlueprint* Blueprint,
		const FString& Search = FString(),
		const FString& NodeClass = FString(),
		const FString& GraphName = FString(),
		const FString& GraphType = FString());

	/**
	 * Retrieve a single node by ID with complete pin data.
	 * @param Blueprint - Blueprint to search
	 * @param NodeId - Node ID to find
	 * @param GraphName - Optional graph name hint (optimization)
	 */
	static TSharedPtr<FJsonObject> GetNode(UBlueprint* Blueprint,
		const FString& NodeId,
		const FString& GraphName = FString());

	/**
	 * Check whether a specific connection exists between two nodes/pins.
	 * @param Blueprint - Blueprint to check
	 * @param SourceNodeId - Source node ID
	 * @param TargetNodeId - Target node ID
	 * @param SourcePin - Optional source pin name (empty = any connection)
	 * @param TargetPin - Optional target pin name (empty = any connection)
	 */
	static TSharedPtr<FJsonObject> VerifyConnection(UBlueprint* Blueprint,
		const FString& SourceNodeId,
		const FString& TargetNodeId,
		const FString& SourcePin = FString(),
		const FString& TargetPin = FString());

	/**
	 * Get all connections for a specific node.
	 * @param Blueprint - Blueprint to search
	 * @param NodeId - Node ID to inspect
	 * @param Direction - Optional: "input", "output", "both" (default: both)
	 * @param PinFilter - Optional comma-separated pin names to include
	 */
	static TSharedPtr<FJsonObject> GetNodeConnections(UBlueprint* Blueprint,
		const FString& NodeId,
		const FString& Direction = FString(),
		const FString& PinFilter = FString());

	/**
	 * Lightweight graph census: node counts by type, entry points, all node IDs.
	 * Auto-assigns IDs to nodes that lack them.
	 * @param Blueprint - Blueprint to summarize
	 * @param GraphName - Optional graph name filter
	 * @param GraphType - Optional graph type filter: "EventGraph", "AnimGraph", "All"
	 */
	static TSharedPtr<FJsonObject> GetGraphSummary(UBlueprint* Blueprint,
		const FString& GraphName = FString(),
		const FString& GraphType = FString());

	/**
	 * Walk exec chain from a specific entry node with depth limit.
	 * Scoped version of ReadEventGraph.
	 * @param Blueprint - Blueprint to walk
	 * @param NodeId - Starting node ID
	 * @param MaxDepth - Max walk depth (default 50)
	 */
	static TSharedPtr<FJsonObject> GetExecChain(UBlueprint* Blueprint,
		const FString& NodeId,
		int32 MaxDepth = 50);

	// ===== Cross-Graph Node Lookup =====

	/**
	 * Find a node by ID across all graphs in a Blueprint.
	 * Searches MCP-assigned IDs first, falls back to UE node name.
	 * @param Blueprint - Blueprint to search
	 * @param NodeId - Node ID to find
	 * @param GraphNameHint - Optional graph name to search first
	 * @return Search result with node, graph, and graph metadata
	 */
	static FNodeSearchResult FindNodeByIdAcrossGraphs(UBlueprint* Blueprint,
		const FString& NodeId,
		const FString& GraphNameHint = FString());

private:
	// --- State-Bound Graph Discovery ---
	struct FStateBoundGraph
	{
		UEdGraph* Graph;
		FString StateMachineName;
		FString StateName;
	};
	static TArray<FStateBoundGraph> CollectStateBoundGraphs(UBlueprint* Blueprint);

	// --- Node Labeling ---
	static FString GetNodeLabel(UEdGraphNode* Node);
	static FString GetK2NodeLabel(UEdGraphNode* Node);
	static FString GetAnimNodeLabel(UEdGraphNode* Node);

	// --- Pin Helpers ---
	static bool IsExecPin(UEdGraphPin* Pin);
	static bool IsPoseLinkPin(UEdGraphPin* Pin);
	static TArray<UEdGraphPin*> GetExecOutputPins(UEdGraphNode* Node);
	static TArray<UEdGraphPin*> GetPoseInputPins(UEdGraphNode* Node);
	static TArray<UEdGraphPin*> GetDataInputPins(UEdGraphNode* Node);
	static UEdGraphNode* FollowThroughKnots(UEdGraphNode* Node, EEdGraphPinDirection Direction);

	// --- EventGraph Walking ---
	static void WalkExecChain(UEdGraphNode* Node, int32 Depth, FString& Output,
		TSet<UEdGraphNode*>& Visited, TArray<FString>* OutVisitedIds = nullptr);
	static FString GetDataInputSummary(UEdGraphNode* Node);

	// --- AnimGraph Walking ---
	static void WalkPoseChain(UEdGraphNode* Node, int32 Depth, FString& Output,
		TSet<UEdGraphNode*>& Visited, const FString& PinLabel = FString());
	static FString GetBlendPinLabel(UEdGraphPin* Pin, UEdGraphNode* OwnerNode);

	// --- State Machine ---
	static void FormatStateMachine(UAnimGraphNode_StateMachine* SMNode, FString& Output,
		TSharedPtr<FJsonObject>& SMMetadata);
	static FString BuildTransitionRuleExpression(UEdGraph* TransitionGraph);
	static FString BuildExpression(UEdGraphNode* Node, TSet<UEdGraphNode*>& Visited);
	static FString ResolveOperatorSymbol(const FString& FuncName);

	// --- K2 Node Serialization ---
	static TSharedPtr<FJsonObject> SerializeK2NodeInfo(UEdGraphNode* Node, UEdGraph* Graph);
	static void DeriveK2NodeTypeAndContext(UEdGraphNode* Node, FString& OutNodeType, FString& OutContext);

	// --- Anim Node Serialization ---
	static TSharedPtr<FJsonObject> SerializeAnimNodeInfo(UEdGraphNode* Node, UEdGraph* Graph);

	// --- Graph Classification ---
	enum class EGraphType { EventGraph, AnimGraph, StateMachine, Other };
	static EGraphType ClassifyGraph(UEdGraph* Graph);
	static FString GraphTypeToString(EGraphType Type);

	// --- Node ID Helpers ---
	static FString GetOrAssignNodeId(UEdGraphNode* Node, UEdGraph* Graph);
};
