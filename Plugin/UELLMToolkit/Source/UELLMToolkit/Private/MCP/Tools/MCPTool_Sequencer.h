// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class FMCPTool_Sequencer : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("sequencer");
		Info.Description = TEXT(
			"Take Recorder control and Sequencer viewport manipulation.\n\n"
			"**Take Recorder:**\n"
			"- 'take_start': Start Take Recorder (optional slate_name)\n"
			"- 'take_stop': Stop Take Recorder, returns recorded sequence path\n"
			"- 'take_status': Check Take Recorder state\n\n"
			"**Sequencer:**\n"
			"- 'open': Open a Level Sequence in the Sequencer editor. Optional: frame (scrub to), "
			"camera_location/camera_rotation (set viewport).\n"
			"- 'scrub': Scrub to a specific display frame\n"
			"- 'get_state': Get current frame, playback range, frame rate, viewport camera pos/rot\n"
			"- 'close': Close the active Sequencer\n"
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation: take_start, take_stop, take_status, open, scrub, get_state, close"), true),
			FMCPToolParameter(TEXT("slate_name"), TEXT("string"),
				TEXT("take_start: custom slate name for the take"), false),
			FMCPToolParameter(TEXT("sequence_path"), TEXT("string"),
				TEXT("open: asset path of the Level Sequence to open"), false),
			FMCPToolParameter(TEXT("frame"), TEXT("number"),
				TEXT("open/scrub: display frame number to scrub to"), false),
			FMCPToolParameter(TEXT("camera_location"), TEXT("object"),
				TEXT("open: viewport camera position {x, y, z}"), false),
			FMCPToolParameter(TEXT("camera_rotation"), TEXT("object"),
				TEXT("open: viewport camera rotation {pitch, yaw, roll}"), false)
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult ExecuteTakeStart(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteTakeStop(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteTakeStatus(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteOpen(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteScrub(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteGetState(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteClose(const TSharedRef<FJsonObject>& Params);
};
