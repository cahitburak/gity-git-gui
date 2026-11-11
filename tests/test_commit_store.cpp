// The commit metadata arena (ADR-005): every row's strings live in one
// buffer, so the views handed out must survive the buffer growing.
#include "core/model/CommitStore.h"

#include <gtest/gtest.h>

#include <string>

using gity::model::CommitStore;

namespace {

git_oid oidFrom(unsigned char seed) {
    git_oid oid{};
    for (auto& byte : oid.id) {
        byte = seed;
    }
    return oid;
}

} // namespace

TEST(CommitStore, StringsSurviveTheArenaGrowing) {
    CommitStore store;
    // Enough rows that the arena reallocates several times.
    for (int i = 0; i < 5000; ++i) {
        static_cast<void>(store.append(oidFrom(static_cast<unsigned char>(i)),
                                       "summary " + std::to_string(i),
                                       "author " + std::to_string(i % 7), 1700000000 + i));
    }
    ASSERT_EQ(store.size(), 5000u);
    EXPECT_EQ(store.summary(0), "summary 0");
    EXPECT_EQ(store.summary(4999), "summary 4999");
    EXPECT_EQ(store.author(13), "author 6");
    EXPECT_EQ(store.when(10), 1700000010);
}

TEST(CommitStore, ShortIdsAreHex) {
    CommitStore store;
    static_cast<void>(store.append(oidFrom(0xab), "s", "a", 0));
    EXPECT_EQ(store.shortId(0), "abababab");
    EXPECT_EQ(store.shortId(0, 4), "abab");
}

TEST(CommitStore, EmptyStringsAreFine) {
    CommitStore store;
    static_cast<void>(store.append(oidFrom(1), "", "", 0));
    EXPECT_TRUE(store.summary(0).empty());
    EXPECT_TRUE(store.author(0).empty());
}

TEST(CommitStore, ClearEmptiesIt) {
    CommitStore store;
    static_cast<void>(store.append(oidFrom(1), "s", "a", 0));
    store.clear();
    EXPECT_TRUE(store.empty());
}
