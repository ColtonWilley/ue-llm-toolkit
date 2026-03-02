#!/usr/bin/env bash
# build.sh — Smart Build Dispatcher for UE projects
# Auto-detects editor/VS state and picks the best build strategy.
#
# Usage: bash Scripts/build.sh [--auto|--live|--vs|--clean|--full]
#   --auto  (default) Detect state and escalate: live coding → VS → full rebuild
#   --live  Force live coding (editor must be running)
#   --vs    Force VS build
#   --clean Close editor + VS build
#   --full  Full rebuild: close editor, close VS, regenerate project files, build
#
# Stdout: minimal status lines for LLM consumption
# Full output: /tmp/ue-build.log

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG="/tmp/ue-build.log"
BUILD_CONFIG="Development Editor|Win64"

# ── Auto-detect Paths ─────────────────────────────────────────────────────────

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
  echo "[ERROR] No .uproject found. Run from your project directory." >&2
  return 1
}

find_sln() {
  local project_dir
  project_dir="$(dirname "$UPROJECT")"
  local found
  found=$(ls "$project_dir"/*.sln 2>/dev/null | head -1)
  if [[ -n "$found" ]]; then
    echo "$found"
    return 0
  fi
  echo "[ERROR] No .sln found next to $UPROJECT. Run --full to regenerate project files." >&2
  return 1
}

find_devenv() {
  # Check env var first
  if [[ -n "${VS_DEVENV:-}" && -f "$VS_DEVENV" ]]; then
    echo "$VS_DEVENV"
    return 0
  fi
  # Search common VS install locations
  for edition in Community Professional Enterprise; do
    for year in 2022 2024 2019; do
      local p="C:/Program Files/Microsoft Visual Studio/$year/$edition/Common7/IDE/devenv.com"
      if [[ -f "$p" ]]; then
        echo "$p"
        return 0
      fi
    done
  done
  echo "[ERROR] devenv.com not found. Set VS_DEVENV env var." >&2
  return 1
}

find_ubt() {
  # Check env var first
  if [[ -n "${UE_UBT:-}" && -f "$UE_UBT" ]]; then
    echo "$UE_UBT"
    return 0
  fi
  # Search common UE install location
  local p="C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe"
  if [[ -f "$p" ]]; then
    echo "$p"
    return 0
  fi
  echo "[ERROR] UnrealBuildTool not found. Set UE_UBT env var." >&2
  return 1
}

# Resolve paths once at startup
UPROJECT="$(find_uproject)" || exit 1
PROJECT_NAME="$(basename "$UPROJECT" .uproject)"
PROJECT_DIR="$(dirname "$UPROJECT")"

has_source_dir() {
  [[ -d "$PROJECT_DIR/Source" ]]
}

# ── Helpers ───────────────────────────────────────────────────────────────────

is_running() {
  tasklist //FI "IMAGENAME eq $1" //NH 2>/dev/null | grep -qi "$1"
}

editor_running() { is_running "UnrealEditor.exe"; }
vs_running()     { is_running "devenv.exe"; }

log_init() {
  echo "=== $PROJECT_NAME Build — $(date) ===" > "$LOG"
}

log_append() {
  echo "$1" >> "$LOG"
}

status() {
  echo "$1"
}

has_state_errors() {
  grep -qEi "LNK1120|LNK2019|cannot open file.*(\.obj|\.lib)|fatal error C1083|MSB3073.*exit code 6" "$LOG"
}

has_code_errors() {
  grep -qEi "error C[0-9]{4}|error LNK[0-9]{4}|fatal error" "$LOG"
}

build_summary() {
  local errors warnings
  errors=$(grep -Ei "error C[0-9]{4}|error LNK[0-9]{4}|fatal error" "$LOG" 2>/dev/null | wc -l | tr -d ' ')
  warnings=$(grep -Ei "warning C[0-9]{4}|warning LNK[0-9]{4}" "$LOG" 2>/dev/null | wc -l | tr -d ' ')
  status "[SUMMARY] ${errors} error(s), ${warnings} warning(s)"
  if [ "$errors" -gt 0 ]; then
    grep -Ei "error C[0-9]{4}|error LNK[0-9]{4}|fatal error" "$LOG" | head -20
  fi
}

close_editor() {
  if [[ -f "$SCRIPT_DIR/ue-tool.sh" ]]; then
    bash "$SCRIPT_DIR/ue-tool.sh" close
  else
    status "[WARN] ue-tool.sh not found — skipping graceful shutdown"
  fi
}

launch_editor() {
  if [[ -f "$SCRIPT_DIR/ue-tool.sh" ]]; then
    bash "$SCRIPT_DIR/ue-tool.sh" launch
  else
    status "[WARN] ue-tool.sh not found — skipping editor launch"
  fi
}

# ── Build Strategies ──────────────────────────────────────────────────────────

do_live_coding() {
  status "[STRATEGY] live_coding"
  log_append "--- Live Coding ---"

  local trigger_resp
  trigger_resp=$(bash "$SCRIPT_DIR/ue-tool.sh" call run_console_command '{"command":"LiveCoding.Compile"}' 2>&1)
  log_append "Trigger response: $trigger_resp"

  sleep 8

  local log_resp
  log_resp=$(bash "$SCRIPT_DIR/ue-tool.sh" call get_output_log '{"lines":40}' 2>&1)
  log_append "Output log: $log_resp"

  if echo "$log_resp" | grep -qi "Live coding succeeded"; then
    status "[RESULT] SUCCESS live_coding"
    return 0
  else
    status "[RESULT] FAIL live_coding"
    return 1
  fi
}

do_vs_build() {
  local devenv
  devenv="$(find_devenv)" || return 1
  local sln
  sln="$(find_sln)" || return 1

  status "[STRATEGY] vs_build"
  log_append "--- VS Build ---"

  status "[BUILDING] VS build in progress..."
  "$devenv" "$sln" //Build "$BUILD_CONFIG" >> "$LOG" 2>&1
  local rc=$?

  build_summary
  if [ $rc -eq 0 ]; then
    status "[RESULT] SUCCESS vs_build"
    return 0
  else
    log_append "VS build exited with code $rc"
    return 1
  fi
}

do_clean_vs_build() {
  status "[STRATEGY] clean_vs_build"
  log_append "--- Clean VS Build ---"

  if editor_running; then
    close_editor || {
      status "[RESULT] FAIL clean_vs_build (editor close failed)"
      return 1
    }
  fi

  do_vs_build
}

do_full_rebuild() {
  local devenv ubt sln
  devenv="$(find_devenv)" || return 1
  ubt="$(find_ubt)" || return 1

  status "[STRATEGY] full_rebuild"
  log_append "--- Full Rebuild ---"

  if editor_running; then
    close_editor || {
      status "[RESULT] FAIL full_rebuild (editor close failed)"
      return 1
    }
  fi

  taskkill //F //IM devenv.exe >> "$LOG" 2>&1 || true
  sleep 2

  status "[BUILDING] Regenerating project files..."
  "$ubt" -ProjectFiles -Project="$UPROJECT" -Game >> "$LOG" 2>&1
  local ubt_rc=$?
  if [ $ubt_rc -ne 0 ]; then
    status "[RESULT] FAIL full_rebuild (project file generation failed)"
    build_summary
    return 1
  fi

  sln="$(find_sln)" || return 1

  status "[BUILDING] VS build in progress..."
  "$devenv" "$sln" //Build "$BUILD_CONFIG" >> "$LOG" 2>&1
  local vs_rc=$?

  build_summary
  if [ $vs_rc -eq 0 ]; then
    status "[RESULT] SUCCESS full_rebuild"
    return 0
  else
    status "[RESULT] FAIL full_rebuild (vs_build failed)"
    return 1
  fi
}

# ── Auto Mode ─────────────────────────────────────────────────────────────────
# Escalation chain: live coding → clean VS build → full rebuild

do_auto() {
  local ed_state="not running"
  local vs_state="not running"
  editor_running && ed_state="running"
  vs_running && vs_state="running"
  status "[DETECT] Editor: ${ed_state} | VS: ${vs_state}"

  # Editor running → try live coding first
  if [ "$ed_state" = "running" ]; then
    if do_live_coding; then
      return 0
    fi
    status "[ESCALATE] live_coding failed — trying clean_vs_build"
    if do_clean_vs_build; then
      launch_editor
      return 0
    fi
    if has_state_errors; then
      status "[ESCALATE] state errors detected — running full_rebuild"
      if do_full_rebuild; then
        launch_editor
        return 0
      fi
    fi
    return 1
  fi

  # VS running → try VS build
  if [ "$vs_state" = "running" ]; then
    if do_vs_build; then
      launch_editor
      return 0
    fi
    if has_state_errors; then
      status "[ESCALATE] state errors detected — running full_rebuild"
      if do_full_rebuild; then
        launch_editor
        return 0
      fi
      return 1
    fi
    status "[RESULT] FAIL vs_build (code_errors)"
    return 1
  fi

  # Nothing running — try VS build, escalate if needed
  status "[DETECT] Nothing running — trying vs_build first"
  if do_vs_build; then
    launch_editor
    return 0
  fi
  if has_state_errors; then
    status "[ESCALATE] state errors detected — running full_rebuild"
    if do_full_rebuild; then
      launch_editor
      return 0
    fi
    return 1
  fi
  status "[RESULT] FAIL vs_build (code_errors)"
  return 1
}

# ── Main ──────────────────────────────────────────────────────────────────────

main() {
  local mode=""

  while [ $# -gt 0 ]; do
    case "$1" in
      --auto|--live|--vs|--clean|--full) mode="$1"; shift ;;
      *) mode="$1"; shift ;;
    esac
  done
  mode="${mode:---auto}"

  status "[PROJECT] $PROJECT_NAME ($UPROJECT)"
  log_init

  # Blueprint-only projects have no Source/ dir — build.sh requires C++ to compile
  if ! has_source_dir; then
    if [ "$mode" = "--live" ] && editor_running; then
      # Live coding can still work if the editor is already running
      :
    else
      status "[ERROR] No Source/ directory found in $PROJECT_DIR"
      status "[ERROR] build.sh requires a C++ project. For Blueprint-only projects,"
      status "[ERROR] open the .uproject directly — the editor will compile the plugin automatically."
      return 1
    fi
  fi

  local build_rc=0
  case "$mode" in
    --auto)
      do_auto || build_rc=$?
      ;;
    --live)
      if ! editor_running; then
        status "[ERROR] Editor not running — live coding requires the editor"
        return 1
      fi
      do_live_coding || build_rc=$?
      ;;
    --vs)
      do_vs_build || build_rc=$?
      [ $build_rc -eq 0 ] && launch_editor
      ;;
    --clean)
      do_clean_vs_build || build_rc=$?
      [ $build_rc -eq 0 ] && launch_editor
      ;;
    --full)
      do_full_rebuild || build_rc=$?
      [ $build_rc -eq 0 ] && launch_editor
      ;;
    *)
      status "[ERROR] Unknown mode: $mode (use --auto, --live, --vs, --clean, --full)"
      return 1
      ;;
  esac

  return $build_rc
}

main "$@"
