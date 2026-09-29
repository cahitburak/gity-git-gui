// Built on a real repository, because what is under test is how libgit2
// reports a symbolic <remote>/HEAD while iterating branches.
#include "core/git/Refs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <git2.h>

namespace {

namespace fs = std::filesystem;

const gity::git::RefEntry* find(const std::vector<gity::git::RefEntry>& entries,
                                const std::string& name) {
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [&name](const auto& entry) { return entry.name == name; });
    return it == entries.end() ? nullptr : &*it;
}

} // namespace

TEST(LoadRefs, MarksTheBranchTheRemoteHeadNames) {
    git_libgit2_init();
    const fs::path path = fs::temp_directory_path() /
                          ("gity-refs-test-" +
                           std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    fs::remove_all(path);
    fs::create_directories(path);

    git_repository* repo = nullptr;
    // The function, not the INIT macro, which leaves a field out on libgit2 1.7.
    git_repository_init_options options{};
    git_repository_init_options_init(&options, GIT_REPOSITORY_INIT_OPTIONS_VERSION);
    options.initial_head = "main";
    ASSERT_EQ(git_repository_init_ext(&repo, path.string().c_str(), &options), 0);

    // One empty commit, and every branch at it.
    git_index* index = nullptr;
    ASSERT_EQ(git_repository_index(&index, repo), 0);
    git_oid tree{};
    ASSERT_EQ(git_index_write_tree(&tree, index), 0);
    git_index_free(index);
    git_tree* treeObject = nullptr;
    ASSERT_EQ(git_tree_lookup(&treeObject, repo, &tree), 0);
    git_signature* who = nullptr;
    ASSERT_EQ(git_signature_now(&who, "Test", "test@example.com"), 0);
    git_oid commit{};
    ASSERT_EQ(git_commit_create_v(&commit, repo, "HEAD", who, who, nullptr, "first", treeObject, 0),
              0);
    git_signature_free(who);
    git_tree_free(treeObject);

    const auto make = [&](const char* name) {
        git_reference* ref = nullptr;
        ASSERT_EQ(git_reference_create(&ref, repo, name, &commit, 0, nullptr), 0);
        git_reference_free(ref);
    };
    make("refs/heads/release-2");
    make("refs/remotes/origin/main");
    make("refs/remotes/origin/release-2");
    // What clone writes, and what fetch updates when it follows the remote.
    git_reference* head = nullptr;
    ASSERT_EQ(git_reference_symbolic_create(&head, repo, "refs/remotes/origin/HEAD",
                                            "refs/remotes/origin/release-2", 0, nullptr),
              0);
    git_reference_free(head);

    // A branch still set to track a remote branch that was deleted and pruned.
    make("refs/heads/feature/done");
    git_config* config = nullptr;
    ASSERT_EQ(git_repository_config(&config, repo), 0);
    ASSERT_EQ(git_config_set_string(config, "remote.origin.url", "/nowhere"), 0);
    ASSERT_EQ(git_config_set_string(config, "remote.origin.fetch",
                                    "+refs/heads/*:refs/remotes/origin/*"),
              0);
    ASSERT_EQ(git_config_set_string(config, "branch.feature/done.remote", "origin"), 0);
    ASSERT_EQ(git_config_set_string(config, "branch.feature/done.merge", "refs/heads/feature/done"),
              0);
    ASSERT_EQ(git_config_set_string(config, "branch.release-2.remote", "origin"), 0);
    ASSERT_EQ(git_config_set_string(config, "branch.release-2.merge", "refs/heads/release-2"), 0);
    git_config_free(config);

    const gity::git::RefSet refs = gity::git::loadRefs(repo);
    ASSERT_NE(find(refs.localBranches, "feature/done"), nullptr);
    EXPECT_TRUE(find(refs.localBranches, "feature/done")->upstreamGone);
    EXPECT_FALSE(find(refs.localBranches, "feature/done")->hasUpstream);
    EXPECT_EQ(find(refs.localBranches, "feature/done")->upstream, "origin/feature/done");
    // Tracking one that exists is not gone.
    EXPECT_FALSE(find(refs.localBranches, "release-2")->upstreamGone);
    EXPECT_TRUE(find(refs.localBranches, "release-2")->hasUpstream);
    // And one that tracks nothing is not gone either.
    EXPECT_FALSE(find(refs.localBranches, "main")->upstreamGone);
    EXPECT_EQ(refs.remoteDefaults, std::vector<std::string>{"origin/release-2"});
    // origin/HEAD itself is still not listed as a branch.
    EXPECT_EQ(find(refs.remoteBranches, "origin/HEAD"), nullptr);
    ASSERT_NE(find(refs.remoteBranches, "origin/release-2"), nullptr);
    EXPECT_TRUE(find(refs.remoteBranches, "origin/release-2")->isDefault);
    EXPECT_FALSE(find(refs.remoteBranches, "origin/main")->isDefault);
    // A local branch tracking nothing counts by name.
    EXPECT_TRUE(find(refs.localBranches, "release-2")->isDefault);
    EXPECT_FALSE(find(refs.localBranches, "main")->isDefault);

    git_repository_free(repo);
    std::error_code error;
    fs::remove_all(path, error);
    git_libgit2_shutdown();
}
