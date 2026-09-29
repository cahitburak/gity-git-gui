// The one test here that builds a real repository.
//
// Every other test in this suite is pure logic, deliberately. This defect
// could not be caught that way: it was an interaction between two libgit2
// option flags, where a pathspec silently matched nothing unless untracked
// directories were also recursed. Nothing about the shape of the code was
// wrong — only what libgit2 does with the combination — so the only test that
// could have found it is one that asks libgit2.
#include "core/git/Staging.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <cstdio>
#include <git2.h>

namespace {

namespace fs = std::filesystem;

/// A repository with one committed file and, optionally, an untracked one.
class Repo {
public:
    Repo() {
        git_libgit2_init();
        path_ = fs::temp_directory_path() /
                ("gity-diff-test-" + std::to_string(::testing::UnitTest::GetInstance()
                                                        ->random_seed()) +
                 "-" + std::to_string(counter_++));
        fs::remove_all(path_);
        fs::create_directories(path_ / "Assets");

        // The function, not GIT_REPOSITORY_INIT_OPTIONS_INIT: on libgit2 1.7 the
        // macro leaves a field out, which Clang reports as an error here.
        git_repository_init_options options{};
        git_repository_init_options_init(&options, GIT_REPOSITORY_INIT_OPTIONS_VERSION);
        options.initial_head = "main";
        EXPECT_EQ(git_repository_init_ext(&repo_, path_.string().c_str(), &options), 0);
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
        ASSERT_EQ(git_commit_create_v(&commitId, repo_, "HEAD", who, who, nullptr, "base", tree,
                                      0),
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

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/New.cs",
                                                 gity::git::DiffSide::Unstaged, {});
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

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::string contents;
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return contents;
    }
    char buffer[4096];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        contents.append(buffer, read);
    }
    std::fclose(file);
    return contents;
}

bool isStaged(git_repository* repo, const char* path) {
    unsigned int flags = 0;
    if (git_status_file(&flags, repo, path) < 0) {
        return false;
    }
    return (flags & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED |
                     GIT_STATUS_INDEX_DELETED)) != 0;
}

} // namespace

TEST(WorkingDiff, DiscardingAnUnstagedHunkRewritesTheFile) {
    // Discard hunk works on the *unstaged* diff — index against worktree — so
    // the reversed patch describes the worktree and nothing else. Applied to
    // the index as well, it cannot match, and the discard fails outright.
    Repo repo;
    repo.write("Assets/Player.cs", "one\n");
    repo.commitAll();
    repo.write("Assets/Player.cs", "one\ntwo\n");

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/Player.cs",
                                                 gity::git::DiffSide::Unstaged, {});
    ASSERT_EQ(diff.hunks.size(), 1u);
    const std::string patch =
        gity::git::buildPartialPatch(diff, gity::git::linesOfHunk(diff, 0), true);
    ASSERT_FALSE(patch.empty());

    EXPECT_NO_THROW(gity::git::applyPatchToWorktree(repo.get(), patch));
    EXPECT_EQ(readFile(std::filesystem::path(git_repository_workdir(repo.get())) /
                       "Assets/Player.cs"),
              "one\n");
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

    const auto diff = gity::git::loadWorkingDiff(repo.get(), "Assets/[id].cs",
                                                 gity::git::DiffSide::Unstaged, {});
    EXPECT_TRUE(diff.hunks.empty()) << "[id].cs is unchanged; d.cs's change leaked in";
}

TEST(WorkingDiff, UnstagingAPathWithGlobCharactersLeavesItsNeighbours) {
    Repo repo;
    repo.write("Assets/[id].cs", "one\n");
    repo.write("Assets/d.cs", "one\n");
    repo.commitAll();
    repo.write("Assets/[id].cs", "two\n");
    repo.write("Assets/d.cs", "two\n");
    gity::git::stageFiles(repo.get(), {"Assets/[id].cs", "Assets/d.cs"});

    gity::git::unstageFiles(repo.get(), {"Assets/[id].cs"});
    EXPECT_FALSE(isStaged(repo.get(), "Assets/[id].cs"));
    EXPECT_TRUE(isStaged(repo.get(), "Assets/d.cs")) << "unstaging [id].cs unstaged d.cs";
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
    ASSERT_EQ(git_commit_create(&commitId, raw, "HEAD", who, who, nullptr, "rename", tree, 1,
                                parents),
              0);
    git_signature_free(who);
    git_commit_free(parent);
    git_tree_free(tree);

    const auto diff = gity::git::loadFileDiff(raw, commitId, "Assets/New.cs", {});
    EXPECT_EQ(diff.status, gity::git::ChangeStatus::Renamed);
    EXPECT_EQ(diff.oldPath, "Assets/Old.cs");
    EXPECT_EQ(diff.additions(), 1u) << "only the added line, not the whole file";
    EXPECT_EQ(diff.deletions(), 0u);
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
    EXPECT_EQ(git_commit_create(&id, repo, ref, who, who, nullptr, message, tree,
                                parents.size(),
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
