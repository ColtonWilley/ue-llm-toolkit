// Copyright Natali Caggiano. All Rights Reserved.

#include "ComponentInspector.h"
#include "UnrealClaudeUtils.h"
#include "PropertySerializer.h"

#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/AudioComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/CollisionProfile.h"
#include "UObject/UnrealType.h"

// ============================================================================
// Component Tree
// ============================================================================

TSharedPtr<FJsonObject> FComponentInspector::SerializeComponentTree(UBlueprint* Blueprint)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!Blueprint)
	{
		return Result;
	}

	Result->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
	}

	if (!Blueprint->GeneratedClass)
	{
		Result->SetNumberField(TEXT("total_components"), 0);
		Result->SetArrayField(TEXT("components"), {});
		return Result;
	}

	// Collect SCS variable names for origin tagging
	TSet<FName> SCSVarNames;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			SCSVarNames.Add(Node->GetVariableName());
		}
	}

	// Walk CDO for native (C++ constructor) components
	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{
		Result->SetNumberField(TEXT("total_components"), 0);
		Result->SetArrayField(TEXT("components"), {});
		return Result;
	}

	TInlineComponentArray<UActorComponent*> AllComponents;
	CDO->GetComponents(AllComponents);

	// Track which component names the CDO already has
	TSet<FName> CDOComponentNames;
	for (UActorComponent* Comp : AllComponents)
	{
		CDOComponentNames.Add(Comp->GetFName());
	}

	// Separate scene vs non-scene from CDO
	USceneComponent* RootComp = CDO->GetRootComponent();
	TArray<USceneComponent*> SceneComponents;
	TArray<UActorComponent*> NonSceneComponents;
	for (UActorComponent* Comp : AllComponents)
	{
		if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
		{
			SceneComponents.Add(SceneComp);
		}
		else
		{
			NonSceneComponents.Add(Comp);
		}
	}

	// CDOs don't populate AttachChildren — build parent-child map from GetAttachParent()
	TMap<USceneComponent*, TArray<USceneComponent*>> ChildrenMap;
	for (USceneComponent* Comp : SceneComponents)
	{
		if (Comp != RootComp)
		{
			USceneComponent* Parent = Comp->GetAttachParent();
			if (!Parent)
			{
				Parent = RootComp;
			}
			ChildrenMap.FindOrAdd(Parent).Add(Comp);
		}
	}

	// Build native scene component tree
	TArray<TSharedPtr<FJsonValue>> ComponentArray;
	if (RootComp)
	{
		ComponentArray.Add(MakeShared<FJsonValueObject>(
			SerializeSceneComponentNode(RootComp, SCSVarNames, ChildrenMap)));
	}

	// Non-scene components from CDO
	TArray<TSharedPtr<FJsonValue>> NonSceneArray;
	for (UActorComponent* Comp : NonSceneComponents)
	{
		TSharedPtr<FJsonObject> CompJson = MakeShared<FJsonObject>();
		CompJson->SetStringField(TEXT("name"), Comp->GetName());
		CompJson->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		CompJson->SetStringField(TEXT("origin"),
			SCSVarNames.Contains(Comp->GetFName()) ? TEXT("blueprint") : TEXT("cpp"));

		TSharedPtr<FJsonObject> Props = GetComponentProperties(Comp);
		if (Props.IsValid() && Props->Values.Num() > 0)
		{
			CompJson->SetObjectField(TEXT("properties"), Props);
		}

		NonSceneArray.Add(MakeShared<FJsonValueObject>(CompJson));
	}

	// ----------------------------------------------------------------
	// SCS pass: inject BP-added components not already in CDO
	// ----------------------------------------------------------------
	int32 SCSInjectedCount = 0;
	if (Blueprint->SimpleConstructionScript)
	{
		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;

		for (USCS_Node* RootNode : SCS->GetRootNodes())
		{
			if (!RootNode || CDOComponentNames.Contains(RootNode->GetVariableName()))
			{
				continue;
			}

			TSharedPtr<FJsonObject> SCSJson = SerializeSCSNodeForTree(RootNode, CDOComponentNames, SCSInjectedCount);

			// Find the native parent component to inject under
			FString ParentName;
			if (RootNode->bIsParentComponentNative)
			{
				ParentName = RootNode->ParentComponentOrVariableName.ToString();
			}

			if (!ParentName.IsEmpty())
			{
				InjectIntoJsonTree(ComponentArray, ParentName, SCSJson);
			}
			else
			{
				ComponentArray.Add(MakeShared<FJsonValueObject>(SCSJson));
			}
		}

		// Also check for SCS non-scene components not in CDO
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node || CDOComponentNames.Contains(Node->GetVariableName()))
			{
				continue;
			}
			if (!Node->ComponentTemplate || Node->ComponentTemplate->IsA<USceneComponent>())
			{
				continue;
			}

			TSharedPtr<FJsonObject> CompJson = MakeShared<FJsonObject>();
			CompJson->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompJson->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
			CompJson->SetStringField(TEXT("origin"), TEXT("blueprint"));

			TSharedPtr<FJsonObject> Props = GetComponentProperties(Node->ComponentTemplate);
			if (Props.IsValid() && Props->Values.Num() > 0)
			{
				CompJson->SetObjectField(TEXT("properties"), Props);
			}

			NonSceneArray.Add(MakeShared<FJsonValueObject>(CompJson));
			SCSInjectedCount++;
		}
	}

	int32 TotalCount = AllComponents.Num() + SCSInjectedCount;
	Result->SetNumberField(TEXT("total_components"), TotalCount);
	Result->SetArrayField(TEXT("components"), ComponentArray);
	if (NonSceneArray.Num() > 0)
	{
		Result->SetArrayField(TEXT("non_scene_components"), NonSceneArray);
	}

	return Result;
}

TSharedPtr<FJsonObject> FComponentInspector::SerializeSCSNode(
	USCS_Node* Node,
	const TSet<FName>& ThisBlueprintVarNames)
{
	TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();

	if (!Node)
	{
		return NodeJson;
	}

	FName VarName = Node->GetVariableName();
	NodeJson->SetStringField(TEXT("name"), VarName.ToString());

	UActorComponent* Template = Node->ComponentTemplate;
	if (Template)
	{
		NodeJson->SetStringField(TEXT("class"), Template->GetClass()->GetName());
	}
	else if (Node->ComponentClass)
	{
		NodeJson->SetStringField(TEXT("class"), Node->ComponentClass->GetName());
	}

	// Tags
	TArray<TSharedPtr<FJsonValue>> Tags;
	if (!ThisBlueprintVarNames.Contains(VarName))
	{
		Tags.Add(MakeShared<FJsonValueString>(TEXT("inherited")));
	}
	// Root check: it's the root if it has a native parent or has children
	if (Node->bIsParentComponentNative || Node->ChildNodes.Num() > 0)
	{
		// More reliable: check if any root node matches
	}
	if (!Tags.IsEmpty())
	{
		NodeJson->SetArrayField(TEXT("tags"), Tags);
	}

	// Properties
	if (Template)
	{
		TSharedPtr<FJsonObject> Props = GetComponentProperties(Template);
		if (Props.IsValid() && Props->Values.Num() > 0)
		{
			NodeJson->SetObjectField(TEXT("properties"), Props);
		}
	}

	// Children
	if (Node->ChildNodes.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		for (USCS_Node* Child : Node->ChildNodes)
		{
			Children.Add(MakeShared<FJsonValueObject>(
				SerializeSCSNode(Child, ThisBlueprintVarNames)));
		}
		NodeJson->SetArrayField(TEXT("children"), Children);
	}

	return NodeJson;
}

TSharedPtr<FJsonObject> FComponentInspector::SerializeSceneComponentNode(
	USceneComponent* Component,
	const TSet<FName>& SCSVarNames,
	const TMap<USceneComponent*, TArray<USceneComponent*>>& ChildrenMap)
{
	TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();

	if (!Component)
	{
		return NodeJson;
	}

	NodeJson->SetStringField(TEXT("name"), Component->GetName());
	NodeJson->SetStringField(TEXT("class"), Component->GetClass()->GetName());
	NodeJson->SetStringField(TEXT("origin"),
		SCSVarNames.Contains(Component->GetFName()) ? TEXT("blueprint") : TEXT("cpp"));

	TSharedPtr<FJsonObject> Props = GetComponentProperties(Component);
	if (Props.IsValid() && Props->Values.Num() > 0)
	{
		NodeJson->SetObjectField(TEXT("properties"), Props);
	}

	// Recurse into children using pre-built map (CDOs don't populate AttachChildren)
	if (const TArray<USceneComponent*>* ChildComps = ChildrenMap.Find(Component))
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		for (USceneComponent* Child : *ChildComps)
		{
			Children.Add(MakeShared<FJsonValueObject>(
				SerializeSceneComponentNode(Child, SCSVarNames, ChildrenMap)));
		}
		NodeJson->SetArrayField(TEXT("children"), Children);
	}

	return NodeJson;
}

// ============================================================================
// SCS Tree Helpers
// ============================================================================

TSharedPtr<FJsonObject> FComponentInspector::SerializeSCSNodeForTree(
	USCS_Node* Node,
	const TSet<FName>& CDOComponentNames,
	int32& OutInjectedCount)
{
	TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();

	if (!Node)
	{
		return NodeJson;
	}

	NodeJson->SetStringField(TEXT("name"), Node->GetVariableName().ToString());

	if (Node->ComponentTemplate)
	{
		NodeJson->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
	}
	else if (Node->ComponentClass)
	{
		NodeJson->SetStringField(TEXT("class"), Node->ComponentClass->GetName());
	}

	NodeJson->SetStringField(TEXT("origin"), TEXT("blueprint"));

	if (Node->ComponentTemplate)
	{
		TSharedPtr<FJsonObject> Props = GetComponentProperties(Node->ComponentTemplate);
		if (Props.IsValid() && Props->Values.Num() > 0)
		{
			NodeJson->SetObjectField(TEXT("properties"), Props);
		}
	}

	OutInjectedCount++;

	if (Node->ChildNodes.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		for (USCS_Node* Child : Node->ChildNodes)
		{
			if (Child && !CDOComponentNames.Contains(Child->GetVariableName()))
			{
				Children.Add(MakeShared<FJsonValueObject>(
					SerializeSCSNodeForTree(Child, CDOComponentNames, OutInjectedCount)));
			}
		}
		if (Children.Num() > 0)
		{
			NodeJson->SetArrayField(TEXT("children"), Children);
		}
	}

	return NodeJson;
}

bool FComponentInspector::InjectIntoJsonTree(
	TArray<TSharedPtr<FJsonValue>>& Tree,
	const FString& ParentName,
	TSharedPtr<FJsonObject> NodeToInject)
{
	for (TSharedPtr<FJsonValue>& Entry : Tree)
	{
		TSharedPtr<FJsonObject> Obj = Entry->AsObject();
		if (!Obj.IsValid())
		{
			continue;
		}

		FString Name;
		if (Obj->TryGetStringField(TEXT("name"), Name) && Name == ParentName)
		{
			const TArray<TSharedPtr<FJsonValue>>* ExistingChildren = nullptr;
			TArray<TSharedPtr<FJsonValue>> ChildArray;
			if (Obj->TryGetArrayField(TEXT("children"), ExistingChildren))
			{
				ChildArray = *ExistingChildren;
			}
			ChildArray.Add(MakeShared<FJsonValueObject>(NodeToInject));
			Obj->SetArrayField(TEXT("children"), ChildArray);
			return true;
		}

		if (Obj->HasField(TEXT("children")))
		{
			TArray<TSharedPtr<FJsonValue>> ChildArray = Obj->GetArrayField(TEXT("children"));
			if (InjectIntoJsonTree(ChildArray, ParentName, NodeToInject))
			{
				Obj->SetArrayField(TEXT("children"), ChildArray);
				return true;
			}
		}
	}
	return false;
}

// ============================================================================
// Component Properties
// ============================================================================

TSharedPtr<FJsonObject> FComponentInspector::GetComponentProperties(UActorComponent* Component)
{
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

	if (!Component)
	{
		return Props;
	}

	UClass* CompClass = Component->GetClass();

	UActorComponent* DefaultComp = NewObject<UActorComponent>(
		GetTransientPackage(), CompClass, NAME_None, RF_Transient);
	if (!DefaultComp)
	{
		return Props;
	}

	for (TFieldIterator<FProperty> It(CompClass); It; ++It)
	{
		FProperty* Property = *It;

		if (FPropertySerializer::ShouldSkipProperty(Property))
		{
			continue;
		}

		const void* CompValuePtr = Property->ContainerPtrToValuePtr<void>(Component);
		const void* DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(DefaultComp);

		if (Property->Identical(CompValuePtr, DefaultValuePtr))
		{
			continue;
		}

		TSharedPtr<FJsonValue> JsonVal = FPropertySerializer::PropertyToJsonValue(Property, CompValuePtr);
		if (JsonVal.IsValid())
		{
			Props->SetField(Property->GetName(), JsonVal);
		}
	}

	DefaultComp->MarkAsGarbage();

	return Props;
}

// ============================================================================
// Single Component
// ============================================================================

TSharedPtr<FJsonObject> FComponentInspector::SerializeSingleComponent(UBlueprint* Blueprint, const FString& ComponentName)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("Invalid Blueprint or no generated class"));
		return Result;
	}

	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not get CDO"));
		return Result;
	}

	TSet<FName> SCSVarNames;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			SCSVarNames.Add(Node->GetVariableName());
		}
	}

	TInlineComponentArray<UActorComponent*> AllComponents;
	CDO->GetComponents(AllComponents);

	// Build SCS variable name → CDO component mapping
	TMap<FName, UActorComponent*> SCSVarToComp;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			FName VarName = Node->GetVariableName();
			for (UActorComponent* Comp : AllComponents)
			{
				if (Comp->GetFName() == VarName)
				{
					SCSVarToComp.Add(VarName, Comp);
					break;
				}
			}
		}
	}

	// Search CDO components first
	UActorComponent* Found = nullptr;
	FName SearchName(*ComponentName);
	for (UActorComponent* Comp : AllComponents)
	{
		if (Comp->GetFName() == SearchName)
		{
			Found = Comp;
			break;
		}
	}

	// If not in CDO, search SCS templates (BP-added components)
	USCS_Node* FoundSCSNode = nullptr;
	if (!Found && Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName() == SearchName)
			{
				FoundSCSNode = Node;
				break;
			}
		}
	}

	if (Found)
	{
		Result->SetStringField(TEXT("name"), Found->GetName());
		Result->SetStringField(TEXT("class"), Found->GetClass()->GetName());
		Result->SetStringField(TEXT("origin"),
			SCSVarNames.Contains(Found->GetFName()) ? TEXT("blueprint") : TEXT("cpp"));

		TSharedPtr<FJsonObject> Props = GetComponentProperties(Found);
		if (Props.IsValid() && Props->Values.Num() > 0)
		{
			Result->SetObjectField(TEXT("properties"), Props);
		}
	}
	else if (FoundSCSNode)
	{
		Result->SetStringField(TEXT("name"), FoundSCSNode->GetVariableName().ToString());
		if (FoundSCSNode->ComponentTemplate)
		{
			Result->SetStringField(TEXT("class"), FoundSCSNode->ComponentTemplate->GetClass()->GetName());

			TSharedPtr<FJsonObject> Props = GetComponentProperties(FoundSCSNode->ComponentTemplate);
			if (Props.IsValid() && Props->Values.Num() > 0)
			{
				Result->SetObjectField(TEXT("properties"), Props);
			}
		}
		else if (FoundSCSNode->ComponentClass)
		{
			Result->SetStringField(TEXT("class"), FoundSCSNode->ComponentClass->GetName());
		}
		Result->SetStringField(TEXT("origin"), TEXT("blueprint"));
	}
	else
	{
		TArray<FString> Names;
		for (UActorComponent* Comp : AllComponents)
		{
			Names.Add(Comp->GetName());
		}
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node)
				{
					FString VarName = Node->GetVariableName().ToString();
					if (!Names.Contains(VarName))
					{
						Names.Add(VarName);
					}
				}
			}
		}
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Component '%s' not found. Available: %s"),
			*ComponentName, *FString::Join(Names, TEXT(", "))));
		return Result;
	}

	return Result;
}

// ============================================================================
// Collision
// ============================================================================

TSharedPtr<FJsonObject> FComponentInspector::SerializeCollisionForBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), TEXT("Invalid Blueprint or no generated class"));
		return Err;
	}

	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), TEXT("Could not get CDO"));
		return Err;
	}

	TArray<UPrimitiveComponent*> PrimComps;
	CDO->GetComponents<UPrimitiveComponent>(PrimComps);

	return BuildCollisionResult(PrimComps, TEXT("Blueprint CDO"), Blueprint->GetName());
}

TSharedPtr<FJsonObject> FComponentInspector::SerializeCollisionForActor(AActor* Actor)
{
	if (!Actor)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), TEXT("Null actor"));
		return Err;
	}

	TArray<UPrimitiveComponent*> PrimComps;
	Actor->GetComponents<UPrimitiveComponent>(PrimComps);

	TSharedPtr<FJsonObject> Result = BuildCollisionResult(
		PrimComps, TEXT("Level Actor"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetName());
	return Result;
}

TSharedPtr<FJsonObject> FComponentInspector::BuildCollisionResult(
	const TArray<UPrimitiveComponent*>& Components,
	const FString& Source,
	const FString& SourceName)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("source"), Source);
	Result->SetStringField(TEXT("source_name"), SourceName);
	Result->SetNumberField(TEXT("primitive_component_count"), Components.Num());

	TArray<TSharedPtr<FJsonValue>> CompArray;
	for (UPrimitiveComponent* Comp : Components)
	{
		CompArray.Add(MakeShared<FJsonValueObject>(SerializeCollision(Comp)));
	}
	Result->SetArrayField(TEXT("components"), CompArray);

	return Result;
}

TSharedPtr<FJsonObject> FComponentInspector::SerializeCollision(UPrimitiveComponent* Component)
{
	TSharedPtr<FJsonObject> CompJson = MakeShared<FJsonObject>();

	if (!Component)
	{
		return CompJson;
	}

	CompJson->SetStringField(TEXT("name"), Component->GetName());
	CompJson->SetStringField(TEXT("class"), Component->GetClass()->GetName());

	ECollisionEnabled::Type CollisionEnabled = Component->GetCollisionEnabled();
	CompJson->SetStringField(TEXT("collision_enabled"), CollisionEnabledToString(CollisionEnabled));

	ECollisionChannel ObjectType = Component->GetCollisionObjectType();
	CompJson->SetStringField(TEXT("object_type"), CollisionChannelToString(ObjectType));
	CompJson->SetStringField(TEXT("collision_profile"), Component->GetCollisionProfileName().ToString());
	CompJson->SetBoolField(TEXT("generate_overlap_events"), Component->GetGenerateOverlapEvents());

	if (CollisionEnabled == ECollisionEnabled::NoCollision)
	{
		CompJson->SetStringField(TEXT("note"), TEXT("Collision disabled - channel responses not relevant"));
		return CompJson;
	}

	// Built-in channel responses
	TSharedPtr<FJsonObject> BuiltinResponses = MakeShared<FJsonObject>();
	struct FChannelName { ECollisionChannel Channel; const TCHAR* Name; };
	static const FChannelName BuiltinChannels[] = {
		{ ECC_WorldStatic,    TEXT("WorldStatic") },
		{ ECC_WorldDynamic,   TEXT("WorldDynamic") },
		{ ECC_Pawn,           TEXT("Pawn") },
		{ ECC_Visibility,     TEXT("Visibility") },
		{ ECC_Camera,         TEXT("Camera") },
		{ ECC_PhysicsBody,    TEXT("PhysicsBody") },
		{ ECC_Vehicle,        TEXT("Vehicle") },
		{ ECC_Destructible,   TEXT("Destructible") },
	};

	for (const auto& Ch : BuiltinChannels)
	{
		ECollisionResponse Resp = Component->GetCollisionResponseToChannel(Ch.Channel);
		BuiltinResponses->SetStringField(Ch.Name, CollisionResponseToString(Resp));
	}
	CompJson->SetObjectField(TEXT("builtin_responses"), BuiltinResponses);

	// Custom channel responses — iterate all 32 channels via the response container
	const FBodyInstance& BodyInst = Component->BodyInstance;
	const FCollisionResponse& CollisionResponse = BodyInst.GetCollisionResponse();
	const FCollisionResponseContainer& ResponseContainer = CollisionResponse.GetResponseContainer();

	// Built-in channel indices (ECC_WorldStatic=0 through ECC_Destructible=7)
	static const TSet<int32> BuiltinChannelIndices = { 0, 1, 2, 3, 4, 5, 6, 7 };

	TSharedPtr<FJsonObject> CustomResponses = MakeShared<FJsonObject>();

	// Check custom channels (indices 8-31: ECC_GameTraceChannel1 through ECC_GameTraceChannel18 + engine trace channels)
	const UCollisionProfile* CollisionProfile = UCollisionProfile::Get();
	for (int32 i = 8; i < 32; ++i)
	{
		ECollisionChannel Channel = static_cast<ECollisionChannel>(i);
		ECollisionResponse Response = ResponseContainer.GetResponse(Channel);
		if (Response != ECR_MAX)
		{
			FName ChannelName = CollisionProfile ? CollisionProfile->ReturnChannelNameFromContainerIndex(i) : NAME_None;
			if (ChannelName != NAME_None)
			{
				CustomResponses->SetStringField(ChannelName.ToString(), CollisionResponseToString(Response));
			}
		}
	}

	if (CustomResponses->Values.Num() > 0)
	{
		CompJson->SetObjectField(TEXT("custom_responses"), CustomResponses);
	}

	return CompJson;
}

// ============================================================================
// Enum Helpers
// ============================================================================

FString FComponentInspector::CollisionResponseToString(ECollisionResponse Response)
{
	switch (Response)
	{
	case ECR_Ignore:  return TEXT("Ignore");
	case ECR_Overlap: return TEXT("Overlap");
	case ECR_Block:   return TEXT("Block");
	default:          return TEXT("Unknown");
	}
}

FString FComponentInspector::CollisionEnabledToString(ECollisionEnabled::Type Type)
{
	switch (Type)
	{
	case ECollisionEnabled::NoCollision:      return TEXT("NoCollision");
	case ECollisionEnabled::QueryOnly:        return TEXT("QueryOnly");
	case ECollisionEnabled::PhysicsOnly:      return TEXT("PhysicsOnly");
	case ECollisionEnabled::QueryAndPhysics:  return TEXT("QueryAndPhysics");
	case ECollisionEnabled::ProbeOnly:        return TEXT("ProbeOnly");
	case ECollisionEnabled::QueryAndProbe:    return TEXT("QueryAndProbe");
	default:                                  return TEXT("Unknown");
	}
}

FString FComponentInspector::CollisionChannelToString(ECollisionChannel Channel)
{
	switch (Channel)
	{
	case ECC_WorldStatic:    return TEXT("WorldStatic");
	case ECC_WorldDynamic:   return TEXT("WorldDynamic");
	case ECC_Pawn:           return TEXT("Pawn");
	case ECC_Visibility:     return TEXT("Visibility");
	case ECC_Camera:         return TEXT("Camera");
	case ECC_PhysicsBody:    return TEXT("PhysicsBody");
	case ECC_Vehicle:        return TEXT("Vehicle");
	case ECC_Destructible:   return TEXT("Destructible");
	default:
		// Custom channels: ECC_GameTraceChannel1..18 and ECC_EngineTraceChannel1..6
		return FString::Printf(TEXT("Channel_%d"), static_cast<int32>(Channel));
	}
}
