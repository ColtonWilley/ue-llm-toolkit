# UE LLM Toolkit

**Give your AI the ability to actually *see inside* your Unreal project.**

A pure C++ plugin that exposes **37 tools** and **200+ operations** over HTTP, giving LLMs read/write access to major editor subsystems. Drop it into any UE 5.7 project and get a `localhost` REST API that works with [Claude Code](https://claude.ai/claude-code), Cursor, Windsurf, or any HTTP client. No Python runtime in the editor. No MCP lock-in. Just HTTP + JSON.

This is **not** a "prompt-to-game" tool. It won't generate a forest or build you a castle from a sentence. This is a **debugging, analysis, and productivity tool** for developers who are already building something in Unreal and want an AI that can actually help — one that can read their Blueprint graphs, trace their animation state machines, inspect their montage notifies, and understand what their project is doing well enough to be useful.

> Forked from [Natfii/UnrealClaude](https://github.com/Natfii/UnrealClaude) by [Natali Caggiano](https://github.com/Natfii) and significantly extended.

## Features

`.uasset` files are binary. Blueprint logic lives in graph editors, not text files. Your AI can see your C++ but is blind to everything in the editor. This plugin fixes that.

| Domain | Read | Write |
|--------|------|-------|
| **Blueprints** | Walk event graphs, trace exec chains, targeted node queries (find by class/label, get pin data, verify connections), inspect components, variables, defaults, collision | Create Blueprints, add/delete nodes, wire pins, set values, add variables/functions, compile, auto-layout |
| **Anim Blueprints** | State machine diagrams (ASCII + JSON), transition rules, condition nodes, linked anim layer interfaces and graphs | Create state machines, add states/transitions, wire transition logic, assign animations, configure blend nodes, manage animation layer interfaces |
| **Montages** | Full structure — sections, segments, notifies, blend curves, float curve keys | Create montages, add/remove sections, segments, notifies; set blend in/out; add/edit curves |
| **Blend Spaces** | Axes, samples, interpolation, geometry | Create 1D/2D, add/move/remove samples, configure axes |
| **Anim Sequences** | Per-frame bone transforms at sampled frames | Adjust bone tracks, resample frame rates, replace skeletons |
| **PIE Automation** | Start/stop, status, always-on input recording, list past recordings | Input injection, timed replay sequences, deterministic record/replay at 60fps, viewport capture during playback |
| **Control Rigs** | Full RigVM graph — nodes, pins, links, hierarchy | Add/remove nodes, link/unlink pins, set defaults, recompile |
| **IK Retargeters** | Skeleton hierarchies, bind poses, bone comparison | Create rigs/retargeters, add chains, auto-map, batch retarget, diagnose health |
| **Level & Actors** | List actors by class, search by name, detailed dumps (transform, components, collision) | Spawn, move, rotate, scale, delete, set any property |
| **Assets** | Metadata, content hashes, dependency trees, reverse references | Import/export FBX, save, duplicate, rename, delete, migrate |
| **Enhanced Input** | Query actions and mapping contexts | Create actions/contexts, add bindings, triggers, modifiers |
| **UMG Widgets** | Inspect widget trees, properties, layout slots | Create widgets, add/remove children, set properties |
| **Characters** | List, inspect mesh/animation/components, query movement params | Set movement params, manage data assets and stats tables |
| **Materials** | Query details | Create instances, set parameters, assign to meshes *(early — lightly tested)* |
| **Diagnostics** | Output log (filtered, incremental), viewport screenshots, asset previews | Console commands, script execution (C++/Python/console batch) |
| **Build** | Auto-detect editor/VS state | Smart build dispatch — live coding → VS → full rebuild escalation, editor lifecycle management |

## Install

Copy `Plugin/UELLMToolkit/` and `Scripts/` into your project:

```
YourProject/
  Plugins/
    UELLMToolkit/     <-- copy Plugin/UELLMToolkit/ here
  Scripts/
    ue-tool.sh        <-- copy Scripts/ here
    build.sh
  Content/
  Source/              (if you have C++)
  YourProject.uproject
```

Open your `.uproject` in Unreal Editor — it will compile the plugin automatically. (If you have a C++ project, you can also regenerate project files and build from Visual Studio.) The plugin starts an HTTP server on `localhost:3000`.

Verify:

```bash
curl http://localhost:3000/mcp/status
```

## Quick Start

### **Always load a domain first.**

Unreal is too large and complex for an LLM to hold everything it needs in context at once. The toolkit uses a **domain system** — token-efficient summaries of your project that get loaded into context before you start working.

**One-time setup** (requires editor running with plugin loaded):

1. Copy `Plugins/UELLMToolkit/CLAUDE.md.default` to `CLAUDE.md` in your project root
2. Tell your AI:
```
prep one time init
```

The AI uses the plugin tools to explore your project — queries your modules, characters, anim blueprints, blend spaces, skeletons, input actions, content structure — and writes concise domain files to `domains/`. Because the LLM is doing the scanning, it decides what's relevant and keeps the files token-efficient rather than dumping everything. You only need to do this once. It should be noted for large projects this could
be token expensive, but it is a one time cost designed to save you tokens in the long run.

**Then, at the start of every conversation:**

```
prep blueprints
```

Available domains: `code`, `blueprints`, `assets`, `debug`. Combine them: `prep blueprints + code`.

**If you skip this step, the AI is working blind.** Unreal specific knowledge is wrapped into domains. Always prep first.

### How NOT to use this

> *"Implement a locomotion system for my character"*

This will not work well. The AI doesn't know your skeleton, your existing animations, your anim blueprint structure, your state machine layout, or your C++ movement code. A vague prompt produces vague results.

### How to use this

> *prep code blueprints*
> *"I have idle, walk, and run animations in `/Game/Animations/Locomotion/`. Make a plan to implement basic idle/walk/run blending. Use a 1D blend space driven by ground speed. Consider what needs to change in C++, the character Blueprint, the anim graph, and the locomotion state machine."*

The AI can now query your anim blueprint structure, inspect what states and transitions already exist, check your C++ character class for movement variables, look at the animations you're pointing it to, and build a plan grounded in your actual project — not a generic template.

### Debugging Your Game

> *prep debug + code*
>
> *"My light attack is getting interrupted early. Grab my last PIE recording, recreate it as a replay sequence, add UE_LOG instrumentation to the attack flow — montage callbacks, notify fires, movement input — then run the replay and show me what's actually happening. Don't fix anything yet, just find root cause."*

The AI grabs the recording, instruments your code, rebuilds, replays the exact inputs in PIE, reads the output log, and tells you exactly which callback fired out of order and why. No screenshots, no copy-pasting logs, no describing symptoms back and forth. More on this pattern in [Debugging — Diagnose, Then Fix](#debugging--diagnose-then-fix) below.

**The plugin is a multiplier on your expertise, not a replacement for it.** You know what you want to build. The AI can now see enough of your project to help you build and debug it.

## Example Workflows

The pattern is always the same: **prep a domain, give specific context, make a plan, tune plan, execute plan.**

### General Tips

- **Always prep first.** Context is everything. The AI with domain knowledge loaded is a different tool than the AI without it.
- **Specify your assets.** Point the AI at specific paths. AI does very poorly guessing which assets to use, and will burn time and tokens trying to figure it out. "I have animations in `/Game/Animations/Combat/`" beats "I have some attack animations somewhere."
- **Scope tightly.** "Add light and heavy attacks" is better than "implement a combat system." Small, concrete tasks with clear boundaries get better results. New task = new chat with appropriate domains loaded.
- **Ask for a plan before execution.** Let the AI inspect and propose before it starts editing. Plans are cheap, undoing bad edits is not.
- **Say what you DON'T want.** "No combos yet," "don't touch the existing state machine," "keep the current blend space" — constraints help as much as requirements.
- **Update your domains.** After finishing a non-trivial task, ask the AI to update the relevant domain file with what it learned. Your domains get smarter over time.

### 8-Way Strafe Locomotion

**Bad:**
> *"Add strafing to my character"*

The AI doesn't know your skeleton, what animations you have, whether you're using a 2D blend space or state machine, or how your movement code feeds direction into the anim blueprint. It will guess — and guess wrong.

**Good:**
> *prep code + blueprints*
>
> *"I want to add 8-way strafe locomotion. I have directional animations (F, B, L, R, FL, FR, BL, BR) at `/Game/Animations/Strafe/` for walk and run. Currently using a 1D blend space for forward locomotion only. Make a plan — I think I need a 2D blend space driven by direction and speed, changes to the anim graph to swap in the new blend space, and C++ changes to calculate a strafe direction angle relative to the character's facing."*

Now the AI can inspect your existing blend space, look at the anim graph to see how it's currently wired, check your C++ for how movement input maps to animation variables, preview the strafe animations to verify they exist, and build a concrete plan against your actual setup.

### Light / Heavy Attacks

**Bad:**
> *"Implement a light and heavy attack combo system"*

"Combo system" means wildly different things. This prompt gives the AI no constraints — it'll either over-engineer something you don't want or produce something too generic to be useful.

**Good:**
> *prep code + blueprints*
>
> *"I want basic light (R1) and heavy (R2) attacks. I have montages `AM_Attack_Light` and `AM_Attack_Heavy` in `/Game/Animations/Combat/`. For now, just single attacks — no chaining, no combos. R1 plays the light montage, R2 plays heavy. The player shouldn't be able to interrupt one attack with another until recovery. I already have Enhanced Input actions IA_LightAttack and IA_HeavyAttack bound. Make a plan for the C++ side and any montage notify setup needed."*

The AI can inspect the montages (sections, notifies, blend times), check the input actions exist, read your character class to see how input is currently handled, and plan the implementation knowing exactly what you want and what you don't (no combos yet).

### Debugging — Diagnose, Then Fix

This is where the toolkit really shines. Most AI coding tools can help you write new code or blueprints. Very few can help you figure out why existing code is broken.

**Bad:**
> *"Something is wrong with my attack animation, it snaps back to idle before the montage finishes. Can you fix it?"*

The AI can't see the problem. It'll speculate about common causes, suggest generic fixes, and you'll go back and forth describing symptoms while it guesses.

**Good — Session 1 (diagnosis):**
> *prep debug + code*
>
> *"My light attack montage is getting interrupted early — the character snaps back to idle about halfway through AM_Attack_Light. Look at my last PIE recording and recreate it as a replay sequence. Then add UE_LOG instrumentation to the attack flow — montage start/end callbacks, any notify fires, and the movement input handler. Run the replay and let's look at what's actually happening. Don't try to fix anything yet — goal is root cause."*

The AI grabs the last recording, builds a replay sequence, adds targeted debug logging to your C++ code, rebuilds, runs the replay in PIE, reads the output log, and shows you exactly what happened: which callback fired, what notify triggered early, what state changed when. If the first pass isn't enough, iterate — *"add more logging around the recovery cancel path and run it again."*

Once root cause is clear, **Produce a summary of the root cause and close the session.** The diagnosis context is spent.

**Good — Session 2 (fix):**
> *prep code + blueprints*
>
> *"Root cause: the RecoveryCancelOpen notify is placed at 0.45s in AM_Attack_Light, but the combo window opens at the same time, so movement input during the combo window is also triggering recovery cancel.
Lets look online to understand best practices for implementing this type of attack system, understand the root cause, and make a plan to fix the current implementation."*

Clean context, specific problem, specific fix. The AI reads the relevant code, makes the targeted change, and you're done.

**The two-session pattern — diagnose then fix — is intentional.** Diagnosis fills context with log output, replays, and iteration. Fixing needs clean context focused on the code change. Don't try to do both in one conversation.

### Custom Domains — Make It Yours

The built-in domains (`code`, `blueprints`, `assets`, `debug`) are generated by `prep one time init`, but the system is designed to be extended. You can create your own domain files and the AI will load them with `prep <name>`.

Created a combat system? Write a `domains/combat.md` that documents your montage naming conventions, notify patterns, combo state machine, and known gotchas. Next time you `prep combat`, all of that context is loaded automatically.

The `meta` domain is for maintaining the domain system itself:

> *prep meta*
>
> *"I just finished implementing the combo system. Update the combat domain with what we learned — the montage notify pattern, the frame guard for stale notifies, and the recovery cancel constraint."*

Over time your domains become a living knowledge base of your project — written by the AI based on what it actually encountered while working on your code. This is what makes the second, third, and hundredth session productive instead of starting from scratch every time.

### CLI Wrapper — `ue-tool.sh`

This is not a convenience script. It's a **core tooling layer** that sits between the LLM and the plugin HTTP API. Without it, every tool call costs the LLM tokens to construct a `curl` command, guess parameter names, parse raw JSON responses, and handle connection errors. With it, calls are one-liners and responses come back pre-formatted for LLM consumption.

In practice this **reduces token usage by ~70%** on typical tool-heavy sessions. The LLM spends tokens on your problem, not on HTTP plumbing.

What it does:
- **`help <tool>`** — parameter docs inline, so the LLM knows what to pass without guessing
- **`call <tool> '{...}'`** — handles connectivity checks, error formatting, and compact JSON output
- **`save`** — save all dirty assets (one word instead of a JSON POST)
- **`list`** — all available tools at a glance
- **`--port`** — multi-editor support for staging workflows

```bash
bash Scripts/ue-tool.sh list
bash Scripts/ue-tool.sh help blueprint_query
bash Scripts/ue-tool.sh call asset_search '{"search_term":"Character"}'
bash Scripts/ue-tool.sh save
```

### Raw HTTP API

If you're not using the CLI wrapper (heavily not recommended! please use ue-tool.sh), all tools are available via `POST /mcp/tool/<tool_name>` with a JSON body.

```bash
# Query a Blueprint's event graph
curl -X POST http://localhost:3000/mcp/tool/blueprint_query \
  -H "Content-Type: application/json" \
  -d '{"asset_path":"/Game/Blueprints/BP_Hero","operation":"get_event_graph"}'

# Inspect an animation state machine
curl -X POST http://localhost:3000/mcp/tool/anim_blueprint_modify \
  -H "Content-Type: application/json" \
  -d '{"asset_path":"/Game/Blueprints/ABP_Hero","operation":"get_state_machine_diagram","state_machine":"DefaultGroup"}'

# Save all
curl -X POST http://localhost:3000/mcp/tool/asset \
  -H "Content-Type: application/json" \
  -d '{"operation":"save_all"}'
```

Discovery endpoints:
- `GET /mcp/tools` — list all tools with full parameter schemas
- `GET /mcp/status` — plugin health check

## Configuration

### Port

Default port is `3000`. Override via command line:

```
UnrealEditor.exe MyProject.uproject -MCPPort=3001
```

Or with the CLI wrapper:

```bash
bash Scripts/ue-tool.sh --port 3001 call asset_search '{"search_term":"Hero"}'
```

### MCP Bridge (Optional)

For MCP-native clients (Claude Desktop, etc.), a Node.js bridge is included:

```bash
cd Plugin/UELLMToolkit/Resources/mcp-bridge
npm install
node index.js
```

### Build Script — `build.sh`

Smart build dispatcher that auto-detects your editor/VS state and picks the right strategy. No hardcoded paths — it finds your `.uproject`, `.sln`, Visual Studio, and UBT automatically.

**Note:** `build.sh` requires a C++ project (a `Source/` directory with Target.cs and at least one module). If you have a Blueprint-only project, just open the `.uproject` in Unreal Editor directly — it will compile the plugin automatically. You can add C++ later via "Tools → New C++ Class" in the editor, which creates the Source folder and enables `build.sh`.

```bash
bash Scripts/build.sh           # auto-detect and build (default)
bash Scripts/build.sh --live    # force live coding (editor must be running)
bash Scripts/build.sh --vs      # force VS build
bash Scripts/build.sh --clean   # close editor + VS build
bash Scripts/build.sh --full    # close everything, regenerate project files, rebuild from scratch
```

**Auto mode** (`--auto`) escalates automatically:
1. Editor running? Try **live coding** first (fastest — no editor restart)
2. Live coding failed? Close editor, try **VS build**
3. Linker errors suggesting stale state? **Full rebuild** (regen project files + VS build)
4. After successful build, relaunches the editor and waits for plugin connectivity

Use `--full` when you've added/removed source files, changed `UCLASS`/`UPROPERTY` macros, or modified `.Build.cs` — anything that changes the module structure.

Full output goes to `/tmp/ue-build.log`. Stdout is minimal status lines designed for LLM consumption.

If auto-detect can't find VS or UBT, set `VS_DEVENV` and `UE_UBT` environment variables to the full paths.

## Tool Reference

Use `bash Scripts/ue-tool.sh help <tool_name>` for full parameter docs. Quick index:

| Domain | Tools |
|--------|-------|
| Blueprints | `blueprint_query`, `blueprint_modify` |
| Animation | `anim_blueprint_modify`, `montage_modify`, `blend_space`, `anim_edit`, `control_rig`, `retarget` |
| Level & Actors | `level_query`, `get_level_actors`, `open_level`, `spawn_actor`, `move_actor`, `delete_actors`, `set_property` |
| Assets | `asset`, `asset_search`, `asset_import`, `asset_dependencies`, `asset_referencers` |
| Materials | `material` |
| Characters | `character`, `character_data` |
| Input | `enhanced_input` |
| UI / Widgets | `widget_editor` |
| Diagnostics | `capture_viewport`, `get_output_log`, `run_console_command`, `gameplay_debug` |
| Scripting | `execute_script`, `cleanup_scripts`, `get_script_history` |
| Async | `task_submit`, `task_status`, `task_result`, `task_list`, `task_cancel` |

## Architecture

```
Plugin/UELLMToolkit/
  Source/UELLMToolkit/
    Private/
      MCP/                    # HTTP server + tool registry
        Tools/                # Tool implementations (MCPTool_*.h/.cpp)
      Scripting/              # Script execution engine
      Tests/                  # Automation tests
      Widgets/                # Editor widgets
      *.cpp / *.h             # Animation, Blueprint, BlendSpace, Montage,
                              # ControlRig, Retarget, Asset, PIE helpers
    Public/                   # Module headers, subsystem, constants
```

Each tool follows the `MCPToolBase` pattern: register parameters, implement `Execute()`, return JSON. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to add new tools.

## Planned Features

This toolkit grows alongside an actual game. I'm building a 3D action RPG in UE 5.7, and I extend the tooling as my development needs it — every feature here exists because I needed it while making a real game.

Likely next areas as I get to them in my own dev:
- **Niagara** — particle system inspection and editing
- **UMG Widgets** — deeper widget tooling (current coverage is basic)
- **Dialogue / Data Tables** — broader data asset support
- **Sequencer** — cinematic and cutscene inspection
- And whatever else I need as I need it

If you need something that isn't covered, see [CONTRIBUTING.md](CONTRIBUTING.md) — the tool pattern is straightforward to extend.

## Requirements

- Unreal Engine 5.7
- Windows (Win64) or Linux
- Python 3.x (for CLI wrapper output formatting — optional)
- Node.js 18+ (for MCP bridge — optional)

### Version Support

This plugin is built and tested against **UE 5.7 only**. Unreal's internal APIs (Blueprint graph structures, animation node types, asset import pipeline, etc.) change between engine versions. Older versions like 5.5 or 5.6 will likely have compile errors or runtime failures — we don't test against them and can't guarantee anything works.

If you're on an older version and want to try porting, the tool pattern itself is straightforward — it's the UE API calls inside each tool that would need updating. PRs for version compatibility are welcome.

## License

[MIT](LICENSE)

## Credits

- **Original plugin**: [Natali Caggiano](https://github.com/Natfii) — [UnrealClaude](https://github.com/Natfii/UnrealClaude)
