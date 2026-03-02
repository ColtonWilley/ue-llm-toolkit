// Copyright Natali Caggiano. All Rights Reserved.

#include "AnimLayerEditor.h"
#include "AnimGraphEditor.h"
#include "AnimGraphFinder.h"
#include "AnimationBlueprintUtils.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimLayerInterface.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "Engine/MemberReference.h"
#include "AnimGraphNode_Root.h"
#include "AnimationGraph.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// Read Operations
// ============================================================================

TSharedPtr<FJsonObject> FAnimLayerEditor::GetImplementedLayerInterfaces(
	UAnimBlueprint* AnimBP,
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

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint"), AnimBP->GetName());

	TArray<TSharedPtr<FJsonValue>> InterfacesArray;

	for (const FBPInterfaceDescription& InterfaceDesc : AnimBP->ImplementedInterfaces)
	{
		UClass* InterfaceClass = InterfaceDesc.Interface;
		if (!InterfaceClass)
		{
			continue;
		}

		if (!InterfaceClass->IsChildOf(UAnimLayerInterface::StaticClass()))
		{
			continue;
		}

		TSharedPtr<FJsonObject> InterfaceObj = MakeShared<FJsonObject>();
		InterfaceObj->SetStringField(TEXT("name"), InterfaceClass->GetName());
		InterfaceObj->SetStringField(TEXT("class_path"), InterfaceClass->GetPathName());

		TArray<TSharedPtr<FJsonValue>> FunctionNames;
		for (const UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			if (Graph)
			{
				FunctionNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));
			}
		}
		InterfaceObj->SetArrayField(TEXT("layer_functions"), FunctionNames);

		InterfacesArray.Add(MakeShared<FJsonValueObject>(InterfaceObj));
	}

	Result->SetArrayField(TEXT("interfaces"), InterfacesArray);
	Result->SetNumberField(TEXT("count"), InterfacesArray.Num());

	return Result;
}

TSharedPtr<FJsonObject> FAnimLayerEditor::GetAvailableLayers(
	UAnimBlueprint* AnimBP,
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

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint"), AnimBP->GetName());

	TArray<TSharedPtr<FJsonValue>> LayersArray;

	UAnimBlueprintGeneratedClass* GenClass = Cast<UAnimBlueprintGeneratedClass>(AnimBP->GeneratedClass);
	if (GenClass)
	{
		IAnimClassInterface* AnimClassInterface = GenClass;
		const TArray<FAnimBlueprintFunction>& Functions = AnimClassInterface->GetAnimBlueprintFunctions();

		for (const FAnimBlueprintFunction& Func : Functions)
		{
			if (Func.Name == NAME_None || Func.Name == FName(TEXT("AnimGraph")))
			{
				continue;
			}

			TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
			LayerObj->SetStringField(TEXT("name"), Func.Name.ToString());
			LayerObj->SetStringField(TEXT("group"), Func.Group.ToString());
			LayerObj->SetBoolField(TEXT("implemented"), Func.bImplemented);

			TArray<TSharedPtr<FJsonValue>> InputPoseNames;
			for (const FName& PoseName : Func.InputPoseNames)
			{
				InputPoseNames.Add(MakeShared<FJsonValueString>(PoseName.ToString()));
			}
			LayerObj->SetArrayField(TEXT("input_poses"), InputPoseNames);

			LayersArray.Add(MakeShared<FJsonValueObject>(LayerObj));
		}
	}
	else
	{
		// If no generated class yet, try reading from interface descriptions
		for (const FBPInterfaceDescription& InterfaceDesc : AnimBP->ImplementedInterfaces)
		{
			UClass* InterfaceClass = InterfaceDesc.Interface;
			if (!InterfaceClass || !InterfaceClass->IsChildOf(UAnimLayerInterface::StaticClass()))
			{
				continue;
			}

			for (const UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				if (!Graph || Graph->GetName() == TEXT("AnimGraph"))
				{
					continue;
				}

				TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
				LayerObj->SetStringField(TEXT("name"), Graph->GetName());
				LayerObj->SetStringField(TEXT("group"), TEXT(""));
				LayerObj->SetBoolField(TEXT("implemented"), true);
				LayerObj->SetStringField(TEXT("source_interface"), InterfaceClass->GetName());
				LayerObj->SetArrayField(TEXT("input_poses"), TArray<TSharedPtr<FJsonValue>>());

				LayersArray.Add(MakeShared<FJsonValueObject>(LayerObj));
			}
		}
	}

	Result->SetArrayField(TEXT("layers"), LayersArray);
	Result->SetNumberField(TEXT("count"), LayersArray.Num());

	return Result;
}

TSharedPtr<FJsonObject> FAnimLayerEditor::GetLinkedLayerNodes(
	UAnimBlueprint* AnimBP,
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

	UEdGraph* AnimGraph = FAnimGraphFinder::FindAnimGraph(AnimBP, OutError);
	if (!AnimGraph)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint"), AnimBP->GetName());

	TArray<TSharedPtr<FJsonValue>> NodesArray;

	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (!Node) continue;

		UAnimGraphNode_LinkedAnimLayer* LayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node);
		if (!LayerNode)
		{
			continue;
		}

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

		FString NodeId = FAnimGraphEditor::GetNodeId(LayerNode);
		NodeObj->SetStringField(TEXT("node_id"), NodeId.IsEmpty() ? TEXT("(unnamed)") : NodeId);
		NodeObj->SetStringField(TEXT("layer_name"), LayerNode->Node.Layer.ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), LayerNode->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), LayerNode->NodePosY);

		UClass* InterfaceClass = *LayerNode->Node.Interface;
		if (InterfaceClass)
		{
			NodeObj->SetStringField(TEXT("interface_class"), InterfaceClass->GetPathName());
			NodeObj->SetStringField(TEXT("interface_name"), InterfaceClass->GetName());
		}
		else
		{
			NodeObj->SetStringField(TEXT("interface_class"), TEXT("(self)"));
		}

		const FAnimNode_LinkedAnimLayer& AnimNode = LayerNode->Node;
		UClass* InstanceClass = AnimNode.GetTargetClass();
		if (InstanceClass && InstanceClass != *AnimNode.Interface)
		{
			NodeObj->SetStringField(TEXT("instance_class"), InstanceClass->GetPathName());
		}

		bool bConnected = false;
		for (UEdGraphPin* Pin : LayerNode->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
			{
				bConnected = true;
				break;
			}
		}
		NodeObj->SetBoolField(TEXT("connected_to_output"), bConnected);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetNumberField(TEXT("count"), NodesArray.Num());

	return Result;
}

// ============================================================================
// Layer Graph Finding
// ============================================================================

UEdGraph* FAnimLayerEditor::FindLayerFunctionGraph(
	UAnimBlueprint* AnimBP,
	const FString& LayerName,
	FString& OutError)
{
	if (!AnimBP)
	{
		OutError = TEXT("Invalid Animation Blueprint");
		return nullptr;
	}

	TArray<FString> AvailableNames;

	for (const FBPInterfaceDescription& InterfaceDesc : AnimBP->ImplementedInterfaces)
	{
		UClass* InterfaceClass = InterfaceDesc.Interface;
		if (!InterfaceClass || !InterfaceClass->IsChildOf(UAnimLayerInterface::StaticClass()))
		{
			continue;
		}

		for (UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			if (!Graph) continue;

			FString GraphName = Graph->GetName();
			AvailableNames.Add(GraphName);

			if (GraphName.Equals(LayerName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}
	}

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (!Graph) continue;

		if (Graph->IsA<UAnimationGraph>())
		{
			FString GraphName = Graph->GetName();
			if (!AvailableNames.Contains(GraphName))
			{
				AvailableNames.Add(GraphName);
			}

			if (GraphName.Equals(LayerName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}
	}

	OutError = FString::Printf(TEXT("Layer function graph '%s' not found. Available: %s"),
		*LayerName,
		AvailableNames.Num() > 0 ? *FString::Join(AvailableNames, TEXT(", ")) : TEXT("(none)"));

	return nullptr;
}

TSharedPtr<FJsonObject> FAnimLayerEditor::InspectLayerGraph(
	UAnimBlueprint* AnimBP,
	const FString& LayerName,
	FString& OutError)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UEdGraph* Graph = FindLayerFunctionGraph(AnimBP, LayerName, OutError);
	if (!Graph)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("graph_name"), Graph->GetName());
	Result->SetStringField(TEXT("graph_class"), Graph->GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> NodesArray;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

		FString NodeId = FAnimGraphEditor::GetNodeId(Node);
		NodeObj->SetStringField(TEXT("node_id"), NodeId.IsEmpty() ? TEXT("(unnamed)") : NodeId);
		NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;

			TSharedPtr<FJsonObject> PinObj = FAnimGraphEditor::SerializeDetailedPinInfo(Pin);
			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetNumberField(TEXT("node_count"), NodesArray.Num());

	return Result;
}

// ============================================================================
// Write Operations
// ============================================================================

TSharedPtr<FJsonObject> FAnimLayerEditor::AddLayerInterface(
	UAnimBlueprint* AnimBP,
	const FString& InterfaceClassPath,
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

	UClass* InterfaceClass = LoadClass<UInterface>(nullptr, *InterfaceClassPath);
	if (!InterfaceClass)
	{
		OutError = FString::Printf(TEXT("Could not load interface class: %s"), *InterfaceClassPath);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	if (!InterfaceClass->IsChildOf(UAnimLayerInterface::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not an Animation Layer Interface (must derive from UAnimLayerInterface)"), *InterfaceClass->GetName());
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	// Check if already implemented
	for (const FBPInterfaceDescription& Existing : AnimBP->ImplementedInterfaces)
	{
		if (Existing.Interface == InterfaceClass)
		{
			OutError = FString::Printf(TEXT("Interface '%s' is already implemented"), *InterfaceClass->GetName());
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), OutError);
			return Result;
		}
	}

	// Record graph count before adding
	int32 GraphCountBefore = 0;
	for (const FBPInterfaceDescription& Desc : AnimBP->ImplementedInterfaces)
	{
		GraphCountBefore += Desc.Graphs.Num();
	}

	bool bSuccess = FBlueprintEditorUtils::ImplementNewInterface(AnimBP, InterfaceClass->GetClassPathName());

	if (!bSuccess)
	{
		OutError = FString::Printf(TEXT("Failed to implement interface '%s'"), *InterfaceClass->GetName());
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	// Recompile to make layers available
	FString CompileError;
	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, CompileError);

	// Collect new layer graphs
	TArray<TSharedPtr<FJsonValue>> NewGraphs;
	for (const FBPInterfaceDescription& Desc : AnimBP->ImplementedInterfaces)
	{
		if (Desc.Interface == InterfaceClass)
		{
			for (const UEdGraph* Graph : Desc.Graphs)
			{
				if (Graph)
				{
					NewGraphs.Add(MakeShared<FJsonValueString>(Graph->GetName()));
				}
			}
			break;
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("interface_name"), InterfaceClass->GetName());
	Result->SetStringField(TEXT("interface_path"), InterfaceClass->GetPathName());
	Result->SetArrayField(TEXT("layer_graphs_created"), NewGraphs);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Added interface '%s' with %d layer graph(s)"),
		*InterfaceClass->GetName(), NewGraphs.Num()));

	return Result;
}

TSharedPtr<FJsonObject> FAnimLayerEditor::RemoveLayerInterface(
	UAnimBlueprint* AnimBP,
	const FString& InterfaceClassPath,
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

	UClass* InterfaceClass = LoadClass<UInterface>(nullptr, *InterfaceClassPath);
	if (!InterfaceClass)
	{
		OutError = FString::Printf(TEXT("Could not load interface class: %s"), *InterfaceClassPath);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	// Verify it's implemented
	bool bFound = false;
	for (const FBPInterfaceDescription& Desc : AnimBP->ImplementedInterfaces)
	{
		if (Desc.Interface == InterfaceClass)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		OutError = FString::Printf(TEXT("Interface '%s' is not implemented by this blueprint"), *InterfaceClass->GetName());
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	FBlueprintEditorUtils::RemoveInterface(AnimBP, InterfaceClass->GetClassPathName(), false);

	FString CompileError;
	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, CompileError);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("interface_name"), InterfaceClass->GetName());
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Removed interface '%s'"), *InterfaceClass->GetName()));

	return Result;
}

TSharedPtr<FJsonObject> FAnimLayerEditor::CreateLinkedLayerNode(
	UAnimBlueprint* AnimBP,
	const FString& LayerName,
	FVector2D Position,
	const FString& InstanceClass,
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

	UEdGraph* AnimGraph = FAnimGraphFinder::FindAnimGraph(AnimBP, OutError);
	if (!AnimGraph)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	FGraphNodeCreator<UAnimGraphNode_LinkedAnimLayer> NodeCreator(*AnimGraph);
	UAnimGraphNode_LinkedAnimLayer* LayerNode = NodeCreator.CreateNode();

	if (!LayerNode)
	{
		OutError = TEXT("Failed to create LinkedAnimLayer node");
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	LayerNode->NodePosX = static_cast<int32>(Position.X);
	LayerNode->NodePosY = static_cast<int32>(Position.Y);

	LayerNode->Node.Layer = FName(*LayerName);

	FString NodeId = FAnimGraphEditor::GenerateAnimNodeId(
		TEXT("LinkedLayer"), LayerName, AnimGraph);
	FAnimGraphEditor::SetNodeId(LayerNode, NodeId);

	NodeCreator.Finalize();

	// Resolve interface from AnimBP's implemented interfaces
	// (replicates protected GetInterfaceForLayer + GetGuidForLayer)
	for (FBPInterfaceDescription& InterfaceDesc : AnimBP->ImplementedInterfaces)
	{
		for (UEdGraph* InterfaceGraph : InterfaceDesc.Graphs)
		{
			if (InterfaceGraph->GetFName() == FName(*LayerName))
			{
				LayerNode->Node.Interface = InterfaceDesc.Interface;
				LayerNode->InterfaceGuid = InterfaceGraph->InterfaceGuid;
				break;
			}
		}
		if (LayerNode->Node.Interface.Get()) break;
	}

	// Self-layers can't have override implementations
	if (!LayerNode->Node.Interface.Get())
	{
		LayerNode->Node.InstanceClass = nullptr;
	}

	// Set FunctionReference via property reflection
	// (FunctionReference is protected on UAnimGraphNode_LinkedAnimGraphBase,
	// but accessible via UPROPERTY reflection — same pattern as BlueprintGraphEditor.cpp:1138)
	FProperty* FuncRefProp = LayerNode->GetClass()->FindPropertyByName(TEXT("FunctionReference"));
	if (FuncRefProp)
	{
		FMemberReference* MemberRef = FuncRefProp->ContainerPtrToValuePtr<FMemberReference>(LayerNode);
		UClass* TargetClass = AnimBP->SkeletonGeneratedClass
			? *AnimBP->SkeletonGeneratedClass : nullptr;
		FName FunctionName = FName(*LayerName);
		if (TargetClass)
		{
			FGuid FunctionGuid;
			FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
				FBlueprintEditorUtils::GetMostUpToDateClass(TargetClass),
				FunctionName, FunctionGuid);
			MemberRef->SetExternalMember(FunctionName, TargetClass, FunctionGuid);
		}
		else
		{
			MemberRef->SetSelfMember(FunctionName);
		}
	}

	// Reconstruct node to generate proper pins from FunctionReference
	LayerNode->ReconstructNode();

	// Optionally set instance class
	if (!InstanceClass.IsEmpty())
	{
		UAnimBlueprint* InstBP = Cast<UAnimBlueprint>(
			StaticLoadObject(UAnimBlueprint::StaticClass(), nullptr, *InstanceClass));
		if (InstBP && InstBP->GetAnimBlueprintGeneratedClass())
		{
			LayerNode->Node.InstanceClass = InstBP->GetAnimBlueprintGeneratedClass();
			LayerNode->ReconstructNode();
		}
	}

	AnimGraph->Modify();

	FString CompileError;
	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, CompileError);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("layer_name"), LayerName);

	UClass* InterfaceClass = *LayerNode->Node.Interface;
	if (InterfaceClass)
	{
		Result->SetStringField(TEXT("interface_class"), InterfaceClass->GetPathName());
		Result->SetStringField(TEXT("interface_name"), InterfaceClass->GetName());
	}
	else
	{
		Result->SetStringField(TEXT("interface_class"), TEXT("(self)"));
	}

	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Created LinkedAnimLayer node for layer '%s'"), *LayerName));

	return Result;
}

TSharedPtr<FJsonObject> FAnimLayerEditor::SetLinkedLayerInstance(
	UAnimBlueprint* AnimBP,
	const FString& NodeId,
	const FString& InstanceClassPath,
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

	UEdGraph* AnimGraph = FAnimGraphFinder::FindAnimGraph(AnimBP, OutError);
	if (!AnimGraph)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	UEdGraphNode* FoundNode = FAnimGraphEditor::FindNodeById(AnimGraph, NodeId);
	if (!FoundNode)
	{
		OutError = FString::Printf(TEXT("Node with ID '%s' not found in AnimGraph"), *NodeId);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	UAnimGraphNode_LinkedAnimLayer* LayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(FoundNode);
	if (!LayerNode)
	{
		OutError = FString::Printf(TEXT("Node '%s' is not a LinkedAnimLayer node (class: %s)"),
			*NodeId, *FoundNode->GetClass()->GetName());
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	UAnimBlueprint* InstBP = Cast<UAnimBlueprint>(
		StaticLoadObject(UAnimBlueprint::StaticClass(), nullptr, *InstanceClassPath));
	if (!InstBP)
	{
		OutError = FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *InstanceClassPath);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	UAnimBlueprintGeneratedClass* GenClass = InstBP->GetAnimBlueprintGeneratedClass();
	if (!GenClass)
	{
		OutError = FString::Printf(TEXT("AnimBlueprint '%s' has no generated class (needs compilation)"), *InstanceClassPath);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		return Result;
	}

	LayerNode->Node.InstanceClass = GenClass;
	LayerNode->ReconstructNode();
	AnimGraph->Modify();

	FString CompileError;
	FAnimationBlueprintUtils::CompileAnimBlueprint(AnimBP, CompileError);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("instance_class"), GenClass->GetPathName());
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Set instance class to '%s' on node '%s'"),
		*InstBP->GetName(), *NodeId));

	return Result;
}
