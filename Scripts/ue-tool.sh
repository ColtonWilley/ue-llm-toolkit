#!/usr/bin/env bash
# ue-tool.sh — Token-efficient wrapper for UE LLM Toolkit plugin tools
# Usage:
#   ue-tool.sh list                     — list all tool names
#   ue-tool.sh help <tool>              — show tool params (from live plugin)
#   ue-tool.sh call <tool> '<json>'     — call a tool (compact output)
#   ue-tool.sh call --raw <tool> '<json>' — call a tool (raw JSON output)
#   ue-tool.sh generate-ref             — regenerate tool reference markdown
#   ue-tool.sh save                     — save all assets via plugin
#   ue-tool.sh close                    — save + graceful editor shutdown
#   ue-tool.sh launch                   — start editor, optionally wait for plugin
#   ue-tool.sh restart                  — close + launch
#   ue-tool.sh status                   — report editor/VS/plugin state

set -euo pipefail

PORT=3000
if [[ "${1:-}" == "--port" ]]; then
    PORT="${2:?'--port requires a port number'}"
    shift 2
fi

BASE_URL="http://localhost:$PORT"
TOOLS_ENDPOINT="$BASE_URL/mcp/tools"
STATUS_ENDPOINT="$BASE_URL/mcp/status"
TOOL_ENDPOINT="$BASE_URL/mcp/tool"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
GRACEFUL_TIMEOUT=30
PLUGIN_WAIT_TIMEOUT=120

# --- Editor/project detection ---

# Auto-detect .uproject in the current working directory or parent
find_uproject() {
    local dir="$PWD"
    while [[ "$dir" != "/" && "$dir" != "" ]]; do
        local found
        found=$(ls "$dir"/*.uproject 2>/dev/null | head -1)
        if [[ -n "$found" ]]; then
            echo "$found"
            return 0
        fi
        dir="$(dirname "$dir")"
    done
    return 1
}

# Auto-detect UE editor path
find_editor() {
    # Check env var first
    if [[ -n "${UE_EDITOR:-}" && -f "$UE_EDITOR" ]]; then
        echo "$UE_EDITOR"
        return 0
    fi
    # Check default UE 5.7 install location
    local p="C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe"
    if [[ -f "$p" ]]; then
        echo "$p"
        return 0
    fi
    return 1
}

is_running() {
    tasklist //FI "IMAGENAME eq $1" //NH 2>/dev/null | grep -qi "$1"
}

editor_running() { is_running "UnrealEditor.exe"; }
vs_running()     { is_running "devenv.exe"; }

plugin_responding() {
    curl -s --max-time 3 "$STATUS_ENDPOINT" > /dev/null 2>&1
}

check_connectivity() {
    if ! curl -s --max-time 3 "$STATUS_ENDPOINT" > /dev/null 2>&1; then
        echo "[ERROR] Plugin not responding — make sure the editor is running." >&2
        exit 1
    fi
}

check_python() {
    # py (Windows Python Launcher) is most reliable — never triggers Store stub
    if command -v py &> /dev/null 2>&1 && py --version &> /dev/null 2>&1; then
        echo "py"
    elif command -v python3 &> /dev/null 2>&1; then
        echo "python3"
    elif command -v python &> /dev/null 2>&1; then
        echo "python"
    else
        # Windows: search common install locations when nothing is in PATH
        for p in \
            "/c/Users/$USER/AppData/Local/Programs/Python/Python3"*/python.exe \
            "/c/Python3"*/python.exe \
            "/c/Program Files/Python3"*/python.exe; do
            if [[ -f "$p" ]] 2>/dev/null; then
                echo "$p"
                return
            fi
        done
        echo ""
    fi
}

cmd_list() {
    local py
    py=$(check_python)

    if [[ -n "$py" ]]; then
        curl -s --max-time 10 "$TOOLS_ENDPOINT" | "$py" -c "
import json, sys
data = json.load(sys.stdin)
tools = data if isinstance(data, list) else data.get('tools', [])
for t in sorted(tools, key=lambda x: x.get('name','')):
    print(t.get('name',''))
"
    else
        # Fallback: raw curl (user can parse)
        echo "[WARN] Python not available — showing raw JSON" >&2
        curl -s --max-time 10 "$TOOLS_ENDPOINT"
    fi
}

cmd_help() {
    local tool_name="$1"
    local py
    py=$(check_python)

    if [[ -z "$py" ]]; then
        echo "[ERROR] Python required for 'help' command." >&2
        exit 1
    fi

    curl -s --max-time 10 "$TOOLS_ENDPOINT" | "$py" -c "
import json, sys

tool_name = '$tool_name'
data = json.load(sys.stdin)
tools = data if isinstance(data, list) else data.get('tools', [])

match = None
for t in tools:
    if t.get('name') == tool_name:
        match = t
        break

if not match:
    print(f'[ERROR] Tool \"{tool_name}\" not found.', file=sys.stderr)
    print('Available tools:', file=sys.stderr)
    for t in sorted(tools, key=lambda x: x.get('name','')):
        print(f'  {t.get(\"name\",\"\")}', file=sys.stderr)
    sys.exit(1)

desc = match.get('description', 'No description')
# Show first line as title, rest as body
desc_lines = desc.split('\\n')
print(f'{tool_name} — {desc_lines[0]}')
if len(desc_lines) > 1:
    print('\\n'.join(desc_lines[1:]))
print()

# Support both formats:
# 1. Plugin format: 'parameters' array of {name, type, required, description, default}
# 2. MCP format: 'inputSchema.properties' object
params_array = match.get('parameters', [])
schema = match.get('inputSchema', match.get('input_schema', {}))
props = schema.get('properties', {})
schema_required = set(schema.get('required', []))

# Normalize to list of dicts
params = []
if params_array and isinstance(params_array, list):
    params = params_array
elif props:
    for k, v in props.items():
        p = dict(v)
        p['name'] = k
        p['required'] = k in schema_required
        params.append(p)

if not params:
    print('No parameters.')
    sys.exit(0)

req_params = [p for p in params if p.get('required')]
opt_params = [p for p in params if not p.get('required')]

if req_params:
    print('Required:')
    for p in sorted(req_params, key=lambda x: x.get('name','')):
        pname = p.get('name', '?')
        ptype = p.get('type', '?')
        pdesc = p.get('description', '')
        enum = p.get('enum')
        if enum:
            pdesc += f\" [{', '.join(str(e) for e in enum)}]\"
        print(f'  {pname} ({ptype}) — {pdesc}')
    print()

if opt_params:
    print('Optional:')
    for p in sorted(opt_params, key=lambda x: x.get('name','')):
        pname = p.get('name', '?')
        ptype = p.get('type', '?')
        pdesc = p.get('description', '')
        default = p.get('default')
        enum = p.get('enum')
        if enum:
            pdesc += f\" [{', '.join(str(e) for e in enum)}]\"
        if default is not None:
            pdesc += f' [default: {default}]'
        print(f'  {pname} ({ptype}) — {pdesc}')
"
}

cmd_call() {
    local raw_mode=false
    local save_path=""
    while [[ "${1:-}" == --* ]]; do
        case "$1" in
            --raw) raw_mode=true; shift ;;
            --save) save_path="$2"; shift 2 ;;
            *) break ;;
        esac
    done
    local tool_name="$1"
    local json_body="${2:-'{}'}"

    check_connectivity

    local response
    local http_code
    response=$(curl -s --max-time 15 -w '\n%{http_code}' -X POST "$TOOL_ENDPOINT/$tool_name" \
        -H "Content-Type: application/json" \
        -d "$json_body")

    http_code=$(echo "$response" | tail -1)
    local body
    body=$(echo "$response" | sed '$d')

    # Compact output by default, --raw for full JSON
    if [[ "$raw_mode" == false ]]; then
        local py
        py=$(check_python)
        if [[ -n "$py" ]]; then
            echo "$body" | UE_TOOL_SAVE_IMAGE="$save_path" "$py" "$SCRIPT_DIR/json-compact.py"
        else
            echo "$body"
        fi
    else
        echo "$body"
    fi

    # Check for tool-level failure
    if echo "$body" | grep -q '"success"\s*:\s*false'; then
        exit 1
    fi

    # Check for HTTP errors
    if [[ "$http_code" -ge 400 ]]; then
        exit 1
    fi
}

cmd_save() {
    check_connectivity
    echo "[SAVE] Saving all assets..."
    local resp
    resp=$(curl -s --max-time 30 -X POST "$TOOL_ENDPOINT/asset" \
        -H "Content-Type: application/json" \
        -d '{"operation":"save_all"}' 2>&1)
    if echo "$resp" | grep -q '"success"\s*:\s*false'; then
        echo "[ERROR] Save failed: $resp" >&2
        return 1
    fi
    echo "[SAVE] Done"
}

cmd_close() {
    if ! editor_running; then
        echo "[CLOSE] Editor not running"
        return 0
    fi

    if plugin_responding; then
        cmd_save || {
            echo "[WARN] Save failed — aborting to protect unsaved data" >&2
            echo "[WARN] Save manually in the editor, then retry" >&2
            return 1
        }
        sleep 2
    else
        echo "[WARN] Plugin not responding — closing without save"
    fi

    echo "[CLOSE] Requesting graceful editor shutdown..."
    taskkill //IM UnrealEditor.exe > /dev/null 2>&1 || true

    local waited=0
    while editor_running && [ $waited -lt $GRACEFUL_TIMEOUT ]; do
        sleep 2
        waited=$((waited + 2))
    done

    if editor_running; then
        echo "[WARN] Editor didn't exit gracefully after ${GRACEFUL_TIMEOUT}s — force killing"
        taskkill //F //IM UnrealEditor.exe > /dev/null 2>&1 || true
        sleep 3
    fi

    if editor_running; then
        echo "[ERROR] Failed to close editor" >&2
        return 1
    fi
    echo "[CLOSE] Editor stopped"
}

cmd_launch() {
    if editor_running; then
        echo "[LAUNCH] Editor already running"
        if plugin_responding; then
            echo "[LAUNCH] Plugin responding"
        else
            echo "[LAUNCH] Waiting for plugin..."
            local waited=0
            while ! plugin_responding && [ $waited -lt $PLUGIN_WAIT_TIMEOUT ]; do
                sleep 3
                waited=$((waited + 3))
            done
            if plugin_responding; then
                echo "[LAUNCH] Plugin ready"
            else
                echo "[WARN] Plugin not responding after ${PLUGIN_WAIT_TIMEOUT}s"
            fi
        fi
        return 0
    fi

    # Find editor and project
    local editor uproject
    editor=$(find_editor) || {
        echo "[ERROR] Could not find UnrealEditor.exe. Set UE_EDITOR env var." >&2
        exit 1
    }
    uproject=$(find_uproject) || {
        echo "[ERROR] No .uproject found in current directory tree. cd to your project root." >&2
        exit 1
    }

    echo "[LAUNCH] Starting editor..."
    echo "[LAUNCH] Project: $uproject"
    "$editor" "$uproject" &>/dev/null &
    disown

    echo "[LAUNCH] Waiting for plugin connectivity..."
    local waited=0
    while ! plugin_responding && [ $waited -lt $PLUGIN_WAIT_TIMEOUT ]; do
        sleep 3
        waited=$((waited + 3))
    done

    if plugin_responding; then
        echo "[LAUNCH] Editor running, plugin ready"
    else
        echo "[LAUNCH] Editor started but plugin not responding after ${PLUGIN_WAIT_TIMEOUT}s"
        echo "[WARN] Editor may still be loading — try again shortly"
    fi
}

cmd_restart() {
    cmd_close || return 1
    cmd_launch
}

cmd_status() {
    local ed="not running" vs="not running" plug="not responding"
    editor_running && ed="running"
    vs_running && vs="running"
    plugin_responding && plug="responding"
    echo "[STATUS] Editor: $ed | VS: $vs | Plugin: $plug"
}

cmd_generate_ref() {
    local py
    py=$(check_python)

    if [[ -z "$py" ]]; then
        echo "[ERROR] Python required for 'generate-ref' command." >&2
        exit 1
    fi

    check_connectivity

    local output_file="${2:-ue-tool-reference.md}"

    curl -s --max-time 10 "$TOOLS_ENDPOINT" | "$py" -c "
import json, sys
from datetime import datetime

data = json.load(sys.stdin)
tools = data if isinstance(data, list) else data.get('tools', [])
tools = sorted(tools, key=lambda x: x.get('name',''))

lines = []
lines.append('# UE LLM Toolkit — Tool Reference')
lines.append('')
lines.append(f'Auto-generated by \`ue-tool.sh generate-ref\` on {datetime.now().strftime(\"%Y-%m-%d %H:%M\")}.')
lines.append(f'Source: \`GET /mcp/tools\` ({len(tools)} tools)')
lines.append('')

# Quick-reference table
lines.append('## Quick Reference')
lines.append('')
lines.append('| Tool | Purpose |')
lines.append('|------|---------|')
for t in tools:
    name = t.get('name', '')
    desc = t.get('description', '').split('.')[0].split('\\n')[0][:80]
    lines.append(f'| \`{name}\` | {desc} |')
lines.append('')

# Detailed params
lines.append('## Tool Parameters')
lines.append('')
for t in tools:
    name = t.get('name', '')
    desc = t.get('description', 'No description')
    lines.append(f'### \`{name}\`')
    lines.append(f'{desc}')
    lines.append('')

    # Support both formats: plugin array or MCP inputSchema
    params_array = t.get('parameters', [])
    schema = t.get('inputSchema', t.get('input_schema', {}))
    props = schema.get('properties', {})
    schema_required = set(schema.get('required', []))

    params = []
    if params_array and isinstance(params_array, list):
        params = params_array
    elif props:
        for k, v in props.items():
            p = dict(v)
            p['name'] = k
            p['required'] = k in schema_required
            params.append(p)

    if not params:
        lines.append('No parameters.')
        lines.append('')
        continue

    req_params = [p for p in params if p.get('required')]
    opt_params = [p for p in params if not p.get('required')]

    if req_params:
        lines.append('**Required:**')
        for p in sorted(req_params, key=lambda x: x.get('name','')):
            pname = p.get('name', '?')
            ptype = p.get('type', '?')
            pdesc = p.get('description', '')
            enum = p.get('enum')
            if enum:
                pdesc += f\" [{', '.join(str(e) for e in enum)}]\"
            lines.append(f'- \`{pname}\` ({ptype}) — {pdesc}')
        lines.append('')

    if opt_params:
        lines.append('**Optional:**')
        for p in sorted(opt_params, key=lambda x: x.get('name','')):
            pname = p.get('name', '?')
            ptype = p.get('type', '?')
            pdesc = p.get('description', '')
            default = p.get('default')
            enum = p.get('enum')
            if enum:
                pdesc += f\" [{', '.join(str(e) for e in enum)}]\"
            if default is not None:
                pdesc += f' [default: {default}]'
            lines.append(f'- \`{pname}\` ({ptype}) — {pdesc}')
        lines.append('')

print('\\n'.join(lines))
" > "$output_file"

    echo "Generated: $output_file ($(wc -l < "$output_file") lines)"
}

# --- Main dispatch ---

case "${1:-}" in
    list)
        check_connectivity
        cmd_list
        ;;
    help)
        if [[ -z "${2:-}" ]]; then
            echo "Usage: ue-tool.sh help <tool_name>" >&2
            exit 1
        fi
        check_connectivity
        cmd_help "$2"
        ;;
    call)
        # Support: call [--raw] [--save path] <tool> '<json>'
        shift
        call_flags=()
        while [[ "${1:-}" == --* ]]; do
            case "$1" in
                --raw) call_flags+=(--raw); shift ;;
                --save) call_flags+=(--save "$2"); shift 2 ;;
                *) echo "Unknown flag: $1" >&2; exit 1 ;;
            esac
        done
        if [[ -z "${1:-}" ]]; then
            echo "Usage: ue-tool.sh call [--raw] [--save path] <tool_name> '<json>'" >&2
            exit 1
        fi
        cmd_call "${call_flags[@]+"${call_flags[@]}"}" "$1" "${2:-'{}'}"
        ;;
    generate-ref)
        cmd_generate_ref
        ;;
    save)
        cmd_save
        ;;
    close)
        cmd_close
        ;;
    launch)
        cmd_launch
        ;;
    restart)
        cmd_restart
        ;;
    status)
        cmd_status
        ;;
    *)
        echo "Usage: ue-tool.sh [--port PORT] {list|help|call|generate-ref|save|close|launch|restart|status}" >&2
        exit 1
        ;;
esac
