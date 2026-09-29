// The fixtures below are real bytes, captured by running the verbs against a
// throwaway local remote and redirecting git's stderr to a file — carriage
// returns, trailing padding and all. That matters: the last defect of this
// shape got through because the test asserted the format the code intended to
// parse rather than the one git emits.
#include "core/git/NetworkOutcome.h"

#include <gtest/gtest.h>

using gity::git::classifyNetwork;
using gity::git::NetworkFailure;
using gity::git::splitGitOutput;

namespace {

// git push --progress, succeeding. Note the \r-separated progress.
constexpr const char* kPushSuccess =
    "Enumerating objects: 7, done.\n"
    "Counting objects:  14% (1/7)\rCounting objects: 100% (7/7)\rCounting objects: 100% (7/7), "
    "done.\n"
    "Delta compression using up to 32 threads\n"
    "Compressing objects:  33% (1/3)\rCompressing objects: 100% (3/3), done.\n"
    "Writing objects:  25% (1/4)\rWriting objects: 100% (4/4), 361 bytes | 361.00 KiB/s, done.\n"
    "Total 4 (delta 0), reused 0 (delta 0), pack-reused 0 (from 0)\n"
    "To /tmp/scratch/net-remote.git\n"
    "   dfeae99..64bdf07  main -> main\n";

// git fetch --all --prune --progress. The remote: lines are padded out with
// trailing spaces, which is why splitting has to trim them.
constexpr const char* kFetchSuccess =
    "remote: Enumerating objects: 7, done.        \n"
    "remote: Counting objects:  14% (1/7)        \rremote: Counting objects: 100% (7/7), done.  "
    "      \n"
    "remote: Total 4 (delta 0), reused 0 (delta 0), pack-reused 0 (from 0)        \n"
    "From /tmp/scratch/net-remote\n"
    "   64bdf07..4aafe49  main       -> origin/main\n";

constexpr const char* kPushRejectedBehind =
    "To /tmp/scratch/net-remote.git\n"
    " ! [rejected]        main -> main (non-fast-forward)\n"
    "error: failed to push some refs to '/tmp/scratch/net-remote.git'\n"
    "hint: Updates were rejected because the tip of your current branch is behind\n"
    "hint: its remote counterpart. If you want to integrate the remote changes,\n"
    "hint: use 'git pull' before pushing again.\n";

constexpr const char* kPushRejectedFetchFirst =
    "To /tmp/scratch/net-remote.git\n"
    " ! [rejected]        main -> main (fetch first)\n"
    "error: failed to push some refs to '/tmp/scratch/net-remote.git'\n"
    "hint: Updates were rejected because the remote contains work that you do not\n"
    "hint: have locally.\n";

constexpr const char* kPullDiverged =
    "hint: Diverging branches can't be fast-forwarded, you need to either:\n"
    "hint:\n"
    "hint: \tgit merge --no-ff\n"
    "hint:\n"
    "hint: Disable this message with \"git config set advice.diverging false\"\n"
    "fatal: Not possible to fast-forward, aborting.\n";

/// A failure with no specific classification: the host could not be reached
/// at all. Nothing in it matches a known pattern, so it exercises the
/// fallback that reports git's own fatal line.
constexpr const char* kUnreachableHost =
    "fatal: unable to access 'https://example.invalid/repo.git/': Could not resolve host: "
    "example.invalid\n";

} // namespace

TEST(SplitGitOutput, TreatsCarriageReturnsAsLineBreaks) {
    // A \n-only split would return one "line" holding the whole progress bar.
    const auto lines = splitGitOutput("Counting: 10%\rCounting: 100%\nDone\n");
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "Counting: 10%");
    EXPECT_EQ(lines[1], "Counting: 100%");
    EXPECT_EQ(lines[2], "Done");
}

TEST(SplitGitOutput, TrimsTheTrailingPaddingGitAddsToRemoteLines) {
    const auto lines = splitGitOutput("remote: Counting objects: 100% (7/7), done.        \n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "remote: Counting objects: 100% (7/7), done.");
}

TEST(ClassifyNetwork, SuccessfulPushSummarisesTheRefUpdate) {
    const auto outcome = classifyNetwork(true, "", kPushSuccess);
    EXPECT_EQ(outcome.failure, NetworkFailure::None);
    EXPECT_EQ(outcome.summary, "dfeae99..64bdf07  main -> main");
}

TEST(ClassifyNetwork, SuccessfulFetchSummarisesTheRefUpdateNotTheRemoteChatter) {
    const auto outcome = classifyNetwork(true, "", kFetchSuccess);
    EXPECT_EQ(outcome.summary, "64bdf07..4aafe49  main       -> origin/main");
}

TEST(ClassifyNetwork, FetchWithNothingToDoProducesNoSummary) {
    // An up-to-date fetch prints nothing whatsoever. Verified against git.
    const auto outcome = classifyNetwork(true, "", "");
    EXPECT_EQ(outcome.failure, NetworkFailure::None);
    EXPECT_TRUE(outcome.summary.empty());
}

TEST(ClassifyNetwork, FastForwardPullFallsBackToStdout) {
    // pull writes its result to stdout and leaves stderr empty, and only the
    // first line belongs in a status bar.
    const auto outcome = classifyNetwork(
        true, "Updating dfeae99..64bdf07\nFast-forward\n Assets/c.txt | 1 +\n", "");
    EXPECT_EQ(outcome.summary, "Updating dfeae99..64bdf07");
}

TEST(ClassifyNetwork, BothPushRejectionWordingsMeanTheSameThing) {
    EXPECT_EQ(classifyNetwork(false, "", kPushRejectedBehind).failure,
              NetworkFailure::RejectedNonFastForward);
    EXPECT_EQ(classifyNetwork(false, "", kPushRejectedFetchFirst).failure,
              NetworkFailure::RejectedNonFastForward);
}

TEST(ClassifyNetwork, DivergedPullIsNotMistakenForARejection) {
    const auto outcome = classifyNetwork(false, "", kPullDiverged);
    EXPECT_EQ(outcome.failure, NetworkFailure::Diverged);
}

TEST(ClassifyNetwork, UnknownFailureReportsGitsFatalLineNotItsHints) {
    const auto outcome = classifyNetwork(false, "", kUnreachableHost);
    EXPECT_EQ(outcome.failure, NetworkFailure::Unknown);
    EXPECT_EQ(outcome.detail,
              "fatal: unable to access 'https://example.invalid/repo.git/': Could not resolve "
              "host: example.invalid");
}

TEST(ClassifyNetwork, AuthFailureIsRecognised) {
    // Not captured from a live run: reaching it needs a private remote with no
    // usable credentials. The string is git's own, from its credential code,
    // and is what GIT_TERMINAL_PROMPT=0 produces instead of hanging.
    const auto outcome = classifyNetwork(
        false, "",
        "fatal: could not read Username for 'https://github.com': terminal prompts disabled\n");
    EXPECT_EQ(outcome.failure, NetworkFailure::AuthRequired);
}

TEST(ClassifyNetwork, ARejectionGitDoesNotExplainStillShowsItsOwnLine) {
    // A pre-receive hook rejection: no "non-fast-forward", so the specific
    // line has to survive to the user or they learn nothing.
    const auto outcome = classifyNetwork(false, "",
                                         "To git@host:repo.git\n"
                                         " ! [remote rejected] main -> main (pre-receive hook "
                                         "declined)\n"
                                         "error: failed to push some refs\n");
    EXPECT_EQ(outcome.failure, NetworkFailure::RejectedOther);
    EXPECT_EQ(outcome.detail, "! [remote rejected] main -> main (pre-receive hook declined)");
}

TEST(ParseProgress, ReadsThePhaseAndPercentage) {
    gity::git::NetworkProgress progress;
    ASSERT_TRUE(gity::git::parseProgress("Receiving objects:  42% (100/238)", &progress));
    EXPECT_EQ(progress.phase, "Receiving objects");
    EXPECT_EQ(progress.percent, 42);
}

TEST(ParseProgress, StripsTheRemotePrefixBecauseTheUserDoesNotCareWhoIsCounting) {
    gity::git::NetworkProgress progress;
    ASSERT_TRUE(gity::git::parseProgress("remote: Counting objects: 100% (7/7), done.", &progress));
    EXPECT_EQ(progress.phase, "Counting objects");
    EXPECT_EQ(progress.percent, 100);
}

TEST(ParseProgress, PhasesWithoutAPercentageStillReport) {
    gity::git::NetworkProgress progress;
    ASSERT_TRUE(gity::git::parseProgress("Enumerating objects: 7, done.", &progress));
    EXPECT_EQ(progress.phase, "Enumerating objects");
    EXPECT_EQ(progress.percent, -1);
}

TEST(ParseProgress, RejectsLinesThatAreNotProgress) {
    gity::git::NetworkProgress progress;
    // The ref update, the destination, a hint: all real output, none of it a
    // phase, and putting any of it in a progress indicator would be wrong.
    EXPECT_FALSE(gity::git::parseProgress("To /tmp/scratch/net-remote.git", &progress));
    EXPECT_FALSE(gity::git::parseProgress("   64bdf07..4aafe49  main -> origin/main", &progress));
    EXPECT_FALSE(gity::git::parseProgress("hint: use 'git pull' before pushing", &progress));
    EXPECT_FALSE(gity::git::parseProgress("", &progress));
}

TEST(ClassifyNetwork, APrivateRepositoryYouCannotSeeIsNotAMissingRepository) {
    // Captured from a live run against a private repository belonging to an
    // organisation the authenticated account is not a member of. GitHub
    // answers 404 rather than 403 so it does not leak which private
    // repositories exist — so "not found" here almost always means "your
    // credentials cannot see it", and reporting git's words verbatim sends
    // someone hunting for a typo in a URL that is correct.
    const auto outcome = classifyNetwork(
        false, "",
        "remote: Repository not found.\n"
        "fatal: repository 'https://github.com/Org/project.git/' not found\n");
    EXPECT_EQ(outcome.failure, NetworkFailure::NotFoundOrNoAccess);
}

TEST(ClassifyNetwork, AMisspeltRemoteNameLandsInTheSamePlace) {
    // Different wording, same question to ask the user: which remote, and can
    // the credentials in use reach it.
    const auto outcome =
        classifyNetwork(false, "",
                        "fatal: 'upstrem' does not appear to be a git repository\n"
                        "fatal: Could not read from remote repository.\n");
    EXPECT_EQ(outcome.failure, NetworkFailure::NotFoundOrNoAccess);
}

TEST(ClassifyNetwork, AGenuineAuthFailureIsStillToldApartFromNotFound) {
    // The host challenged and refused, which is a different fix.
    const auto outcome = classifyNetwork(
        false, "",
        "remote: Invalid username or token. Password authentication is not supported.\n"
        "fatal: Authentication failed for 'https://github.com/owner/repo.git/'\n");
    EXPECT_EQ(outcome.failure, NetworkFailure::AuthRequired);
}

TEST(NetworkOutcome, AStaleLeaseIsItsOwnRefusal) {
    // What git 2.55 prints when --force-with-lease finds the remote moved.
    const auto outcome = gity::git::classifyNetwork(
        false, "",
        "To ../o.git\n ! [rejected]        main -> main (stale info)\n"
        "error: failed to push some refs to '../o.git'\n");
    EXPECT_EQ(outcome.failure, gity::git::NetworkFailure::LeaseRejected);
}
