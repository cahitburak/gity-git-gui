#include "ConflictDocument.h"

#include <algorithm>

namespace gity::ui {

ConflictDocument::ConflictDocument(QObject* parent) : QObject(parent) {}

void ConflictDocument::setFile(session::ConflictPtr file, git::MergeResolution resolution) {
    file_ = std::move(file);
    resolution_ = std::move(resolution);
    dirty_ = false;
    conflicts_.clear();
    for (std::size_t i = 0; i < resolution_.regionCount(); ++i) {
        if (resolution_.region(i).kind == git::MergeRegion::Kind::Conflict) {
            conflicts_.push_back(i);
        }
    }
    rebuild();
}

void ConflictDocument::clear() {
    setFile(nullptr, git::MergeResolution{});
}

void ConflictDocument::setPick(std::size_t region, git::Pick pick) {
    if (resolution_.pick(region) == pick) {
        return;
    }
    resolution_.setPick(region, pick);
    dirty_ = true;
    rebuild();
}

void ConflictDocument::setEdited(std::size_t region, const QString& text) {
    // What was typed has '\n' between lines; the file's own ending goes on
    // each. The file's last line keeps having none if it had none.
    const std::string& eol = resolution_.eol();
    std::string bytes;
    const QStringList typed = text.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < typed.size(); ++i) {
        bytes += typed[i].toStdString();
        const bool last = i + 1 == typed.size();
        if (!last) {
            bytes += eol;
        }
    }
    const bool endOfFile = region + 1 == resolution_.regionCount();
    const git::Lines& ours = resolution_.region(region).ours;
    const bool fileEndsBare =
        endOfFile && !ours.empty() && !ours.back().empty() && ours.back().back() != '\n';
    if (!bytes.empty() && !fileEndsBare) {
        bytes += eol;
    }
    resolution_.setEdited(region, bytes);
    dirty_ = true;
    rebuild();
}

void ConflictDocument::pickAllConflicts(git::Pick pick) {
    resolution_.pickAllConflicts(pick);
    dirty_ = true;
    rebuild();
}

void ConflictDocument::reset() {
    resolution_.reset();
    dirty_ = true;
    rebuild();
}

void ConflictDocument::rebuild() {
    const std::size_t n = resolution_.regionCount();
    results_.assign(n, {});
    regionRow_.assign(n, 0);
    regionRows_.assign(n, 0);
    firstOurs_.assign(n, 1);
    firstTheirs_.assign(n, 1);
    firstResult_.assign(n, 1);
    int row = 0;
    int ours = 1;
    int theirs = 1;
    int result = 1;
    for (std::size_t i = 0; i < n; ++i) {
        const git::MergeRegion& region = resolution_.region(i);
        results_[i] = resolution_.result(i);
        firstOurs_[i] = ours;
        firstTheirs_[i] = theirs;
        firstResult_[i] = result;
        ours += static_cast<int>(region.ours.size());
        theirs += static_cast<int>(region.theirs.size());
        result += static_cast<int>(results_[i].size());

        std::size_t rows = std::max({region.ours.size(), region.theirs.size(), results_[i].size()});
        // A change keeps a row even where every column is empty —
        // both sides deleted the same lines — so it can still be seen.
        if (region.kind != git::MergeRegion::Kind::Same) {
            rows = std::max<std::size_t>(rows, 1);
        }
        rows += static_cast<std::size_t>(headerRows(i));
        regionRow_[i] = row;
        regionRows_[i] = static_cast<int>(rows);
        row += static_cast<int>(rows);
    }
    rowCount_ = row;
    longestFile_ = std::max({ours, theirs, result});
    emit changed();
}

ConflictDocument::Place ConflictDocument::placeAt(int row) const {
    Place place;
    if (regionRow_.empty()) {
        return place;
    }
    // The last region starting at or before the row. Regions with no rows
    // share a start with the next one; upper_bound passes over them.
    const auto after = std::upper_bound(regionRow_.begin(), regionRow_.end(), row);
    const auto index = static_cast<std::size_t>(
        std::max<std::ptrdiff_t>(0, std::distance(regionRow_.begin(), after) - 1));
    place.region = index;
    place.offset = row - regionRow_[index];
    return place;
}

int ConflictDocument::conflictNumber(std::size_t region) const {
    const auto found = std::lower_bound(conflicts_.begin(), conflicts_.end(), region);
    if (found == conflicts_.end() || *found != region) {
        return 0;
    }
    return static_cast<int>(std::distance(conflicts_.begin(), found)) + 1;
}

const git::Lines& ConflictDocument::lines(Column column, std::size_t region) const {
    switch (column) {
    case Column::Ours:
        return resolution_.region(region).ours;
    case Column::Theirs:
        return resolution_.region(region).theirs;
    case Column::Result:
        break;
    }
    return results_[region];
}

int ConflictDocument::firstLine(Column column, std::size_t region) const {
    switch (column) {
    case Column::Ours:
        return firstOurs_[region];
    case Column::Theirs:
        return firstTheirs_[region];
    case Column::Result:
        break;
    }
    return firstResult_[region];
}

} // namespace gity::ui
