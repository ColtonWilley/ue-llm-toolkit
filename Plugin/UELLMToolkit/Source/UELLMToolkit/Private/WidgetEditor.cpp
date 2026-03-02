// Copyright Natali Caggiano. All Rights Reserved.

#include "WidgetEditor.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Spacer.h"
#include "Components/Button.h"
#include "Components/ScaleBox.h"
#include "Components/GridPanel.h"
#include "Components/WrapBox.h"
#include "Components/PanelWidget.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "WidgetBlueprintFactory.h"

// ============================================================================
// Private Helpers
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::SuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FWidgetEditor::ErrorResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Message);
	return Result;
}

// ============================================================================
// Widget Class Resolution
// ============================================================================

UClass* FWidgetEditor::ResolveWidgetClass(const FString& ShortName)
{
	static TMap<FString, UClass*> ClassMap;
	if (ClassMap.Num() == 0)
	{
		ClassMap.Add(TEXT("CanvasPanel"), UCanvasPanel::StaticClass());
		ClassMap.Add(TEXT("Overlay"), UOverlay::StaticClass());
		ClassMap.Add(TEXT("VerticalBox"), UVerticalBox::StaticClass());
		ClassMap.Add(TEXT("HorizontalBox"), UHorizontalBox::StaticClass());
		ClassMap.Add(TEXT("SizeBox"), USizeBox::StaticClass());
		ClassMap.Add(TEXT("Border"), UBorder::StaticClass());
		ClassMap.Add(TEXT("ProgressBar"), UProgressBar::StaticClass());
		ClassMap.Add(TEXT("TextBlock"), UTextBlock::StaticClass());
		ClassMap.Add(TEXT("Image"), UImage::StaticClass());
		ClassMap.Add(TEXT("Spacer"), USpacer::StaticClass());
		ClassMap.Add(TEXT("Button"), UButton::StaticClass());
		ClassMap.Add(TEXT("ScaleBox"), UScaleBox::StaticClass());
		ClassMap.Add(TEXT("GridPanel"), UGridPanel::StaticClass());
		ClassMap.Add(TEXT("WrapBox"), UWrapBox::StaticClass());
	}

	if (UClass** Found = ClassMap.Find(ShortName))
	{
		return *Found;
	}

	UClass* FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/UMG.%s"), *ShortName));
	return FoundClass;
}

// ============================================================================
// Color Parsing
// ============================================================================

FLinearColor FWidgetEditor::ParseColor(const FString& ColorStr)
{
	FString Str = ColorStr.TrimStartAndEnd();

	if (Str.Equals(TEXT("red"), ESearchCase::IgnoreCase)) return FLinearColor::Red;
	if (Str.Equals(TEXT("green"), ESearchCase::IgnoreCase)) return FLinearColor::Green;
	if (Str.Equals(TEXT("blue"), ESearchCase::IgnoreCase)) return FLinearColor::Blue;
	if (Str.Equals(TEXT("white"), ESearchCase::IgnoreCase)) return FLinearColor::White;
	if (Str.Equals(TEXT("black"), ESearchCase::IgnoreCase)) return FLinearColor::Black;
	if (Str.Equals(TEXT("yellow"), ESearchCase::IgnoreCase)) return FLinearColor::Yellow;
	if (Str.Equals(TEXT("gray"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("grey"), ESearchCase::IgnoreCase))
		return FLinearColor::Gray;
	if (Str.Equals(TEXT("transparent"), ESearchCase::IgnoreCase)) return FLinearColor::Transparent;

	if (Str.StartsWith(TEXT("#")))
	{
		Str = Str.Mid(1);
	}

	FColor ParsedColor = FColor::White;
	if (Str.Len() == 6)
	{
		ParsedColor = FColor::FromHex(Str + TEXT("FF"));
	}
	else if (Str.Len() == 8)
	{
		ParsedColor = FColor::FromHex(Str);
	}

	return ParsedColor.ReinterpretAsLinear();
}

// ============================================================================
// Asset Loading
// ============================================================================

UWidgetBlueprint* FWidgetEditor::LoadWidgetBlueprint(const FString& Path, FString& OutError)
{
	UObject* Loaded = StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *Path);
	if (!Loaded)
	{
		OutError = FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *Path);
		return nullptr;
	}

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Loaded);
	if (!WBP)
	{
		OutError = FString::Printf(TEXT("Asset is not a Widget Blueprint: %s (is %s)"),
			*Path, *Loaded->GetClass()->GetName());
		return nullptr;
	}

	return WBP;
}

// ============================================================================
// Serialization Helpers
// ============================================================================

static FString GetSlotTypeName(UPanelSlot* Slot)
{
	if (!Slot) return TEXT("None");
	if (Cast<UCanvasPanelSlot>(Slot)) return TEXT("CanvasPanelSlot");
	if (Cast<UOverlaySlot>(Slot)) return TEXT("OverlaySlot");
	if (Cast<UVerticalBoxSlot>(Slot)) return TEXT("VerticalBoxSlot");
	if (Cast<UHorizontalBoxSlot>(Slot)) return TEXT("HorizontalBoxSlot");
	return Slot->GetClass()->GetName();
}

static FString AlignToString(EHorizontalAlignment Align)
{
	switch (Align)
	{
	case HAlign_Fill: return TEXT("Fill");
	case HAlign_Left: return TEXT("Left");
	case HAlign_Center: return TEXT("Center");
	case HAlign_Right: return TEXT("Right");
	default: return TEXT("Fill");
	}
}

static FString VAlignToString(EVerticalAlignment Align)
{
	switch (Align)
	{
	case VAlign_Fill: return TEXT("Fill");
	case VAlign_Top: return TEXT("Top");
	case VAlign_Center: return TEXT("Center");
	case VAlign_Bottom: return TEXT("Bottom");
	default: return TEXT("Fill");
	}
}

static EHorizontalAlignment StringToHAlign(const FString& Str)
{
	if (Str.Equals(TEXT("Left"), ESearchCase::IgnoreCase)) return HAlign_Left;
	if (Str.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) return HAlign_Center;
	if (Str.Equals(TEXT("Right"), ESearchCase::IgnoreCase)) return HAlign_Right;
	return HAlign_Fill;
}

static EVerticalAlignment StringToVAlign(const FString& Str)
{
	if (Str.Equals(TEXT("Top"), ESearchCase::IgnoreCase)) return VAlign_Top;
	if (Str.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) return VAlign_Center;
	if (Str.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase)) return VAlign_Bottom;
	return VAlign_Fill;
}

static TSharedPtr<FJsonObject> ColorToJson(const FLinearColor& Color)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("r"), Color.R);
	Obj->SetNumberField(TEXT("g"), Color.G);
	Obj->SetNumberField(TEXT("b"), Color.B);
	Obj->SetNumberField(TEXT("a"), Color.A);
	FColor SRGBColor = Color.ToFColor(true);
	Obj->SetStringField(TEXT("hex"), FString::Printf(TEXT("#%02X%02X%02X%02X"),
		SRGBColor.R, SRGBColor.G, SRGBColor.B, SRGBColor.A));
	return Obj;
}

TSharedPtr<FJsonObject> FWidgetEditor::SerializeSlotProperties(UWidget* Widget)
{
	TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
	UPanelSlot* Slot = Widget->Slot;
	if (!Slot) return SlotJson;

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		FVector2D Pos = CanvasSlot->GetPosition();
		FVector2D Size = CanvasSlot->GetSize();
		FAnchors Anchors = CanvasSlot->GetAnchors();
		FVector2D Alignment = CanvasSlot->GetAlignment();

		TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
		PosObj->SetNumberField(TEXT("x"), Pos.X);
		PosObj->SetNumberField(TEXT("y"), Pos.Y);
		SlotJson->SetObjectField(TEXT("position"), PosObj);

		TSharedPtr<FJsonObject> SizeObj = MakeShared<FJsonObject>();
		SizeObj->SetNumberField(TEXT("x"), Size.X);
		SizeObj->SetNumberField(TEXT("y"), Size.Y);
		SlotJson->SetObjectField(TEXT("size"), SizeObj);

		TSharedPtr<FJsonObject> AnchorsObj = MakeShared<FJsonObject>();
		AnchorsObj->SetNumberField(TEXT("min_x"), Anchors.Minimum.X);
		AnchorsObj->SetNumberField(TEXT("min_y"), Anchors.Minimum.Y);
		AnchorsObj->SetNumberField(TEXT("max_x"), Anchors.Maximum.X);
		AnchorsObj->SetNumberField(TEXT("max_y"), Anchors.Maximum.Y);
		SlotJson->SetObjectField(TEXT("anchors"), AnchorsObj);

		TSharedPtr<FJsonObject> AlignObj = MakeShared<FJsonObject>();
		AlignObj->SetNumberField(TEXT("x"), Alignment.X);
		AlignObj->SetNumberField(TEXT("y"), Alignment.Y);
		SlotJson->SetObjectField(TEXT("alignment"), AlignObj);

		SlotJson->SetBoolField(TEXT("auto_size"), CanvasSlot->GetAutoSize());
		SlotJson->SetNumberField(TEXT("z_order"), CanvasSlot->GetZOrder());
	}
	else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
	{
		SlotJson->SetStringField(TEXT("h_align"), AlignToString(OverlaySlot->GetHorizontalAlignment()));
		SlotJson->SetStringField(TEXT("v_align"), VAlignToString(OverlaySlot->GetVerticalAlignment()));

		FMargin Pad = OverlaySlot->GetPadding();
		TSharedPtr<FJsonObject> PadObj = MakeShared<FJsonObject>();
		PadObj->SetNumberField(TEXT("left"), Pad.Left);
		PadObj->SetNumberField(TEXT("top"), Pad.Top);
		PadObj->SetNumberField(TEXT("right"), Pad.Right);
		PadObj->SetNumberField(TEXT("bottom"), Pad.Bottom);
		SlotJson->SetObjectField(TEXT("padding"), PadObj);
	}
	else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		SlotJson->SetStringField(TEXT("h_align"), AlignToString(VSlot->GetHorizontalAlignment()));
		SlotJson->SetStringField(TEXT("v_align"), VAlignToString(VSlot->GetVerticalAlignment()));

		FSlateChildSize SizeRule = VSlot->GetSize();
		SlotJson->SetStringField(TEXT("size_rule"), SizeRule.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
		SlotJson->SetNumberField(TEXT("fill_weight"), SizeRule.Value);

		FMargin Pad = VSlot->GetPadding();
		TSharedPtr<FJsonObject> PadObj = MakeShared<FJsonObject>();
		PadObj->SetNumberField(TEXT("left"), Pad.Left);
		PadObj->SetNumberField(TEXT("top"), Pad.Top);
		PadObj->SetNumberField(TEXT("right"), Pad.Right);
		PadObj->SetNumberField(TEXT("bottom"), Pad.Bottom);
		SlotJson->SetObjectField(TEXT("padding"), PadObj);
	}
	else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		SlotJson->SetStringField(TEXT("h_align"), AlignToString(HSlot->GetHorizontalAlignment()));
		SlotJson->SetStringField(TEXT("v_align"), VAlignToString(HSlot->GetVerticalAlignment()));

		FSlateChildSize SizeRule = HSlot->GetSize();
		SlotJson->SetStringField(TEXT("size_rule"), SizeRule.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
		SlotJson->SetNumberField(TEXT("fill_weight"), SizeRule.Value);

		FMargin Pad = HSlot->GetPadding();
		TSharedPtr<FJsonObject> PadObj = MakeShared<FJsonObject>();
		PadObj->SetNumberField(TEXT("left"), Pad.Left);
		PadObj->SetNumberField(TEXT("top"), Pad.Top);
		PadObj->SetNumberField(TEXT("right"), Pad.Right);
		PadObj->SetNumberField(TEXT("bottom"), Pad.Bottom);
		SlotJson->SetObjectField(TEXT("padding"), PadObj);
	}

	return SlotJson;
}

TSharedPtr<FJsonObject> FWidgetEditor::SerializeWidgetProperties(UWidget* Widget)
{
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	if (!Widget) return Props;

	Props->SetStringField(TEXT("visibility"), StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)Widget->GetVisibility()));
	Props->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());
	Props->SetBoolField(TEXT("is_enabled"), Widget->GetIsEnabled());

	if (UProgressBar* PB = Cast<UProgressBar>(Widget))
	{
		Props->SetNumberField(TEXT("percent"), PB->GetPercent());
		Props->SetObjectField(TEXT("fill_color"), ColorToJson(PB->GetFillColorAndOpacity()));
		Props->SetStringField(TEXT("bar_fill_type"), StaticEnum<EProgressBarFillType::Type>()->GetNameStringByValue((int64)PB->GetBarFillType()));
		const FProgressBarStyle& Style = PB->GetWidgetStyle();
		Props->SetObjectField(TEXT("style_fill_tint"), ColorToJson(Style.FillImage.TintColor.GetSpecifiedColor()));
		Props->SetObjectField(TEXT("style_background_tint"), ColorToJson(Style.BackgroundImage.TintColor.GetSpecifiedColor()));
	}
	else if (UTextBlock* TB = Cast<UTextBlock>(Widget))
	{
		Props->SetStringField(TEXT("text"), TB->GetText().ToString());
		Props->SetObjectField(TEXT("color"), ColorToJson(TB->GetColorAndOpacity().GetSpecifiedColor()));
		FSlateFontInfo FontInfo = TB->GetFont();
		Props->SetNumberField(TEXT("font_size"), FontInfo.Size);
	}
	else if (UImage* Img = Cast<UImage>(Widget))
	{
		Props->SetObjectField(TEXT("color_and_opacity"), ColorToJson(Img->GetColorAndOpacity()));
	}
	else if (USizeBox* SB = Cast<USizeBox>(Widget))
	{
		Props->SetNumberField(TEXT("width_override"), SB->GetWidthOverride());
		Props->SetNumberField(TEXT("height_override"), SB->GetHeightOverride());
		Props->SetNumberField(TEXT("min_desired_width"), SB->GetMinDesiredWidth());
		Props->SetNumberField(TEXT("min_desired_height"), SB->GetMinDesiredHeight());
		Props->SetNumberField(TEXT("max_desired_width"), SB->GetMaxDesiredWidth());
		Props->SetNumberField(TEXT("max_desired_height"), SB->GetMaxDesiredHeight());
	}

	return Props;
}

TSharedPtr<FJsonObject> FWidgetEditor::SerializeWidgetTree(UWidget* Widget, UWidgetBlueprint* WBP)
{
	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	if (!Widget) return Node;

	Node->SetStringField(TEXT("name"), Widget->GetName());
	Node->SetStringField(TEXT("class"), Widget->GetClass()->GetName());

	if (Widget->Slot)
	{
		Node->SetStringField(TEXT("slot_type"), GetSlotTypeName(Widget->Slot));
		Node->SetObjectField(TEXT("slot_properties"), SerializeSlotProperties(Widget));
	}

	Node->SetObjectField(TEXT("visual_properties"), SerializeWidgetProperties(Widget));

	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	if (Panel)
	{
		TArray<TSharedPtr<FJsonValue>> ChildrenArray;
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			UWidget* Child = Panel->GetChildAt(i);
			if (Child)
			{
				ChildrenArray.Add(MakeShared<FJsonValueObject>(SerializeWidgetTree(Child, WBP)));
			}
		}
		Node->SetArrayField(TEXT("children"), ChildrenArray);
	}

	return Node;
}

// ============================================================================
// InspectWidgetTree
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::InspectWidgetTree(const FString& AssetPath)
{
	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return ErrorResult(LoadError);
	}

	UWidgetTree* WidgetTree = WBP->WidgetTree;
	if (!WidgetTree)
	{
		return ErrorResult(TEXT("Widget Blueprint has no WidgetTree"));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(TEXT("Widget tree inspected"));
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("blueprint_name"), WBP->GetName());

	UWidget* RootWidget = WidgetTree->RootWidget;
	if (RootWidget)
	{
		Result->SetObjectField(TEXT("widget_tree"), SerializeWidgetTree(RootWidget, WBP));
	}
	else
	{
		Result->SetStringField(TEXT("widget_tree"), TEXT("empty"));
	}

	TArray<UWidget*> AllWidgets;
	WidgetTree->GetAllWidgets(AllWidgets);
	Result->SetNumberField(TEXT("total_widgets"), AllWidgets.Num());

	return Result;
}

// ============================================================================
// GetWidgetProperties
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::GetWidgetProperties(const FString& AssetPath, const FString& WidgetName)
{
	FString LoadError;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath, LoadError);
	if (!WBP)
	{
		return ErrorResult(LoadError);
	}

	UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return ErrorResult(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
	}

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Properties for %s (%s)"), *WidgetName, *Widget->GetClass()->GetName()));
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetName());
	Result->SetObjectField(TEXT("visual_properties"), SerializeWidgetProperties(Widget));

	if (Widget->Slot)
	{
		Result->SetStringField(TEXT("slot_type"), GetSlotTypeName(Widget->Slot));
		Result->SetObjectField(TEXT("slot_properties"), SerializeSlotProperties(Widget));
	}

	return Result;
}

// ============================================================================
// CreateWidgetBlueprint
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::CreateWidgetBlueprint(const FString& Name, const FString& PackagePath,
	const FString& ParentClass, const FString& RootWidgetClass)
{
	UClass* ParentUClass = UUserWidget::StaticClass();
	if (!ParentClass.IsEmpty() && !ParentClass.Equals(TEXT("UserWidget"), ESearchCase::IgnoreCase))
	{
		ParentUClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/UMG.%s"), *ParentClass));
		if (!ParentUClass)
		{
			ParentUClass = LoadClass<UUserWidget>(nullptr, *ParentClass);
		}
		if (!ParentUClass)
		{
			return ErrorResult(FString::Printf(TEXT("Could not find parent class: %s"), *ParentClass));
		}
	}

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentUClass;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
	if (!NewAsset)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to create Widget Blueprint: %s/%s"), *PackagePath, *Name));
	}

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(NewAsset);
	if (!WBP)
	{
		return ErrorResult(TEXT("Created asset is not a Widget Blueprint"));
	}

	if (!RootWidgetClass.IsEmpty() && !RootWidgetClass.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		UClass* RootClass = ResolveWidgetClass(RootWidgetClass);
		if (!RootClass)
		{
			return ErrorResult(FString::Printf(TEXT("Unknown root widget class: %s"), *RootWidgetClass));
		}

		UWidget* Root = WBP->WidgetTree->ConstructWidget<UWidget>(RootClass, FName(TEXT("RootPanel")));
		if (Root)
		{
			WBP->WidgetTree->RootWidget = Root;
		}
	}

	WBP->MarkPackageDirty();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Created Widget Blueprint: %s"), *WBP->GetPathName()));
	Result->SetStringField(TEXT("path"), WBP->GetPathName());
	return Result;
}

// ============================================================================
// AddWidget
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::AddWidget(UWidgetBlueprint* WBP, const FString& WidgetClass,
	const FString& WidgetName, const FString& ParentName)
{
	if (!WBP || !WBP->WidgetTree)
	{
		return ErrorResult(TEXT("Invalid Widget Blueprint or missing WidgetTree"));
	}

	UClass* WidgetUClass = ResolveWidgetClass(WidgetClass);
	if (!WidgetUClass)
	{
		return ErrorResult(FString::Printf(TEXT("Unknown widget class: %s"), *WidgetClass));
	}

	if (WBP->WidgetTree->FindWidget(FName(*WidgetName)))
	{
		return ErrorResult(FString::Printf(TEXT("Widget with name '%s' already exists"), *WidgetName));
	}

	UWidget* NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetUClass, FName(*WidgetName));
	if (!NewWidget)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to construct widget of class %s"), *WidgetClass));
	}

	UPanelWidget* Parent = nullptr;
	if (ParentName.IsEmpty() || ParentName.Equals(TEXT("root"), ESearchCase::IgnoreCase))
	{
		Parent = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget);
	}
	else
	{
		UWidget* ParentWidget = WBP->WidgetTree->FindWidget(FName(*ParentName));
		if (!ParentWidget)
		{
			WBP->WidgetTree->RemoveWidget(NewWidget);
			return ErrorResult(FString::Printf(TEXT("Parent widget not found: %s"), *ParentName));
		}
		Parent = Cast<UPanelWidget>(ParentWidget);
		if (!Parent)
		{
			WBP->WidgetTree->RemoveWidget(NewWidget);
			return ErrorResult(FString::Printf(TEXT("Parent '%s' is not a panel widget (is %s)"),
				*ParentName, *ParentWidget->GetClass()->GetName()));
		}
	}

	if (!Parent)
	{
		if (!WBP->WidgetTree->RootWidget)
		{
			WBP->WidgetTree->RootWidget = NewWidget;

			WBP->MarkPackageDirty();
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

			TSharedPtr<FJsonObject> Result = SuccessResult(
				FString::Printf(TEXT("Set %s as root widget"), *WidgetName));
			Result->SetStringField(TEXT("widget_name"), WidgetName);
			Result->SetStringField(TEXT("widget_class"), WidgetClass);
			Result->SetStringField(TEXT("slot_type"), TEXT("None (root)"));
			return Result;
		}
		else
		{
			WBP->WidgetTree->RemoveWidget(NewWidget);
			return ErrorResult(TEXT("Root widget is not a panel — cannot add children"));
		}
	}

	UPanelSlot* Slot = Parent->AddChild(NewWidget);
	if (!Slot)
	{
		WBP->WidgetTree->RemoveWidget(NewWidget);
		return ErrorResult(TEXT("AddChild returned null slot"));
	}

	WBP->MarkPackageDirty();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Added %s '%s' to %s"), *WidgetClass, *WidgetName, *Parent->GetName()));
	Result->SetStringField(TEXT("widget_name"), WidgetName);
	Result->SetStringField(TEXT("widget_class"), WidgetClass);
	Result->SetStringField(TEXT("parent_name"), Parent->GetName());
	Result->SetStringField(TEXT("slot_type"), GetSlotTypeName(Slot));
	return Result;
}

// ============================================================================
// RemoveWidget
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::RemoveWidget(UWidgetBlueprint* WBP, const FString& WidgetName)
{
	if (!WBP || !WBP->WidgetTree)
	{
		return ErrorResult(TEXT("Invalid Widget Blueprint or missing WidgetTree"));
	}

	UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return ErrorResult(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
	}

	FString WidgetClass = Widget->GetClass()->GetName();
	bool bRemoved = WBP->WidgetTree->RemoveWidget(Widget);
	if (!bRemoved)
	{
		return ErrorResult(FString::Printf(TEXT("Failed to remove widget: %s"), *WidgetName));
	}

	WBP->MarkPackageDirty();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Removed widget %s (%s)"), *WidgetName, *WidgetClass));
	return Result;
}

// ============================================================================
// SetWidgetProperty
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::SetWidgetProperty(UWidgetBlueprint* WBP, const FString& WidgetName,
	const TSharedPtr<FJsonObject>& Properties)
{
	if (!WBP || !WBP->WidgetTree)
	{
		return ErrorResult(TEXT("Invalid Widget Blueprint or missing WidgetTree"));
	}

	UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return ErrorResult(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
	}

	TArray<FString> SetProperties;
	FString StrVal;
	double NumVal;
	bool BoolVal;

	if (Properties->TryGetNumberField(TEXT("render_opacity"), NumVal))
	{
		Widget->SetRenderOpacity(static_cast<float>(NumVal));
		SetProperties.Add(TEXT("render_opacity"));
	}

	if (Properties->TryGetBoolField(TEXT("is_enabled"), BoolVal))
	{
		Widget->SetIsEnabled(BoolVal);
		SetProperties.Add(TEXT("is_enabled"));
	}

	if (Properties->TryGetStringField(TEXT("visibility"), StrVal))
	{
		int64 EnumVal = StaticEnum<ESlateVisibility>()->GetValueByNameString(StrVal);
		if (EnumVal != INDEX_NONE)
		{
			Widget->SetVisibility((ESlateVisibility)EnumVal);
			SetProperties.Add(TEXT("visibility"));
		}
	}

	if (UProgressBar* PB = Cast<UProgressBar>(Widget))
	{
		if (Properties->TryGetNumberField(TEXT("percent"), NumVal))
		{
			PB->SetPercent(static_cast<float>(NumVal));
			SetProperties.Add(TEXT("percent"));
		}
		if (Properties->TryGetStringField(TEXT("fill_color"), StrVal))
		{
			PB->SetFillColorAndOpacity(ParseColor(StrVal));
			SetProperties.Add(TEXT("fill_color"));
		}
		if (Properties->TryGetStringField(TEXT("bar_fill_type"), StrVal))
		{
			int64 EnumVal = StaticEnum<EProgressBarFillType::Type>()->GetValueByNameString(StrVal);
			if (EnumVal != INDEX_NONE)
			{
				PB->SetBarFillType((EProgressBarFillType::Type)EnumVal);
				SetProperties.Add(TEXT("bar_fill_type"));
			}
		}
		if (Properties->TryGetBoolField(TEXT("is_marquee"), BoolVal))
		{
			PB->SetIsMarquee(BoolVal);
			SetProperties.Add(TEXT("is_marquee"));
		}
		if (Properties->TryGetStringField(TEXT("style_fill_tint"), StrVal))
		{
			FProgressBarStyle Style = PB->GetWidgetStyle();
			Style.FillImage.TintColor = FSlateColor(ParseColor(StrVal));
			PB->SetWidgetStyle(Style);
			SetProperties.Add(TEXT("style_fill_tint"));
		}
		if (Properties->TryGetStringField(TEXT("style_background_tint"), StrVal))
		{
			FProgressBarStyle Style = PB->GetWidgetStyle();
			Style.BackgroundImage.TintColor = FSlateColor(ParseColor(StrVal));
			PB->SetWidgetStyle(Style);
			SetProperties.Add(TEXT("style_background_tint"));
		}
	}
	else if (UTextBlock* TB = Cast<UTextBlock>(Widget))
	{
		if (Properties->TryGetStringField(TEXT("text"), StrVal))
		{
			TB->SetText(FText::FromString(StrVal));
			SetProperties.Add(TEXT("text"));
		}
		if (Properties->TryGetStringField(TEXT("color"), StrVal))
		{
			FLinearColor Color = ParseColor(StrVal);
			TB->SetColorAndOpacity(FSlateColor(Color));
			SetProperties.Add(TEXT("color"));
		}
		if (Properties->TryGetNumberField(TEXT("font_size"), NumVal))
		{
			FSlateFontInfo FontInfo = TB->GetFont();
			FontInfo.Size = static_cast<int32>(NumVal);
			TB->SetFont(FontInfo);
			SetProperties.Add(TEXT("font_size"));
		}
		if (Properties->TryGetStringField(TEXT("justification"), StrVal))
		{
			if (StrVal.Equals(TEXT("Left"), ESearchCase::IgnoreCase))
				TB->SetJustification(ETextJustify::Left);
			else if (StrVal.Equals(TEXT("Center"), ESearchCase::IgnoreCase))
				TB->SetJustification(ETextJustify::Center);
			else if (StrVal.Equals(TEXT("Right"), ESearchCase::IgnoreCase))
				TB->SetJustification(ETextJustify::Right);
			SetProperties.Add(TEXT("justification"));
		}
		if (Properties->TryGetBoolField(TEXT("auto_wrap"), BoolVal))
		{
			TB->SetAutoWrapText(BoolVal);
			SetProperties.Add(TEXT("auto_wrap"));
		}
	}
	else if (UImage* Img = Cast<UImage>(Widget))
	{
		if (Properties->TryGetStringField(TEXT("color_and_opacity"), StrVal))
		{
			Img->SetColorAndOpacity(ParseColor(StrVal));
			SetProperties.Add(TEXT("color_and_opacity"));
		}
		if (Properties->TryGetStringField(TEXT("brush_texture"), StrVal))
		{
			UTexture2D* Tex = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *StrVal));
			if (Tex)
			{
				Img->SetBrushFromTexture(Tex);
				SetProperties.Add(TEXT("brush_texture"));
			}
		}
	}
	else if (USizeBox* SB = Cast<USizeBox>(Widget))
	{
		if (Properties->TryGetNumberField(TEXT("width_override"), NumVal))
		{
			SB->SetWidthOverride(static_cast<float>(NumVal));
			SetProperties.Add(TEXT("width_override"));
		}
		if (Properties->TryGetNumberField(TEXT("height_override"), NumVal))
		{
			SB->SetHeightOverride(static_cast<float>(NumVal));
			SetProperties.Add(TEXT("height_override"));
		}
		if (Properties->TryGetNumberField(TEXT("min_desired_width"), NumVal))
		{
			SB->SetMinDesiredWidth(static_cast<float>(NumVal));
			SetProperties.Add(TEXT("min_desired_width"));
		}
		if (Properties->TryGetNumberField(TEXT("min_desired_height"), NumVal))
		{
			SB->SetMinDesiredHeight(static_cast<float>(NumVal));
			SetProperties.Add(TEXT("min_desired_height"));
		}
		if (Properties->TryGetNumberField(TEXT("max_desired_width"), NumVal))
		{
			SB->SetMaxDesiredWidth(static_cast<float>(NumVal));
			SetProperties.Add(TEXT("max_desired_width"));
		}
		if (Properties->TryGetNumberField(TEXT("max_desired_height"), NumVal))
		{
			SB->SetMaxDesiredHeight(static_cast<float>(NumVal));
			SetProperties.Add(TEXT("max_desired_height"));
		}
	}

	if (SetProperties.Num() == 0)
	{
		return ErrorResult(TEXT("No recognized properties were set. Check property names and widget type."));
	}

	WBP->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Set %d properties on %s: %s"),
			SetProperties.Num(), *WidgetName, *FString::Join(SetProperties, TEXT(", "))));
	return Result;
}

// ============================================================================
// SetSlotProperty
// ============================================================================

static FMargin ExtractPadding(const TSharedPtr<FJsonObject>& PadObj)
{
	FMargin Pad(0);
	double Val;
	if (PadObj->TryGetNumberField(TEXT("left"), Val)) Pad.Left = static_cast<float>(Val);
	if (PadObj->TryGetNumberField(TEXT("top"), Val)) Pad.Top = static_cast<float>(Val);
	if (PadObj->TryGetNumberField(TEXT("right"), Val)) Pad.Right = static_cast<float>(Val);
	if (PadObj->TryGetNumberField(TEXT("bottom"), Val)) Pad.Bottom = static_cast<float>(Val);
	return Pad;
}

TSharedPtr<FJsonObject> FWidgetEditor::SetSlotProperty(UWidgetBlueprint* WBP, const FString& WidgetName,
	const TSharedPtr<FJsonObject>& SlotProperties)
{
	if (!WBP || !WBP->WidgetTree)
	{
		return ErrorResult(TEXT("Invalid Widget Blueprint or missing WidgetTree"));
	}

	UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return ErrorResult(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
	}

	UPanelSlot* Slot = Widget->Slot;
	if (!Slot)
	{
		return ErrorResult(FString::Printf(TEXT("Widget '%s' has no slot (is it the root?)"), *WidgetName));
	}

	TArray<FString> SetProps;
	double NumVal;
	bool BoolVal;
	FString StrVal;

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		const TSharedPtr<FJsonObject>* PosObj;
		if (SlotProperties->TryGetObjectField(TEXT("position"), PosObj))
		{
			FVector2D Pos = CanvasSlot->GetPosition();
			double X, Y;
			if ((*PosObj)->TryGetNumberField(TEXT("x"), X)) Pos.X = static_cast<float>(X);
			if ((*PosObj)->TryGetNumberField(TEXT("y"), Y)) Pos.Y = static_cast<float>(Y);
			CanvasSlot->SetPosition(Pos);
			SetProps.Add(TEXT("position"));
		}
		const TSharedPtr<FJsonObject>* SizeObj;
		if (SlotProperties->TryGetObjectField(TEXT("size"), SizeObj))
		{
			FVector2D Size = CanvasSlot->GetSize();
			double X, Y;
			if ((*SizeObj)->TryGetNumberField(TEXT("x"), X)) Size.X = static_cast<float>(X);
			if ((*SizeObj)->TryGetNumberField(TEXT("y"), Y)) Size.Y = static_cast<float>(Y);
			CanvasSlot->SetSize(Size);
			SetProps.Add(TEXT("size"));
		}
		const TSharedPtr<FJsonObject>* AnchorsObj;
		if (SlotProperties->TryGetObjectField(TEXT("anchors"), AnchorsObj))
		{
			FAnchors Anchors = CanvasSlot->GetAnchors();
			double Val;
			if ((*AnchorsObj)->TryGetNumberField(TEXT("min_x"), Val)) Anchors.Minimum.X = static_cast<float>(Val);
			if ((*AnchorsObj)->TryGetNumberField(TEXT("min_y"), Val)) Anchors.Minimum.Y = static_cast<float>(Val);
			if ((*AnchorsObj)->TryGetNumberField(TEXT("max_x"), Val)) Anchors.Maximum.X = static_cast<float>(Val);
			if ((*AnchorsObj)->TryGetNumberField(TEXT("max_y"), Val)) Anchors.Maximum.Y = static_cast<float>(Val);
			CanvasSlot->SetAnchors(Anchors);
			SetProps.Add(TEXT("anchors"));
		}
		const TSharedPtr<FJsonObject>* AlignObj;
		if (SlotProperties->TryGetObjectField(TEXT("alignment"), AlignObj))
		{
			FVector2D Align = CanvasSlot->GetAlignment();
			double X, Y;
			if ((*AlignObj)->TryGetNumberField(TEXT("x"), X)) Align.X = static_cast<float>(X);
			if ((*AlignObj)->TryGetNumberField(TEXT("y"), Y)) Align.Y = static_cast<float>(Y);
			CanvasSlot->SetAlignment(Align);
			SetProps.Add(TEXT("alignment"));
		}
		if (SlotProperties->TryGetBoolField(TEXT("auto_size"), BoolVal))
		{
			CanvasSlot->SetAutoSize(BoolVal);
			SetProps.Add(TEXT("auto_size"));
		}
		if (SlotProperties->TryGetNumberField(TEXT("z_order"), NumVal))
		{
			CanvasSlot->SetZOrder(static_cast<int32>(NumVal));
			SetProps.Add(TEXT("z_order"));
		}
	}
	else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
	{
		if (SlotProperties->TryGetStringField(TEXT("h_align"), StrVal))
		{
			OverlaySlot->SetHorizontalAlignment(StringToHAlign(StrVal));
			SetProps.Add(TEXT("h_align"));
		}
		if (SlotProperties->TryGetStringField(TEXT("v_align"), StrVal))
		{
			OverlaySlot->SetVerticalAlignment(StringToVAlign(StrVal));
			SetProps.Add(TEXT("v_align"));
		}
		const TSharedPtr<FJsonObject>* PadObj;
		if (SlotProperties->TryGetObjectField(TEXT("padding"), PadObj))
		{
			OverlaySlot->SetPadding(ExtractPadding(*PadObj));
			SetProps.Add(TEXT("padding"));
		}
	}
	else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		if (SlotProperties->TryGetStringField(TEXT("h_align"), StrVal))
		{
			VSlot->SetHorizontalAlignment(StringToHAlign(StrVal));
			SetProps.Add(TEXT("h_align"));
		}
		if (SlotProperties->TryGetStringField(TEXT("v_align"), StrVal))
		{
			VSlot->SetVerticalAlignment(StringToVAlign(StrVal));
			SetProps.Add(TEXT("v_align"));
		}
		if (SlotProperties->TryGetStringField(TEXT("size_rule"), StrVal))
		{
			FSlateChildSize SizeRule = VSlot->GetSize();
			if (StrVal.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
				SizeRule.SizeRule = ESlateSizeRule::Automatic;
			else if (StrVal.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
				SizeRule.SizeRule = ESlateSizeRule::Fill;
			VSlot->SetSize(SizeRule);
			SetProps.Add(TEXT("size_rule"));
		}
		if (SlotProperties->TryGetNumberField(TEXT("fill_weight"), NumVal))
		{
			FSlateChildSize SizeRule = VSlot->GetSize();
			SizeRule.Value = static_cast<float>(NumVal);
			VSlot->SetSize(SizeRule);
			SetProps.Add(TEXT("fill_weight"));
		}
		const TSharedPtr<FJsonObject>* PadObj;
		if (SlotProperties->TryGetObjectField(TEXT("padding"), PadObj))
		{
			VSlot->SetPadding(ExtractPadding(*PadObj));
			SetProps.Add(TEXT("padding"));
		}
	}
	else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		if (SlotProperties->TryGetStringField(TEXT("h_align"), StrVal))
		{
			HSlot->SetHorizontalAlignment(StringToHAlign(StrVal));
			SetProps.Add(TEXT("h_align"));
		}
		if (SlotProperties->TryGetStringField(TEXT("v_align"), StrVal))
		{
			HSlot->SetVerticalAlignment(StringToVAlign(StrVal));
			SetProps.Add(TEXT("v_align"));
		}
		if (SlotProperties->TryGetStringField(TEXT("size_rule"), StrVal))
		{
			FSlateChildSize SizeRule = HSlot->GetSize();
			if (StrVal.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
				SizeRule.SizeRule = ESlateSizeRule::Automatic;
			else if (StrVal.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
				SizeRule.SizeRule = ESlateSizeRule::Fill;
			HSlot->SetSize(SizeRule);
			SetProps.Add(TEXT("size_rule"));
		}
		if (SlotProperties->TryGetNumberField(TEXT("fill_weight"), NumVal))
		{
			FSlateChildSize SizeRule = HSlot->GetSize();
			SizeRule.Value = static_cast<float>(NumVal);
			HSlot->SetSize(SizeRule);
			SetProps.Add(TEXT("fill_weight"));
		}
		const TSharedPtr<FJsonObject>* PadObj;
		if (SlotProperties->TryGetObjectField(TEXT("padding"), PadObj))
		{
			HSlot->SetPadding(ExtractPadding(*PadObj));
			SetProps.Add(TEXT("padding"));
		}
	}
	else
	{
		return ErrorResult(FString::Printf(TEXT("Unsupported slot type: %s"), *Slot->GetClass()->GetName()));
	}

	if (SetProps.Num() == 0)
	{
		return ErrorResult(TEXT("No recognized slot properties were set. Check property names and slot type."));
	}

	WBP->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = SuccessResult(
		FString::Printf(TEXT("Set %d slot properties on %s: %s"),
			SetProps.Num(), *WidgetName, *FString::Join(SetProps, TEXT(", "))));
	return Result;
}

// ============================================================================
// Save
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::SaveWidgetBlueprint(UWidgetBlueprint* WBP)
{
	if (!WBP)
	{
		return ErrorResult(TEXT("Null Widget Blueprint"));
	}

	FString AssetPath = WBP->GetPathName();
	bool bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, false);
	if (bSaved)
	{
		return SuccessResult(FString::Printf(TEXT("Saved: %s"), *AssetPath));
	}
	return ErrorResult(FString::Printf(TEXT("Failed to save: %s"), *AssetPath));
}

// ============================================================================
// Batch
// ============================================================================

TSharedPtr<FJsonObject> FWidgetEditor::DispatchBatchOp(UWidgetBlueprint* WBP, const TSharedPtr<FJsonObject>& OpData)
{
	FString OpName;
	OpData->TryGetStringField(TEXT("op"), OpName);
	OpName = OpName.ToLower();

	if (OpName == TEXT("add_widget"))
	{
		FString WidgetClass, WidgetName, ParentName;
		if (!OpData->TryGetStringField(TEXT("widget_class"), WidgetClass) || WidgetClass.IsEmpty())
		{
			return ErrorResult(TEXT("add_widget: missing widget_class"));
		}
		if (!OpData->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
		{
			return ErrorResult(TEXT("add_widget: missing widget_name"));
		}
		OpData->TryGetStringField(TEXT("parent_name"), ParentName);
		return AddWidget(WBP, WidgetClass, WidgetName, ParentName);
	}
	else if (OpName == TEXT("remove_widget"))
	{
		FString WidgetName;
		if (!OpData->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
		{
			return ErrorResult(TEXT("remove_widget: missing widget_name"));
		}
		return RemoveWidget(WBP, WidgetName);
	}
	else if (OpName == TEXT("set_property"))
	{
		FString WidgetName;
		if (!OpData->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
		{
			return ErrorResult(TEXT("set_property: missing widget_name"));
		}
		const TSharedPtr<FJsonObject>* PropsObj;
		if (!OpData->TryGetObjectField(TEXT("properties"), PropsObj))
		{
			return ErrorResult(TEXT("set_property: missing properties object"));
		}
		return SetWidgetProperty(WBP, WidgetName, *PropsObj);
	}
	else if (OpName == TEXT("set_slot"))
	{
		FString WidgetName;
		if (!OpData->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
		{
			return ErrorResult(TEXT("set_slot: missing widget_name"));
		}
		const TSharedPtr<FJsonObject>* SlotObj;
		if (!OpData->TryGetObjectField(TEXT("slot_properties"), SlotObj))
		{
			return ErrorResult(TEXT("set_slot: missing slot_properties object"));
		}
		return SetSlotProperty(WBP, WidgetName, *SlotObj);
	}

	return ErrorResult(FString::Printf(
		TEXT("Unknown batch op: '%s'. Valid: add_widget, remove_widget, set_property, set_slot"),
		*OpName));
}

TSharedPtr<FJsonObject> FWidgetEditor::ExecuteBatch(UWidgetBlueprint* WBP, const TArray<TSharedPtr<FJsonValue>>& Operations)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!WBP)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Null Widget Blueprint"));
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 OKCount = 0;
	int32 ErrCount = 0;

	for (int32 i = 0; i < Operations.Num(); ++i)
	{
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

		TSharedPtr<FJsonObject> OpResult = DispatchBatchOp(WBP, OpData);
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

	Result->SetBoolField(TEXT("success"), ErrCount == 0);
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("ok_count"), OKCount);
	Result->SetNumberField(TEXT("error_count"), ErrCount);
	Result->SetNumberField(TEXT("total"), Operations.Num());

	return Result;
}
