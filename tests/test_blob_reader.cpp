// LFS pointer detection. Reading a pointer as an image shows a 130-byte text
// file instead of a image, so every caller has to be able to tell the
// difference — and real Unity projects have both side by side.

#include "core/git/BlobReader.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

using namespace gity::git;
namespace fs = std::filesystem;

BlobContent contentOf(const std::string& text) {
    BlobContent content;
    content.bytes.assign(text.begin(), text.end());
    parseLfsPointer(content);
    return content;
}

constexpr const char* kPointer =
    "version https://git-lfs.github.com/spec/v1\n"
    "oid sha256:4d7a214614ab2935c943f9e0ff69d22eadbb8f32b1258daaa5e2ca24d17e2393\n"
    "size 12345678\n";

TEST(BlobReader, RecognisesAnLfsPointer) {
    const BlobContent content = contentOf(kPointer);
    EXPECT_TRUE(content.lfsPointer);
    EXPECT_EQ(content.lfsOid,
              "sha256:4d7a214614ab2935c943f9e0ff69d22eadbb8f32b1258daaa5e2ca24d17e2393");
    EXPECT_EQ(content.lfsSize, 12345678u);
}

TEST(BlobReader, RealFileContentIsNotAPointer) {
    EXPECT_FALSE(contentOf("\x89PNG\r\n\x1a\n....").lfsPointer);
    EXPECT_FALSE(contentOf("just some text").lfsPointer);
    EXPECT_FALSE(contentOf("").lfsPointer);
}

TEST(BlobReader, LargeContentIsNeverScanned) {
    // A pointer is a small text file. Scanning a 40 MB image for a magic
    // string would be pure waste, so size gates the check.
    std::string big(2048, 'x');
    big.replace(0, 40, "version https://git-lfs.github.com/spec/");
    EXPECT_FALSE(contentOf(big).lfsPointer);
}

TEST(BlobReader, PointerWithoutSizeStillParses) {
    const BlobContent content =
        contentOf("version https://git-lfs.github.com/spec/v1\noid sha256:abc\n");
    EXPECT_TRUE(content.lfsPointer);
    EXPECT_EQ(content.lfsOid, "sha256:abc");
    EXPECT_EQ(content.lfsSize, 0u);
}

TEST(BlobReader, SimilarPrefixIsNotAPointer) {
    EXPECT_FALSE(contentOf("version https://git-lfs.github.com/spe").lfsPointer);
    EXPECT_FALSE(contentOf("# version https://git-lfs.github.com/spec/v1\n").lfsPointer);
}

/// A working copy next to a folder outside it, holding a file that must never
/// be read through the working copy.
class WorktreeFiles : public ::testing::Test {
protected:
    void SetUp() override {
        base_ =
            fs::temp_directory_path() /
            ("gity-blob-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
             "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(base_);
        fs::create_directories(base_ / "work" / "inside");
        fs::create_directories(base_ / "outside");
        write(base_ / "work" / "inside" / "file.txt", "in");
        write(base_ / "work" / "empty.txt", "");
        write(base_ / "outside" / "secret.txt", "secret");
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(base_, error);
    }

    static void write(const fs::path& path, const std::string& contents) {
        // stdio rather than <fstream>, as in the other repository tests.
        std::FILE* file = std::fopen(path.string().c_str(), "wb");
        ASSERT_NE(file, nullptr);
        std::fwrite(contents.data(), 1, contents.size(), file);
        std::fclose(file);
    }

    [[nodiscard]] std::string work() const { return (base_ / "work").string(); }

    fs::path base_;
};

TEST_F(WorktreeFiles, AFileInsideIsRead) {
    const BlobContent content = readWorktreeFile(work(), "inside/file.txt");
    EXPECT_TRUE(content.found);
    EXPECT_EQ(std::string(content.bytes.begin(), content.bytes.end()), "in");
}

TEST_F(WorktreeFiles, AnEmptyFileIsFoundAndEmpty) {
    // Telling it from a file that could not be read is what lets an empty
    // file be a diff side like any other.
    const BlobContent content = readWorktreeFile(work(), "empty.txt");
    EXPECT_TRUE(content.found);
    EXPECT_TRUE(content.empty());
    EXPECT_FALSE(readWorktreeFile(work(), "missing.txt").found);
}

TEST_F(WorktreeFiles, APathThatClimbsOutReadsNothing) {
    EXPECT_FALSE(readWorktreeFile(work(), "../outside/secret.txt").found);
    EXPECT_FALSE(readWorktreeFile(work(), "inside/../../outside/secret.txt").found);
    EXPECT_FALSE(readWorktreeFile(work(), (base_ / "outside" / "secret.txt").string()).found);
    EXPECT_FALSE(readWorktreeFile(work(), "").found);
}

TEST_F(WorktreeFiles, AFolderThatIsASymlinkOutIsNotFollowed) {
    // The last component alone was checked for being a link, so a linked
    // folder on the way read whatever it pointed at.
    std::error_code error;
    fs::create_directory_symlink(base_ / "outside", base_ / "work" / "linked", error);
    if (error) {
        GTEST_SKIP() << "symlinks are not available here: " << error.message();
    }
    const BlobContent content = readWorktreeFile(work(), "linked/secret.txt");
    EXPECT_FALSE(content.found);
    EXPECT_TRUE(content.empty());
}

TEST_F(WorktreeFiles, AFolderThatIsASymlinkWithinIsFine) {
    std::error_code error;
    fs::create_directory_symlink(base_ / "work" / "inside", base_ / "work" / "alias", error);
    if (error) {
        GTEST_SKIP() << "symlinks are not available here: " << error.message();
    }
    EXPECT_TRUE(readWorktreeFile(work(), "alias/file.txt").found);
}

} // namespace
