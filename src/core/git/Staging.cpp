#include "Staging.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace gity::git {
namespace {

/// Reads the index, runs `mutate`, writes it back. Every index change goes
/// through here so no path can forget the write.
template <typename Fn>
void withIndex(git_repository* repo, const char* what, Fn&& mutate) {
    IndexHandle index;
    check(git_repository_index(index.receive(), repo), what);
    mutate(index.get());
    check(git_index_write(index.get()), what);
}

/// Staging a deletion means removing the entry, not adding the missing file.
bool pathIsGone(git_repository* repo, const std::string& path) {
    const char* workdir = git_repository_workdir(repo);
    if (workdir == nullptr) {
        return false;
    }
    std::string full(workdir);
    full += path;
    // symlink_status, not exists(): exists() follows the link, so a symlink
    // whose target is missing would read as gone and be staged as a deletion
    // while it is still sitting in the worktree.
    std::error_code error;
    return !std::filesystem::exists(std::filesystem::symlink_status(full, error));
}

} // namespace

FileDiff loadWorkingDiff(git_repository* repo, const std::string& path, DiffSide side,
                         const FileDiffOptions& options) {
    FileDiff out;
    out.path = path;

    git_diff_options diffOptions;
    check(git_diff_options_init(&diffOptions, GIT_DIFF_OPTIONS_VERSION),
          "initialize diff options");
    diffOptions.context_lines = static_cast<std::uint32_t>(options.contextLines);

    char* pathspec = const_cast<char*>(path.c_str());
    diffOptions.pathspec.strings = &pathspec;
    diffOptions.pathspec.count = 1;
    // An untracked file has nothing to diff against unless it is included.
    //
    // RECURSE_UNTRACKED_DIRS is not optional here even though nothing recurses
    // in the usual sense: without it libgit2 reports untracked content
    // collapsed at the *directory* level, so a pathspec naming the file
    // matches nothing and a brand new file silently shows "no textual
    // changes". Measured: with a pathspec and without this flag, 0 deltas;
    // with it, 1. The status options next door already pair these two.
    diffOptions.flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT |
                         GIT_DIFF_RECURSE_UNTRACKED_DIRS;
    // A path, not a pattern. `[id].tsx` is an ordinary file name in several web
    // frameworks, and as a glob it also matches `d.tsx` — whose hunks would
    // then be shown, and staged, under this file's name.
    diffOptions.flags |= GIT_DIFF_DISABLE_PATHSPEC_MATCH;

    IndexHandle index;
    check(git_repository_index(index.receive(), repo), "read the index");

    DiffHandle diff;
    if (side == DiffSide::Unstaged) {
        check(git_diff_index_to_workdir(diff.receive(), repo, index.get(), &diffOptions),
              "diff the index against the worktree");
    } else if (side == DiffSide::Combined) {
        TreeHandle headTree;
        git_oid headOid{};
        if (git_reference_name_to_id(&headOid, repo, "HEAD") == 0) {
            CommitHandle head;
            check(git_commit_lookup(head.receive(), repo, &headOid), "look up HEAD");
            check(git_commit_tree(headTree.receive(), head.get()), "read the HEAD tree");
        }
        // _with_index rather than plain tree_to_workdir: without the index git
        // cannot tell a staged rename or deletion from an untracked file, and
        // the counts would be drawn from a picture the repository disagrees
        // with.
        check(git_diff_tree_to_workdir_with_index(diff.receive(), repo, headTree.get(),
                                                  &diffOptions),
              "diff HEAD against the worktree");
    } else {
        TreeHandle headTree;
        git_oid headOid{};
        if (git_reference_name_to_id(&headOid, repo, "HEAD") == 0) {
            CommitHandle head;
            check(git_commit_lookup(head.receive(), repo, &headOid), "look up HEAD");
            check(git_commit_tree(headTree.receive(), head.get()), "read the HEAD tree");
        }
        check(git_diff_tree_to_index(diff.receive(), repo, headTree.get(), index.get(),
                                     &diffOptions),
              "diff HEAD against the index");
    }

    if (git_diff_num_deltas(diff.get()) == 0) {
        return out;
    }

    PatchHandle patch;
    check(git_patch_from_diff(patch.receive(), diff.get(), 0), "build the patch");

    const std::size_t hunkCount = git_patch_num_hunks(patch.get());
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
            const git_diff_line* line = nullptr;
            if (git_patch_get_line_in_hunk(&line, patch.get(), h, l) < 0 || line == nullptr) {
                continue;
            }
            std::string_view content(line->content, line->content_len);
            while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
                content.remove_suffix(1);
            }

            DiffLine outLine;
            switch (line->origin) {
            case GIT_DIFF_LINE_ADDITION:
                outLine.kind = DiffLineKind::Addition;
                break;
            case GIT_DIFF_LINE_DELETION:
                outLine.kind = DiffLineKind::Deletion;
                break;
            case GIT_DIFF_LINE_ADD_EOFNL:
            case GIT_DIFF_LINE_DEL_EOFNL:
                outLine.kind = DiffLineKind::NoNewlineMarker;
                break;
            default:
                outLine.kind = DiffLineKind::Context;
                break;
            }
            outLine.oldLine =
                line->old_lineno > 0 ? static_cast<std::uint32_t>(line->old_lineno) : 0u;
            outLine.newLine =
                line->new_lineno > 0 ? static_cast<std::uint32_t>(line->new_lineno) : 0u;
            outLine.textOffset = static_cast<std::uint32_t>(out.text.size());
            outLine.textLength = static_cast<std::uint32_t>(content.size());
            out.text.append(content);
            out.lines.push_back(outLine);
        }

        outHunk.lineCount = static_cast<std::uint32_t>(out.lines.size()) - outHunk.firstLine;
        out.hunks.push_back(std::move(outHunk));
    }

    return out;
}

void stageFiles(git_repository* repo, const std::vector<std::string>& paths) {
    withIndex(repo, "stage files", [&](git_index* index) {
        for (const auto& path : paths) {
            if (pathIsGone(repo, path)) {
                check(git_index_remove_bypath(index, path.c_str()), "stage a deletion");
            } else {
                check(git_index_add_bypath(index, path.c_str()), "stage a file");
            }
        }
    });
}

void unstageFiles(git_repository* repo, const std::vector<std::string>& paths) {
    // Each path goes back to its HEAD version, or out of the index if HEAD
    // has none — which is what `git reset -- <path>` does. Done entry by entry
    // rather than through git_reset_default, because that takes a pathspec:
    // unstaging `[id].tsx` would unstage `d.tsx` along with it.
    TreeHandle headTree;
    git_oid headOid{};
    if (git_reference_name_to_id(&headOid, repo, "HEAD") == 0) {
        CommitHandle head;
        check(git_commit_lookup(head.receive(), repo, &headOid), "look up HEAD");
        check(git_commit_tree(headTree.receive(), head.get()), "read the HEAD tree");
    }

    withIndex(repo, "unstage files", [&](git_index* index) {
        for (const auto& path : paths) {
            // A conflict is a staged state too; unstaging leaves the file as
            // it is on disk with no three-way entries behind.
            static_cast<void>(git_index_conflict_remove(index, path.c_str()));

            git_tree_entry* raw = nullptr;
            if (!headTree || git_tree_entry_bypath(&raw, headTree.get(), path.c_str()) < 0) {
                static_cast<void>(git_index_remove_bypath(index, path.c_str()));
                continue;
            }
            const Handle<git_tree_entry, git_tree_entry_free> entry(raw);
            git_index_entry restored{};
            restored.mode = static_cast<std::uint32_t>(git_tree_entry_filemode(entry.get()));
            restored.id = *git_tree_entry_id(entry.get());
            restored.path = path.c_str();
            check(git_index_add(index, &restored), "unstage a file");
        }
    });
}

LineSelection linesOfHunk(const FileDiff& diff, std::size_t hunkIndex) {
    LineSelection selected;
    if (hunkIndex >= diff.hunks.size()) {
        return selected;
    }
    const DiffHunk& hunk = diff.hunks[hunkIndex];
    for (std::uint32_t i = 0; i < hunk.lineCount; ++i) {
        const std::uint32_t index = hunk.firstLine + i;
        const DiffLineKind kind = diff.lines[index].kind;
        if (kind == DiffLineKind::Addition || kind == DiffLineKind::Deletion) {
            selected.insert(index);
        }
    }
    return selected;
}

namespace {

/// Inverts a unified diff: additions become deletions, the two sides of every
/// hunk header swap, and context is untouched.
///
/// Reversing only the file headers — which is the tempting shortcut — produces
/// a patch that claims one direction and contains the other, and libgit2 will
/// apply it happily. Unstaging would then stage more.
std::string reversePatch(const std::string& forward, const std::string& oldPath,
                         const std::string& newPath) {
    std::istringstream in(forward);
    std::ostringstream out;
    std::string line;

    out << "diff --git a/" << newPath << " b/" << oldPath << '\n'
        << "--- a/" << newPath << '\n'
        << "+++ b/" << oldPath << '\n';

    while (std::getline(in, line)) {
        if (line.rfind("diff --git ", 0) == 0 || line.rfind("--- ", 0) == 0 ||
            line.rfind("+++ ", 0) == 0) {
            continue; // already written, swapped
        }
        if (line.rfind("@@ ", 0) == 0) {
            // "@@ -a,b +c,d @@" becomes "@@ -c,d +a,b @@"
            const auto minus = line.find('-');
            const auto plus = line.find('+', minus);
            const auto tail = line.find(" @@", plus);
            if (minus != std::string::npos && plus != std::string::npos &&
                tail != std::string::npos) {
                const std::string oldSide = line.substr(minus + 1, plus - minus - 2);
                const std::string newSide = line.substr(plus + 1, tail - plus - 1);
                out << "@@ -" << newSide << " +" << oldSide << " @@\n";
                continue;
            }
            out << line << '\n';
            continue;
        }
        if (!line.empty() && line[0] == '+') {
            out << '-' << line.substr(1) << '\n';
        } else if (!line.empty() && line[0] == '-') {
            out << '+' << line.substr(1) << '\n';
        } else {
            out << line << '\n';
        }
    }
    return out.str();
}

} // namespace

std::string buildPartialPatch(const FileDiff& diff, const LineSelection& selected, bool reverse) {
    if (diff.lines.empty()) {
        return {};
    }

    const std::string oldPath = diff.oldPath.empty() ? diff.path : diff.oldPath;

    std::ostringstream body;
    std::size_t emittedHunks = 0;

    for (const auto& hunk : diff.hunks) {
        std::ostringstream hunkBody;
        int oldCount = 0;
        int newCount = 0;
        bool anySelected = false;

        for (std::uint32_t i = 0; i < hunk.lineCount; ++i) {
            const std::uint32_t index = hunk.firstLine + i;
            const DiffLine& line = diff.lines[index];
            const std::string text(diff.lineText(line));
            const bool isSelected = selected.count(index) != 0;

            switch (line.kind) {
            case DiffLineKind::Context:
                hunkBody << ' ' << text << '\n';
                ++oldCount;
                ++newCount;
                break;

            case DiffLineKind::Addition:
                if (isSelected) {
                    hunkBody << '+' << text << '\n';
                    ++newCount;
                    anySelected = true;
                }
                // Unselected additions are simply absent: they are not part of
                // the result being built.
                break;

            case DiffLineKind::Deletion:
                if (isSelected) {
                    hunkBody << '-' << text << '\n';
                    ++oldCount;
                    anySelected = true;
                } else {
                    // Unselected deletions survive, so they become context on
                    // both sides. Dropping them instead would silently delete
                    // the line.
                    hunkBody << ' ' << text << '\n';
                    ++oldCount;
                    ++newCount;
                }
                break;

            case DiffLineKind::NoNewlineMarker:
                hunkBody << "\\ No newline at end of file\n";
                break;
            }
        }

        if (!anySelected) {
            continue; // a hunk with nothing chosen contributes nothing
        }

        body << "@@ -" << hunk.oldStart << ',' << oldCount << " +" << hunk.newStart << ','
             << newCount << " @@\n"
             << hunkBody.str();
        ++emittedHunks;
    }

    if (emittedHunks == 0) {
        return {};
    }

    // The "diff --git" line is not decoration: git_diff_from_buffer rejects a
    // patch without it ("invalid hunk header outside patch"). The unit tests
    // validated the intended format; only applying one to a real index found
    // the format libgit2 actually parses.
    std::ostringstream patch;
    patch << "diff --git a/" << oldPath << " b/" << diff.path << '\n'
          << "--- a/" << oldPath << '\n'
          << "+++ b/" << diff.path << '\n'
          << body.str();

    return reverse ? reversePatch(patch.str(), oldPath, diff.path) : patch.str();
}

namespace {

void applyToIndex(git_repository* repo, const std::string& patchText, const char* what) {
    if (patchText.empty()) {
        return;
    }

    DiffHandle diff;
    check(git_diff_from_buffer(diff.receive(), patchText.data(), patchText.size()),
          "parse the synthesized patch");

    git_apply_options options;
    check(git_apply_options_init(&options, GIT_APPLY_OPTIONS_VERSION),
          "initialize apply options");

    // GIT_APPLY_LOCATION_INDEX is what makes this staging rather than a
    // worktree edit: the file on disk is untouched. Direction is already baked
    // into the patch text by buildPartialPatch.
    check(git_apply(repo, diff.get(), GIT_APPLY_LOCATION_INDEX, &options), what);
}

} // namespace

void applyPatchToIndex(git_repository* repo, const std::string& patchText) {
    applyToIndex(repo, patchText, "apply the patch to the index");
}

void applyPatchToWorktree(git_repository* repo, const std::string& patchText) {
    if (patchText.empty()) {
        return;
    }
    DiffHandle diff;
    check(git_diff_from_buffer(diff.receive(), patchText.data(), patchText.size()),
          "parse the synthesized patch");

    git_apply_options options;
    check(git_apply_options_init(&options, GIT_APPLY_OPTIONS_VERSION),
          "initialize apply options");
    // The worktree alone. The patch is the reverse of an *unstaged* hunk —
    // index against worktree — so the index never held these lines, and
    // asking for both locations made every discard fail to apply.
    check(git_apply(repo, diff.get(), GIT_APPLY_LOCATION_WORKDIR, &options),
          "discard the selected changes");
}

} // namespace gity::git
