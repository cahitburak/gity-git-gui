// LFS pointer detection. Reading a pointer as an image shows a 130-byte text
// file instead of a image, so every caller has to be able to tell the
// difference — and real Unity projects have both side by side.

#include "core/git/BlobReader.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace gity::git;

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
    const BlobContent content = contentOf(
        "version https://git-lfs.github.com/spec/v1\noid sha256:abc\n");
    EXPECT_TRUE(content.lfsPointer);
    EXPECT_EQ(content.lfsOid, "sha256:abc");
    EXPECT_EQ(content.lfsSize, 0u);
}

TEST(BlobReader, SimilarPrefixIsNotAPointer) {
    EXPECT_FALSE(contentOf("version https://git-lfs.github.com/spe").lfsPointer);
    EXPECT_FALSE(contentOf("# version https://git-lfs.github.com/spec/v1\n").lfsPointer);
}

} // namespace
