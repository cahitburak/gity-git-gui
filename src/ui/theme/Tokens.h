// GENERATED FROM GityDesign/TOKENS.json — DO NOT HAND-EDIT.
// Regenerate with: tools/generate-tokens.py
//
// QT_MAPPING.md: the graph painter and the delegates must read the same
// constants. Hand-editing here reintroduces exactly the drift this file exists
// to prevent.
#pragma once

#include <QColor>

#include <array>
#include <utility>
#include <vector>

namespace gity::ui::tokens {

// --- colour ---------------------------------------------------------

inline QColor desk{QColor(0x0E, 0x10, 0x13)};
inline QColor surfaceWindow{QColor(0x1C, 0x1F, 0x26)};
inline QColor surfacePanel{QColor(0x19, 0x1C, 0x22)};
inline QColor surfaceHeader{QColor(0x21, 0x25, 0x2C)};
inline QColor surfaceToolbar{QColor(0x1F, 0x23, 0x2A)};
inline QColor surfaceMenubar{QColor(0x23, 0x27, 0x2F)};
inline QColor surfaceTitlebarTop{QColor(0x2A, 0x2F, 0x39)};
inline QColor surfaceTitlebarBottom{QColor(0x23, 0x27, 0x2F)};
inline QColor surfaceSunken{QColor(0x14, 0x17, 0x1C)};
inline QColor surfaceGutter{QColor(0x1A, 0x1D, 0x23)};
inline QColor surfaceHunkHeader{QColor(0x23, 0x28, 0x33)};
inline QColor surfaceCheckbox{QColor(0x22, 0x26, 0x2E)};
inline QColor surfacePickDot{QColor(0x4A, 0x52, 0x60)};
inline QColor surfaceCheckerboard{QColor(0x20, 0x24, 0x2B)};
inline QColor borderWindow{QColor(0x2F, 0x35, 0x42)};
inline QColor borderHard{QColor(0x17, 0x1A, 0x20)};
inline QColor borderSoft{QColor(0x26, 0x2B, 0x34)};
inline QColor borderHairline{QColor(0x1F, 0x23, 0x2A)};
inline QColor borderControl{QColor(0x36, 0x3D, 0x49)};
inline QColor borderDivider{QColor(0x2F, 0x35, 0x3F)};
inline QColor borderPanelDivider{QColor(0x22, 0x26, 0x2E)};
inline QColor borderCheckbox{QColor(0x4A, 0x50, 0x5D)};
inline QColor textPrimary{QColor(0xE8, 0xEC, 0xF2)};
inline QColor textBody{QColor(0xD6, 0xDA, 0xE2)};
inline QColor textStrong{QColor(0xDB, 0xE1, 0xEA)};
inline QColor textDefault{QColor(0xCB, 0xD3, 0xDE)};
inline QColor textSecondary{QColor(0xB6, 0xBF, 0xCB)};
inline QColor textMuted{QColor(0xA8, 0xB1, 0xBF)};
inline QColor textDim{QColor(0x8B, 0x93, 0xA1)};
inline QColor textFaint{QColor(0x85, 0x8E, 0x9B)};
inline QColor textLabel{QColor(0x86, 0x8E, 0x9B)};
inline QColor textLabelDim{QColor(0x85, 0x8E, 0x9B)};
inline QColor textDisabled{QColor(0x4F, 0x57, 0x64)};
inline QColor textSelectedPrimary{QColor(0xF0, 0xF4, 0xFA)};
inline QColor textSelectedDim{QColor(0xA9, 0xB6, 0xC8)};
inline QColor textGutterNumber{QColor(0x7D, 0x87, 0x97)};
inline QColor textHunkHeader{QColor(0x87, 0x91, 0xA1)};
inline QColor selectionBg{QColor(0x2C, 0x36, 0x46)};
inline QColor selectionBorder{QColor(0x3A, 0x4A, 0x60)};
inline QColor selectionMarker{QColor(0x6A, 0xA9, 0xE0)};
inline QColor selectionHover{QColor(0x22, 0x27, 0x31)};
inline QColor selectionControlHover{QColor(0x2B, 0x31, 0x3B)};
inline QColor selectionControlBg{QColor(0x2B, 0x31, 0x3B)};
inline QColor selectionActiveTabBorder{QColor(0x4A, 0x61, 0x80)};
inline QColor accentPrimary{QColor(0x6A, 0xA9, 0xE0)};
inline QColor accentPrimaryLight{QColor(0x7F, 0xB7, 0xE8)};
inline QColor accentButtonBg{QColor(0x4D, 0x7F, 0xB8)};
inline QColor accentButtonHover{QColor(0x5A, 0x8D, 0xC7)};
inline QColor accentButtonFg{QColor(0xF0, 0xF5, 0xFB)};
inline QColor semanticAdd{QColor(0x82, 0xC4, 0x8D)};
inline QColor semanticAddBright{QColor(0x9D, 0xCA, 0xA6)};
inline QColor semanticAddText{QColor(0xC8, 0xE6, 0xCD)};
inline QColor semanticRemove{QColor(0xD9, 0x80, 0x80)};
inline QColor semanticRemoveText{QColor(0xE8, 0xCC, 0xCC)};
inline QColor semanticRemoveSoft{QColor(0xE0, 0xA7, 0xA7)};
inline QColor semanticWarn{QColor(0xE6, 0xA9, 0x4F)};
inline QColor semanticWarnSoft{QColor(0xA0, 0x83, 0x56)};
inline QColor semanticInfo{QColor(0x6A, 0xA9, 0xE0)};
inline QColor semanticMeta{QColor(0xC7, 0x8F, 0xC9)};

// Tint fills: the semantic hue at low alpha over surface.window.

inline QColor fillAddRow{QColor(130, 196, 141, 26)};
inline QColor fillAddGutter{QColor(130, 196, 141, 18)};
inline QColor fillAddChip{QColor(130, 196, 141, 31)};
inline QColor fillAddChipBorder{QColor(130, 196, 141, 92)};
inline QColor fillRemoveRow{QColor(217, 128, 128, 26)};
inline QColor fillRemoveGutter{QColor(217, 128, 128, 18)};
inline QColor fillRemoveChip{QColor(217, 128, 128, 31)};
inline QColor fillRemoveChipBorder{QColor(217, 128, 128, 102)};
inline QColor fillWarnChip{QColor(230, 169, 79, 31)};
inline QColor fillWarnChipBorder{QColor(230, 169, 79, 97)};
inline QColor fillInfoChip{QColor(106, 169, 224, 36)};
inline QColor fillInfoChipBorder{QColor(106, 169, 224, 115)};
inline QColor fillMetaChip{QColor(199, 143, 201, 36)};
inline QColor fillMetaChipBorder{QColor(199, 143, 201, 107)};
inline QColor fillNeutralChip{QColor(139, 147, 161, 26)};
inline QColor fillNeutralChipBorder{QColor(139, 147, 161, 66)};
inline QColor fillChangedRow{QColor(230, 169, 79, 9)};

/// Every colour above, by the name the stylesheet and theme variants use.
/// The stylesheet is
/// text and cannot reach a C++ constant, so it names tokens and they are
/// substituted at load; this table is what makes a typo in the sheet a
/// reported error instead of a silently missing colour.
/// Mutable on purpose: a theme is applied by assigning through this table,
/// so the hand-painted views pick the new value up on their next paint
/// without every call site knowing a theme can change.
[[nodiscard]] inline const std::vector<std::pair<const char*, QColor*>>& byName() {
    static const std::vector<std::pair<const char*, QColor*>> table{
        {"desk", &desk},
        {"surfaceWindow", &surfaceWindow},
        {"surfacePanel", &surfacePanel},
        {"surfaceHeader", &surfaceHeader},
        {"surfaceToolbar", &surfaceToolbar},
        {"surfaceMenubar", &surfaceMenubar},
        {"surfaceTitlebarTop", &surfaceTitlebarTop},
        {"surfaceTitlebarBottom", &surfaceTitlebarBottom},
        {"surfaceSunken", &surfaceSunken},
        {"surfaceGutter", &surfaceGutter},
        {"surfaceHunkHeader", &surfaceHunkHeader},
        {"surfaceCheckbox", &surfaceCheckbox},
        {"surfacePickDot", &surfacePickDot},
        {"surfaceCheckerboard", &surfaceCheckerboard},
        {"borderWindow", &borderWindow},
        {"borderHard", &borderHard},
        {"borderSoft", &borderSoft},
        {"borderHairline", &borderHairline},
        {"borderControl", &borderControl},
        {"borderDivider", &borderDivider},
        {"borderPanelDivider", &borderPanelDivider},
        {"borderCheckbox", &borderCheckbox},
        {"textPrimary", &textPrimary},
        {"textBody", &textBody},
        {"textStrong", &textStrong},
        {"textDefault", &textDefault},
        {"textSecondary", &textSecondary},
        {"textMuted", &textMuted},
        {"textDim", &textDim},
        {"textFaint", &textFaint},
        {"textLabel", &textLabel},
        {"textLabelDim", &textLabelDim},
        {"textDisabled", &textDisabled},
        {"textSelectedPrimary", &textSelectedPrimary},
        {"textSelectedDim", &textSelectedDim},
        {"textGutterNumber", &textGutterNumber},
        {"textHunkHeader", &textHunkHeader},
        {"selectionBg", &selectionBg},
        {"selectionBorder", &selectionBorder},
        {"selectionMarker", &selectionMarker},
        {"selectionHover", &selectionHover},
        {"selectionControlHover", &selectionControlHover},
        {"selectionControlBg", &selectionControlBg},
        {"selectionActiveTabBorder", &selectionActiveTabBorder},
        {"accentPrimary", &accentPrimary},
        {"accentPrimaryLight", &accentPrimaryLight},
        {"accentButtonBg", &accentButtonBg},
        {"accentButtonHover", &accentButtonHover},
        {"accentButtonFg", &accentButtonFg},
        {"semanticAdd", &semanticAdd},
        {"semanticAddBright", &semanticAddBright},
        {"semanticAddText", &semanticAddText},
        {"semanticRemove", &semanticRemove},
        {"semanticRemoveText", &semanticRemoveText},
        {"semanticRemoveSoft", &semanticRemoveSoft},
        {"semanticWarn", &semanticWarn},
        {"semanticWarnSoft", &semanticWarnSoft},
        {"semanticInfo", &semanticInfo},
        {"semanticMeta", &semanticMeta},
        {"fillAddRow", &fillAddRow},
        {"fillAddGutter", &fillAddGutter},
        {"fillAddChip", &fillAddChip},
        {"fillAddChipBorder", &fillAddChipBorder},
        {"fillRemoveRow", &fillRemoveRow},
        {"fillRemoveGutter", &fillRemoveGutter},
        {"fillRemoveChip", &fillRemoveChip},
        {"fillRemoveChipBorder", &fillRemoveChipBorder},
        {"fillWarnChip", &fillWarnChip},
        {"fillWarnChipBorder", &fillWarnChipBorder},
        {"fillInfoChip", &fillInfoChip},
        {"fillInfoChipBorder", &fillInfoChipBorder},
        {"fillMetaChip", &fillMetaChip},
        {"fillMetaChipBorder", &fillMetaChipBorder},
        {"fillNeutralChip", &fillNeutralChip},
        {"fillNeutralChipBorder", &fillNeutralChipBorder},
        {"fillChangedRow", &fillChangedRow},
    };
    return table;
}

/// The values as authored in TOKENS.json, before any theme variant.
///
/// Captured once, on first use, so a variant is always applied over the
/// design's own colours rather than over whatever the last one left behind.
[[nodiscard]] inline const std::vector<std::pair<const char*, QColor>>& defaults() {
    static const std::vector<std::pair<const char*, QColor>> captured = [] {
        std::vector<std::pair<const char*, QColor>> out;
        out.reserve(byName().size());
        for (const auto& [name, colour] : byName()) {
            out.emplace_back(name, *colour);
        }
        return out;
    }();
    return captured;
}


/// Lane colours cycle by lane index. Six, not eight: the spec fixes the count
/// because edge colour is the *parent's* lane colour and a longer cycle makes
/// adjacent lanes harder to tell apart.
inline const std::array<QColor, 11> graphLanes{QColor(0x6A, 0xA9, 0xE0), QColor(0xE6, 0xA9, 0x4F), QColor(0x82, 0xC4, 0x8D), QColor(0xC7, 0x8F, 0xC9), QColor(0xD9, 0x80, 0x80), QColor(0x5F, 0xBE, 0xC4), QColor(0x7C, 0x7C, 0xE4), QColor(0xD0, 0xE0, 0x69), QColor(0xE0, 0x69, 0xE0), QColor(0x69, 0xE0, 0x69), QColor(0xC4, 0xA9, 0x82)};

[[nodiscard]] inline QColor laneColor(int lane) noexcept {
    return graphLanes[static_cast<std::size_t>(lane) % graphLanes.size()];
}

// --- metrics --------------------------------------------------------

/// Row pitch is the single source of truth for graph geometry (SPEC.md).
/// The delegate's sizeHint().height() must equal this, or the lanes drift a
/// pixel per row — 14px of error by row 15, which reads as a broken graph.
inline constexpr int rowHeight = 26;
inline constexpr int rowHeightDense = 22;
inline constexpr int lanePitch = 14;
inline constexpr int firstLaneX = 18;
inline constexpr double nodeRadius = 3.6;
inline constexpr double mergeNodeRadius = 4.2;
inline constexpr double nodeStrokeWidth = 1.2;
inline constexpr double mergeNodeStrokeWidth = 1.8;
inline constexpr double edgeStrokeWidth = 1.6;
inline constexpr int textGutterAfterLastLane = 16;

/// Bézier control offsets for a lane-changing edge, in logical px at pitch 26.
/// Scale by rowHeight / 26 when dense rows are on.
inline constexpr int edgeControlDown = 13;
inline constexpr int edgeControlUp = -15;

inline constexpr int windowWidth = 1440;
inline constexpr int windowHeight = 900;
inline constexpr int windowMinWidth = 1024;

// chrome
inline constexpr int chromeTitlebar = 34;
inline constexpr int chromeMenubar = 27;
inline constexpr int chromeToolbar = 46;
inline constexpr int chromeStatusbar = 27;
inline constexpr int chromePaneHeader = 33;
inline constexpr int chromeColumnHeader = 25;
inline constexpr int chromeListSectionHeader = 29;

// panes
inline constexpr int panesSidebar = 236;
inline constexpr int panesHistoryGraphHeight = 372;
inline constexpr int panesCommitDetail = 330;
inline constexpr int panesWorkingCopyList = 396;

// columns
inline constexpr int columnsGraphDescription = 400;
inline constexpr int columnsMessageMin = 220;
inline constexpr int columnsAuthor = 150;
inline constexpr int columnsDate = 108;
inline constexpr int columnsCommit = 84;

// diffGutter
inline constexpr int diffGutterStagePickWidth = 22;
inline constexpr int diffGutterLineNumberWidth = 46;
inline constexpr int diffGutterLineNumberWidthStaged = 42;
inline constexpr int diffGutterSignWidth = 15;

// radius
inline constexpr int radiusChip = 3;
inline constexpr int radiusControl = 4;
inline constexpr int radiusWindow = 8;
inline constexpr int radiusPill = 999;

// --- type -----------------------------------------------------------

inline constexpr int fontSizeMicroLabel = 10;
inline constexpr int fontSizeChip = 10;
inline constexpr int fontSizeCaption = 11;
inline constexpr int fontSizeControl = 12;
inline constexpr int fontSizeCode = 12;
inline constexpr int fontSizeBody = 12;
inline constexpr int fontSizeRow = 12;
inline constexpr int fontSizeDetailTitle = 14;
inline constexpr int fontSizeSectionTitle = 17;

inline constexpr int codeLineHeight = 18;

} // namespace gity::ui::tokens
