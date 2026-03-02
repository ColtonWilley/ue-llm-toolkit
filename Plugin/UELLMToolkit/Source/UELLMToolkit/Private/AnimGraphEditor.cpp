// Copyright Natali Caggiano. All Rights Reserved.

/**
 * AnimGraphEditor - Facade for Animation Blueprint graph operations
 *
 * This class delegates to specialized helper classes:
 * - FAnimGraphFinder: Graph finding utilities
 * - FAnimNodePinUtils: Pin finding and connection utilities
 * - FAnimTransitionConditionFactory: Transition condition node creation
 * - FAnimAssetNodeFactory: Animation asset node creation
 *
 * See TECH_DEBT_REMEDIATION.md for details on the refactoring.
 */

#include "AnimGraphEditor.h"
#include "AnimGraphFinder.h"
#include "AnimNodePinUtils.h"
#include "AnimTransitionConditionFactory.h"
#include "AnimAssetNodeFactory.h"
#include "AnimStateMachineEditor.h"
#include "AnimLayerEditor.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateTransitionNode.h"
#include "AnimationGraph.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_VariableGet.h"
#include "HAL/PlatformAtomics.h"

volatile int32 FAnimGraphEditor::NodeIdCounter = 0;
const FString FAnimGraphEditor::NodeIdPrefix = TEXT("MCP_ANIM_ID:");

// ===== Graph Finding (delegates to FAnimGraphFinder) =====

UEdGraph* FAnimGraphEditor::FindAnimGraph(UAnimBlueprint* AnimBP, FString& OutError)
{
	return FAnimGraphFinder::FindAnimGraph(AnimBP, OutError);
}

UEdGraph* FAnimGraphEditor::FindStateBoundGraph(
	UAnimBlueprint* AnimBP,
	const FString& StateMachineName,
	const FString& StateName,
	FString& OutError)
{
	return FAnimGraphFinder::FindStateBoundGraph(AnimBP, StateMachineName, StateName, OutError);
}

UEdGraph* FAnimGraphEditor::FindTransitionGraph(
	UAnimBlueprint* AnimBP,
	const FString& StateMachineName,
	const FString& FromState,
	const FString& ToState,
	FString& OutError)
{
	return FAnimGraphFinder::FindTransitionGraph(AnimBP, StateMachineName, FromState, ToState, OutError);
}

// ===== Transition Condition Nodes (delegates to FAnimTransitionConditionFactory) =====

UEdGraphNode* FAnimGraphEditor::CreateTransitionConditionNode(
	UEdGraph* TransitionGraph,
	const FString& NodeType,
	const TSharedPtr<FJsonObject>& Params,
	int32 PosX,
	int32 PosY,
	FString& OutNodeId,
	FString& OutError)
{
	return FAnimTransitionConditionFactory::CreateTransitionConditionNode(
		TransitionGraph, NodeType, Params, PosX, PosY, OutNodeId, OutError);
}

bool FAnimGraphEditor::ConnectTransitionNodes(
	UEdGraph* TransitionGraph,
	const FString& SourceNodeId,
	const FString& SourcePinName,
	const FString& TargetNodeId,
	const FString& TargetPinName,
	FString& OutError)
{
	return FAnimTransitionConditionFactory::ConnectTransitionNodes(
		TransitionGraph, SourceNodeId, SourcePinName, TargetNodeId, TargetPinName, OutError);
}

bool FAnimGraphEditor::ConnectToTransitionResult(
	UEdGraph* TransitionGraph,
	const FString& ConditionNodeId,
	const FString& ConditionPinName,
	FString& OutError)
{
	return FAnimTransitionConditionFactory::ConnectToTransitionResult(
		TransitionGraph, ConditionNodeId, ConditionPinName, OutError);
}

// ===== Animation Asset Nodes (delegates to FAnimAssetNodeFactory) =====

UEdGraphNode* FAnimGraphEditor::CreateAnimSequenceNode(
	UEdGraph* StateGraph,
	UAnimSequence* AnimSequence,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	return FAnimAssetNodeFactory::CreateAnimSequenceNode(StateGraph, AnimSequence, Position, OutNodeId, OutError);
}

UEdGraphNode* FAnimGraphEditor::CreateBlendSpaceNode(
	UEdGraph* StateGraph,
	UBlendSpace* BlendSpace,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	return FAnimAssetNodeFactory::CreateBlendSpaceNode(StateGraph, BlendSpace, Position, OutNodeId, OutError);
}

UEdGraphNode* FAnimGraphEditor::CreateBlendSpace1DNode(
	UEdGraph* StateGraph,
	UBlendSpace1D* BlendSpace,
	FVector2D Position,
	FString& OutNodeId,
	FString& OutError)
{
	return FAnimAssetNodeFactory::CreateBlendSpace1DNode(StateGraph, BlendSpace, Position, OutNodeId, OutError);
}

bool FAnimGraphEditor::ConnectToOutputPose(
	UEdGraph* StateGraph,
	const FString& AnimNodeId,
	FString& OutError)
{
	return FAnimAssetNodeFactory::ConnectToOutputPose(StateGraph, AnimNodeId, OutError);
}

bool FAnimGraphEditor::ClearStateGraph(UEdGraph* StateGraph, FString& OutError)
{
	return FAnimAssetNodeFactory::ClearStateGraph(StateGraph, OutError);
}

// ===== Generic Anim Node Connection =====

bool FAnimGraphEditor::ConnectAnimNodes(
	UAnimBlueprint* AnimBP,
	const FString& SourceNodeId,
	const FString& TargetNodeId,
	const FString& TargetGraphName,
	FString& OutError,
	const FString& SourcePinName,
	const FString& TargetPinName,
	const FString& StateMachineName,
	const FString& StateName)
{
	if (!AnimBP)
	{
		OutError = TEXT("Invalid Animation Blueprint");
		return false;
	}

	UEdGraph* Graph = nullptr;
	if (!StateMachineName.IsEmpty() && !StateName.IsEmpty())
	{
		Graph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, OutError);
	}
	else if (!TargetGraphName.IsEmpty())
	{
		Graph = FAnimLayerEditor::FindLayerFunctionGraph(AnimBP, TargetGraphName, OutError);
	}
	else
	{
		Graph = FAnimGraphFinder::FindAnimGraph(AnimBP, OutError);
	}

	if (!Graph)
	{
		return false;
	}

	UEdGraphNode* SourceNode = FindNodeById(Graph, SourceNodeId);
	if (!SourceNode)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(SourceNodeId))
			{
				SourceNode = Node;
				break;
			}
		}
	}

	if (!SourceNode)
	{
		OutError = FString::Printf(TEXT("Source node '%s' not found in graph '%s'"),
			*SourceNodeId, *Graph->GetName());
		return false;
	}

	bool bTargetIsRoot = TargetNodeId.Equals(TEXT("root"), ESearchCase::IgnoreCase) ||
		TargetNodeId.Equals(TEXT("output"), ESearchCase::IgnoreCase) ||
		TargetNodeId.Equals(TEXT("output_pose"), ESearchCase::IgnoreCase);

	UEdGraphNode* TargetNode = nullptr;
	if (bTargetIsRoot)
	{
		TargetNode = FindResultNode(Graph);
		if (!TargetNode)
		{
			OutError = FString::Printf(TEXT("Result/output node not found in graph '%s'"), *Graph->GetName());
			return false;
		}
	}
	else
	{
		TargetNode = FindNodeById(Graph, TargetNodeId);
		if (!TargetNode)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(TargetNodeId))
				{
					TargetNode = Node;
					break;
				}
			}
		}
	}

	if (!TargetNode)
	{
		OutError = FString::Printf(TEXT("Target node '%s' not found in graph '%s'"),
			*TargetNodeId, *Graph->GetName());
		return false;
	}

	bool bExplicitPins = !SourcePinName.IsEmpty() && !TargetPinName.IsEmpty();

	UEdGraphPin* SourcePin = nullptr;
	UEdGraphPin* TargetPin = nullptr;

	if (bExplicitPins)
	{
		SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Output);
		if (!SourcePin)
		{
			for (UEdGraphPin* Pin : SourceNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output &&
					Pin->PinName.ToString().Contains(SourcePinName))
				{
					SourcePin = Pin;
					break;
				}
			}
		}
		if (!SourcePin)
		{
			OutError = FString::Printf(TEXT("Output pin '%s' not found on source node '%s'. %s"),
				*SourcePinName, *SourceNodeId,
				*BuildAvailablePinsError(SourceNode, EGPD_Output, SourcePinName));
			return false;
		}

		TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_Input);
		if (!TargetPin)
		{
			for (UEdGraphPin* Pin : TargetNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input &&
					Pin->PinName.ToString().Contains(TargetPinName))
				{
					TargetPin = Pin;
					break;
				}
			}
		}
		if (!TargetPin)
		{
			OutError = FString::Printf(TEXT("Input pin '%s' not found on target node '%s'. %s"),
				*TargetPinName, *TargetNodeId,
				*BuildAvailablePinsError(TargetNode, EGPD_Input, TargetPinName));
			return false;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema)
		{
			TargetPin->BreakAllPinLinks();
			FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
			if (Response.Response == CONNECT_RESPONSE_DISALLOW)
			{
				OutError = FString::Printf(TEXT("Schema rejected connection '%s' -> '%s': %s"),
					*SourcePinName, *TargetPinName, *Response.Message.ToString());
				return false;
			}
			Schema->TryCreateConnection(SourcePin, TargetPin);
		}
		else
		{
			TargetPin->BreakAllPinLinks();
			SourcePin->MakeLinkTo(TargetPin);
		}
	}
	else
	{
		FPinSearchConfig OutputConfig = FPinSearchConfig::Output({
			FName(TEXT("Pose")), FName(TEXT("Output")), FName(TEXT("OutputPose")), FName(TEXT("Result"))
		}).WithNameContains(TEXT("Pose")).AcceptAny();

		FString SourcePinError;
		SourcePin = FindPinWithFallbacks(SourceNode, OutputConfig, &SourcePinError);
		if (!SourcePin)
		{
			OutError = FString::Printf(TEXT("No output pose pin on source node '%s'. %s"),
				*SourceNodeId, *SourcePinError);
			return false;
		}

		FPinSearchConfig InputConfig = FPinSearchConfig::Input({
			FName(TEXT("Result")), FName(TEXT("Pose")), FName(TEXT("InPose")), FName(TEXT("InputPose"))
		}).WithNameContains(TEXT("Pose")).AcceptAny();

		FString TargetPinError;
		TargetPin = FindPinWithFallbacks(TargetNode, InputConfig, &TargetPinError);
		if (!TargetPin)
		{
			OutError = FString::Printf(TEXT("No input pose pin on target node '%s'. %s"),
				*TargetNodeId, *TargetPinError);
			return false;
		}

		TargetPin->BreakAllPinLinks();

		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema)
		{
			Schema->TryCreateConnection(SourcePin, TargetPin);
		}
		else
		{
			SourcePin->MakeLinkTo(TargetPin);
		}
	}

	Graph->Modify();
	return true;
}

bool FAnimGraphEditor::BindVariable(
	UAnimBlueprint* AnimBP,
	const FString& VariableName,
	const FString& TargetNodeId,
	const FString& TargetPinName,
	const FString& TargetGraphName,
	FString& OutNodeId,
	FString& OutError,
	const FString& StateMachineName,
	const FString& StateName)
{
	if (!AnimBP)
	{
		OutError = TEXT("Invalid Animation Blueprint");
		return false;
	}

	UEdGraph* Graph = nullptr;
	if (!StateMachineName.IsEmpty() && !StateName.IsEmpty())
	{
		Graph = FAnimGraphEditor::FindStateBoundGraph(AnimBP, StateMachineName, StateName, OutError);
	}
	else if (!TargetGraphName.IsEmpty())
	{
		Graph = FAnimLayerEditor::FindLayerFunctionGraph(AnimBP, TargetGraphName, OutError);
	}
	else
	{
		Graph = FAnimGraphFinder::FindAnimGraph(AnimBP, OutError);
	}

	if (!Graph)
	{
		return false;
	}

	UEdGraphNode* TargetNode = FindNodeById(Graph, TargetNodeId);
	if (!TargetNode)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(TargetNodeId))
			{
				TargetNode = Node;
				break;
			}
		}
	}
	if (!TargetNode)
	{
		OutError = FString::Printf(TEXT("Target node '%s' not found in graph '%s'"),
			*TargetNodeId, *Graph->GetName());
		return false;
	}

	UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_Input);
	if (!TargetPin)
	{
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input &&
				Pin->PinName.ToString().Contains(TargetPinName))
			{
				TargetPin = Pin;
				break;
			}
		}
	}
	if (!TargetPin)
	{
		OutError = FString::Printf(TEXT("Input pin '%s' not found on target node '%s'. %s"),
			*TargetPinName, *TargetNodeId,
			*BuildAvailablePinsError(TargetNode, EGPD_Input, TargetPinName));
		return false;
	}

	FProperty* Property = FindFProperty<FProperty>(AnimBP->GeneratedClass, *VariableName);
	if (!Property)
	{
		Property = FindFProperty<FProperty>(AnimBP->SkeletonGeneratedClass, *VariableName);
	}
	if (!Property)
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found in AnimBlueprint"), *VariableName);
		return false;
	}

	UK2Node_VariableGet* VarNode = NewObject<UK2Node_VariableGet>(Graph);
	if (!VarNode)
	{
		OutError = TEXT("Failed to create variable get node");
		return false;
	}

	VarNode->VariableReference.SetSelfMember(FName(*VariableName));
	VarNode->NodePosX = TargetNode->NodePosX - 300;
	VarNode->NodePosY = TargetPin->GetOwningNode()->NodePosY;

	Graph->AddNode(VarNode, false, false);
	VarNode->ReconstructNode();

	OutNodeId = GenerateAnimNodeId(TEXT("Var"), VariableName, Graph);
	SetNodeId(VarNode, OutNodeId);

	UEdGraphPin* VarOutputPin = nullptr;
	for (UEdGraphPin* Pin : VarNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != UEdGraphSchema_K2::PN_Self)
		{
			VarOutputPin = Pin;
			break;
		}
	}

	if (!VarOutputPin)
	{
		OutError = FString::Printf(TEXT("Variable node for '%s' has no output pin"), *VariableName);
		return false;
	}

	TargetPin->BreakAllPinLinks();

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (Schema)
	{
		FPinConnectionResponse Response = Schema->CanCreateConnection(VarOutputPin, TargetPin);
		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			OutError = FString::Printf(TEXT("Schema rejected connection: variable '%s' output -> pin '%s': %s"),
				*VariableName, *TargetPinName, *Response.Message.ToString());
			return false;
		}
		Schema->TryCreateConnection(VarOutputPin, TargetPin);
	}
	else
	{
		VarOutputPin->MakeLinkTo(TargetPin);
	}

	Graph->Modify();
	return true;
}

// ===== Node Finding =====

UEdGraphNode* FAnimGraphEditor::FindNodeById(UEdGraph* Graph, const FString& NodeId)
{
	if (!Graph) return nullptr;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (GetNodeId(Node) == NodeId)
		{
			return Node;
		}
	}

	FString BPPrefix = TEXT("MCP_ID:");
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (Node->NodeComment.StartsWith(BPPrefix) &&
			Node->NodeComment.Mid(BPPrefix.Len()) == NodeId)
		{
			return Node;
		}
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->GetName() == NodeId)
		{
			return Node;
		}
	}

	return nullptr;
}

UEdGraphNode* FAnimGraphEditor::FindResultNode(UEdGraph* Graph)
{
	return FAnimNodePinUtils::FindResultNode(Graph);
}

UEdGraphPin* FAnimGraphEditor::FindPinByName(
	UEdGraphNode* Node,
	const FString& PinName,
	EEdGraphPinDirection Direction)
{
	return FAnimNodePinUtils::FindPinByName(Node, PinName, Direction);
}

UEdGraphPin* FAnimGraphEditor::FindPinWithFallbacks(
	UEdGraphNode* Node,
	const FPinSearchConfig& Config,
	FString* OutError)
{
	return FAnimNodePinUtils::FindPinWithFallbacks(Node, Config, OutError);
}

FString FAnimGraphEditor::BuildAvailablePinsError(
	UEdGraphNode* Node,
	EEdGraphPinDirection Direction,
	const FString& Context)
{
	return FAnimNodePinUtils::BuildAvailablePinsError(Node, Direction, Context);
}

// ===== Node ID System =====

FString FAnimGraphEditor::GenerateAnimNodeId(
	const FString& NodeType,
	const FString& Context,
	UEdGraph* Graph)
{
	int32 Counter = FPlatformAtomics::InterlockedIncrement(&NodeIdCounter);
	FString SafeContext = Context.Replace(TEXT(" "), TEXT("_"));
	FString NodeId = FString::Printf(TEXT("%s_%s_%d"), *NodeType, *SafeContext, Counter);

	// Verify uniqueness
	if (Graph)
	{
		bool bUnique = true;
		do
		{
			bUnique = true;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (GetNodeId(Node) == NodeId)
				{
					Counter = FPlatformAtomics::InterlockedIncrement(&NodeIdCounter);
					NodeId = FString::Printf(TEXT("%s_%s_%d"), *NodeType, *SafeContext, Counter);
					bUnique = false;
					break;
				}
			}
		} while (!bUnique);
	}

	return NodeId;
}

void FAnimGraphEditor::SetNodeId(UEdGraphNode* Node, const FString& NodeId)
{
	if (Node)
	{
		Node->NodeComment = NodeIdPrefix + NodeId;
	}
}

FString FAnimGraphEditor::GetNodeId(UEdGraphNode* Node)
{
	if (Node && Node->NodeComment.StartsWith(NodeIdPrefix))
	{
		return Node->NodeComment.Mid(NodeIdPrefix.Len());
	}
	return FString();
}

TSharedPtr<FJsonObject> FAnimGraphEditor::SerializeAnimNodeInfo(UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();

	if (!Node) return Json;

	Json->SetStringField(TEXT("node_id"), GetNodeId(Node));
	Json->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	Json->SetNumberField(TEXT("pos_x"), Node->NodePosX);
	Json->SetNumberField(TEXT("pos_y"), Node->NodePosY);

	// Add pin info
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();
		PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PinJson->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
		PinsArray.Add(MakeShared<FJsonValueObject>(PinJson));
	}
	Json->SetArrayField(TEXT("pins"), PinsArray);

	return Json;
}

// ===== AnimGraph Root Connection =====

UAnimGraphNode_Root* FAnimGraphEditor::FindAnimGraphRoot(UAnimBlueprint* AnimBP, FString& OutError)
{
	return FAnimGraphFinder::FindAnimGraphRoot(AnimBP, OutError);
}

bool FAnimGraphEditor::ConnectStateMachineToAnimGraphRoot(
	UAnimBlueprint* AnimBP,
	const FString& StateMachineName,
	FString& OutError,
	const FString& TargetGraphName)
{
	if (!AnimBP)
	{
		OutError = TEXT("Invalid Animation Blueprint");
		return false;
	}

	UEdGraph* Graph = nullptr;
	if (!TargetGraphName.IsEmpty())
	{
		Graph = FAnimLayerEditor::FindLayerFunctionGraph(AnimBP, TargetGraphName, OutError);
	}
	else
	{
		Graph = FAnimGraphFinder::FindAnimGraph(AnimBP, OutError);
	}
	if (!Graph)
	{
		return false;
	}

	UAnimGraphNode_StateMachine* StateMachineNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
		{
			if (SMNode->GetStateMachineName().Equals(StateMachineName, ESearchCase::IgnoreCase))
			{
				StateMachineNode = SMNode;
				break;
			}
		}
	}
	if (!StateMachineNode)
	{
		TArray<FString> Available;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
			{
				Available.Add(SMNode->GetStateMachineName());
			}
		}
		OutError = FString::Printf(TEXT("State machine '%s' not found in graph '%s'. Available: %s"),
			*StateMachineName, *Graph->GetName(),
			Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("(none)"));
		return false;
	}

	UEdGraphNode* ResultNode = FindResultNode(Graph);
	if (!ResultNode)
	{
		OutError = FString::Printf(TEXT("Result/output node not found in graph '%s'"), *Graph->GetName());
		return false;
	}

	FPinSearchConfig OutputConfig = FPinSearchConfig::Output({
		FName(TEXT("Pose")), FName(TEXT("Output")), FName(TEXT("OutputPose")), FName(TEXT("Result"))
	}).WithNameContains(TEXT("Pose")).AcceptAny();

	FString SMPinError;
	UEdGraphPin* SMOutputPin = FindPinWithFallbacks(StateMachineNode, OutputConfig, &SMPinError);
	if (!SMOutputPin)
	{
		OutError = FString::Printf(TEXT("State Machine '%s' has no output pose pin. %s"), *StateMachineName, *SMPinError);
		return false;
	}

	FPinSearchConfig InputConfig = FPinSearchConfig::Input({
		FName(TEXT("Result")), FName(TEXT("Pose")), FName(TEXT("InPose")), FName(TEXT("InputPose"))
	}).WithNameContains(TEXT("Pose")).AcceptAny();

	FString RootPinError;
	UEdGraphPin* RootInputPin = FindPinWithFallbacks(ResultNode, InputConfig, &RootPinError);
	if (!RootInputPin)
	{
		OutError = FString::Printf(TEXT("Result node has no input pose pin. %s"), *RootPinError);
		return false;
	}

	RootInputPin->BreakAllPinLinks();

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (Schema)
	{
		FPinConnectionResponse Response = Schema->CanCreateConnection(SMOutputPin, RootInputPin);
		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			OutError = FString::Printf(TEXT("Schema rejected connection: %s"), *Response.Message.ToString());
			return false;
		}
		Schema->TryCreateConnection(SMOutputPin, RootInputPin);
	}
	else
	{
		SMOutputPin->MakeLinkTo(RootInputPin);
	}

	Graph->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);

	return true;
}

// ===== Transition Graph Node Operations =====

TSharedPtr<FJsonObject> FAnimGraphEditor::SerializeDetailedPinInfo(UEdGraphPin* Pin)
{
	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();

	if (!Pin) return PinObj;

	PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));

	// Detailed type info
	FString TypeStr;
	FString SubCategoryStr;

	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		TypeStr = TEXT("bool");
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		TypeStr = TEXT("int32");
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
	{
		TypeStr = TEXT("int64");
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		TypeStr = (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double) ? TEXT("double") : TEXT("float");
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		TypeStr = TEXT("FString");
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		TypeStr = TEXT("FName");
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		TypeStr = TEXT("FText");
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		TypeStr = TEXT("exec");
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			TypeStr = TEXT("struct");
			SubCategoryStr = Struct->GetName();
		}
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		if (UClass* Class = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			TypeStr = TEXT("object");
			SubCategoryStr = Class->GetName();
		}
	}
	else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
	{
		TypeStr = TEXT("class");
	}
	else
	{
		TypeStr = Pin->PinType.PinCategory.ToString();
	}

	PinObj->SetStringField(TEXT("type"), TypeStr);
	if (!SubCategoryStr.IsEmpty())
	{
		PinObj->SetStringField(TEXT("sub_type"), SubCategoryStr);
	}

	// Default value
	if (!Pin->DefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	}
	if (!Pin->AutogeneratedDefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("auto_default_value"), Pin->AutogeneratedDefaultValue);
	}

	// Connection info
	PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);
	PinObj->SetNumberField(TEXT("connection_count"), Pin->LinkedTo.Num());

	// Connected to
	if (Pin->LinkedTo.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ConnectedTo;
		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (LinkedPin && LinkedPin->GetOwningNode())
			{
				TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
				LinkObj->SetStringField(TEXT("node_id"), GetNodeId(LinkedPin->GetOwningNode()));
				LinkObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
				ConnectedTo.Add(MakeShared<FJsonValueObject>(LinkObj));
			}
		}
		PinObj->SetArrayField(TEXT("connected_to"), ConnectedTo);
	}

	return PinObj;
}

TSharedPtr<FJsonObject> FAnimGraphEditor::GetTransitionGraphNodes(
	UEdGraph* TransitionGraph,
	FString& OutError)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!TransitionGraph)
	{
		OutError = TEXT("Invalid transition graph");
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("graph_name"), TransitionGraph->GetName());

	TArray<TSharedPtr<FJsonValue>> NodesArray;

	for (UEdGraphNode* Node : TransitionGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

		// Basic node info
		FString NodeId = GetNodeId(Node);
		NodeObj->SetStringField(TEXT("node_id"), NodeId.IsEmpty() ? TEXT("(unnamed)") : NodeId);
		NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

		// Check if this is the result node
		bool bIsResultNode = Node->IsA<UAnimGraphNode_TransitionResult>();
		NodeObj->SetBoolField(TEXT("is_result_node"), bIsResultNode);

		// Detailed pins
		TArray<TSharedPtr<FJsonValue>> InputPins;
		TArray<TSharedPtr<FJsonValue>> OutputPins;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;

			TSharedPtr<FJsonObject> PinObj = SerializeDetailedPinInfo(Pin);

			if (Pin->Direction == EGPD_Input)
			{
				InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
			}
			else
			{
				OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
			}
		}

		NodeObj->SetArrayField(TEXT("input_pins"), InputPins);
		NodeObj->SetArrayField(TEXT("output_pins"), OutputPins);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetNumberField(TEXT("node_count"), NodesArray.Num());

	return Result;
}

TSharedPtr<FJsonObject> FAnimGraphEditor::GetAllTransitionNodes(
	UAnimBlueprint* AnimBP,
	const FString& StateMachineName,
	FString& OutError)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!AnimBP)
	{
		OutError = TEXT("Invalid Animation Blueprint");
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	// Find state machine
	UAnimGraphNode_StateMachine* StateMachine = FAnimStateMachineEditor::FindStateMachine(AnimBP, StateMachineName, OutError);
	if (!StateMachine)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	FString SMName = StateMachine->GetStateMachineName();
	TArray<UAnimStateTransitionNode*> Transitions = FAnimStateMachineEditor::GetAllTransitions(AnimBP, SMName, OutError);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_machine"), StateMachineName);

	TArray<TSharedPtr<FJsonValue>> TransitionsArray;

	for (UAnimStateTransitionNode* Transition : Transitions)
	{
		if (!Transition) continue;

		TSharedPtr<FJsonObject> TransitionObj = MakeShared<FJsonObject>();

		FString FromState, ToState;
		if (Transition->GetPreviousState())
		{
			FromState = Transition->GetPreviousState()->GetStateName();
		}
		if (Transition->GetNextState())
		{
			ToState = Transition->GetNextState()->GetStateName();
		}

		TransitionObj->SetStringField(TEXT("from_state"), FromState);
		TransitionObj->SetStringField(TEXT("to_state"), ToState);
		TransitionObj->SetStringField(TEXT("transition_name"), FString::Printf(TEXT("%s -> %s"), *FromState, *ToState));

		UEdGraph* TransGraph = FAnimStateMachineEditor::GetTransitionGraph(Transition, OutError);
		if (TransGraph)
		{
			TSharedPtr<FJsonObject> NodesInfo = GetTransitionGraphNodes(TransGraph, OutError);
			TransitionObj->SetObjectField(TEXT("graph"), NodesInfo);
		}

		TransitionsArray.Add(MakeShared<FJsonValueObject>(TransitionObj));
	}

	Result->SetArrayField(TEXT("transitions"), TransitionsArray);
	Result->SetNumberField(TEXT("transition_count"), TransitionsArray.Num());

	return Result;
}

bool FAnimGraphEditor::SetAnimNodeProperty(
	UEdGraph* Graph,
	const FString& NodeId,
	const FString& PropertyName,
	const FString& Value,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Invalid graph");
		return false;
	}

	UEdGraphNode* Node = FindNodeById(Graph, NodeId);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Node '%s' not found in graph '%s'"), *NodeId, *Graph->GetName());
		return false;
	}

	UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
	if (!AnimNode)
	{
		OutError = FString::Printf(TEXT("Node '%s' is not an anim graph node (class: %s)"),
			*NodeId, *Node->GetClass()->GetName());
		return false;
	}

	FStructProperty* NodeStructProp = nullptr;
	for (TFieldIterator<FStructProperty> It(AnimNode->GetClass()); It; ++It)
	{
		if (It->GetFName() == TEXT("Node") && It->Struct && It->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
		{
			NodeStructProp = *It;
			break;
		}
	}

	if (!NodeStructProp)
	{
		OutError = FString::Printf(TEXT("Node '%s' (class: %s) has no embedded FAnimNode struct"),
			*NodeId, *AnimNode->GetClass()->GetName());
		return false;
	}

	UScriptStruct* NodeStruct = NodeStructProp->Struct;
	void* NodeStructData = NodeStructProp->ContainerPtrToValuePtr<void>(AnimNode);

	FProperty* TargetProp = NodeStruct->FindPropertyByName(FName(*PropertyName));
	if (!TargetProp)
	{
		TArray<FString> AvailableProps;
		for (TFieldIterator<FProperty> PropIt(NodeStruct); PropIt; ++PropIt)
		{
			AvailableProps.Add(PropIt->GetName());
		}
		OutError = FString::Printf(TEXT("Property '%s' not found on %s. Available: %s"),
			*PropertyName, *NodeStruct->GetName(), *FString::Join(AvailableProps, TEXT(", ")));
		return false;
	}

	void* PropData = TargetProp->ContainerPtrToValuePtr<void>(NodeStructData);
	const TCHAR* ValuePtr = *Value;
	if (!TargetProp->ImportText_Direct(ValuePtr, PropData, nullptr, PPF_None))
	{
		OutError = FString::Printf(TEXT("Failed to set '%s' to '%s' (type: %s)"),
			*PropertyName, *Value, *TargetProp->GetCPPType());
		return false;
	}

	AnimNode->Modify();
	Graph->Modify();

	return true;
}

bool FAnimGraphEditor::ValidatePinValueType(
	UEdGraphPin* Pin,
	const FString& Value,
	FString& OutError)
{
	return FAnimNodePinUtils::ValidatePinValueType(Pin, Value, OutError);
}

bool FAnimGraphEditor::SetPinDefaultValueWithValidation(
	UEdGraph* Graph,
	const FString& NodeId,
	const FString& PinName,
	const FString& Value,
	FString& OutError)
{
	return FAnimNodePinUtils::SetPinDefaultValueWithValidation(Graph, NodeId, PinName, Value, OutError);
}

TSharedPtr<FJsonObject> FAnimGraphEditor::CreateComparisonChain(
	UAnimBlueprint* AnimBP,
	UEdGraph* TransitionGraph,
	const FString& VariableName,
	const FString& ComparisonType,
	const FString& CompareValue,
	FVector2D Position,
	FString& OutError)
{
	return FAnimTransitionConditionFactory::CreateComparisonChain(
		AnimBP, TransitionGraph, VariableName, ComparisonType, CompareValue, Position, OutError);
}
