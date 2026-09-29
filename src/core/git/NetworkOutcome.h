// ADR-003 — network verbs run through the git binary, so the only thing this
// client knows about their result is what git wrote to stdout and stderr.
//
// Turning that text into something a person can act on is parsing, and parsing
// is where a client quietly gets things wrong: git writes progress with
// carriage returns, puts a push's rejection reason several lines below the
// least useful line in the output, and says nothing at all when a fetch had
// nothing to do. The classification lives here, Qt-free, so it can be tested
// against real captured git output rather than against what git is imagined
// to print. The wording stays in the UI layer, where it can be translated.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

enum class NetworkFailure {
    None,
    NoRepository,
    /// git could not obtain credentials; with GIT_TERMINAL_PROMPT=0 it fails
    /// here rather than hanging on a prompt no GUI would ever show.
    AuthRequired,
    /// pull --ff-only against a branch that has moved on both sides.
    Diverged,
    /// push refused because the remote holds commits this branch does not.
    RejectedNonFastForward,
    /// A force push with a lease, refused because the remote branch moved since
    /// it was last fetched — someone pushed. The lease doing its job.
    LeaseRejected,
    /// push refused for some other reason — a hook, a protected branch.
    RejectedOther,
    /// The remote says the repository does not exist. For a private repo on
    /// GitHub this is what *no access* looks like: it answers 404 rather than
    /// 403 so it does not leak which private repositories exist.
    NotFoundOrNoAccess,
    Unknown,
};

struct NetworkOutcome {
    NetworkFailure failure = NetworkFailure::None;
    /// The one line from git worth putting in front of the user, already
    /// stripped of progress noise. Empty when the enum says everything.
    std::string detail;
    /// Success only: the ref update, e.g. "64bdf07..4aafe49  main -> origin/main".
    /// Empty when git did nothing, which is itself the answer.
    std::string summary;
};

/// One step of a transfer, as git reports it while it works.
struct NetworkProgress {
    std::string phase;  ///< "Receiving objects", "Resolving deltas", ...
    int percent = -1;   ///< -1 when the phase reports a count but no percentage.
};

/// Recognises a git progress line. Returns false for anything else, which is
/// most of what git prints — the caller shows the last recognised phase and
/// ignores the rest rather than putting raw transfer chatter in front of
/// someone. Handles the "remote: " prefix git adds to the server's own phases.
[[nodiscard]] bool parseProgress(std::string_view line, NetworkProgress* out);

/// Splits on both \n and \r — git's progress writes carriage returns without
/// newlines, so a \n-only split yields "lines" holding a whole progress bar.
/// Trailing whitespace goes too: git pads remote: lines out to a fixed width.
[[nodiscard]] std::vector<std::string> splitGitOutput(std::string_view text);

[[nodiscard]] NetworkOutcome classifyNetwork(bool ok, std::string_view standardOutput,
                                             std::string_view errorOutput);

} // namespace gity::git
