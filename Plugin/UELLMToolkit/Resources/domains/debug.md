# Debug Domain — PIE Automation & Diagnostics

## gameplay_debug Tool

Primary tool for PIE (Play-In-Editor) automation. Handles start/stop, input injection via Enhanced Input, viewport capture, and timed sequences.

### Preferred: run_sequence

Single-call atomic lifecycle: starts PIE, settles, executes typed steps, writes manifest.json + named captures to disk, stops PIE.

```bash
ue-tool.sh call gameplay_debug '{"operation":"run_sequence","steps":[...],"settle_ms":500,"output_dir":"D:/path/output/"}'
```

Returns immediately with `output_dir` + `estimated_duration_ms`. Sleep, then read results.

### sequence_file

Load steps from a JSON file instead of inline:
```bash
ue-tool.sh call gameplay_debug '{"operation":"run_sequence","sequence_file":"D:/path/to/sequence.json"}'
```
File format: `{"steps":[...]}` (top-level keys `name`/`settle_ms`/`output_dir`/`auto_capture_every_n_frames` act as defaults) or bare `[...]` array. Inline params always override file values.

## Step Types

### input (Single Frame)
```json
{"type": "input", "action": "/Game/Input/IA_Move", "value_x": 0, "value_y": 1, "delay_ms": 0}
```
Single-frame value injection. Good for axis actions (IA_Move). **Not suitable for digital actions** — see `hold`.

### hold (Sustained)
```json
{"type": "hold", "action": "/Game/Input/IA_Equip", "value": true, "duration_ms": 100, "delay_ms": 500}
```
Sustains a value across multiple frames. **Required for digital actions** (IA_Equip, IA_Dodge, IA_Jump, etc.) — Enhanced Input's `Pressed` trigger needs a false-to-true state transition across frames.

### capture (Screenshot)
```json
{"type": "capture", "name": "after_attack", "delay_ms": 200}
```
Saves a named screenshot to the output directory.

### console (Console Command)
```json
{"type": "console", "command": "stat fps"}
```
Executes any console command mid-sequence. ~1 frame latency.

### input_tape (Per-Frame Injection)
```json
{"type": "input_tape", "action": "/Game/Input/IA_Move", "values": [[0,1],[0,1],[0.5,1],...]}
```
Per-frame value injection at 60fps via `FCoreDelegates::OnEndFrame`. Zero inter-frame gaps (unlike timer-based `hold`). Multiple tapes run in parallel. Each entry = one game frame. Value formats: `[x,y]` for Axis2D, `[x]` or bare number for 1D/Boolean, `[x,y,z]` for 3D.

## Digital vs Axis Actions

| Action Type | Step Type | Why |
|-------------|-----------|-----|
| Axis (IA_Move, IA_Look) | `input` or `hold` | Continuous values work on any frame |
| Digital (IA_Equip, IA_Dodge) | `hold` (100ms+) | Needs false-to-true transition across frames |

### Consecutive Digital Presses
Back-to-back holds on the same digital action: insert a zero-value hold (50ms) between presses for the inactive-to-active transition.

### IA_Move Axis Mapping
`value_x` = strafe (-1=left, +1=right), `value_y` = forward/back (+1=forward, -1=backward). Bypasses IMC modifiers.

## delay_ms Semantics

`delay_ms` is relative to the **previous step's START**, not absolute from settle. For back-to-back holds, use `delay_ms` = previous step's `duration_ms`.

## Auto-Capture

```json
{"operation": "run_sequence", "auto_capture_every_n_frames": 2, "steps": [...]}
```
Async GPU readback via UE's `FFrameGrabber`. Near-zero game thread stall, GPU-side resize to 1024x576. Files: `f{frame}_t{time}.jpg`.

## Output Log

### get_output_log
```bash
ue-tool.sh call get_output_log '{"since": true, "categories": ["LogTemp"], "min_verbosity": "Warning"}'
```

### Cursor Semantics
The `since` cursor is **byte-offset based, not timestamp-based**. It tracks how far into the file the last call read.
- `since: true` — returns only bytes appended after the previous call's read position
- `since: false` — reads from start, but also advances the cursor

**Common trap**: Calling `get_output_log` with `since` after data was already written returns 0 lines — the cursor is past it. For data from a completed sequence, grep the log file directly.

### Post-Hoc Log Analysis
After PIE finishes, grep the log file directly — it's just a text file:
```
Grep pattern="MyLogTag" path="<ProjectDir>/Saved/Logs/<Project>.log"
```
This is instant, supports regex, and has no token overhead.

### Useful Filters
- `strip_timestamps: true` — reduces token count
- `compact: true` — collapses duplicate lines
- Noisy categories to exclude: `LogStreaming`, `LogLinker`, `LogEOSSDK`

## PIE Technical Notes

- **PIE start is async** — `RequestPlaySession()` queues for next tick. The runner handles this; poll `pie_status` for manual ops.
- **Editor throttle** — PIE runs at ~3fps when editor is not focused. The runner forces `t.MaxFPS 60` + steals foreground.
- **Spring arm follows pawn** — captures always show camera behind character.
- **Live reload crashes on struct changes** — always do a full rebuild after struct changes, not live coding.
- **Stale PIE data** — after code changes, old run data is invalid. Always re-run sequences.
- **Editor world timers stop during PIE** — use `FTSTicker::GetCoreTicker()` for polling that must survive PIE transitions.

## Monitor Mode

Passive observation: every-frame input edge detection + periodic state snapshots. All logged as `[PIE-DBG]` lines.

```bash
ue-tool.sh call gameplay_debug '{"operation":"start_monitor","interval_ms":100,"start_pie":true,"log_axes":true}'
```

Auto-stops on PIE end when `log_axes: true`. Read via `get_output_log {"filter":"[PIE-DBG]"}`.

Use for **debug observation** (raw Enhanced Input inspection, IMC diagnostics) — not for record/replay.
