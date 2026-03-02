// Copyright Natali Caggiano. All Rights Reserved.

#include "ComponentInspector.h"
#include "UnrealClaudeUtils.h"

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

	// Collect SCS variable names for origin tagging (blueprint-added vs C++ constructor)
	TSet<FName> SCSVarNames;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			SCSVarNames.Add(Node->GetVariableName());
		}
	}

	// Always walk CDO — this sees ALL components (C++ constructor + Blueprint-added)
	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{
		Result->SetNumberField(TEXT("total_components"), 0);
		Result->SetArrayField(TEXT("components"), {});
		return Result;
	}

	TInlineComponentArray<UActorComponent*> AllComponents;
	CDO->GetComponents(AllComponents);

	// Separate scene vs non-scene components
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

	// CDOs don't populate AttachChildren, so GetChildrenComponents() returns empty.
	// Build the parent-child map manually from GetAttachParent() pointers.
	TMap<USceneComponent*, TArray<USceneComponent*>> ChildrenMap;
	for (USceneComponent* Comp : SceneComponents)
	{
		if (Comp != RootComp)
		{
			USceneComponent* Parent = Comp->GetAttachParent();
			if (!Parent)
			{
				Parent = RootComp; // Orphans attach to root
			}
			ChildrenMap.FindOrAdd(Parent).Add(Comp);
		}
	}

	// Build scene component tree from root
	TArray<TSharedPtr<FJsonValue>> ComponentArray;
	if (RootComp)
	{
		ComponentArray.Add(MakeShared<FJsonValueObject>(
			SerializeSceneComponentNode(RootComp, SCSVarNames, ChildrenMap)));
	}

	// Collect non-scene components (CharacterMovement, AttributeComponent, etc.)
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

	Result->SetNumberField(TEXT("total_components"), AllComponents.Num());
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
// Component Properties
// ============================================================================

TSharedPtr<FJsonObject> FComponentInspector::GetComponentProperties(UActorComponent* Component)
{
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

	if (!Component)
	{
		return Props;
	}

	// --- SceneComponent properties ---
	if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
	{
		FVector Loc = SceneComp->GetRelativeLocation();
		if (!Loc.IsNearlyZero())
		{
			Props->SetObjectField(TEXT("relative_location"), UnrealClaudeJsonUtils::VectorToJson(Loc));
		}

		FRotator Rot = SceneComp->GetRelativeRotation();
		if (!Rot.IsNearlyZero())
		{
			Props->SetObjectField(TEXT("relative_rotation"), UnrealClaudeJsonUtils::RotatorToJson(Rot));
		}

		FVector Scale = SceneComp->GetRelativeScale3D();
		if (!Scale.Equals(FVector::OneVector))
		{
			Props->SetObjectField(TEXT("relative_scale3d"), UnrealClaudeJsonUtils::VectorToJson(Scale));
		}

		if (!SceneComp->IsVisible())
		{
			Props->SetBoolField(TEXT("is_visible"), false);
		}

		if (SceneComp->Mobility != EComponentMobility::Movable)
		{
			Props->SetStringField(TEXT("mobility"),
				SceneComp->Mobility == EComponentMobility::Static ? TEXT("Static") : TEXT("Stationary"));
		}
	}

	// --- PrimitiveComponent properties ---
	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
	{
		if (PrimComp->IsSimulatingPhysics())
		{
			Props->SetBoolField(TEXT("simulate_physics"), true);
		}

		FName ProfileName = PrimComp->GetCollisionProfileName();
		if (ProfileName != NAME_None)
		{
			Props->SetStringField(TEXT("collision_profile_name"), ProfileName.ToString());
		}

		if (PrimComp->GetGenerateOverlapEvents())
		{
			Props->SetBoolField(TEXT("generate_overlap_events"), true);
		}
	}

	// --- CapsuleComponent ---
	if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Component))
	{
		Props->SetNumberField(TEXT("capsule_half_height"), Capsule->GetUnscaledCapsuleHalfHeight());
		Props->SetNumberField(TEXT("capsule_radius"), Capsule->GetUnscaledCapsuleRadius());
	}

	// --- SphereComponent ---
	if (USphereComponent* Sphere = Cast<USphereComponent>(Component))
	{
		Props->SetNumberField(TEXT("sphere_radius"), Sphere->GetUnscaledSphereRadius());
	}

	// --- BoxComponent ---
	if (UBoxComponent* Box = Cast<UBoxComponent>(Component))
	{
		FVector Extent = Box->GetUnscaledBoxExtent();
		Props->SetObjectField(TEXT("box_extent"), UnrealClaudeJsonUtils::VectorToJson(Extent));
	}

	// --- SkeletalMeshComponent ---
	if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Component))
	{
		if (SkelMesh->GetSkeletalMeshAsset())
		{
			Props->SetStringField(TEXT("skeletal_mesh"), SkelMesh->GetSkeletalMeshAsset()->GetPathName());
		}

		if (SkelMesh->AnimClass)
		{
			Props->SetStringField(TEXT("anim_class"), SkelMesh->AnimClass->GetPathName());
		}
	}

	// --- StaticMeshComponent ---
	if (UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component))
	{
		if (StaticMesh->GetStaticMesh())
		{
			Props->SetStringField(TEXT("static_mesh"), StaticMesh->GetStaticMesh()->GetPathName());
		}
	}

	// --- CharacterMovementComponent ---
	if (UCharacterMovementComponent* MoveComp = Cast<UCharacterMovementComponent>(Component))
	{
		Props->SetNumberField(TEXT("max_walk_speed"), MoveComp->MaxWalkSpeed);
		Props->SetNumberField(TEXT("jump_z_velocity"), MoveComp->JumpZVelocity);

		if (!FMath::IsNearlyEqual(MoveComp->GravityScale, 1.0f))
		{
			Props->SetNumberField(TEXT("gravity_scale"), MoveComp->GravityScale);
		}

		Props->SetNumberField(TEXT("max_acceleration"), MoveComp->MaxAcceleration);
		Props->SetNumberField(TEXT("air_control"), MoveComp->AirControl);
		Props->SetNumberField(TEXT("max_step_height"), MoveComp->MaxStepHeight);
		Props->SetNumberField(TEXT("walkable_floor_angle"), MoveComp->GetWalkableFloorAngle());
	}

	// --- SpringArmComponent ---
	if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(Component))
	{
		Props->SetNumberField(TEXT("target_arm_length"), SpringArm->TargetArmLength);
		Props->SetBoolField(TEXT("use_pawn_control_rotation"), SpringArm->bUsePawnControlRotation);

		FVector SocketOff = SpringArm->SocketOffset;
		if (!SocketOff.IsNearlyZero())
		{
			Props->SetObjectField(TEXT("socket_offset"), UnrealClaudeJsonUtils::VectorToJson(SocketOff));
		}

		FVector TargetOff = SpringArm->TargetOffset;
		if (!TargetOff.IsNearlyZero())
		{
			Props->SetObjectField(TEXT("target_offset"), UnrealClaudeJsonUtils::VectorToJson(TargetOff));
		}

		Props->SetBoolField(TEXT("enable_camera_lag"), SpringArm->bEnableCameraLag);
		if (SpringArm->bEnableCameraLag)
		{
			Props->SetNumberField(TEXT("camera_lag_speed"), SpringArm->CameraLagSpeed);
		}
	}

	// --- CameraComponent ---
	if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
	{
		Props->SetNumberField(TEXT("field_of_view"), Camera->FieldOfView);
	}

	// --- WidgetComponent ---
	if (UWidgetComponent* Widget = Cast<UWidgetComponent>(Component))
	{
		if (Widget->GetWidgetClass())
		{
			Props->SetStringField(TEXT("widget_class"), Widget->GetWidgetClass()->GetName());
		}
		Props->SetStringField(TEXT("space"),
			Widget->GetWidgetSpace() == EWidgetSpace::World ? TEXT("World") : TEXT("Screen"));
		FVector2D DrawSize = Widget->GetDrawSize();
		Props->SetNumberField(TEXT("draw_size_x"), DrawSize.X);
		Props->SetNumberField(TEXT("draw_size_y"), DrawSize.Y);
	}

	return Props;
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
