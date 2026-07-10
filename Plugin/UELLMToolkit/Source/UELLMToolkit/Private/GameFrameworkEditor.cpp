// Copyright Natali Caggiano. All Rights Reserved.

#include "GameFrameworkEditor.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "GameFramework/SpectatorPawn.h"
#include "Blueprint/UserWidget.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PropertySerializer.h"

TSharedPtr<FJsonObject> FGameFrameworkEditor::SuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::ErrorResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::InspectGameMode()
{
	if (!GEditor)
	{
		return ErrorResult(TEXT("Editor not available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return ErrorResult(TEXT("No editor world available"));
	}

	AGameModeBase* GameMode = World->GetAuthGameMode();
	UClass* GameModeClass = nullptr;

	if (GameMode)
	{
		GameModeClass = GameMode->GetClass();
	}
	else
	{
		AWorldSettings* WorldSettings = World->GetWorldSettings();
		if (WorldSettings && WorldSettings->DefaultGameMode)
		{
			GameModeClass = WorldSettings->DefaultGameMode;
		}
	}

	if (!GameModeClass)
	{
		return ErrorResult(TEXT("No game mode configured. Neither AuthGameMode nor WorldSettings->DefaultGameMode is set."));
	}

	AGameModeBase* CDO = GameModeClass->GetDefaultObject<AGameModeBase>();
	if (!CDO)
	{
		return ErrorResult(TEXT("Failed to get game mode CDO"));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Game mode: %s"), *GameModeClass->GetPathName()));

	Result->SetStringField(TEXT("game_mode_class"), GameModeClass->GetPathName());
	Result->SetStringField(TEXT("default_pawn_class"),
		CDO->DefaultPawnClass ? CDO->DefaultPawnClass->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("player_controller_class"),
		CDO->PlayerControllerClass ? CDO->PlayerControllerClass->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("hud_class"),
		CDO->HUDClass ? CDO->HUDClass->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("spectator_class"),
		CDO->SpectatorClass ? CDO->SpectatorClass->GetPathName() : TEXT("None"));
	Result->SetBoolField(TEXT("from_pie"), GameMode != nullptr);

	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::SetGameModeDefaults(const FString& DefaultPawnClass, const FString& PlayerControllerClass, const FString& HUDClass)
{
	if (!GEditor)
	{
		return ErrorResult(TEXT("Editor not available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return ErrorResult(TEXT("No editor world available"));
	}

	AWorldSettings* WorldSettings = World->GetWorldSettings();
	if (!WorldSettings || !WorldSettings->DefaultGameMode)
	{
		return ErrorResult(TEXT("No DefaultGameMode set in WorldSettings"));
	}

	AGameModeBase* CDO = WorldSettings->DefaultGameMode->GetDefaultObject<AGameModeBase>();
	if (!CDO)
	{
		return ErrorResult(TEXT("Failed to get game mode CDO"));
	}

	TArray<FString> ChangesApplied;

	if (!DefaultPawnClass.IsEmpty())
	{
		UClass* PawnClass = LoadClass<APawn>(nullptr, *DefaultPawnClass);
		if (!PawnClass)
		{
			return ErrorResult(FString::Printf(TEXT("Failed to load pawn class: %s"), *DefaultPawnClass));
		}
		CDO->DefaultPawnClass = PawnClass;
		ChangesApplied.Add(FString::Printf(TEXT("DefaultPawnClass=%s"), *PawnClass->GetPathName()));
	}

	if (!PlayerControllerClass.IsEmpty())
	{
		UClass* PCClass = LoadClass<APlayerController>(nullptr, *PlayerControllerClass);
		if (!PCClass)
		{
			return ErrorResult(FString::Printf(TEXT("Failed to load player controller class: %s"), *PlayerControllerClass));
		}
		CDO->PlayerControllerClass = PCClass;
		ChangesApplied.Add(FString::Printf(TEXT("PlayerControllerClass=%s"), *PCClass->GetPathName()));
	}

	if (!HUDClass.IsEmpty())
	{
		UClass* HUDCls = LoadClass<AHUD>(nullptr, *HUDClass);
		if (!HUDCls)
		{
			return ErrorResult(FString::Printf(TEXT("Failed to load HUD class: %s"), *HUDClass));
		}
		CDO->HUDClass = HUDCls;
		ChangesApplied.Add(FString::Printf(TEXT("HUDClass=%s"), *HUDCls->GetPathName()));
	}

	if (ChangesApplied.Num() == 0)
	{
		return ErrorResult(TEXT("No class overrides specified. Provide at least one of: default_pawn_class, player_controller_class, hud_class"));
	}

	CDO->GetPackage()->Modify();
	CDO->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Updated %d game mode defaults"), ChangesApplied.Num()));

	TArray<TSharedPtr<FJsonValue>> ChangesArray;
	for (const FString& Change : ChangesApplied)
	{
		ChangesArray.Add(MakeShared<FJsonValueString>(Change));
	}
	Result->SetArrayField(TEXT("changes"), ChangesArray);

	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::SetInputMode(const FString& InputMode)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		return ErrorResult(TEXT("PIE is not running. Player Controller operations require Play-In-Editor."));
	}

	APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
	if (!PC)
	{
		return ErrorResult(TEXT("No player controller found in PIE world"));
	}

	if (InputMode == TEXT("game_only"))
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
	else if (InputMode == TEXT("ui_only"))
	{
		PC->SetInputMode(FInputModeUIOnly());
	}
	else if (InputMode == TEXT("game_and_ui"))
	{
		PC->SetInputMode(FInputModeGameAndUI());
	}
	else
	{
		return ErrorResult(FString::Printf(TEXT("Invalid input_mode: '%s'. Valid: game_only, ui_only, game_and_ui"), *InputMode));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(TEXT("Input mode set to %s"), *InputMode));
	Result->SetStringField(TEXT("input_mode"), InputMode);
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::ShowWidget(const FString& WidgetClassPath, int32 ZOrder)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		return ErrorResult(TEXT("PIE is not running. Player Controller operations require Play-In-Editor."));
	}

	UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetClassPath);
	if (!WidgetClass)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to load widget class: %s"), *WidgetClassPath));
	}

	APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
	if (!PC)
	{
		return ErrorResult(TEXT("No player controller found in PIE world"));
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (!Widget)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to create widget of class: %s"), *WidgetClassPath));
	}

	Widget->AddToViewport(ZOrder);

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(TEXT("Widget %s added to viewport"), *WidgetClassPath));
	Result->SetStringField(TEXT("widget_class"), WidgetClassPath);
	Result->SetNumberField(TEXT("z_order"), ZOrder);
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::RemoveWidget(const FString& WidgetClassPath)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		return ErrorResult(TEXT("PIE is not running. Player Controller operations require Play-In-Editor."));
	}

	UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetClassPath);
	if (!WidgetClass)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to load widget class: %s"), *WidgetClassPath));
	}

	int32 RemovedCount = 0;
	ForEachObjectOfClass(WidgetClass, [&](UObject* Obj)
	{
		UUserWidget* Widget = Cast<UUserWidget>(Obj);
		if (Widget && Widget->IsInViewport())
		{
			Widget->RemoveFromParent();
			RemovedCount++;
		}
	});

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(TEXT("Removed %d widget(s) of class %s"), RemovedCount, *WidgetClassPath));
	Result->SetNumberField(TEXT("removed_count"), RemovedCount);
	Result->SetStringField(TEXT("widget_class"), WidgetClassPath);
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::SetPause(bool bPaused)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		return ErrorResult(TEXT("PIE is not running. Player Controller operations require Play-In-Editor."));
	}

	APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
	if (!PC)
	{
		return ErrorResult(TEXT("No player controller found in PIE world"));
	}

	PC->SetPause(bPaused);

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(TEXT("Game %s"), bPaused ? TEXT("paused") : TEXT("unpaused")));
	Result->SetBoolField(TEXT("paused"), bPaused);
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::SetMouseCursor(bool bShowCursor)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		return ErrorResult(TEXT("PIE is not running. Player Controller operations require Play-In-Editor."));
	}

	APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
	if (!PC)
	{
		return ErrorResult(TEXT("No player controller found in PIE world"));
	}

	PC->bShowMouseCursor = bShowCursor;

	TSharedPtr<FJsonObject> Result = SuccessResult(FString::Printf(TEXT("Mouse cursor %s"), bShowCursor ? TEXT("shown") : TEXT("hidden")));
	Result->SetBoolField(TEXT("show_cursor"), bShowCursor);
	return Result;
}

// ===== Private Helpers =====

UDataTable* FGameFrameworkEditor::LoadDataTable(const FString& TablePath, FString& OutError)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!Table)
	{
		OutError = FString::Printf(TEXT("DataTable not found: %s"), *TablePath);
	}
	return Table;
}

TSharedPtr<FJsonValue> FGameFrameworkEditor::PropertyToJson(FProperty* Property, const void* ValuePtr)
{
	return FPropertySerializer::PropertyToJsonValue(Property, ValuePtr);
}

bool FGameFrameworkEditor::JsonToProperty(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	if (!Property || !ValuePtr || !JsonValue.IsValid())
	{
		OutError = TEXT("Invalid property, value pointer, or JSON value");
		return false;
	}

	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			TextProp->SetPropertyValue(ValuePtr, FText::FromString(StrVal));
			return true;
		}
		OutError = FString::Printf(TEXT("FTextProperty '%s' requires a string value"), *Property->GetName());
		return false;
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* ObjVal;
		if (JsonValue->TryGetObject(ObjVal) && ObjVal && (*ObjVal).IsValid())
		{
			for (const auto& Pair : (*ObjVal)->Values)
			{
				FProperty* FieldProp = StructProp->Struct->FindPropertyByName(FName(*Pair.Key));
				if (!FieldProp)
				{
					continue;
				}
				void* FieldPtr = FieldProp->ContainerPtrToValuePtr<void>(ValuePtr);
				FString FieldError;
				if (!JsonToProperty(FieldProp, FieldPtr, Pair.Value, FieldError))
				{
					OutError = FString::Printf(TEXT("Field '%s': %s"), *Pair.Key, *FieldError);
					return false;
				}
			}
			return true;
		}
		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			const TCHAR* ImportResult = StructProp->ImportText_Direct(*StrVal, ValuePtr, nullptr, 0);
			if (ImportResult)
			{
				return true;
			}
			OutError = FString::Printf(TEXT("Failed to ImportText struct '%s' from string"), *Property->GetName());
			return false;
		}
		OutError = FString::Printf(TEXT("Struct property '%s' requires a JSON object or string"), *Property->GetName());
		return false;
	}

	// For arrays, maps, and simple types: fall back to ImportText
	FString StrVal;
	if (JsonValue->TryGetString(StrVal))
	{
		const TCHAR* ImportResult = Property->ImportText_Direct(*StrVal, ValuePtr, nullptr, 0);
		if (ImportResult)
		{
			return true;
		}
	}
	// Try numeric for simple numeric properties
	double NumVal;
	if (JsonValue->TryGetNumber(NumVal))
	{
		if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
		{
			if (NumProp->IsInteger())
			{
				NumProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(NumVal));
			}
			else
			{
				NumProp->SetFloatingPointPropertyValue(ValuePtr, NumVal);
			}
			return true;
		}
	}
	bool BoolVal;
	if (JsonValue->TryGetBool(BoolVal))
	{
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
		{
			BoolProp->SetPropertyValue(ValuePtr, BoolVal);
			return true;
		}
	}
	OutError = FString::Printf(TEXT("Cannot convert JSON to property '%s' (type: %s)"), *Property->GetName(), *Property->GetCPPType());
	return false;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::SerializeRow(const UScriptStruct* RowStruct, const void* RowData)
{
	TSharedPtr<FJsonObject> RowObj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
	{
		FProperty* Property = *It;
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowData);
		RowObj->SetField(Property->GetName(), PropertyToJson(Property, ValuePtr));
	}
	return RowObj;
}

// ===== DataTable Operations =====

TSharedPtr<FJsonObject> FGameFrameworkEditor::ListDataTables(const FString& FolderPath)
{
	FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*FolderPath));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> TablesArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> TableObj = MakeShared<FJsonObject>();
		TableObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		TableObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());

		FString RowStructName;
		if (Asset.GetTagValue(FName("RowStructure"), RowStructName))
		{
			TableObj->SetStringField(TEXT("row_struct_name"), RowStructName);
		}

		TablesArray.Add(MakeShared<FJsonValueObject>(TableObj));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Found %d DataTable(s) in %s"), TablesArray.Num(), *FolderPath));
	Result->SetArrayField(TEXT("tables"), TablesArray);
	Result->SetNumberField(TEXT("count"), TablesArray.Num());
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::InspectDataTable(const FString& TablePath)
{
	FString LoadError;
	UDataTable* Table = LoadDataTable(TablePath, LoadError);
	if (!Table)
	{
		return ErrorResult(LoadError);
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		return ErrorResult(TEXT("DataTable has no row struct"));
	}

	TArray<TSharedPtr<FJsonValue>> ColumnsArray;
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
	{
		FProperty* Property = *It;
		TSharedPtr<FJsonObject> ColObj = MakeShared<FJsonObject>();
		ColObj->SetStringField(TEXT("name"), Property->GetName());
		ColObj->SetStringField(TEXT("type"), Property->GetCPPType());
		ColObj->SetBoolField(TEXT("is_editable"), Property->HasAnyPropertyFlags(CPF_Edit));

		FString Category;
		if (Property->HasMetaData(TEXT("Category")))
		{
			Category = Property->GetMetaData(TEXT("Category"));
		}
		ColObj->SetStringField(TEXT("category"), Category);

		ColumnsArray.Add(MakeShared<FJsonValueObject>(ColObj));
	}

	TArray<FName> RowNames = Table->GetRowNames();

	TArray<TSharedPtr<FJsonValue>> RowNamesArray;
	for (const FName& Name : RowNames)
	{
		RowNamesArray.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("DataTable %s: %d columns, %d rows"), *TablePath, ColumnsArray.Num(), RowNames.Num()));
	Result->SetStringField(TEXT("table_name"), Table->GetName());
	Result->SetStringField(TEXT("table_path"), TablePath);

	TSharedPtr<FJsonObject> StructInfo = MakeShared<FJsonObject>();
	StructInfo->SetStringField(TEXT("name"), RowStruct->GetName());
	StructInfo->SetStringField(TEXT("path"), RowStruct->GetPathName());
	Result->SetObjectField(TEXT("row_struct"), StructInfo);

	Result->SetArrayField(TEXT("columns"), ColumnsArray);
	Result->SetArrayField(TEXT("row_names"), RowNamesArray);
	Result->SetNumberField(TEXT("row_count"), RowNames.Num());
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::GetRow(const FString& TablePath, const FString& RowName)
{
	FString LoadError;
	UDataTable* Table = LoadDataTable(TablePath, LoadError);
	if (!Table)
	{
		return ErrorResult(LoadError);
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		return ErrorResult(TEXT("DataTable has no row struct"));
	}

	void* RowData = Table->FindRowUnchecked(FName(*RowName));
	if (!RowData)
	{
		return ErrorResult(FString::Printf(TEXT("Row '%s' not found"), *RowName));
	}

	TSharedPtr<FJsonObject> RowObj = SerializeRow(RowStruct, RowData);

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Row '%s' from %s"), *RowName, *TablePath));
	Result->SetStringField(TEXT("row_name"), RowName);
	Result->SetObjectField(TEXT("row_data"), RowObj);
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::SetRow(const FString& TablePath, const FString& RowName, const TSharedPtr<FJsonObject>& RowData)
{
	if (!RowData.IsValid())
	{
		return ErrorResult(TEXT("row_data is required"));
	}

	FString LoadError;
	UDataTable* Table = LoadDataTable(TablePath, LoadError);
	if (!Table)
	{
		return ErrorResult(LoadError);
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		return ErrorResult(TEXT("DataTable has no row struct"));
	}

	FName RowFName(*RowName);
	bool bIsNewRow = false;
	void* RowPtr = Table->FindRowUnchecked(RowFName);

	uint8* AllocatedData = nullptr;
	if (!RowPtr)
	{
		bIsNewRow = true;
		AllocatedData = (uint8*)FMemory::Malloc(RowStruct->GetStructureSize(), RowStruct->GetMinAlignment());
		RowStruct->InitializeStruct(AllocatedData);
		RowPtr = AllocatedData;
	}

	TArray<FString> UpdatedFields;
	for (const auto& Pair : RowData->Values)
	{
		FProperty* FieldProp = FindFProperty<FProperty>(RowStruct, FName(*Pair.Key));
		if (!FieldProp)
		{
			if (AllocatedData)
			{
				RowStruct->DestroyStruct(AllocatedData);
				FMemory::Free(AllocatedData);
			}
			return ErrorResult(FString::Printf(TEXT("Property '%s' not found in row struct '%s'"), *Pair.Key, *RowStruct->GetName()));
		}

		void* FieldPtr = FieldProp->ContainerPtrToValuePtr<void>(RowPtr);
		FString FieldError;
		if (!JsonToProperty(FieldProp, FieldPtr, Pair.Value, FieldError))
		{
			if (AllocatedData)
			{
				RowStruct->DestroyStruct(AllocatedData);
				FMemory::Free(AllocatedData);
			}
			return ErrorResult(FString::Printf(TEXT("Failed to set '%s': %s"), *Pair.Key, *FieldError));
		}
		UpdatedFields.Add(FString(Pair.Key));
	}

	if (bIsNewRow)
	{
		Table->AddRow(RowFName, *reinterpret_cast<FTableRowBase*>(AllocatedData));
		RowStruct->DestroyStruct(AllocatedData);
		FMemory::Free(AllocatedData);
		RowPtr = Table->FindRowUnchecked(RowFName);
	}

	Table->Modify();
	Table->MarkPackageDirty();

	TSharedPtr<FJsonObject> FinalRow = SerializeRow(RowStruct, RowPtr);

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("%s row '%s' in %s (%d fields updated)"),
			bIsNewRow ? TEXT("Added") : TEXT("Updated"), *RowName, *TablePath, UpdatedFields.Num()));
	Result->SetStringField(TEXT("row_name"), RowName);
	Result->SetObjectField(TEXT("row_data"), FinalRow);
	Result->SetBoolField(TEXT("is_new_row"), bIsNewRow);
	return Result;
}

TSharedPtr<FJsonObject> FGameFrameworkEditor::DeleteRow(const FString& TablePath, const FString& RowName)
{
	FString LoadError;
	UDataTable* Table = LoadDataTable(TablePath, LoadError);
	if (!Table)
	{
		return ErrorResult(LoadError);
	}

	FName RowFName(*RowName);
	if (!Table->FindRowUnchecked(RowFName))
	{
		return ErrorResult(FString::Printf(TEXT("Row '%s' not found"), *RowName));
	}

	Table->RemoveRow(RowFName);
	Table->Modify();
	Table->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Deleted row '%s' from %s"), *RowName, *TablePath));
	Result->SetStringField(TEXT("deleted_row_name"), RowName);
	return Result;
}
