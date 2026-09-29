// ADR-005 — streaming lane assignment.
#pragma once

#include "GraphRow.h"

#include <span>

namespace gity::graph {

/// Incremental lane assignment over a topologically ordered walk.
///
/// Lanes are slots; each occupied slot holds the key of the commit it is
/// waiting for. For each commit, in walk order:
///
///   1. The leftmost slot awaiting this commit is its lane.
///   2. Any other matching slots close — their edges converge into that lane.
///   3. If no slot matched, take the leftmost free one: this is a branch tip.
///   4. The lane goes on to await the first parent; additional parents open
///      new lanes.
///
/// The assigner holds the slot vector between calls, so rows can be appended
/// as the walk produces them rather than after it completes. Cost is
/// O(active_lanes) per commit, and active lanes stay small — 14 at most on the
/// real 13k-commit repository M0 measured.
class LaneAssigner {
public:
    /// Appends one row to `out`. Parent keys must be in first-parent order.
    void append(CommitKey key, std::span<const CommitKey> parents, Graph& out);

    /// Discards slot state so the next append starts a fresh graph.
    void reset() noexcept { slots_.clear(); }

    /// Slots currently live — the graph's width at this point in the walk.
    [[nodiscard]] std::size_t activeLanes() const noexcept { return slots_.size(); }

    /// Convenience for a complete table: assigns every row in one pass.
    [[nodiscard]] static Graph assign(const CommitTable& commits);

private:
    struct Slot {
        CommitKey key = 0;
        bool occupied = false; ///< A lane exists in this column.
        bool awaiting = false; ///< ...and it is waiting for `key`.
                               ///< False only for the row that just claimed a
                               ///< free slot as a branch tip: it occupies the
                               ///< column but nothing arrives into it.
    };

    std::uint16_t takeFreeSlot();

    std::vector<Slot> slots_;
};

} // namespace gity::graph
