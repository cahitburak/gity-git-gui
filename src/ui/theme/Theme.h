// ADR-002 consequence — Qt gives us a widget set, not a design system.
//
// The risk register says to build this first, not last, because it is the main
// reason Widgets applications look dated and it is expensive to retrofit. Every
// colour and metric in the UI comes from here; no literal colours in views.
#pragma once

#include "Tokens.h"

#include <QColor>
#include <QFont>
#include <QPalette>

#include <vector>

namespace gity::ui {

/// Geometry comes from the design tokens, not from the font.
///
/// SPEC.md is explicit that row pitch is the single source of truth for graph
/// geometry: deriving it from font metrics — which is what this used to do —
/// makes the lane pitch drift with the user's font size and breaks the
/// alignment the whole graph depends on.
struct Metrics {
    int rowHeight = tokens::rowHeight;
    int laneWidth = tokens::lanePitch;
    int laneOriginX = tokens::firstLaneX;
    int gutterPadding = tokens::textGutterAfterLastLane;
    int columnGap = 12;
    int shortIdChars = 8;
};

class Theme {
public:
    static const Theme& current();

    /// Re-reads the application palette. Call on palette change.
    static void refresh();

    /// The themes a user can choose between.
    ///
    /// Variants are overrides on top of the authored tokens rather than whole
    /// palettes: a variant states the handful of surfaces and text colours it
    /// changes, and everything the design already got right — the semantic
    /// hues, the lane colours, the chips — is inherited rather than restated
    /// and left to drift.
    enum class Variant {
        Dark,     ///< "Navy Dark": the design as authored in TOKENS.json.
        Midnight, ///< Deeper surfaces for a dark room.
        Light,    ///< "Navy Light".
        /// Navy Dark or Navy Light, following the desktop's own setting and
        /// changing with it.
        System,
        /// "Gity Dark" and "Gity Light": neutral greys and a standard blue —
        /// the Navy pair without its slate tint. Same layout, same semantic
        /// colours. (Named Neutral in code, from before the rename.)
        NeutralDark,
        NeutralLight,
        /// "Ember Steel Dark": smoked graphite, metallic grey text, and red accents.
        EmberSteel,
        /// "Crimson Steel": near-black carbon, brushed-steel greys and silver
        /// text, blood-red selections and crimson actions — darker and redder
        /// than Ember Steel.
        CrimsonSteel,
    };

    /// The variant actually in force: System resolved to Dark or Light.
    [[nodiscard]] static Variant effectiveVariant();
    /// Whether `variant` is one of the light ones — for colours tuned by
    /// hand per ground, like the graph lanes.
    [[nodiscard]] static bool isLight(Variant variant);
    /// What a person calls it: "Gity Dark", "Navy Light", …
    /// Only this changed in the 2026-09 rename; the stored names did not, so
    /// a saved choice keeps its look.
    [[nodiscard]] static QString displayName(Variant variant);
    /// Every theme, in the order a chooser lists them: System, the Gity pair,
    /// the Navy pair, then the tinted ones. One list, so the Settings page and
    /// View ▸ Theme cannot disagree.
    [[nodiscard]] static const std::vector<Variant>& choices();

    /// Row height for the history: Comfortable is the design's 26px, Compact
    /// its dense 22px. Applied at once and remembered.
    static void setCompact(bool compact, QApplication* app);
    [[nodiscard]] static bool compact();
    /// Bumped whenever metrics change, so painted views know to re-lay out.
    [[nodiscard]] static int generation();

    [[nodiscard]] static Variant variant();
    /// Applies `variant` and repaints. Persisted, so it survives a restart.
    static void setVariant(Variant variant, QApplication* app);

    /// Applies whichever variant the user last chose. Both programs call it —
    /// the credential prompt belongs to the same application and should not
    /// open in a different theme from the window that caused it.
    static void applySavedVariant(QApplication* app);

    [[nodiscard]] static QString variantName(Variant variant);
    [[nodiscard]] static Variant variantFromName(const QString& name);

    /// Puts the design tokens into the application palette.
    ///
    /// The hot views paint themselves and the chrome carries its own
    /// stylesheets, so nothing on the five main screens needs this. Ordinary
    /// dialogs do: a QDialog full of QLineEdits inherits whatever palette the
    /// desktop supplies, which is how a fixed-dark application ends up opening
    /// a bright white dialog on top of itself.
    static void applyPalette(QApplication* app);

    /// Loads the application stylesheet and applies it.
    ///
    /// Kept apart from applyPalette because they answer different needs: the
    /// palette is what an unstyled widget falls back to, the sheet is what the
    /// chrome actually looks like. Called by applyPalette, so ordinary callers
    /// need only that one.
    static void applyStyleSheet(QApplication* app);

    /// The sheet with its @token@ placeholders substituted. Exposed for
    /// testing the substitution without a running application.
    [[nodiscard]] static QString resolveStyleSheet(const QString& sheet,
                                                   QStringList* unknownTokens = nullptr);

    [[nodiscard]] bool isDark() const noexcept { return dark_; }

    QColor background;
    QColor rowAlternate;
    QColor selection;
    QColor selectionText;
    QColor hover;
    QColor text;
    QColor textMuted;
    QColor textFaint;
    QColor separator;

    /// Diff surfaces. Backgrounds are deliberately low-saturation: the eye
    /// should find the changed *lines*, and a vivid wash makes the text on top
    /// of them harder to read, which is the opposite of the point.
    QColor diffAddBg;
    QColor diffDelBg;
    QColor diffAddMarker;
    QColor diffDelMarker;
    QColor gutterBg;
    QColor gutterText;
    QColor hunkHeaderBg;
    QColor hunkHeaderText;

    /// Lane colours, cycled by lane index. Chosen to stay distinguishable on
    /// both grounds rather than to be maximally saturated.
    [[nodiscard]] QColor laneColor(int lane) const;

    Metrics metrics;

private:
    Theme() = default;
    static Theme& instance();
    void build(bool dark);

    bool dark_ = false;
};

} // namespace gity::ui
