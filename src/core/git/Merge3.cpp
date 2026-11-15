#include "Merge3.h"

#include <git2.h>

#include <algorithm>
#include <stdexcept>

namespace gity::git {

Lines splitLines(std::string_view text) {
    Lines lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline + 1;
        lines.emplace_back(text.substr(start, end - start));
        start = end;
    }
    return lines;
}

std::string joinLines(const Lines& lines) {
    std::string out;
    for (const auto& line : lines) {
        out += line;
    }
    return out;
}

std::size_t Merge3::conflictCount() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(regions.begin(), regions.end(),
                      [](const MergeRegion& r) { return r.kind == MergeRegion::Kind::Conflict; }));
}

namespace {

/// One change from the base to a side: base lines [baseBegin, baseEnd)
/// became side lines [sideBegin, sideEnd). Either range may be empty.
struct Change {
    std::size_t baseBegin = 0;
    std::size_t baseEnd = 0;
    std::size_t sideBegin = 0;
    std::size_t sideEnd = 0;
};

/// libgit2 numbers lines from 1, and an empty range by the line before it.
std::size_t rangeBegin(int start, int count) {
    return static_cast<std::size_t>(count == 0 ? start : start - 1);
}

std::vector<Change> changesBetween(std::string_view base, std::string_view side) {
    std::vector<Change> changes;
    git_diff_options options = GIT_DIFF_OPTIONS_INIT;
    options.context_lines = 0;
    options.interhunk_lines = 0;
    // Bytes that look binary are still lines here; the caller decided the
    // file is text.
    options.flags = GIT_DIFF_FORCE_TEXT;
    const auto onHunk = [](const git_diff_delta*, const git_diff_hunk* hunk, void* payload) {
        auto* found = static_cast<std::vector<Change>*>(payload);
        Change change;
        change.baseBegin = rangeBegin(hunk->old_start, hunk->old_lines);
        change.baseEnd = change.baseBegin + static_cast<std::size_t>(hunk->old_lines);
        change.sideBegin = rangeBegin(hunk->new_start, hunk->new_lines);
        change.sideEnd = change.sideBegin + static_cast<std::size_t>(hunk->new_lines);
        found->push_back(change);
        return 0;
    };
    const int rc = git_diff_buffers(base.data(), base.size(), nullptr, side.data(), side.size(),
                                    nullptr, &options, nullptr, nullptr, onHunk, nullptr, &changes);
    if (rc < 0) {
        const git_error* error = git_error_last();
        throw std::runtime_error(
            std::string("compare two versions of a file: ") +
            (error != nullptr && error->message != nullptr ? error->message : "unknown error"));
    }
    return changes;
}

Lines slice(const Lines& lines, std::size_t begin, std::size_t end) {
    begin = std::min(begin, lines.size());
    end = std::clamp(end, begin, lines.size());
    return {lines.begin() + static_cast<std::ptrdiff_t>(begin),
            lines.begin() + static_cast<std::ptrdiff_t>(end)};
}

} // namespace

Merge3 mergeLines(std::string_view baseText, std::string_view oursText,
                  std::string_view theirsText) {
    const Lines base = splitLines(baseText);
    const Lines ours = splitLines(oursText);
    const Lines theirs = splitLines(theirsText);
    const std::vector<Change> a = changesBetween(baseText, oursText);
    const std::vector<Change> b = changesBetween(baseText, theirsText);

    Merge3 merge;
    const auto same = [&](std::size_t begin, std::size_t end) {
        if (begin < end) {
            MergeRegion region;
            region.base = slice(base, begin, end);
            region.ours = region.base;
            region.theirs = region.base;
            merge.regions.push_back(std::move(region));
        }
    };

    std::size_t ia = 0;
    std::size_t ib = 0;
    std::size_t done = 0; // base lines already placed in a region
    // How far each side's line numbers have drifted from the base's.
    std::ptrdiff_t driftA = 0;
    std::ptrdiff_t driftB = 0;
    const auto at = [](std::size_t baseLine, std::ptrdiff_t drift) {
        return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(baseLine) + drift);
    };

    while (ia < a.size() || ib < b.size()) {
        const std::size_t start = std::min(ia < a.size() ? a[ia].baseBegin : base.size() + 1,
                                           ib < b.size() ? b[ib].baseBegin : base.size() + 1);
        same(done, start);

        // Gather every change, from either side, that overlaps or touches
        // the region so far.
        std::size_t end = start;
        std::size_t ja = ia;
        std::size_t jb = ib;
        for (bool grew = true; grew;) {
            grew = false;
            if (ja < a.size() && a[ja].baseBegin <= end) {
                end = std::max(end, a[ja].baseEnd);
                ++ja;
                grew = true;
            }
            if (jb < b.size() && b[jb].baseBegin <= end) {
                end = std::max(end, b[jb].baseEnd);
                ++jb;
                grew = true;
            }
        }

        const auto side = [&](const Lines& lines, const std::vector<Change>& changes,
                              std::size_t from, std::size_t to, std::ptrdiff_t& drift) {
            if (from == to) {
                return slice(base, start, end);
            }
            std::ptrdiff_t grown = 0;
            for (std::size_t k = from; k < to; ++k) {
                grown += static_cast<std::ptrdiff_t>(changes[k].sideEnd - changes[k].sideBegin) -
                         static_cast<std::ptrdiff_t>(changes[k].baseEnd - changes[k].baseBegin);
            }
            Lines taken = slice(lines, at(start, drift), at(end, drift + grown));
            drift += grown;
            return taken;
        };

        MergeRegion region;
        region.base = slice(base, start, end);
        region.ours = side(ours, a, ia, ja, driftA);
        region.theirs = side(theirs, b, ib, jb, driftB);
        const bool changedA = ja > ia;
        const bool changedB = jb > ib;
        if (changedA && changedB) {
            region.kind = region.ours == region.theirs ? MergeRegion::Kind::Both
                                                       : MergeRegion::Kind::Conflict;
        } else {
            region.kind = changedA ? MergeRegion::Kind::Ours : MergeRegion::Kind::Theirs;
        }
        merge.regions.push_back(std::move(region));

        done = end;
        ia = ja;
        ib = jb;
    }
    same(done, base.size());

    // Lines both sides start or end a conflict with are not in conflict: they
    // go before and after it, as git's markers leave them out, so a conflict
    // shows only what really differs.
    std::vector<MergeRegion> trimmed;
    trimmed.reserve(merge.regions.size());
    for (auto& region : merge.regions) {
        if (region.kind != MergeRegion::Kind::Conflict) {
            trimmed.push_back(std::move(region));
            continue;
        }
        const std::size_t shorter = std::min(region.ours.size(), region.theirs.size());
        std::size_t head = 0;
        while (head < shorter && region.ours[head] == region.theirs[head]) {
            ++head;
        }
        std::size_t tail = 0;
        while (tail < shorter - head && region.ours[region.ours.size() - 1 - tail] ==
                                            region.theirs[region.theirs.size() - 1 - tail]) {
            ++tail;
        }
        // Where the conflict itself lands, once a head goes in before it.
        const std::size_t conflictAt = trimmed.size() + (head > 0 ? 1 : 0);
        const auto agreed = [conflictAt](const Lines& lines, std::size_t begin, std::size_t end) {
            MergeRegion both;
            both.kind = MergeRegion::Kind::Both;
            both.ours = slice(lines, begin, end);
            both.theirs = both.ours;
            both.edgeOf = conflictAt;
            return both;
        };
        if (head > 0) {
            trimmed.push_back(agreed(region.ours, 0, head));
        }
        MergeRegion middle;
        middle.kind = MergeRegion::Kind::Conflict;
        middle.base = std::move(region.base);
        middle.ours = slice(region.ours, head, region.ours.size() - tail);
        middle.theirs = slice(region.theirs, head, region.theirs.size() - tail);
        trimmed.push_back(std::move(middle));
        if (tail > 0) {
            trimmed.push_back(agreed(region.ours, region.ours.size() - tail, region.ours.size()));
        }
    }
    merge.regions = std::move(trimmed);
    return merge;
}

namespace {

Pick naturalPick(MergeRegion::Kind kind) {
    switch (kind) {
    case MergeRegion::Kind::Theirs:
        return Pick::Theirs;
    case MergeRegion::Kind::Conflict:
        return Pick::Unresolved;
    case MergeRegion::Kind::Same:
    case MergeRegion::Kind::Ours:
    case MergeRegion::Kind::Both:
        break;
    }
    return Pick::Ours;
}

bool endsLine(const std::string& text) {
    return !text.empty() && text.back() == '\n';
}

/// Lines `more` after `lines`, ending the last of `lines` first if it had no
/// ending: two sides' lines must not run together on one line.
void appendLines(Lines& lines, const Lines& more, std::string_view eol) {
    if (!lines.empty() && !more.empty() && !endsLine(lines.back())) {
        lines.back() += eol;
    }
    lines.insert(lines.end(), more.begin(), more.end());
}

} // namespace

MergeResolution::MergeResolution(Merge3 merge) : merge_(std::move(merge)) {
    // New line endings — markers, a side's last line ended so another can
    // follow — are the ones most of the file's lines have, as git's are.
    std::size_t crlf = 0;
    std::size_t lf = 0;
    for (const auto& region : merge_.regions) {
        for (const auto& line : region.ours) {
            if (line.size() >= 2 && line[line.size() - 2] == '\r') {
                ++crlf;
            } else if (endsLine(line)) {
                ++lf;
            }
        }
    }
    eol_ = crlf > lf ? "\r\n" : "\n";
    reset();
}

void MergeResolution::reset() {
    picks_.clear();
    edited_.assign(merge_.regions.size(), {});
    for (const auto& region : merge_.regions) {
        picks_.push_back(naturalPick(region.kind));
    }
}

void MergeResolution::setPick(std::size_t i, Pick pick) {
    picks_[i] = pick;
}

void MergeResolution::setEdited(std::size_t i, std::string_view text) {
    edited_[i] = splitLines(text);
    picks_[i] = Pick::Edited;
}

void MergeResolution::pickAllConflicts(Pick pick) {
    for (std::size_t i = 0; i < merge_.regions.size(); ++i) {
        if (merge_.regions[i].kind == MergeRegion::Kind::Conflict) {
            picks_[i] = pick;
        }
    }
}

Lines MergeResolution::result(std::size_t i) const {
    const MergeRegion& region = merge_.regions[i];
    const std::string_view eol = eol_;
    if (region.edgeOf && picks_[*region.edgeOf] == Pick::Base) {
        return {}; // the base is the whole of what was there before
    }
    switch (picks_[i]) {
    case Pick::Unresolved:
        return {};
    case Pick::Ours:
        return region.ours;
    case Pick::Theirs:
        return region.theirs;
    case Pick::Base:
        return region.base;
    case Pick::Edited:
        return edited_[i];
    case Pick::OursThenTheirs: {
        Lines both = region.ours;
        appendLines(both, region.theirs, eol);
        return both;
    }
    case Pick::TheirsThenOurs: {
        Lines both = region.theirs;
        appendLines(both, region.ours, eol);
        return both;
    }
    }
    return {};
}

std::size_t MergeResolution::unresolvedCount() const noexcept {
    return static_cast<std::size_t>(std::count(picks_.begin(), picks_.end(), Pick::Unresolved));
}

std::optional<std::size_t> MergeResolution::nextUnresolved(std::size_t from) const {
    const std::size_t n = picks_.size();
    for (std::size_t step = 1; step <= n; ++step) {
        const std::size_t i = (from + step) % n;
        if (picks_[i] == Pick::Unresolved) {
            return i;
        }
    }
    return std::nullopt;
}

std::string MergeResolution::text(std::string_view oursLabel, std::string_view theirsLabel) const {
    const std::string& eol = eol_;
    Lines out;
    for (std::size_t i = 0; i < merge_.regions.size(); ++i) {
        if (picks_[i] != Pick::Unresolved) {
            appendLines(out, result(i), eol);
            continue;
        }
        const MergeRegion& region = merge_.regions[i];
        appendLines(out, {"<<<<<<< " + std::string(oursLabel) + eol}, eol);
        appendLines(out, region.ours, eol);
        appendLines(out, {"=======" + eol}, eol);
        appendLines(out, region.theirs, eol);
        appendLines(out, {">>>>>>> " + std::string(theirsLabel) + eol}, eol);
    }
    return joinLines(out);
}

} // namespace gity::git
