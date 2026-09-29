// ADR-005 — row records for the commit graph.
//
// A row must be paintable without consulting its neighbours, because the view
// is virtualized and may start painting at row 700,000. Everything a painter
// needs for one band lives in the row plus its slice of the edge arena.
//
// Commits are identified by CommitKey rather than by row index. That is what
// lets lane assignment run *streaming*: a commit's parents appear later in the
// walk, so their row indices are not known when the commit is processed, but
// their keys are. M0 measured a cold 1M-commit walk at ~4 s, so waiting for a
// complete walk before painting is not an option.
#pragma once

#include <cstdint>
#include <vector>

namespace gity::graph {

/// Identity of a commit, independent of its position in the walk. In
/// production this is the first 8 bytes of the OID; tests use small integers.
using CommitKey = std::uint64_t;

using CommitIndex = std::uint32_t;

inline constexpr std::uint16_t kNoLane = 0xFFFFu;

enum RowFlags : std::uint16_t {
    RowFlag_None = 0,
    RowFlag_Tip = 1u << 0,   ///< No lane awaited this commit; it starts a branch.
    RowFlag_Merge = 1u << 1, ///< More than one parent.
    RowFlag_Root = 1u << 2,  ///< No parents.
};

enum class EdgeKind : std::uint8_t {
    PassThrough, ///< Lane crosses this row untouched: from == to.
    In,          ///< Line arrives from lane `from` above and enters the dot at `to`.
    Out,         ///< Line leaves the dot at `from` toward lane `to` below.
};

struct GraphEdge {
    std::uint16_t from;
    std::uint16_t to;
    EdgeKind kind;
};

struct GraphRow {
    CommitIndex commitIndex;  ///< Row position, and index into the commit store.
    std::uint32_t edgeOffset; ///< Into the owning Graph's edge vector.
    std::uint16_t lane;
    std::uint16_t laneCount;  ///< Columns this row's band occupies.
    std::uint16_t edgeCount;
    std::uint16_t flags;
};

static_assert(sizeof(GraphRow) <= 24, "GraphRow exceeds the ADR-005 memory budget");

/// Parent lists in CSR form, keyed rather than indexed. Parents outside the
/// walk are simply never matched by a slot, which is the correct behaviour for
/// shallow clones and filtered views alike.
struct CommitTable {
    std::vector<CommitKey> key;              ///< one per row, in walk order
    std::vector<std::uint32_t> parentOffset; ///< size n + 1
    std::vector<CommitKey> parentKey;        ///< flattened

    [[nodiscard]] std::size_t size() const noexcept {
        return parentOffset.empty() ? 0 : parentOffset.size() - 1;
    }
};

struct Graph {
    std::vector<GraphRow> rows;
    std::vector<GraphEdge> edges;

    [[nodiscard]] std::size_t bytes() const noexcept {
        return rows.size() * sizeof(GraphRow) + edges.size() * sizeof(GraphEdge);
    }

    void clear() {
        rows.clear();
        edges.clear();
    }
};

} // namespace gity::graph
