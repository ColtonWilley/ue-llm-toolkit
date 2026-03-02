// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class AActor;
class UWorld;

/**
 * Utility class for querying level actors with noise filtering.
 * Filters infrastructure actors (Landscape, Foliage, NavMesh, HLOD, etc.)
 * and provides list, find, and info operations.
 *
 * Replaces: level_query.py
 */
class FLevelQueryHelper
{
public:
	/**
	 * List all gameplay-relevant actors grouped by class.
	 * Filters out infrastructure actors (Landscape, Foliage, NavMesh, HLOD, WorldSettings, etc.)
	 * and lights (unless explicitly included).
	 * Returns JSON with: map_name, total_actors, gameplay_actors, filtered_count, by_class{}
	 */
	static TSharedPtr<FJsonObject> ListGameplayActors(UWorld* World);

	/**
	 * Find actors matching a case-insensitive substring pattern.
	 * Searches both actor label and class name. Includes lights.
	 * Returns JSON with: map_name, pattern, match_count, matches[]
	 */
	static TSharedPtr<FJsonObject> FindActors(UWorld* World, const FString& Pattern);

	/**
	 * Get detailed info for a specific actor by label.
	 * Includes transform, all components, and collision data for primitive components.
	 * Returns JSON with: label, class, blueprint_path, transform, components[], collision[]
	 */
	static TSharedPtr<FJsonObject> InspectActor(UWorld* World, const FString& ActorLabel);

private:
	/** Check if an actor is infrastructure noise that should be filtered out */
	static bool IsInfrastructureActor(AActor* Actor, bool bIncludeLights = false);

	/** Get the noise category for an actor class (empty string if gameplay) */
	static FString ClassifyNoise(const FString& ClassName);

	/** Build a compact actor info JSON (name, label, class, location) */
	static TSharedPtr<FJsonObject> BuildActorSummary(AActor* Actor);
};
