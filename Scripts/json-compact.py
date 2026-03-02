"""Compact JSON→text formatter for ue-tool.sh responses.

Strips JSON syntax overhead (~50% of tokens) while preserving readability.
Piped: curl ... | python json-compact.py

Rules:
  - Strips success=true boilerplate, unwraps single-key result wrapper
  - Scalar values inline with key: name: CapsuleComponent
  - Flat dicts (all scalar) on one line: pos: x=0, y=100, z=0
  - Arrays of flat dicts: one item per line with - prefix
  - Large arrays (>30 items): truncated with count
  - No quotes unless string contains special chars
"""

import json, sys, os, base64, tempfile

DEFAULT_IMAGE_PATH = os.path.join(tempfile.gettempdir(), "ue-capture.jpg")


def extract_image(data, save_path=None):
    """Auto-extract image_base64 from any response. Always saves to file.
    Uses save_path if provided, else DEFAULT_IMAGE_PATH."""
    if not isinstance(data, dict):
        return data
    b64 = data.get("image_base64")
    if not b64:
        for v in data.values():
            if isinstance(v, dict):
                extract_image(v, save_path)
        return data
    dest = save_path or DEFAULT_IMAGE_PATH
    raw = base64.b64decode(b64)
    with open(dest, "wb") as f:
        f.write(raw)
    data["image_base64"] = f"[saved to {dest}] ({len(raw)} bytes)"
    return data


def fmt_val(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    if isinstance(v, str):
        if not v:
            return '""'
        if any(c in v for c in ('\n', ':', ',', '{', '}', '[', ']')):
            return repr(v)
        return v
    return str(v)


def is_flat(d):
    return isinstance(d, dict) and all(
        isinstance(v, (str, int, float, bool, type(None))) for v in d.values()
    )


def flat_pairs(d):
    return ", ".join(f"{k}={fmt_val(v)}" for k, v in d.items())


def fmt(obj, depth=0):
    ind = "  " * depth

    if not isinstance(obj, (dict, list)):
        return f"{ind}{fmt_val(obj)}"

    if isinstance(obj, list):
        if not obj:
            return f"{ind}[]"
        # All scalars → comma-separated on one line
        if all(isinstance(x, (str, int, float, bool, type(None))) for x in obj):
            items = [fmt_val(x) for x in obj[:30]]
            line = ", ".join(items)
            if len(obj) > 30:
                line += f" ... +{len(obj) - 30} more"
            return f"{ind}{line}"
        # Array of objects
        lines = []
        limit = 30
        for x in obj[:limit]:
            if isinstance(x, dict) and is_flat(x):
                lines.append(f"{ind}- {flat_pairs(x)}")
            elif isinstance(x, dict):
                lines.append(f"{ind}-")
                lines.append(fmt_body(x, depth + 1))
            else:
                lines.append(f"{ind}- {fmt_val(x)}")
        if len(obj) > limit:
            lines.append(f"{ind}... +{len(obj) - limit} more")
        return "\n".join(lines)

    # dict
    return fmt_body(obj, depth)


def fmt_body(d, depth=0):
    if not d:
        return "  " * depth + "{}"
    ind = "  " * depth
    lines = []
    for k, v in d.items():
        if isinstance(v, (str, int, float, bool, type(None))):
            lines.append(f"{ind}{k}: {fmt_val(v)}")
        elif isinstance(v, dict) and is_flat(v) and len(v) <= 6:
            lines.append(f"{ind}{k}: {flat_pairs(v)}")
        elif isinstance(v, list) and not v:
            lines.append(f"{ind}{k}: []")
        elif isinstance(v, list) and all(
            isinstance(x, (str, int, float, bool, type(None))) for x in v
        ):
            items = [fmt_val(x) for x in v[:30]]
            line = ", ".join(items)
            if len(v) > 30:
                line += f" ... +{len(v) - 30} more"
            lines.append(f"{ind}{k}: {line}")
        else:
            lines.append(f"{ind}{k}:")
            lines.append(fmt(v, depth + 1))
    return "\n".join(lines)


def unwrap(data):
    """Strip boilerplate: success=true, single-key result wrapper."""
    if not isinstance(data, dict):
        return data
    data.pop("success", None)
    # Unwrap {"result": ...} when it's the only remaining key
    if list(data.keys()) == ["result"]:
        return data["result"]
    return data


def main():
    raw = sys.stdin.read().strip()
    if not raw:
        return

    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        # Not JSON — pass through as-is
        print(raw)
        return

    save_path = os.environ.get("UE_TOOL_SAVE_IMAGE") or None
    data = extract_image(data, save_path)

    data = unwrap(data)

    if isinstance(data, dict):
        print(fmt_body(data, 0))
    else:
        print(fmt(data, 0))


if __name__ == "__main__":
    main()
