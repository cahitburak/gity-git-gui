// The first fixture is real: captured by taking a lock on a live repository
// with `git lfs lock`, reading `git lfs locks --json`, and unlocking again.
// git-lfs 3.7.1 against GitHub.
#include "core/git/LfsLocks.h"

#include <gtest/gtest.h>

using gity::git::lockFor;
using gity::git::parseLfsLocks;

TEST(ParseLfsLocks, TheRealResponse) {
    const auto locks = parseLfsLocks(
        R"([{"id":"49909752","path":"README.md","owner":{"name":"alex-personal"},)"
        R"("locked_at":"2026-08-30T05:12:22Z"}])");
    ASSERT_EQ(locks.size(), 1u);
    EXPECT_EQ(locks[0].id, "49909752");
    EXPECT_EQ(locks[0].path, "README.md");
    EXPECT_EQ(locks[0].owner, "alex-personal");
    EXPECT_EQ(locks[0].lockedAt, "2026-08-30T05:12:22Z");
}

TEST(ParseLfsLocks, NothingLocked) {
    EXPECT_TRUE(parseLfsLocks("[]").empty());
    EXPECT_TRUE(parseLfsLocks("[]\n").empty());
}

TEST(ParseLfsLocks, TheOwnerNameIsReadFromItsOwnObject) {
    // The failure this prevents: scanning the element for "name" attaches the
    // owner of one lock to another, or picks up a "name" field that belongs to
    // something else entirely. Here the second lock's owner must not leak into
    // the first.
    const auto locks = parseLfsLocks(
        R"([{"id":"1","path":"a.psd","owner":{"name":"ayse"},"locked_at":"t1"},)"
        R"({"id":"2","path":"b.psd","owner":{"name":"mehmet"},"locked_at":"t2"}])");
    ASSERT_EQ(locks.size(), 2u);
    EXPECT_EQ(locks[0].owner, "ayse");
    EXPECT_EQ(locks[1].owner, "mehmet");
}

TEST(ParseLfsLocks, UnityPathsWithSpacesSurvive) {
    const auto locks = parseLfsLocks(
        R"([{"id":"7","path":"Assets/Art/Character Images/Hero Albedo.psd",)"
        R"("owner":{"name":"someone"},"locked_at":"t"}])");
    ASSERT_EQ(locks.size(), 1u);
    EXPECT_EQ(locks[0].path, "Assets/Art/Character Images/Hero Albedo.psd");
}

TEST(ParseLfsLocks, EscapesInAPathAreResolvedRatherThanTruncatingIt) {
    // Taking the text between the first two quotes would stop at the escaped
    // one and produce a path that matches nothing.
    const auto locks = parseLfsLocks(
        R"([{"id":"8","path":"Assets/Odd\"Name\\Thing.psd","owner":{"name":"x"},)"
        R"("locked_at":"t"}])");
    ASSERT_EQ(locks.size(), 1u);
    EXPECT_EQ(locks[0].path, "Assets/Odd\"Name\\Thing.psd");
}

TEST(ParseLfsLocks, NonAsciiNamesSurvive) {
    const auto locks = parseLfsLocks(
        R"([{"id":"9","path":"a.psd","owner":{"name":"Çağrı"},"locked_at":"t"}])");
    ASSERT_EQ(locks.size(), 1u);
    EXPECT_EQ(locks[0].owner, "\xC3\x87\x61\xC4\x9F\x72\xC4\xB1"); // Çağrı
}

TEST(ParseLfsLocks, UnfamiliarFieldsDoNotDerailTheElement) {
    // git-lfs may add fields; an unknown one must be stepped over, including
    // when it is itself an object or an array.
    const auto locks = parseLfsLocks(
        R"([{"id":"10","extra":{"nested":[1,2,{"deep":"x"}]},"path":"a.psd",)"
        R"("owner":{"name":"y","email":"y@example.com"},"locked_at":"t","flag":true}])");
    ASSERT_EQ(locks.size(), 1u);
    EXPECT_EQ(locks[0].path, "a.psd");
    EXPECT_EQ(locks[0].owner, "y");
}

TEST(ParseLfsLocks, GarbageYieldsNothingRatherThanHalfALockList) {
    // A lock list is advisory. Half of one is worse than none: it would show
    // a file as free that somebody is holding.
    EXPECT_TRUE(parseLfsLocks("").empty());
    EXPECT_TRUE(parseLfsLocks("not json at all").empty());
    EXPECT_TRUE(parseLfsLocks(R"([{"id":"1","path":)").empty());
    EXPECT_TRUE(parseLfsLocks(R"({"id":"1"})").empty());
}

TEST(ParseLfsLocks, AnErrorMessageOnStdoutIsNotMistakenForALockList) {
    EXPECT_TRUE(parseLfsLocks("Remote \"origin\" does not support the Git LFS locking API")
                    .empty());
}

TEST(LockFor, FindsByExactPath) {
    const auto locks = parseLfsLocks(
        R"([{"id":"1","path":"Assets/a.psd","owner":{"name":"ayse"},"locked_at":"t"}])");
    ASSERT_TRUE(lockFor(locks, "Assets/a.psd").has_value());
    EXPECT_EQ(lockFor(locks, "Assets/a.psd")->owner, "ayse");
    // Not a prefix or suffix match — "a.psd" is a different file.
    EXPECT_FALSE(lockFor(locks, "a.psd").has_value());
    EXPECT_FALSE(lockFor(locks, "Assets/a.psd.meta").has_value());
}
