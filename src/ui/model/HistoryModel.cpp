#include "HistoryModel.h"

#include <git2.h>

#include "core/git/HistoryStream.h"

#include <QDateTime>

#include <algorithm>

namespace gity::ui {

namespace {

/// Refs are keyed by the same OID prefix the lane assigner uses, so the graph
/// and the sidebar cannot disagree about which commit a branch points at.
std::uint64_t keyOf(const git_oid& oid) {
    return gity::git::keyFromOid(oid);
}

} // namespace

void HistoryModel::setRefs(const session::RefSetPtr& refs) {
    refsByCommit_.clear();
    if (!refs) {
        return;
    }

    const auto add = [this](const std::vector<git::RefEntry>& entries) {
        for (const auto& entry : entries) {
            RefLabel label;
            label.text = QString::fromStdString(entry.name);
            label.kind = entry.kind;
            label.isHead = entry.isHead;
            refsByCommit_[keyOf(entry.target)].push_back(std::move(label));
        }
    };
    add(refs->localBranches);
    add(refs->remoteBranches);
    add(refs->tags);

    // HEAD first, then locals, then the rest: the chip a person looks for
    // should not move as branches are added around it.
    for (auto& [key, labels] : refsByCommit_) {
        std::stable_sort(labels.begin(), labels.end(),
                         [](const RefLabel& a, const RefLabel& b) {
                             if (a.isHead != b.isHead) {
                                 return a.isHead;
                             }
                             return static_cast<int>(a.kind) < static_cast<int>(b.kind);
                         });
    }
}

const std::vector<RefLabel>* HistoryModel::refsFor(std::size_t globalRow) const {
    if (refsByCommit_.empty()) {
        return nullptr;
    }
    const RowView view = at(globalRow);
    if (!view.valid()) {
        return nullptr;
    }
    const auto found = refsByCommit_.find(
        keyOf(view.chunk->commits.oid(static_cast<model::CommitIndex>(view.localRow))));
    return found == refsByCommit_.end() ? nullptr : &found->second;
}

void HistoryModel::clear() {
    chunks_.clear();
    firstRow_.clear();
    rowCount_ = 0;
    maxLanes_ = 1;
    bytes_ = 0;
    refsByCommit_.clear();
}

void HistoryModel::append(session::ChunkPtr chunk) {
    if (!chunk || chunk->rowCount() == 0) {
        return;
    }
    firstRow_.push_back(rowCount_);
    rowCount_ += chunk->rowCount();
    bytes_ += chunk->bytes();
    for (const auto& row : chunk->graph.rows) {
        maxLanes_ = std::max(maxLanes_, row.laneCount);
    }
    chunks_.push_back(std::move(chunk));
}

RowView HistoryModel::at(std::size_t globalRow) const {
    if (globalRow >= rowCount_) {
        return {};
    }
    // Last chunk whose first row is <= globalRow.
    const auto it = std::upper_bound(firstRow_.begin(), firstRow_.end(), globalRow);
    const auto index = static_cast<std::size_t>(std::distance(firstRow_.begin(), it) - 1);
    return RowView{chunks_[index].get(), globalRow - firstRow_[index]};
}

QString HistoryModel::summary(std::size_t globalRow) const {
    const RowView view = at(globalRow);
    if (!view.valid()) {
        return {};
    }
    const auto text = view.chunk->commits.summary(static_cast<model::CommitIndex>(view.localRow));
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString HistoryModel::author(std::size_t globalRow) const {
    const RowView view = at(globalRow);
    if (!view.valid()) {
        return {};
    }
    const auto text = view.chunk->commits.author(static_cast<model::CommitIndex>(view.localRow));
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString HistoryModel::shortId(std::size_t globalRow) const {
    const RowView view = at(globalRow);
    if (!view.valid()) {
        return {};
    }
    return QString::fromStdString(
        view.chunk->commits.shortId(static_cast<model::CommitIndex>(view.localRow)));
}

QString HistoryModel::fullId(std::size_t globalRow) const {
    const RowView view = at(globalRow);
    if (!view.valid()) {
        return {};
    }
    return QString::fromStdString(
        view.chunk->commits.shortId(static_cast<model::CommitIndex>(view.localRow),
                                    GIT_OID_SHA1_HEXSIZE));
}

QString HistoryModel::when(std::size_t globalRow) const {
    const RowView view = at(globalRow);
    if (!view.valid()) {
        return {};
    }
    const auto seconds = view.chunk->commits.when(static_cast<model::CommitIndex>(view.localRow));
    const QDateTime when = QDateTime::fromSecsSinceEpoch(seconds);

    // The Date column is 108px in the spec, and a full ISO timestamp does not
    // fit it. Precision is spent where it is actually read: the time for
    // today's commits, the day for this year's, the year for anything older.
    const QDate today = QDate::currentDate();
    if (when.date() == today) {
        return when.toString(QStringLiteral("HH:mm"));
    }
    if (when.date().year() == today.year()) {
        return when.toString(QStringLiteral("MMM d  HH:mm"));
    }
    return when.toString(QStringLiteral("yyyy-MM-dd"));
}

qsizetype HistoryModel::rowForId(const QString& oidHex, std::size_t fromRow) const {
    git_oid wanted{};
    if (oidHex.isEmpty() ||
        git_oid_fromstr(&wanted, oidHex.toLatin1().constData()) != 0) {
        return -1;
    }

    for (std::size_t chunkIndex = 0; chunkIndex < chunks_.size(); ++chunkIndex) {
        const auto& chunk = chunks_[chunkIndex];
        if (!chunk) {
            continue;
        }
        const std::size_t count = chunk->commits.size();
        // Chunks wholly before `fromRow` are skipped: a caller looking for a
        // commit as the history streams in only needs the chunk just added.
        if (firstRow_[chunkIndex] + count <= fromRow) {
            continue;
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (git_oid_equal(&chunk->commits.oid(static_cast<model::CommitIndex>(i)),
                              &wanted) != 0) {
                return static_cast<qsizetype>(firstRow_[chunkIndex] + i);
            }
        }
    }
    return -1;
}

} // namespace gity::ui
