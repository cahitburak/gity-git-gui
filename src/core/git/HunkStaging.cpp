#include "HunkStaging.h"

#include <algorithm>

namespace gity::git {
namespace {

bool isChange(DiffLineKind kind) {
    return kind == DiffLineKind::Addition || kind == DiffLineKind::Deletion;
}

/// The worktree line a changed line sits at.
///
/// An addition has one. A deletion does not — it is a line that is no longer
/// in the file — so it is anchored to the worktree position it was removed
/// from, which is the new-side number the hunk has reached at that point.
/// Without this, deletions could not be attributed to a region at all and a
/// hunk that only removes lines would always report nothing staged.
std::vector<std::uint32_t> anchorLines(const FileDiff& diff) {
    std::vector<std::uint32_t> anchors(diff.lines.size(), 0);

    for (const DiffHunk& hunk : diff.hunks) {
        auto cursor = static_cast<std::uint32_t>(std::max(hunk.newStart, 0));
        const std::size_t end =
            std::min<std::size_t>(diff.lines.size(), hunk.firstLine + hunk.lineCount);
        for (std::size_t i = hunk.firstLine; i < end; ++i) {
            const DiffLine& line = diff.lines[i];
            if (line.newLine != 0) {
                cursor = line.newLine;
            }
            anchors[i] = cursor;
        }
    }
    return anchors;
}

} // namespace

std::vector<HunkStaging> hunkStagingCounts(const FileDiff& unstaged, const FileDiff& combined) {
    std::vector<HunkStaging> counts(unstaged.hunks.size());
    const std::vector<std::uint32_t> combinedAnchors = anchorLines(combined);

    for (std::size_t h = 0; h < unstaged.hunks.size(); ++h) {
        const DiffHunk& hunk = unstaged.hunks[h];

        std::size_t unstagedChanges = 0;
        const std::size_t end =
            std::min<std::size_t>(unstaged.lines.size(), hunk.firstLine + hunk.lineCount);
        for (std::size_t i = hunk.firstLine; i < end; ++i) {
            if (isChange(unstaged.lines[i].kind)) {
                ++unstagedChanges;
            }
        }

        // The worktree range this hunk covers. A hunk that only deletes has
        // newCount 0 but still occupies a position, so the range is never
        // empty — otherwise its staged neighbours would go uncounted.
        const auto from = static_cast<std::uint32_t>(std::max(hunk.newStart, 0));
        const auto to = from + static_cast<std::uint32_t>(std::max(hunk.newCount, 1));

        std::size_t totalChanges = 0;
        for (std::size_t i = 0; i < combined.lines.size(); ++i) {
            if (!isChange(combined.lines[i].kind)) {
                continue;
            }
            const std::uint32_t anchor = combinedAnchors[i];
            if (anchor >= from && anchor < to) {
                ++totalChanges;
            }
        }

        // Never negative: the combined diff should contain everything the
        // unstaged one does, but a clamp beats a count that wraps if the two
        // were ever loaded a moment apart.
        counts[h].total = std::max(totalChanges, unstagedChanges);
        counts[h].staged = counts[h].total - unstagedChanges;
    }
    return counts;
}

} // namespace gity::git
