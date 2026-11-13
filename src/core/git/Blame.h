// Who last changed each line of a file, read from `git blame --porcelain`.
//
// git rather than libgit2: git_blame is many times slower on a file with a
// long history, and git's blame is the one people check an answer against.
// The worker runs the command (ADR-003); this turns its output into rows.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

struct BlameCommit {
    /// Forty hex digits; all zeros for lines not committed yet.
    std::string id;
    std::string author;
    std::int64_t when = 0;
    std::string summary;
    /// The file's name in this commit — different from today's after a rename.
    std::string path;
    /// The commit before this one that had the file, and its name there:
    /// where to look for what the line said before. Empty when the line was
    /// added with the file, or the commit is the edge of a shallow history.
    std::string previousId;
    std::string previousPath;

    [[nodiscard]] bool uncommitted() const noexcept {
        return id.find_first_not_of('0') == std::string::npos;
    }
};

struct BlameLine {
    /// Index into Blame::commits.
    std::uint32_t commit = 0;
    /// The line's number in that commit's version of the file, from 1.
    std::uint32_t originalLine = 0;
    std::string text;
};

struct Blame {
    /// Each commit once, in order of first appearance.
    std::vector<BlameCommit> commits;
    /// One per line of the file, in order.
    std::vector<BlameLine> lines;
};

/// Parses `git blame --porcelain`. A malformed tail is dropped rather than
/// thrown on: what was read before it is still right.
[[nodiscard]] Blame parseBlamePorcelain(std::string_view output);

} // namespace gity::git
