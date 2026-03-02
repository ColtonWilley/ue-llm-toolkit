# Assets Domain — Import, Retarget, Inspect

## Hard Rules

1. **`.uasset` files are BINARY** — never `Read` them directly. Use the plugin tools.
2. **UE 5.7 uses Interchange, not legacy FBX** — imported assets have `UInterchangeAssetImportData`, not `UFbxAssetImportData`. Transform overrides live on `UInterchangeGenericAssetsPipeline`.
3. **Root motion is NOT auto-enabled** — FBX import doesn't set the flag. Enable explicitly via `retarget set_root_motion` after import.
4. **`SetFrameRate()` crashes in UE 5.7** — use FBX reimport with `custom_sample_rate` instead. See Frame Rate section.

## FBX Import

### Direct Import (asset_import)
```bash
ue-tool.sh call asset_import '{"operation":"import","file_path":"D:/path/to/mesh.fbx","destination":"/Game/Meshes/","mesh_type":"skeletal","skeleton_path":"/Game/Path/To/Skeleton","import_materials":false}'
```

Key params:
- `mesh_type`: `"skeletal"` or `"static"`
- `skeleton_path`: required for skeletal mesh/animation import (must match existing skeleton)
- `import_materials: false`: prevents stub material orphans when you'll wire materials manually

### Animation Import (retarget import_fbx)
```bash
ue-tool.sh call retarget '{"operation":"import_fbx","fbx_path":"D:/path/to/anim.fbx","destination":"/Game/Animations/","skeleton_path":"/Game/Path/To/Skeleton","custom_sample_rate":60}'
```

`custom_sample_rate`: 30 = UE standard (~4.5x key reduction), 60 = high quality, 0 or omitted = native rate.

### Reimport Transform Overrides
```bash
ue-tool.sh call asset_import '{"operation":"reimport","asset_path":"/Game/...","import_rotation":{"roll":0,"pitch":0,"yaw":-90}}'
```
Axis mapping: `roll` = X, `pitch` = Y, `yaw` = Z (UE FRotator convention). **Runtime-only** — does NOT modify ref bone pose data.

## Skeleton & Retargeting

### IK Rig Setup
Both source and target IK Rigs need matching chain names for auto-mapping. Essential chains:
- **Root** — required for root motion rotation transfer
- **Pelvis** — name it "Pelvis", not "Root" (Root Motion op gets confused otherwise)
- **Spine**, **Neck**, **Head**
- **LeftArm**, **RightArm** (include clavicle chains separately)
- **LeftLeg**, **RightLeg**
- **Finger chains** — required for grip poses (without them, weapon grips go floppy)

### Retargeting Pitfalls
- Root motion not auto-enabled on retargeted output — enable explicitly
- `add_default_ops()` creates duplicates (6 real + 5 `_0` dupes) — remove `_0` immediately
- After reassigning IK Rigs, must re-run `assign_ik_rig_to_all_ops` and `auto_map_chains`
- FK Pelvis `TranslationMode` should be `GLOBALLY_SCALED` — `NONE` freezes pelvis at bind height
- `import_text()` replaces ALL fields — must include `ChainMapping` or mappings get wiped

## Blender FBX Roundtrip

### The Problem
Blender adds +90 X rotation to armature object on FBX import (Y-up to Z-up conversion). On re-export, the +90 X bleeds into the FBX root bone — character lies flat in UE.

### The Solution
Bake armature object rotation into edit bone matrices (`ebone.matrix = arm_rot @ ebone.matrix`), then zero object rotation. Must use `matrix` (not `head/tail`) to transform the full bone coordinate frame including roll.

### Failure Modes
| Symptom | Cause |
|---------|-------|
| Character on back | No armature bake |
| Character invisible | Used `transform_apply(rotation=True)` — bakes into bind pose |
| Character sideways | Wrong rotation axis |

## Frame Rate

`SetFrameRate()` triggers `SetTickResolutionDirectly()` which crashes on non-multiple rate changes. Correct approach: FBX reimport with `custom_sample_rate`:
```bash
ue-tool.sh call retarget '{"operation":"import_fbx","custom_sample_rate":60,...}'
```
`anim_edit resample` only changes key DATA, not frame rate metadata.

## Visual Inspection

### Capture Viewport
```bash
# Static mesh / skeletal mesh — straight-on front view
ue-tool.sh call capture_viewport '{"asset_path":"/Game/Meshes/SM_MyMesh"}'

# Animation — specific frame with camera preset
ue-tool.sh call capture_viewport '{"asset_path":"/Game/Anims/MyAnim","frame":15,"camera":"from_left"}'
```

- Auto-opens asset editor if not already open
- 3 camera presets: `front` (default), `from_left`, `from_right`
- `camera_location`/`camera_rotation` override presets for custom angles
- `frame` scrubs animation before capture, returns frame metadata
- Response auto-saves image to disk (base64 stripped from output, replaced with file path)

### Inspection Rules
- Never use camera presets for static mesh — always straight-on front view
- If character appears as side profile, the CHARACTER is rotated wrong, not the camera
- Use `Read` on the saved .jpg path to view the image

## Asset Operations

| Operation | Notes |
|-----------|-------|
| `asset save_all` | Silent save, no dialog. Always call after write operations. |
| `asset delete` | `force: true` ignores references. No confirmation dialog. |
| `asset duplicate` | Works for most assets. **BROKEN for AnimBPs** — stale class refs. |
| `asset rename` | Creates redirector. |
| `asset_import export` | T3D text format — useful for reading protected properties. |
| `asset set_preview_mesh` | Preview mesh must use compatible skeleton or nothing renders. |
