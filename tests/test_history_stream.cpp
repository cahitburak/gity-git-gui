// The history walk against a real repository.
#include "core/git/HistoryStream.h"

#include <gtest/gtest.h>

#include <git2.h>

#include <cstdlib>
#include <filesystem>
#include <string>

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
