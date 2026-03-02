// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Widget Blueprint read + write operations.
 *
 * Read Operations:
 *   - 'inspect': Full widget tree of a Widget Blueprint
 *   - 'get_properties': Detailed properties of a single widget
 *
 * Write Operations:
 *   - 'create': Create a new Widget Blueprint
 *   - 'add_widget': Add a widget to the tree
 *   - 'remove_widget': Remove a widget from the tree
 *   - 'set_property': Set visual/behavioral properties on a widget
 *   - 'set_slot': Set layout slot properties (position, anchors, size)
 *   - 'save': Save Widget Blueprint to disk
 *   - 'batch': Multiple operations in sequence
 */
class FMCPTool_Widget : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("widget_editor");
		Info.Description = TEXT(
			"Widget Blueprint read + write tool.\n\n"
			"READ OPERATIONS:\n"
			"- 'inspect': Full widget tree — names, classes, slots, visual properties, children.\n"
			"  Params: asset_path (required)\n\n"
			"- 'get_properties': Detailed properties of a single widget by name.\n"
			"  Params: asset_path, widget_name (required)\n\n"
			"WRITE OPERATIONS:\n"
			"- 'create': Create a new Widget Blueprint.\n"
			"  Params: name, package_path (required); parent_class (optional, default 'UserWidget'), root_widget_class (optional, default 'CanvasPanel')\n\n"
			"- 'add_widget': Add a widget to the tree.\n"
			"  Params: asset_path, widget_class, widget_name (required); parent_name (optional, defaults to root)\n"
			"  Supported classes: CanvasPanel, Overlay, VerticalBox, HorizontalBox, SizeBox, Border, ProgressBar, TextBlock, Image, Spacer, Button, ScaleBox, GridPanel, WrapBox\n\n"
			"- 'remove_widget': Remove a widget from the tree.\n"
			"  Params: asset_path, widget_name (required)\n\n"
			"- 'set_property': Set visual/behavioral properties on a widget.\n"
			"  Params: asset_path, widget_name (required), properties (JSON object)\n"
			"  All widgets: visibility, render_opacity, is_enabled\n"
			"  ProgressBar: percent, fill_color (hex/named), bar_fill_type, is_marquee, style_fill_tint (hex/named), style_background_tint (hex/named)\n"
			"  TextBlock: text, color, font_size, justification (Left/Center/Right), auto_wrap\n"
			"  Image: color_and_opacity, brush_texture (asset path)\n"
			"  SizeBox: width_override, height_override, min_desired_width/height, max_desired_width/height\n\n"
			"- 'set_slot': Set layout slot properties.\n"
			"  Params: asset_path, widget_name (required), slot_properties (JSON object)\n"
			"  CanvasPanelSlot: position {x,y}, size {x,y}, anchors {min_x,min_y,max_x,max_y}, alignment {x,y}, auto_size, z_order\n"
			"  OverlaySlot: h_align, v_align, padding {left,top,right,bottom}\n"
			"  VerticalBoxSlot/HorizontalBoxSlot: h_align, v_align, padding, size_rule (Auto/Fill), fill_weight\n\n"
			"- 'save': Save Widget Blueprint to disk.\n"
			"  Params: asset_path (required)\n\n"
			"- 'batch': Multiple ops in sequence, single save at end.\n"
			"  Params: asset_path (required), operations (array of {op, ...params})\n"
			"  Valid batch ops: add_widget, remove_widget, set_property, set_slot"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation: inspect, get_properties, create, add_widget, remove_widget, set_property, set_slot, save, batch"), true),
			FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Widget Blueprint asset path")),
			FMCPToolParameter(TEXT("widget_name"), TEXT("string"), TEXT("Name of widget within the tree")),
			FMCPToolParameter(TEXT("widget_class"), TEXT("string"), TEXT("Widget class short name (e.g. ProgressBar, TextBlock)")),
			FMCPToolParameter(TEXT("parent_name"), TEXT("string"), TEXT("Parent widget name (defaults to root)")),
			FMCPToolParameter(TEXT("name"), TEXT("string"), TEXT("Asset name (for create)")),
			FMCPToolParameter(TEXT("package_path"), TEXT("string"), TEXT("Package path for new asset (for create)")),
			FMCPToolParameter(TEXT("parent_class"), TEXT("string"), TEXT("Parent class (for create)"), false, TEXT("UserWidget")),
			FMCPToolParameter(TEXT("root_widget_class"), TEXT("string"), TEXT("Root widget class (for create)"), false, TEXT("CanvasPanel")),
			FMCPToolParameter(TEXT("properties"), TEXT("object"), TEXT("Widget properties to set (for set_property)")),
			FMCPToolParameter(TEXT("slot_properties"), TEXT("object"), TEXT("Slot properties to set (for set_slot)")),
			FMCPToolParameter(TEXT("operations"), TEXT("array"), TEXT("Batch operations array [{op, ...params}]"))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult HandleInspect(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleGetProperties(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddWidget(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveWidget(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetProperty(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetSlot(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSave(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleBatch(const TSharedRef<FJsonObject>& Params);
};
