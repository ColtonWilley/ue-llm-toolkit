// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_AnimBlueprintModify.h"
#include "AnimationBlueprintUtils.h"
#include "AnimGraphEditor.h"
#include "AnimLayerEditor.h"
#include "AnimStateMachineEditor.h"
#include "AnimAssetManager.h"
#include "AnimAssetNodeFactory.h"
#include "GraphLayoutHelper.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Serialization/JsonSerializer.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "ControlRig.h"
#include "Animation/AimOffsetBlendSpace.h"

FMCPToolResult FMCPTool_AnimBlueprintModify::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Extract required parameters
	FString BlueprintPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("blueprint_path"), BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	// Validate blueprint path for security (block engine paths, path traversal, etc.)
	if (!ValidateBlueprintPathParam(BlueprintPath, Error))
	{
		return Error.GetValue();
	}

	FString Operation;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	// Route to appropriate handler
	if (Operation == TEXT("get_info"))
	{
		return HandleGetInfo(BlueprintPath);
	}
	else if (Operation == TEXT("get_state_machine"))
	{
		return HandleGetStateMachine(BlueprintPath, Params);
	}
	else if (Operation == TEXT("create_state_machine"))
	{
		return HandleCreateStateMachine(BlueprintPath, Params);
	}
	else if (Operation == TEXT("add_state"))
	{
		return HandleAddState(BlueprintPath, Params);
	}
	else if (Operation == TEXT("remove_state"))
	{
		return HandleRemoveState(BlueprintPath, Params);
	}
	else if (Operation == TEXT("set_entry_state"))
	{
		return HandleSetEntryState(BlueprintPath, Params);
	}
	else if (Operation == TEXT("add_transition"))
	{
		return HandleAddTransition(BlueprintPath, Params);
	}
	else if (Operation == TEXT("remove_transition"))
	{
		return HandleRemoveTransition(BlueprintPath, Params);
	}
	else if (Operation == TEXT("set_transition_duration"))
	{
		return HandleSetTransitionDuration(BlueprintPath, Params);
	}
	else if (Operation == TEXT("set_transition_priority"))
	{
		return HandleSetTransitionPriority(BlueprintPath, Params);
	}
	else if (Operation == TEXT("add_condition_node"))
	{
		return HandleAddConditionNode(BlueprintPath, Params);
	}
	else if (Operation == TEXT("delete_condition_node"))
	{
		return HandleDeleteConditionNode(BlueprintPath, Params);
	}
	else if (Operation == TEXT("connect_condition_nodes"))
	{
		return HandleConnectConditionNodes(BlueprintPath, Params);
	}
	else if (Operation == TEXT("connect_to_result"))
	{
		return HandleConnectToResult(BlueprintPath, Params);
	}
	else if (Operation == TEXT("connect_state_machine_to_output"))
	{
		return HandleConnectStateMachineToOutput(BlueprintPath, Params);
	}
	else if (Operation == TEXT("set_state_animation"))
	{
		return HandleSetStateAnimation(BlueprintPath, Params);
	}
	else if (Operation == TEXT("add_anim_node"))
	{
		return HandleAddAnimNode(BlueprintPath, Params);
	}
	else if (Operation == TEXT("delete_anim_node"))
	{
		return HandleDeleteAnimNode(BlueprintPath, Params);
	}
	else if (Operation == TEXT("find_animations"))
	{
		return HandleFindAnimations(BlueprintPath, Params);
	}
	else if (Operation == TEXT("batch"))
	{
		return HandleBatch(BlueprintPath, Params);
	}
	// NEW operations for enhanced pin/node introspection
	else if (Operation == TEXT("get_transition_nodes"))
	{
		return HandleGetTransitionNodes(BlueprintPath, Params);
	}
	else if (Operation == TEXT("inspect_node_pins"))
	{
		return HandleInspectNodePins(BlueprintPath, Params);
	}
	else if (Operation == TEXT("set_pin_default_value"))
	{
		return HandleSetPinDefaultValue(BlueprintPath, Params);
	}
	else if (Operation == TEXT("add_comparison_chain"))
	{
		return HandleAddComparisonChain(BlueprintPath, Params);
	}
	else if (Operation == TEXT("validate_blueprint"))
	{
		return HandleValidateBlueprint(BlueprintPath);
	}
	else if (Operation == TEXT("get_state_machine_diagram"))
	{
		return HandleGetStateMachineDiagram(BlueprintPath, Params);
	}
	else if (Operation == TEXT("get_anim_node_property"))
	{
		return HandleGetAnimNodeProperty(BlueprintPath, Params);
	}
	else if (Operation == TEXT("set_anim_node_property"))
	{
		return HandleSetAnimNodeProperty(BlueprintPath, Params);
	}
	// Bulk operation for setting up multiple transition conditions
	else if (Operation == TEXT("setup_transition_conditions"))
	{
		return HandleSetupTransitionConditions(BlueprintPath, Params);
	}
	// Animation Layer Interface operations
	else if (Operation == TEXT("list_layer_interfaces"))
	{
		return HandleListLayerInterfaces(BlueprintPath);
	}
	else if (Operation == TEXT("list_layers"))
	{
		return HandleListLayers(BlueprintPath);
	}
	else if (Operation == TEXT("list_linked_layer_nodes"))
	{
		return HandleListLinkedLayerNodes(BlueprintPath);
	}
	else if (Operation == TEXT("add_layer_interface"))
	{
		return HandleAddLayerInterface(BlueprintPath, Params);
	}
	else if (Operation == TEXT("add_linked_layer_node"))
	{
		return HandleAddLinkedLayerNode(BlueprintPath, Params);
	}
	else if (Operation == TEXT("set_layer_instance"))
	{
		return HandleSetLayerInstance(BlueprintPath, Params);
	}
	else if (Operation == TEXT("connect_anim_nodes"))
	{
		return HandleConnectAnimNodes(BlueprintPath, Params);
	}
	else if (Operation == TEXT("bind_variable"))
	{
		return HandleBindVariable(BlueprintPath, Params);
	}
	else if (Operation == TEXT("inspect_layer_graph"))
	{
		return HandleInspectLayerGraph(BlueprintPath, Params);
	}
	else if (Operation == TEXT("layout_graph"))
	{
		return HandleLayoutGraph(BlueprintPath, Params);
	}

	return FMCPToolResult::Error(FString::Printf(TEXT("Unknown operation: %s"), *Operation));
}

FVector2D FMCPTool_AnimBlueprintModify::ExtractPosition(const TSharedRef<FJsonObject>& Params)
{
	FVector2D Position(0, 0);
	const TSharedPtr<FJsonObject>* PosObj;
	if (Params->TryGetObjectField(TEXT("position"), PosObj) && PosObj && (*PosObj).IsValid())
	{
		Position.X = (*PosObj)->GetNumberField(TEXT("x"));
		Position.Y = (*PosObj)->GetNumberField(TEXT("y"));
	}
	return Position;
}

TOptional<FMCPToolResult> FMCPTool_AnimBlueprintModify::LoadAnimBlueprintOrError(
	const FString& Path,
	UAnimBlueprint*& OutBP)
{
	FString Error;
	OutBP = FAnimationBlueprintUtils::LoadAnimBlueprint(Path, Error);
	if (!OutBP)
	{
		return FMCPToolResult::Error(Error);
	}
	return TOptional<FMCPToolResult>();
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleGetInfo(const FString& BlueprintPath)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FAnimationBlueprintUtils::SerializeAnimBlueprintInfo(AnimBP);
	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleGetStateMachine(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	if (StateMachineName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine parameter required"));
	}

	TSharedPtr<FJsonObject> Result = FAnimationBlueprintUtils::SerializeStateMachineInfo(AnimBP, StateMachineName);
	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleCreateStateMachine(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString MachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	if (MachineName.IsEmpty())
	{
		MachineName = TEXT("Locomotion");
	}

	FString TargetGraphName = ExtractOptionalString(Params, TEXT("target_graph"));
	FVector2D Position = ExtractPosition(Params);
	FString NodeId, Error;

	UAnimGraphNode_StateMachine* SM = nullptr;

	if (TargetGraphName.IsEmpty())
	{
		SM = FAnimationBlueprintUtils::CreateStateMachine(
			AnimBP, MachineName, Position, NodeId, Error);
	}
	else
	{
		UEdGraph* TargetGraph = FAnimLayerEditor::FindLayerFunctionGraph(AnimBP, TargetGraphName, Error);
		if (!TargetGraph)
		{
			return FMCPToolResult::Error(Error);
		}

		SM = FAnimStateMachineEditor::CreateStateMachine(
			AnimBP, TargetGraph, MachineName, Position, NodeId, Error);
	}

	if (!SM)
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("name"), MachineName);
	if (!TargetGraphName.IsEmpty())
	{
		Result->SetStringField(TEXT("target_graph"), TargetGraphName);
	}
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created state machine '%s'%s"),
		*MachineName,
		TargetGraphName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" in layer graph '%s'"), *TargetGraphName)));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleAddState(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));

	if (StateMachineName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine parameter required"));
	}
	if (StateName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_name parameter required"));
	}

	FVector2D Position = ExtractPosition(Params);
	bool bIsEntry = ExtractOptionalBool(Params, TEXT("is_entry_state"), false);
	FString NodeId, Error;

	UAnimStateNode* State = FAnimationBlueprintUtils::AddState(
		AnimBP, StateMachineName, StateName, Position, bIsEntry, NodeId, Error);

	if (!State)
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetBoolField(TEXT("is_entry_state"), bIsEntry);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added state '%s' to '%s'"), *StateName, *StateMachineName));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleRemoveState(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));

	if (StateMachineName.IsEmpty() || StateName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine and state_name parameters required"));
	}

	FString Error;
	if (!FAnimationBlueprintUtils::RemoveState(AnimBP, StateMachineName, StateName, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Removed state '%s' from '%s'"), *StateName, *StateMachineName));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleSetEntryState(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));

	if (StateMachineName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine parameter required"));
	}
	if (StateName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_name parameter required"));
	}

	FString Error;
	if (!FAnimationBlueprintUtils::SetEntryState(AnimBP, StateMachineName, StateName, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_machine"), StateMachineName);
	Result->SetStringField(TEXT("entry_state"), StateName);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Set '%s' as entry state for '%s'"), *StateName, *StateMachineName));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleAddTransition(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, and to_state parameters required"));
	}

	FString NodeId, Error;
	UAnimStateTransitionNode* Transition = FAnimationBlueprintUtils::CreateTransition(
		AnimBP, StateMachineName, FromState, ToState, NodeId, Error);

	if (!Transition)
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("from_state"), FromState);
	Result->SetStringField(TEXT("to_state"), ToState);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created transition '%s' -> '%s'"), *FromState, *ToState));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleRemoveTransition(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, and to_state parameters required"));
	}

	FString Error;
	if (!FAnimationBlueprintUtils::RemoveTransition(AnimBP, StateMachineName, FromState, ToState, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Removed transition '%s' -> '%s'"), *FromState, *ToState));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleSetTransitionDuration(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	float Duration = ExtractOptionalNumber<float>(Params, TEXT("duration"), 0.2f);

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, and to_state parameters required"));
	}

	FString Error;
	if (!FAnimationBlueprintUtils::SetTransitionDuration(AnimBP, StateMachineName, FromState, ToState, Duration, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("duration"), Duration);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Set transition duration to %.2fs"), Duration));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleSetTransitionPriority(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	int32 Priority = ExtractOptionalNumber<int32>(Params, TEXT("priority"), 1);

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, and to_state parameters required"));
	}

	FString Error;
	if (!FAnimationBlueprintUtils::SetTransitionPriority(AnimBP, StateMachineName, FromState, ToState, Priority, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("priority"), Priority);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Set transition priority to %d"), Priority));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleAddConditionNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	FString NodeType = ExtractOptionalString(Params, TEXT("node_type"));

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty() || NodeType.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, to_state, and node_type parameters required"));
	}

	// Get node params
	TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* ParamsObj;
	if (Params->TryGetObjectField(TEXT("node_params"), ParamsObj))
	{
		NodeParams = *ParamsObj;
	}

	FVector2D Position = ExtractPosition(Params);
	FString NodeId, Error;

	UEdGraphNode* Node = FAnimationBlueprintUtils::AddConditionNode(
		AnimBP, StateMachineName, FromState, ToState,
		NodeType, NodeParams, Position, NodeId, Error);

	if (!Node)
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("node_type"), NodeType);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added %s condition node"), *NodeType));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleDeleteConditionNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	FString NodeId = ExtractOptionalString(Params, TEXT("node_id"));

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty() || NodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, to_state, and node_id parameters required"));
	}

	FString Error;
	if (!FAnimationBlueprintUtils::DeleteConditionNode(
		AnimBP, StateMachineName, FromState, ToState, NodeId, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("deleted_node_id"), NodeId);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Deleted condition node %s"), *NodeId));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleConnectConditionNodes(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	FString SourceNodeId = ExtractOptionalString(Params, TEXT("source_node_id"));
	FString SourcePin = ExtractOptionalString(Params, TEXT("source_pin"));
	FString TargetNodeId = ExtractOptionalString(Params, TEXT("target_node_id"));
	FString TargetPin = ExtractOptionalString(Params, TEXT("target_pin"));

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty() ||
		SourceNodeId.IsEmpty() || TargetNodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, to_state, source_node_id, and target_node_id required"));
	}

	FString Error;
	if (!FAnimationBlueprintUtils::ConnectConditionNodes(
		AnimBP, StateMachineName, FromState, ToState,
		SourceNodeId, SourcePin, TargetNodeId, TargetPin, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Connected %s -> %s"), *SourceNodeId, *TargetNodeId));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleConnectToResult(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	FString ConditionNodeId = ExtractOptionalString(Params, TEXT("source_node_id"));
	FString ConditionPin = ExtractOptionalString(Params, TEXT("source_pin"), TEXT("Result"));

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty() || ConditionNodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, to_state, and source_node_id required"));
	}

	FString Error;
	if (!FAnimationBlueprintUtils::ConnectToTransitionResult(
		AnimBP, StateMachineName, FromState, ToState,
		ConditionNodeId, ConditionPin, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("Connected condition to transition result"));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleConnectStateMachineToOutput(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	if (StateMachineName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine parameter required"));
	}

	FString TargetGraph = ExtractOptionalString(Params, TEXT("target_graph"));

	FString Error;
	if (!FAnimGraphEditor::ConnectStateMachineToAnimGraphRoot(AnimBP, StateMachineName, Error, TargetGraph))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_machine"), StateMachineName);
	if (!TargetGraph.IsEmpty())
	{
		Result->SetStringField(TEXT("target_graph"), TargetGraph);
	}
	FString GraphLabel = TargetGraph.IsEmpty() ? TEXT("AnimGraph") : TargetGraph;
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Connected State Machine '%s' to '%s' Output Pose"), *StateMachineName, *GraphLabel));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleSetStateAnimation(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));
	FString AnimType = ExtractOptionalString(Params, TEXT("animation_type"), TEXT("sequence"));
	FString AnimPath = ExtractOptionalString(Params, TEXT("animation_path"));

	if (StateMachineName.IsEmpty() || StateName.IsEmpty() || AnimPath.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, state_name, and animation_path required"));
	}

	FString Error;
	bool bSuccess = false;

	if (AnimType == TEXT("sequence"))
	{
		bSuccess = FAnimationBlueprintUtils::SetStateAnimSequence(
			AnimBP, StateMachineName, StateName, AnimPath, Error);
	}
	else if (AnimType == TEXT("blendspace"))
	{
		TMap<FString, FString> Bindings;
		const TSharedPtr<FJsonObject>* BindingsObj;
		if (Params->TryGetObjectField(TEXT("parameter_bindings"), BindingsObj))
		{
			for (const auto& Pair : (*BindingsObj)->Values)
			{
				Bindings.Add(Pair.Key, Pair.Value->AsString());
			}
		}
		bSuccess = FAnimationBlueprintUtils::SetStateBlendSpace(
			AnimBP, StateMachineName, StateName, AnimPath, Bindings, Error);
	}
	else if (AnimType == TEXT("blendspace1d"))
	{
		FString Binding = ExtractOptionalString(Params, TEXT("parameter_bindings"));
		// Also try getting from object if it was passed that way
		if (Binding.IsEmpty())
		{
			const TSharedPtr<FJsonObject>* BindingsObj;
			if (Params->TryGetObjectField(TEXT("parameter_bindings"), BindingsObj))
			{
				// Get first value as the binding
				for (const auto& Pair : (*BindingsObj)->Values)
				{
					Binding = Pair.Value->AsString();
					break;
				}
			}
		}
		bSuccess = FAnimationBlueprintUtils::SetStateBlendSpace1D(
			AnimBP, StateMachineName, StateName, AnimPath, Binding, Error);
	}
	else if (AnimType == TEXT("montage"))
	{
		bSuccess = FAnimationBlueprintUtils::SetStateMontage(
			AnimBP, StateMachineName, StateName, AnimPath, Error);
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Unknown animation type: %s"), *AnimType));
	}

	if (!bSuccess)
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetStringField(TEXT("animation_type"), AnimType);
	Result->SetStringField(TEXT("animation_path"), AnimPath);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Set %s animation for state '%s'"), *AnimType, *StateName));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleFindAnimations(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	FString Error;
	UAnimBlueprint* AnimBP = nullptr;

	// AnimBlueprint is optional for this operation - used for skeleton filtering
	if (!BlueprintPath.IsEmpty() && BlueprintPath != TEXT("*"))
	{
		AnimBP = FAnimationBlueprintUtils::LoadAnimBlueprint(BlueprintPath, Error);
	}

	FString SearchPattern = ExtractOptionalString(Params, TEXT("search_pattern"), TEXT("*"));
	FString AssetType = ExtractOptionalString(Params, TEXT("asset_type"), TEXT("All"));

	TArray<FString> Assets = FAnimationBlueprintUtils::FindAnimationAssets(
		SearchPattern, AssetType, AnimBP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Assets.Num());
	Result->SetArrayField(TEXT("assets"), StringArrayToJsonArray(Assets));

	if (AnimBP)
	{
		Result->SetStringField(TEXT("skeleton_filter"), AnimBP->GetName());
	}

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleBatch(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	const TArray<TSharedPtr<FJsonValue>>* Operations;
	if (!Params->TryGetArrayField(TEXT("operations"), Operations))
	{
		return FMCPToolResult::Error(TEXT("operations array required for batch mode"));
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimationBlueprintUtils::ExecuteBatchOperations(
		AnimBP, *Operations, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		// Still return the partial results with the error
		return FMCPToolResult::Success(TEXT("Batch operation completed with errors"), Result);
	}

	return FMCPToolResult::Success(TEXT("Batch operation completed successfully"), Result);
}

// ===== NEW Handlers for Enhanced Operations =====

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleGetTransitionNodes(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));

	if (StateMachineName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine parameter required"));
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimationBlueprintUtils::GetTransitionNodes(
		AnimBP, StateMachineName, FromState, ToState, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleInspectNodePins(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));
	FString NodeId = ExtractOptionalString(Params, TEXT("node_id"));

	if (StateMachineName.IsEmpty() || NodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine and node_id parameters required"));
	}

	bool bUseStateBound = !StateName.IsEmpty();
	bool bUseTransition = !FromState.IsEmpty() && !ToState.IsEmpty();

	if (!bUseStateBound && !bUseTransition)
	{
		return FMCPToolResult::Error(TEXT("Either state_name OR (from_state + to_state) required"));
	}

	FString Error;
	UEdGraph* Graph = nullptr;

	if (bUseStateBound)
	{
		Graph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, Error);
	}
	else
	{
		Graph = FAnimGraphEditor::FindTransitionGraph(AnimBP, StateMachineName, FromState, ToState, Error);
	}

	if (!Graph)
	{
		return FMCPToolResult::Error(Error);
	}

	UEdGraphNode* Node = FAnimGraphEditor::FindNodeById(Graph, NodeId);
	if (!Node)
	{
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && N->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(NodeId))
			{
				Node = N;
				break;
			}
		}
	}

	if (!Node)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	Result->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	Result->SetNumberField(TEXT("pos_x"), Node->NodePosX);
	Result->SetNumberField(TEXT("pos_y"), Node->NodePosY);

	TArray<TSharedPtr<FJsonValue>> InputPins;
	TArray<TSharedPtr<FJsonValue>> OutputPins;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;

		TSharedPtr<FJsonObject> PinObj = FAnimGraphEditor::SerializeDetailedPinInfo(Pin);

		if (Pin->Direction == EGPD_Input)
		{
			InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		else
		{
			OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
	}

	Result->SetArrayField(TEXT("input_pins"), InputPins);
	Result->SetArrayField(TEXT("output_pins"), OutputPins);

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleSetPinDefaultValue(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));
	FString NodeId = ExtractOptionalString(Params, TEXT("node_id"));
	FString PinName = ExtractOptionalString(Params, TEXT("pin_name"));
	FString PinValue = ExtractOptionalString(Params, TEXT("pin_value"));

	if (StateMachineName.IsEmpty() || NodeId.IsEmpty() || PinName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, node_id, and pin_name parameters required"));
	}

	bool bUseStateBound = !StateName.IsEmpty();
	bool bUseTransition = !FromState.IsEmpty() && !ToState.IsEmpty();

	if (!bUseStateBound && !bUseTransition)
	{
		return FMCPToolResult::Error(TEXT("Either state_name OR (from_state + to_state) required"));
	}

	FString Error;

	if (bUseStateBound)
	{
		UEdGraph* Graph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, Error);
		if (!Graph)
		{
			return FMCPToolResult::Error(Error);
		}

		UEdGraphNode* Node = FAnimGraphEditor::FindNodeById(Graph, NodeId);
		if (!Node)
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (N && N->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(NodeId))
				{
					Node = N;
					break;
				}
			}
		}
		if (!Node)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));
		}

		UEdGraphPin* Pin = nullptr;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P && P->Direction == EGPD_Input && P->PinName.ToString() == PinName)
			{
				Pin = P;
				break;
			}
		}
		if (!Pin)
		{
			FString AvailablePins;
			for (UEdGraphPin* P : Node->Pins)
			{
				if (P && P->Direction == EGPD_Input)
				{
					AvailablePins += FString::Printf(TEXT("[%s] "), *P->PinName.ToString());
				}
			}
			return FMCPToolResult::Error(FString::Printf(TEXT("Input pin '%s' not found. Available: %s"),
				*PinName, AvailablePins.IsEmpty() ? TEXT("none") : *AvailablePins));
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema)
		{
			Schema->TrySetDefaultValue(*Pin, PinValue);
		}
		else
		{
			Pin->DefaultValue = PinValue;
		}

		Graph->Modify();
		FAnimationBlueprintUtils::MarkAnimBlueprintModified(AnimBP);
	}
	else
	{
		if (!FAnimationBlueprintUtils::SetPinDefaultValue(
			AnimBP, StateMachineName, FromState, ToState, NodeId, PinName, PinValue, Error))
		{
			return FMCPToolResult::Error(Error);
		}
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("pin_name"), PinName);
	Result->SetStringField(TEXT("value"), PinValue);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Set pin '%s' value to '%s'"), *PinName, *PinValue));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleAddComparisonChain(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString FromState = ExtractOptionalString(Params, TEXT("from_state"));
	FString ToState = ExtractOptionalString(Params, TEXT("to_state"));
	FString VariableName = ExtractOptionalString(Params, TEXT("variable_name"));
	FString ComparisonType = ExtractOptionalString(Params, TEXT("comparison_type"), TEXT("Less"));
	FString CompareValue = ExtractOptionalString(Params, TEXT("compare_value"));
	FVector2D Position = ExtractPosition(Params);

	if (StateMachineName.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty() || VariableName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine, from_state, to_state, and variable_name parameters required"));
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimationBlueprintUtils::AddComparisonChain(
		AnimBP, StateMachineName, FromState, ToState,
		VariableName, ComparisonType, CompareValue, Position, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	return FMCPToolResult::Success(TEXT("Comparison chain created successfully"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleValidateBlueprint(const FString& BlueprintPath)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimationBlueprintUtils::ValidateBlueprint(AnimBP, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	FString Message = Result->GetBoolField(TEXT("is_valid"))
		? TEXT("Blueprint is valid")
		: FString::Printf(TEXT("Blueprint has %d error(s), %d warning(s)"),
			static_cast<int32>(Result->GetNumberField(TEXT("error_count"))),
			static_cast<int32>(Result->GetNumberField(TEXT("warning_count"))));

	return FMCPToolResult::Success(Message, Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleGetStateMachineDiagram(
	const FString& BlueprintPath,
	const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = Params->GetStringField(TEXT("state_machine"));
	if (StateMachineName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine parameter required"));
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimationBlueprintUtils::GetStateMachineDiagram(AnimBP, StateMachineName, Error);

	if (!Result.IsValid() || !Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Error.IsEmpty() ? TEXT("Failed to generate diagram") : Error);
	}

	// Return ASCII diagram in message for easy viewing, with full JSON data
	FString AsciiDiagram = Result->GetStringField(TEXT("ascii_diagram"));
	return FMCPToolResult::Success(AsciiDiagram, Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleGetAnimNodeProperty(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString NodeId = ExtractOptionalString(Params, TEXT("node_id"));
	if (NodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("node_id parameter required"));
	}

	FString PropertyName = ExtractOptionalString(Params, TEXT("property_name"));
	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));
	FString TargetGraphName = ExtractOptionalString(Params, TEXT("target_graph"));

	FString Error;
	UEdGraph* Graph = nullptr;
	if (!StateMachineName.IsEmpty() && !StateName.IsEmpty())
	{
		Graph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, Error);
	}
	else if (!TargetGraphName.IsEmpty())
	{
		Graph = FAnimLayerEditor::FindLayerFunctionGraph(AnimBP, TargetGraphName, Error);
	}
	else
	{
		Graph = FAnimGraphEditor::FindAnimGraph(AnimBP, Error);
	}

	if (!Graph)
	{
		return FMCPToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result;
	if (!FAnimGraphEditor::GetAnimNodeProperty(Graph, NodeId, PropertyName, Result, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	Result->SetBoolField(TEXT("success"), true);
	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleSetAnimNodeProperty(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString NodeId = ExtractOptionalString(Params, TEXT("node_id"));
	FString PropertyName = ExtractOptionalString(Params, TEXT("property_name"));
	FString PropertyValue = ExtractOptionalString(Params, TEXT("property_value"));

	if (NodeId.IsEmpty() || PropertyName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("node_id and property_name parameters required"));
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));
	FString TargetGraphName = ExtractOptionalString(Params, TEXT("target_graph"));

	FString Error;
	UEdGraph* Graph = nullptr;
	if (!StateMachineName.IsEmpty() && !StateName.IsEmpty())
	{
		Graph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, Error);
	}
	else if (!TargetGraphName.IsEmpty())
	{
		Graph = FAnimLayerEditor::FindLayerFunctionGraph(AnimBP, TargetGraphName, Error);
	}
	else
	{
		Graph = FAnimGraphEditor::FindAnimGraph(AnimBP, Error);
	}

	if (!Graph)
	{
		return FMCPToolResult::Error(Error);
	}

	if (!FAnimGraphEditor::SetAnimNodeProperty(Graph, NodeId, PropertyName, PropertyValue, Error))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("property_value"), PropertyValue);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Set %s = %s on node %s"), *PropertyName, *PropertyValue, *NodeId));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleSetupTransitionConditions(
	const FString& BlueprintPath,
	const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	// Extract required parameters
	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	if (StateMachineName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("state_machine parameter required"));
	}

	// Get rules array
	const TArray<TSharedPtr<FJsonValue>>* RulesArray;
	if (!Params->TryGetArrayField(TEXT("rules"), RulesArray))
	{
		return FMCPToolResult::Error(TEXT("rules array required for setup_transition_conditions"));
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimationBlueprintUtils::SetupTransitionConditions(
		AnimBP, StateMachineName, *RulesArray, Error);

	if (!Result.IsValid())
	{
		return FMCPToolResult::Error(Error.IsEmpty() ? TEXT("Failed to setup transition conditions") : Error);
	}

	if (!Result->GetBoolField(TEXT("success")))
	{
		FString ErrorMsg = Result->HasField(TEXT("error"))
			? Result->GetStringField(TEXT("error"))
			: TEXT("Unknown error setting up transition conditions");
		// Return partial results with error
		return FMCPToolResult::Success(ErrorMsg, Result);
	}

	// Build success message with statistics
	int32 RulesProcessed = static_cast<int32>(Result->GetNumberField(TEXT("rules_processed")));
	int32 TransitionsModified = static_cast<int32>(Result->GetNumberField(TEXT("transitions_modified")));

	FString Message = FString::Printf(
		TEXT("Setup transition conditions: %d rules processed, %d transitions modified"),
		RulesProcessed, TransitionsModified);

	return FMCPToolResult::Success(Message, Result);
}

// ===== Animation Layer Interface Handlers =====

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleListLayerInterfaces(const FString& BlueprintPath)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimLayerEditor::GetImplementedLayerInterfaces(AnimBP, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleListLayers(const FString& BlueprintPath)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimLayerEditor::GetAvailableLayers(AnimBP, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleListLinkedLayerNodes(const FString& BlueprintPath)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimLayerEditor::GetLinkedLayerNodes(AnimBP, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleAddLayerInterface(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString InterfacePath = ExtractOptionalString(Params, TEXT("interface_path"));
	if (InterfacePath.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("interface_path parameter required"));
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimLayerEditor::AddLayerInterface(AnimBP, InterfacePath, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(Result->GetStringField(TEXT("message")), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleAddLinkedLayerNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString LayerName = ExtractOptionalString(Params, TEXT("layer_name"));
	if (LayerName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("layer_name parameter required"));
	}

	FVector2D Position = ExtractPosition(Params);
	FString InstanceClass = ExtractOptionalString(Params, TEXT("instance_class"));

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimLayerEditor::CreateLinkedLayerNode(
		AnimBP, LayerName, Position, InstanceClass, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(Result->GetStringField(TEXT("message")), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleSetLayerInstance(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString NodeId = ExtractOptionalString(Params, TEXT("node_id"));
	FString InstanceClass = ExtractOptionalString(Params, TEXT("instance_class"));

	if (NodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("node_id parameter required"));
	}
	if (InstanceClass.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("instance_class parameter required"));
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimLayerEditor::SetLinkedLayerInstance(
		AnimBP, NodeId, InstanceClass, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(Result->GetStringField(TEXT("message")), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleConnectAnimNodes(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString SourceNodeId = ExtractOptionalString(Params, TEXT("source_node_id"));
	FString TargetNodeId = ExtractOptionalString(Params, TEXT("target_node_id"));
	FString TargetGraphName = ExtractOptionalString(Params, TEXT("target_graph"));
	FString SourcePinName = ExtractOptionalString(Params, TEXT("source_pin"));
	FString TargetPinName = ExtractOptionalString(Params, TEXT("target_pin"));
	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));

	if (SourceNodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("source_node_id parameter required"));
	}
	if (TargetNodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("target_node_id parameter required"));
	}

	FString Error;
	if (!FAnimGraphEditor::ConnectAnimNodes(AnimBP, SourceNodeId, TargetNodeId, TargetGraphName, Error, SourcePinName, TargetPinName, StateMachineName, StateName))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("source_node_id"), SourceNodeId);
	Result->SetStringField(TEXT("target_node_id"), TargetNodeId);
	if (!TargetGraphName.IsEmpty())
	{
		Result->SetStringField(TEXT("target_graph"), TargetGraphName);
	}
	if (!StateMachineName.IsEmpty())
	{
		Result->SetStringField(TEXT("state_machine"), StateMachineName);
		Result->SetStringField(TEXT("state_name"), StateName);
	}
	if (!SourcePinName.IsEmpty())
	{
		Result->SetStringField(TEXT("source_pin"), SourcePinName);
	}
	if (!TargetPinName.IsEmpty())
	{
		Result->SetStringField(TEXT("target_pin"), TargetPinName);
	}

	FString GraphContext;
	if (!StateMachineName.IsEmpty())
	{
		GraphContext = FString::Printf(TEXT(" in state '%s' of '%s'"), *StateName, *StateMachineName);
	}
	else if (!TargetGraphName.IsEmpty())
	{
		GraphContext = FString::Printf(TEXT(" in graph '%s'"), *TargetGraphName);
	}
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Connected '%s' -> '%s'%s"),
		*SourceNodeId, *TargetNodeId, *GraphContext));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleBindVariable(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString VariableName = ExtractOptionalString(Params, TEXT("variable_name"));
	FString TargetNodeId = ExtractOptionalString(Params, TEXT("target_node_id"));
	FString TargetPinName = ExtractOptionalString(Params, TEXT("target_pin"));
	FString TargetGraphName = ExtractOptionalString(Params, TEXT("target_graph"));
	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));

	if (VariableName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("variable_name parameter required"));
	}
	if (TargetNodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("target_node_id parameter required"));
	}
	if (TargetPinName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("target_pin parameter required"));
	}

	FString OutNodeId, Error;
	if (!FAnimGraphEditor::BindVariable(AnimBP, VariableName, TargetNodeId, TargetPinName, TargetGraphName, OutNodeId, Error, StateMachineName, StateName))
	{
		return FMCPToolResult::Error(Error);
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("variable_node_id"), OutNodeId);
	Result->SetStringField(TEXT("variable_name"), VariableName);
	Result->SetStringField(TEXT("target_node_id"), TargetNodeId);
	Result->SetStringField(TEXT("target_pin"), TargetPinName);
	if (!TargetGraphName.IsEmpty())
	{
		Result->SetStringField(TEXT("target_graph"), TargetGraphName);
	}
	if (!StateMachineName.IsEmpty())
	{
		Result->SetStringField(TEXT("state_machine"), StateMachineName);
		Result->SetStringField(TEXT("state_name"), StateName);
	}

	FString GraphContext;
	if (!StateMachineName.IsEmpty())
	{
		GraphContext = FString::Printf(TEXT(" in state '%s' of '%s'"), *StateName, *StateMachineName);
	}
	else if (!TargetGraphName.IsEmpty())
	{
		GraphContext = FString::Printf(TEXT(" in graph '%s'"), *TargetGraphName);
	}
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Bound variable '%s' to pin '%s' on node '%s'%s"),
		*VariableName, *TargetPinName, *TargetNodeId, *GraphContext));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleInspectLayerGraph(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString TargetGraphName = ExtractOptionalString(Params, TEXT("target_graph"));
	if (TargetGraphName.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("target_graph parameter required"));
	}

	FString Error;
	TSharedPtr<FJsonObject> Result = FAnimLayerEditor::InspectLayerGraph(AnimBP, TargetGraphName, Error);

	if (!Result->GetBoolField(TEXT("success")))
	{
		return FMCPToolResult::Error(Result->GetStringField(TEXT("error")));
	}

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleAddAnimNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString AnimType = ExtractOptionalString(Params, TEXT("animation_type"));
	FString AnimPath = ExtractOptionalString(Params, TEXT("animation_path"));
	FString TargetGraphName = ExtractOptionalString(Params, TEXT("target_graph"));
	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));
	bool bAutoConnect = ExtractOptionalBool(Params, TEXT("auto_connect"), false);
	FVector2D Position = ExtractPosition(Params);

	if (AnimType.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("animation_type parameter required (sequence, blendspace, blendspace1d, slot, inertialization, dead_blending, copy_pose_from_mesh, modify_bone, two_bone_ik, control_rig, layered_blend_per_bone, aim_offset, local_to_component, component_to_local)"));
	}
	if (AnimPath.IsEmpty() && AnimType != TEXT("slot")
		&& AnimType != TEXT("inertialization") && AnimType != TEXT("dead_blending")
		&& AnimType != TEXT("copy_pose_from_mesh")
		&& AnimType != TEXT("modify_bone")
		&& AnimType != TEXT("two_bone_ik")
		&& AnimType != TEXT("control_rig")
		&& AnimType != TEXT("layered_blend_per_bone")
		&& AnimType != TEXT("local_to_component")
		&& AnimType != TEXT("component_to_local"))
	{
		return FMCPToolResult::Error(TEXT("animation_path parameter required"));
	}

	FString Error;

	UEdGraph* TargetGraph = nullptr;
	FString GraphContext;

	if (!StateMachineName.IsEmpty() && !StateName.IsEmpty())
	{
		TargetGraph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, Error);
		GraphContext = FString::Printf(TEXT("state '%s' in '%s'"), *StateName, *StateMachineName);
	}
	else if (!TargetGraphName.IsEmpty())
	{
		TargetGraph = FAnimLayerEditor::FindLayerFunctionGraph(AnimBP, TargetGraphName, Error);
		GraphContext = FString::Printf(TEXT("layer graph '%s'"), *TargetGraphName);
	}
	else
	{
		TargetGraph = FAnimGraphEditor::FindAnimGraph(AnimBP, Error);
		GraphContext = TEXT("AnimGraph");
	}

	if (!TargetGraph)
	{
		return FMCPToolResult::Error(Error);
	}

	UEdGraphNode* CreatedNode = nullptr;
	FString NodeId;
	FString NodeClass;

	if (AnimType == TEXT("sequence"))
	{
		UAnimSequence* AnimSeq = FAnimAssetManager::LoadAnimSequence(AnimPath, Error);
		if (!AnimSeq)
		{
			return FMCPToolResult::Error(Error);
		}
		if (!FAnimAssetManager::ValidateAnimationCompatibility(AnimBP, AnimSeq, Error))
		{
			return FMCPToolResult::Error(Error);
		}
		CreatedNode = FAnimAssetNodeFactory::CreateAnimSequenceNode(TargetGraph, AnimSeq, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_SequencePlayer");
	}
	else if (AnimType == TEXT("blendspace"))
	{
		UBlendSpace* BS = FAnimAssetManager::LoadBlendSpace(AnimPath, Error);
		if (!BS)
		{
			return FMCPToolResult::Error(Error);
		}
		if (!FAnimAssetManager::ValidateAnimationCompatibility(AnimBP, BS, Error))
		{
			return FMCPToolResult::Error(Error);
		}
		CreatedNode = FAnimAssetNodeFactory::CreateBlendSpaceNode(TargetGraph, BS, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_BlendSpacePlayer");
	}
	else if (AnimType == TEXT("blendspace1d"))
	{
		UBlendSpace1D* BS1D = FAnimAssetManager::LoadBlendSpace1D(AnimPath, Error);
		if (!BS1D)
		{
			return FMCPToolResult::Error(Error);
		}
		if (!FAnimAssetManager::ValidateAnimationCompatibility(AnimBP, BS1D, Error))
		{
			return FMCPToolResult::Error(Error);
		}
		CreatedNode = FAnimAssetNodeFactory::CreateBlendSpace1DNode(TargetGraph, BS1D, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_BlendSpacePlayer");
	}
	else if (AnimType == TEXT("slot"))
	{
		FString SlotName = ExtractOptionalString(Params, TEXT("slot_name"));
		if (SlotName.IsEmpty()) SlotName = TEXT("DefaultSlot");
		CreatedNode = FAnimAssetNodeFactory::CreateSlotNode(TargetGraph, FName(*SlotName), Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_Slot");
	}
	else if (AnimType == TEXT("inertialization"))
	{
		CreatedNode = FAnimAssetNodeFactory::CreateInertializationNode(TargetGraph, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_Inertialization");
	}
	else if (AnimType == TEXT("dead_blending"))
	{
		CreatedNode = FAnimAssetNodeFactory::CreateDeadBlendingNode(TargetGraph, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_DeadBlending");
	}
	else if (AnimType == TEXT("copy_pose_from_mesh"))
	{
		bool bUseAttachedParent = ExtractOptionalBool(Params, TEXT("use_attached_parent"), true);
		CreatedNode = FAnimAssetNodeFactory::CreateCopyPoseFromMeshNode(TargetGraph, bUseAttachedParent, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_CopyPoseFromMesh");
	}
	else if (AnimType == TEXT("modify_bone"))
	{
		FString BoneName = ExtractOptionalString(Params, TEXT("bone_name"));
		if (BoneName.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("bone_name parameter required for modify_bone"));
		}

		// Parse rotation (default: zero)
		FRotator Rotation = FRotator::ZeroRotator;
		if (const TSharedPtr<FJsonObject>* RotObj; Params->TryGetObjectField(TEXT("rotation"), RotObj))
		{
			Rotation.Pitch = (*RotObj)->GetNumberField(TEXT("pitch"));
			Rotation.Yaw = (*RotObj)->GetNumberField(TEXT("yaw"));
			Rotation.Roll = (*RotObj)->GetNumberField(TEXT("roll"));
		}

		// Parse translation (default: zero)
		FVector Translation = FVector::ZeroVector;
		if (const TSharedPtr<FJsonObject>* TransObj; Params->TryGetObjectField(TEXT("translation"), TransObj))
		{
			Translation.X = (*TransObj)->GetNumberField(TEXT("x"));
			Translation.Y = (*TransObj)->GetNumberField(TEXT("y"));
			Translation.Z = (*TransObj)->GetNumberField(TEXT("z"));
		}

		// Parse rotation_mode (default: additive)
		FString RotModeStr = ExtractOptionalString(Params, TEXT("rotation_mode"), TEXT("additive"));
		EBoneModificationMode RotMode = BMM_Additive;
		if (RotModeStr == TEXT("replace")) RotMode = BMM_Replace;
		else if (RotModeStr == TEXT("ignore")) RotMode = BMM_Ignore;

		// Translation mode: additive if translation specified, else ignore
		EBoneModificationMode TransMode = BMM_Ignore;
		if (Params->HasField(TEXT("translation")))
		{
			FString TransModeStr = ExtractOptionalString(Params, TEXT("translation_mode"), TEXT("additive"));
			if (TransModeStr == TEXT("replace")) TransMode = BMM_Replace;
			else TransMode = BMM_Additive;
		}

		// Parse coordinate spaces (default: bone space)
		auto ParseSpace = [](const FString& Str) -> EBoneControlSpace {
			if (Str == TEXT("component")) return BCS_ComponentSpace;
			if (Str == TEXT("parent")) return BCS_ParentBoneSpace;
			if (Str == TEXT("world")) return BCS_WorldSpace;
			return BCS_BoneSpace;
		};

		EBoneControlSpace RotSpace = ParseSpace(ExtractOptionalString(Params, TEXT("rotation_space"), TEXT("bone")));
		EBoneControlSpace TransSpace = ParseSpace(ExtractOptionalString(Params, TEXT("translation_space"), TEXT("bone")));

		CreatedNode = FAnimAssetNodeFactory::CreateModifyBoneNode(
			TargetGraph, FName(*BoneName), Rotation, Translation,
			RotMode, TransMode, RotSpace, TransSpace, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_ModifyBone");
	}
	else if (AnimType == TEXT("control_rig"))
	{
		FString ControlRigPath = ExtractOptionalString(Params, TEXT("control_rig_class"));
		if (ControlRigPath.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("control_rig_class parameter required for control_rig type"));
		}

		UClass* RigClass = LoadObject<UClass>(nullptr, *ControlRigPath);
		if (!RigClass || !RigClass->IsChildOf(UControlRig::StaticClass()))
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Invalid control_rig_class: '%s' — must be a UControlRig subclass"), *ControlRigPath));
		}

		CreatedNode = FAnimAssetNodeFactory::CreateControlRigNode(
			TargetGraph, RigClass, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_ControlRig");
	}
	else if (AnimType == TEXT("layered_blend_per_bone"))
	{
		FString BoneName = ExtractOptionalString(Params, TEXT("bone_name"));
		if (BoneName.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("bone_name parameter required for layered_blend_per_bone"));
		}
		int32 BlendDepth = ExtractOptionalNumber<int32>(Params, TEXT("blend_depth"), 0);
		bool bMeshSpaceRotationBlend = ExtractOptionalBool(Params, TEXT("mesh_space_rotation_blend"), false);

		CreatedNode = FAnimAssetNodeFactory::CreateLayeredBoneBlendNode(
			TargetGraph, FName(*BoneName), BlendDepth, bMeshSpaceRotationBlend,
			Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_LayeredBoneBlend");
	}
	else if (AnimType == TEXT("two_bone_ik"))
	{
		FString BoneName = ExtractOptionalString(Params, TEXT("bone_name"));
		if (BoneName.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("bone_name parameter required for two_bone_ik"));
		}
		FString EffectorBone = ExtractOptionalString(Params, TEXT("effector_bone"));
		if (EffectorBone.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("effector_bone parameter required for two_bone_ik"));
		}

		EBoneControlSpace EffectorSpace = BCS_BoneSpace;
		FString SpaceStr = ExtractOptionalString(Params, TEXT("effector_space"));
		if (SpaceStr == TEXT("ComponentSpace")) EffectorSpace = BCS_ComponentSpace;
		else if (SpaceStr == TEXT("WorldSpace")) EffectorSpace = BCS_WorldSpace;
		else if (SpaceStr == TEXT("ParentBoneSpace")) EffectorSpace = BCS_ParentBoneSpace;

		FString JointBone = ExtractOptionalString(Params, TEXT("joint_target_bone"));
		bool bAllowStretching = ExtractOptionalBool(Params, TEXT("allow_stretching"), false);

		CreatedNode = FAnimAssetNodeFactory::CreateTwoBoneIKNode(
			TargetGraph, FName(*BoneName), FName(*EffectorBone),
			EffectorSpace, FName(*JointBone), bAllowStretching,
			Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_TwoBoneIK");
	}
	else if (AnimType == TEXT("aim_offset"))
	{
		UAimOffsetBlendSpace* AO = FAnimAssetManager::LoadAimOffset(AnimPath, Error);
		if (!AO)
		{
			return FMCPToolResult::Error(Error);
		}
		if (!FAnimAssetManager::ValidateAnimationCompatibility(AnimBP, AO, Error))
		{
			return FMCPToolResult::Error(Error);
		}
		CreatedNode = FAnimAssetNodeFactory::CreateAimOffsetNode(
			TargetGraph, AO, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_RotationOffsetBlendSpace");
	}
	else if (AnimType == TEXT("local_to_component"))
	{
		CreatedNode = FAnimAssetNodeFactory::CreateLocalToComponentNode(
			TargetGraph, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_LocalToComponentSpace");
	}
	else if (AnimType == TEXT("component_to_local"))
	{
		CreatedNode = FAnimAssetNodeFactory::CreateComponentToLocalNode(
			TargetGraph, Position, NodeId, Error);
		NodeClass = TEXT("AnimGraphNode_ComponentToLocalSpace");
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown animation_type: '%s'. Expected: sequence, blendspace, blendspace1d, slot, inertialization, dead_blending, copy_pose_from_mesh, modify_bone, two_bone_ik, control_rig, layered_blend_per_bone, aim_offset, local_to_component, component_to_local"), *AnimType));
	}

	if (!CreatedNode)
	{
		return FMCPToolResult::Error(Error);
	}

	bool bConnected = false;
	if (bAutoConnect)
	{
		bConnected = FAnimAssetNodeFactory::ConnectToOutputPose(TargetGraph, NodeId, Error);
		if (!bConnected)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Node created (id=%s) but auto_connect failed: %s"), *NodeId, *Error));
		}
	}

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("node_class"), NodeClass);
	Result->SetStringField(TEXT("animation_type"), AnimType);
	Result->SetStringField(TEXT("animation_path"), AnimPath);
	if (!TargetGraphName.IsEmpty())
	{
		Result->SetStringField(TEXT("target_graph"), TargetGraphName);
	}
	if (!StateMachineName.IsEmpty())
	{
		Result->SetStringField(TEXT("state_machine"), StateMachineName);
		Result->SetStringField(TEXT("state_name"), StateName);
	}
	Result->SetBoolField(TEXT("auto_connected"), bConnected);

	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : CreatedNode->Pins)
	{
		if (!Pin) continue;
		PinsArray.Add(MakeShared<FJsonValueObject>(FAnimGraphEditor::SerializeDetailedPinInfo(Pin)));
	}
	Result->SetArrayField(TEXT("pins"), PinsArray);

	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Created %s node in %s"), *AnimType, *GraphContext));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleDeleteAnimNode(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString NodeId = ExtractOptionalString(Params, TEXT("node_id"));
	if (NodeId.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("node_id parameter required"));
	}

	FString TargetGraphName = ExtractOptionalString(Params, TEXT("target_graph"));
	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));
	FString Error;

	UEdGraph* TargetGraph = nullptr;
	FString GraphContext;

	if (!StateMachineName.IsEmpty() && !StateName.IsEmpty())
	{
		TargetGraph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, Error);
		GraphContext = FString::Printf(TEXT("state '%s' in '%s'"), *StateName, *StateMachineName);
	}
	else if (!TargetGraphName.IsEmpty())
	{
		TargetGraph = FAnimLayerEditor::FindLayerFunctionGraph(AnimBP, TargetGraphName, Error);
		GraphContext = FString::Printf(TEXT("layer graph '%s'"), *TargetGraphName);
	}
	else
	{
		TargetGraph = FAnimGraphEditor::FindAnimGraph(AnimBP, Error);
		GraphContext = TEXT("AnimGraph");
	}

	if (!TargetGraph)
	{
		return FMCPToolResult::Error(Error);
	}

	UEdGraphNode* NodeToDelete = FAnimGraphEditor::FindNodeById(TargetGraph, NodeId);
	if (!NodeToDelete)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Node '%s' not found in %s"), *NodeId, *GraphContext));
	}

	if (NodeToDelete->IsA<UAnimGraphNode_Root>() ||
		NodeToDelete->IsA<UAnimGraphNode_StateResult>() ||
		NodeToDelete->IsA<UAnimGraphNode_TransitionResult>())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Cannot delete protected node '%s' (type: %s)"),
			*NodeId, *NodeToDelete->GetClass()->GetName()));
	}

	NodeToDelete->BreakAllNodeLinks();
	TargetGraph->RemoveNode(NodeToDelete);
	TargetGraph->Modify();

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("deleted_node_id"), NodeId);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Deleted node '%s' from %s"), *NodeId, *GraphContext));

	return FMCPToolResult::Success(TEXT("Operation completed"), Result);
}

FMCPToolResult FMCPTool_AnimBlueprintModify::HandleLayoutGraph(const FString& BlueprintPath, const TSharedRef<FJsonObject>& Params)
{
	UAnimBlueprint* AnimBP = nullptr;
	if (auto ErrorResult = LoadAnimBlueprintOrError(BlueprintPath, AnimBP))
	{
		return ErrorResult.GetValue();
	}

	FString StateMachineName = ExtractOptionalString(Params, TEXT("state_machine"));
	FString StateName = ExtractOptionalString(Params, TEXT("state_name"));
	FString Error;

	UEdGraph* TargetGraph = nullptr;
	FString GraphContext;

	if (!StateMachineName.IsEmpty() && !StateName.IsEmpty())
	{
		TargetGraph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, Error);
		GraphContext = FString::Printf(TEXT("state '%s' in '%s'"), *StateName, *StateMachineName);
	}
	else
	{
		TargetGraph = FAnimGraphEditor::FindAnimGraph(AnimBP, Error);
		GraphContext = TEXT("AnimGraph");
	}

	if (!TargetGraph)
	{
		return FMCPToolResult::Error(Error);
	}

	FGraphLayoutConfig Config;
	Config.SpacingX = ExtractOptionalNumber<float>(Params, TEXT("spacing_x"), 400.0f);
	Config.SpacingY = ExtractOptionalNumber<float>(Params, TEXT("spacing_y"), 200.0f);
	Config.bPreserveExisting = ExtractOptionalBool(Params, TEXT("preserve_existing"), false);
	Config.bReverseDepth = true;

	FGraphLayoutResult LayoutResult = FGraphLayoutHelper::LayoutGraph(
		TargetGraph, Config,
		FGraphLayoutHelper::MakeAnimPosePolicy(),
		FGraphLayoutHelper::MakeAnimEntryFinder(),
		FGraphLayoutHelper::MakeDataConsumerFinder()
	);

	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("graph_name"), GraphContext);
	Result->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	Result->SetNumberField(TEXT("total_nodes"), LayoutResult.TotalNodes);
	Result->SetNumberField(TEXT("layout_nodes"), LayoutResult.LayoutNodes);
	Result->SetNumberField(TEXT("skipped_nodes"), LayoutResult.SkippedNodes);
	Result->SetNumberField(TEXT("disconnected_nodes"), LayoutResult.DisconnectedNodes);
	Result->SetNumberField(TEXT("data_only_nodes"), LayoutResult.DataOnlyNodes);
	Result->SetNumberField(TEXT("entry_points"), LayoutResult.EntryPoints);
	Result->SetBoolField(TEXT("compiled"), true);
	Result->SetStringField(TEXT("compile_status"), Error.IsEmpty() ? TEXT("OK") : Error);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Laid out %d/%d nodes in %s (%d entry points, %d disconnected, %d data-only)"),
			LayoutResult.LayoutNodes, LayoutResult.TotalNodes, *GraphContext,
			LayoutResult.EntryPoints, LayoutResult.DisconnectedNodes, LayoutResult.DataOnlyNodes),
		Result);
}
