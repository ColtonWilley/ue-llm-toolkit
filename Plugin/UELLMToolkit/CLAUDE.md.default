# UE LLM Toolkit - Claude Code Instructions for Unreal Engine 5.7

This file provides guidance to Claude Code when working with the UE LLM Toolkit plugin and Unreal Engine 5.7 projects.

## Setup

Copy this file to `CLAUDE.md` in the same directory and customize the build paths for your system.

## Project Overview

**UE LLM Toolkit** is an Unreal Engine 5.7 plugin that provides an HTTP REST API for LLM-driven editor automation — 38 tools, 201 operations for Blueprint editing, animation, materials, level management, and more.

### Key Directories
- `Source/UELLMToolkit/Private/MCP/` - HTTP server and tool implementations
- `Source/UELLMToolkit/Private/MCP/Tools/` - Individual tool classes
- `Source/UELLMToolkit/Private/Tests/` - Automation tests
- `Resources/mcp-bridge/` - Node.js MCP bridge

### Build Commands
```bash
# Build plugin - customize these paths for your system:
# - UE_PATH: Your Unreal Engine installation
# - PLUGIN_PATH: Path to UELLMToolkit.uplugin
# - OUTPUT_PATH: Temporary build output directory

"<UE_PATH>/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin -Plugin="<PLUGIN_PATH>/UELLMToolkit.uplugin" -Package="<OUTPUT_PATH>" -TargetPlatforms=Win64 -Rocket

# Run tests (in Unreal Editor console)
Automation RunTests UELLMToolkit
```

---

## Unreal Engine 5.7 C++ Standards

### IMPORTANT: Stay Focused on UE 5.7
- This project targets **Unreal Engine 5.7** exclusively
- Use UE 5.7 API patterns and conventions
- Do NOT suggest deprecated APIs or patterns from older engine versions

### File Organization
- **Maximum 500 lines per file** - Split large files into logical units
- **Maximum 50 lines per function** - Extract helper functions for clarity
- **Private/** folder for implementation files
- **Public/** folder for headers meant for external use

### UPROPERTY Specifiers (UE 5.7)
```cpp
// Editor-visible and Blueprint-accessible
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
int32 EditableProperty;

// Asset references (UE 5.x preferred)
UPROPERTY(EditAnywhere)
TObjectPtr<UTexture2D> Texture;  // Hard reference

UPROPERTY(EditAnywhere)
TSoftObjectPtr<UStaticMesh> MeshAsset;  // Soft reference
```

### UFUNCTION Specifiers (UE 5.7)
```cpp
// Blueprint callable (has execution pins)
UFUNCTION(BlueprintCallable, Category = "MyCategory")
void DoSomething();

// Blueprint pure (no execution pins, no side effects)
UFUNCTION(BlueprintPure, Category = "MyCategory")
int32 GetValue() const;
```

---

## Tool Development Guidelines

### Creating New Tools
1. Create header in `Private/MCP/Tools/MCPTool_YourTool.h`
2. Inherit from `FMCPToolBase`
3. Implement `GetInfo()` with proper parameters and annotations
4. Implement `Execute()` with parameter validation
5. Register in `MCPToolRegistry.cpp`
6. Add tests in `Private/Tests/`

### Tool Parameter Validation Pattern
```cpp
FMCPToolResult FMCPTool_YourTool::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Extract and validate required parameters
    FString RequiredParam;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("param_name"), RequiredParam, Error))
    {
        return Error.GetValue();
    }

    // Do work...
    return FMCPToolResult::Success(TEXT("Operation completed"));
}
```

### Tool Annotations
```cpp
// Read-only tool (safe, no modifications)
Info.Annotations = FMCPToolAnnotations::ReadOnly();

// Modifying tool (changes state, reversible)
Info.Annotations = FMCPToolAnnotations::Modifying();

// Destructive tool (cannot be undone via MCP)
Info.Annotations = FMCPToolAnnotations::Destructive();
```

---

## Dynamic UE 5.7 Context System

The MCP bridge includes dynamic context files that provide accurate UE 5.7 API documentation.

### Context Files Location
```
Resources/mcp-bridge/contexts/
├── animation.md   # Animation Blueprint, state machines, UAnimInstance
├── blueprint.md   # Blueprint graphs, UK2Node, FBlueprintEditorUtils
├── slate.md       # Slate UI widgets, SNew/SAssignNew patterns
├── actor.md       # Actor spawning, components, transforms
├── assets.md      # Asset loading, TSoftObjectPtr, async loading
└── replication.md # Network replication, RPCs, DOREPLIFETIME
```

---

## Domain Knowledge System

Operational knowledge files covering proven workflows, common pitfalls, and battle-tested patterns for working with UE 5.7 through the plugin. Complements the API reference in `contexts/`.

### Domain Files
```
Resources/domains/
├── README.md       # Module table + loading guide
├── code.md         # C++ gameplay: TObjectPtr, CMC, build strategy, UE 5.7 pitfalls
├── blueprints.md   # Node IDs, targeted queries, AnimBP creation, state-bound graphs
├── assets.md       # FBX import, retargeting, Blender roundtrip, visual inspection
├── debug.md        # PIE automation, input injection, output log, sequence building
├── tooling.md      # Plugin architecture, extension patterns, capability map
└── meta.md         # Domain system overview, loading instructions
```

### Loading Domains
Load the relevant domain file before starting a task. For Claude Code, use `Read` on the file path. For MCP, query `unreal_get_ue_context` with domain-relevant keywords.

See `Resources/domains/meta.md` for full loading instructions and the task-to-domain mapping.

### `prep one time init`

When the user says `prep one time init`, read the full procedure at:

```
Plugins/UELLMToolkit/Resources/prep-init-guide.md
```

Follow it step by step. The guide covers: discovery scans, player character detection, deep inspection heuristics, output templates for 4 domain files (code.md, blueprints.md, assets.md, debug.md), scaling rules for large projects, and error handling. Do not improvise — the guide is self-contained.

### `prep <domain>`

When the user says `prep <domain>` (e.g., `prep code`, `prep blueprints + debug`), load the matching domain files:

1. **Plugin domain** (general UE 5.7 patterns): `Plugins/UELLMToolkit/Resources/domains/<domain>.md`
2. **Project domain** (project-specific): `domains/<domain>.md`

Read both if they exist (plugin first, then project). For combined preps like `prep X + Y`, load all requested domains.

---

## Testing Standards

### Automation Test Pattern
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMyTest,
    "UELLMToolkit.Category.TestName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMyTest::RunTest(const FString& Parameters)
{
    TestTrue("Description of what should be true", bCondition);
    TestEqual("Values should match", ActualValue, ExpectedValue);
    return true;
}
```

---

## Security Considerations

### Parameter Validation (ALWAYS DO THIS)
1. **Path Validation**: Block `/Engine/`, `/Script/`, path traversal (`../`)
2. **Actor Name Validation**: Block special characters `<>|&;$(){}[]!*?~`
3. **Console Command Validation**: Block dangerous commands (quit, crash, shutdown)
4. **Numeric Validation**: Check for NaN, Infinity, reasonable bounds

---

## Commit Standards

When committing changes to this project:
- `feat:` New tools or features
- `fix:` Bug fixes
- `test:` Test additions or fixes
- `docs:` Documentation updates
- `refactor:` Code refactoring without behavior changes
