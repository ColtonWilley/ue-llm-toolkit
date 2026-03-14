// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class FMCPTool_MontageModify : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("montage_modify");
		Info.Description = TEXT(
			"Animation Montage editing tool.\n\n"
			"Read Operations:\n"
			"- 'get_info': Get full montage structure (sections, segments, notifies, blends, curve summary)\n"
			"- 'get_curves': Get all float curves with full key data\n\n"
			"Create/Save:\n"
			"- 'create': Create a new empty montage\n"
			"- 'save': Save montage to disk\n\n"
			"Section Operations:\n"
			"- 'add_section': Add composite section at time\n"
			"- 'remove_section': Remove section by name\n"
			"- 'link_sections': Link section to next section for playback ordering\n\n"
			"Segment Operations (animation clips in slot tracks):\n"
			"- 'add_segment': Add animation sequence to a slot track\n"
			"- 'remove_segment': Remove segment from slot track\n"
			"- 'set_segment': Modify segment properties (play rate, timing)\n\n"
			"Slot Operations:\n"
			"- 'add_slot': Add new slot track\n"
			"- 'remove_slot': Remove slot track by index\n\n"
			"Notify Operations:\n"
			"- 'add_notify': Add instant anim notify\n"
			"- 'add_notify_state': Add duration-based notify state (optional 'properties' to configure on creation)\n"
			"- 'remove_notify': Remove notify by name\n"
			"- 'move_notify': Move notify to a different track by index\n"
			"- 'rename_track': Set display name for a notify track\n"
			"- 'cleanup_tracks': Remove empty notify tracks beyond the last used track index\n"
			"- 'set_notify_properties': Set properties on an existing notify by index (supports bool, number, string, FName, enum, instanced UObject via _class)\n\n"
			"Blend Settings:\n"
			"- 'set_blend_in': Set blend-in time and curve\n"
			"- 'set_blend_out': Set blend-out time and curve\n\n"
			"Curve Operations:\n"
			"- 'get_curves': Get all float curves with full key data (time, value, interp/tangent modes)\n"
			"- 'add_curve': Create a named float curve (optional initial keys)\n"
			"- 'remove_curve': Delete a curve by name\n"
			"- 'set_curve_keys': Replace all keys on an existing curve"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("montage_path"), TEXT("string"), TEXT("Path to montage asset (e.g., '/Game/Animations/AM_Attack')"), true),
			FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("Operation: get_info, create, save, add_section, remove_section, link_sections, add_segment, remove_segment, set_segment, add_slot, remove_slot, add_notify, add_notify_state, remove_notify, move_notify, rename_track, cleanup_tracks, set_notify_properties, set_blend_in, set_blend_out, get_curves, add_curve, remove_curve, set_curve_keys"), true),
			FMCPToolParameter(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton path (for create)")),
			FMCPToolParameter(TEXT("package_path"), TEXT("string"), TEXT("Package path (for create, e.g., '/Game/Animations')")),
			FMCPToolParameter(TEXT("montage_name"), TEXT("string"), TEXT("Name for new montage (for create)")),
			FMCPToolParameter(TEXT("section_name"), TEXT("string"), TEXT("Section name")),
			FMCPToolParameter(TEXT("next_section"), TEXT("string"), TEXT("Next section name for linking")),
			FMCPToolParameter(TEXT("start_time"), TEXT("number"), TEXT("Start time in seconds")),
			FMCPToolParameter(TEXT("duration"), TEXT("number"), TEXT("Duration in seconds")),
			FMCPToolParameter(TEXT("animation_path"), TEXT("string"), TEXT("Path to animation sequence")),
			FMCPToolParameter(TEXT("slot_index"), TEXT("number"), TEXT("Slot track index"), false, TEXT("0")),
			FMCPToolParameter(TEXT("slot_name"), TEXT("string"), TEXT("Slot name for add_slot")),
			FMCPToolParameter(TEXT("segment_index"), TEXT("number"), TEXT("Segment index in slot")),
			FMCPToolParameter(TEXT("notify_name"), TEXT("string"), TEXT("Notify name")),
			FMCPToolParameter(TEXT("notify_class"), TEXT("string"), TEXT("Notify class name or path (e.g., 'AnimNotify_PlaySound' or '/Script/Engine.AnimNotify_PlaySound'). Omit for untyped notify.")),
			FMCPToolParameter(TEXT("trigger_time"), TEXT("number"), TEXT("Notify trigger time in seconds")),
			FMCPToolParameter(TEXT("track_index"), TEXT("number"), TEXT("Notify track index"), false, TEXT("0")),
			FMCPToolParameter(TEXT("notify_index"), TEXT("number"), TEXT("Notify array index (from get_info) for move_notify or set_notify_properties")),
			FMCPToolParameter(TEXT("properties"), TEXT("object"), TEXT("Properties to set on notify UObject. Keys are property names, values are bool/number/string. For instanced sub-objects, use {\"_class\": \"/Script/Module.ClassName\", ...} with nested properties.")),
			FMCPToolParameter(TEXT("track_name"), TEXT("string"), TEXT("Display name for notify track (for rename_track)")),
			FMCPToolParameter(TEXT("blend_time"), TEXT("number"), TEXT("Blend time in seconds")),
			FMCPToolParameter(TEXT("blend_option"), TEXT("string"), TEXT("Blend curve: Linear, Cubic, HermiteCubic, Sinusoidal, QuadraticInOut, CubicInOut, QuarticInOut, QuinticInOut, CircularIn, CircularOut, CircularInOut, ExpIn, ExpOut, ExpInOut"), false, TEXT("Linear")),
			FMCPToolParameter(TEXT("play_rate"), TEXT("number"), TEXT("Segment play rate"), false, TEXT("1.0")),
			FMCPToolParameter(TEXT("anim_start_time"), TEXT("number"), TEXT("Segment animation start time")),
			FMCPToolParameter(TEXT("anim_end_time"), TEXT("number"), TEXT("Segment animation end time")),
			FMCPToolParameter(TEXT("start_pos"), TEXT("number"), TEXT("Segment start position in montage timeline"), false, TEXT("0")),
			FMCPToolParameter(TEXT("curve_name"), TEXT("string"), TEXT("Curve name (for curve operations)")),
			FMCPToolParameter(TEXT("keys"), TEXT("array"), TEXT("Array of key objects: [{time, value, interp_mode?, tangent_mode?, arrive_tangent?, leave_tangent?}]. interp_mode: Linear|Constant|Cubic. tangent_mode: Auto|User|Break|SmartAuto."))
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult HandleGetInfo(const FString& MontagePath);
	FMCPToolResult HandleCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSave(const FString& MontagePath);
	FMCPToolResult HandleAddSection(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveSection(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleLinkSections(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddSegment(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveSegment(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetSegment(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddSlot(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveSlot(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddNotify(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleAddNotifyState(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveNotify(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleMoveNotify(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRenameTrack(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleCleanupTracks(const FString& MontagePath);
	FMCPToolResult HandleSetNotifyProperties(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetBlendIn(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetBlendOut(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleGetCurves(const FString& MontagePath);
	FMCPToolResult HandleAddCurve(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleRemoveCurve(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleSetCurveKeys(const FString& MontagePath, const TSharedRef<FJsonObject>& Params);

	TOptional<FMCPToolResult> LoadMontageOrError(const FString& Path, UAnimMontage*& OutMontage);
};
