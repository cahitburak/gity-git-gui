// Reading a conflicted file's versions out of the index.
#include "core/git/Conflict.h"

#include <git2.h>
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

/// A repository whose index holds a conflict for `file.txt`, built directly
/// so the test needs no git binary.
class ConflictRepo {
public:
    ConflictRepo() {
        git_libgit2_init();
        path_ = fs::temp_directory_path() /
                ("gity-conflict-test-" +
                 std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(path_);
        EXPECT_EQ(git_repository_init(&repo_, path_.string().c_str(), 0), 0);
    }

    ~ConflictRepo() {
        git_repository_free(repo_);
        std::error_code error;
        fs::remove_all(path_, error);
        git_libgit2_shutdown();
    }

    ConflictRepo(const ConflictRepo&) = delete;
    ConflictRepo& operator=(const ConflictRepo&) = delete;

    /// A null version leaves that stage out.
    void conflict(const char* base, const char* ours, const char* theirs) {
        git_index* index = nullptr;
        ASSERT_EQ(git_repository_index(&index, repo_), 0);
        git_index_entry entries[3]{};
        const char* texts[3] = {base, ours, theirs};
        const git_index_entry* stages[3] = {nullptr, nullptr, nullptr};
        for (int i = 0; i < 3; ++i) {
            if (texts[i] == nullptr) {
                continue;
            }
            entries[i].path = "file.txt";
            entries[i].mode = GIT_FILEMODE_BLOB;
            ASSERT_EQ(
                git_blob_create_from_buffer(&entries[i].id, repo_, texts[i], std::strlen(texts[i])),
                0);
            stages[i] = &entries[i];
        }
        ASSERT_EQ(git_index_conflict_add(index, stages[0], stages[1], stages[2]), 0);
        ASSERT_EQ(git_index_write(index), 0);
        git_index_free(index);
    }

    git_repository* get() { return repo_; }

private:
    fs::path path_;
    git_repository* repo_ = nullptr;
};

} // namespace

TEST(Conflict, ReadsTheThreeVersionsAndSplitsThem) {
    ConflictRepo repo;
    repo.conflict("a\nb\nc\n", "a\nOURS\nc\n", "a\nTHEIRS\nc\n");
    const auto file = gity::git::loadConflict(repo.get(), "file.txt");
    EXPECT_TRUE(file.base.present);
    EXPECT_TRUE(file.bothSides());
    EXPECT_FALSE(file.binary);
    EXPECT_EQ(file.ours.text, "a\nOURS\nc\n");
    EXPECT_EQ(file.theirs.text, "a\nTHEIRS\nc\n");
    EXPECT_EQ(file.merge.conflictCount(), 1U);
}

TEST(Conflict, ASideThatDeletedTheFileIsMissing) {
    ConflictRepo repo;
    repo.conflict("a\n", "a\nmore\n", nullptr);
    const auto file = gity::git::loadConflict(repo.get(), "file.txt");
    EXPECT_TRUE(file.ours.present);
    EXPECT_FALSE(file.theirs.present);
    EXPECT_FALSE(file.bothSides());
    EXPECT_TRUE(file.merge.regions.empty());
}

TEST(Conflict, BinaryVersionsAreNotSplit) {
    // Built by hand: conflict() takes C strings, which stop at a NUL.
    ConflictRepo binary;
    git_index* index = nullptr;
    ASSERT_EQ(git_repository_index(&index, binary.get()), 0);
    const char bytes[] = {'a', '\0', 'b'};
    git_index_entry ours{};
    git_index_entry theirs{};
    ours.path = theirs.path = "file.txt";
    ours.mode = theirs.mode = GIT_FILEMODE_BLOB;
    ASSERT_EQ(git_blob_create_from_buffer(&ours.id, binary.get(), bytes, sizeof(bytes)), 0);
    ASSERT_EQ(git_blob_create_from_buffer(&theirs.id, binary.get(), "text", 4), 0);
    ASSERT_EQ(git_index_conflict_add(index, nullptr, &ours, &theirs), 0);
    ASSERT_EQ(git_index_write(index), 0);
    git_index_free(index);

    const auto file = gity::git::loadConflict(binary.get(), "file.txt");
    EXPECT_TRUE(file.binary);
    EXPECT_TRUE(file.merge.regions.empty());
}

TEST(Conflict, APathThatIsNotConflictedIsAnError) {
    ConflictRepo repo;
    EXPECT_THROW(static_cast<void>(gity::git::loadConflict(repo.get(), "file.txt")),
                 std::runtime_error);
}
