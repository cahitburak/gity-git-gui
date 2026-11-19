// Reading a conflicted file's versions out of the index.
#include "TempDir.h"

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
        path_ = gity::test::freshTempDir("conflict-test");
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
    // Never merged, so never read through the filters either.
    EXPECT_TRUE(file.ours.text.empty());
    EXPECT_TRUE(file.theirs.text.empty());
}

namespace {

/// Puts a conflict on `path` whose ours and theirs are entries of `mode`,
/// with the ids given.
void conflictOfMode(git_repository* repo, const char* path, std::uint32_t mode,
                    const git_oid& oursId, const git_oid& theirsId) {
    git_index* index = nullptr;
    ASSERT_EQ(git_repository_index(&index, repo), 0);
    git_index_entry ours{};
    git_index_entry theirs{};
    ours.path = theirs.path = path;
    ours.mode = theirs.mode = mode;
    ours.id = oursId;
    theirs.id = theirsId;
    ASSERT_EQ(git_index_conflict_add(index, nullptr, &ours, &theirs), 0);
    ASSERT_EQ(git_index_write(index), 0);
    git_index_free(index);
}

} // namespace

TEST(Conflict, ASubmoduleConflictLoadsAsWholeVersions) {
    // A submodule's entry is a commit id, and looking it up as a blob threw:
    // the conflict could not be opened at all.
    ConflictRepo repo;
    git_oid ours{};
    git_oid theirs{};
    ASSERT_EQ(git_oid_fromstr(&ours, "1111111111111111111111111111111111111111"), 0);
    ASSERT_EQ(git_oid_fromstr(&theirs, "2222222222222222222222222222222222222222"), 0);
    conflictOfMode(repo.get(), "lib", GIT_FILEMODE_COMMIT, ours, theirs);

    const auto file = gity::git::loadConflict(repo.get(), "lib");
    EXPECT_TRUE(file.binary) << "only a whole side can be taken";
    EXPECT_TRUE(file.bothSides());
    EXPECT_EQ(file.ours.mode, static_cast<std::uint32_t>(GIT_FILEMODE_COMMIT));
    EXPECT_EQ(file.theirs.mode, static_cast<std::uint32_t>(GIT_FILEMODE_COMMIT));
    EXPECT_TRUE(file.merge.regions.empty());
}

TEST(Conflict, ASymlinkConflictIsNotMergedAsText) {
    // Merged as text, the two targets were saved back as a regular file.
    ConflictRepo repo;
    git_oid ours{};
    git_oid theirs{};
    ASSERT_EQ(git_blob_create_from_buffer(&ours, repo.get(), "target-a", 8), 0);
    ASSERT_EQ(git_blob_create_from_buffer(&theirs, repo.get(), "target-b", 8), 0);
    conflictOfMode(repo.get(), "link", GIT_FILEMODE_LINK, ours, theirs);

    const auto file = gity::git::loadConflict(repo.get(), "link");
    EXPECT_TRUE(file.binary);
    EXPECT_EQ(file.ours.mode, static_cast<std::uint32_t>(GIT_FILEMODE_LINK));
    EXPECT_TRUE(file.merge.regions.empty());
}

TEST(Conflict, AnEmptyVersionIsReadAsEmpty) {
    ConflictRepo repo;
    repo.conflict("", "a\n", "b\n");
    const auto file = gity::git::loadConflict(repo.get(), "file.txt");
    EXPECT_TRUE(file.base.present);
    EXPECT_TRUE(file.base.text.empty());
    EXPECT_EQ(file.base.mode, static_cast<std::uint32_t>(GIT_FILEMODE_BLOB));
    EXPECT_FALSE(file.binary);
    EXPECT_EQ(file.merge.conflictCount(), 1U);
}

TEST(Conflict, APathThatIsNotConflictedIsAnError) {
    ConflictRepo repo;
    EXPECT_THROW(static_cast<void>(gity::git::loadConflict(repo.get(), "file.txt")),
                 std::runtime_error);
}
