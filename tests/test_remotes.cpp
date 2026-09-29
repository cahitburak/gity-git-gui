#include "core/git/Remotes.h"

#include <gtest/gtest.h>

using gity::git::checkRemoteName;
using gity::git::RemoteEntry;
using gity::git::RemoteNameProblem;

namespace {
const std::vector<RemoteEntry> kExisting{{"origin", "https://host/a.git", "https://host/a.git"},
                                         {"upstream", "https://host/b.git", ""}};
} // namespace

TEST(CheckRemoteName, AcceptsOrdinaryNames) {
    EXPECT_EQ(checkRemoteName("fork", kExisting), RemoteNameProblem::None);
    EXPECT_EQ(checkRemoteName("team-mirror", kExisting), RemoteNameProblem::None);
    EXPECT_EQ(checkRemoteName("v2.staging", kExisting), RemoteNameProblem::None);
}

TEST(CheckRemoteName, RejectsWhatIsAlreadyThere) {
    EXPECT_EQ(checkRemoteName("origin", kExisting), RemoteNameProblem::AlreadyExists);
}

TEST(CheckRemoteName, RejectsWhitespaceSeparatelyFromOtherIllegalCharacters) {
    // Worth its own answer: a trailing space from a paste is the likeliest
    // mistake here, and "remove the space" is more useful than "illegal".
    EXPECT_EQ(checkRemoteName("my remote", kExisting), RemoteNameProblem::Whitespace);
    EXPECT_EQ(checkRemoteName("tab\there", kExisting), RemoteNameProblem::Whitespace);
}

TEST(CheckRemoteName, RejectsTheCharactersRefsForbid) {
    // A remote becomes refs/remotes/<name>/…, so a name git cannot turn into a
    // ref has to be refused before git is asked to try.
    for (const char* name : {"a~b", "a^b", "a:b", "a?b", "a*b", "a[b", "a\\b", "a..b", "a@{b"}) {
        EXPECT_EQ(checkRemoteName(name, kExisting), RemoteNameProblem::IllegalCharacter) << name;
    }
}

TEST(CheckRemoteName, RejectsLeadingAndTrailingDots) {
    EXPECT_EQ(checkRemoteName(".hidden", kExisting), RemoteNameProblem::LeadingOrTrailingDot);
    EXPECT_EQ(checkRemoteName("trailing.", kExisting), RemoteNameProblem::LeadingOrTrailingDot);
}

TEST(CheckRemoteName, RejectsAnEmptyName) {
    EXPECT_EQ(checkRemoteName("", kExisting), RemoteNameProblem::Empty);
}

TEST(RemoteEntry, APushUrlIsOnlyWorthShowingWhenItDiffers) {
    const RemoteEntry same{"origin", "https://host/a.git", "https://host/a.git"};
    const RemoteEntry split{"origin", "https://host/a.git", "ssh://host/a.git"};
    const RemoteEntry unset{"origin", "https://host/a.git", ""};
    EXPECT_FALSE(same.pushesElsewhere());
    EXPECT_TRUE(split.pushesElsewhere());
    EXPECT_FALSE(unset.pushesElsewhere());
}
