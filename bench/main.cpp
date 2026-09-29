// M0 — the milestone that exists to falsify the plan cheaply.
//
// Measures the four operations the whole architecture bets on, before any UI
// exists to hide behind:
//
//   * revwalk + parent table   (ADR-003 read path)
//   * lane assignment          (ADR-005, and its memory budget)
//   * status                   (the highest-severity risk in the record)
//   * tree-to-tree diff        (the diff view's floor)
//
// libgit2 status is compared directly against `git status`, because "libgit2
// is slower than git on large worktrees" is the assumption most likely to be
// wrong, and week one is when that is cheap to learn.

#include "core/git/HistoryStream.h"
#include "core/git/WorkingCopy.h"
#include "core/git/Repository.h"
#include "core/graph/LaneAssigner.h"
#include "core/model/HistoryChunk.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

class Stopwatch {
public:
    void reset() { start_ = Clock::now(); }

    [[nodiscard]] double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_ = Clock::now();
};

struct Measurement {
    std::string name;
    double ms = 0.0;
    std::string detail;
};

std::vector<Measurement> results;

void record(std::string name, double ms, std::string detail) {
    results.push_back({std::move(name), ms, std::move(detail)});
}

/// Peak resident set, in KiB. Linux only; the other platforms report 0 and the
/// row is omitted.
std::size_t peakRssKib() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key == "VmHWM:") {
            std::size_t value = 0;
            status >> value;
            return value;
        }
        status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
#endif
    return 0;
}

struct CommandResult {
    double ms = 0.0;
    std::size_t lines = 0;
    bool ok = false;
};

/// Times a shell command and counts its output lines. Used only to compare
/// libgit2 against the CLI — the real client never parses human-facing text
/// (ADR-003, CLI invocation contract).
CommandResult timeCommand(const std::string& command) {
    CommandResult result;
    Stopwatch watch;

#if defined(_WIN32)
    std::FILE* pipe = _popen(command.c_str(), "r");
#else
    std::FILE* pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return result;
    }

    std::array<char, 8192> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        ++result.lines;
    }

#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    result.ms = watch.elapsedMs();
    result.ok = true;
    return result;
}

void printUsage(const std::filesystem::path& tried) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(tried, ec);

    std::fprintf(stderr, "\n  No Git repository at %s\n\n",
                 (ec ? tried : absolute).string().c_str());
    std::fprintf(stderr, "  gity-bench measures libgit2 against a real repository. Point it at one:\n\n");
    std::fprintf(stderr, "      gity-bench /path/to/a/repository\n\n");
    std::fprintf(stderr, "  The M0 gate is a real repository with LFS — synthetic history has\n");
    std::fprintf(stderr, "  uniform object sizes and a clean worktree, so it flatters status. For a\n");
    std::fprintf(stderr, "  smoke test anyway:\n\n");
    std::fprintf(stderr, "      tools/make-bench-repo.sh 200000 /tmp/bench-repo\n");
    std::fprintf(stderr, "      gity-bench /tmp/bench-repo\n\n");
    std::fprintf(stderr, "  Running from an IDE? The working directory is the build folder, so the\n");
    std::fprintf(stderr, "  path must be explicit. In Qt Creator: Projects -> Run ->\n");
    std::fprintf(stderr, "  Command line arguments.\n\n");
}

void printTable() {
    std::printf("\n  %-34s %12s   %s\n", "OPERATION", "TIME (ms)", "RESULT");
    std::printf("  %s\n", std::string(78, '-').c_str());
    for (const auto& r : results) {
        std::printf("  %-34s %12.2f   %s\n", r.name.c_str(), r.ms, r.detail.c_str());
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    const std::string path = (argc > 1) ? argv[1] : ".";

    try {
        const gity::git::LibGit2 libgit2;

        // libgit2's object cache defaults to 256 MiB and dominates RSS on a
        // full-history walk. GITY_BENCH_CACHE_MB=<n> exists to measure that.
        std::string cacheNote = "libgit2 default (256 MiB)";
        if (const char* cacheMb = std::getenv("GITY_BENCH_CACHE_MB")) {
            const auto megabytes = std::strtoll(cacheMb, nullptr, 10);
            // GIT_OPT_SET_CACHE_MAX_SIZE takes ssize_t, which MSVC does not
            // define. ptrdiff_t is the same width everywhere we build, and
            // width is what matters for a varargs call.
            gity::git::check(
                git_libgit2_opts(GIT_OPT_SET_CACHE_MAX_SIZE,
                                 static_cast<std::ptrdiff_t>(megabytes * 1024 * 1024)),
                "set the libgit2 cache size");
            cacheNote = std::string(cacheMb) + " MiB (GITY_BENCH_CACHE_MB)";
        }

        Stopwatch watch;
        gity::git::RepositoryHandle repo;
        try {
            repo = gity::git::openRepository(path);
        } catch (const gity::git::GitException&) {
            // The only failure here a user is likely to hit, and the libgit2
            // message ("could not find repository at '.'") does not tell them
            // what to do about it.
            printUsage(path);
            return 2;
        }
        record("open repository", watch.elapsedMs(), path);

        const char* workdirRaw = git_repository_workdir(repo.get());
        const std::filesystem::path workdir =
            (workdirRaw != nullptr) ? std::filesystem::path(workdirRaw) : std::filesystem::path(path);

        // --- how does the walk cost split? -------------------------------------
        // The revwalk alone yields OIDs in order. Parents need a full object
        // lookup per commit, which is the part that can be moved onto the
        // ADR-004 read pool. Measuring them separately decides whether that is
        // worth doing.
        watch.reset();
        {
            gity::git::RevwalkHandle walk;
            gity::git::check(git_revwalk_new(walk.receive(), repo.get()), "create a revwalk");
            git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
            git_revwalk_push_glob(walk.get(), "refs/heads/*");
            git_revwalk_push_glob(walk.get(), "refs/remotes/*");

            git_oid walkOid{};
            std::size_t walked = 0;
            while (git_revwalk_next(&walkOid, walk.get()) == 0) {
                ++walked;
            }
            record("  revwalk only (oids, no parents)", watch.elapsedMs(),
                   std::to_string(walked) + " commits");
        }

        // --- streaming walk + lane assignment ---------------------------------
        // M1 paints rows as the walk produces them, so the number that matters
        // is time to the FIRST chunk, not time to completion.
        std::uint16_t maxLanes = 0;
        std::size_t merges = 0;
        std::size_t tips = 0;
        std::size_t chunks = 0;
        std::size_t graphBytes = 0;
        std::size_t storeBytes = 0;
        std::size_t totalRows = 0;

        gity::git::StreamOptions streamOptions;
        watch.reset();
        const auto stats = gity::git::streamHistory(
            repo.get(), streamOptions, [&](gity::model::HistoryChunk&& chunk) {
                ++chunks;
                totalRows += chunk.rowCount();
                graphBytes += chunk.graph.bytes();
                storeBytes += chunk.commits.bytes();
                for (const auto& row : chunk.graph.rows) {
                    maxLanes = std::max(maxLanes, row.laneCount);
                    if ((row.flags & gity::graph::RowFlag_Merge) != 0) {
                        ++merges;
                    }
                    if ((row.flags & gity::graph::RowFlag_Tip) != 0) {
                        ++tips;
                    }
                }
                return true;
            });

        const std::size_t commits = stats.commits;
        if (commits == 0) {
            std::fprintf(stderr, "No commits found in %s\n", path.c_str());
            return 1;
        }

        record("time to FIRST paintable chunk", stats.firstChunkMs,
               std::to_string(std::min(streamOptions.batchSize, commits)) +
                   " rows — budget 1000 ms");
        record("full stream (walk + lanes)", stats.totalMs,
               std::to_string(commits) + " commits, " +
                   std::to_string(static_cast<std::size_t>(
                       static_cast<double>(commits) / std::max(stats.totalMs, 0.001) * 1000.0)) +
                   "/s, " + std::to_string(chunks) + " chunks");
        record("  graph shape", 0.0,
               std::to_string(totalRows) + " rows, " + std::to_string(maxLanes) +
                   " lanes max, " + std::to_string(merges) + " merges, " +
                   std::to_string(tips) + " tips");

        const auto perRow = [&](std::size_t total) {
            return static_cast<int>(static_cast<double>(total) /
                                    static_cast<double>(std::max<std::size_t>(totalRows, 1)));
        };
        record("  graph memory", 0.0,
               std::to_string(graphBytes / 1024) + " KiB (" + std::to_string(perRow(graphBytes)) +
                   " B/row incl. edges)");
        record("  commit metadata (arena)", 0.0,
               std::to_string(storeBytes / 1024) + " KiB (" + std::to_string(perRow(storeBytes)) +
                   " B/row)");

        // --- status -----------------------------------------------------------
        // git_status_options_init() rather than the GIT_STATUS_OPTIONS_INIT
        // macro: the macro leaves later fields unmentioned, which AppleClang
        // rejects under -Wmissing-field-initializers.
        git_status_options statusOpts;
        gity::git::check(git_status_options_init(&statusOpts, GIT_STATUS_OPTIONS_VERSION),
                         "initialize status options");
        statusOpts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                           GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

        watch.reset();
        gity::git::StatusListHandle statusList;
        gity::git::check(git_status_list_new(statusList.receive(), repo.get(), &statusOpts),
                       "compute status");
        const double statusMs = watch.elapsedMs();
        const std::size_t statusEntries = git_status_list_entrycount(statusList.get());
        record("status (libgit2)", statusMs, std::to_string(statusEntries) + " entries");

#if defined(_WIN32)
        constexpr const char* kDevNull = " 2>NUL";
#else
        constexpr const char* kDevNull = " 2>/dev/null";
#endif
        const auto cliStatus = timeCommand("git -C \"" + workdir.string() +
                                           "\" status --porcelain=v2" + kDevNull);
        if (cliStatus.ok) {
            record("status (git CLI)", cliStatus.ms, std::to_string(cliStatus.lines) + " lines");
        }

        // --- diffs -------------------------------------------------------------
        git_oid headOid{};
        gity::git::check(git_reference_name_to_id(&headOid, repo.get(), "HEAD"), "resolve HEAD");

        gity::git::CommitHandle head;
        gity::git::check(git_commit_lookup(head.receive(), repo.get(), &headOid), "look up HEAD");

        gity::git::TreeHandle headTree;
        gity::git::check(git_commit_tree(headTree.receive(), head.get()), "read the HEAD tree");

        if (git_commit_parentcount(head.get()) > 0) {
            gity::git::CommitHandle parent;
            gity::git::check(git_commit_parent(parent.receive(), head.get(), 0),
                           "look up the first parent");
            gity::git::TreeHandle parentTree;
            gity::git::check(git_commit_tree(parentTree.receive(), parent.get()),
                           "read the parent tree");

            watch.reset();
            gity::git::DiffHandle diff;
            gity::git::check(git_diff_tree_to_tree(diff.receive(), repo.get(), parentTree.get(),
                                                 headTree.get(), nullptr),
                           "diff HEAD~1 against HEAD");
            const double diffMs = watch.elapsedMs();
            record("diff HEAD~1..HEAD", diffMs,
                   std::to_string(git_diff_num_deltas(diff.get())) + " deltas");
        }

        watch.reset();
        gity::git::DiffHandle workdirDiff;
        gity::git::check(git_diff_tree_to_workdir_with_index(workdirDiff.receive(), repo.get(),
                                                           headTree.get(), nullptr),
                       "diff HEAD against the worktree");
        const double workdirMs = watch.elapsedMs();
        record("diff HEAD..worktree", workdirMs,
               std::to_string(git_diff_num_deltas(workdirDiff.get())) + " deltas");

        // --- report -------------------------------------------------------------
        std::printf("\n  Repository ....... %s\n", workdir.string().c_str());
        std::printf("  Commits .......... %zu\n", commits);
        std::printf("  Object cache ..... %s\n", cacheNote.c_str());
        std::printf("  Batch size ....... %zu rows/chunk\n", streamOptions.batchSize);
        std::printf("  commit-graph ..... %s\n",
                    std::filesystem::exists(workdir / ".git" / "objects" / "info" /
                                            "commit-graph")
                        ? "present"
                        : "absent");
        if (stats.cancelled) {
            std::printf("  Stream ........... cancelled\n");
        }
        printTable();

        if (const std::size_t rss = peakRssKib(); rss > 0) {
            std::printf("  Peak RSS ......... %zu MiB (ADR budget: 350 MiB at 1M commits)\n",
                        rss / 1024);
        }
        if (cliStatus.ok && statusMs > 0.0) {
            const double ratio = cliStatus.ms / statusMs;
            std::printf("  status ratio ..... libgit2 is %.2fx %s than the CLI here\n",
                        ratio >= 1.0 ? ratio : 1.0 / ratio,
                        ratio >= 1.0 ? "faster" : "SLOWER");
        }
        std::printf("\n");

        return 0;
    } catch (const gity::git::GitException& e) {
        std::fprintf(stderr, "git error: %s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
