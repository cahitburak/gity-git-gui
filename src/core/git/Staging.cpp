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
    check(git_repository_index(index.receive(), repo), "open the index");
    // The index on disk, not the one the repository handle read when it was
    // first asked: every write to it is made by the git binary. Given an index,
    // libgit2's diffs use it as it is; only a status refreshes the shared one.
    // Not forced, so an index that has not changed is not read again.
    check(git_index_read(index.get(), 0), "read the index");

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

constexpr std::string_view kNoNewline = "\\ No newline at end of file\n";

/// A line of a hunk being built: ' ' kept, '-' removed from the target, '+'
/// added to it.
struct PatchLine {
    char marker = ' ';
    std::string_view text;
    bool carriageReturn = false;
    /// The line ends its file without a newline, on the side it came from.
    bool noNewline = false;
};

/// Writes a hunk's lines. A "\ No newline at end of file" applies to the line
/// before it on each side that line is on, so it belongs only after the last
/// line of a side, and only when that line ended its file without one.
///
/// Which line is last can change with the selection. A line kept as context
/// that ends the target without a newline may have selected additions after
/// it, and then it ends the target but not the result: it is written removed
/// without the newline and added back with one, as git itself writes it.
void emitHunk(std::ostringstream& out, const std::vector<PatchLine>& lines) {
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t lastOld = kNone;
    std::size_t lastNew = kNone;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].marker != '+') {
            lastOld = i;
        }
        if (lines[i].marker != '-') {
            lastNew = i;
        }
    }
    const PatchLine* endsResult = nullptr;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const PatchLine& line = lines[i];
        const bool bareOld = i == lastOld && line.noNewline;
        const bool bareNew = i == lastNew && line.noNewline;
        if (line.marker == ' ' && bareOld != bareNew) {
            emitLine(out, '-', line.text, line.carriageReturn);
            if (bareOld) {
                out << kNoNewline;
                emitLine(out, '+', line.text, line.carriageReturn);
            } else {
                // The last line of the result, so written after everything
                // else; only removals can follow it.
                endsResult = &line;
            }
            continue;
        }
        emitLine(out, line.marker, line.text, line.carriageReturn);
        if (bareOld || bareNew) {
            out << kNoNewline;
        }
    }
    if (endsResult != nullptr) {
        emitLine(out, '+', endsResult->text, endsResult->carriageReturn);
        out << kNoNewline;
    }
}

/// `prefix` and `path` as a patch header names them: as they are, or quoted
/// in C style as git quotes a name holding a control character, a quote or a
/// backslash. Unquoted, a tab ends the name and a newline ends the line.
std::string headerName(std::string_view prefix, std::string_view path) {
    const auto special = [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte < 0x20 || byte == 0x7F || c == '"' || c == '\\';
    };
    std::string out;
    if (std::none_of(path.begin(), path.end(), special)) {
        out.append(prefix).append(path);
        return out;
    }
    out += '"';
    out.append(prefix);
    for (const char c : path) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\a':
            out += "\\a";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\v':
            out += "\\v";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            if (special(c)) {
                const auto byte = static_cast<unsigned char>(c);
                out += '\\';
                out += static_cast<char>('0' + ((byte >> 6U) & 7U));
                out += static_cast<char>('0' + ((byte >> 3U) & 7U));
                out += static_cast<char>('0' + (byte & 7U));
            } else {
                out += c;
            }
            break;
        }
    }
    out += '"';
    return out;
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
    int resultLines = 0;
    std::vector<PatchLine> lines;

    for (const auto& hunk : diff.hunks) {
        lines.clear();
        int oldCount = 0;
        int newCount = 0;
        bool anySelected = false;

        for (std::uint32_t i = 0; i < hunk.lineCount; ++i) {
            const std::uint32_t index = hunk.firstLine + i;
            const DiffLine& line = diff.lines[index];
            const std::string_view text = diff.lineText(line);
            const bool isSelected = selected.count(index) != 0;
            // The marker after a line says that line ends its file without a
            // newline. Where it is written is emitHunk's decision.
            const bool noNewline = i + 1 < hunk.lineCount &&
                                   diff.lines[index + 1].kind == DiffLineKind::NoNewlineMarker;

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
                continue; // carried by the line before it, as noNewline
            }

            if (marker == 0) {
                continue;
            }
            lines.push_back(PatchLine{marker, text, line.carriageReturn, noNewline});
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
        body << hunkHeader(oldStart, oldCount, newCount, offset);
        emitHunk(body, lines);
        offset += newCount - oldCount;
        resultLines += newCount;
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
    // And the other way round: every line of a deleted file staged, or every
    // line of an added one unstaged, removes the file. Without saying so the
    // patch only empties it, and git keeps an empty file in the index.
    const bool deletes =
        targetMode != 0 && resultMode == 0 && emittedHunks == diff.hunks.size() && resultLines == 0;

    // Both sides are named by the file's current path. For a staged rename the
    // index already holds it there; naming the old path on one side would make
    // the patch a rename of its own.
    const std::string oldName = headerName("a/", diff.path);
    const std::string newName = headerName("b/", diff.path);
    std::ostringstream patch;
    patch << "diff --git " << oldName << ' ' << newName << '\n';
    if (creates) {
        patch << "new file mode " << octal(resultMode) << '\n' << "--- /dev/null\n";
    } else if (deletes) {
        patch << "deleted file mode " << octal(targetMode) << '\n' << "--- " << oldName << '\n';
    } else {
        patch << "--- " << oldName << '\n';
    }
    patch << "+++ " << (deletes ? std::string("/dev/null") : newName) << '\n' << body.str();
    return patch.str();
}

} // namespace gity::git
