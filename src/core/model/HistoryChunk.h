// ADR-004 — the unit that crosses the thread boundary.
//
// Each chunk is self-contained and immutable once emitted: its rows, its edge
// arena, and the metadata for exactly those rows. Nothing derived from libgit2
// travels with it, so the UI thread never touches a git_repository.
#pragma once

#include "../graph/GraphRow.h"
#include "CommitStore.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace gity::model {

struct HistoryChunk {
    /// Global row index of this chunk's first row.
    std::size_t firstRow = 0;

    /// Rows and edges. `GraphRow::edgeOffset` and `commitIndex` are *local* to
    /// this chunk; add `firstRow` for a global row number.
    graph::Graph graph;

    /// Metadata for this chunk's rows, in the same order.
    CommitStore commits;

    /// For the history of one file that was renamed: the rows where the file
    /// had another name, by local row, with that name. Rows not listed had
    /// the name asked for. In row order.
    std::vector<std::pair<CommitIndex, std::string>> renamedPaths;

    [[nodiscard]] std::size_t rowCount() const noexcept { return graph.rows.size(); }

    [[nodiscard]] std::size_t bytes() const noexcept { return graph.bytes() + commits.bytes(); }
};

} // namespace gity::model
