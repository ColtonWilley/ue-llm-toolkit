// Copyright Natali Caggiano. All Rights Reserved.

#include "ControlRigEditor.h"
#include "ControlRigBlueprintLegacy.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/Nodes/RigVMTemplateNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMLink.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMVariableDescription.h"
#include "Rigs/RigHierarchy.h"

// ============================================================================
// Type Object Path Mapping
// ============================================================================

static const TMap<FString, FString> GTypeObjectPaths = {
	{ TEXT("bool"), TEXT("") },
	{ TEXT("float"), TEXT("") },
	{ TEXT("double"), TEXT("") },
	{ TEXT("int32"), TEXT("") },
	{ TEXT("FName"), TEXT("") },
	{ TEXT("FString"), TEXT("") },
	{ TEXT("FText"), TEXT("") },
	{ TEXT("FVector"), TEXT("/Script/CoreUObject.Vector") },
	{ TEXT("FRotator"), TEXT("/Script/CoreUObject.Rotator") },
	{ TEXT("FTransform"), TEXT("/Script/CoreUObject.Transform") },
	{ TEXT("FQuat"), TEXT("/Script/CoreUObject.Quat") },
	{ TEXT("FLinearColor"), TEXT("/Script/CoreUObject.LinearColor") },
	{ TEXT("FRigElementKey"), TEXT("/Script/ControlRig.RigElementKey") },
	{ TEXT("FVector2D"), TEXT("/Script/CoreUObject.Vector2D") },
};

// ============================================================================
// Asset Loading
// ============================================================================

UControlRigBlueprint* FControlRigEditor::LoadControlRig(const FString& AssetPath, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UControlRigBlueprint::StaticClass(), nullptr, *AssetPath);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load Control Rig: %s"), *AssetPath);
		return nullptr;
	}

	UControlRigBlueprint* RigBP = Cast<UControlRigBlueprint>(Loaded);
	if (!RigBP)
	{
		OutError = FString::Printf(TEXT("Asset is not a ControlRigBlueprint: %s (is %s)"),
			*AssetPath, *Loaded->GetClass()->GetName());
		return nullptr;
	}

	return RigBP;
}

URigVMGraph* FControlRigEditor::GetDefaultGraph(UControlRigBlueprint* RigBlueprint)
{
	if (!RigBlueprint)
	{
		return nullptr;
	}

	// Try GetDefaultModel first (UE5.7 API)
	URigVMGraph* Graph = RigBlueprint->GetDefaultModel();
	if (Graph)
	{
		return Graph;
	}

	// Fallback: get all models, use first
	TArray<URigVMGraph*> Models = RigBlueprint->GetAllModels();
	if (Models.Num() > 0)
	{
		return Models[0];
	}

	return nullptr;
}

URigVMController* FControlRigEditor::GetController(UControlRigBlueprint* RigBlueprint, URigVMGraph* Graph, FString& OutError)
{
	if (!RigBlueprint || !Graph)
	{
		OutError = TEXT("Invalid blueprint or graph");
		return nullptr;
	}

	URigVMController* Controller = RigBlueprint->GetController(Graph);
	if (!Controller)
	{
		Controller = RigBlueprint->GetOrCreateController(Graph);
	}
	if (!Controller)
	{
		OutError = TEXT("Cannot obtain RigVM controller for graph");
		return nullptr;
	}

	return Controller;
}

// ============================================================================
// Read: SerializeGraph
// ============================================================================

TSharedPtr<FJsonObject> FControlRigEditor::SerializeGraph(UControlRigBlueprint* RigBlueprint)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!RigBlueprint)
	{
		Result->SetStringField(TEXT("error"), TEXT("Null blueprint"));
		return Result;
	}

	Result->SetStringField(TEXT("name"), RigBlueprint->GetName());
	Result->SetStringField(TEXT("path"), RigBlueprint->GetPathName());

	// Collect all graphs/models
	TArray<URigVMGraph*> Models = RigBlueprint->GetAllModels();
	TArray<TSharedPtr<FJsonValue>> GraphsArray;

	// Also build a human-readable summary
	FString Summary;
	Summary += FString::Printf(TEXT("=== Control Rig: %s ===\n"), *RigBlueprint->GetName());
	Summary += FString::Printf(TEXT("Asset Path: %s\n\n"), *RigBlueprint->GetPathName());

	for (URigVMGraph* Graph : Models)
	{
		if (!Graph)
		{
			continue;
		}

		TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();
		GraphJson->SetStringField(TEXT("name"), Graph->GetGraphName());

		// Nodes
		TArray<TSharedPtr<FJsonValue>> NodesArray;
		Summary += FString::Printf(TEXT("--- Graph: %s ---\n\n"), *Graph->GetGraphName());

		TArray<URigVMNode*> Nodes = Graph->GetNodes();
		for (URigVMNode* Node : Nodes)
		{
			if (!Node)
			{
				continue;
			}

			NodesArray.Add(MakeShared<FJsonValueObject>(SerializeNode(Node)));

			// Summary text
			FString NodeTitle = Node->GetNodeTitle();
			FString NodePath = Node->GetNodePath();
			FString TypeName;

			URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(Node);
			UScriptStruct* ScriptStruct = TemplateNode ? TemplateNode->GetScriptStruct() : nullptr;
			if (ScriptStruct)
			{
				TypeName = ScriptStruct->GetName();
			}

			Summary += FString::Printf(TEXT("[Node] %s"), *NodeTitle);
			if (!TypeName.IsEmpty())
			{
				Summary += FString::Printf(TEXT(" (%s)"), *TypeName);
			}
			Summary += TEXT("\n");
			Summary += FString::Printf(TEXT("  Path: %s\n"), *NodePath);

			// Pins
			TArray<URigVMPin*> Pins = Node->GetPins();
			for (URigVMPin* Pin : Pins)
			{
				if (!Pin)
				{
					continue;
				}

				FString DirStr = PinDirectionToString(static_cast<int32>(Pin->GetDirection()));
				FString PinName = Pin->GetName();
				FString CppType = Pin->GetCPPType();
				FString DefaultVal = Pin->GetDefaultValue();

				Summary += FString::Printf(TEXT("  %s %s: %s"), *DirStr, *PinName, *CppType);
				if (!DefaultVal.IsEmpty() && DefaultVal != TEXT("()"))
				{
					Summary += FString::Printf(TEXT(" = %s"), *DefaultVal);
				}
				Summary += TEXT("\n");
			}
			Summary += TEXT("\n");
		}

		GraphJson->SetArrayField(TEXT("nodes"), NodesArray);
		GraphJson->SetNumberField(TEXT("node_count"), Nodes.Num());

		// Links
		TArray<URigVMLink*> Links = Graph->GetLinks();
		TArray<TSharedPtr<FJsonValue>> LinksArray;

		if (Links.Num() > 0)
		{
			Summary += TEXT("--- Links ---\n");
		}

		for (URigVMLink* Link : Links)
		{
			if (!Link)
			{
				continue;
			}

			URigVMPin* SourcePin = Link->GetSourcePin();
			URigVMPin* TargetPin = Link->GetTargetPin();
			if (SourcePin && TargetPin)
			{
				TSharedPtr<FJsonObject> LinkJson = MakeShared<FJsonObject>();
				LinkJson->SetStringField(TEXT("source"), SourcePin->GetPinPath());
				LinkJson->SetStringField(TEXT("target"), TargetPin->GetPinPath());
				LinksArray.Add(MakeShared<FJsonValueObject>(LinkJson));

				Summary += FString::Printf(TEXT("%s -> %s\n"),
					*SourcePin->GetPinPath(), *TargetPin->GetPinPath());
			}
		}

		GraphJson->SetArrayField(TEXT("links"), LinksArray);
		GraphJson->SetNumberField(TEXT("link_count"), Links.Num());

		if (Links.Num() > 0)
		{
			Summary += TEXT("\n");
		}

		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphJson));
	}

	Result->SetArrayField(TEXT("graphs"), GraphsArray);
	Result->SetNumberField(TEXT("graph_count"), Models.Num());
	Result->SetStringField(TEXT("summary"), Summary);

	return Result;
}

// ============================================================================
// Read: SerializeHierarchy
// ============================================================================

TSharedPtr<FJsonObject> FControlRigEditor::SerializeHierarchy(UControlRigBlueprint* RigBlueprint)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!RigBlueprint)
	{
		Result->SetStringField(TEXT("error"), TEXT("Null blueprint"));
		return Result;
	}

	URigHierarchy* Hierarchy = RigBlueprint->Hierarchy;
	if (!Hierarchy)
	{
		Result->SetStringField(TEXT("error"), TEXT("Hierarchy not available"));
		return Result;
	}

	TArray<FRigElementKey> AllKeys = Hierarchy->GetAllKeys();

	// Group by element type
	TMap<FString, TArray<FString>> GroupedElements;
	TArray<TSharedPtr<FJsonValue>> ElementsArray;

	for (const FRigElementKey& Key : AllKeys)
	{
		FString TypeLabel;
		switch (Key.Type)
		{
		case ERigElementType::Bone:      TypeLabel = TEXT("Bone"); break;
		case ERigElementType::Control:   TypeLabel = TEXT("Control"); break;
		case ERigElementType::Null:      TypeLabel = TEXT("Null"); break;
		case ERigElementType::Curve:     TypeLabel = TEXT("Curve"); break;
		case ERigElementType::Connector: TypeLabel = TEXT("Connector"); break;
		case ERigElementType::Socket:    TypeLabel = TEXT("Socket"); break;
		case ERigElementType::Physics:   TypeLabel = TEXT("Physics"); break;
		case ERigElementType::Reference: TypeLabel = TEXT("Reference"); break;
		default:                         TypeLabel = TEXT("Unknown"); break;
		}

		GroupedElements.FindOrAdd(TypeLabel).Add(Key.Name.ToString());

		TSharedPtr<FJsonObject> ElemJson = MakeShared<FJsonObject>();
		ElemJson->SetStringField(TEXT("type"), TypeLabel);
		ElemJson->SetStringField(TEXT("name"), Key.Name.ToString());
		ElementsArray.Add(MakeShared<FJsonValueObject>(ElemJson));
	}

	Result->SetArrayField(TEXT("elements"), ElementsArray);
	Result->SetNumberField(TEXT("total_elements"), AllKeys.Num());

	// Summary counts per type
	TSharedPtr<FJsonObject> CountsJson = MakeShared<FJsonObject>();
	for (const auto& Pair : GroupedElements)
	{
		CountsJson->SetNumberField(Pair.Key, Pair.Value.Num());
	}
	Result->SetObjectField(TEXT("counts"), CountsJson);

	// Human-readable summary
	FString Summary = TEXT("--- Hierarchy ---\n");
	for (const FRigElementKey& Key : AllKeys)
	{
		FString TypeLabel;
		switch (Key.Type)
		{
		case ERigElementType::Bone:      TypeLabel = TEXT("Bone"); break;
		case ERigElementType::Control:   TypeLabel = TEXT("Control"); break;
		case ERigElementType::Null:      TypeLabel = TEXT("Null"); break;
		case ERigElementType::Curve:     TypeLabel = TEXT("Curve"); break;
		case ERigElementType::Connector: TypeLabel = TEXT("Connector"); break;
		case ERigElementType::Socket:    TypeLabel = TEXT("Socket"); break;
		case ERigElementType::Physics:   TypeLabel = TEXT("Physics"); break;
		case ERigElementType::Reference: TypeLabel = TEXT("Reference"); break;
		default:                         TypeLabel = TEXT("Unknown"); break;
		}
		Summary += FString::Printf(TEXT("  %s: %s\n"), *TypeLabel, *Key.Name.ToString());
	}

	Result->SetStringField(TEXT("summary"), Summary);

	return Result;
}

// ============================================================================
// Read: ListStructs
// ============================================================================

TSharedPtr<FJsonObject> FControlRigEditor::ListStructs(URigVMController* Controller, const FString& Filter)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!Controller)
	{
		Result->SetStringField(TEXT("error"), TEXT("Null controller"));
		return Result;
	}

	// Get registered unit structs (static method on controller in UE 5.7)
	TArray<UScriptStruct*> Structs = URigVMController::GetRegisteredUnitStructs();

	TArray<TSharedPtr<FJsonValue>> StructsArray;
	int32 Count = 0;

	for (UScriptStruct* Struct : Structs)
	{
		if (!Struct)
		{
			continue;
		}

		FString Name = Struct->GetName();
		FString Path = Struct->GetPathName();

		if (!Filter.IsEmpty())
		{
			if (!Name.Contains(Filter, ESearchCase::IgnoreCase) &&
				!Path.Contains(Filter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> StructJson = MakeShared<FJsonObject>();
		StructJson->SetStringField(TEXT("name"), Name);
		StructJson->SetStringField(TEXT("path"), Path);
		StructsArray.Add(MakeShared<FJsonValueObject>(StructJson));
		Count++;

		if (Count >= 500)
		{
			break;
		}
	}

	Result->SetArrayField(TEXT("structs"), StructsArray);
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetNumberField(TEXT("total_available"), Structs.Num());
	if (Count >= 500)
	{
		Result->SetBoolField(TEXT("truncated"), true);
	}

	return Result;
}

// ============================================================================
// Read: ListTemplates
// ============================================================================

TSharedPtr<FJsonObject> FControlRigEditor::ListTemplates(URigVMController* Controller, const FString& Filter)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!Controller)
	{
		Result->SetStringField(TEXT("error"), TEXT("Null controller"));
		return Result;
	}

	// Get registered templates (static method on controller in UE 5.7)
	TArray<FString> Templates = URigVMController::GetRegisteredTemplates();

	TArray<TSharedPtr<FJsonValue>> TemplatesArray;
	int32 Count = 0;

	for (const FString& Template : Templates)
	{
		if (!Filter.IsEmpty() && !Template.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TemplatesArray.Add(MakeShared<FJsonValueString>(Template));
		Count++;

		if (Count >= 500)
		{
			break;
		}
	}

	Result->SetArrayField(TEXT("templates"), TemplatesArray);
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetNumberField(TEXT("total_available"), Templates.Num());
	if (Count >= 500)
	{
		Result->SetBoolField(TEXT("truncated"), true);
	}

	return Result;
}

// ============================================================================
// Write Operations
// ============================================================================

TSharedPtr<FJsonObject> FControlRigEditor::AddNode(URigVMController* Controller,
	const FString& StructPath, const FString& NodeName, float X, float Y)
{
	if (!Controller)
	{
		return ErrorResult(TEXT("Null controller"));
	}

	URigVMNode* Node = Controller->AddUnitNodeFromStructPath(
		StructPath, TEXT("Execute"), FVector2D(X, Y), NodeName);

	if (Node)
	{
		return NodeResult(TEXT("Added unit node"), Node);
	}
	return ErrorResult(FString::Printf(TEXT("Failed to add node (struct: %s)"), *StructPath));
}

TSharedPtr<FJsonObject> FControlRigEditor::AddTemplateNode(URigVMController* Controller,
	const FString& Notation, const FString& NodeName, float X, float Y)
{
	if (!Controller)
	{
		return ErrorResult(TEXT("Null controller"));
	}

	URigVMNode* Node = Controller->AddTemplateNode(FName(*Notation), FVector2D(X, Y), NodeName);
	if (Node)
	{
		return NodeResult(TEXT("Added template node"), Node);
	}
	return ErrorResult(FString::Printf(TEXT("Failed to add template (notation: %s)"), *Notation));
}

TSharedPtr<FJsonObject> FControlRigEditor::AddVariableNode(UControlRigBlueprint* RigBlueprint,
	URigVMController* Controller, const FString& VarName, bool bIsGetter,
	const FString& CppType, float X, float Y)
{
	if (!Controller || !RigBlueprint)
	{
		return ErrorResult(TEXT("Null controller or blueprint"));
	}

	FString ActualCppType = CppType;
	FString ObjectPath;

	if (ActualCppType.IsEmpty())
	{
		if (!GuessMemberVarType(RigBlueprint, VarName, ActualCppType, ObjectPath))
		{
			return ErrorResult(FString::Printf(
				TEXT("Cannot determine type for '%s'. Pass cpp_type parameter (e.g. FTransform, float, FVector)"),
				*VarName));
		}
	}
	else
	{
		ObjectPath = GetTypeObjectPath(ActualCppType);
	}

	FString Suffix = bIsGetter ? TEXT("Get") : TEXT("Set");
	FString NodeName = FString::Printf(TEXT("%s_%s"), *VarName, *Suffix);

	URigVMNode* Node = Controller->AddVariableNodeFromObjectPath(
		FName(*VarName), ActualCppType, ObjectPath, bIsGetter, FString(),
		FVector2D(X, Y), NodeName);

	if (Node)
	{
		FString Label = bIsGetter ? TEXT("Added variable getter") : TEXT("Added variable setter");
		return NodeResult(Label, Node);
	}
	return ErrorResult(FString::Printf(TEXT("Failed to add variable node for '%s' (type: %s)"),
		*VarName, *ActualCppType));
}

TSharedPtr<FJsonObject> FControlRigEditor::RemoveNode(URigVMController* Controller, const FString& NodeName)
{
	if (!Controller)
	{
		return ErrorResult(TEXT("Null controller"));
	}

	bool bOK = Controller->RemoveNodeByName(*NodeName);
	if (bOK)
	{
		return SuccessResult(FString::Printf(TEXT("Removed node '%s'"), *NodeName));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to remove '%s' (not found or protected)"), *NodeName));
}

TSharedPtr<FJsonObject> FControlRigEditor::AddLink(URigVMController* Controller,
	const FString& OutputPinPath, const FString& InputPinPath)
{
	if (!Controller)
	{
		return ErrorResult(TEXT("Null controller"));
	}

	bool bOK = Controller->AddLink(OutputPinPath, InputPinPath);
	if (bOK)
	{
		return SuccessResult(FString::Printf(TEXT("Linked %s -> %s"), *OutputPinPath, *InputPinPath));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to link %s -> %s"), *OutputPinPath, *InputPinPath));
}

TSharedPtr<FJsonObject> FControlRigEditor::BreakLink(URigVMController* Controller,
	const FString& OutputPinPath, const FString& InputPinPath)
{
	if (!Controller)
	{
		return ErrorResult(TEXT("Null controller"));
	}

	bool bOK = Controller->BreakLink(OutputPinPath, InputPinPath);
	if (bOK)
	{
		return SuccessResult(FString::Printf(TEXT("Unlinked %s -/-> %s"), *OutputPinPath, *InputPinPath));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to unlink %s -> %s"), *OutputPinPath, *InputPinPath));
}

TSharedPtr<FJsonObject> FControlRigEditor::BreakAllLinks(URigVMController* Controller,
	const FString& PinPath, bool bAsInput)
{
	if (!Controller)
	{
		return ErrorResult(TEXT("Null controller"));
	}

	bool bOK = Controller->BreakAllLinks(PinPath, bAsInput);
	if (bOK)
	{
		FString Side = bAsInput ? TEXT("input") : TEXT("output");
		return SuccessResult(FString::Printf(TEXT("Broke all %s links on %s"), *Side, *PinPath));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to break links on %s"), *PinPath));
}

TSharedPtr<FJsonObject> FControlRigEditor::SetPinDefault(URigVMController* Controller,
	const FString& PinPath, const FString& Value)
{
	if (!Controller)
	{
		return ErrorResult(TEXT("Null controller"));
	}

	bool bOK = Controller->SetPinDefaultValue(PinPath, Value, false);
	if (bOK)
	{
		return SuccessResult(FString::Printf(TEXT("%s = %s"), *PinPath, *Value));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to set default on %s"), *PinPath));
}

TSharedPtr<FJsonObject> FControlRigEditor::SetPinExpansion(URigVMController* Controller,
	const FString& PinPath, bool bExpanded)
{
	if (!Controller)
	{
		return ErrorResult(TEXT("Null controller"));
	}

	bool bOK = Controller->SetPinExpansion(PinPath, bExpanded);
	if (bOK)
	{
		FString State = bExpanded ? TEXT("Expanded") : TEXT("Collapsed");
		return SuccessResult(FString::Printf(TEXT("%s %s"), *State, *PinPath));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to set expansion on %s"), *PinPath));
}

TSharedPtr<FJsonObject> FControlRigEditor::AddMemberVariable(UControlRigBlueprint* RigBlueprint,
	const FString& Name, const FString& CppType, bool bIsPublic, const FString& DefaultValue)
{
	if (!RigBlueprint)
	{
		return ErrorResult(TEXT("Null blueprint"));
	}

	// Use the blueprint's AddMemberVariable method (returns FName in UE 5.7)
	FName ResultName = RigBlueprint->AddMemberVariable(FName(*Name), CppType, bIsPublic, false, DefaultValue);
	if (ResultName != NAME_None)
	{
		FString Visibility = bIsPublic ? TEXT("public") : TEXT("private");
		return SuccessResult(FString::Printf(TEXT("Added member variable '%s' (%s, %s)"),
			*Name, *CppType, *Visibility));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to add member variable '%s'"), *Name));
}

TSharedPtr<FJsonObject> FControlRigEditor::RemoveMemberVariable(UControlRigBlueprint* RigBlueprint,
	const FString& Name)
{
	if (!RigBlueprint)
	{
		return ErrorResult(TEXT("Null blueprint"));
	}

	bool bOK = RigBlueprint->RemoveMemberVariable(*Name);
	if (bOK)
	{
		return SuccessResult(FString::Printf(TEXT("Removed member variable '%s'"), *Name));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to remove variable '%s'"), *Name));
}

TSharedPtr<FJsonObject> FControlRigEditor::Recompile(UControlRigBlueprint* RigBlueprint)
{
	if (!RigBlueprint)
	{
		return ErrorResult(TEXT("Null blueprint"));
	}

	RigBlueprint->RecompileVM();
	return SuccessResult(TEXT("Recompiled RigVM"));
}

// ============================================================================
// Batch Execution
// ============================================================================

TSharedPtr<FJsonObject> FControlRigEditor::ExecuteBatch(UControlRigBlueprint* RigBlueprint,
	URigVMController* Controller, const TArray<TSharedPtr<FJsonValue>>& Operations)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!Controller || !RigBlueprint)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Null controller or blueprint"));
		return Result;
	}

	Controller->OpenUndoBracket(TEXT("CR Edit: batch"));

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 OKCount = 0;
	int32 ErrCount = 0;

	for (int32 i = 0; i < Operations.Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!Operations[i].IsValid() || Operations[i]->Type != EJson::Object)
		{
			TSharedPtr<FJsonObject> SkipResult = ErrorResult(
				FString::Printf(TEXT("[%d] not a JSON object"), i));
			ResultsArray.Add(MakeShared<FJsonValueObject>(SkipResult));
			ErrCount++;
			continue;
		}

		TSharedPtr<FJsonObject> OpData = Operations[i]->AsObject();
		FString OpName;
		if (!OpData->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
		{
			TSharedPtr<FJsonObject> SkipResult = ErrorResult(
				FString::Printf(TEXT("[%d] missing 'op' field"), i));
			ResultsArray.Add(MakeShared<FJsonValueObject>(SkipResult));
			ErrCount++;
			continue;
		}

		TSharedPtr<FJsonObject> OpResult = DispatchBatchOp(RigBlueprint, Controller, OpData);
		ResultsArray.Add(MakeShared<FJsonValueObject>(OpResult));

		bool bSuccess = false;
		OpResult->TryGetBoolField(TEXT("success"), bSuccess);
		if (bSuccess)
		{
			OKCount++;
		}
		else
		{
			ErrCount++;
		}
	}

	Controller->CloseUndoBracket();

	Result->SetBoolField(TEXT("success"), ErrCount == 0);
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("ok_count"), OKCount);
	Result->SetNumberField(TEXT("error_count"), ErrCount);
	Result->SetNumberField(TEXT("total"), Operations.Num());

	return Result;
}

TSharedPtr<FJsonObject> FControlRigEditor::DispatchBatchOp(UControlRigBlueprint* RigBlueprint,
	URigVMController* Controller, const TSharedPtr<FJsonObject>& OpData)
{
	FString OpName;
	OpData->TryGetStringField(TEXT("op"), OpName);
	OpName = OpName.ToLower();

	if (OpName == TEXT("add_node"))
	{
		FString Struct = OpData->GetStringField(TEXT("struct"));
		FString Name = OpData->GetStringField(TEXT("name"));
		double X = 0, Y = 0;
		OpData->TryGetNumberField(TEXT("x"), X);
		OpData->TryGetNumberField(TEXT("y"), Y);
		return AddNode(Controller, Struct, Name, static_cast<float>(X), static_cast<float>(Y));
	}
	else if (OpName == TEXT("add_template"))
	{
		FString Notation = OpData->GetStringField(TEXT("notation"));
		FString Name = OpData->GetStringField(TEXT("name"));
		double X = 0, Y = 0;
		OpData->TryGetNumberField(TEXT("x"), X);
		OpData->TryGetNumberField(TEXT("y"), Y);
		return AddTemplateNode(Controller, Notation, Name, static_cast<float>(X), static_cast<float>(Y));
	}
	else if (OpName == TEXT("add_var_node"))
	{
		FString VarName = OpData->GetStringField(TEXT("var"));
		FString Mode = OpData->GetStringField(TEXT("mode"));
		bool bIsGetter = Mode.ToLower() == TEXT("get") || Mode.ToLower() == TEXT("getter");
		FString CppType;
		OpData->TryGetStringField(TEXT("cpp_type"), CppType);
		double X = 0, Y = 0;
		OpData->TryGetNumberField(TEXT("x"), X);
		OpData->TryGetNumberField(TEXT("y"), Y);
		return AddVariableNode(RigBlueprint, Controller, VarName, bIsGetter, CppType,
			static_cast<float>(X), static_cast<float>(Y));
	}
	else if (OpName == TEXT("remove_node"))
	{
		return RemoveNode(Controller, OpData->GetStringField(TEXT("name")));
	}
	else if (OpName == TEXT("link"))
	{
		return AddLink(Controller,
			OpData->GetStringField(TEXT("source")),
			OpData->GetStringField(TEXT("target")));
	}
	else if (OpName == TEXT("unlink"))
	{
		return BreakLink(Controller,
			OpData->GetStringField(TEXT("source")),
			OpData->GetStringField(TEXT("target")));
	}
	else if (OpName == TEXT("unlink_all"))
	{
		FString Dir;
		OpData->TryGetStringField(TEXT("direction"), Dir);
		bool bAsInput = Dir.ToLower() == TEXT("in") || Dir.ToLower() == TEXT("input");
		return BreakAllLinks(Controller, OpData->GetStringField(TEXT("pin")), bAsInput);
	}
	else if (OpName == TEXT("set_default"))
	{
		return SetPinDefault(Controller,
			OpData->GetStringField(TEXT("pin")),
			OpData->GetStringField(TEXT("value")));
	}
	else if (OpName == TEXT("set_expand"))
	{
		bool bExpanded = true;
		OpData->TryGetBoolField(TEXT("expanded"), bExpanded);
		return SetPinExpansion(Controller, OpData->GetStringField(TEXT("pin")), bExpanded);
	}
	else if (OpName == TEXT("add_member_var"))
	{
		FString Name = OpData->GetStringField(TEXT("name"));
		FString Type = OpData->GetStringField(TEXT("type"));
		bool bPublic = true;
		OpData->TryGetBoolField(TEXT("public"), bPublic);
		FString DefaultVal;
		OpData->TryGetStringField(TEXT("default"), DefaultVal);
		return AddMemberVariable(RigBlueprint, Name, Type, bPublic, DefaultVal);
	}
	else if (OpName == TEXT("remove_member_var"))
	{
		return RemoveMemberVariable(RigBlueprint, OpData->GetStringField(TEXT("name")));
	}
	else if (OpName == TEXT("recompile"))
	{
		return Recompile(RigBlueprint);
	}
	else if (OpName == TEXT("list_structs"))
	{
		FString Filter;
		OpData->TryGetStringField(TEXT("filter"), Filter);
		return ListStructs(Controller, Filter);
	}
	else if (OpName == TEXT("list_templates"))
	{
		FString Filter;
		OpData->TryGetStringField(TEXT("filter"), Filter);
		return ListTemplates(Controller, Filter);
	}

	return ErrorResult(FString::Printf(TEXT("Unknown batch operation: '%s'"), *OpName));
}

// ============================================================================
// Private Helpers
// ============================================================================

TSharedPtr<FJsonObject> FControlRigEditor::SerializeNode(URigVMNode* Node)
{
	TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();

	NodeJson->SetStringField(TEXT("title"), Node->GetNodeTitle());
	NodeJson->SetStringField(TEXT("path"), Node->GetNodePath());

	URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(Node);
	UScriptStruct* ScriptStruct = TemplateNode ? TemplateNode->GetScriptStruct() : nullptr;
	if (ScriptStruct)
	{
		NodeJson->SetStringField(TEXT("struct_name"), ScriptStruct->GetName());
		NodeJson->SetStringField(TEXT("struct_path"), ScriptStruct->GetPathName());
	}

	// Serialize pins
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (URigVMPin* Pin : Node->GetPins())
	{
		if (!Pin)
		{
			continue;
		}

		TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();
		PinJson->SetStringField(TEXT("name"), Pin->GetName());
		PinJson->SetStringField(TEXT("direction"), PinDirectionToString(static_cast<int32>(Pin->GetDirection())));
		PinJson->SetStringField(TEXT("cpp_type"), Pin->GetCPPType());

		FString DefaultVal = Pin->GetDefaultValue();
		if (!DefaultVal.IsEmpty() && DefaultVal != TEXT("()"))
		{
			PinJson->SetStringField(TEXT("default_value"), DefaultVal);
		}

		PinsArray.Add(MakeShared<FJsonValueObject>(PinJson));
	}

	NodeJson->SetArrayField(TEXT("pins"), PinsArray);
	NodeJson->SetNumberField(TEXT("pin_count"), PinsArray.Num());

	return NodeJson;
}

TSharedPtr<FJsonObject> FControlRigEditor::NodeResult(const FString& Message, URigVMNode* Node)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);

	if (Node)
	{
		Result->SetObjectField(TEXT("node"), SerializeNode(Node));
	}

	return Result;
}

TSharedPtr<FJsonObject> FControlRigEditor::SuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FControlRigEditor::ErrorResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Message);
	return Result;
}

bool FControlRigEditor::GuessMemberVarType(UControlRigBlueprint* RigBlueprint,
	const FString& VarName, FString& OutCppType, FString& OutObjectPath)
{
	if (!RigBlueprint)
	{
		return false;
	}

	TArray<FRigVMGraphVariableDescription> Variables = RigBlueprint->GetMemberVariables();
	for (const FRigVMGraphVariableDescription& Var : Variables)
	{
		if (Var.Name.ToString() == VarName)
		{
			OutCppType = Var.CPPType;
			if (Var.CPPTypeObject)
			{
				OutObjectPath = Var.CPPTypeObject->GetPathName();
			}
			else
			{
				OutObjectPath = GetTypeObjectPath(OutCppType);
			}
			return true;
		}
	}

	return false;
}

FString FControlRigEditor::GetTypeObjectPath(const FString& CppType)
{
	const FString* Found = GTypeObjectPaths.Find(CppType);
	if (Found)
	{
		return *Found;
	}
	return FString();
}

FString FControlRigEditor::PinDirectionToString(int32 Direction)
{
	switch (static_cast<ERigVMPinDirection>(Direction))
	{
	case ERigVMPinDirection::Input:  return TEXT("IN ");
	case ERigVMPinDirection::Output: return TEXT("OUT");
	case ERigVMPinDirection::IO:     return TEXT("I/O");
	case ERigVMPinDirection::Hidden: return TEXT("HID");
	default:                         return TEXT("???");
	}
}
