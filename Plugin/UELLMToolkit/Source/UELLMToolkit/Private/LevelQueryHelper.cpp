// Copyright Natali Caggiano. All Rights Reserved.

#include "LevelQueryHelper.h"
#include "ComponentInspector.h"
#include "UnrealClaudeUtils.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

// ============================================================================
// Infrastructure filtering
// ============================================================================

static const TArray<FString> GNoisePrefixes = {
	TEXT("Landscape"),
	TEXT("Foliage"),
	TEXT("InstancedFoliage"),
	TEXT("WorldPartition"),
	TEXT("WorldDataLayers"),
	TEXT("NavMesh"),
	TEXT("RecastNavMesh"),
	TEXT("AbstractNavData"),
	TEXT("NavigationData"),
	TEXT("HLOD"),
	TEXT("DataLayer"),
	TEXT("LevelInstance"),
	TEXT("PackedLevelActor"),
};

static const TSet<FString> GNoiseExact = {
	TEXT("WorldSettings"),
	TEXT("GameModeBase"),
	TEXT("GameMode"),
	TEXT("GameStateBase"),
	TEXT("GameState"),
	TEXT("PlayerState"),
	TEXT("GameSession"),
	TEXT("HUD"),
	TEXT("GameNetworkManager"),
	TEXT("AtmosphericFog"),
	TEXT("SkyAtmosphere"),
	TEXT("SkyLight"),
	TEXT("VolumetricCloud"),
	TEXT("ExponentialHeightFog"),
	TEXT("Note"),
	TEXT("GroupActor"),
	TEXT("LevelBounds"),
	TEXT("LevelScriptActor"),
	TEXT("DefaultPhysicsVolume"),
	TEXT("Brush"),
	TEXT("WorldPartitionReplay"),
};

static const TSet<FString> GLightClasses = {
	TEXT("DirectionalLight"),
	TEXT("PointLight"),
	TEXT("SpotLight"),
	TEXT("RectLight"),
};

FString FLevelQueryHelper::ClassifyNoise(const FString& ClassName)
{
	for (const FString& Prefix : GNoisePrefixes)
	{
		if (ClassName.StartsWith(Prefix))
		{
			return Prefix;
		}
	}

	if (GNoiseExact.Contains(ClassName))
	{
		return ClassName;
	}

	if (GLightClasses.Contains(ClassName))
	{
		return TEXT("Lights");
	}

	return FString();
}

bool FLevelQueryHelper::IsInfrastructureActor(AActor* Actor, bool bIncludeLights)
{
	if (!Actor)
	{
		return true;
	}

	FString ClassName = Actor->GetClass()->GetName();

	for (const FString& Prefix : GNoisePrefixes)
	{
		if (ClassName.StartsWith(Prefix))
		{
			return true;
		}
	}

	if (GNoiseExact.Contains(ClassName))
	{
		return true;
	}

	if (!bIncludeLights && GLightClasses.Contains(ClassName))
	{
		return true;
	}

	return false;
}

TSharedPtr<FJsonObject> FLevelQueryHelper::BuildActorSummary(AActor* Actor)
{
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

	if (!Actor)
	{
		return Info;
	}

	Info->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Info->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Info->SetObjectField(TEXT("location"), UnrealClaudeJsonUtils::VectorToJson(Actor->GetActorLocation()));

	return Info;
}

// ============================================================================
// List
// ============================================================================

TSharedPtr<FJsonObject> FLevelQueryHelper::ListGameplayActors(UWorld* World)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No world"));
		return Result;
	}

	Result->SetStringField(TEXT("map_name"), World->GetName());

	int32 TotalActors = 0;
	TArray<AActor*> GameplayActors;
	TMap<FString, int32> NoiseCounts;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TotalActors++;

		FString ClassName = Actor->GetClass()->GetName();
		FString NoiseCategory = ClassifyNoise(ClassName);

		if (!NoiseCategory.IsEmpty())
		{
			NoiseCounts.FindOrAdd(NoiseCategory)++;
		}
		else
		{
			GameplayActors.Add(Actor);
		}
	}

	Result->SetNumberField(TEXT("total_actors"), TotalActors);
	Result->SetNumberField(TEXT("gameplay_actors"), GameplayActors.Num());
	Result->SetNumberField(TEXT("filtered_count"), TotalActors - GameplayActors.Num());

	// Group by class
	TMap<FString, TArray<AActor*>> ByClass;
	for (AActor* Actor : GameplayActors)
	{
		FString ClassName = Actor->GetClass()->GetName();
		ByClass.FindOrAdd(ClassName).Add(Actor);
	}

	// Sort class names
	TArray<FString> ClassNames;
	ByClass.GetKeys(ClassNames);
	ClassNames.Sort();

	TSharedPtr<FJsonObject> ByClassJson = MakeShared<FJsonObject>();
	for (const FString& ClassName : ClassNames)
	{
		TArray<AActor*>& Actors = ByClass[ClassName];

		// Sort actors by label
		Actors.Sort([](const AActor& A, const AActor& B)
		{
			return A.GetActorLabel() < B.GetActorLabel();
		});

		TArray<TSharedPtr<FJsonValue>> ActorArray;
		for (AActor* Actor : Actors)
		{
			ActorArray.Add(MakeShared<FJsonValueObject>(BuildActorSummary(Actor)));
		}
		ByClassJson->SetArrayField(ClassName, ActorArray);
	}
	Result->SetObjectField(TEXT("by_class"), ByClassJson);

	// Filtered summary
	if (NoiseCounts.Num() > 0)
	{
		TSharedPtr<FJsonObject> FilteredJson = MakeShared<FJsonObject>();
		for (const auto& Pair : NoiseCounts)
		{
			FilteredJson->SetNumberField(Pair.Key, Pair.Value);
		}
		Result->SetObjectField(TEXT("filtered_categories"), FilteredJson);
	}

	return Result;
}

// ============================================================================
// Find
// ============================================================================

TSharedPtr<FJsonObject> FLevelQueryHelper::FindActors(UWorld* World, const FString& Pattern)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No world"));
		return Result;
	}

	Result->SetStringField(TEXT("map_name"), World->GetName());
	Result->SetStringField(TEXT("pattern"), Pattern);

	FString PatLower = Pattern.ToLower();
	TArray<AActor*> Matches;
	int32 TotalActors = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TotalActors++;

		// Skip infrastructure but include lights in find
		if (IsInfrastructureActor(Actor, /*bIncludeLights=*/ true))
		{
			continue;
		}

		FString Label = Actor->GetActorLabel();
		FString ClassName = Actor->GetClass()->GetName();

		if (Label.ToLower().Contains(PatLower) || ClassName.ToLower().Contains(PatLower))
		{
			Matches.Add(Actor);
		}
	}

	// Sort matches by label
	Matches.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetActorLabel() < B.GetActorLabel();
	});

	Result->SetNumberField(TEXT("total_actors"), TotalActors);
	Result->SetNumberField(TEXT("match_count"), Matches.Num());

	TArray<TSharedPtr<FJsonValue>> MatchArray;
	for (AActor* Actor : Matches)
	{
		TSharedPtr<FJsonObject> ActorJson = MakeShared<FJsonObject>();
		ActorJson->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorJson->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ActorJson->SetObjectField(TEXT("location"), UnrealClaudeJsonUtils::VectorToJson(Actor->GetActorLocation()));
		ActorJson->SetObjectField(TEXT("rotation"), UnrealClaudeJsonUtils::RotatorToJson(Actor->GetActorRotation()));
		ActorJson->SetObjectField(TEXT("scale"), UnrealClaudeJsonUtils::VectorToJson(Actor->GetActorScale3D()));

		FString FolderPath = Actor->GetFolderPath().ToString();
		if (!FolderPath.IsEmpty())
		{
			ActorJson->SetStringField(TEXT("folder"), FolderPath);
		}

		MatchArray.Add(MakeShared<FJsonValueObject>(ActorJson));
	}
	Result->SetArrayField(TEXT("matches"), MatchArray);

	return Result;
}

// ============================================================================
// Info
// ============================================================================

TSharedPtr<FJsonObject> FLevelQueryHelper::InspectActor(UWorld* World, const FString& ActorLabel)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No world"));
		return Result;
	}

	Result->SetStringField(TEXT("map_name"), World->GetName());

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

		// Provide suggestions
		FString PatLower = ActorLabel.ToLower();
		TArray<TSharedPtr<FJsonValue>> Suggestions;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			FString Label = (*It)->GetActorLabel();
			if (Label.ToLower().Contains(PatLower) && Suggestions.Num() < 10)
			{
				Suggestions.Add(MakeShared<FJsonValueString>(Label));
			}
		}
		if (Suggestions.Num() > 0)
		{
			Result->SetArrayField(TEXT("suggestions"), Suggestions);
		}
		return Result;
	}

	// Basic info
	Result->SetStringField(TEXT("label"), FoundActor->GetActorLabel());
	Result->SetStringField(TEXT("class"), FoundActor->GetClass()->GetName());
	Result->SetStringField(TEXT("name"), FoundActor->GetName());

	// Blueprint path (if it's a Blueprint-generated class)
	FString ClassName = FoundActor->GetClass()->GetName();
	if (ClassName.EndsWith(TEXT("_C")))
	{
		UObject* Outer = FoundActor->GetClass()->GetOuter();
		if (Outer)
		{
			Result->SetStringField(TEXT("blueprint_path"), Outer->GetPathName());
		}
	}

	// Transform
	TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
	Transform->SetObjectField(TEXT("location"), UnrealClaudeJsonUtils::VectorToJson(FoundActor->GetActorLocation()));
	Transform->SetObjectField(TEXT("rotation"), UnrealClaudeJsonUtils::RotatorToJson(FoundActor->GetActorRotation()));
	Transform->SetObjectField(TEXT("scale"), UnrealClaudeJsonUtils::VectorToJson(FoundActor->GetActorScale3D()));
	Result->SetObjectField(TEXT("transform"), Transform);

	// All components
	TInlineComponentArray<UActorComponent*> AllComponents;
	FoundActor->GetComponents(AllComponents);

	TArray<UPrimitiveComponent*> PrimitiveComps;
	FoundActor->GetComponents<UPrimitiveComponent>(PrimitiveComps);

	TSet<FName> PrimitiveNames;
	for (UPrimitiveComponent* PC : PrimitiveComps)
	{
		PrimitiveNames.Add(PC->GetFName());
	}

	TArray<TSharedPtr<FJsonValue>> ComponentArray;
	for (UActorComponent* Comp : AllComponents)
	{
		TSharedPtr<FJsonObject> CompJson = MakeShared<FJsonObject>();
		CompJson->SetStringField(TEXT("name"), Comp->GetName());
		CompJson->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		if (PrimitiveNames.Contains(Comp->GetFName()))
		{
			CompJson->SetBoolField(TEXT("is_primitive"), true);
		}
		ComponentArray.Add(MakeShared<FJsonValueObject>(CompJson));
	}
	Result->SetNumberField(TEXT("component_count"), AllComponents.Num());
	Result->SetArrayField(TEXT("components"), ComponentArray);

	// Collision data for primitive components
	if (PrimitiveComps.Num() > 0)
	{
		TSharedPtr<FJsonObject> CollisionData = FComponentInspector::SerializeCollisionForActor(FoundActor);
		Result->SetObjectField(TEXT("collision"), CollisionData);
	}

	return Result;
}
