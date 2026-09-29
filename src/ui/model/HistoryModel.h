// Main-thread accumulator for the chunks the session emits.
//
// Chunks arrive append-only and are never mutated, so the model just keeps
// them and maps a global row to (chunk, local row) with a binary search over
// cumulative offsets. At 2048 rows per chunk that is a handful of chunks for
// a typical repository and a couple of hundred for a very large one.
#pragma once

#include "core/model/HistoryChunk.h"
#include "session/RepoSession.h"

#include <QString>

#include <unordered_map>
#include <vector>

namespace gity::ui {

/// A ref pointing at a commit, ready to paint as a chip.
struct RefLabel {
    QString text;
    git::RefKind kind = git::RefKind::LocalBranch;
    bool isHead = false; ///< The checked-out branch: chip is warn-coloured.
};

struct RowView {
    const model::HistoryChunk* chunk = nullptr;
    std::size_t localRow = 0;

    [[nodiscard]] bool valid() const noexcept { return chunk != nullptr; }
    [[nodiscard]] const graph::GraphRow& row() const { return chunk->graph.rows[localRow]; }
};

class HistoryModel {
public:
    void clear();
    void append(session::ChunkPtr chunk);

    /// Indexes refs by the commit they point at, so the graph can paint chips
    /// without a lookup per painted row.
    void setRefs(const session::RefSetPtr& refs);

    /// Refs at this row, or nullptr. Most rows have none, so returning a
    /// pointer avoids constructing an empty vector per painted row.
    [[nodiscard]] const std::vector<RefLabel>* refsFor(std::size_t globalRow) const;

    [[nodiscard]] std::size_t rowCount() const noexcept { return rowCount_; }
    [[nodiscard]] std::uint16_t maxLanes() const noexcept { return maxLanes_; }

    [[nodiscard]] RowView at(std::size_t globalRow) const;

    [[nodiscard]] QString summary(std::size_t globalRow) const;
    [[nodiscard]] QString author(std::size_t globalRow) const;
    [[nodiscard]] QString shortId(std::size_t globalRow) const;
    [[nodiscard]] QString fullId(std::size_t globalRow) const;
    [[nodiscard]] QString when(std::size_t globalRow) const;

    /// The row holding `oidHex`, or -1. Compares raw oid bytes rather than
    /// formatting each row's id, so selecting a branch does not allocate a
    /// string per commit in the history.
    [[nodiscard]] qsizetype rowForId(const QString& oidHex, std::size_t fromRow = 0) const;

    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }

private:
    std::vector<session::ChunkPtr> chunks_;
    std::vector<std::size_t> firstRow_; ///< chunks_[i] starts at firstRow_[i]
    std::size_t rowCount_ = 0;
    std::uint16_t maxLanes_ = 1;
    std::size_t bytes_ = 0;
    std::unordered_map<std::uint64_t, std::vector<RefLabel>> refsByCommit_;
};

} // namespace gity::ui
