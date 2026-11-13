// The history walk against a real repository.
#include "core/git/HistoryStream.h"

#include <gtest/gtest.h>

#include <git2.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// A repository with `count` commits on main, built through libgit2 alone so
/// this test needs neither git nor Qt.
class LinearRepo {
public:
    explicit LinearRepo(int count) {
        git_libgit2_init();
        path_ = fs::temp_directory_path() /
                ("gity-history-test-" +
                 std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(path_);
        git_repository_init_options options{};
        git_repository_init_options_init(&options, GIT_REPOSITORY_INIT_OPTIONS_VERSION);
        options.flags |= GIT_REPOSITORY_INIT_MKPATH;
        options.initial_head = "main";
        EXPECT_EQ(git_repository_init_ext(&repo_, path_.string().c_str(), &options), 0);

        git_signature* who = nullptr;
        EXPECT_EQ(git_signature_new(&who, "Test", "test@invalid", 1700000000, 0), 0);
        git_treebuilder* builder = nullptr;
        EXPECT_EQ(git_treebuilder_new(&builder, repo_, nullptr), 0);
        git_oid treeId{};
        EXPECT_EQ(git_treebuilder_write(&treeId, builder), 0);
        git_treebuilder_free(builder);
        git_tree* tree = nullptr;
        EXPECT_EQ(git_tree_lookup(&tree, repo_, &treeId), 0);

        git_commit* parent = nullptr;
        for (int i = 0; i < count; ++i) {
            const std::string message = (i % 2 == 0 ? "even " : "odd ") + std::to_string(i);
            git_oid id{};
            const git_commit* parents[] = {parent};
            EXPECT_EQ(git_commit_create(&id, repo_, "HEAD", who, who, nullptr, message.c_str(),
                                        tree, parent != nullptr ? 1 : 0, parents),
                      0);
            if (parent != nullptr) {
                git_commit_free(parent);
            }
            EXPECT_EQ(git_commit_lookup(&parent, repo_, &id), 0);
        }
        git_commit_free(parent);
        git_tree_free(tree);
        git_signature_free(who);
    }

    ~LinearRepo() {
        git_repository_free(repo_);
        std::error_code error;
        fs::remove_all(path_, error);
        git_libgit2_shutdown();
    }

    LinearRepo(const LinearRepo&) = delete;
    LinearRepo& operator=(const LinearRepo&) = delete;

    [[nodiscard]] git_repository* get() const { return repo_; }

private:
    git_repository* repo_ = nullptr;
    fs::path path_;
};

} // namespace

TEST(HistoryStream, WalksEveryCommitInChunks) {
    LinearRepo repo(25);
    gity::git::StreamOptions options;
    options.batchSize = 10;
    std::size_t rows = 0;
    std::size_t chunks = 0;
    const auto stats =
        gity::git::streamHistory(repo.get(), options, [&](gity::model::HistoryChunk&& chunk) {
            rows += chunk.rowCount();
            ++chunks;
            return true;
        });
    EXPECT_EQ(stats.commits, 25u);
    EXPECT_EQ(rows, 25u);
    EXPECT_GE(chunks, 3u);
}

TEST(HistoryStream, FilteredRowsAreFullyInitialised) {
    // Filtered rows are built by hand rather than by the lane assigner, and
    // once carried stack garbage in edgeCount and edgeOffset — which the
    // painter then walked, out of an empty edge vector.
    LinearRepo repo(12);
    gity::git::StreamOptions options;
    options.filter.text = "even";
    std::size_t matched = 0;
    static_cast<void>(
        gity::git::streamHistory(repo.get(), options, [&](gity::model::HistoryChunk&& chunk) {
            EXPECT_TRUE(chunk.graph.edges.empty());
            for (std::size_t i = 0; i < chunk.graph.rows.size(); ++i) {
                const auto& row = chunk.graph.rows[i];
                EXPECT_EQ(row.edgeCount, 0u);
                EXPECT_EQ(row.edgeOffset, 0u);
                EXPECT_EQ(row.flags, 0u);
                EXPECT_EQ(row.lane, 0u);
                EXPECT_EQ(row.commitIndex, i);
            }
            matched += chunk.rowCount();
            return true;
        }));
    EXPECT_EQ(matched, 6u);
}

namespace {

/// Commits built from file contents, for path filtering: each commit is the
/// whole tree, as {path, content} with at most one folder level.
class FileRepo {
public:
    using Files = std::vector<std::pair<std::string, std::string>>;

    FileRepo() {
        git_libgit2_init();
        path_ =
            fs::temp_directory_path() /
            ("gity-path-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(path_);
        EXPECT_EQ(git_repository_init(&repo_, path_.string().c_str(), 0), 0);
        EXPECT_EQ(git_signature_new(&who_, "Test", "test@invalid", 1700000000, 0), 0);
    }

    ~FileRepo() {
        git_signature_free(who_);
        git_repository_free(repo_);
        std::error_code error;
        fs::remove_all(path_, error);
        git_libgit2_shutdown();
    }

    FileRepo(const FileRepo&) = delete;
    FileRepo& operator=(const FileRepo&) = delete;

    /// Commits `files` on top of `parents` (by message), moving HEAD to it.
    void commit(const std::string& message, const Files& files,
                const std::vector<std::string>& parents) {
        const git_oid tree = writeTree(files);
        git_tree* treeObject = nullptr;
        EXPECT_EQ(git_tree_lookup(&treeObject, repo_, &tree), 0);
        std::vector<git_commit*> parentCommits;
        for (const auto& name : parents) {
            git_commit* parent = nullptr;
            EXPECT_EQ(git_commit_lookup(&parent, repo_, &ids_.at(name)), 0);
            parentCommits.push_back(parent);
        }
        std::vector<const git_commit*> constParents(parentCommits.begin(), parentCommits.end());
        git_oid id{};
        EXPECT_EQ(git_commit_create(&id, repo_, nullptr, who_, who_, nullptr, message.c_str(),
                                    treeObject, constParents.size(), constParents.data()),
                  0);
        for (git_commit* parent : parentCommits) {
            git_commit_free(parent);
        }
        git_tree_free(treeObject);
        ids_[message] = id;
        git_reference* ref = nullptr;
        EXPECT_EQ(git_reference_create(&ref, repo_, "refs/heads/main", &id, 1, nullptr), 0);
        git_reference_free(ref);
        EXPECT_EQ(git_repository_set_head(repo_, "refs/heads/main"), 0);
    }

    /// Summaries of the commits that changed `path`, newest first.
    std::vector<std::string> history(const std::string& path, bool follow = true) {
        std::vector<std::string> found;
        for (auto& [summary, name] : historyWithNames(path, follow)) {
            found.push_back(std::move(summary));
        }
        return found;
    }

    /// The same, each with the name the file had in that commit.
    std::vector<std::pair<std::string, std::string>> historyWithNames(const std::string& path,
                                                                      bool follow = true) {
        gity::git::StreamOptions options;
        options.filter.path = path;
        options.filter.followRenames = follow;
        options.batchSize = 2; // so names are looked up across chunks too
        std::vector<std::pair<std::string, std::string>> found;
        static_cast<void>(
            gity::git::streamHistory(repo_, options, [&](gity::model::HistoryChunk&& chunk) {
                for (std::size_t i = 0; i < chunk.rowCount(); ++i) {
                    const auto local = static_cast<gity::model::CommitIndex>(i);
                    std::string name = path;
                    for (const auto& [row, renamed] : chunk.renamedPaths) {
                        if (row == local) {
                            name = renamed;
                        }
                    }
                    found.emplace_back(chunk.commits.summary(local), name);
                }
                return true;
            }));
        return found;
    }

private:
    git_oid writeBlob(const std::string& content) {
        git_oid id{};
        EXPECT_EQ(git_blob_create_from_buffer(&id, repo_, content.data(), content.size()), 0);
        return id;
    }

    git_oid writeTree(const Files& files) {
        std::map<std::string, Files> folders;
        git_treebuilder* builder = nullptr;
        EXPECT_EQ(git_treebuilder_new(&builder, repo_, nullptr), 0);
        for (const auto& [name, content] : files) {
            const auto slash = name.find('/');
            if (slash != std::string::npos) {
                folders[name.substr(0, slash)].emplace_back(name.substr(slash + 1), content);
                continue;
            }
            const git_oid blob = writeBlob(content);
            EXPECT_EQ(
                git_treebuilder_insert(nullptr, builder, name.c_str(), &blob, GIT_FILEMODE_BLOB),
                0);
        }
        for (const auto& [folder, inside] : folders) {
            const git_oid sub = writeTree(inside);
            EXPECT_EQ(
                git_treebuilder_insert(nullptr, builder, folder.c_str(), &sub, GIT_FILEMODE_TREE),
                0);
        }
        git_oid id{};
        EXPECT_EQ(git_treebuilder_write(&id, builder), 0);
        git_treebuilder_free(builder);
        return id;
    }

    git_repository* repo_ = nullptr;
    git_signature* who_ = nullptr;
    std::map<std::string, git_oid> ids_;
    fs::path path_;
};

using Names = std::vector<std::string>;

} // namespace

TEST(HistoryStream, PathKeepsOnlyCommitsThatChangedIt) {
    FileRepo repo;
    repo.commit("add both", {{"a.txt", "1"}, {"src/b.txt", "1"}}, {});
    repo.commit("edit a", {{"a.txt", "2"}, {"src/b.txt", "1"}}, {"add both"});
    repo.commit("edit b", {{"a.txt", "2"}, {"src/b.txt", "2"}}, {"edit a"});
    repo.commit("add c", {{"a.txt", "2"}, {"src/b.txt", "2"}, {"src/c.txt", "1"}}, {"edit b"});
    repo.commit("delete a", {{"src/b.txt", "2"}, {"src/c.txt", "1"}}, {"add c"});

    EXPECT_EQ(repo.history("a.txt"), (Names{"delete a", "edit a", "add both"}));
    EXPECT_EQ(repo.history("src/b.txt"), (Names{"edit b", "add both"}));
    // A folder: anything inside it.
    EXPECT_EQ(repo.history("src"), (Names{"add c", "edit b", "add both"}));
    EXPECT_TRUE(repo.history("missing.txt").empty());
}

TEST(HistoryStream, PathSkipsAMergeThatTookOneSideUnchanged) {
    FileRepo repo;
    repo.commit("base", {{"a.txt", "1"}, {"b.txt", "1"}}, {});
    repo.commit("side edits a", {{"a.txt", "2"}, {"b.txt", "1"}}, {"base"});
    repo.commit("main edits b", {{"a.txt", "1"}, {"b.txt", "2"}}, {"base"});
    // A clean merge: a.txt comes from the side unchanged, so the merge itself
    // did not change it — git log -- a.txt does not list it either.
    repo.commit("merge", {{"a.txt", "2"}, {"b.txt", "2"}}, {"main edits b", "side edits a"});
    EXPECT_EQ(repo.history("a.txt"), (Names{"side edits a", "base"}));

    // A merge that resolved a.txt to something neither side had did change it.
    repo.commit("main edits a", {{"a.txt", "3"}, {"b.txt", "2"}}, {"merge"});
    repo.commit("side again", {{"a.txt", "4"}, {"b.txt", "1"}}, {"side edits a"});
    repo.commit("resolved", {{"a.txt", "5"}, {"b.txt", "2"}}, {"main edits a", "side again"});
    const Names found = repo.history("a.txt");
    ASSERT_FALSE(found.empty());
    EXPECT_EQ(found.front(), "resolved");
}

namespace {

/// A file long enough that an edit leaves it recognisably the same file.
std::string lines(int changed) {
    std::string text;
    for (int i = 0; i < 20; ++i) {
        text += "line " + std::to_string(i == 0 ? changed : i) + "\n";
    }
    return text;
}

} // namespace

TEST(HistoryStream, PathFollowsAFileThroughRenames) {
    FileRepo repo;
    repo.commit("add", {{"old.txt", lines(1)}, {"other.txt", "x"}}, {});
    repo.commit("edit", {{"old.txt", lines(2)}, {"other.txt", "x"}}, {"add"});
    repo.commit("unrelated", {{"old.txt", lines(2)}, {"other.txt", "y"}}, {"edit"});
    // A pure rename, then a rename with an edit, into a folder.
    repo.commit("rename", {{"mid.txt", lines(2)}, {"other.txt", "y"}}, {"unrelated"});
    repo.commit("move and edit", {{"src/new.txt", lines(3)}, {"other.txt", "y"}}, {"rename"});
    repo.commit("edit new", {{"src/new.txt", lines(4)}, {"other.txt", "y"}}, {"move and edit"});

    using Named = std::vector<std::pair<std::string, std::string>>;
    EXPECT_EQ(repo.historyWithNames("src/new.txt"), (Named{{"edit new", "src/new.txt"},
                                                           {"move and edit", "src/new.txt"},
                                                           {"rename", "mid.txt"},
                                                           {"edit", "old.txt"},
                                                           {"add", "old.txt"}}));
    // Not following stops where the file arrived under this name.
    EXPECT_EQ(repo.history("src/new.txt", false), (Names{"edit new", "move and edit"}));
    // The old name's own history ends where it went away.
    EXPECT_EQ(repo.history("old.txt"), (Names{"rename", "edit", "add"}));
}

TEST(HistoryStream, PathDoesNotFollowAFolderOrANewFile) {
    FileRepo repo;
    repo.commit("add", {{"a/one.txt", lines(1)}, {"a/two.txt", lines(5)}}, {});
    repo.commit("move folder", {{"b/one.txt", lines(1)}, {"b/two.txt", lines(5)}}, {"add"});
    EXPECT_EQ(repo.history("b"), (Names{"move folder"}));

    // A file that is new, while an unlike file goes away, is not a rename.
    repo.commit("swap", {{"b/one.txt", lines(1)}, {"c.txt", "nothing alike"}}, {"move folder"});
    EXPECT_EQ(repo.history("c.txt"), (Names{"swap"}));
}

TEST(HistoryStream, PathFollowsARenameOnOneSideOfAMerge) {
    FileRepo repo;
    repo.commit("base", {{"old.txt", lines(1)}, {"b.txt", "1"}}, {});
    repo.commit("side renames", {{"new.txt", lines(1)}, {"b.txt", "1"}}, {"base"});
    repo.commit("main edits b", {{"old.txt", lines(1)}, {"b.txt", "2"}}, {"base"});
    repo.commit("merge", {{"new.txt", lines(1)}, {"b.txt", "2"}}, {"main edits b", "side renames"});
    repo.commit("edit new", {{"new.txt", lines(2)}, {"b.txt", "2"}}, {"merge"});

    // The merge took the side's new.txt unchanged, so it is not listed; the
    // side's rename is, and the walk carries on to base under the old name.
    const Names found = repo.history("new.txt");
    EXPECT_EQ(found, (Names{"edit new", "side renames", "base"}));
}
