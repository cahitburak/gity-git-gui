// One conflicted file as the conflict editor's columns see it.
//
// The columns — ours, the result, theirs — are painted side by side or one
// above another, and a region has to start on the same row in every column
// beside it, or the eye loses which lines answer which. So the document lays
// out rows: each region takes as many as the longest of its columns, and a
// column with fewer lines there fills the rest.
#pragma once

#include "core/git/Merge3.h"
#include "session/SessionTypes.h"

#include <QObject>
#include <QString>

#include <vector>

namespace gity::ui {

class ConflictDocument : public QObject {
    Q_OBJECT

public:
    enum class Column { Ours, Result, Theirs };

    explicit ConflictDocument(QObject* parent = nullptr);

    void setFile(session::ConflictPtr file, git::MergeResolution resolution);
    void clear();

    [[nodiscard]] const session::ConflictPtr& file() const noexcept { return file_; }

    [[nodiscard]] const git::MergeResolution& resolution() const noexcept { return resolution_; }

    [[nodiscard]] std::size_t regionCount() const noexcept { return resolution_.regionCount(); }

    [[nodiscard]] const git::MergeRegion& region(std::size_t i) const {
        return resolution_.region(i);
    }

    [[nodiscard]] git::Pick pick(std::size_t i) const { return resolution_.pick(i); }

    void setPick(std::size_t region, git::Pick pick);
    /// `text` as typed: lines split on '\n', each given the file's ending.
    void setEdited(std::size_t region, const QString& text);
    void pickAllConflicts(git::Pick pick);
    void reset();

    /// Changed since it was loaded or last saved.
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

    void markClean() { dirty_ = false; }

    /// Choices brought back from before were never written.
    void markDirty() {
        dirty_ = true;
        emit changed();
    }

    [[nodiscard]] int rowCount() const noexcept { return rowCount_; }

    struct Place {
        std::size_t region = 0;
        int offset = 0; ///< Row within the region.
    };

    [[nodiscard]] Place placeAt(int row) const;

    [[nodiscard]] int regionRow(std::size_t region) const { return regionRow_[region]; }

    [[nodiscard]] int regionRows(std::size_t region) const { return regionRows_[region]; }

    /// Rows at the top of a region before its lines: a conflict has one, for
    /// its buttons and what has been chosen, so they never cover a line.
    [[nodiscard]] int headerRows(std::size_t region) const {
        return resolution_.region(region).kind == git::MergeRegion::Kind::Conflict ? 1 : 0;
    }

    /// Which conflict, from 1, `region` is; 0 for one that is not.
    [[nodiscard]] int conflictNumber(std::size_t region) const;

    /// A column's lines in a region, with their endings.
    [[nodiscard]] const git::Lines& lines(Column column, std::size_t region) const;
    /// The number, from 1, of a region's first line in that column's file.
    [[nodiscard]] int firstLine(Column column, std::size_t region) const;

    /// The most lines any column's file has: how wide line numbers get.
    [[nodiscard]] int longestFile() const noexcept { return longestFile_; }

    /// The regions that are conflicts, in order.
    [[nodiscard]] const std::vector<std::size_t>& conflicts() const noexcept { return conflicts_; }

signals:
    /// Content or rows changed; repaint, and the rows may have moved.
    void changed();

private:
    void rebuild();

    session::ConflictPtr file_;
    git::MergeResolution resolution_;
    std::vector<git::Lines> results_;
    std::vector<int> regionRow_;
    std::vector<int> regionRows_;
    std::vector<int> firstOurs_;
    std::vector<int> firstTheirs_;
    std::vector<int> firstResult_;
    std::vector<std::size_t> conflicts_;
    int rowCount_ = 0;
    int longestFile_ = 0;
    bool dirty_ = false;
};

} // namespace gity::ui
