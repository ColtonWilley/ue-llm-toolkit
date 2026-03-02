// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Inspect and edit Control Rig graphs.
 *
 * Read Operations:
 *   - inspect: Get full graph structure (nodes, pins, links) + hierarchy
 *   - list_structs: List available unit struct types
 *   - list_templates: List available template notations
 *
 * Write Operations:
 *   - add_node: Add a unit node from struct path
 *   - add_template: Add a template node from notation
 *   - add_var_node: Add a variable getter/setter node
 *   - remove_node: Remove a node by name
 *   - link: Connect two pins
 *   - unlink: Disconnect two pins
 *   - unlink_all: Disconnect all links on a pin
 *   - set_default: Set a pin's default value
 *   - set_expand: Expand/collapse a compound pin
 *   - add_member_var: Add a member variable
 *   - remove_member_var: Remove a member variable
 *   - recompile: Recompile the RigVM
 *   - batch: Execute multiple operations in a single undo bracket
 */
class FMCPTool_ControlRig : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("control_rig");
		Info.Description = TEXT(
			"Control Rig inspection and editing tool.\n\n"
			"Read Operations:\n"
			"- 'inspect': Get full graph structure (nodes, pins, links, hierarchy)\n"
			"- 'list_structs': List available RigVM unit struct types (filter optional)\n"
			"- 'list_templates': List available template notations (filter optional)\n\n"
			"Write Operations:\n"
			"- 'add_node': Add unit node from struct path\n"
			"- 'add_template': Add template node from notation string\n"
			"- 'add_var_node': Add variable getter/setter node\n"
			"- 'remove_node': Remove node by name\n"
			"- 'link': Connect two pins (output -> input)\n"
			"- 'unlink': Disconnect two pins\n"
			"- 'unlink_all': Disconnect all links on a pin\n"
			"- 'set_default': Set pin default value\n"
			"- 'set_expand': Expand/collapse compound pin\n"
			"- 'add_member_var': Add member variable to rig\n"
			"- 'remove_member_var': Remove member variable\n"
			"- 'recompile': Recompile the RigVM\n"
			"- 'batch': Execute multiple ops in single undo bracket (JSON array)\n\n"
			"Pin paths are dot-separated: 'NodeName.PinName.SubPin'\n"
			"Struct paths like: '/Script/ControlRig.RigUnit_GetTransform'"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("rig_path"), TEXT("string"),
				TEXT("Control Rig asset path (e.g., '/Game/Rigs/CR_FootIK')"), true),
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation: inspect, list_structs, list_templates, add_node, add_template, add_var_node, remove_node, link, unlink, unlink_all, set_default, set_expand, add_member_var, remove_member_var, recompile, batch"), true),
			// Node creation params
			FMCPToolParameter(TEXT("struct_path"), TEXT("string"),
				TEXT("Unit struct path for add_node (e.g., '/Script/ControlRig.RigUnit_GetTransform')")),
			FMCPToolParameter(TEXT("notation"), TEXT("string"),
				TEXT("Template notation for add_template")),
			FMCPToolParameter(TEXT("node_name"), TEXT("string"),
				TEXT("Name for new node")),
			FMCPToolParameter(TEXT("x"), TEXT("number"),
				TEXT("Node X position"), false, TEXT("0")),
			FMCPToolParameter(TEXT("y"), TEXT("number"),
				TEXT("Node Y position"), false, TEXT("0")),
			// Variable node params
			FMCPToolParameter(TEXT("var_name"), TEXT("string"),
				TEXT("Variable name for add_var_node")),
			FMCPToolParameter(TEXT("is_getter"), TEXT("boolean"),
				TEXT("True for getter, false for setter (add_var_node)"), false, TEXT("true")),
			FMCPToolParameter(TEXT("cpp_type"), TEXT("string"),
				TEXT("C++ type (e.g., FTransform, float, FVector)")),
			// Link params
			FMCPToolParameter(TEXT("output_pin"), TEXT("string"),
				TEXT("Output pin path for link/unlink (e.g., 'NodeName.PinName')")),
			FMCPToolParameter(TEXT("input_pin"), TEXT("string"),
				TEXT("Input pin path for link/unlink")),
			// Pin params
			FMCPToolParameter(TEXT("pin_path"), TEXT("string"),
				TEXT("Pin path for set_default/set_expand/unlink_all")),
			FMCPToolParameter(TEXT("value"), TEXT("string"),
				TEXT("Value for set_default")),
			FMCPToolParameter(TEXT("expanded"), TEXT("boolean"),
				TEXT("Expansion state for set_expand"), false, TEXT("true")),
			FMCPToolParameter(TEXT("as_input"), TEXT("boolean"),
				TEXT("For unlink_all: true=input links, false=output links"), false, TEXT("true")),
			// Member variable params
			FMCPToolParameter(TEXT("is_public"), TEXT("boolean"),
				TEXT("Visibility for add_member_var"), false, TEXT("true")),
			FMCPToolParameter(TEXT("default_value"), TEXT("string"),
				TEXT("Default value for add_member_var")),
			// Filter for list operations
			FMCPToolParameter(TEXT("filter"), TEXT("string"),
				TEXT("Name filter for list_structs/list_templates")),
			// Batch operations
			FMCPToolParameter(TEXT("operations"), TEXT("array"),
				TEXT("JSON array of operation objects for batch mode"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// Read operations
	FMCPToolResult HandleInspect(const FString& RigPath);
	FMCPToolResult HandleListStructs(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleListTemplates(const FString& RigPath, const TSharedRef<FJsonObject>& Params);

	// Write operations
	FMCPToolResult HandleAddNode(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddTemplate(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddVarNode(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveNode(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleLink(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleUnlink(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleUnlinkAll(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetDefault(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetExpand(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddMemberVar(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveMemberVar(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRecompile(const FString& RigPath);
	FMCPToolResult HandleBatch(const FString& RigPath, const TSharedRef<FJsonObject>& Params);
};
