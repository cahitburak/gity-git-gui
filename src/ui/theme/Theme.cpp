#include "Theme.h"

#include "Tokens.h"

#include <QApplication>
#include <QWidget>
#include <QFile>
#include <cstring>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#include <QFontMetrics>

/// Pulls the compiled stylesheet into the link.
///
/// At global scope on purpose, and not in an anonymous namespace:
/// Q_INIT_RESOURCE expands to a call on a symbol the resource compiler defines
/// at global scope, and an anonymous namespace makes the call resolve to a
/// local one that nothing defines.
static void initialiseThemeResources() {
    Q_INIT_RESOURCE(theme);
}

namespace gity::ui {
namespace {

bool g_initialized = false;

bool paletteIsDark(const QPalette& palette) {
    return palette.color(QPalette::Window).lightness() < 128;
}

} // namespace

Theme& Theme::instance() {
    static Theme theme;
    return theme;
}

const Theme& Theme::current() {
    if (!g_initialized) {
        refresh();
    }
    return instance();
}

void Theme::applyPalette(QApplication* app) {
    if (app == nullptr) {
        return;
    }
    // Fusion first: the native styles on some platforms ignore palette roles
    // and paint their own colours, which would leave a dialog half themed.
    app->setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, tokens::surfaceWindow);
    palette.setColor(QPalette::WindowText, tokens::textDefault);
    palette.setColor(QPalette::Base, tokens::surfaceSunken);
    palette.setColor(QPalette::AlternateBase, tokens::surfacePanel);
    palette.setColor(QPalette::Text, tokens::textDefault);
    palette.setColor(QPalette::Button, tokens::surfacePanel);
    palette.setColor(QPalette::ButtonText, tokens::textDefault);
    palette.setColor(QPalette::BrightText, tokens::textStrong);
    palette.setColor(QPalette::Highlight, tokens::selectionBg);
    palette.setColor(QPalette::HighlightedText, tokens::textStrong);
    palette.setColor(QPalette::Link, tokens::accentPrimary);
    palette.setColor(QPalette::ToolTipBase, tokens::surfacePanel);
    palette.setColor(QPalette::ToolTipText, tokens::textDefault);
    palette.setColor(QPalette::PlaceholderText, tokens::textLabel);

    // Disabled roles have to be set explicitly. Left to Qt they are derived
    // from a light palette and a disabled button comes out brighter than an
    // enabled one — which reads as the opposite of disabled.
    palette.setColor(QPalette::Disabled, QPalette::Text, tokens::textLabelDim);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, tokens::textLabelDim);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, tokens::textLabelDim);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, tokens::surfacePanel);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, tokens::textLabelDim);

    app->setPalette(palette);
    refresh();
    applyStyleSheet(app);
}

namespace {

/// Overrides for one variant, by token name. Only what changes.
using Overrides = std::vector<std::pair<const char*, QColor>>;

const Overrides& midnightOverrides() {
    // The same design, further from the light. Surfaces drop, borders come up
    // slightly so edges stay findable on the darker ground, and nothing else
    // moves — the semantic colours are already tuned against a dark surface.
    static const Overrides table{
        {"surfaceWindow", QColor(0x14, 0x16, 0x1B)},
        {"surfacePanel", QColor(0x11, 0x13, 0x18)},
        {"surfaceToolbar", QColor(0x16, 0x19, 0x1E)},
        {"surfaceHeader", QColor(0x13, 0x15, 0x1A)},
        {"surfaceSunken", QColor(0x0D, 0x0F, 0x13)},
        {"borderHard", QColor(0x0B, 0x0D, 0x11)},
        {"borderPanelDivider", QColor(0x1C, 0x20, 0x27)},
        {"borderControl", QColor(0x3B, 0x43, 0x51)},
        {"selectionBg", QColor(0x22, 0x2B, 0x39)},
        {"selectionHover", QColor(0x1A, 0x1E, 0x25)},
    };
    return table;
}

const Overrides& lightOverrides() {
    // Surfaces and text invert; the semantic hues are darkened rather than
    // reused, because a green tuned to glow on near-black is unreadable on
    // white. Diff and chip fills are restated for the same reason.
    static const Overrides table{
        {"surfaceWindow", QColor(0xFA, 0xFB, 0xFC)},
        {"surfacePanel", QColor(0xF1, 0xF3, 0xF6)},
        {"surfaceToolbar", QColor(0xEC, 0xEF, 0xF3)},
        {"surfaceHeader", QColor(0xE7, 0xEA, 0xEF)},
        {"surfaceSunken", QColor(0xFF, 0xFF, 0xFF)},

        {"borderHard", QColor(0xD0, 0xD7, 0xDE)},
        {"borderPanelDivider", QColor(0xDD, 0xE2, 0xE8)},
        {"borderDivider", QColor(0xD0, 0xD7, 0xDE)},
        {"borderControl", QColor(0xC4, 0xCC, 0xD5)},
        {"borderSoft", QColor(0xE4, 0xE8, 0xED)},

        // Secondary text is held to 4.5:1 on the darkest surface it sits on
        // (the header); hierarchy comes from size and weight, not from fading
        // text past legibility (2026-09 UI review).
        {"textStrong", QColor(0x14, 0x18, 0x1D)},
        {"textDefault", QColor(0x24, 0x2A, 0x31)},
        {"textMuted", QColor(0x45, 0x4D, 0x57)},
        {"textDim", QColor(0x57, 0x60, 0x6B)},
        {"textLabel", QColor(0x61, 0x69, 0x74)},
        {"textLabelDim", QColor(0x61, 0x69, 0x73)},
        {"textFaint", QColor(0x60, 0x69, 0x75)},
        {"textDisabled", QColor(0xA5, 0xAD, 0xB8)},
        {"textSelectedPrimary", QColor(0x10, 0x14, 0x19)},

        {"selectionBg", QColor(0xDD, 0xE7, 0xF5)},
        {"selectionHover", QColor(0xEC, 0xF0, 0xF5)},
        {"selectionMarker", QColor(0x2F, 0x6F, 0xB8)},
        {"selectionControlBg", QColor(0xE8, 0xEC, 0xF1)},
        {"selectionControlHover", QColor(0xDF, 0xE4, 0xEB)},

        {"accentPrimary", QColor(0x1F, 0x5F, 0xA8)},
        {"accentPrimaryLight", QColor(0x2F, 0x6F, 0xB8)},
        {"accentButtonBg", QColor(0x1F, 0x5F, 0xA8)},
        {"accentButtonFg", QColor(0xFF, 0xFF, 0xFF)},

        {"semanticAdd", QColor(0x1A, 0x7F, 0x37)},
        {"semanticAddBright", QColor(0x14, 0x6C, 0x2E)},
        {"semanticRemove", QColor(0xB3, 0x2D, 0x2D)},
        {"semanticWarn", QColor(0x9A, 0x62, 0x00)},
        {"semanticWarnSoft", QColor(0xA8, 0x72, 0x1C)},
        {"semanticInfo", QColor(0x1F, 0x5F, 0xA8)},
        {"semanticMeta", QColor(0x6B, 0x46, 0xA8)},

        {"fillAddRow", QColor(0xE3, 0xF5, 0xE8)},
        {"fillRemoveRow", QColor(0xFC, 0xE8, 0xE8)},
        {"fillChangedRow", QColor(0xFD, 0xF3, 0xE0)},
        {"fillAddChip", QColor(0xDF, 0xF3, 0xE5)},
        {"fillRemoveChip", QColor(0xFB, 0xE4, 0xE4)},
        {"fillWarnChip", QColor(0xFC, 0xF1, 0xDC)},
        {"fillNeutralChip", QColor(0xE7, 0xEB, 0xF0)},
        {"fillInfoChip", QColor(0xE2, 0xEC, 0xF8)},
        {"fillMetaChip", QColor(0xEE, 0xE7, 0xF8)},

        // The surfaces the diff paints on. Missed on the first pass, which
        // left a black gutter and hunk header sitting in a light window —
        // these are read by hand-painted code rather than by the stylesheet,
        // so nothing about the chrome looking right implied they were covered.
        {"surfaceGutter", QColor(0xF1, 0xF3, 0xF6)},
        {"surfaceHunkHeader", QColor(0xE7, 0xEC, 0xF2)},
        {"surfaceMenubar", QColor(0xEC, 0xEF, 0xF3)},
        {"surfaceTitlebarTop", QColor(0xF2, 0xF4, 0xF7)},
        {"surfaceTitlebarBottom", QColor(0xE9, 0xED, 0xF2)},
        {"desk", QColor(0xE4, 0xE8, 0xEE)},

        {"borderWindow", QColor(0xC8, 0xD0, 0xD9)},
        {"borderHairline", QColor(0xE2, 0xE6, 0xEC)},
        {"selectionBorder", QColor(0xB6, 0xC8, 0xE2)},
        {"selectionActiveTabBorder", QColor(0x2F, 0x6F, 0xB8)},

        {"textPrimary", QColor(0x14, 0x18, 0x1D)},
        {"textBody", QColor(0x24, 0x2A, 0x31)},
        {"textSecondary", QColor(0x45, 0x4D, 0x57)},
        {"textSelectedDim", QColor(0x3A, 0x44, 0x52)},

        {"accentButtonHover", QColor(0x2A, 0x6C, 0xB6)},
        {"semanticAddText", QColor(0x11, 0x5C, 0x28)},
        {"semanticRemoveText", QColor(0x8E, 0x22, 0x22)},
        {"semanticRemoveSoft", QColor(0xC0, 0x4A, 0x4A)},

        {"fillAddGutter", QColor(0xC9, 0xE9, 0xD3)},
        {"fillRemoveGutter", QColor(0xF6, 0xD2, 0xD2)},
        {"fillAddChipBorder", QColor(0xA8, 0xD8, 0xB8)},
        {"fillRemoveChipBorder", QColor(0xEB, 0xB6, 0xB6)},
        {"fillWarnChipBorder", QColor(0xE6, 0xCE, 0x9C)},
        {"fillNeutralChipBorder", QColor(0xCE, 0xD5, 0xDE)},
        {"fillInfoChipBorder", QColor(0xB6, 0xCD, 0xEA)},
        {"fillMetaChipBorder", QColor(0xD2, 0xC4, 0xEC)},

        // The six the designer confirmed, which are read by hand-painted code
        // and so are invisible to the stylesheet — the same way the diff
        // gutter was missed the first time a variant was written.
        {"textGutterNumber", QColor(0x65, 0x6E, 0x7B)},
        {"textHunkHeader", QColor(0x55, 0x5E, 0x6B)},
        {"surfaceCheckbox", QColor(0xFF, 0xFF, 0xFF)},
        {"borderCheckbox", QColor(0xB4, 0xBD, 0xC7)},
        {"surfacePickDot", QColor(0xA7, 0xB0, 0xBC)},
        {"surfaceCheckerboard", QColor(0xE8, 0xEB, 0xEF)},
    };
    return table;
}

const Overrides& neutralDarkOverrides() {
    // Grey without the slate: every surface, border and text colour loses
    // its blue, and the accent becomes a plain, brighter blue. The semantic
    // hues are the authored ones — they were tuned against a dark ground and
    // read the same on this one (all 4.5:1 or better on the header).
    static const Overrides table{
        {"desk", QColor(0x10, 0x10, 0x10)},
        {"surfaceWindow", QColor(0x1E, 0x1E, 0x1E)},
        {"surfacePanel", QColor(0x1B, 0x1B, 0x1B)},
        {"surfaceHeader", QColor(0x25, 0x25, 0x25)},
        {"surfaceToolbar", QColor(0x23, 0x23, 0x23)},
        {"surfaceMenubar", QColor(0x26, 0x26, 0x26)},
        {"surfaceTitlebarTop", QColor(0x2C, 0x2C, 0x2C)},
        {"surfaceTitlebarBottom", QColor(0x26, 0x26, 0x26)},
        {"surfaceSunken", QColor(0x17, 0x17, 0x17)},
        {"surfaceGutter", QColor(0x1C, 0x1C, 0x1C)},
        {"surfaceHunkHeader", QColor(0x28, 0x28, 0x28)},
        {"surfaceCheckbox", QColor(0x26, 0x26, 0x26)},
        {"surfacePickDot", QColor(0x55, 0x55, 0x55)},
        {"surfaceCheckerboard", QColor(0x24, 0x24, 0x24)},

        {"borderWindow", QColor(0x36, 0x36, 0x36)},
        {"borderHard", QColor(0x15, 0x15, 0x15)},
        {"borderSoft", QColor(0x2A, 0x2A, 0x2A)},
        {"borderHairline", QColor(0x23, 0x23, 0x23)},
        {"borderControl", QColor(0x3E, 0x3E, 0x3E)},
        {"borderDivider", QColor(0x34, 0x34, 0x34)},
        {"borderPanelDivider", QColor(0x26, 0x26, 0x26)},
        {"borderCheckbox", QColor(0x54, 0x54, 0x54)},

        {"textPrimary", QColor(0xED, 0xED, 0xED)},
        {"textBody", QColor(0xDA, 0xDA, 0xDA)},
        {"textStrong", QColor(0xE2, 0xE2, 0xE2)},
        {"textDefault", QColor(0xD0, 0xD0, 0xD0)},
        {"textSecondary", QColor(0xBD, 0xBD, 0xBD)},
        {"textMuted", QColor(0xAD, 0xAD, 0xAD)},
        {"textDim", QColor(0x99, 0x99, 0x99)},
        {"textFaint", QColor(0x94, 0x94, 0x94)},
        {"textLabel", QColor(0x96, 0x96, 0x96)},
        {"textLabelDim", QColor(0x94, 0x94, 0x94)},
        {"textDisabled", QColor(0x55, 0x55, 0x55)},
        {"textSelectedPrimary", QColor(0xF5, 0xF5, 0xF5)},
        {"textSelectedDim", QColor(0xB4, 0xB4, 0xB4)},
        {"textGutterNumber", QColor(0x8A, 0x8A, 0x8A)},
        {"textHunkHeader", QColor(0x94, 0x94, 0x94)},

        {"selectionBg", QColor(0x37, 0x39, 0x3E)},
        {"selectionBorder", QColor(0x48, 0x4B, 0x52)},
        {"selectionMarker", QColor(0x4A, 0x9E, 0xFF)},
        {"selectionHover", QColor(0x2A, 0x2A, 0x2B)},
        {"selectionControlHover", QColor(0x36, 0x36, 0x36)},
        {"selectionControlBg", QColor(0x31, 0x31, 0x31)},
        {"selectionActiveTabBorder", QColor(0x4A, 0x9E, 0xFF)},

        {"accentPrimary", QColor(0x4A, 0x9E, 0xFF)},
        {"accentPrimaryLight", QColor(0x6C, 0xB2, 0xFF)},
        {"accentButtonBg", QColor(0x1F, 0x6F, 0xC5)},
        {"accentButtonHover", QColor(0x2A, 0x7D, 0xD6)},
        {"accentButtonFg", QColor(0xFF, 0xFF, 0xFF)},
        {"semanticInfo", QColor(0x4A, 0x9E, 0xFF)},

        {"fillNeutralChip", QColor(150, 150, 150, 28)},
        {"fillNeutralChipBorder", QColor(150, 150, 150, 70)},
        {"fillInfoChip", QColor(74, 158, 255, 36)},
        {"fillInfoChipBorder", QColor(74, 158, 255, 115)},
    };
    return table;
}

const Overrides& neutralLightOverrides() {
    // Applied over Navy Light, which already restates the semantic hues and
    // fills for a light ground: this only takes the blue out of the surfaces,
    // borders and text, and moves the accent to a plain blue.
    static const Overrides table{
        {"desk", QColor(0xE8, 0xE8, 0xE8)},
        {"surfaceWindow", QColor(0xFF, 0xFF, 0xFF)},
        {"surfacePanel", QColor(0xF7, 0xF7, 0xF7)},
        {"surfaceToolbar", QColor(0xF3, 0xF3, 0xF3)},
        {"surfaceHeader", QColor(0xF0, 0xF0, 0xF0)},
        {"surfaceSunken", QColor(0xFF, 0xFF, 0xFF)},
        {"surfaceGutter", QColor(0xF7, 0xF7, 0xF7)},
        {"surfaceHunkHeader", QColor(0xEF, 0xEF, 0xEF)},
        {"surfaceMenubar", QColor(0xF3, 0xF3, 0xF3)},
        {"surfaceTitlebarTop", QColor(0xF5, 0xF5, 0xF5)},
        {"surfaceTitlebarBottom", QColor(0xED, 0xED, 0xED)},
        {"surfaceCheckbox", QColor(0xFF, 0xFF, 0xFF)},
        {"surfacePickDot", QColor(0xA8, 0xA8, 0xA8)},
        {"surfaceCheckerboard", QColor(0xEB, 0xEB, 0xEB)},

        {"borderWindow", QColor(0xCC, 0xCC, 0xCC)},
        {"borderHard", QColor(0xD4, 0xD4, 0xD4)},
        {"borderPanelDivider", QColor(0xE3, 0xE3, 0xE3)},
        {"borderDivider", QColor(0xD4, 0xD4, 0xD4)},
        {"borderControl", QColor(0xC8, 0xC8, 0xC8)},
        {"borderSoft", QColor(0xE8, 0xE8, 0xE8)},
        {"borderHairline", QColor(0xE6, 0xE6, 0xE6)},
        {"borderCheckbox", QColor(0xB5, 0xB5, 0xB5)},

        {"textPrimary", QColor(0x1A, 0x1A, 0x1A)},
        {"textBody", QColor(0x26, 0x26, 0x26)},
        {"textStrong", QColor(0x1A, 0x1A, 0x1A)},
        {"textDefault", QColor(0x26, 0x26, 0x26)},
        {"textSecondary", QColor(0x4A, 0x4A, 0x4A)},
        {"textMuted", QColor(0x4A, 0x4A, 0x4A)},
        {"textDim", QColor(0x59, 0x59, 0x59)},
        {"textLabel", QColor(0x63, 0x63, 0x63)},
        {"textLabelDim", QColor(0x63, 0x63, 0x63)},
        {"textFaint", QColor(0x63, 0x63, 0x63)},
        {"textDisabled", QColor(0xA8, 0xA8, 0xA8)},
        {"textSelectedPrimary", QColor(0x11, 0x11, 0x11)},
        {"textSelectedDim", QColor(0x3C, 0x3C, 0x3C)},
        {"textGutterNumber", QColor(0x6B, 0x6B, 0x6B)},
        {"textHunkHeader", QColor(0x59, 0x59, 0x59)},

        {"selectionBg", QColor(0xD7, 0xE4, 0xF5)},
        {"selectionBorder", QColor(0xB8, 0xCC, 0xE6)},
        {"selectionMarker", QColor(0x00, 0x67, 0xC0)},
        {"selectionHover", QColor(0xF0, 0xF0, 0xF0)},
        {"selectionControlBg", QColor(0xED, 0xED, 0xED)},
        {"selectionControlHover", QColor(0xE3, 0xE3, 0xE3)},
        {"selectionActiveTabBorder", QColor(0x00, 0x67, 0xC0)},

        {"accentPrimary", QColor(0x00, 0x60, 0xC0)},
        {"accentPrimaryLight", QColor(0x1A, 0x73, 0xD1)},
        {"accentButtonBg", QColor(0x00, 0x67, 0xC0)},
        {"accentButtonHover", QColor(0x1A, 0x75, 0xD2)},
        {"accentButtonFg", QColor(0xFF, 0xFF, 0xFF)},
        {"semanticInfo", QColor(0x00, 0x60, 0xC0)},
        // Nudged to 4.5:1 on the header, which is paler than Navy Light's.
        {"semanticAdd", QColor(0x18, 0x7A, 0x34)},
        {"semanticWarn", QColor(0x94, 0x5E, 0x00)},

        {"fillNeutralChip", QColor(0xEB, 0xEB, 0xEB)},
        {"fillNeutralChipBorder", QColor(0xD0, 0xD0, 0xD0)},
        {"fillInfoChip", QColor(0xE0, 0xEC, 0xFA)},
        {"fillInfoChipBorder", QColor(0xB0, 0xCD, 0xEE)},
    };
    return table;
}

const Overrides& emberSteelOverrides() {
    // Layer over Gity Dark (NeutralDark): smoked graphite surfaces and cool silver text,
    // with deep red selection fills and brighter red focus/links. Information
    // chips stay metallic so the red accent does not recolour every file status.
    static const Overrides table{
        {"desk", QColor(0x12, 0x13, 0x15)},
        {"surfaceWindow", QColor(0x20, 0x21, 0x23)},
        {"surfacePanel", QColor(0x1B, 0x1C, 0x1E)},
        {"surfaceHeader", QColor(0x2B, 0x2D, 0x30)},
        {"surfaceToolbar", QColor(0x26, 0x28, 0x2B)},
        {"surfaceMenubar", QColor(0x29, 0x2B, 0x2E)},
        {"surfaceTitlebarTop", QColor(0x37, 0x3A, 0x3E)},
        {"surfaceTitlebarBottom", QColor(0x29, 0x2B, 0x2E)},
        {"surfaceSunken", QColor(0x16, 0x17, 0x19)},
        {"surfaceGutter", QColor(0x1C, 0x1D, 0x20)},
        {"surfaceHunkHeader", QColor(0x2D, 0x2F, 0x33)},
        {"surfaceCheckbox", QColor(0x29, 0x2B, 0x2E)},
        {"surfacePickDot", QColor(0x61, 0x65, 0x6C)},
        {"surfaceCheckerboard", QColor(0x2A, 0x2C, 0x2F)},

        {"borderWindow", QColor(0x43, 0x46, 0x4C)},
        {"borderHard", QColor(0x12, 0x13, 0x15)},
        {"borderSoft", QColor(0x34, 0x36, 0x3B)},
        {"borderHairline", QColor(0x28, 0x2A, 0x2D)},
        {"borderControl", QColor(0x52, 0x56, 0x5D)},
        {"borderDivider", QColor(0x40, 0x43, 0x49)},
        {"borderPanelDivider", QColor(0x32, 0x34, 0x38)},
        {"borderCheckbox", QColor(0x6B, 0x70, 0x78)},

        {"textPrimary", QColor(0xEA, 0xED, 0xF0)},
        {"textBody", QColor(0xD8, 0xDB, 0xE0)},
        {"textStrong", QColor(0xE1, 0xE4, 0xE8)},
        {"textDefault", QColor(0xD0, 0xD3, 0xD8)},
        {"textSecondary", QColor(0xC0, 0xC5, 0xCC)},
        {"textMuted", QColor(0xB5, 0xBB, 0xC3)},
        {"textDim", QColor(0xAA, 0xB0, 0xB8)},
        {"textFaint", QColor(0xA2, 0xA7, 0xAF)},
        {"textLabel", QColor(0xAA, 0xB0, 0xB8)},
        {"textLabelDim", QColor(0xA2, 0xA7, 0xAF)},
        {"textDisabled", QColor(0x64, 0x68, 0x70)},
        {"textSelectedPrimary", QColor(0xF0, 0xF2, 0xF5)},
        {"textSelectedDim", QColor(0xCA, 0xCE, 0xD4)},
        {"textGutterNumber", QColor(0x98, 0x9F, 0xA9)},
        {"textHunkHeader", QColor(0xAA, 0xB0, 0xB8)},

        {"selectionBg", QColor(0x49, 0x2A, 0x2E)},
        {"selectionBorder", QColor(0x88, 0x40, 0x48)},
        {"selectionMarker", QColor(0xE8, 0x6B, 0x72)},
        {"selectionHover", QColor(0x32, 0x26, 0x29)},
        {"selectionControlBg", QColor(0x30, 0x32, 0x36)},
        {"selectionControlHover", QColor(0x40, 0x30, 0x34)},
        {"selectionActiveTabBorder", QColor(0xE8, 0x6B, 0x72)},

        {"accentPrimary", QColor(0xF0, 0x78, 0x7E)},
        {"accentPrimaryLight", QColor(0xF5, 0x98, 0x9D)},
        {"accentButtonBg", QColor(0x96, 0x33, 0x3D)},
        {"accentButtonHover", QColor(0xAD, 0x3C, 0x47)},
        {"accentButtonFg", QColor(0xEE, 0xF0, 0xF3)},
        {"semanticInfo", QColor(0xBF, 0xC5, 0xCD)},
        {"semanticRemove", QColor(0xE8, 0x9A, 0x90)},
        {"semanticWarnSoft", QColor(0xBE, 0xA1, 0x6E)},
        {"semanticMeta", QColor(0xD1, 0xA1, 0xD4)},
        {"fillNeutralChip", QColor(170, 176, 184, 26)},
        {"fillNeutralChipBorder", QColor(170, 176, 184, 66)},
        {"fillInfoChip", QColor(170, 176, 184, 28)},
        {"fillInfoChipBorder", QColor(170, 176, 184, 90)},
        {"fillRemoveRow", QColor(232, 154, 144, 26)},
        {"fillRemoveGutter", QColor(232, 154, 144, 18)},
        {"fillRemoveChip", QColor(232, 154, 144, 31)},
        {"fillRemoveChipBorder", QColor(232, 154, 144, 102)},
        {"fillMetaChip", QColor(209, 161, 212, 36)},
        {"fillMetaChipBorder", QColor(209, 161, 212, 107)},
    };
    return table;
}

const Overrides& crimsonSteelOverrides() {
    // Layer over Gity Dark (NeutralDark), sampled from a carbon-and-crimson reference:
    // a near-black ground (#080808-#181818), steel greys up to a silver
    // highlight (#B7B1B3), blood-red panels (#580808) and crimson (#D82828,
    // hot #FF5352). Headers carry the maroon-grey of the reference's
    // perforated band. Selection is blood red, actions crimson; information
    // stays silver so red means "you are here" or "do this", not every status.
    // Removal is salmon, not crimson, so a deleted line never reads as the
    // accent. Text is 4.5:1 or better on every surface it sits on.
    static const Overrides table{
        {"desk", QColor(0x08, 0x08, 0x08)},
        {"surfaceWindow", QColor(0x11, 0x11, 0x12)},
        {"surfacePanel", QColor(0x0D, 0x0D, 0x0E)},
        {"surfaceHeader", QColor(0x1A, 0x15, 0x15)},
        {"surfaceToolbar", QColor(0x16, 0x12, 0x13)},
        {"surfaceMenubar", QColor(0x14, 0x11, 0x12)},
        {"surfaceTitlebarTop", QColor(0x22, 0x1A, 0x1B)},
        {"surfaceTitlebarBottom", QColor(0x16, 0x12, 0x13)},
        {"surfaceSunken", QColor(0x0A, 0x0A, 0x0B)},
        {"surfaceGutter", QColor(0x0F, 0x0F, 0x10)},
        {"surfaceHunkHeader", QColor(0x22, 0x18, 0x19)},
        {"surfaceCheckbox", QColor(0x1C, 0x1C, 0x1D)},
        {"surfacePickDot", QColor(0x58, 0x58, 0x58)},
        {"surfaceCheckerboard", QColor(0x1A, 0x1A, 0x1B)},

        {"borderWindow", QColor(0x38, 0x38, 0x38)},
        {"borderHard", QColor(0x05, 0x05, 0x05)},
        {"borderSoft", QColor(0x23, 0x23, 0x23)},
        {"borderHairline", QColor(0x1A, 0x1A, 0x1A)},
        {"borderControl", QColor(0x48, 0x48, 0x48)},
        {"borderDivider", QColor(0x2C, 0x2C, 0x2C)},
        {"borderPanelDivider", QColor(0x1E, 0x1E, 0x1E)},
        {"borderCheckbox", QColor(0x68, 0x68, 0x68)},

        {"textPrimary", QColor(0xED, 0xEA, 0xEB)},
        {"textBody", QColor(0xDC, 0xD8, 0xD9)},
        {"textStrong", QColor(0xE6, 0xE2, 0xE3)},
        {"textDefault", QColor(0xCF, 0xCB, 0xCC)},
        {"textSecondary", QColor(0xBD, 0xB8, 0xB9)},
        {"textMuted", QColor(0xB0, 0xAB, 0xAC)},
        {"textDim", QColor(0x9E, 0x99, 0x9A)},
        {"textFaint", QColor(0x99, 0x94, 0x95)},
        {"textLabel", QColor(0x9C, 0x97, 0x98)},
        {"textLabelDim", QColor(0x99, 0x94, 0x95)},
        {"textDisabled", QColor(0x55, 0x51, 0x52)},
        {"textSelectedPrimary", QColor(0xFF, 0xF5, 0xF5)},
        {"textSelectedDim", QColor(0xD8, 0xC8, 0xC9)},
        {"textGutterNumber", QColor(0x8E, 0x89, 0x8A)},
        {"textHunkHeader", QColor(0xA3, 0x9D, 0x9E)},

        {"selectionBg", QColor(0x5A, 0x0C, 0x10)},
        {"selectionBorder", QColor(0x98, 0x18, 0x18)},
        {"selectionMarker", QColor(0xE8, 0x28, 0x28)},
        {"selectionHover", QColor(0x22, 0x13, 0x14)},
        {"selectionControlBg", QColor(0x1F, 0x1F, 0x20)},
        {"selectionControlHover", QColor(0x2E, 0x1A, 0x1B)},
        {"selectionActiveTabBorder", QColor(0xE8, 0x28, 0x28)},

        {"accentPrimary", QColor(0xFF, 0x53, 0x52)},
        {"accentPrimaryLight", QColor(0xFF, 0x73, 0x72)},
        {"accentButtonBg", QColor(0xB8, 0x18, 0x18)},
        {"accentButtonHover", QColor(0xD8, 0x28, 0x28)},
        {"accentButtonFg", QColor(0xFF, 0xFF, 0xFF)},
        {"semanticInfo", QColor(0xB7, 0xB1, 0xB3)},
        {"semanticRemove", QColor(0xF2, 0x8B, 0x82)},
        {"semanticMeta", QColor(0xC9, 0xA0, 0xDC)},
        {"fillNeutralChip", QColor(183, 177, 179, 24)},
        {"fillNeutralChipBorder", QColor(183, 177, 179, 64)},
        {"fillInfoChip", QColor(183, 177, 179, 28)},
        {"fillInfoChipBorder", QColor(183, 177, 179, 90)},
        {"fillRemoveRow", QColor(242, 139, 130, 26)},
        {"fillRemoveGutter", QColor(242, 139, 130, 18)},
        {"fillRemoveChip", QColor(242, 139, 130, 31)},
        {"fillRemoveChipBorder", QColor(242, 139, 130, 102)},
        {"fillMetaChip", QColor(201, 160, 220, 36)},
        {"fillMetaChipBorder", QColor(201, 160, 220, 107)},
    };
    return table;
}

Theme::Variant& currentVariant() {
    static Theme::Variant variant = Theme::Variant::Dark;
    return variant;
}

Theme::Variant& resolvedVariant() {
    static Theme::Variant variant = Theme::Variant::Dark;
    return variant;
}

int& metricsGeneration() {
    static int generation = 0;
    return generation;
}

Theme::Variant systemVariant() {
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light
               ? Theme::Variant::Light
               : Theme::Variant::Dark;
}

} // namespace

Theme::Variant Theme::variant() {
    return currentVariant();
}

Theme::Variant Theme::effectiveVariant() {
    return resolvedVariant();
}

int Theme::generation() {
    return metricsGeneration();
}

bool Theme::compact() {
    return instance().metrics.rowHeight == tokens::rowHeightDense;
}

void Theme::setCompact(bool compact, QApplication* app) {
    instance().metrics.rowHeight = compact ? tokens::rowHeightDense : tokens::rowHeight;
    ++metricsGeneration();
    QSettings(QStringLiteral("Gity"), QStringLiteral("Gity"))
        .setValue(QStringLiteral("appearance/density"),
                  compact ? QStringLiteral("compact") : QStringLiteral("comfortable"));
    if (app != nullptr) {
        for (QWidget* widget : app->allWidgets()) {
            widget->update();
        }
    }
}

void Theme::applySavedVariant(QApplication* app) {
    const QSettings settings(QStringLiteral("Gity"), QStringLiteral("Gity"));
    // Density first: it changes no colour, so it costs nothing to set before
    // the palette is built.
    instance().metrics.rowHeight =
        settings.value(QStringLiteral("appearance/density")).toString() == QLatin1String("compact")
            ? tokens::rowHeightDense
            : tokens::rowHeight;
    setVariant(variantFromName(
                   settings.value(QStringLiteral("appearance/variant"), QStringLiteral("dark"))
                       .toString()),
               app);
}

// The stored names stay as they were — "dark" and "light" are the Gity pair
// — so a saved choice means the same theme after the rename.
QString Theme::variantName(Variant variant) {
    switch (variant) {
    case Variant::Midnight:
        return QStringLiteral("midnight");
    case Variant::Light:
        return QStringLiteral("light");
    case Variant::System:
        return QStringLiteral("system");
    case Variant::NeutralDark:
        return QStringLiteral("neutral-dark");
    case Variant::NeutralLight:
        return QStringLiteral("neutral-light");
    case Variant::CrimsonSteel:
        return QStringLiteral("crimson-steel");
    case Variant::EmberSteel:
        return QStringLiteral("ember-steel");
    case Variant::Dark:
        break;
    }
    return QStringLiteral("dark");
}

QString Theme::displayName(Variant variant) {
    switch (variant) {
    case Variant::Midnight:
        return QObject::tr("Midnight");
    case Variant::Light:
        return QObject::tr("Navy Light");
    case Variant::System:
        return QObject::tr("System");
    case Variant::NeutralDark:
        return QObject::tr("Gity Dark");
    case Variant::NeutralLight:
        return QObject::tr("Gity Light");
    case Variant::CrimsonSteel:
        return QObject::tr("Crimson Steel");
    case Variant::EmberSteel:
        return QObject::tr("Ember Steel Dark");
    case Variant::Dark:
        break;
    }
    return QObject::tr("Navy Dark");
}

const std::vector<Theme::Variant>& Theme::choices() {
    static const std::vector<Variant> list{Variant::System,     Variant::NeutralDark,
                                           Variant::NeutralLight, Variant::Dark,
                                           Variant::Light,      Variant::Midnight,
                                           Variant::EmberSteel, Variant::CrimsonSteel};
    return list;
}

bool Theme::isLight(Variant variant) {
    return variant == Variant::Light || variant == Variant::NeutralLight;
}

Theme::Variant Theme::variantFromName(const QString& name) {
    if (name == QLatin1String("midnight")) {
        return Variant::Midnight;
    }
    if (name == QLatin1String("neutral-dark")) {
        return Variant::NeutralDark;
    }
    if (name == QLatin1String("neutral-light")) {
        return Variant::NeutralLight;
    }
    if (name == QLatin1String("crimson-steel")) {
        return Variant::CrimsonSteel;
    }
    if (name == QLatin1String("ember-steel")) {
        return Variant::EmberSteel;
    }
    if (name == QLatin1String("light")) {
        return Variant::Light;
    }
    if (name == QLatin1String("system")) {
        return Variant::System;
    }
    return Variant::Dark;
}

void Theme::setVariant(Variant chosen, QApplication* app) {
    currentVariant() = chosen;
    // System follows the desktop, now and whenever it changes: the connection
    // is made once and re-applies whatever is chosen at the time, so choosing
    // a fixed theme later simply makes it a no-op.
    if (chosen == Variant::System && app != nullptr) {
        static bool following = false;
        if (!following) {
            following = true;
            QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, app,
                             [app] {
                                 if (currentVariant() == Variant::System) {
                                     setVariant(Variant::System, app);
                                 }
                             });
        }
    }
    const Variant variant = chosen == Variant::System ? systemVariant() : chosen;
    resolvedVariant() = variant;

    // Always from the authored values, never from whatever the last variant
    // left behind: an override that one variant sets and another does not
    // would otherwise persist across a switch.
    for (const auto& [name, colour] : tokens::defaults()) {
        for (const auto& [token, target] : tokens::byName()) {
            if (std::strcmp(name, token) == 0) {
                *target = colour;
                break;
            }
        }
    }

    // In order, each over the last: Gity Light (NeutralLight) is Navy Light
    // with the tint taken out, so it inherits the light semantic colours rather than
    // restating them.
    std::vector<const Overrides*> layers;
    if (variant == Variant::Midnight) {
        layers.push_back(&midnightOverrides());
    } else if (variant == Variant::Light) {
        layers.push_back(&lightOverrides());
    } else if (variant == Variant::NeutralDark) {
        layers.push_back(&neutralDarkOverrides());
    } else if (variant == Variant::NeutralLight) {
        layers.push_back(&lightOverrides());
        layers.push_back(&neutralLightOverrides());
    } else if (variant == Variant::CrimsonSteel) {
        layers.push_back(&neutralDarkOverrides());
        layers.push_back(&crimsonSteelOverrides());
    } else if (variant == Variant::EmberSteel) {
        layers.push_back(&neutralDarkOverrides());
        layers.push_back(&emberSteelOverrides());
    }
    for (const Overrides* overrides : layers) {
        for (const auto& [name, colour] : *overrides) {
            for (const auto& [token, target] : tokens::byName()) {
                if (std::strcmp(name, token) == 0) {
                    *target = colour;
                    break;
                }
            }
        }
    }

    QSettings(QStringLiteral("Gity"), QStringLiteral("Gity"))
        .setValue(QStringLiteral("appearance/variant"), variantName(chosen));

    if (app == nullptr) {
        return;
    }
    applyPalette(app);
    // The hand-painted views read tokens at paint time, so they only need to
    // be told to paint again.
    for (QWidget* widget : app->allWidgets()) {
        widget->update();
    }
}

QString Theme::resolveStyleSheet(const QString& sheet, QStringList* unknownTokens) {
    QString resolved;
    resolved.reserve(sheet.size());

    qsizetype at = 0;
    while (at < sheet.size()) {
        const qsizetype open = sheet.indexOf(QChar('@'), at);
        if (open < 0) {
            resolved += sheet.mid(at);
            break;
        }
        const qsizetype close = sheet.indexOf(QChar('@'), open + 1);
        if (close < 0) {
            // An unpaired @ is text, not a placeholder.
            resolved += sheet.mid(at);
            break;
        }

        resolved += sheet.mid(at, open - at);
        const QString name = sheet.mid(open + 1, close - open - 1);

        const QColor* found = nullptr;
        for (const auto& [token, colour] : tokens::byName()) {
            if (name == QLatin1String(token)) {
                found = colour;
                break;
            }
        }
        if (found != nullptr) {
            // rgba(), not name(): name() drops alpha, and the tint fills are
            // defined at low alpha, and name() would drop it silently.
            resolved += QStringLiteral("rgba(%1,%2,%3,%4)")
                            .arg(found->red())
                            .arg(found->green())
                            .arg(found->blue())
                            .arg(found->alpha());
        } else {
            if (unknownTokens != nullptr) {
                *unknownTokens << name;
            }
            // Left as written so it is visible in the sheet rather than
            // silently becoming nothing.
            resolved += sheet.mid(open, close - open + 1);
        }
        at = close + 1;
    }
    return resolved;
}

void Theme::applyStyleSheet(QApplication* app) {
    if (app == nullptr) {
        return;
    }
    // gity_ui is a static library, and a linker drops an object nobody
    // references — including the one holding the compiled stylesheet. Without
    // this the resource simply is not there, `:/gity/theme/gity.qss` fails to
    // open, and the application runs unstyled with no error at all. It looked
    // exactly like success until the failure path was tested.
    initialiseThemeResources();

    QFile file(QStringLiteral(":/gity/theme/gity.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Compiled in, so this cannot happen in a working build — and if it
        // ever does, an unstyled application is better than a dead one.
        qWarning("gity: theme stylesheet missing from resources");
        return;
    }

    QStringList unknown;
    const QString sheet =
        resolveStyleSheet(QString::fromUtf8(file.readAll()), &unknown);
    if (!unknown.isEmpty()) {
        // Named rather than swallowed: a token that does not exist is a typo
        // in the sheet, and finding it by noticing a wrong colour is far worse
        // than being told.
        unknown.removeDuplicates();
        qWarning("gity: stylesheet names unknown tokens: %s",
                 qPrintable(unknown.join(QStringLiteral(", "))));
    }
    app->setStyleSheet(sheet);
}

void Theme::refresh() {
    Theme& theme = instance();
    theme.build(paletteIsDark(QApplication::palette()));
    g_initialized = true;
}

void Theme::build(bool dark) {
    dark_ = dark;

    // Every value below comes from GityDesign/TOKENS.json via Tokens.h. The
    // design is dark-only for now; a light theme is explicitly out of scope in
    // the handoff, so the light branch mirrors dark rather than inventing a
    // palette the designer has not specified.
    background = tokens::surfaceWindow;
    // The spec has no alternating rows — state is carried by hover and
    // selection alone — so this matches the window surface.
    rowAlternate = tokens::surfaceWindow;
    selection = tokens::selectionBg;
    selectionText = tokens::textSelectedPrimary;
    hover = tokens::selectionHover;
    text = tokens::textDefault;
    textMuted = tokens::textMuted;
    textFaint = tokens::textFaint;
    separator = tokens::borderSoft;

    diffAddBg = tokens::fillAddRow;
    diffDelBg = tokens::fillRemoveRow;
    diffAddMarker = tokens::semanticAdd;
    diffDelMarker = tokens::semanticRemove;
    gutterBg = tokens::surfaceGutter;
    // Confirmed by the designer and added to TOKENS.json; these were nearest
    // tokens standing in until then.
    gutterText = tokens::textGutterNumber;
    hunkHeaderBg = tokens::surfaceHunkHeader;
    hunkHeaderText = tokens::textHunkHeader;
}

QColor Theme::laneColor(int lane) const {
    // Light has its own lanes: the dark ones are tuned to glow on near-black
    // and measured 1.4–2.7:1 on the light window. These keep each hue — so a
    // lane stays recognisably the same colour across themes — darkened until
    // it reaches 4.5:1 (2026-09 UI review).
    static const std::array<QColor, 11> lightLanes{
        QColor(0x26, 0x74, 0xB9), QColor(0x9D, 0x66, 0x16), QColor(0x3C, 0x80, 0x48),
        QColor(0xA5, 0x51, 0xA8), QColor(0xC7, 0x45, 0x45), QColor(0x2F, 0x7C, 0x81),
        QColor(0x63, 0x63, 0xDF), QColor(0x6C, 0x78, 0x19), QColor(0xC2, 0x28, 0xC2),
        QColor(0x1B, 0x83, 0x1B), QColor(0x8A, 0x6C, 0x41)};
    static_assert(lightLanes.size() == tokens::graphLanes.size());
    if (isLight(effectiveVariant())) {
        return lightLanes[static_cast<std::size_t>(lane) % lightLanes.size()];
    }
    return tokens::laneColor(lane);
}

} // namespace gity::ui
