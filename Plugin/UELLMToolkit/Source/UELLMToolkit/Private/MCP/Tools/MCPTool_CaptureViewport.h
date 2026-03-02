// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Capture a screenshot of the active viewport or an asset editor preview
 * Returns base64-encoded JPEG (1024x576, quality 70)
 * Captures PIE viewport if running, otherwise active editor viewport.
 * With asset_path, captures the 3D preview from an open asset editor (SkeletalMesh, Animation, etc.)
 */
class FMCPTool_CaptureViewport : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("capture_viewport");
		Info.Description = TEXT(
			"Capture a screenshot of a viewport.\n\n"
			"Without parameters: captures PIE viewport (if running) or the active editor viewport.\n"
			"With asset_path: captures the 3D preview viewport from an open asset editor "
			"(SkeletalMesh, Animation, AnimBlueprint, Physics Asset, etc.). The asset editor must already be open.\n"
			"With asset_path + frame: scrubs the animation preview to the specified frame before capturing.\n"
			"With camera_location/camera_rotation: positions the camera before capturing (works with both level and asset editor viewports).\n\n"
			"Output: 1024x576 JPEG image encoded as base64 string.\n\n"
			"Use cases:\n"
			"- Verify actor placement after spawning/moving\n"
			"- Check lighting changes\n"
			"- Visually verify mesh or animation assets in their editor preview\n"
			"- Capture a specific animation frame for pose verification\n"
			"- Get consistent camera angles for comparison\n"
			"- Debug visual issues\n\n"
			"Returns: Base64-encoded JPEG image data."
		);
		Info.Parameters = {
			{TEXT("asset_path"), TEXT("string"), TEXT("(Optional) Asset path to capture from its editor preview. Auto-opens editor if not already open."), false},
			{TEXT("frame"), TEXT("integer"), TEXT("(Optional) Frame number to scrub to before capturing. Requires asset_path pointing to an animation asset."), false},
			{TEXT("camera"), TEXT("string"), TEXT("(Optional) Named camera preset for asset editor captures: 'front' (default), 'from_left', 'from_right'. Ignored if camera_location/camera_rotation are provided."), false},
			{TEXT("camera_location"), TEXT("object"), TEXT("(Optional) Camera position as {\"x\":0,\"y\":0,\"z\":0}. Overrides camera preset."), false},
			{TEXT("camera_rotation"), TEXT("object"), TEXT("(Optional) Camera rotation as {\"pitch\":0,\"yaw\":0,\"roll\":0} in degrees. Overrides camera preset."), false}
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;
};
