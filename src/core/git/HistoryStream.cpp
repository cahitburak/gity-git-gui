#include "HistoryStream.h"

#include "../graph/LaneAssigner.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <unordered_map>

namespace gity::git {

bool commitMatches(std::string_view summary, std::string_view author, std::string_view idHex,
                   std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    const auto containsFold = [](std::string_view haystack, std::string_view needle) {
        if (needle.size() > haystack.size()) {
            return false;
        }
        const auto fold = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                           [&](char a, char b) {
                               return fold(static_cast<unsigned char>(a)) ==
                                      fold(static_cast<unsigned char>(b));
                           }) != haystack.end();
    };
    // Id matched as a prefix rather than anywhere: a hex fragment appearing in
    // the middle of an unrelated hash is a coincidence, not a match.
    return containsFold(summary, filter) || containsFold(author, filter) ||
           (idHex.size() >= filter.size() && containsFold(idHex.substr(0, filter.size()), filter));
}

bool CommitFilter::matches(std::string_view summary, std::string_view authorName,
                           std::string_view idHex, std::int64_t when) const {
    if (since != 0 && when < since) {
        return false;
    }
    if (until != 0 && when > until) {
        return false;
    }
    if (!author.empty() && !commitMatches({}, authorName, {}, author)) {
        return false;
    }
    return text.empty() || commitMatches(summary, authorName, idHex, text);
}

namespace {

/// What `path` names in a tree: its id — a blob, a folder's tree, a
/// submodule's commit — or zero when there is nothing there, its kind, and
/// its mode.
struct Entry {
    git_oid id{};
    git_object_t type = GIT_OBJECT_INVALID;
    git_filemode_t mode = GIT_FILEMODE_UNREADABLE;

    [[nodiscard]] bool present() const noexcept { return git_oid_is_zero(&id) == 0; }

    /// The same content with the same mode. The mode counts, as it does for
    /// git: a commit that only made a file executable changed it.
    [[nodiscard]] bool same(const Entry& other) const noexcept {
        return git_oid_equal(&id, &other.id) != 0 && mode == other.mode;
    }
};

Entry entryAt(git_tree* tree, const std::string& path) {
    Entry found;
    git_tree_entry* entry = nullptr;
    const int rc = git_tree_entry_bypath(&entry, tree, path.c_str());
    if (rc == 0) {
        git_oid_cpy(&found.id, git_tree_entry_id(entry));
        found.type = git_tree_entry_type(entry);
        found.mode = git_tree_entry_filemode(entry);
        git_tree_entry_free(entry);
    } else if (rc != GIT_ENOTFOUND) {
        throwLastError(rc, "look up a path in a commit");
    }
    return found;
}

TreeHandle treeOf(git_commit* commit) {
    TreeHandle tree;
    check(git_commit_tree(tree.receive(), commit), "read a commit's tree");
    return tree;
}

/// The name `path` had in `parentTree`, when the change from it to `tree`
/// renamed a file to `path`; empty when nothing was renamed to it. The whole
/// change is looked at, since the file's old name could be anywhere; trees
/// the change leaves alone are skipped without being read.
std::string renamedTo(git_repository* repo, git_tree* parentTree, git_tree* tree,
                      const std::string& path) {
    DiffHandle diff;
    check(git_diff_tree_to_tree(diff.receive(), repo, parentTree, tree, nullptr),
          "compare a commit with its parent");
    git_diff_find_options find = GIT_DIFF_FIND_OPTIONS_INIT;
    find.flags = GIT_DIFF_FIND_RENAMES;
    check(git_diff_find_similar(diff.get(), &find), "find renamed files");
    const std::size_t count = git_diff_num_deltas(diff.get());
    for (std::size_t i = 0; i < count; ++i) {
        const git_diff_delta* delta = git_diff_get_delta(diff.get(), i);
        if (delta->status == GIT_DELTA_RENAMED && delta->new_file.path != nullptr &&
            delta->old_file.path != nullptr && path == delta->new_file.path) {
            return delta->old_file.path;
        }
    }
    return {};
}

} // namespace

PathChange pathChange(git_repository* repo, git_commit* commit, const std::string& path,
                      bool followRenames) {
    PathChange change;
    const TreeHandle tree = treeOf(commit);
    const Entry mine = entryAt(tree.get(), path);
    const unsigned int parents = git_commit_parentcount(commit);
    if (parents == 0) {
        change.touched = mine.present();
        return change;
    }
    // Only a file is followed; a folder that moved is a different folder.
    const bool follow = followRenames && mine.type == GIT_OBJECT_BLOB;
    if (followRenames) {
        change.renamedFrom.resize(parents);
        change.inParent.resize(parents);
    }
    change.touched = true;
    for (unsigned int p = 0; p < parents; ++p) {
        CommitHandle parent;
        check(git_commit_lookup(parent.receive(), repo, git_commit_parent_id(commit, p)),
              "look up a parent commit");
        const TreeHandle parentTree = treeOf(parent.get());
        const Entry theirs = entryAt(parentTree.get(), path);
        if (followRenames) {
            change.inParent[p] = theirs.present();
        }
        if (mine.same(theirs)) {
            // The same as one parent: this commit did not change it. The
            // other parents are still asked what the file was called there,
            // since the walk goes down every side.
            change.touched = false;
            if (!followRenames) {
                return change;
            }
            continue;
        }
        if (follow && !theirs.present()) {
            change.renamedFrom[p] = renamedTo(repo, parentTree.get(), tree.get(), path);
        }
    }
    return change;
}

bool commitTouchesPath(git_repository* repo, git_commit* commit, const std::string& path) {
    return pathChange(repo, commit, path, false).touched;
}

namespace {

/// The name a followed file has in each commit the walk has yet to reach.
/// The walk is topological, so every child of a commit is visited before it:
/// a commit is told its name by its children, and forgets it once visited,
/// so only the walk's frontier is held. Names are kept once and referred to
/// by index, since nearly every commit shares one.
class FollowedNames {
public:
    explicit FollowedNames(std::string path) { names_.push_back(std::move(path)); }

    /// The name in `id`, which is being visited now. Tips have the name asked
    /// for.
    std::uint32_t take(const git_oid& id) {
        const auto found = at_.find(keyFromOid(id));
        if (found == at_.end()) {
            return 0;
        }
        const std::uint32_t name = found->second.name;
        at_.erase(found);
        return name;
    }

    /// Tells `parent` the file's name there, `known` when the parent has a
    /// file by that name. Children on other branches pass on whatever name
    /// they were given, whether the file exists there or not; the first child
    /// to reach a commit is the newest, not the one that knows. So a known
    /// name replaces a guess, and otherwise the first answer stands.
    void expect(const git_oid& parent, std::uint32_t name, bool known) {
        const auto [it, added] = at_.try_emplace(keyFromOid(parent), Expected{name, known});
        if (!added && known && !it->second.known) {
            it->second = Expected{name, known};
        }
    }

    std::uint32_t intern(std::string name) {
        const auto known = std::find(names_.begin(), names_.end(), name);
        if (known != names_.end()) {
            return static_cast<std::uint32_t>(known - names_.begin());
        }
        names_.push_back(std::move(name));
        return static_cast<std::uint32_t>(names_.size() - 1);
    }

    [[nodiscard]] const std::string& name(std::uint32_t index) const { return names_[index]; }

private:
    struct Expected {
        std::uint32_t name = 0;
        bool known = false;
    };

    std::vector<std::string> names_;
    std::unordered_map<graph::CommitKey, Expected> at_;
};

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// push_glob reports ENOTFOUND for a namespace with no refs; that is not an
/// error, it just means the repository has no remotes or no branches.
void pushGlobIfPresent(git_revwalk* walk, const char* glob) {
    const int rc = git_revwalk_push_glob(walk, glob);
    if (rc < 0 && rc != GIT_ENOTFOUND) {
        throwLastError(rc, "push refs onto the revwalk");
    }
}

std::string_view summaryOf(git_commit* commit) {
    const char* summary = git_commit_summary(commit);
    return (summary != nullptr) ? std::string_view(summary) : std::string_view();
}

std::string_view authorOf(git_commit* commit) {
    const git_signature* author = git_commit_author(commit);
    if (author == nullptr || author->name == nullptr) {
        return {};
    }
    return {author->name};
}

} // namespace

StreamStats streamHistory(git_repository* repo, const StreamOptions& options,
                          const ChunkSink& sink) {
    const auto started = Clock::now();
    StreamStats stats;

    RevwalkHandle walk;
    check(git_revwalk_new(walk.receive(), repo), "create a revwalk");
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    pushGlobIfPresent(walk.get(), "refs/heads/*");
    pushGlobIfPresent(walk.get(), "refs/remotes/*");
    // HEAD as well, for when it is on no branch: a rebase in progress, a
    // checked-out tag, commits made while detached. None of those are
    // reachable from a branch, and leaving HEAD out made the commit you are
    // standing on missing from the graph. An unborn HEAD has nothing to push.
    if (const int head = git_revwalk_push_head(walk.get());
        head < 0 && head != GIT_ENOTFOUND && head != GIT_EUNBORNBRANCH) {
        throwLastError(head, "push HEAD onto the revwalk");
    }

    // Slot state persists across chunks; that is what makes streaming and
    // whole-table assignment produce identical graphs.
    graph::LaneAssigner assigner;

    model::HistoryChunk chunk;
    chunk.commits.reserve(options.batchSize);
    chunk.graph.rows.reserve(options.batchSize);
    chunk.graph.edges.reserve(options.batchSize * 3);

    std::vector<graph::CommitKey> parentKeys;
    std::size_t globalRow = 0;
    bool firstChunkEmitted = false;

    const auto emit = [&](bool final) -> bool {
        if (chunk.graph.rows.empty() && !final) {
            return true;
        }
        if (!firstChunkEmitted) {
            stats.firstChunkMs = msSince(started);
            firstChunkEmitted = true;
        }
        const std::size_t emittedRows = chunk.graph.rows.size();
        model::HistoryChunk out = std::move(chunk);

        chunk = model::HistoryChunk{};
        chunk.firstRow = globalRow;
        chunk.commits.reserve(options.batchSize);
        chunk.graph.rows.reserve(options.batchSize);
        chunk.graph.edges.reserve(options.batchSize * 3);

        if (emittedRows == 0) {
            return true;
        }
        return sink(std::move(out));
    };

    const bool filtering = options.filter.active();
    const bool byPath = !options.filter.path.empty();
    FollowedNames followed(options.filter.path);
    char oidHex[GIT_OID_SHA1_HEXSIZE + 1] = {};

    git_oid oid;
    int rc = 0;
    std::size_t visited = 0;
    while ((rc = git_revwalk_next(&oid, walk.get())) == 0) {
        if ((++visited & 0x3FF) == 0 && options.cancel != nullptr &&
            options.cancel->load(std::memory_order_relaxed)) {
            stats.cancelled = true;
            stats.totalMs = msSince(started);
            return stats;
        }
        CommitHandle commit;
        check(git_commit_lookup(commit.receive(), repo, &oid), "look up a commit");

        const auto summary = summaryOf(commit.get());
        const auto author = authorOf(commit.get());
        std::uint32_t name = 0;
        if (byPath) {
            name = followed.take(oid);
            const PathChange change =
                pathChange(repo, commit.get(), followed.name(name), options.filter.followRenames);
            const unsigned int parents = git_commit_parentcount(commit.get());
            for (unsigned int p = 0; p < parents; ++p) {
                const bool renamed =
                    p < change.renamedFrom.size() && !change.renamedFrom[p].empty();
                const bool known = renamed || (p < change.inParent.size() && change.inParent[p]);
                followed.expect(*git_commit_parent_id(commit.get(), p),
                                renamed ? followed.intern(change.renamedFrom[p]) : name, known);
            }
            if (!change.touched) {
                continue;
            }
        }
        if (filtering) {
            git_oid_tostr(oidHex, sizeof(oidHex), &oid);
            if (!options.filter.matches(summary, author, oidHex,
                                        static_cast<std::int64_t>(git_commit_time(commit.get())))) {
                continue;
            }
        }

        parentKeys.clear();
        const unsigned int parentCount = git_commit_parentcount(commit.get());
        for (unsigned int p = 0; p < parentCount; ++p) {
            parentKeys.push_back(keyFromOid(*git_commit_parent_id(commit.get(), p)));
        }

        if (filtering) {
            // One lane, no edges. The commits between two matches were skipped,
            // so any line drawn between them would join commits that are not
            // adjacent in the real history.
            graph::GraphRow row{};
            row.commitIndex = static_cast<graph::CommitIndex>(chunk.graph.rows.size());
            row.lane = 0;
            row.laneCount = 1;
            chunk.graph.rows.push_back(row);
        } else {
            assigner.append(keyFromOid(oid), parentKeys, chunk.graph);
        }
        const model::CommitIndex local = chunk.commits.append(
            oid, summary, author, static_cast<std::int64_t>(git_commit_time(commit.get())));
        if (name != 0) {
            chunk.renamedPaths.emplace_back(local, followed.name(name));
        }

        ++globalRow;
        ++stats.commits;

        if (chunk.graph.rows.size() >= options.batchSize) {
            if (!emit(false)) {
                stats.cancelled = true;
                stats.totalMs = msSince(started);
                return stats;
            }
        }
    }

    if (rc != GIT_ITEROVER && rc < 0) {
        throwLastError(rc, "walk the commit history");
    }

    if (!emit(true)) {
        stats.cancelled = true;
    }

    stats.totalMs = msSince(started);
    if (!firstChunkEmitted) {
        stats.firstChunkMs = stats.totalMs;
    }
    return stats;
}

} // namespace gity::git
