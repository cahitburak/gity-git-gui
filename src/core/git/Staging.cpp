#include "Staging.h"

#include <algorithm>
#include <sstream>

namespace gity::git {

FileDiff loadWorkingDiff(git_repository* repo, const std::string& path, DiffSide side,
                         const FileDiffOptions& options) {
    FileDiff out;
    out.path = path;

    git_diff_options diffOptions;
    check(git_diff_options_init(&diffOptions, GIT_DIFF_OPTIONS_VERSION), "initialize diff options");
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
        check(
            git_diff_tree_to_workdir_with_index(diff.receive(), repo, headTree.get(), &diffOptions),
            "diff HEAD against the worktree");
    } else {
        TreeHandle headTree;
        git_oid headOid{};
        if (git_reference_name_to_id(&headOid, repo, "HEAD") == 0) {
            CommitHandle head;
            check(git_commit_lookup(head.receive(), repo, &headOid), "look up HEAD");
            check(git_commit_tree(headTree.receive(), head.get()), "read the HEAD tree");
        }
        check(
            git_diff_tree_to_index(diff.receive(), repo, headTree.get(), index.get(), &diffOptions),
            "diff HEAD against the index");
    }

    if (git_diff_num_deltas(diff.get()) == 0) {
        return out;
    }

    PatchHandle patch;
    check(git_patch_from_diff(patch.receive(), diff.get(), 0), "build the patch");
    readPatch(patch.get(), out, options.maxLines, options.readSources);
    out.path = path;
    if (out.status != ChangeStatus::Renamed) {
        out.oldPath.clear();
    }

    return out;
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

/// One line of patch body, with the line ending the file actually uses.
void emitLine(std::ostringstream& out, char marker, std::string_view text, bool carriageReturn) {
    out << marker << text;
    if (carriageReturn) {
        out << '\r';
    }
    out << '\n';
}

/// The `@@` header for a hunk of `oldCount` and `newCount` lines whose old
/// side starts at `oldStart`, `offset` lines after the hunks before it moved
/// things. An empty side names the line *before* where it would be, which is
/// the unified-diff convention git checks.
std::string hunkHeader(int oldStart, int oldCount, int newCount, int offset) {
    int newStart = oldStart + offset;
    if (oldCount == 0 && newCount > 0) {
        ++newStart;
    } else if (newCount == 0 && oldCount > 0) {
        newStart = std::max(0, newStart - 1);
    }
    std::ostringstream header;
    header << "@@ -" << oldStart << ',' << oldCount << " +" << newStart << ',' << newCount
           << " @@\n";
    return header.str();
}

std::string octal(std::uint32_t mode) {
    std::ostringstream out;
    out << std::oct << mode;
    return out.str();
}

} // namespace

std::string buildPartialPatch(const FileDiff& diff, const LineSelection& selected, bool reverse) {
    if (diff.lines.empty()) {
        return {};
    }

    // The patch is applied to one side of the diff and produces the other with
    // only the selected changes made. Staging applies to the old side (the
    // index) and moves towards the new; unstaging and discarding apply to the
    // new side (the index, or the worktree) and move back.
    //
    // Which lines survive differs between the two, and is not a mirror image
    // of one patch: going forward, an unselected addition is not there yet and
    // an unselected deletion still is; going back, an unselected addition is
    // still there and an unselected deletion is already gone. Building the
    // forward patch and inverting it gets this wrong whenever some but not all
    // of a hunk is selected — the patch then expects lines the target lacks.
    std::ostringstream body;
    std::size_t emittedHunks = 0;
    int offset = 0;

    for (const auto& hunk : diff.hunks) {
        std::ostringstream hunkBody;
        int oldCount = 0;
        int newCount = 0;
        bool anySelected = false;
        bool previousEmitted = false;

        for (std::uint32_t i = 0; i < hunk.lineCount; ++i) {
            const std::uint32_t index = hunk.firstLine + i;
            const DiffLine& line = diff.lines[index];
            const std::string_view text = diff.lineText(line);
            const bool isSelected = selected.count(index) != 0;

            // What this line is in the patch being built: '-' removed from the
            // target, '+' added to it, ' ' kept, or nothing at all.
            char marker = 0;
            switch (line.kind) {
            case DiffLineKind::Context:
                marker = ' ';
                break;
            case DiffLineKind::Addition:
                if (!reverse) {
                    marker = isSelected ? '+' : 0;
                } else {
                    marker = isSelected ? '-' : ' ';
                }
                break;
            case DiffLineKind::Deletion:
                if (!reverse) {
                    marker = isSelected ? '-' : ' ';
                } else {
                    marker = isSelected ? '+' : 0;
                }
                break;
            case DiffLineKind::NoNewlineMarker:
                // It describes the line before it, so it goes wherever that
                // line went.
                if (previousEmitted) {
                    hunkBody << "\\ No newline at end of file\n";
                }
                continue;
            }

            previousEmitted = marker != 0;
            if (marker == 0) {
                continue;
            }
            emitLine(hunkBody, marker, text, line.carriageReturn);
            if (marker != '+') {
                ++oldCount;
            }
            if (marker != '-') {
                ++newCount;
            }
            if (marker != ' ') {
                anySelected = true;
            }
        }

        if (!anySelected) {
            continue; // a hunk with nothing chosen contributes nothing
        }

        const int oldStart = reverse ? hunk.newStart : hunk.oldStart;
        body << hunkHeader(oldStart, oldCount, newCount, offset) << hunkBody.str();
        offset += newCount - oldCount;
        ++emittedHunks;
    }

    if (emittedHunks == 0) {
        return {};
    }

    // The side the patch applies to may not exist yet: staging lines of an
    // untracked file creates it in the index, and unstaging lines of a staged
    // deletion puts it back. git refuses a patch against a missing file unless
    // the header says it creates one.
    const std::uint32_t targetMode = reverse ? diff.newMode : diff.oldMode;
    const std::uint32_t resultMode = reverse ? diff.oldMode : diff.newMode;
    const bool creates = targetMode == 0 && resultMode != 0;

    // Both sides are named by the file's current path. For a staged rename the
    // index already holds it there; naming the old path on one side would make
    // the patch a rename of its own.
    std::ostringstream patch;
    patch << "diff --git a/" << diff.path << " b/" << diff.path << '\n';
    if (creates) {
        patch << "new file mode " << octal(resultMode) << '\n' << "--- /dev/null\n";
    } else {
        patch << "--- a/" << diff.path << '\n';
    }
    patch << "+++ b/" << diff.path << '\n' << body.str();
    return patch.str();
}

} // namespace gity::git
