# `prep one time init` — LLM-Driven Project Scan

This document is the full procedure for scanning a UE project and generating project-specific domain files. **Read this file in full before starting. Do not skim.**

You will use the plugin HTTP API (via `ue-tool.sh` or `curl localhost:3000`) to explore the project, then write 4 domain files to `domains/` in the project root. The goal is token-efficient summaries — not raw dumps.

**Critical rule**: Before calling any tool for the first time during this procedure, run `ue-tool.sh help <tool>` to verify exact parameter names and available operations. Parameter names and operations vary across plugin versions — do not assume. The examples below use common names but yours may differ.

---

## Phase 1: Discovery

Run all of these. Each scan is independent — if one fails, skip it and note the gap.

### 1.1 Project metadata

```bash
curl -s http://localhost:3000/mcp/status
```

Extract: `projectName`, `projectDirectory`, `engineVersion`.

### 1.2 Module structure

Read `Source/*/*.Build.cs` files (use Glob to find them). Extract:
- Module name (filename stem)
- Export macro (grep for `_API` pattern)
- `PublicDependencyModuleNames`

If no `Source/` directory exists, this is a **Blueprint-only project**. Note it and skip C++-related scans.

### 1.3 Source directories

List top-2-level directories under `Source/<Module>/`. This gives the code organization (Characters/, Combat/, Animation/, etc.).

### 1.4 Asset inventories

Run `ue-tool.sh help asset_search` first to confirm parameter names. Then search with class filters:

| Search | Purpose |
|--------|---------|
| `class_filter: "Blueprint"` | All Blueprints |
| `class_filter: "AnimBlueprint"` | All AnimBPs |
| `class_filter: "Skeleton"` | All skeletons |
| `class_filter: "BlendSpace"` | 2D blend spaces |
| `class_filter: "BlendSpace1D"` | 1D blend spaces |
| `class_filter: "InputAction"` | Enhanced Input actions |
| `class_filter: "InputMappingContext"` | Input mapping contexts |

Use `search_term: ""` (empty) with class filters to get all assets of that type. If results have `hasMore: true`, paginate with `offset`.

### 1.5 Enhanced Input

Input actions and mapping contexts are found via `asset_search` (see 1.4 above). If no InputAction assets are found, the project may use legacy input.

### 1.6 Level info

Run `ue-tool.sh help level_query` to find the correct operation for listing actors. Common operations: `list`, `find`, `info`. Example:

```
level_query: { "operation": "list" }
```

Extract: map name, total actor count, actor types by class.

---

## Phase 2: Deep Inspection

Identify the **player character**, then inspect it deeply. Everything else gets shallow coverage.

### Player Detection Heuristic

Try these in order. Stop at the first confident match.

1. **Name match**: A Blueprint whose name contains "Player", "Hero", or "Main" (case-insensitive) but NOT "Enemy", "NPC", or "AI"
2. **GameMode default pawn**: Search for a GameMode Blueprint, inspect its `DefaultPawnClass`
3. **Only one Character BP**: In small projects with a single Character/Pawn BP, that's the player
4. **Most components**: If ambiguous, pick the Character BP with the most components

If no match is found, list candidates and write `[Could not auto-detect player character]` in the output files.

### Deep Inspection Targets

**Run `ue-tool.sh help <tool>` for each tool before calling it.** Parameter names vary — `blueprint_query` and `anim_blueprint_modify` typically use `blueprint_path`, while `blend_space` uses `asset_path`. Operation names also vary across versions.

Inspect the player character and its immediate adjacents. **Maximum 5 deep inspections total.**

| Target | Tool | What to Do | What to Extract |
|--------|------|------------|-----------------|
| Player BP | `blueprint_query` | `get_components` operation | Component tree (mesh, capsule, movement, custom) |
| Player BP | `blueprint_query` | `inspect` with `include_variables: true` | Parent class, key state variables |
| Player AnimBP | `blueprint_query` | `get_graph_summary` | All graphs — state machine names, state counts, variable usage |
| Player AnimBP | `anim_blueprint_modify` | `get_info` | Skeleton, state machine list with state names |
| Player blend spaces | `blend_space` | `inspect` operation | Axes, samples, interpolation |

The AnimBP `get_graph_summary` is the most token-efficient way to get the full picture — it returns all state machines, states, and variable references in one call. Use `get_info` for the skeleton name and state machine overview.

Also read 2-3 key `.h` files if they exist:
- Player character header → class hierarchy, key UPROPERTY/UFUNCTION
- Anim instance header → animation variables, class hierarchy

### What NOT to Inspect

- Individual NPC/enemy BPs (count + names only)
- Materials, UI widgets, environment actors
- Full event graph dumps
- Every blend space (only player-related ones)
- More than 3 header files

---

## Phase 3: Write Domain Files

Write 4 files to `domains/` in the project root (sibling to `Content/`, `Source/`, etc.). Create the `domains/` directory if it doesn't exist.

**Token budget per file:**
- Target: 500-1500 tokens
- Hard ceiling: ~2000 tokens
- If a section would exceed budget, summarize more aggressively

### Scaling Rules (Critical for Large Projects)

These rules prevent output bloat regardless of project size:

- **Blueprints >20**: Group by parent class. Example: `"147 BPs total: 1 player (CharacterBase), 12 enemies (EnemyBase), 8 weapons (WeaponBase), 23 UI widgets, 103 other"`
- **AnimBPs >10**: Group by skeleton. Example: `"15 AnimBPs on Mannequin_Skeleton, 3 on Creature_Skeleton"`
- **Content folders**: Top-level only with asset counts. No subfolder enumeration.
- **Input actions >15**: Group by prefix/type instead of listing each one.
- **Variables >20**: List the 10 most important, summarize the rest.

---

## Output Templates

### `code.md`

```markdown
# Project Code Domain — {ProjectName}

## Class Hierarchy

AActor
  └─ ACharacter
       └─ A{Project}CharacterBase    (Source/{Module}/...)
            ├─ A{Project}PlayerCharacter (Source/{Module}/...)
            └─ A{Project}EnemyBase       (Source/{Module}/...)

Key classes:
- `AProjectCharacterBase` — {1-line summary: what it owns, key properties}
- `AProjectPlayerCharacter` — {1-line: input handling, unique mechanics}
- `UProjectAnimInstance` — {1-line: key anim params like GroundSpeed, Direction}

## Module

**{ModuleName}** (`Source/{ModuleName}/{ModuleName}.Build.cs`)
- Export macro: `{MODULE_API}`
- Dependencies: Core, CoreUObject, Engine, ...
- Source dirs: Characters/, Combat/, Animation/, ...

## Key Headers

- `{Path}/PlayerCharacter.h` — {what to find here}
- `{Path}/AnimInstance.h` — {what to find here}
```

**Blueprint-only projects**: Replace the entire Class Hierarchy section with:
```markdown
## Project Type

Blueprint-only project — no C++ source module. Parent classes are engine defaults (ACharacter, AGameModeBase, etc.).
```

### `blueprints.md`

```markdown
# Project Blueprints Domain — {ProjectName}

## Player AnimBP

**ABP_Player** (`/Game/...`) — Skeleton: `SkeletonName`
- State machines: Locomotion (4 states: Idle, Walk, Run, Jump), Combat (3 states: ...)
- Key variables: GroundSpeed (Float), Direction (Float), bIsAttacking (Bool)

## Animation Blueprints

{N} total. Player: ABP_Player. Others: ABP_Enemy_Skeleton, ABP_NPC_Villager, ...
{If many: "12 enemy AnimBPs on UE4_Mannequin_Skeleton, 3 creature AnimBPs on Creature_Skeleton"}

## Blend Spaces

| Name | Type | Axes |
|------|------|------|
| BS_Locomotion | 2D | Speed (0-600), Direction (-180..180) |
| BS_Strafe | 1D | Direction (-180..180) |

{Only player-related blend spaces get the detailed row. Others: "5 additional blend spaces (NPC locomotion)"}

## Key Blueprints

- **BP_PlayerCharacter** (`/Game/...`) — parent: AProjectPlayerCharacter
  Components: SkeletalMesh, Camera, SpringArm, {custom components}
- **BP_GameMode** (`/Game/...`) — parent: AGameModeBase
- **BP_Weapon_Sword** (`/Game/...`) — parent: AProjectWeapon
{Max ~10 entries. Group if many: "8 weapon BPs (ProjectWeapon subclass)"}
```

### `assets.md`

```markdown
# Project Assets Domain — {ProjectName}

## Skeleton

{Single/Multiple} skeleton(s). Master: `{path}`
{If multiple: list each + what uses it}

## Content Structure

Content/
  Blueprints/     — {N} BPs (Player, Enemies, Weapons)
  Characters/     — Skeletal meshes, physics assets
  Animations/     — {N} sequences, {M} montages
  Maps/           — {map names}
  Materials/      — {N} materials
  {VendorPack}/   — Third-party content from {vendor}

## Key Paths

- Player mesh: `{path}`
- Player animations: `{folder}`
- Master skeleton: `{path}`
```

### `debug.md`

```markdown
# Project Debug Domain — {ProjectName}

## Log File

`{ProjectDir}/Saved/Logs/{ProjectName}.log`

## Input Actions

For PIE injection (hold, input, input_tape steps):

| Action | Path | Type |
|--------|------|------|
| IA_Move | /Game/Input/... | Axis2D |
| IA_Look | /Game/Input/... | Axis2D |
| IA_Jump | /Game/Input/... | Bool |

## Mapping Context

`{IMC_path}`

## Current Map

`{map_name}` — {actor_count} actors
Key actor types: {top 3-5 actor classes by count}
```

---

## Error Handling

Each scan step is independent. A failure in one does not abort the rest.

| Situation | Action |
|-----------|--------|
| Tool timeout or error | Skip that scan. Write `[scan incomplete — {tool} did not respond]` in the relevant section |
| No `Source/` directory | code.md says "Blueprint-only project". Skip class hierarchy, module, headers |
| No AnimBPs found | blueprints.md notes "No animation blueprints found" |
| No Enhanced Input | debug.md notes "No Enhanced Input actions found" |
| No skeletons found | assets.md notes "No skeletons found" |
| Can't identify player | List candidates, note "Could not auto-detect player character" |
| Plugin not responding | **Stop entirely.** Tell the user: "Plugin not responding — make sure the editor is running with UELLMToolkit loaded." |

---

## Checklist

Before writing the files, verify:

- [ ] You have the project name, directory, and engine version
- [ ] You identified the player character (or noted ambiguity)
- [ ] You inspected the player BP components and variables (if found)
- [ ] You inspected the player AnimBP state machines (if found)
- [ ] Each output file is under ~2000 tokens
- [ ] Large inventories are summarized, not listed line-by-line
- [ ] The `domains/` directory exists (create if needed)

After writing, tell the user:
```
Domain files written to domains/. Run `prep code` (or blueprints, assets, debug) to load them.
Re-run `prep one time init` after major project changes to refresh.
```
