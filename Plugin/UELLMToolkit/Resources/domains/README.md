# UE LLM Toolkit — Domain Knowledge System

Operational knowledge for working effectively with UE 5.7 projects through the plugin.

## Module Table

| Module | Scope |
|--------|-------|
| code | C++ gameplay — headers, components, CMC, build strategy |
| blueprints | Blueprint graphs, AnimBP, state machines, blend spaces |
| assets | FBX import, retargeting, skeleton, visual inspection |
| debug | PIE automation, input injection, output log, diagnostics |
| tooling | Plugin architecture, extension patterns, known gotchas |
| meta | Domain system guide, loading instructions |

## When to Load

Load domains based on the task at hand — not all at once.

| Task Type | Domains |
|-----------|---------|
| C++ gameplay code | code |
| Blueprint/AnimBP editing | blueprints |
| Asset import, retarget, inspect | assets |
| PIE testing, input sequences | debug |
| Plugin extension, new tools | tooling |
| First time setup | meta |

## Relationship to `contexts/`

- **`contexts/`** = UE 5.7 API reference (class hierarchies, method signatures, property specifiers)
- **`domains/`** = Operational knowledge (how to accomplish tasks, what to avoid, proven patterns)

They complement each other. Load API context when you need class/method details; load domains when you need workflow guidance.

## Loading Domains

**Claude Code**: `Read` the domain file directly:
```
Read domains/code.md
```

**MCP bridge**: Use `unreal_get_ue_context` with domain keywords — the context loader resolves domain files alongside API contexts.

## Project Domains

The domain system supports two layers:

```
Plugin domains (general UE 5.7)        Project domains (your project)
Resources/domains/code.md         +    MyProject/domains/code.md
Resources/domains/blueprints.md   +    MyProject/domains/blueprints.md
Resources/domains/assets.md       +    MyProject/domains/assets.md
                                       MyProject/domains/combat.md  ← user-created
```

When loading a domain category, both files are concatenated (plugin general first, then project-specific). User-created domains with no plugin counterpart are discovered dynamically.

### Generating Project Domains

Tell your AI:
```
prep one time init
```

The LLM reads the scan procedure from `Plugins/UELLMToolkit/Resources/prep-init-guide.md` and follows it — exploring your project with plugin tools and writing 4 domain files (code, blueprints, assets, debug) to `domains/` in your project root. Requires the editor running with the plugin loaded. Re-run after major project changes. See `domains/meta.md` for details.
