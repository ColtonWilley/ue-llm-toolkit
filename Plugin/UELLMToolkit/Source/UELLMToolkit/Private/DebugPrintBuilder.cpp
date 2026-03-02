// Copyright Natali Caggiano. All Rights Reserved.

#include "DebugPrintBuilder.h"
#include "BlueprintGraphEditor.h"
#include "UnrealClaudeModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableGet.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Animation/AnimInstance.h"

FString FDebugPrintBuilder::MakeNodeIdPrefix(const FString& Label)
{
	return FString::Printf(TEXT("DbgPrint_%s_"), *Label);
}

UEdGraphNode* FDebugPrintBuilder::FindExistingEventNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& EventName)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
		if (!EventNode)
		{
			continue;
		}
		FName FuncName = EventNode->EventReference.GetMemberName();
		if (FuncName.ToString().Equals(EventName, ESearchCase::IgnoreCase))
		{
			return Node;
		}
		if (EventName.Equals(TEXT("BlueprintUpdateAnimation"), ESearchCase::IgnoreCase)
			&& FuncName == FName("BlueprintUpdateAnimation"))
		{
			return Node;
		}
	}
	return nullptr;
}

int32 FDebugPrintBuilder::RemoveNodesByPrefix(UEdGraph* Graph, const FString& Prefix)
{
	TArray<UEdGraphNode*> ToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		FString NodeId = FBlueprintGraphEditor::GetNodeId(Node);
		if (NodeId.StartsWith(Prefix))
		{
			ToRemove.Add(Node);
		}
	}

	for (UEdGraphNode* Node : ToRemove)
	{
		Node->BreakAllNodeLinks();
		Graph->RemoveNode(Node);
	}

	return ToRemove.Num();
}

FDebugPrintResult FDebugPrintBuilder::AddDebugPrint(UBlueprint* Blueprint, const FDebugPrintConfig& Config)
{
	FDebugPrintResult Result;

	if (!Blueprint)
	{
		Result.Error = TEXT("Blueprint is null");
		return Result;
	}

	if (Config.Label.IsEmpty())
	{
		Result.Error = TEXT("Label is required");
		return Result;
	}

	if (Config.Variables.Num() == 0 && Config.Functions.Num() == 0)
	{
		Result.Error = TEXT("At least one variable or function is required");
		return Result;
	}

	FString GraphError;
	UEdGraph* Graph = FBlueprintGraphEditor::FindGraph(Blueprint, TEXT(""), false, GraphError);
	if (!Graph)
	{
		Result.Error = FString::Printf(TEXT("Could not find event graph: %s"), *GraphError);
		return Result;
	}

	FString IdPrefix = MakeNodeIdPrefix(Config.Label);
	RemoveNodesByPrefix(Graph, IdPrefix);

	UEdGraphNode* EventNode = FindExistingEventNode(Graph, Blueprint, Config.EventName);
	UEdGraphPin* ExistingExecChain = nullptr;
	bool bCreatedEvent = false;

	if (EventNode)
	{
		UEdGraphPin* EventExecOut = FBlueprintGraphEditor::GetExecPin(EventNode, true);
		if (EventExecOut && EventExecOut->LinkedTo.Num() > 0)
		{
			for (UEdGraphPin* LinkedPin : EventExecOut->LinkedTo)
			{
				FString LinkedNodeId = FBlueprintGraphEditor::GetNodeId(LinkedPin->GetOwningNode());
				if (!LinkedNodeId.StartsWith(IdPrefix))
				{
					ExistingExecChain = LinkedPin;
					break;
				}
			}
		}
	}
	else
	{
		FString NodeId;
		FString CreateError;
		TSharedPtr<FJsonObject> EventParams = MakeShared<FJsonObject>();
		EventParams->SetStringField(TEXT("event"), Config.EventName);
		EventNode = FBlueprintGraphEditor::CreateNode(Graph, TEXT("Event"), EventParams, -400, 0, NodeId, CreateError);
		if (!EventNode)
		{
			Result.Error = FString::Printf(TEXT("Failed to create event node '%s': %s"), *Config.EventName, *CreateError);
			return Result;
		}
		FString EventNodeId = IdPrefix + TEXT("Event");
		FBlueprintGraphEditor::SetNodeId(EventNode, EventNodeId);
		Result.CreatedNodeIds.Add(EventNodeId);
		bCreatedEvent = true;
	}

	int32 TotalItems = Config.Variables.Num() + Config.Functions.Num();
	int32 NumSeqOutputs = ExistingExecChain ? TotalItems + 1 : TotalItems;

	FGraphNodeCreator<UK2Node_ExecutionSequence> SeqCreator(*Graph);
	UK2Node_ExecutionSequence* SeqNode = SeqCreator.CreateNode();
	SeqNode->NodePosX = 0;
	SeqNode->NodePosY = 0;
	SeqCreator.Finalize();

	while (SeqNode->Pins.Num() < NumSeqOutputs + 1)
	{
		SeqNode->AddInputPin();
	}

	FString SeqNodeId = IdPrefix + TEXT("Seq");
	FBlueprintGraphEditor::SetNodeId(SeqNode, SeqNodeId);
	Result.CreatedNodeIds.Add(SeqNodeId);

	UEdGraphPin* EventExecOut = FBlueprintGraphEditor::GetExecPin(EventNode, true);
	if (EventExecOut)
	{
		if (ExistingExecChain)
		{
			EventExecOut->BreakLinkTo(ExistingExecChain);
		}
		UEdGraphPin* SeqExecIn = FBlueprintGraphEditor::GetExecPin(SeqNode, false);
		if (SeqExecIn)
		{
			EventExecOut->MakeLinkTo(SeqExecIn);
		}
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	int32 SeqOutputIndex = 0;
	int32 ItemIndex = 0;
	int32 YOffset = 200;

	auto GetSeqOutputPin = [&](int32 Index) -> UEdGraphPin*
	{
		FString PinName = FString::Printf(TEXT("then_%d"), Index);
		return FBlueprintGraphEditor::FindPinByName(SeqNode, PinName, EGPD_Output);
	};

	auto CreatePrintSubgraph = [&](const FString& ItemLabel, const FString& VarDisplayName, UEdGraphNode* DataSourceNode, const FString& DataPinName) -> bool
	{
		UEdGraphPin* SeqOut = GetSeqOutputPin(SeqOutputIndex);
		if (!SeqOut)
		{
			Result.Error = FString::Printf(TEXT("Sequence output pin %d not found"), SeqOutputIndex);
			return false;
		}

		int32 NodeX = 300;
		int32 NodeY = SeqOutputIndex * YOffset;

		UFunction* ConcatFunc = UKismetStringLibrary::StaticClass()->FindFunctionByName(FName("Concat_StrStr"));
		if (!ConcatFunc)
		{
			Result.Error = TEXT("Could not find Concat_StrStr function");
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> ConcatCreator(*Graph);
		UK2Node_CallFunction* ConcatNode = ConcatCreator.CreateNode();
		ConcatNode->SetFromFunction(ConcatFunc);
		ConcatNode->NodePosX = NodeX;
		ConcatNode->NodePosY = NodeY;
		ConcatCreator.Finalize();

		FString ConcatNodeId = FString::Printf(TEXT("%sConcat_%d"), *IdPrefix, ItemIndex);
		FBlueprintGraphEditor::SetNodeId(ConcatNode, ConcatNodeId);
		Result.CreatedNodeIds.Add(ConcatNodeId);

		FString PrefixStr = FString::Printf(TEXT("[%s] %s: "), *Config.Label, *VarDisplayName);
		UEdGraphPin* PinA = FBlueprintGraphEditor::FindPinByName(ConcatNode, TEXT("A"), EGPD_Input);
		if (PinA)
		{
			K2Schema->TrySetDefaultValue(*PinA, PrefixStr);
		}

		UEdGraphPin* PinB = FBlueprintGraphEditor::FindPinByName(ConcatNode, TEXT("B"), EGPD_Input);
		if (PinB && DataSourceNode)
		{
			UEdGraphPin* DataOut = FBlueprintGraphEditor::FindPinByName(DataSourceNode, DataPinName, EGPD_Output);
			if (DataOut)
			{
				K2Schema->TryCreateConnection(DataOut, PinB);
			}
		}

		UFunction* PrintFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName("PrintString"));
		if (!PrintFunc)
		{
			Result.Error = TEXT("Could not find PrintString function");
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> PrintCreator(*Graph);
		UK2Node_CallFunction* PrintNode = PrintCreator.CreateNode();
		PrintNode->SetFromFunction(PrintFunc);
		PrintNode->NodePosX = NodeX + 400;
		PrintNode->NodePosY = NodeY;
		PrintCreator.Finalize();

		FString PrintNodeId = FString::Printf(TEXT("%sPrint_%d"), *IdPrefix, ItemIndex);
		FBlueprintGraphEditor::SetNodeId(PrintNode, PrintNodeId);
		Result.CreatedNodeIds.Add(PrintNodeId);

		UEdGraphPin* PrintExecIn = FBlueprintGraphEditor::GetExecPin(PrintNode, false);
		if (PrintExecIn)
		{
			SeqOut->MakeLinkTo(PrintExecIn);
		}

		UEdGraphPin* ConcatReturnPin = FBlueprintGraphEditor::FindPinByName(ConcatNode, TEXT("ReturnValue"), EGPD_Output);
		UEdGraphPin* PrintInString = FBlueprintGraphEditor::FindPinByName(PrintNode, TEXT("InString"), EGPD_Input);
		if (ConcatReturnPin && PrintInString)
		{
			K2Schema->TryCreateConnection(ConcatReturnPin, PrintInString);
		}

		UEdGraphPin* PrintToScreenPin = FBlueprintGraphEditor::FindPinByName(PrintNode, TEXT("bPrintToScreen"), EGPD_Input);
		if (PrintToScreenPin)
		{
			K2Schema->TrySetDefaultValue(*PrintToScreenPin, Config.bPrintToScreen ? TEXT("true") : TEXT("false"));
		}

		UEdGraphPin* PrintToLogPin = FBlueprintGraphEditor::FindPinByName(PrintNode, TEXT("bPrintToLog"), EGPD_Input);
		if (PrintToLogPin)
		{
			K2Schema->TrySetDefaultValue(*PrintToLogPin, Config.bPrintToLog ? TEXT("true") : TEXT("false"));
		}

		SeqOutputIndex++;
		ItemIndex++;
		return true;
	};

	for (const FString& VarName : Config.Variables)
	{
		int32 NodeY = SeqOutputIndex * YOffset;

		FName VarFName(*VarName);
		bool bFound = false;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == VarFName)
			{
				bFound = true;
				break;
			}
		}
		if (!bFound && Blueprint->ParentClass)
		{
			bFound = (Blueprint->ParentClass->FindPropertyByName(VarFName) != nullptr);
		}

		if (!bFound)
		{
			Result.Error = FString::Printf(TEXT("Variable '%s' not found in Blueprint or parent class"), *VarName);
			return Result;
		}

		FGraphNodeCreator<UK2Node_VariableGet> GetCreator(*Graph);
		UK2Node_VariableGet* GetNode = GetCreator.CreateNode();
		GetNode->VariableReference.SetSelfMember(VarFName);
		GetNode->NodePosX = 100;
		GetNode->NodePosY = NodeY;
		GetCreator.Finalize();

		FString GetNodeId = FString::Printf(TEXT("%sGet_%s_%d"), *IdPrefix, *VarName, ItemIndex);
		FBlueprintGraphEditor::SetNodeId(GetNode, GetNodeId);
		Result.CreatedNodeIds.Add(GetNodeId);

		FString OutputPinName;
		for (UEdGraphPin* Pin : GetNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				OutputPinName = Pin->PinName.ToString();
				break;
			}
		}

		if (!CreatePrintSubgraph(VarName, VarName, GetNode, OutputPinName))
		{
			return Result;
		}
	}

	for (const FDebugPrintFunctionItem& FuncItem : Config.Functions)
	{
		int32 NodeY = SeqOutputIndex * YOffset;

		UClass* FuncOwner = nullptr;
		if (!FuncItem.TargetClass.IsEmpty())
		{
			FuncOwner = FindObject<UClass>(nullptr, *FuncItem.TargetClass);
			if (!FuncOwner)
			{
				if (FuncItem.TargetClass.Equals(TEXT("AnimInstance"), ESearchCase::IgnoreCase))
				{
					FuncOwner = UAnimInstance::StaticClass();
				}
				else if (FuncItem.TargetClass.Equals(TEXT("KismetSystemLibrary"), ESearchCase::IgnoreCase))
				{
					FuncOwner = UKismetSystemLibrary::StaticClass();
				}
				else if (FuncItem.TargetClass.Equals(TEXT("KismetStringLibrary"), ESearchCase::IgnoreCase))
				{
					FuncOwner = UKismetStringLibrary::StaticClass();
				}
			}
		}

		UFunction* Function = nullptr;
		if (FuncOwner)
		{
			Function = FuncOwner->FindFunctionByName(FName(*FuncItem.FunctionName));
		}
		if (!Function)
		{
			Function = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(*FuncItem.FunctionName));
		}

		if (!Function)
		{
			Result.Error = FString::Printf(TEXT("Function '%s' not found"), *FuncItem.FunctionName);
			return Result;
		}

		FGraphNodeCreator<UK2Node_CallFunction> FuncCreator(*Graph);
		UK2Node_CallFunction* FuncNode = FuncCreator.CreateNode();
		FuncNode->SetFromFunction(Function);
		FuncNode->NodePosX = 100;
		FuncNode->NodePosY = NodeY;
		FuncCreator.Finalize();

		FString FuncNodeId = FString::Printf(TEXT("%sFunc_%s_%d"), *IdPrefix, *FuncItem.FunctionName, ItemIndex);
		FBlueprintGraphEditor::SetNodeId(FuncNode, FuncNodeId);
		Result.CreatedNodeIds.Add(FuncNodeId);

		for (const auto& Param : FuncItem.Params)
		{
			UEdGraphPin* ParamPin = FBlueprintGraphEditor::FindPinByName(FuncNode, Param.Key, EGPD_Input);
			if (ParamPin)
			{
				K2Schema->TrySetDefaultValue(*ParamPin, Param.Value);
			}
		}

		FString OutputPinName = TEXT("ReturnValue");
		for (UEdGraphPin* Pin : FuncNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& Pin->PinName != TEXT("self"))
			{
				OutputPinName = Pin->PinName.ToString();
				break;
			}
		}

		FString DisplayLabel = FuncItem.Label.IsEmpty() ? FuncItem.FunctionName : FuncItem.Label;
		if (!CreatePrintSubgraph(FuncItem.FunctionName, DisplayLabel, FuncNode, OutputPinName))
		{
			return Result;
		}
	}

	if (ExistingExecChain)
	{
		UEdGraphPin* LastSeqOut = GetSeqOutputPin(SeqOutputIndex);
		if (LastSeqOut)
		{
			LastSeqOut->MakeLinkTo(ExistingExecChain);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	Result.bSuccess = true;
	UE_LOG(LogUnrealClaude, Log, TEXT("AddDebugPrint: Created %d nodes for label '%s' on '%s'"),
		Result.CreatedNodeIds.Num(), *Config.Label, *Blueprint->GetName());

	return Result;
}

FDebugPrintResult FDebugPrintBuilder::RemoveDebugPrint(UBlueprint* Blueprint, const FString& Label)
{
	FDebugPrintResult Result;

	if (!Blueprint)
	{
		Result.Error = TEXT("Blueprint is null");
		return Result;
	}

	if (Label.IsEmpty())
	{
		Result.Error = TEXT("Label is required");
		return Result;
	}

	FString GraphError;
	UEdGraph* Graph = FBlueprintGraphEditor::FindGraph(Blueprint, TEXT(""), false, GraphError);
	if (!Graph)
	{
		Result.Error = FString::Printf(TEXT("Could not find event graph: %s"), *GraphError);
		return Result;
	}

	FString IdPrefix = MakeNodeIdPrefix(Label);

	UEdGraphNode* SeqNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		FString NodeId = FBlueprintGraphEditor::GetNodeId(Node);
		if (NodeId == IdPrefix + TEXT("Seq"))
		{
			SeqNode = Node;
			break;
		}
	}

	if (SeqNode)
	{
		UEdGraphPin* SeqExecIn = FBlueprintGraphEditor::GetExecPin(SeqNode, false);
		UEdGraphPin* UpstreamExecOut = nullptr;
		UEdGraphPin* DownstreamExecIn = nullptr;

		if (SeqExecIn && SeqExecIn->LinkedTo.Num() > 0)
		{
			UpstreamExecOut = SeqExecIn->LinkedTo[0];
		}

		for (UEdGraphPin* Pin : SeqNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output
				&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
				&& Pin->LinkedTo.Num() > 0)
			{
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					FString LinkedNodeId = FBlueprintGraphEditor::GetNodeId(LinkedPin->GetOwningNode());
					if (!LinkedNodeId.StartsWith(IdPrefix))
					{
						DownstreamExecIn = LinkedPin;
						break;
					}
				}
				if (DownstreamExecIn)
				{
					break;
				}
			}
		}

		Result.RemovedCount = RemoveNodesByPrefix(Graph, IdPrefix);

		if (UpstreamExecOut && DownstreamExecIn)
		{
			UpstreamExecOut->MakeLinkTo(DownstreamExecIn);
		}
	}
	else
	{
		Result.RemovedCount = RemoveNodesByPrefix(Graph, IdPrefix);
	}

	if (Result.RemovedCount > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	Result.bSuccess = true;
	UE_LOG(LogUnrealClaude, Log, TEXT("RemoveDebugPrint: Removed %d nodes for label '%s' from '%s'"),
		Result.RemovedCount, *Label, *Blueprint->GetName());

	return Result;
}
