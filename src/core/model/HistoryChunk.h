// ADR-004 — the unit that crosses the thread boundary.
//
// Each chunk is self-contained and immutable once emitted: its rows, its edge
// arena, and the metadata for exactly those rows. Nothing derived from libgit2
// travels with it, so the UI thread never touches a git_repository.
#pragma once

#include "../graph/GraphRow.h"
#include "CommitStore.h"

#include <cstddef>

namespace gity::model {

struct HistoryChunk {
    /// Global row index of this chunk's first row.
    std::size_t firstRow = 0;

    /// Rows and edges. `GraphRow::edgeOffset` and `commitIndex` are *local* to
    /// this chunk; add `firstRow` for a global row number.
    graph::Graph graph;

    /// Metadata for this chunk's rows, in the same order.
    CommitStore commits;

    [[nodiscard]] std::size_t rowCount() const noexcept { return graph.rows.size(); }

    [[nodiscard]] std::size_t bytes() const noexcept {
        return graph.bytes() + commits.bytes();
    }
};

} // namespace gity::model
