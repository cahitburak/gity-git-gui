#include "Compare.h"

namespace gity::git {
namespace {

TreeHandle treeOf(git_repository* repo, const git_oid& oid) {
    CommitHandle commit;
    check(git_commit_lookup(commit.receive(), repo, &oid), "look up a commit to compare");
    TreeHandle tree;
    check(git_commit_tree(tree.receive(), commit.get()), "read a tree to compare");
    return tree;
}

/// Commits reachable from `include` and not from `exclude`, newest first,
/// listed up to `limit` and counted in full.
std::size_t walkOnlyIn(git_repository* repo, const git_oid& include, const git_oid& exclude,
                       std::size_t limit, std::vector<ComparedCommit>& out) {
    RevwalkHandle walk;
    check(git_revwalk_new(walk.receive(), repo), "create a revwalk");
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    check(git_revwalk_push(walk.get(), &include), "start the comparison walk");
    check(git_revwalk_hide(walk.get(), &exclude), "bound the comparison walk");

    std::size_t count = 0;
    git_oid oid{};
    while (git_revwalk_next(&oid, walk.get()) == 0) {
        if (count < limit) {
            ComparedCommit entry;
            entry.oid = oid;
            CommitHandle commit;
            if (git_commit_lookup(commit.receive(), repo, &oid) == 0) {
                const char* summary = git_commit_summary(commit.get());
                entry.summary = summary != nullptr ? summary : "";
            }
            out.push_back(std::move(entry));
        }
        ++count;
    }
    return count;
}

} // namespace

Comparison loadComparison(git_repository* repo, const git_oid& from, const git_oid& to,
                          const CompareOptions& options) {
    Comparison out;
    git_oid_cpy(&out.from, &from);
    git_oid_cpy(&out.to, &to);

    const TreeHandle fromTree = treeOf(repo, from);
    const TreeHandle toTree = treeOf(repo, to);
    out.files = listChangedFiles(repo, fromTree.get(), toTree.get(), options.files,
                                 &out.filesTruncated);

    out.hasMergeBase = git_merge_base(&out.mergeBase, repo, &from, &to) == 0;
    out.onlyInTo = walkOnlyIn(repo, to, from, options.maxCommits, out.onlyInToCommits);
    out.onlyInFrom = walkOnlyIn(repo, from, to, options.maxCommits, out.onlyInFromCommits);
    return out;
}

} // namespace gity::git
