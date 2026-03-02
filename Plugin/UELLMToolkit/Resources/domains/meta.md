# Meta Domain — Domain System Guide

## Module Table

| Module | Scope | When to Load |
|--------|-------|-------------|
| code | C++ gameplay patterns, CMC, build strategy | Writing C++ gameplay code |
| blueprints | Blueprint graphs, AnimBP, state machines, blend spaces | Editing Blueprints/AnimBPs via plugin |
| assets | FBX import, retargeting, visual inspection | Asset pipeline work |
| debug | PIE automation, input injection, output log | Testing, debugging, sequence building |
| tooling | Plugin architecture, extension, known gotchas | Extending the plugin |
| meta | This file — system overview | First-time orientation |

## Loading Instructions

### Claude Code (Read tool)
```
Read <plugin_path>/Resources/domains/code.md
```

### MCP Bridge (unreal_get_ue_context)
The context loader resolves domain files alongside API contexts based on keyword matching. Query with domain-relevant keywords and matching domains are included automatically.

## Relationship to contexts/

```
Resources/
  mcp-bridge/contexts/   <-- API Reference (class hierarchies, method signatures)
  domains/               <-- Operational Knowledge (workflows, gotchas, patterns)
```

- **contexts/** answers "what API exists?" — UE 5.7 classes, methods, property specifiers
- **domains/** answers "how do I use it?" — proven workflows, failure modes, workarounds

Load both when needed. They don't overlap.

## Project-Specific Domains

Run `prep one time init` to generate project domain files. The LLM reads the full procedure from `Plugins/UELLMToolkit/Resources/prep-init-guide.md` and follows it — scanning your project with plugin tools and writing 4 concise domain files to `domains/` in your project root:

- **code.md** — class hierarchy, module structure, export macro, key headers
- **blueprints.md** — player AnimBP deep-dive, BP inventory, blend spaces
- **assets.md** — skeleton family, content structure, key paths
- **debug.md** — log path, input actions for PIE injection, current map

Re-run after major project changes to refresh. Add your own domain files (e.g., `combat.md`, `networking.md`) to the same directory — they're discovered automatically.

### Two-Layer Loading

When loading a domain (e.g., `domain_code`), both layers are concatenated:
1. **Plugin domain** (general UE 5.7 patterns) — `Resources/domains/code.md`
2. **Project domain** (your specific project) — `<project>/domains/code.md`

Project-only domains (no plugin counterpart) are discovered dynamically.

## Hard Rules

These apply across all domains:

1. **`.uasset` files are BINARY** — never read them directly. Use the plugin tools.
2. **The plugin is the sole interface to the editor** — no Python remote execution, no direct engine scripting.
3. **If the plugin can't do something, stop** — don't workaround. Extend the plugin to cover the gap.
4. **Never speculate about values** — query actual data via the plugin, then state facts.
5. **Always `save_all` after write operations** — asset modifications must be persisted.
6. **Always run `help <tool>` before using an unfamiliar tool** — parameter names and requirements change.
