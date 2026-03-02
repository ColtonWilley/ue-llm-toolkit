# Tooling Domain — Plugin Architecture & Extension

## Architecture

The plugin follows a three-layer pattern:

1. **Utility classes** — domain logic, no JSON. Pure UE C++ operating on UObjects.
2. **Tool classes** — inherit `FMCPToolBase`, handle JSON dispatch, parameter validation, call utilities.
3. **Registry** — `MCPToolRegistry.cpp` maps tool names to instances.

### Tool Implementation Pattern
```cpp
// In GetInfo():
Info.Name = TEXT("my_tool");
Info.Description = TEXT("What it does");
Info.Annotations = FMCPToolAnnotations::Modifying();  // or ReadOnly(), Destructive()
FMCPToolParameter::Required(TEXT("param"), TEXT("description"));

// In Execute():
FString RequiredParam;
TOptional<FMCPToolResult> Error;
if (!ExtractRequiredString(Params, TEXT("param"), RequiredParam, Error))
    return Error.GetValue();
// Do work...
return FMCPToolResult::Success(TEXT("Result"));
```

## Tool Capability Map

| Category | Tools | Read/Write |
|----------|-------|------------|
| Blueprint | `blueprint_query`, `blueprint_modify` | Full R/W |
| AnimBP | `anim_blueprint_modify` | Full R/W |
| Montage | `montage_modify` | Full R/W |
| Control Rig | `control_rig` | Inspect + 16 write ops |
| IK/Retarget | `retarget` | Inspect + create/configure/batch |
| Blend Space | `blend_space` | Inspect/list + 8 write ops |
| Anim Tracks | `anim_edit` | Inspect + adjust/resample/replace_skeleton |
| Assets | `asset` | Full CRUD + save_all + hashes |
| Import/Export | `asset_import` | Import/batch/export/reimport |
| Level | `level_query`, `spawn_actor`, `move_actor`, `delete_actors`, `set_property` | Full R/W |
| Widget | `widget_editor` | Inspect + full editing |
| Enhanced Input | `enhanced_input` | Full R/W |
| Materials | `material` | Write only |
| PIE/Debug | `gameplay_debug` | Full automation |
| Output Log | `get_output_log` | Structured read |
| Viewport | `capture_viewport` | Capture + camera control |

## Known UE 5.7 Gotchas

These are engine-level issues, not plugin bugs:

| Issue | Impact | Workaround |
|-------|--------|------------|
| `SetFrameRate()` crash | Assertion on non-multiple rate changes | FBX reimport with `custom_sample_rate` |
| `SavePackage` returns `bool` | Not `FSavePackageResultStruct` like older versions | Check return value directly |
| Interchange pipeline | `UInterchangeAssetImportData` not `UFbxAssetImportData` | Plugin handles both automatically |
| AnimBP duplication broken | Stale generated class references | Build from scratch, never duplicate |
| `add_transition` ignores duration | Always creates with 0.2s default | Call `set_transition_duration` after |
| `setup_transition_conditions` broken | `type`/`comparison_type` ignored | Use `add_comparison_chain` instead |
| Win32 `TRUE`/`FALSE` macros | Undefined after UE headers | Use `1`/`0` literals |
| `get_asset_by_object_path` deprecated | 5.7 removal | Use `GetAssetsByPath()` + filter |

## Game Thread Dispatch

All plugin tool calls touching UObjects run on the game thread via `MCPTaskQueue`. The HTTP server receives requests on a background thread and queues lambdas for game thread execution.

Key implications:
- Long operations block the editor briefly (expected)
- PIE operations use `FCoreDelegates` for frame-synced injection
- Timeout handling prevents indefinite hangs

## T3D Export Technique

For reading protected asset properties not exposed by the plugin API:
```bash
ue-tool.sh call asset_import '{"operation":"export","asset_path":"/Game/...","output_path":"D:/tmp/export.t3d"}'
```

Key fields in T3D format:
- Montage: `Notifies(N)=`, `CompositeSections(N)=`, `SlotAnimTracks(N)=`
- AnimBP: `K2Node_Event`, `K2Node_CallFunction`, `LinkedTo=()`
- Use `.t3d` extension (not `.uasset`)

## Extension Pattern

### Adding a New Tool

1. Create `MCPTool_YourTool.h` in `Private/MCP/Tools/`
2. Inherit from `FMCPToolBase`
3. Implement `GetInfo()` with parameters and annotations
4. Implement `Execute()` with validation
5. Register in `MCPToolRegistry.cpp`
6. Add tests in `Private/Tests/`

### Adding a New Operation to an Existing Tool

1. Add operation string to the tool's dispatch switch
2. Implement handler method
3. Update `GetInfo()` to document the new operation
4. Add tests

### Parameter Validation Rules
- Path validation: block `/Engine/`, `/Script/`, path traversal (`../`)
- Actor name validation: block special characters
- Console command validation: block dangerous commands (quit, crash, shutdown)
- Numeric validation: check for NaN, Infinity, reasonable bounds

## Property System

### Type Handling
Object reference properties (`set_property`, `set_asset_property`, `set_component_default`):
- `FClassProperty` (TSubclassOf) — check before FObjectPropertyBase (inheritance)
- `FObjectPropertyBase` (TObjectPtr)
- `FSoftClassProperty`, `FSoftObjectProperty`
- `TMap` properties: pass as JSON object `{"key":"value"}`

### CDO vs Placed Actors
`set_cdo_default` only affects **future spawns**. Actors already placed in a level have property values serialized in the `.umap` — CDO changes are invisible to them. To update existing actors: also call `set_property` on each placed instance, or delete and re-place.

### Enum Values
- Fully qualified names work: `EMyEnum::ValueName`
- Numeric index also works: `1`
- Bare names without enum prefix silently fail for `TEnumAsByte` properties

## Save After Write Operations

Always call `save_all` after any operation that modifies assets:
```bash
ue-tool.sh call asset '{"operation":"save_all"}'
```
Never use `run_console_command SaveAll` — it triggers UE's native confirmation dialog.
