// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UControlRigBlueprint;
class URigVMGraph;
class URigVMController;
class URigVMNode;

/**
 * Utility class for inspecting and editing Control Rig graphs.
 * Read operations use URigVMGraph for traversal.
 * Write operations use URigVMController for undo-safe mutations.
 *
 * Replaces: dump_control_rig.py (read) and edit_control_rig.py (write)
 */
class FControlRigEditor
{
public:
	// ===== Asset Loading =====

	/** Load a ControlRigBlueprint from an asset path. Returns nullptr + sets OutError on failure. */
	static UControlRigBlueprint* LoadControlRig(const FString& AssetPath, FString& OutError);

	/** Get the default graph from a ControlRigBlueprint. Returns nullptr if not found. */
	static URigVMGraph* GetDefaultGraph(UControlRigBlueprint* RigBlueprint);

	/** Get or create a controller for the given graph. Returns nullptr + sets OutError on failure. */
	static URigVMController* GetController(UControlRigBlueprint* RigBlueprint, URigVMGraph* Graph, FString& OutError);

	// ===== Read Operations =====

	/** Serialize the full graph structure (nodes, pins, links) to JSON. */
	static TSharedPtr<FJsonObject> SerializeGraph(UControlRigBlueprint* RigBlueprint);

	/** Serialize the rig hierarchy (bones, controls, nulls, curves) to JSON. */
	static TSharedPtr<FJsonObject> SerializeHierarchy(UControlRigBlueprint* RigBlueprint);

	/** List available unit struct types (optionally filtered). */
	static TSharedPtr<FJsonObject> ListStructs(URigVMController* Controller, const FString& Filter = FString());

	/** List available template notations (optionally filtered). */
	static TSharedPtr<FJsonObject> ListTemplates(URigVMController* Controller, const FString& Filter = FString());

	// ===== Write Operations =====
	// All write operations return a JSON result with "success" bool and either "node"/"message" or "error" fields.

	/** Add a unit node from a struct path. */
	static TSharedPtr<FJsonObject> AddNode(URigVMController* Controller,
		const FString& StructPath, const FString& NodeName, float X = 0.f, float Y = 0.f);

	/** Add a template node from a notation string. */
	static TSharedPtr<FJsonObject> AddTemplateNode(URigVMController* Controller,
		const FString& Notation, const FString& NodeName, float X = 0.f, float Y = 0.f);

	/** Add a variable getter/setter node. */
	static TSharedPtr<FJsonObject> AddVariableNode(UControlRigBlueprint* RigBlueprint, URigVMController* Controller,
		const FString& VarName, bool bIsGetter, const FString& CppType = FString(), float X = 0.f, float Y = 0.f);

	/** Remove a node by name. */
	static TSharedPtr<FJsonObject> RemoveNode(URigVMController* Controller, const FString& NodeName);

	/** Link two pins. */
	static TSharedPtr<FJsonObject> AddLink(URigVMController* Controller,
		const FString& OutputPinPath, const FString& InputPinPath);

	/** Break a specific link between two pins. */
	static TSharedPtr<FJsonObject> BreakLink(URigVMController* Controller,
		const FString& OutputPinPath, const FString& InputPinPath);

	/** Break all links on a pin. */
	static TSharedPtr<FJsonObject> BreakAllLinks(URigVMController* Controller,
		const FString& PinPath, bool bAsInput);

	/** Set a pin's default value. */
	static TSharedPtr<FJsonObject> SetPinDefault(URigVMController* Controller,
		const FString& PinPath, const FString& Value);

	/** Set pin expansion state. */
	static TSharedPtr<FJsonObject> SetPinExpansion(URigVMController* Controller,
		const FString& PinPath, bool bExpanded);

	/** Add a member variable to the rig blueprint. */
	static TSharedPtr<FJsonObject> AddMemberVariable(UControlRigBlueprint* RigBlueprint,
		const FString& Name, const FString& CppType, bool bIsPublic = true, const FString& DefaultValue = FString());

	/** Remove a member variable from the rig blueprint. */
	static TSharedPtr<FJsonObject> RemoveMemberVariable(UControlRigBlueprint* RigBlueprint, const FString& Name);

	/** Recompile the RigVM. */
	static TSharedPtr<FJsonObject> Recompile(UControlRigBlueprint* RigBlueprint);

	/** Execute a batch of operations within a single undo bracket. */
	static TSharedPtr<FJsonObject> ExecuteBatch(UControlRigBlueprint* RigBlueprint,
		URigVMController* Controller, const TArray<TSharedPtr<FJsonValue>>& Operations);

private:
	/** Serialize a single RigVM node to JSON. */
	static TSharedPtr<FJsonObject> SerializeNode(URigVMNode* Node);

	/** Build a success result with node info. */
	static TSharedPtr<FJsonObject> NodeResult(const FString& Message, URigVMNode* Node);

	/** Build a success result with a message. */
	static TSharedPtr<FJsonObject> SuccessResult(const FString& Message);

	/** Build an error result. */
	static TSharedPtr<FJsonObject> ErrorResult(const FString& Message);

	/** Guess a member variable's C++ type from the blueprint. */
	static bool GuessMemberVarType(UControlRigBlueprint* RigBlueprint, const FString& VarName,
		FString& OutCppType, FString& OutObjectPath);

	/** Map from common C++ type names to their UObject paths. */
	static FString GetTypeObjectPath(const FString& CppType);

	/** Pin direction to string. */
	static FString PinDirectionToString(int32 Direction);

	/** Dispatch a single batch operation. */
	static TSharedPtr<FJsonObject> DispatchBatchOp(UControlRigBlueprint* RigBlueprint,
		URigVMController* Controller, const TSharedPtr<FJsonObject>& OpData);
};
