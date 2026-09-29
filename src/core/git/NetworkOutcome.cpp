#include "NetworkOutcome.h"

#include <algorithm>
#include <cctype>

namespace gity::git {
namespace {

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

/// Counting, compressing, writing: lines that describe the transfer rather
/// than its result. None of them is what a person wants in a status bar.
bool isProgressNoise(const std::string& line) {
    static constexpr std::string_view kNoise[] = {
        "Enumerating objects", "Counting objects",  "Compressing objects",
        "Writing objects",     "Receiving objects", "Resolving deltas",
        "Delta compression",   "Total ",            "Unpacking objects",
    };
    return std::any_of(std::begin(kNoise), std::end(kNoise),
                       [&](std::string_view noise) { return contains(line, noise); });
}

} // namespace

bool parseProgress(std::string_view line, NetworkProgress* out) {
    if (out == nullptr) {
        return false;
    }
    // The server's phases arrive prefixed; the user does not care which side
    // of the connection is counting.
    constexpr std::string_view kRemote = "remote: ";
    if (line.starts_with(kRemote)) {
        line.remove_prefix(kRemote.size());
    }

    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    const std::string_view phase = line.substr(0, colon);
    if (!isProgressNoise(std::string(phase))) {
        return false;
    }

    out->phase = std::string(phase);
    out->percent = -1;

    // "Receiving objects:  42% (100/238)" — the percentage, when there is one.
    const std::size_t percentSign = line.find('%');
    if (percentSign != std::string_view::npos && percentSign > colon) {
        std::size_t start = percentSign;
        while (start > colon + 1 && std::isdigit(static_cast<unsigned char>(line[start - 1]))) {
            --start;
        }
        if (start < percentSign) {
            out->percent = std::stoi(std::string(line.substr(start, percentSign - start)));
        }
    }
    return true;
}

std::vector<std::string> splitGitOutput(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    const auto flush = [&] {
        while (!current.empty() && (current.back() == ' ' || current.back() == '\t')) {
            current.pop_back();
        }
        if (!current.empty()) {
            lines.push_back(current);
        }
        current.clear();
    };
    for (const char c : text) {
        if (c == '\n' || c == '\r') {
            flush();
            continue;
        }
        current.push_back(c);
    }
    flush();
    return lines;
}

NetworkOutcome classifyNetwork(bool ok, std::string_view standardOutput,
                               std::string_view errorOutput) {
    NetworkOutcome outcome;
    const std::vector<std::string> errorLines = splitGitOutput(errorOutput);

    if (ok) {
        // The ref update is the result; everything else is transfer chatter.
        // "remote:" lines are the server talking about its own work.
        for (auto line = errorLines.rbegin(); line != errorLines.rend(); ++line) {
            if (contains(*line, "->") && !line->starts_with("remote:")) {
                outcome.summary = *line;
                // Leading whitespace is git's column alignment, not content.
                outcome.summary.erase(0, outcome.summary.find_first_not_of(" \t"));
                return outcome;
            }
        }
        // pull writes its result to stdout instead: "Updating a..b".
        for (const std::string& line : splitGitOutput(standardOutput)) {
            if (!isProgressNoise(line)) {
                outcome.summary = line;
                return outcome;
            }
        }
        return outcome; // nothing to do; the caller says so in its own words
    }

    if (contains(errorOutput, "could not read Username") ||
        contains(errorOutput, "could not read Password") ||
        contains(errorOutput, "Authentication failed") ||
        contains(errorOutput, "terminal prompts disabled")) {
        outcome.failure = NetworkFailure::AuthRequired;
        return outcome;
    }

    // Before the generic unknown case, and after auth: a host that challenged
    // and refused says "Authentication failed", while one that will not admit
    // the repository exists says this.
    if (contains(errorOutput, "Repository not found") ||
        contains(errorOutput, "' not found") ||
        contains(errorOutput, "does not appear to be a git repository") ||
        contains(errorOutput, "ERROR: Repository not found")) {
        outcome.failure = NetworkFailure::NotFoundOrNoAccess;
        return outcome;
    }

    if (contains(errorOutput, "Not possible to fast-forward") ||
        contains(errorOutput, "Need to specify how to reconcile")) {
        outcome.failure = NetworkFailure::Diverged;
        return outcome;
    }

    // "rejected]" and not "[rejected]": git writes "! [remote rejected]" when
    // the server refuses — a protected branch or a pre-receive hook — which is
    // the rejection a team is most likely to meet, and matching the bracket
    // would skip every one of them.
    if (contains(errorOutput, "rejected]")) {
        // Both wordings mean the same thing to the user; git picks between
        // them by whether the branch is behind or merely divergent.
        // Checked first: a stale lease is also a rejection, but the advice for
        // it is the opposite of "pull first" — someone else's work arrived.
        if (contains(errorOutput, "(stale info)")) {
            outcome.failure = NetworkFailure::LeaseRejected;
            return outcome;
        }
        if (contains(errorOutput, "non-fast-forward") || contains(errorOutput, "fetch first")) {
            outcome.failure = NetworkFailure::RejectedNonFastForward;
            return outcome;
        }
        outcome.failure = NetworkFailure::RejectedOther;
        for (const std::string& line : errorLines) {
            if (contains(line, "rejected]")) {
                outcome.detail = line;
                outcome.detail.erase(0, outcome.detail.find_first_not_of(" \t"));
                break;
            }
        }
        return outcome;
    }

    outcome.failure = NetworkFailure::Unknown;
    // "fatal:" is git's own summary of why it stopped, and it is the last
    // thing it prints. "error:" is the fallback for verbs that use it instead.
    for (auto line = errorLines.rbegin(); line != errorLines.rend(); ++line) {
        if (line->starts_with("fatal:") || line->starts_with("error:")) {
            outcome.detail = *line;
            return outcome;
        }
    }
    for (auto line = errorLines.rbegin(); line != errorLines.rend(); ++line) {
        if (!isProgressNoise(*line) && !line->starts_with("hint:")) {
            outcome.detail = *line;
            return outcome;
        }
    }
    return outcome;
}

} // namespace gity::git
