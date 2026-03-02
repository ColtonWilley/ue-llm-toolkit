// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Widget.h"
#include "WidgetEditor.h"

static FMCPToolResult WidgetJsonToToolResult(const TSharedPtr<FJsonObject>& Result, const FString& SuccessContext)
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

FMCPToolResult FMCPTool_Widget::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("inspect"))
	{
		return HandleInspect(Params);
	}
	else if (Operation == TEXT("get_properties"))
	{
		return HandleGetProperties(Params);
	}
	else if (Operation == TEXT("create"))
	{
		return HandleCreate(Params);
	}
	else if (Operation == TEXT("add_widget"))
	{
		return HandleAddWidget(Params);
	}
	else if (Operation == TEXT("remove_widget"))
	{
		return HandleRemoveWidget(Params);
	}
	else if (Operation == TEXT("set_property"))
	{
		return HandleSetProperty(Params);
	}
	else if (Operation == TEXT("set_slot"))
	{
		return HandleSetSlot(Params);
	}
	else if (Operation == TEXT("save"))
	{
		return HandleSave(Params);
	}
	else if (Operation == TEXT("batch"))
	{
		return HandleBatch(Params);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation: '%s'. Valid: inspect, get_properties, create, add_widget, remove_widget, set_property, set_slot, save, batch"),
		*Operation));
}

// ============================================================================
// Read Handlers
// ============================================================================

FMCPToolResult FMCPTool_Widget::HandleInspect(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FWidgetEditor::InspectWidgetTree(AssetPath);
	return WidgetJsonToToolResult(Result, TEXT("Widget tree inspected"));
}

FMCPToolResult FMCPTool_Widget::HandleGetProperties(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, WidgetName;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("widget_name"), WidgetName, Error))
	{
		return Error.GetValue();
	}

	TSharedPtr<FJsonObject> Result = FWidgetEditor::GetWidgetProperties(AssetPath, WidgetName);
	return WidgetJsonToToolResult(Result, TEXT("Widget properties retrieved"));
}

// ============================================================================
// Write Handlers
// ============================================================================

FMCPToolResult FMCPTool_Widget::HandleCreate(const TSharedRef<FJsonObject>& Params)
{
	FString Name, PackagePath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("name"), Name, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Error))
	{
		return Error.GetValue();
	}

	FString ParentClass = ExtractOptionalString(Params, TEXT("parent_class"), TEXT("UserWidget"));
	FString RootWidgetClass = ExtractOptionalString(Params, TEXT("root_widget_class"), TEXT("CanvasPanel"));

	TSharedPtr<FJsonObject> Result = FWidgetEditor::CreateWidgetBlueprint(Name, PackagePath, ParentClass, RootWidgetClass);
	return WidgetJsonToToolResult(Result, TEXT("Widget Blueprint created"));
}

FMCPToolResult FMCPTool_Widget::HandleAddWidget(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, WidgetClass, WidgetName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("widget_class"), WidgetClass, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("widget_name"), WidgetName, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UWidgetBlueprint* WBP = FWidgetEditor::LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FString ParentName = ExtractOptionalString(Params, TEXT("parent_name"), FString());

	TSharedPtr<FJsonObject> Result = FWidgetEditor::AddWidget(WBP, WidgetClass, WidgetName, ParentName);
	return WidgetJsonToToolResult(Result, TEXT("Widget added"));
}

FMCPToolResult FMCPTool_Widget::HandleRemoveWidget(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, WidgetName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("widget_name"), WidgetName, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UWidgetBlueprint* WBP = FWidgetEditor::LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TSharedPtr<FJsonObject> Result = FWidgetEditor::RemoveWidget(WBP, WidgetName);
	return WidgetJsonToToolResult(Result, TEXT("Widget removed"));
}

FMCPToolResult FMCPTool_Widget::HandleSetProperty(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, WidgetName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("widget_name"), WidgetName, Error))
	{
		return Error.GetValue();
	}

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropsObj) || !PropsObj)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: properties (JSON object)"));
	}

	FString LoadError;
	UWidgetBlueprint* WBP = FWidgetEditor::LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TSharedPtr<FJsonObject> Result = FWidgetEditor::SetWidgetProperty(WBP, WidgetName, *PropsObj);
	return WidgetJsonToToolResult(Result, TEXT("Properties set"));
}

FMCPToolResult FMCPTool_Widget::HandleSetSlot(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath, WidgetName;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("widget_name"), WidgetName, Error))
	{
		return Error.GetValue();
	}

	const TSharedPtr<FJsonObject>* SlotObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("slot_properties"), SlotObj) || !SlotObj)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: slot_properties (JSON object)"));
	}

	FString LoadError;
	UWidgetBlueprint* WBP = FWidgetEditor::LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TSharedPtr<FJsonObject> Result = FWidgetEditor::SetSlotProperty(WBP, WidgetName, *SlotObj);
	return WidgetJsonToToolResult(Result, TEXT("Slot properties set"));
}

FMCPToolResult FMCPTool_Widget::HandleSave(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UWidgetBlueprint* WBP = FWidgetEditor::LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TSharedPtr<FJsonObject> Result = FWidgetEditor::SaveWidgetBlueprint(WBP);
	return WidgetJsonToToolResult(Result, TEXT("Widget Blueprint saved"));
}

FMCPToolResult FMCPTool_Widget::HandleBatch(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Error;

	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Error))
	{
		return Error.GetValue();
	}

	FString LoadError;
	UWidgetBlueprint* WBP = FWidgetEditor::LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return FMCPToolResult::Error(LoadError);
	}

	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("operations"), OpsArray) || !OpsArray)
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: operations (JSON array)"));
	}

	TSharedPtr<FJsonObject> Result = FWidgetEditor::ExecuteBatch(WBP, *OpsArray);

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
		FMCPToolResult PartialResult;
		PartialResult.bSuccess = false;
		PartialResult.Message = Message;
		PartialResult.Data = Result;
		return PartialResult;
	}
}
