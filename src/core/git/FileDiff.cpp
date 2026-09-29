#include "FileDiff.h"

#include <algorithm>
#include <cstring>

namespace gity::git {
namespace {



DiffLineKind kindOf(char origin) noexcept {
    switch (origin) {
    case GIT_DIFF_LINE_ADDITION:
        return DiffLineKind::Addition;
    case GIT_DIFF_LINE_DELETION:
        return DiffLineKind::Deletion;
    case GIT_DIFF_LINE_ADD_EOFNL:
    case GIT_DIFF_LINE_DEL_EOFNL:
        return DiffLineKind::NoNewlineMarker;
    default:
        return DiffLineKind::Context;
    }
}

/// libgit2 hands back line content with its trailing newline. The view draws
/// one row per line and adds its own spacing, so the terminator is stripped
/// here rather than at every paint.
std::string_view withoutTerminator(const char* content, std::size_t length) noexcept {
    std::string_view text(content, length);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

ChangeStatus statusOf(git_delta_t delta) noexcept {
    switch (delta) {
    case GIT_DELTA_ADDED:
        return ChangeStatus::Added;
    case GIT_DELTA_MODIFIED:
        return ChangeStatus::Modified;
    case GIT_DELTA_DELETED:
        return ChangeStatus::Deleted;
    case GIT_DELTA_RENAMED:
        return ChangeStatus::Renamed;
    case GIT_DELTA_COPIED:
        return ChangeStatus::Copied;
    case GIT_DELTA_TYPECHANGE:
        return ChangeStatus::TypeChanged;
    default:
        return ChangeStatus::Unknown;
    }
}

} // namespace

std::size_t FileDiff::additions() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        lines.begin(), lines.end(),
        [](const DiffLine& line) { return line.kind == DiffLineKind::Addition; }));
}

std::size_t FileDiff::deletions() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        lines.begin(), lines.end(),
        [](const DiffLine& line) { return line.kind == DiffLineKind::Deletion; }));
}

namespace {

TreeHandle treeOf(git_repository* repo, const git_oid& commitOid) {
    CommitHandle commit;
    check(git_commit_lookup(commit.receive(), repo, &commitOid), "look up the commit to diff");
    TreeHandle tree;
    check(git_commit_tree(tree.receive(), commit.get()), "read the commit tree");
    return tree;
}

/// One path between two trees. `parentTree` may be null — a root commit, or
/// a comparison against nothing — and then everything is an addition.
FileDiff diffTrees(git_repository* repo, const TreeHandle& parentTree, const TreeHandle& tree,
                   const std::string& path, const FileDiffOptions& options) {
    FileDiff out;
    out.path = path;

    git_diff_options diffOptions;
    check(git_diff_options_init(&diffOptions, GIT_DIFF_OPTIONS_VERSION),
          "initialize diff options");
    diffOptions.context_lines = static_cast<std::uint32_t>(options.contextLines);

    // Restrict to the one path. Diffing the whole tree and discarding the rest
    // would be wasteful on a merge that touched thousands of files.
    char* pathspec = const_cast<char*>(path.c_str());
    diffOptions.pathspec.strings = &pathspec;
    diffOptions.pathspec.count = 1;
    // A path, not a pattern: `[id].tsx` read as a glob matches `d.tsx`, and the
    // pane would show that file's diff under this one's name.
    diffOptions.flags |= GIT_DIFF_DISABLE_PATHSPEC_MATCH;

    DiffHandle diff;
    check(git_diff_tree_to_tree(diff.receive(), repo, parentTree.get(), tree.get(), &diffOptions),
          "diff the selected file");

    if (git_diff_num_deltas(diff.get()) == 0) {
        return out;
    }

    // An added file may be the new half of a rename, which one path cannot
    // show: the file list detects renames, and without doing the same here a
    // file it calls "renamed" opened as its whole content added. Checked only
    // for additions, because rename detection needs the whole commit's diff.
    std::size_t deltaIndex = 0;
    if (const git_diff_delta* first = git_diff_get_delta(diff.get(), 0);
        first != nullptr && first->status == GIT_DELTA_ADDED && parentTree) {
        git_diff_options wholeOptions;
        check(git_diff_options_init(&wholeOptions, GIT_DIFF_OPTIONS_VERSION),
              "initialize diff options");
        wholeOptions.context_lines = diffOptions.context_lines;
        DiffHandle whole;
        git_diff_find_options findOptions;
        if (git_diff_tree_to_tree(whole.receive(), repo, parentTree.get(), tree.get(),
                                  &wholeOptions) == 0 &&
            git_diff_find_options_init(&findOptions, GIT_DIFF_FIND_OPTIONS_VERSION) == 0) {
            findOptions.flags = GIT_DIFF_FIND_RENAMES;
            if (git_diff_find_similar(whole.get(), &findOptions) == 0) {
                const std::size_t count = git_diff_num_deltas(whole.get());
                for (std::size_t i = 0; i < count; ++i) {
                    const git_diff_delta* delta = git_diff_get_delta(whole.get(), i);
                    if (delta != nullptr && delta->status == GIT_DELTA_RENAMED &&
                        delta->new_file.path != nullptr && path == delta->new_file.path) {
                        diff = std::move(whole);
                        deltaIndex = i;
                        break;
                    }
                }
            }
        }
    }

    if (const git_diff_delta* delta = git_diff_get_delta(diff.get(), deltaIndex);
        delta != nullptr) {
        out.status = statusOf(delta->status);
        out.binary = (delta->flags & GIT_DIFF_FLAG_BINARY) != 0;
        if (delta->old_file.path != nullptr && out.status == ChangeStatus::Renamed) {
            out.oldPath = delta->old_file.path;
        }
    }

    PatchHandle patch;
    check(git_patch_from_diff(patch.receive(), diff.get(), deltaIndex), "build the patch");

    const std::size_t hunkCount = git_patch_num_hunks(patch.get());
    out.hunks.reserve(hunkCount);

    for (std::size_t h = 0; h < hunkCount; ++h) {
        const git_diff_hunk* hunk = nullptr;
        std::size_t linesInHunk = 0;
        if (git_patch_get_hunk(&hunk, &linesInHunk, patch.get(), h) < 0 || hunk == nullptr) {
            continue;
        }

        DiffHunk outHunk;
        outHunk.header = std::string(hunk->header, strnlen(hunk->header, sizeof(hunk->header)));
        while (!outHunk.header.empty() &&
               (outHunk.header.back() == '\n' || outHunk.header.back() == '\r')) {
            outHunk.header.pop_back();
        }
        outHunk.oldStart = hunk->old_start;
        outHunk.oldCount = hunk->old_lines;
        outHunk.newStart = hunk->new_start;
        outHunk.newCount = hunk->new_lines;
        outHunk.firstLine = static_cast<std::uint32_t>(out.lines.size());

        for (std::size_t l = 0; l < linesInHunk; ++l) {
            if (out.lines.size() >= options.maxLines) {
                out.tooLarge = true;
                break;
            }
            const git_diff_line* line = nullptr;
            if (git_patch_get_line_in_hunk(&line, patch.get(), h, l) < 0 || line == nullptr) {
                continue;
            }

            const std::string_view content = withoutTerminator(line->content, line->content_len);

            DiffLine outLine;
            outLine.kind = kindOf(line->origin);
            outLine.oldLine = line->old_lineno > 0 ? static_cast<std::uint32_t>(line->old_lineno)
                                                   : 0u;
            outLine.newLine = line->new_lineno > 0 ? static_cast<std::uint32_t>(line->new_lineno)
                                                   : 0u;
            outLine.textOffset = static_cast<std::uint32_t>(out.text.size());
            outLine.textLength = static_cast<std::uint32_t>(content.size());
            out.text.append(content);
            out.lines.push_back(outLine);
        }

        outHunk.lineCount = static_cast<std::uint32_t>(out.lines.size()) - outHunk.firstLine;
        out.hunks.push_back(std::move(outHunk));

        if (out.tooLarge) {
            break;
        }
    }

    return out;
}

} // namespace

FileDiff loadFileDiff(git_repository* repo, const git_oid& commitOid, const std::string& path,
                      const FileDiffOptions& options) {
    CommitHandle commit;
    check(git_commit_lookup(commit.receive(), repo, &commitOid), "look up the commit to diff");
    TreeHandle tree;
    check(git_commit_tree(tree.receive(), commit.get()), "read the commit tree");

    TreeHandle parentTree;
    if (git_commit_parentcount(commit.get()) > 0) {
        CommitHandle parent;
        check(git_commit_parent(parent.receive(), commit.get(), 0), "look up the first parent");
        check(git_commit_tree(parentTree.receive(), parent.get()), "read the parent tree");
    }
    return diffTrees(repo, parentTree, tree, path, options);
}

FileDiff loadFileDiffBetween(git_repository* repo, const git_oid& fromOid, const git_oid& toOid,
                             const std::string& path, const FileDiffOptions& options) {
    return diffTrees(repo, treeOf(repo, fromOid), treeOf(repo, toOid), path, options);
}

} // namespace gity::git
