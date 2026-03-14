// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Query Blueprint information (read-only operations)
 *
 * Operations:
 *   - list: List all Blueprints in project (with optional filters)
 *   - inspect: Get detailed Blueprint info (variables, functions, parent class)
 *   - get_graph: Get graph information (node count, events)
 *   - get_components: Get full component hierarchy with properties
 *   - get_collision: Get collision settings for all primitive components
 *   - get_event_graph: Get event graph execution chains
 *   - get_anim_graph: Get anim graph pose chain
 *   - get_state_machine_detail: Get state machine states, transitions, rules
 *   - get_defaults: Get CDO default property values (all overrides or specific properties)
 *   - find_nodes: Search for nodes by class, label, or type (lightweight, no pins)
 *   - get_node: Get single node with full pin data
 *   - verify_connection: Check if connection exists between two nodes
 *   - get_node_connections: Get all connections for a node
 *   - get_graph_summary: Lightweight graph census (node counts, IDs, entry points)
 *   - get_exec_chain: Walk exec chain from a specific entry node
 */
class FMCPTool_BlueprintQuery : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("blueprint_query");
		Info.Description = TEXT(
			"Query Blueprint information (read-only).\n\n"
			"Operations:\n"
			"- 'list': Find Blueprints in project with optional filters\n"
			"- 'inspect': Get detailed Blueprint info (variables, functions, parent class)\n"
			"- 'get_graph': Get graph structure (node count, events, connections)\n"
			"- 'get_components': Get full component hierarchy tree with class-specific properties\n"
			"- 'get_collision': Get collision settings (profiles, channels, custom channels) for all primitive components\n"
			"- 'get_event_graph': Get event graph execution chains (events, function calls, branches)\n"
			"- 'get_anim_graph': Get anim graph pose chain (Root backward through blends/slots/players)\n"
			"- 'get_state_machine_detail': Get state machine states, transitions, and rule expressions\n"
			"- 'get_defaults': Get CDO default property values (all overrides or specific properties)\n"
			"- 'find_nodes': Search nodes by class/label/type — lightweight summaries, no pin arrays\n"
			"- 'get_node': Single node by ID with complete pin data\n"
			"- 'verify_connection': Check if connection exists between two nodes/pins\n"
			"- 'get_node_connections': All connections for one node (input/output/both)\n"
			"- 'get_graph_summary': Lightweight census — node counts by type, entry points, all IDs\n"
			"- 'get_exec_chain': Walk exec chain from one entry node with depth limit\n\n"
			"Use 'list' first to discover Blueprints, then other operations for details.\n"
			"Use targeted queries (find_nodes, get_node, verify_connection) instead of full dumps when possible.\n\n"
			"Example paths:\n"
			"- '/Game/Blueprints/BP_Character'\n"
			"- '/Game/UI/WBP_MainMenu'\n"
			"- '/Game/Characters/ABP_Hero' (Animation Blueprint)\n\n"
			"Returns: Blueprint metadata, variables, functions, graph structure, components, or collision info."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation: 'list', 'inspect', 'get_graph', 'get_components', 'get_collision', 'get_event_graph', 'get_anim_graph', 'get_state_machine_detail', 'get_defaults', 'find_nodes', 'get_node', 'verify_connection', 'get_node_connections', 'get_graph_summary', 'get_exec_chain'"), true),
			FMCPToolParameter(TEXT("path_filter"), TEXT("string"),
				TEXT("Path prefix filter (e.g., '/Game/Blueprints/')"), false, TEXT("/Game/")),
			FMCPToolParameter(TEXT("type_filter"), TEXT("string"),
				TEXT("Blueprint type filter: 'Actor', 'Object', 'Widget', 'AnimBlueprint', etc."), false),
			FMCPToolParameter(TEXT("name_filter"), TEXT("string"),
				TEXT("Name substring filter"), false),
			FMCPToolParameter(TEXT("limit"), TEXT("number"),
				TEXT("Maximum results to return (1-1000, default: 25)"), false, TEXT("25")),
			FMCPToolParameter(TEXT("blueprint_path"), TEXT("string"),
				TEXT("Full Blueprint asset path (required for most operations except 'list')"), false),
			FMCPToolParameter(TEXT("include_variables"), TEXT("boolean"),
				TEXT("Include variable list in inspect result (default: false)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("include_functions"), TEXT("boolean"),
				TEXT("Include function list in inspect result (default: false)"), false, TEXT("false")),
			FMCPToolParameter(TEXT("include_graphs"), TEXT("boolean"),
				TEXT("Include graph info in inspect result"), false, TEXT("false")),
			FMCPToolParameter(TEXT("component_name"), TEXT("string"),
				TEXT("For 'get_components': return only this component (by CDO name or SCS variable name)"), false),
			FMCPToolParameter(TEXT("actor_label"), TEXT("string"),
				TEXT("For 'get_collision': inspect a level actor instead of Blueprint CDO"), false),
			FMCPToolParameter(TEXT("graph_name"), TEXT("string"),
				TEXT("For graph operations: target a specific graph by name"), false),
			FMCPToolParameter(TEXT("state_machine_name"), TEXT("string"),
				TEXT("For 'get_state_machine_detail': which state machine to inspect"), false),
			FMCPToolParameter(TEXT("properties"), TEXT("array"),
				TEXT("For 'get_defaults': specific property names/paths to read (omit for all overrides)"), false),
			FMCPToolParameter(TEXT("search"), TEXT("string"),
				TEXT("For 'find_nodes': label substring to search for"), false),
			FMCPToolParameter(TEXT("node_class"), TEXT("string"),
				TEXT("For 'find_nodes': node class filter (e.g. 'K2Node_CallFunction')"), false),
			FMCPToolParameter(TEXT("graph_type"), TEXT("string"),
				TEXT("For 'find_nodes'/'get_graph_summary': 'EventGraph', 'AnimGraph', or 'All'"), false),
			FMCPToolParameter(TEXT("node_id"), TEXT("string"),
				TEXT("For 'get_node'/'get_exec_chain'/'get_node_connections': target node ID"), false),
			FMCPToolParameter(TEXT("source_node_id"), TEXT("string"),
				TEXT("For 'verify_connection': source node ID"), false),
			FMCPToolParameter(TEXT("target_node_id"), TEXT("string"),
				TEXT("For 'verify_connection': target node ID"), false),
			FMCPToolParameter(TEXT("source_pin"), TEXT("string"),
				TEXT("For 'verify_connection': source pin name (empty = any)"), false),
			FMCPToolParameter(TEXT("target_pin"), TEXT("string"),
				TEXT("For 'verify_connection': target pin name (empty = any)"), false),
			FMCPToolParameter(TEXT("direction"), TEXT("string"),
				TEXT("For 'get_node_connections': 'input', 'output', or 'both' (default: both)"), false),
			FMCPToolParameter(TEXT("pin_filter"), TEXT("string"),
				TEXT("For 'get_node_connections': comma-separated pin names to include"), false),
			FMCPToolParameter(TEXT("max_depth"), TEXT("number"),
				TEXT("For 'get_exec_chain': max walk depth (default: 50)"), false, TEXT("50"))
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/** List Blueprints matching filters */
	FMCPToolResult ExecuteList(const TSharedRef<FJsonObject>& Params);

	/** Get detailed Blueprint info */
	FMCPToolResult ExecuteInspect(const TSharedRef<FJsonObject>& Params);

	/** Get graph information */
	FMCPToolResult ExecuteGetGraph(const TSharedRef<FJsonObject>& Params);

	/** Get full component hierarchy with properties */
	FMCPToolResult ExecuteGetComponents(const TSharedRef<FJsonObject>& Params);

	/** Get collision settings for all primitive components */
	FMCPToolResult ExecuteGetCollision(const TSharedRef<FJsonObject>& Params);

	/** Get event graph execution chains */
	FMCPToolResult ExecuteGetEventGraph(const TSharedRef<FJsonObject>& Params);

	/** Get anim graph pose chain */
	FMCPToolResult ExecuteGetAnimGraph(const TSharedRef<FJsonObject>& Params);

	/** Get state machine detail */
	FMCPToolResult ExecuteGetStateMachineDetail(const TSharedRef<FJsonObject>& Params);

	/** Get CDO default property values */
	FMCPToolResult ExecuteGetDefaults(const TSharedRef<FJsonObject>& Params);

	/** Search for nodes (lightweight) */
	FMCPToolResult ExecuteFindNodes(const TSharedRef<FJsonObject>& Params);

	/** Get single node with full pins */
	FMCPToolResult ExecuteGetNode(const TSharedRef<FJsonObject>& Params);

	/** Verify connection between two nodes */
	FMCPToolResult ExecuteVerifyConnection(const TSharedRef<FJsonObject>& Params);

	/** Get all connections for a node */
	FMCPToolResult ExecuteGetNodeConnections(const TSharedRef<FJsonObject>& Params);

	/** Get lightweight graph census */
	FMCPToolResult ExecuteGetGraphSummary(const TSharedRef<FJsonObject>& Params);

	/** Walk exec chain from entry node */
	FMCPToolResult ExecuteGetExecChain(const TSharedRef<FJsonObject>& Params);
};
