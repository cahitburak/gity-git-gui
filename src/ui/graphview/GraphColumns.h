// SPEC.md § history — the column geometry, in one place.
//
// The header widget and the row painter must agree exactly or the labels sit
// over the wrong columns. That is the same class of bug as the 1px row-pitch
// trap: two places computing what should be one number.
#pragma once

#include "ui/theme/Tokens.h"

#include <algorithm>

namespace gity::ui {

/// Left-to-right: Graph · Description, Message, Author, Date, Commit.
/// Everything but Message is fixed and non-shrinking.
struct GraphColumns {
    int graphWidth = tokens::columnsGraphDescription;
    int messageWidth = tokens::columnsMessageMin;
    int authorWidth = tokens::columnsAuthor;
    int dateWidth = tokens::columnsDate;
    int commitWidth = tokens::columnsCommit;

    [[nodiscard]] int graphX() const noexcept { return 0; }
    [[nodiscard]] int messageX() const noexcept { return graphWidth; }
    [[nodiscard]] int authorX() const noexcept { return messageX() + messageWidth; }
    [[nodiscard]] int dateX() const noexcept { return authorX() + authorWidth; }
    [[nodiscard]] int commitX() const noexcept { return dateX() + dateWidth; }
    [[nodiscard]] int totalWidth() const noexcept { return commitX() + commitWidth; }

    /// Message takes the slack; a secondary column at width 0 is not shown.
    [[nodiscard]] static GraphColumns forWidth(int viewportWidth, int maxLanes) {
        GraphColumns columns;

        // The graph column must be wide enough that ref chips clear the widest
        // lane — SPEC.md gives the formula exactly.
        const int lanesWidth = tokens::firstLaneX + maxLanes * tokens::lanePitch +
                               tokens::textGutterAfterLastLane;
        // Sized to the lanes, not to a 400px design column: ref chips now sit
        // inline before the subject, so the subject starts right after the
        // graph (2026-09 UI review, Refined Classic).
        columns.graphWidth = lanesWidth;

        // On a narrow window the secondary columns give way, least useful
        // first — the SHA, then the date, then the author — so the message
        // keeps its minimum rather than the view scrolling sideways
        // (2026-09 UI review: 1024px workspaces).
        const auto fixed = [&columns] {
            return columns.graphWidth + columns.authorWidth + columns.dateWidth +
                   columns.commitWidth;
        };
        for (int* column : {&columns.commitWidth, &columns.dateWidth, &columns.authorWidth}) {
            if (viewportWidth - fixed() >= tokens::columnsMessageMin) {
                break;
            }
            *column = 0;
        }
        columns.messageWidth = std::max(tokens::columnsMessageMin, viewportWidth - fixed());
        return columns;
    }
};

} // namespace gity::ui
