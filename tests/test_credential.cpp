// The two ends of `git credential`. A mistake here either shows the wrong
// credential or — worse — writes one with a field smuggled into another.
#include "core/git/Credential.h"

#include <gtest/gtest.h>

using namespace gity::git;

TEST(Credential, HttpsUrlsIdentifyTheirHost) {
    Credential c;
    ASSERT_TRUE(credentialForUrl("https://github.com/owner/repo.git", false, &c));
    EXPECT_EQ(c.protocol, "https");
    EXPECT_EQ(c.host, "github.com");
    EXPECT_TRUE(c.path.empty()) << "path only when credential.useHttpPath is set";
    EXPECT_TRUE(c.username.empty());
}

TEST(Credential, ThePathIsIncludedOnlyWhenAskedFor) {
    Credential c;
    ASSERT_TRUE(credentialForUrl("https://dev.azure.com/org/project/_git/repo", true, &c));
    EXPECT_EQ(c.path, "org/project/_git/repo");
}

TEST(Credential, AUsernameInTheUrlSelectsTheAccountButAPasswordIsNotCarried) {
    Credential c;
    ASSERT_TRUE(credentialForUrl("https://someone:secret@GitLab.example.com:8443/g/r.git", false,
                                 &c));
    EXPECT_EQ(c.username, "someone");
    EXPECT_TRUE(c.password.empty());
    EXPECT_EQ(c.host, "gitlab.example.com:8443");
}

TEST(Credential, SshAndLocalRemotesHaveNoStoredCredential) {
    EXPECT_FALSE(credentialForUrl("git@github.com:owner/repo.git", false, nullptr));
    EXPECT_FALSE(credentialForUrl("ssh://git@github.com/owner/repo.git", false, nullptr));
    EXPECT_FALSE(credentialForUrl("/home/me/repo", false, nullptr));
    EXPECT_FALSE(credentialForUrl("file:///home/me/repo", false, nullptr));
}

TEST(Credential, FillOutputParses) {
    const Credential c = parseCredential(
        "protocol=https\nhost=github.com\nusername=me\npassword=tok=with=equals\n\nignored=1\n");
    EXPECT_EQ(c.username, "me");
    EXPECT_EQ(c.password, "tok=with=equals") << "only the first = separates key from value";
    EXPECT_TRUE(c.hasSecret());
}

TEST(Credential, SerializationRoundTripsAndEndsWithABlankLine) {
    Credential c;
    c.protocol = "https";
    c.host = "github.com";
    c.username = "me";
    c.password = "tok";
    const std::string text = serializeCredential(c);
    EXPECT_EQ(text, "protocol=https\nhost=github.com\nusername=me\npassword=tok\n\n");
    const Credential back = parseCredential(text);
    EXPECT_EQ(back.password, "tok");
}

TEST(Credential, ANewlineCannotBeWritten) {
    Credential c;
    c.protocol = "https";
    c.host = "github.com";
    c.username = "me\nhost=evil.example";
    EXPECT_FALSE(isWritable(c));
    c.username = "me";
    c.password = std::string("a\0b", 3);
    EXPECT_FALSE(isWritable(c));
    c.password = "fine";
    EXPECT_TRUE(isWritable(c));
}
