// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UAnimBlueprint;

/**
 * AnimLayerEditor - Read/write operations for Animation Layer Interfaces
 * and LinkedAnimLayer nodes in Animation Blueprints.
 *
 * Supports:
 * - Inspecting implemented layer interfaces
 * - Listing available layer functions
 * - Finding LinkedAnimLayer nodes in the AnimGraph
 * - Adding/removing layer interfaces
 * - Creating LinkedAnimLayer nodes
 * - Setting linked layer instance class
 */
class FAnimLayerEditor
{
public:
	// ===== Read Operations =====

	/**
	 * Get all Animation Layer Interfaces implemented by an AnimBlueprint.
	 * Filters ImplementedInterfaces to only those deriving from UAnimLayerInterface.
	 *
	 * @param AnimBP The Animation Blueprint to inspect
	 * @param OutError Error message if operation fails
	 * @return JSON with interface list (name, class_path, layer_functions)
	 */
	static TSharedPtr<FJsonObject> GetImplementedLayerInterfaces(
		UAnimBlueprint* AnimBP,
		FString& OutError);

	/**
	 * Get all available layer functions (from compiled class data).
	 * Lists FAnimBlueprintFunction entries from IAnimClassInterface.
	 *
	 * @param AnimBP The Animation Blueprint to inspect
	 * @param OutError Error message if operation fails
	 * @return JSON with layer function list (name, group, input_poses, implemented)
	 */
	static TSharedPtr<FJsonObject> GetAvailableLayers(
		UAnimBlueprint* AnimBP,
		FString& OutError);

	/**
	 * Get all LinkedAnimLayer nodes in the AnimGraph.
	 *
	 * @param AnimBP The Animation Blueprint to inspect
	 * @param OutError Error message if operation fails
	 * @return JSON with node list (node_id, layer_name, interface_class, instance_class, connected)
	 */
	static TSharedPtr<FJsonObject> GetLinkedLayerNodes(
		UAnimBlueprint* AnimBP,
		FString& OutError);

	/**
	 * Find a layer function graph by name.
	 * Searches ImplementedInterfaces[].Graphs[] (ALI-sourced) and
	 * FunctionGraphs[] filtered to UAnimationGraph (self-defined layers).
	 *
	 * @param AnimBP The Animation Blueprint to search
	 * @param LayerName Name of the layer function graph
	 * @param OutError Descriptive error listing available graphs if not found
	 * @return The layer function graph, or nullptr if not found
	 */
	static UEdGraph* FindLayerFunctionGraph(
		UAnimBlueprint* AnimBP,
		const FString& LayerName,
		FString& OutError);

	/**
	 * Inspect a layer function graph — list all nodes with pin info.
	 *
	 * @param AnimBP The Animation Blueprint
	 * @param LayerName Name of the layer function graph
	 * @param OutError Error message if operation fails
	 * @return JSON with node list (node_id, class, pins, connections)
	 */
	static TSharedPtr<FJsonObject> InspectLayerGraph(
		UAnimBlueprint* AnimBP,
		const FString& LayerName,
		FString& OutError);

	// ===== Write Operations =====

	/**
	 * Add an Animation Layer Interface to an AnimBlueprint.
	 * Uses FBlueprintEditorUtils::ImplementNewInterface and recompiles.
	 *
	 * @param AnimBP The Animation Blueprint to modify
	 * @param InterfaceClassPath Full class path of the ALI (e.g., "/Game/Animation/ALI_Locomotion.ALI_Locomotion_C")
	 * @param OutError Error message if operation fails
	 * @return JSON with success status and list of new layer graphs created
	 */
	static TSharedPtr<FJsonObject> AddLayerInterface(
		UAnimBlueprint* AnimBP,
		const FString& InterfaceClassPath,
		FString& OutError);

	/**
	 * Remove an Animation Layer Interface from an AnimBlueprint.
	 *
	 * @param AnimBP The Animation Blueprint to modify
	 * @param InterfaceClassPath Full class path of the ALI to remove
	 * @param OutError Error message if operation fails
	 * @return JSON with success status
	 */
	static TSharedPtr<FJsonObject> RemoveLayerInterface(
		UAnimBlueprint* AnimBP,
		const FString& InterfaceClassPath,
		FString& OutError);

	/**
	 * Create a LinkedAnimLayer node in the AnimGraph.
	 *
	 * @param AnimBP The Animation Blueprint to modify
	 * @param LayerName Name of the layer function to reference
	 * @param Position Node position in the graph
	 * @param InstanceClass Optional: path to AnimBP class to use as linked instance
	 * @param OutError Error message if operation fails
	 * @return JSON with node_id, layer_name, interface info
	 */
	static TSharedPtr<FJsonObject> CreateLinkedLayerNode(
		UAnimBlueprint* AnimBP,
		const FString& LayerName,
		FVector2D Position,
		const FString& InstanceClass,
		FString& OutError);

	/**
	 * Set the instance class on a LinkedAnimLayer node.
	 *
	 * @param AnimBP The Animation Blueprint to modify
	 * @param NodeId The MCP node ID of the LinkedAnimLayer node
	 * @param InstanceClassPath Path to the AnimBP class to set as instance
	 * @param OutError Error message if operation fails
	 * @return JSON with success status
	 */
	static TSharedPtr<FJsonObject> SetLinkedLayerInstance(
		UAnimBlueprint* AnimBP,
		const FString& NodeId,
		const FString& InstanceClassPath,
		FString& OutError);
};
