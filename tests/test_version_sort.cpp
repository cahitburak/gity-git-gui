// Tag order in the sidebar. As plain strings, `v1.10` sorted between `v1.1`
// and `v1.2`, so the newest release of a series sat in the middle of it.
#include "core/git/Refs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using gity::git::versionLess;

std::vector<std::string> sorted(std::vector<std::string> names) {
    std::sort(names.begin(), names.end(),
              [](const std::string& a, const std::string& b) { return versionLess(a, b); });
    return names;
}

} // namespace

TEST(VersionSort, NumbersCompareByValue) {
    EXPECT_EQ(sorted({"v1.10", "v1.2", "v1.9", "v1.1"}),
              (std::vector<std::string>{"v1.1", "v1.2", "v1.9", "v1.10"}));
    EXPECT_EQ(sorted({"v10.0.0", "v2.0.0", "v2.0.10", "v2.0.9"}),
              (std::vector<std::string>{"v2.0.0", "v2.0.9", "v2.0.10", "v10.0.0"}));
}

TEST(VersionSort, TextStillSortsAsText) {
    EXPECT_EQ(sorted({"release", "beta", "alpha"}),
              (std::vector<std::string>{"alpha", "beta", "release"}));
    EXPECT_TRUE(versionLess("build-2", "build-10"));
    EXPECT_TRUE(versionLess("a1", "b0")) << "text before the number decides first";
}

TEST(VersionSort, APrefixSortsFirstAsGitDoes) {
    // git tag --sort=version:refname, without versionsort.suffix, gives this
    // order too; treating -rc as earlier would be a guess about naming.
    EXPECT_TRUE(versionLess("v1.0", "v1.0-rc1"));
    EXPECT_TRUE(versionLess("v1.0-rc1", "v1.0-rc2"));
    EXPECT_TRUE(versionLess("v1.0-rc2", "v1.0-rc10"));
}

TEST(VersionSort, IsAStrictOrder) {
    // std::sort needs a strict weak ordering: nothing is less than itself, and
    // names differing only in leading zeros still order one way round.
    for (const char* name : {"", "v1", "v01", "abc", "1.2.3"}) {
        EXPECT_FALSE(versionLess(name, name)) << name;
    }
    EXPECT_NE(versionLess("v01", "v1"), versionLess("v1", "v01"));
    EXPECT_TRUE(versionLess("", "a"));
}

TEST(VersionSort, HugeNumbersDoNotOverflow) {
    EXPECT_TRUE(versionLess("v99999999999999999999999", "v100000000000000000000000"));
}
