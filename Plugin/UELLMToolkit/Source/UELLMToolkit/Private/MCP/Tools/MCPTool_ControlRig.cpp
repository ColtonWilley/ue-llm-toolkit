// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_ControlRig.h"
#include "ControlRigEditor.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMController.h"
#include "MCP/MCPParamValidator.h"

// Helper macro: load rig + get graph + get controller, or return error
#define LOAD_RIG_AND_CONTROLLER(RigPath, OutBP, OutGraph, OutController) \
	FString LoadError; \
	UControlRigBlueprint* OutBP = FControlRigEditor::LoadControlRig(RigPath, LoadError); \
	if (!OutBP) { return FMCPToolResult::Error(LoadError); } \
	URigVMGraph* OutGraph = FControlRigEditor::GetDefaultGraph(OutBP); \
	if (!OutGraph) { return FMCPToolResult::Error(TEXT("No graph model found in Control Rig")); } \
	FString CtrlError; \
	URigVMController* OutController = FControlRigEditor::GetController(OutBP, OutGraph, CtrlError); \
	if (!OutController) { return FMCPToolResult::Error(CtrlError); }

// Helper: convert utility result JSON to FMCPToolResult
static FMCPToolResult JsonResultToToolResult(const TSharedPtr<FJsonObject>& Result, const FString& SuccessContext)
{
	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);

	if (bSuccess)
	{
		FString Message;
		Result->TryGetStringField(TEXT("message"), Message);
		if (Message.IsEmpty())
		{
			Message = SuccessContext;
		}
		return FMCPToolResult::Success(Message, Result);
	}
	else
	{
		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		return FMCPToolResult::Error(Error.IsEmpty() ? TEXT("Unknown error") : Error);
	}
}

// ============================================================================
// Main Dispatch
// ============================================================================

FMCPToolResult FMCPTool_ControlRig::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString RigPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("rig_path"), RigPath, Error))
	{
		return Error.GetValue();
	}

	FString Operation;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	// Read operations
	if (Operation == TEXT("inspect"))
	{
		return HandleInspect(RigPath);
	}
	else if (Operation == TEXT("list_structs"))
	{
		return HandleListStructs(RigPath, Params);
	}
	else if (Operation == TEXT("list_templates"))
	{
		return HandleListTemplates(RigPath, Params);
	}
	// Write operations
	else if (Operation == TEXT("add_node"))
	{
		return HandleAddNode(RigPath, Params);
	}
	else if (Operation == TEXT("add_template"))
	{
		return HandleAddTemplate(RigPath, Params);
	}
	else if (Operation == TEXT("add_var_node"))
	{
		return HandleAddVarNode(RigPath, Params);
	}
	else if (Operation == TEXT("remove_node"))
	{
		return HandleRemoveNode(RigPath, Params);
	}
	else if (Operation == TEXT("link"))
	{
		return HandleLink(RigPath, Params);
	}
	else if (Operation == TEXT("unlink"))
	{
		return HandleUnlink(RigPath, Params);
	}
	else if (Operation == TEXT("unlink_all"))
	{
		return HandleUnlinkAll(RigPath, Params);
	}
	else if (Operation == TEXT("set_default"))
	{
		return HandleSetDefault(RigPath, Params);
	}
	else if (Operation == TEXT("set_expand"))
	{
		return HandleSetExpand(RigPath, Params);
	}
	else if (Operation == TEXT("add_member_var"))
	{
		return HandleAddMemberVar(RigPath, Params);
	}
	else if (Operation == TEXT("remove_member_var"))
	{
		return HandleRemoveMemberVar(RigPath, Params);
	}
	else if (Operation == TEXT("recompile"))
	{
		return HandleRecompile(RigPath);
	}
	else if (Operation == TEXT("batch"))
	{
		return HandleBatch(RigPath, Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid: inspect, list_structs, list_templates, add_node, add_template, add_var_node, remove_node, link, unlink, unlink_all, set_default, set_expand, add_member_var, remove_member_var, recompile, batch"),
		*Operation));
}

// ============================================================================
// Read Operations
// ============================================================================

FMCPToolResult FMCPTool_ControlRig::HandleInspect(const FString& RigPath)
{
	FString LoadError;
	UControlRigBlueprint* RigBP = FControlRigEditor::LoadControlRig(RigPath, LoadError);
	if (!RigBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TSharedPtr<FJsonObject> GraphData = FControlRigEditor::SerializeGraph(RigBP);
	TSharedPtr<FJsonObject> HierarchyData = FControlRigEditor::SerializeHierarchy(RigBP);

	// Combine into single response
	TSharedPtr<FJsonObject> ResponseData = MakeShared<FJsonObject>();
	ResponseData->SetObjectField(TEXT("graph"), GraphData);
	ResponseData->SetObjectField(TEXT("hierarchy"), HierarchyData);

	// Build combined summary
	FString GraphSummary, HierarchySummary;
	GraphData->TryGetStringField(TEXT("summary"), GraphSummary);
	HierarchyData->TryGetStringField(TEXT("summary"), HierarchySummary);
	ResponseData->SetStringField(TEXT("summary"), GraphSummary + TEXT("\n") + HierarchySummary);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Control Rig inspection: %s"), *RigBP->GetName()),
		ResponseData);
}

FMCPToolResult FMCPTool_ControlRig::HandleListStructs(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString Filter = ExtractOptionalString(Params, TEXT("filter"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::ListStructs(Controller, Filter);

	FString ErrorMsg;
	if (Result->TryGetStringField(TEXT("error"), ErrorMsg))
	{
		return FMCPToolResult::Error(ErrorMsg);
	}

	int32 Count = static_cast<int32>(Result->GetNumberField(TEXT("count")));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d structs"), Count),
		Result);
}

FMCPToolResult FMCPTool_ControlRig::HandleListTemplates(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString Filter = ExtractOptionalString(Params, TEXT("filter"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::ListTemplates(Controller, Filter);

	FString ErrorMsg;
	if (Result->TryGetStringField(TEXT("error"), ErrorMsg))
	{
		return FMCPToolResult::Error(ErrorMsg);
	}

	int32 Count = static_cast<int32>(Result->GetNumberField(TEXT("count")));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d templates"), Count),
		Result);
}

// ============================================================================
// Write Operations
// ============================================================================

FMCPToolResult FMCPTool_ControlRig::HandleAddNode(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString StructPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("struct_path"), StructPath, Error))
	{
		return Error.GetValue();
	}

	FString NodeName = ExtractOptionalString(Params, TEXT("node_name"), TEXT(""));
	float X = ExtractOptionalNumber<float>(Params, TEXT("x"), 0.f);
	float Y = ExtractOptionalNumber<float>(Params, TEXT("y"), 0.f);

	Controller->OpenUndoBracket(TEXT("CR Edit: add_node"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::AddNode(Controller, StructPath, NodeName, X, Y);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("Node added"));
}

FMCPToolResult FMCPTool_ControlRig::HandleAddTemplate(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString Notation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("notation"), Notation, Error))
	{
		return Error.GetValue();
	}

	FString NodeName = ExtractOptionalString(Params, TEXT("node_name"), TEXT(""));
	float X = ExtractOptionalNumber<float>(Params, TEXT("x"), 0.f);
	float Y = ExtractOptionalNumber<float>(Params, TEXT("y"), 0.f);

	Controller->OpenUndoBracket(TEXT("CR Edit: add_template"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::AddTemplateNode(Controller, Notation, NodeName, X, Y);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("Template node added"));
}

FMCPToolResult FMCPTool_ControlRig::HandleAddVarNode(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString VarName;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("var_name"), VarName, Error))
	{
		return Error.GetValue();
	}

	bool bIsGetter = ExtractOptionalBool(Params, TEXT("is_getter"), true);
	FString CppType = ExtractOptionalString(Params, TEXT("cpp_type"));
	float X = ExtractOptionalNumber<float>(Params, TEXT("x"), 0.f);
	float Y = ExtractOptionalNumber<float>(Params, TEXT("y"), 0.f);

	Controller->OpenUndoBracket(TEXT("CR Edit: add_var_node"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::AddVariableNode(
		RigBP, Controller, VarName, bIsGetter, CppType, X, Y);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("Variable node added"));
}

FMCPToolResult FMCPTool_ControlRig::HandleRemoveNode(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString NodeName;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("node_name"), NodeName, Error))
	{
		return Error.GetValue();
	}

	Controller->OpenUndoBracket(TEXT("CR Edit: remove_node"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::RemoveNode(Controller, NodeName);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("Node removed"));
}

FMCPToolResult FMCPTool_ControlRig::HandleLink(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString OutputPin, InputPin;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("output_pin"), OutputPin, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("input_pin"), InputPin, Error))
	{
		return Error.GetValue();
	}

	Controller->OpenUndoBracket(TEXT("CR Edit: link"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::AddLink(Controller, OutputPin, InputPin);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("Pins linked"));
}

FMCPToolResult FMCPTool_ControlRig::HandleUnlink(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString OutputPin, InputPin;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("output_pin"), OutputPin, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("input_pin"), InputPin, Error))
	{
		return Error.GetValue();
	}

	Controller->OpenUndoBracket(TEXT("CR Edit: unlink"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::BreakLink(Controller, OutputPin, InputPin);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("Pins unlinked"));
}

FMCPToolResult FMCPTool_ControlRig::HandleUnlinkAll(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString PinPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("pin_path"), PinPath, Error))
	{
		return Error.GetValue();
	}

	bool bAsInput = ExtractOptionalBool(Params, TEXT("as_input"), true);

	Controller->OpenUndoBracket(TEXT("CR Edit: unlink_all"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::BreakAllLinks(Controller, PinPath, bAsInput);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("All links broken"));
}

FMCPToolResult FMCPTool_ControlRig::HandleSetDefault(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString PinPath, Value;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("pin_path"), PinPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Error))
	{
		return Error.GetValue();
	}

	Controller->OpenUndoBracket(TEXT("CR Edit: set_default"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::SetPinDefault(Controller, PinPath, Value);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("Default value set"));
}

FMCPToolResult FMCPTool_ControlRig::HandleSetExpand(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	FString PinPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("pin_path"), PinPath, Error))
	{
		return Error.GetValue();
	}

	bool bExpanded = ExtractOptionalBool(Params, TEXT("expanded"), true);

	Controller->OpenUndoBracket(TEXT("CR Edit: set_expand"));
	TSharedPtr<FJsonObject> Result = FControlRigEditor::SetPinExpansion(Controller, PinPath, bExpanded);
	Controller->CloseUndoBracket();

	return JsonResultToToolResult(Result, TEXT("Pin expansion set"));
}

FMCPToolResult FMCPTool_ControlRig::HandleAddMemberVar(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	FString LoadError;
	UControlRigBlueprint* RigBP = FControlRigEditor::LoadControlRig(RigPath, LoadError);
	if (!RigBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString Name, CppType;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("var_name"), Name, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("cpp_type"), CppType, Error))
	{
		return Error.GetValue();
	}

	bool bIsPublic = ExtractOptionalBool(Params, TEXT("is_public"), true);
	FString DefaultValue = ExtractOptionalString(Params, TEXT("default_value"));

	TSharedPtr<FJsonObject> Result = FControlRigEditor::AddMemberVariable(
		RigBP, Name, CppType, bIsPublic, DefaultValue);

	return JsonResultToToolResult(Result, TEXT("Member variable added"));
}

FMCPToolResult FMCPTool_ControlRig::HandleRemoveMemberVar(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	FString LoadError;
	UControlRigBlueprint* RigBP = FControlRigEditor::LoadControlRig(RigPath, LoadError);
	if (!RigBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString Name;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("var_name"), Name, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FControlRigEditor::RemoveMemberVariable(RigBP, Name);

	return JsonResultToToolResult(Result, TEXT("Member variable removed"));
}

FMCPToolResult FMCPTool_ControlRig::HandleRecompile(const FString& RigPath)
{
	FString LoadError;
	UControlRigBlueprint* RigBP = FControlRigEditor::LoadControlRig(RigPath, LoadError);
	if (!RigBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TSharedPtr<FJsonObject> Result = FControlRigEditor::Recompile(RigBP);
	return JsonResultToToolResult(Result, TEXT("RigVM recompiled"));
}

FMCPToolResult FMCPTool_ControlRig::HandleBatch(const FString& RigPath, const TSharedRef<FJsonObject>& Params)
{
	LOAD_RIG_AND_CONTROLLER(RigPath, RigBP, Graph, Controller);

	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("operations"), OpsArray) || !OpsArray)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: operations (JSON array)"));
	}

	TSharedPtr<FJsonObject> Result = FControlRigEditor::ExecuteBatch(
		RigBP, Controller, *OpsArray);

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);

	int32 OKCount = static_cast<int32>(Result->GetNumberField(TEXT("ok_count")));
	int32 ErrCount = static_cast<int32>(Result->GetNumberField(TEXT("error_count")));
	int32 Total = static_cast<int32>(Result->GetNumberField(TEXT("total")));

	FString Message = FString::Printf(TEXT("Batch: %d OK, %d errors, %d total"), OKCount, ErrCount, Total);

	if (bSuccess)
	{
		return FMCPToolResult::Success(Message, Result);
	}
	else
	{
		// Partial success — still return data but mark as error
		FMCPToolResult PartialResult;
		PartialResult.bSuccess = false;
		PartialResult.Message = Message;
		PartialResult.Data = Result;
		return PartialResult;
	}
}

#undef LOAD_RIG_AND_CONTROLLER
