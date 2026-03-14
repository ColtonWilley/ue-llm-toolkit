// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
class UActorComponent;
class UPrimitiveComponent;
class USceneComponent;
class AActor;
class USCS_Node;

/**
 * Utility class for inspecting Blueprint component trees and collision settings.
 * Provides structured JSON output for MCP tools — no JSON parsing here, just data.
 *
 * Replaces: dump_components.py, dump_collision.py
 */
class FComponentInspector
{
public:
	/**
	 * Serialize the full component tree for a Blueprint.
	 * Walks CDO components to see both C++ constructor and Blueprint-added components.
	 * Tags each component with origin ("cpp" or "blueprint") via SCS cross-reference.
	 * Returns JSON with: blueprint_name, parent_class, total_components, components[], non_scene_components[]
	 */
	static TSharedPtr<FJsonObject> SerializeComponentTree(UBlueprint* Blueprint);

	/**
	 * Serialize a single component by name from a Blueprint CDO.
	 * Returns JSON with component details, or an "error" field if not found.
	 */
	static TSharedPtr<FJsonObject> SerializeSingleComponent(UBlueprint* Blueprint, const FString& ComponentName);

	/**
	 * Serialize collision settings for all primitive components on a Blueprint CDO.
	 * Returns JSON with: source, source_name, primitive_component_count, components[]
	 */
	static TSharedPtr<FJsonObject> SerializeCollisionForBlueprint(UBlueprint* Blueprint);

	/**
	 * Serialize collision settings for all primitive components on a level actor.
	 * Returns JSON with: source, actor_label, actor_class, primitive_component_count, components[]
	 */
	static TSharedPtr<FJsonObject> SerializeCollisionForActor(AActor* Actor);

private:
	/**
	 * Serialize an SCS node and its children recursively.
	 * Includes component class, properties, tags, and child nodes.
	 */
	static TSharedPtr<FJsonObject> SerializeSCSNode(
		USCS_Node* Node,
		const TSet<FName>& ThisBlueprintVarNames);

	/**
	 * Serialize a scene component and its attached children from CDO.
	 * Uses pre-built ChildrenMap since CDOs don't populate AttachChildren.
	 * Includes origin tagging (cpp vs blueprint) via SCS cross-reference.
	 */
	static TSharedPtr<FJsonObject> SerializeSceneComponentNode(
		USceneComponent* Component,
		const TSet<FName>& SCSVarNames,
		const TMap<USceneComponent*, TArray<USceneComponent*>>& ChildrenMap);

	/**
	 * Get class-specific properties for a component, filtering out defaults.
	 * Returns only interesting non-default property values.
	 */
	static TSharedPtr<FJsonObject> GetComponentProperties(UActorComponent* Component);

	/**
	 * Serialize an SCS node and its children for the unified component tree.
	 * Same output format as SerializeSceneComponentNode (name, class, origin, properties, children).
	 * Skips nodes already present in CDO to avoid duplicates.
	 */
	static TSharedPtr<FJsonObject> SerializeSCSNodeForTree(
		USCS_Node* Node,
		const TSet<FName>& CDOComponentNames,
		int32& OutInjectedCount);

	/**
	 * Recursively find a node by name in a JSON component tree and inject a child.
	 * Returns true if the parent was found and child was injected.
	 */
	static bool InjectIntoJsonTree(
		TArray<TSharedPtr<FJsonValue>>& Tree,
		const FString& ParentName,
		TSharedPtr<FJsonObject> NodeToInject);

	/**
	 * Serialize collision settings for a single primitive component.
	 * Includes collision enabled, object type, profile, overlap events,
	 * built-in channel responses, and custom channel responses.
	 */
	static TSharedPtr<FJsonObject> SerializeCollision(UPrimitiveComponent* Component);

	/**
	 * Collect collision data from an array of primitive components into JSON.
	 */
	static TSharedPtr<FJsonObject> BuildCollisionResult(
		const TArray<UPrimitiveComponent*>& Components,
		const FString& Source,
		const FString& SourceName);

	// --- Enum-to-string helpers ---
	static FString CollisionResponseToString(ECollisionResponse Response);
	static FString CollisionEnabledToString(ECollisionEnabled::Type Type);
	static FString CollisionChannelToString(ECollisionChannel Channel);
};
