// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"
#include "InputActionValue.h"
#include "Containers/Ticker.h"

class FPIESequenceRunner;
class FPIEFrameGrabber;
class UInputAction;
class UEnhancedInputComponent;

class FMCPTool_GameplayDebug : public FMCPToolBase
{
public:
	virtual ~FMCPTool_GameplayDebug();

	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("gameplay_debug");
		Info.Description = TEXT(
			"Automate PIE gameplay for debugging combat, combos, hit reactions, and state machines.\n\n"
			"**Atomic Sequence (preferred workflow):**\n"
			"- 'run_sequence': Single call that starts PIE, runs timed steps, captures to disk, writes manifest, stops PIE.\n"
			"  Returns immediately with output_dir + estimated_duration_ms. Sleep, then read manifest.json + captures.\n"
			"  Steps: [{\"type\":\"input|hold|capture|console|input_tape\", \"delay_ms\":N, ...}]\n"
			"  - input: single-frame inject. Fields: action, value_x/y/z\n"
			"  - hold: continuous inject for duration. Fields: action, value_x/y/z, duration_ms\n"
			"  - input_tape: per-frame value injection at 60fps. Fields: action, values (array of [x,y] or numbers). Zero gaps.\n"
			"  - capture: screenshot to disk. Fields: name (becomes {name}.jpg)\n"
			"  - console: execute console command mid-sequence. Fields: command (e.g. \"stat fps\")\n"
			"  delay_ms is cumulative from previous step start.\n\n"
			"PIE Lifecycle (manual):\n"
			"- 'start_pie': Start a Play-In-Editor session\n"
			"- 'stop_pie': Stop the current PIE session\n"
			"- 'pie_status': Check PIE state (running, frame_number, game_time)\n\n"
			"Input Injection (manual, via Enhanced Input pipeline):\n"
			"- 'inject_input': Single-frame input (attack, dodge, etc.)\n"
			"- 'start_continuous': Begin holding an input (movement stick)\n"
			"- 'update_continuous': Change continuous injection value\n"
			"- 'stop_continuous': Release continuous injection\n\n"
			"Capture (manual):\n"
			"- 'capture_pie': One-shot PIE viewport capture with frame number\n\n"
			"Monitor Mode (passive observation):\n"
			"- 'start_monitor': Begin logging player state + input events to UE output log during manual play.\n"
			"  Params: interval_ms (state snapshot rate, default 500), axis_threshold (edge dead zone, default 0.15)\n"
			"  Auto-attaches to PIE when available; stays active across PIE sessions.\n"
			"  Read results via get_output_log with filter '[PIE-DBG]'.\n"
			"- 'stop_monitor': Stop monitoring, returns duration + event count.\n"
			"- 'monitor_status': Check monitor state (active, duration, events, tracked actions).\n\n"
			"Montage Control (during PIE):\n"
			"- 'play_montage': Play an AnimMontage on a SkeletalMeshComponent's AnimInstance\n"
			"- 'montage_jump_to_section': Jump to a named section in the active montage\n"
			"- 'montage_stop': Stop the active montage with blend out\n\n"
			"Legacy Sequences:\n"
			"- 'execute_sequence': Play timed input sequence with auto-capture\n"
			"- 'sequence_status': Poll running sequence progress\n\n"
			"Input values: Digital actions need no value. Axis1D: {\"value_x\": 1.0}. "
			"Axis2D: {\"value_x\": 0.5, \"value_y\": 1.0}.\n"
			"All input uses UInputAction asset paths (e.g., /Game/Input/IA_Attack), "
			"respecting the full Enhanced Input pipeline (triggers, modifiers, mappings)."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation to perform (see description)"), true),
			FMCPToolParameter(TEXT("action_path"), TEXT("string"),
				TEXT("Path to UInputAction asset (e.g., '/Game/Input/IA_Attack')"), false),
			FMCPToolParameter(TEXT("value_x"), TEXT("number"),
				TEXT("X axis value for Axis1D/Axis2D actions (default: 1.0 for Digital)"), false),
			FMCPToolParameter(TEXT("value_y"), TEXT("number"),
				TEXT("Y axis value for Axis2D actions"), false),
			FMCPToolParameter(TEXT("value_z"), TEXT("number"),
				TEXT("Z axis value for Axis3D actions"), false),
			FMCPToolParameter(TEXT("injection_id"), TEXT("string"),
				TEXT("ID returned by start_continuous, used for update/stop"), false),
			FMCPToolParameter(TEXT("steps"), TEXT("array"),
				TEXT("run_sequence steps: [{\"type\":\"input\",\"delay_ms\":0,\"action\":\"/Game/...\"},"
					"{\"type\":\"hold\",\"delay_ms\":0,\"action\":\"/Game/...\",\"duration_ms\":500},"
					"{\"type\":\"capture\",\"delay_ms\":100,\"name\":\"idle\"}]"), false),
			FMCPToolParameter(TEXT("settle_ms"), TEXT("number"),
				TEXT("run_sequence: ms to wait after PIE starts before first step (default: 500)"), false),
			FMCPToolParameter(TEXT("sequence"), TEXT("array"),
				TEXT("Legacy execute_sequence steps: [{\"action\":\"/Game/Input/IA_Attack\",\"delay_ms\":0}, ...]"), false),
			FMCPToolParameter(TEXT("capture_mode"), TEXT("string"),
				TEXT("Capture mode: 'normal' (ReadPixels, ~5-50ms) or 'high_speed' (FFrameGrabber, ~2-5ms). Default: normal"), false),
			FMCPToolParameter(TEXT("capture_interval_ms"), TEXT("number"),
				TEXT("Auto-capture interval in ms for normal mode (default: 200)"), false),
			FMCPToolParameter(TEXT("capture_every_n_frames"), TEXT("number"),
				TEXT("Auto-capture every N game frames for high-speed mode (default: 3)"), false),
			FMCPToolParameter(TEXT("sequence_file"), TEXT("string"),
				TEXT("run_sequence: path to a JSON file on disk containing steps array (alternative to inline 'steps'). File schema: {\"steps\":[...]} or just [...]. Also supports top-level name/settle_ms/auto_capture_every_n_frames/output_dir overrides. Inline params always win over file values."), false),
			FMCPToolParameter(TEXT("auto_capture_every_n_frames"), TEXT("number"),
				TEXT("run_sequence: enable async auto-capture alongside steps. Captures every N frames via UE FFrameGrabber (near-zero game thread stall, GPU-side resize to 1024x576). 0=disabled (default). Try 2 for ~60 captures/sec at 120fps."), false),
			FMCPToolParameter(TEXT("output_dir"), TEXT("string"),
				TEXT("Directory for captured frames (default: $TEMP/pie-debug/)"), false),
			FMCPToolParameter(TEXT("name"), TEXT("string"),
				TEXT("Name for the sequence run (used in output dir and manifest)"), false),
			FMCPToolParameter(TEXT("max_duration_ms"), TEXT("number"),
				TEXT("run_sequence: safety timeout in ms after settle completes. Force-fails sequence if exceeded. Default: 120000 (2 min). Set 0 to disable."), false),
			FMCPToolParameter(TEXT("start_pie"), TEXT("boolean"),
				TEXT("start_monitor: automatically start PIE if not already running (default: false)"), false),
			FMCPToolParameter(TEXT("interval_ms"), TEXT("number"),
				TEXT("start_monitor: state snapshot interval in ms (default: 500)"), false),
			FMCPToolParameter(TEXT("axis_threshold"), TEXT("number"),
				TEXT("start_monitor: dead zone for axis edge detection (default: 0.15)"), false),
			FMCPToolParameter(TEXT("log_axes"), TEXT("boolean"),
				TEXT("start_monitor: log axis values every tick (~60Hz) when active. Enables [PIE-DBG] AXIS lines for precise input replay. (default: false)"), false),
			FMCPToolParameter(TEXT("values"), TEXT("array"),
				TEXT("input_tape step: per-frame values array. Each entry: [x,y] for Axis2D, [x] or number for Axis1D. One value = one game frame at 60fps."), false),
			FMCPToolParameter(TEXT("montage_path"), TEXT("string"),
				TEXT("play_montage/montage_jump_to_section: path to UAnimMontage asset"), false),
			FMCPToolParameter(TEXT("component_name"), TEXT("string"),
				TEXT("play_montage/montage_stop/montage_jump_to_section: SkeletalMeshComponent name (empty = character mesh)"), false),
			FMCPToolParameter(TEXT("play_rate"), TEXT("number"),
				TEXT("play_montage: playback rate (default: 1.0)"), false),
			FMCPToolParameter(TEXT("start_section"), TEXT("string"),
				TEXT("play_montage: section to start at after playing"), false),
			FMCPToolParameter(TEXT("section_name"), TEXT("string"),
				TEXT("montage_jump_to_section: section name to jump to"), false),
			FMCPToolParameter(TEXT("blend_out_time"), TEXT("number"),
				TEXT("montage_stop: blend out duration in seconds (default: 0.25)"), false),
			FMCPToolParameter(TEXT("take_record"), TEXT("boolean"),
				TEXT("run_sequence: enable Take Recorder during PIE sequence (default: false)"), false),
			FMCPToolParameter(TEXT("take_slate"), TEXT("string"),
				TEXT("run_sequence: custom slate name for the take (only with take_record)"), false)
		};
		Info.Annotations = FMCPToolAnnotations::Modifying();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult ExecuteStartPIE(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteStopPIE(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecutePIEStatus(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteInjectInput(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteStartContinuous(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteUpdateContinuous(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteStopContinuous(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteCapturePIE(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteSequence(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteSequenceStatus(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteRunSequence(const TSharedRef<FJsonObject>& Params);

	UInputAction* LoadInputAction(const FString& Path, FString& OutError);
	FInputActionValue BuildInputValue(UInputAction* Action, const TSharedRef<FJsonObject>& Params);

	struct FContinuousInjection
	{
		FString Id;
		TWeakObjectPtr<UInputAction> Action;
		FInputActionValue Value;
		FTimerHandle TimerHandle;
	};

	struct FMonitoredActionState
	{
		TWeakObjectPtr<const UInputAction> Action;
		FString ActionName;
		EInputActionValueType ValueType = EInputActionValueType::Boolean;
		FInputActionValue LastValue;
		bool bWasActive = false;
	};

	struct FPIEMonitor
	{
		bool bActive = false;
		bool bPIEAttached = false;
		FTSTicker::FDelegateHandle TickerHandle;
		double StartTimeSeconds = 0.0;
		double LastStateLogTime = 0.0;
		float IntervalSeconds = 0.5f;
		float AxisThreshold = 0.15f;
		bool bLogAxesPerTick = false;
		int32 EventsLogged = 0;
		TArray<FMonitoredActionState> TrackedActions;
	};

	FMCPToolResult ExecutePlayMontage(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteMontageJumpToSection(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteMontageStop(const TSharedRef<FJsonObject>& Params);

	FMCPToolResult ExecuteStartMonitor(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteStopMonitor(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult ExecuteMonitorStatus(const TSharedRef<FJsonObject>& Params);
	bool MonitorTick(float DeltaTime);
	void MonitorAttachToPIE(UWorld* PIEWorld);
	void MonitorDetachFromPIE();
	void MonitorLogInputEdges(UWorld* PIEWorld);
	void MonitorLogStateSnapshot(UWorld* PIEWorld);
	int64 MonitorGetElapsedMs() const;

	TMap<FString, TSharedPtr<FContinuousInjection>> ContinuousInjections;
	int32 NextContinuousId = 1;

	FPIEMonitor Monitor;
	TSharedPtr<FPIESequenceRunner> ActiveSequence;
	TSharedPtr<FPIEFrameGrabber> FrameGrabber;
};
