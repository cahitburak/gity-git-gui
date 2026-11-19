// Diffs read from a real repository.
//
// These defects could not be caught with pure logic: the first was an
// interaction between two libgit2 option flags, where a pathspec silently
// matched nothing unless untracked directories were also recursed. Nothing
// about the shape of the code was wrong — only what libgit2 does with the
// combination — so the only test that could have found it is one that asks
// libgit2. Writes to the index go through git and are tested with the session
// layer (tests/session).
#include "core/git/Staging.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <git2.h>
#include <optional>

namespace {

namespace fs = std::filesystem;

/// A repository with one committed file and, optionally, an untracked one.
class Repo {
public:
    Repo() {
        git_libgit2_init();
        path_ =
            fs::temp_directory_path() /
            ("gity-diff-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
             "-" + std::to_string(counter_++));
        fs::remove_all(path_);
        fs::create_directories(path_ / "Assets");

        // The function, not GIT_REPOSITORY_INIT_OPTIONS_INIT: on libgit2 1.7 the
        // macro leaves a field out, which Clang reports as an error here.
        git_repository_init_options options{};
        git_repository_init_options_init(&options, GIT_REPOSITORY_INIT_OPTIONS_VERSION);
        options.initial_head = "main";
        EXPECT_EQ(git_repository_init_ext(&repo_, path_.string().c_str(), &options), 0);

        // Line endings as written, whatever this machine's config says. Git for
        // Windows — and GitHub's Windows runners — set core.autocrlf=true
        // system-wide, so a file restored by a discard came back as "one\r\n"
        // and the byte-for-byte comparisons below failed. Gity honouring that
        // setting is correct; the test just must not depend on it.
        git_config* config = nullptr;
        if (git_repository_config(&config, repo_) == 0) {
            EXPECT_EQ(git_config_set_bool(config, "core.autocrlf", 0), 0);
            git_config_free(config);
        }
    }

    ~Repo() {
        if (repo_ != nullptr) {
            git_repository_free(repo_);
        }
        std::error_code error;
        fs::remove_all(path_, error);
        git_libgit2_shutdown();
    }

    Repo(const Repo&) = delete;
    Repo& operator=(const Repo&) = delete;

    void write(const std::string& relative, const std::string& contents) const {
        // stdio rather than <fstream>: GCC 13 reports a false null dereference
        // inside libstdc++'s streambuf under -Wnull-dereference.
        std::FILE* file = std::fopen((path_ / relative).string().c_str(), "wb");
        ASSERT_NE(file, nullptr);
        std::fwrite(contents.data(), 1, contents.size(), file);
        std::fclose(file);
    }

    /// Stages everything present and commits it.
    void commitAll() const {
        git_index* index = nullptr;
        ASSERT_EQ(git_repository_index(&index, repo_), 0);
        ASSERT_EQ(git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr), 0);
        ASSERT_EQ(git_index_write(index), 0);

        git_oid treeId{};
        ASSERT_EQ(git_index_write_tree(&treeId, index), 0);
        git_tree* tree = nullptr;
        ASSERT_EQ(git_tree_lookup(&tree, repo_, &treeId), 0);

        git_signature* who = nullptr;
        ASSERT_EQ(git_signature_now(&who, "Test", "test@invalid"), 0);

        git_oid commitId{};
        ASSERT_EQ(git_commit_create_v(&commitId, repo_, "HEAD", who, who, nullptr, "base", tree, 0),
                  0);
        git_signature_free(who);
        git_tree_free(tree);
        git_index_free(index);
    }

    [[nodiscard]] git_repository* get() const { return repo_; }

private:
    git_repository* repo_ = nullptr;
    fs::path path_;
    static inline int counter_ = 0;
};

} // namespace

TEST(WorkingDiff, AModifiedTrackedFileDiffs) {
    Repo repo;
    repo.write("Assets/Player.cs", "one\n");
    repo.commitAll();
    repo.write("Assets/Player.cs", "one\ntwo\n");

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/Player.cs",
                                                 gity::git::DiffSide::Unstaged, {});
    EXPECT_EQ(diff.hunks.size(), 1u);
    EXPECT_FALSE(diff.lines.empty());
}

TEST(WorkingDiff, ABrandNewFileDiffsAgainstNothing) {
    // The regression. An untracked file asked for by path returned no hunks,
    // so the working-copy screen said "No textual changes" about a file that
    // plainly had contents — because libgit2 reports untracked content
    // collapsed at the directory level unless untracked directories are
    // recursed, and the pathspec then matched nothing.
    Repo repo;
    repo.write("Assets/Player.cs", "one\n");
    repo.commitAll();
    repo.write("Assets/New.cs", "brand new\n");

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), "Assets/New.cs", gity::git::DiffSide::Unstaged, {});
    EXPECT_EQ(diff.hunks.size(), 1u) << "an untracked file must diff against nothing";
    ASSERT_FALSE(diff.lines.empty());
    EXPECT_EQ(diff.lines.front().kind, gity::git::DiffLineKind::Addition);
}

TEST(WorkingDiff, AnUnchangedFileHasNoHunks) {
    Repo repo;
    repo.write("Assets/Player.cs", "one\n");
    repo.commitAll();

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/Player.cs",
                                                 gity::git::DiffSide::Unstaged, {});
    EXPECT_TRUE(diff.hunks.empty());
}

TEST(WorkingDiff, ABinaryFileIsReportedAsBinary) {
    // libgit2 only learns a file is binary when it loads the contents, which
    // happens when the patch is built — reading the flag earlier missed it.
    Repo repo;
    repo.write("Assets/icon.bin", std::string("\x89PNG\0\0\x01", 7));
    repo.commitAll();
    repo.write("Assets/icon.bin", std::string("\x89PNG\0\0\x02", 7));

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/icon.bin",
                                                 gity::git::DiffSide::Unstaged, {});
    EXPECT_TRUE(diff.binary);
}

TEST(WorkingDiff, CrLfEndingsAreRememberedButNotShown) {
    Repo repo;
    repo.write("Assets/Player.cs", "one\r\ntwo\r\n");
    repo.commitAll();
    repo.write("Assets/Player.cs", "one\r\nTWO\r\n");

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/Player.cs",
                                                 gity::git::DiffSide::Unstaged, {});
    ASSERT_FALSE(diff.lines.empty());
    for (const auto& line : diff.lines) {
        EXPECT_TRUE(line.carriageReturn);
        EXPECT_EQ(diff.lineText(line).find('\r'), std::string_view::npos);
    }
}

TEST(WorkingDiff, AnUntrackedFileHasNoOldSide) {
    Repo repo;
    repo.write("Assets/Player.cs", "one\n");
    repo.commitAll();
    repo.write("Assets/New.cs", "fresh\n");

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), "Assets/New.cs", gity::git::DiffSide::Unstaged, {});
    EXPECT_EQ(diff.oldMode, 0u);
    EXPECT_NE(diff.newMode, 0u);
}

TEST(WorkingDiff, BothSidesComeWholeForHighlighting) {
    // A hunk can open inside a comment that started lines above it; only the
    // whole file says so. The working copy's side is read from disk, since
    // the file is not in the object database until it is staged.
    Repo repo;
    const std::string before = "/* a comment\n   over lines */\nint a;\nint b;\nint c;\nint d;\n";
    repo.write("Assets/Player.cs", before);
    repo.commitAll();
    const std::string after = before + "int e;\n";
    repo.write("Assets/Player.cs", after);

    const auto unstaged = gity::git::loadWorkingDiff(repo.get(), "Assets/Player.cs",
                                                     gity::git::DiffSide::Unstaged, {});
    ASSERT_TRUE(unstaged.hasSources);
    EXPECT_EQ(unstaged.oldSource, before);
    EXPECT_EQ(unstaged.newSource, after);

    repo.write("Assets/New.cs", "fresh\n");
    const auto added =
        gity::git::loadWorkingDiff(repo.get(), "Assets/New.cs", gity::git::DiffSide::Unstaged, {});
    ASSERT_TRUE(added.hasSources);
    EXPECT_TRUE(added.oldSource.empty()) << "an added file has no old side";
    EXPECT_EQ(added.newSource, "fresh\n");
}

TEST(WorkingDiff, AFileEmptiedOnDiskStillHasSources) {
    // An empty file read from disk was taken for one that could not be read,
    // and the whole diff lost its highlighting.
    Repo repo;
    repo.write("Assets/Player.cs", "int a;\n");
    repo.commitAll();
    repo.write("Assets/Player.cs", "");

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/Player.cs",
                                                 gity::git::DiffSide::Unstaged, {});
    ASSERT_FALSE(diff.lines.empty());
    EXPECT_TRUE(diff.hasSources);
    EXPECT_EQ(diff.oldSource, "int a;\n");
    EXPECT_TRUE(diff.newSource.empty());
}

TEST(WorkingDiff, ABinaryFileHasNoSources) {
    Repo repo;
    repo.write("Assets/icon.bin", std::string("\x89PNG\0\0\x01", 7));
    repo.commitAll();
    repo.write("Assets/icon.bin", std::string("\x89PNG\0\0\x02", 7));

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/icon.bin",
                                                 gity::git::DiffSide::Unstaged, {});
    EXPECT_FALSE(diff.hasSources);
    EXPECT_TRUE(diff.newSource.empty());
}

TEST(WorkingDiff, APathWithGlobCharactersDiffsOnlyThatFile) {
    // `[id].tsx` is an ordinary file name in several web frameworks, and read
    // as a pattern it matches `d.tsx` as well. The diff has to be of the file
    // that was asked for, not of whichever match comes first.
    Repo repo;
    repo.write("Assets/[id].cs", "one\n");
    repo.write("Assets/d.cs", "one\n");
    repo.commitAll();
    repo.write("Assets/d.cs", "one\nfrom d\n");

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), "Assets/[id].cs", gity::git::DiffSide::Unstaged, {});
    EXPECT_TRUE(diff.hunks.empty()) << "[id].cs is unchanged; d.cs's change leaked in";
}

#include "core/git/FileDiff.h"

TEST(WorkingDiff, ARenamedFileInACommitDiffsAgainstItsOldName) {
    // The commit's file list reports renames. The diff for one of them used a
    // single path and no rename detection, so a file listed as renamed opened
    // as its entire content added.
    Repo repo;
    const std::string body = "line one\nline two\nline three\nline four\nline five\n";
    repo.write("Assets/Old.cs", body);
    repo.commitAll();

    git_repository* raw = repo.get();
    const std::filesystem::path root(git_repository_workdir(raw));
    std::filesystem::rename(root / "Assets/Old.cs", root / "Assets/New.cs");
    repo.write("Assets/New.cs", body + "line six\n");

    git_index* index = nullptr;
    ASSERT_EQ(git_repository_index(&index, raw), 0);
    ASSERT_EQ(git_index_remove_bypath(index, "Assets/Old.cs"), 0);
    ASSERT_EQ(git_index_add_bypath(index, "Assets/New.cs"), 0);
    ASSERT_EQ(git_index_write(index), 0);
    git_oid treeId{};
    ASSERT_EQ(git_index_write_tree(&treeId, index), 0);
    git_index_free(index);

    git_tree* tree = nullptr;
    ASSERT_EQ(git_tree_lookup(&tree, raw, &treeId), 0);
    git_oid headId{};
    ASSERT_EQ(git_reference_name_to_id(&headId, raw, "HEAD"), 0);
    git_commit* parent = nullptr;
    ASSERT_EQ(git_commit_lookup(&parent, raw, &headId), 0);
    git_signature* who = nullptr;
    ASSERT_EQ(git_signature_now(&who, "Test", "test@invalid"), 0);
    git_oid commitId{};
    const git_commit* parents[] = {parent};
    ASSERT_EQ(
        git_commit_create(&commitId, raw, "HEAD", who, who, nullptr, "rename", tree, 1, parents),
        0);
    git_signature_free(who);
    git_commit_free(parent);
    git_tree_free(tree);

    const auto diff = gity::git::loadFileDiff(raw, commitId, "Assets/New.cs", {});
    EXPECT_EQ(diff.status, gity::git::ChangeStatus::Renamed);
    EXPECT_EQ(diff.oldPath, "Assets/Old.cs");
    EXPECT_EQ(diff.additions(), 1u) << "only the added line, not the whole file";
    EXPECT_EQ(diff.deletions(), 0u);
    // Each side whole, from the commit and its parent, under its own name.
    ASSERT_TRUE(diff.hasSources);
    EXPECT_EQ(diff.oldSource, body);
    EXPECT_EQ(diff.newSource, body + "line six\n");
}

#include "core/git/Compare.h"

namespace {

/// Commits the index onto HEAD with `parents`, returning the new id.
git_oid commitIndex(git_repository* repo, const char* ref, const std::vector<git_oid>& parentIds,
                    const char* message) {
    git_index* index = nullptr;
    EXPECT_EQ(git_repository_index(&index, repo), 0);
    EXPECT_EQ(git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr), 0);
    EXPECT_EQ(git_index_write(index), 0);
    git_oid treeId{};
    EXPECT_EQ(git_index_write_tree(&treeId, index), 0);
    git_index_free(index);
    git_tree* tree = nullptr;
    EXPECT_EQ(git_tree_lookup(&tree, repo, &treeId), 0);

    std::vector<git_commit*> parents;
    for (const git_oid& id : parentIds) {
        git_commit* parent = nullptr;
        EXPECT_EQ(git_commit_lookup(&parent, repo, &id), 0);
        parents.push_back(parent);
    }
    git_signature* who = nullptr;
    EXPECT_EQ(git_signature_now(&who, "Test", "test@invalid"), 0);
    git_oid id{};
    EXPECT_EQ(git_commit_create(&id, repo, ref, who, who, nullptr, message, tree, parents.size(),
                                const_cast<const git_commit**>(parents.data())),
              0);
    git_signature_free(who);
    for (git_commit* parent : parents) {
        git_commit_free(parent);
    }
    git_tree_free(tree);
    return id;
}

} // namespace

TEST(Compare, TwoBranchesDifferByTheirTipsAndTheirOwnCommits) {
    Repo repo;
    git_repository* raw = repo.get();
    repo.write("Assets/shared.txt", "base\n");
    const git_oid base = commitIndex(raw, nullptr, {}, "base");

    // `left` changes shared.txt once; `right` adds a file in two commits.
    repo.write("Assets/shared.txt", "base\nleft\n");
    const git_oid left = commitIndex(raw, nullptr, {base}, "left work");
    repo.write("Assets/shared.txt", "base\n");
    repo.write("Assets/right.txt", "one\n");
    const git_oid right1 = commitIndex(raw, nullptr, {base}, "right one");
    repo.write("Assets/right.txt", "one\ntwo\n");
    const git_oid right2 = commitIndex(raw, nullptr, {right1}, "right two");

    const auto comparison = gity::git::loadComparison(raw, left, right2);
    EXPECT_EQ(comparison.onlyInFrom, 1u);
    EXPECT_EQ(comparison.onlyInTo, 2u);
    ASSERT_EQ(comparison.onlyInToCommits.size(), 2u);
    EXPECT_EQ(comparison.onlyInToCommits.front().summary, "right two") << "newest first";
    ASSERT_TRUE(comparison.hasMergeBase);
    EXPECT_TRUE(git_oid_equal(&comparison.mergeBase, &base));

    // Tip to tip: right.txt appears, and shared.txt loses left's line.
    ASSERT_EQ(comparison.files.size(), 2u);
    const auto diff = gity::git::loadFileDiffBetween(raw, left, right2, "Assets/shared.txt");
    EXPECT_EQ(diff.deletions(), 1u);
    EXPECT_EQ(diff.additions(), 0u);
}

// Partial patches built from real diffs and applied to the index, the way
// `git apply --cached` applies them. The hand-built cases in test_staging.cpp
// pin the text; these pin that the text is a patch git accepts, and that it
// does what was selected — most of all at the end of a file without a
// newline, where the "\ No newline at end of file" lines decide the result.

namespace {

constexpr const char* kAbsent = "<absent>";

/// What the index holds for `path` after `patch` is applied to it: nullopt
/// when the patch does not apply, kAbsent when it took the file out.
std::optional<std::string> indexAfterApplying(git_repository* repo, const std::string& patch,
                                              const std::string& path) {
    git_diff* diff = nullptr;
    if (git_diff_from_buffer(&diff, patch.data(), patch.size()) != 0) {
        return std::nullopt;
    }
    const int rc = git_apply(repo, diff, GIT_APPLY_LOCATION_INDEX, nullptr);
    git_diff_free(diff);
    if (rc != 0) {
        return std::nullopt;
    }
    git_index* index = nullptr;
    EXPECT_EQ(git_repository_index(&index, repo), 0);
    EXPECT_EQ(git_index_read(index, 0), 0);
    std::string contents = kAbsent;
    if (const git_index_entry* entry = git_index_get_bypath(index, path.c_str(), 0);
        entry != nullptr) {
        git_blob* blob = nullptr;
        EXPECT_EQ(git_blob_lookup(&blob, repo, &entry->id), 0);
        const auto size = static_cast<std::size_t>(git_blob_rawsize(blob));
        contents.assign(static_cast<const char*>(git_blob_rawcontent(blob)), size);
        git_blob_free(blob);
    }
    git_index_free(index);
    return contents;
}

/// Stages `path` as it is on disk.
void stage(git_repository* repo, const char* path) {
    git_index* index = nullptr;
    ASSERT_EQ(git_repository_index(&index, repo), 0);
    ASSERT_EQ(git_index_add_bypath(index, path), 0);
    ASSERT_EQ(git_index_write(index), 0);
    git_index_free(index);
}

/// The index of the first line of `kind`.
std::uint32_t firstOf(const gity::git::FileDiff& diff, gity::git::DiffLineKind kind) {
    for (std::uint32_t i = 0; i < diff.lines.size(); ++i) {
        if (diff.lines[i].kind == kind) {
            return i;
        }
    }
    ADD_FAILURE() << "no line of that kind";
    return 0;
}

constexpr const char* kFile = "Assets/Player.cs";

} // namespace

TEST(PartialPatch, AHunkEndingInContextWithoutANewlineStages) {
    // Both versions end without a newline and the last line is context, so
    // the marker follows a context line. It was read as a context line of its
    // own, and the patch then expected a line the file does not have.
    Repo repo;
    repo.write(kFile, "1\n2\n3\n4\n5");
    repo.commitAll();
    repo.write(kFile, "1\n2\nX\n4\n5");

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), kFile, gity::git::DiffSide::Unstaged, {});
    ASSERT_EQ(diff.hunks.size(), 1u);
    EXPECT_EQ(diff.lines.back().kind, gity::git::DiffLineKind::NoNewlineMarker);
    const std::string patch =
        gity::git::buildPartialPatch(diff, gity::git::linesOfHunk(diff, 0), false);
    EXPECT_EQ(indexAfterApplying(repo.get(), patch, kFile), "1\n2\nX\n4\n5") << patch;
}

TEST(PartialPatch, StagingOnlyANewLastLineKeepsTheOldOneAndGivesItANewline) {
    // The unselected deletion of "last" stays as context, followed by the
    // added line: "last" is no longer the end of the file, so it cannot keep
    // its "no newline" marker where it was.
    Repo repo;
    repo.write(kFile, "1\n2\nlast");
    repo.commitAll();
    repo.write(kFile, "1\n2\nnew last");

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), kFile, gity::git::DiffSide::Unstaged, {});
    const std::string patch = gity::git::buildPartialPatch(
        diff, {firstOf(diff, gity::git::DiffLineKind::Addition)}, false);
    EXPECT_EQ(indexAfterApplying(repo.get(), patch, kFile), "1\n2\nlast\nnew last") << patch;
}

TEST(PartialPatch, StagingOnlyTheRemovalOfALastLineLeavesANewlineBefore) {
    Repo repo;
    repo.write(kFile, "1\n2\nlast");
    repo.commitAll();
    repo.write(kFile, "1\n2\nnew last");

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), kFile, gity::git::DiffSide::Unstaged, {});
    const std::string patch = gity::git::buildPartialPatch(
        diff, {firstOf(diff, gity::git::DiffLineKind::Deletion)}, false);
    EXPECT_EQ(indexAfterApplying(repo.get(), patch, kFile), "1\n2\n") << patch;
}

TEST(PartialPatch, UnstagingPartOfAChangedLastLine) {
    // The reverse direction: the patch applies to the index and puts back
    // only what was selected.
    for (const auto kind : {gity::git::DiffLineKind::Deletion, gity::git::DiffLineKind::Addition}) {
        Repo repo;
        repo.write(kFile, "1\n2\nlast");
        repo.commitAll();
        repo.write(kFile, "1\n2\nnew last");
        stage(repo.get(), kFile);

        const auto diff =
            gity::git::loadWorkingDiff(repo.get(), kFile, gity::git::DiffSide::Staged, {});
        const std::string patch = gity::git::buildPartialPatch(diff, {firstOf(diff, kind)}, true);
        // Unstaging the removal of "last" brings it back before the new line;
        // unstaging the new line leaves the removal staged.
        const char* expected =
            kind == gity::git::DiffLineKind::Deletion ? "1\n2\nlast\nnew last" : "1\n2\n";
        EXPECT_EQ(indexAfterApplying(repo.get(), patch, kFile), expected) << patch;
    }
}

TEST(PartialPatch, StagingEveryLineOfADeletedFileStagesTheDeletion) {
    // Without "deleted file mode" and "+++ /dev/null" git empties the file
    // in the index rather than removing it.
    Repo repo;
    repo.write(kFile, "a\nb\n");
    repo.write("Assets/Other.cs", "other\n");
    repo.commitAll();
    fs::remove(fs::path(git_repository_workdir(repo.get())) / kFile);

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), kFile, gity::git::DiffSide::Unstaged, {});
    const std::string all =
        gity::git::buildPartialPatch(diff, gity::git::linesOfHunk(diff, 0), false);
    EXPECT_NE(all.find("deleted file mode 100644\n--- a/Assets/Player.cs\n+++ /dev/null\n"),
              std::string::npos)
        << all;

    // Some of the lines are only an edit.
    const std::string some = gity::git::buildPartialPatch(diff, {0}, false);
    EXPECT_EQ(some.find("deleted file mode"), std::string::npos) << some;
    EXPECT_EQ(indexAfterApplying(repo.get(), all, kFile), kAbsent) << all;
}

TEST(PartialPatch, UnstagingEveryLineOfAnAddedFileTakesItOutOfTheIndex) {
    Repo repo;
    repo.write("Assets/Other.cs", "other\n");
    repo.commitAll();
    repo.write(kFile, "a\nb\n");
    stage(repo.get(), kFile);

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), kFile, gity::git::DiffSide::Staged, {});
    const std::string patch =
        gity::git::buildPartialPatch(diff, gity::git::linesOfHunk(diff, 0), true);
    EXPECT_EQ(indexAfterApplying(repo.get(), patch, kFile), kAbsent) << patch;
}

#ifndef _WIN32
TEST(PartialPatch, APathGitHasToQuoteStages) {
    // A tab ends an unquoted name in a patch header, and a newline ends the
    // line. git quotes such names in C style, and reads them back the same.
    // Not on Windows, which allows neither character in a file name.
    Repo repo;
    const std::string path = "Assets/tab\there \"quoted\" back\\slash.cs";
    repo.write(path, "one\n");
    repo.commitAll();
    repo.write(path, "one\ntwo\n");

    const auto diff =
        gity::git::loadWorkingDiff(repo.get(), path, gity::git::DiffSide::Unstaged, {});
    ASSERT_EQ(diff.hunks.size(), 1u);
    const std::string patch =
        gity::git::buildPartialPatch(diff, gity::git::linesOfHunk(diff, 0), false);
    EXPECT_NE(patch.find("diff --git \"a/Assets/tab\\there \\\"quoted\\\" back\\\\slash.cs\" "
                         "\"b/Assets/tab\\there \\\"quoted\\\" back\\\\slash.cs\"\n"),
              std::string::npos)
        << patch;
    EXPECT_EQ(indexAfterApplying(repo.get(), patch, path), "one\ntwo\n") << patch;
}
#endif
