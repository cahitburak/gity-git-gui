// One file's diff, structured by hunk.
//
// The structure is dictated by M2 as much as by M1: hunk and line staging
// needs each hunk addressable and each line to know which side it came from,
// so the model is built that way now rather than reshaped later.
//
// Line text lives in one arena rather than a string per line, for the same
// reason commit metadata does (ADR-005) — a diff of a generated file can run
// to tens of thousands of lines.
#pragma once

#include "CommitDetail.h"
#include "Repository.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

using PatchHandle = Handle<git_patch, git_patch_free>;

enum class DiffLineKind {
    Context,
    Addition,
    Deletion,
    NoNewlineMarker, ///< "\ No newline at end of file"
};

struct DiffLine {
    DiffLineKind kind = DiffLineKind::Context;
    /// 0 means "not present on this side" — a deletion has no new-side number.
    std::uint32_t oldLine = 0;
    std::uint32_t newLine = 0;
    std::uint32_t textOffset = 0;
    std::uint32_t textLength = 0;
    /// The line ended in CR LF. The text is stored without either, so the diff
    /// view shows no stray glyph — but a patch rebuilt from it must put the CR
    /// back, or it no longer matches a file committed with CRLF endings.
    bool carriageReturn = false;
};

struct DiffHunk {
    std::string header; ///< The @@ line, verbatim.
    std::uint32_t firstLine = 0;
    std::uint32_t lineCount = 0;
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
};

struct FileDiff {
    std::string path;
    std::string oldPath;
    ChangeStatus status = ChangeStatus::Unknown;

    /// Git file modes of the two sides; 0 where the side does not exist — an
    /// untracked or added file has no old side, a deleted one no new side. A
    /// patch that creates or deletes the file has to say so in its header.
    std::uint32_t oldMode = 0;
    std::uint32_t newMode = 0;

    bool binary = false;
    /// True when the file was skipped for size. A 40 MB generated asset has no
    /// readable textual diff and rendering one helps nobody.
    bool tooLarge = false;

    std::vector<DiffHunk> hunks;
    std::vector<DiffLine> lines;
    std::string text; ///< Arena; DiffLine offsets index into this.

    [[nodiscard]] std::string_view lineText(const DiffLine& line) const noexcept {
        return std::string_view(text).substr(line.textOffset, line.textLength);
    }

    [[nodiscard]] bool empty() const noexcept { return lines.empty(); }

    [[nodiscard]] std::size_t additions() const noexcept;
    [[nodiscard]] std::size_t deletions() const noexcept;
};

struct FileDiffOptions {
    /// Beyond this the diff is reported as too large rather than rendered.
    std::size_t maxLines = 200000;
    std::uint32_t contextLines = 3;
};

/// Fills `out` from a libgit2 patch: status, modes and the binary flag from its
/// delta, then every hunk and line, stopping at `maxLines`. The one place a
/// patch becomes a FileDiff, for history and working copy alike.
void readPatch(git_patch* patch, FileDiff& out, std::size_t maxLines);

/// Diffs `path` in `commit` against its first parent.
[[nodiscard]] FileDiff loadFileDiff(git_repository* repo, const git_oid& commitOid,
                                    const std::string& path,
                                    const FileDiffOptions& options = {});

/// Diffs `path` as it is at `toOid` against how it is at `fromOid` — the
/// change you would see going from one commit, or branch tip, to the other.
/// Renames are followed the same way as for a single commit.
[[nodiscard]] FileDiff loadFileDiffBetween(git_repository* repo, const git_oid& fromOid,
                                           const git_oid& toOid, const std::string& path,
                                           const FileDiffOptions& options = {});

} // namespace gity::git
