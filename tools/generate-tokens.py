#!/usr/bin/env python3
"""Generates src/ui/theme/Tokens.h from GityDesign/TOKENS.json.

QT_MAPPING.md is explicit that hex values must not be pasted into paint code:
the graph painter and the delegates have to read the same constants, or they
drift. So the design tokens have exactly one representation in C++, and it is
generated from the file the designer owns.

Run after any change to TOKENS.json:

    tools/generate-tokens.py

The output is committed rather than generated during the build. Generating it
in CMake would put a Python dependency on all five CI legs to produce a file
that changes only when the design does.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOKENS = ROOT / "GityDesign" / "TOKENS.json"
OUTPUT = ROOT / "src" / "ui" / "theme" / "Tokens.h"

RGBA = re.compile(r"rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d.]+)\s*\)")


def to_qcolor(value: str) -> str:
    """Renders a hex or rgba() token as a QColor initialiser."""
    match = RGBA.match(value)
    if match:
        r, g, b, a = match.groups()
        return f"QColor({r}, {g}, {b}, {round(float(a) * 255)})"
    if value.startswith("#") and len(value) == 7:
        r, g, b = (int(value[i : i + 2], 16) for i in (1, 3, 5))
        return f"QColor(0x{r:02X}, 0x{g:02X}, 0x{b:02X})"
    raise ValueError(f"unrecognised colour token: {value!r}")


def camel(name: str) -> str:
    return name[0].lower() + name[1:] if name else name


def emit_colors(out: list[str], prefix: str, node: dict, names: list[str] | None = None) -> None:
    for key, value in node.items():
        if key.startswith("$"):
            continue
        name = f"{prefix}{key[0].upper()}{key[1:]}" if prefix else camel(key)
        if isinstance(value, dict):
            emit_colors(out, name, value, names)
        elif isinstance(value, str):
            out.append(f"inline QColor {name}{{{to_qcolor(value)}}};")
            if names is not None:
                names.append(name)


def main() -> int:
    if not TOKENS.exists():
        print(f"missing {TOKENS}", file=sys.stderr)
        return 1

    data = json.loads(TOKENS.read_text(encoding="utf-8"))
    color = data["color"]
    metrics = data["metrics"]
    type_ = data["type"]

    lines: list[str] = [
        "// GENERATED FROM GityDesign/TOKENS.json — DO NOT HAND-EDIT.",
        "// Regenerate with: tools/generate-tokens.py",
        "//",
        "// QT_MAPPING.md: the graph painter and the delegates must read the same",
        "// constants. Hand-editing here reintroduces exactly the drift this file exists",
        "// to prevent.",
        "#pragma once",
        "",
        "#include <QColor>",
        "",
        "#include <array>",
        "#include <utility>",
        "#include <vector>",
        "",
        "namespace gity::ui::tokens {",
        "",
        "// --- colour ---------------------------------------------------------",
        "",
    ]

    flat = {k: v for k, v in color.items() if k not in ("graphLanes", "$fills")}
    names: list[str] = []
    emit_colors(lines, "", flat, names)

    lines += ["", "// Tint fills: the semantic hue at low alpha over surface.window.", ""]
    for key, value in color["$fills"].items():
        if key.startswith("$"):
            continue
        fill_name = f"fill{key[0].upper()}{key[1:]}"
        lines.append(f"inline QColor {fill_name}{{{to_qcolor(value)}}};")
        names.append(fill_name)

    lines += [
        "",
        "/// Every colour above, by the name the stylesheet and theme variants use.",
        "/// The stylesheet is",
        "/// text and cannot reach a C++ constant, so it names tokens and they are",
        "/// substituted at load; this table is what makes a typo in the sheet a",
        "/// reported error instead of a silently missing colour.",
        "/// Mutable on purpose: a theme is applied by assigning through this table,",
        "/// so the hand-painted views pick the new value up on their next paint",
        "/// without every call site knowing a theme can change.",
        "[[nodiscard]] inline const std::vector<std::pair<const char*, QColor*>>& byName() {",
        "    static const std::vector<std::pair<const char*, QColor*>> table{",
    ]
    for name in names:
        lines.append(f'        {{"{name}", &{name}}},')
    lines += ["    };", "    return table;", "}", ""]

    lines += [
        "/// The values as authored in TOKENS.json, before any theme variant.",
        "///",
        "/// Captured once, on first use, so a variant is always applied over the",
        "/// design's own colours rather than over whatever the last one left behind.",
        "[[nodiscard]] inline const std::vector<std::pair<const char*, QColor>>& defaults() {",
        "    static const std::vector<std::pair<const char*, QColor>> captured = [] {",
        "        std::vector<std::pair<const char*, QColor>> out;",
        "        out.reserve(byName().size());",
        "        for (const auto& [name, colour] : byName()) {",
        "            out.emplace_back(name, *colour);",
        "        }",
        "        return out;",
        "    }();",
        "    return captured;",
        "}",
        "",
    ]

    lanes = ", ".join(to_qcolor(c) for c in color["graphLanes"])
    lines += [
        "",
        "/// Lane colours cycle by lane index. Six, not eight: the spec fixes the count",
        "/// because edge colour is the *parent's* lane colour and a longer cycle makes",
        "/// adjacent lanes harder to tell apart.",
        f"inline const std::array<QColor, {len(color['graphLanes'])}> graphLanes{{{lanes}}};",
        "",
        "[[nodiscard]] inline QColor laneColor(int lane) noexcept {",
        "    return graphLanes[static_cast<std::size_t>(lane) % graphLanes.size()];",
        "}",
        "",
        "// --- metrics --------------------------------------------------------",
        "",
    ]

    graph = metrics["graph"]
    lines += [
        "/// Row pitch is the single source of truth for graph geometry (SPEC.md).",
        "/// The delegate's sizeHint().height() must equal this, or the lanes drift a",
        "/// pixel per row — 14px of error by row 15, which reads as a broken graph.",
        f"inline constexpr int rowHeight = {graph['rowHeight']};",
        f"inline constexpr int rowHeightDense = {graph['rowHeightDense']};",
        f"inline constexpr int lanePitch = {graph['lanePitch']};",
        f"inline constexpr int firstLaneX = {graph['firstLaneX']};",
        f"inline constexpr double nodeRadius = {graph['nodeRadius']};",
        f"inline constexpr double mergeNodeRadius = {graph['mergeNodeRadius']};",
        f"inline constexpr double nodeStrokeWidth = {graph['nodeStrokeWidth']};",
        f"inline constexpr double mergeNodeStrokeWidth = {graph['mergeNodeStrokeWidth']};",
        f"inline constexpr double edgeStrokeWidth = {graph['edgeStrokeWidth']};",
        f"inline constexpr int textGutterAfterLastLane = {graph['textGutterAfterLastLane']};",
        "",
        "/// Bézier control offsets for a lane-changing edge, in logical px at pitch 26.",
        "/// Scale by rowHeight / 26 when dense rows are on.",
        "inline constexpr int edgeControlDown = 13;",
        "inline constexpr int edgeControlUp = -15;",
        "",
    ]

    for key, value in metrics["window"].items():
        if not key.startswith("$"):
            lines.append(f"inline constexpr int window{key[0].upper()}{key[1:]} = {value};")
    lines.append("")

    for group in ("chrome", "panes", "columns", "diffGutter", "radius"):
        lines.append(f"// {group}")
        for key, value in metrics[group].items():
            if key.startswith("$"):
                continue
            lines.append(f"inline constexpr int {camel(group)}{key[0].upper()}{key[1:]} = {value};")
        lines.append("")

    lines += ["// --- type -----------------------------------------------------------", ""]
    for key, value in type_["size"].items():
        if key.startswith("$"):
            continue
        # Fractional sizes are prototype-only; SPEC.md says round to integer points.
        lines.append(f"inline constexpr int fontSize{key[0].upper()}{key[1:]} = {round(value)};")
    lines += [
        "",
        f"inline constexpr int codeLineHeight = {type_['codeLineHeight']};",
        "",
        "} // namespace gity::ui::tokens",
        "",
    ]

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)} ({len(lines)} lines)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
