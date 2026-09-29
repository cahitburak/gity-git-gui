// The username and password prompts below were captured from a live run: git
// was pointed at a logging stub through GIT_ASKPASS with an isolated config so
// no credential helper could answer first, and it passed exactly these two
// strings. The ssh ones are OpenSSH's own format strings from readpass.c.
#include "core/git/AskpassPrompt.h"

#include <gtest/gtest.h>

using gity::git::AskpassKind;
using gity::git::parseAskpassPrompt;

TEST(AskpassPrompt, UsernameIsShownInTheClear) {
    const auto request = parseAskpassPrompt("Username for 'https://github.com': ");
    EXPECT_EQ(request.kind, AskpassKind::Username);
    EXPECT_EQ(request.subject, "https://github.com");
    EXPECT_TRUE(request.echo());
}

TEST(AskpassPrompt, PasswordIsMasked) {
    const auto request = parseAskpassPrompt("Password for 'https://someone@github.com': ");
    EXPECT_EQ(request.kind, AskpassKind::Password);
    EXPECT_EQ(request.subject, "https://someone@github.com");
    EXPECT_FALSE(request.echo());
}

TEST(AskpassPrompt, KeyPassphraseIsNotMistakenForAPassword) {
    // "Enter passphrase for key '...'" contains neither "password" nor
    // "username", and it unlocks a local key rather than authenticating to a
    // server, which is worth saying differently in the dialog.
    const auto request =
        parseAskpassPrompt("Enter passphrase for key '/home/user/.ssh/id_ed25519': ");
    EXPECT_EQ(request.kind, AskpassKind::Passphrase);
    EXPECT_EQ(request.subject, "/home/user/.ssh/id_ed25519");
    EXPECT_FALSE(request.echo());
}

TEST(AskpassPrompt, TheHostKeyQuestionIsAQuestionNotACredential) {
    // This arrives through the same channel as a password. Showing it in a
    // masked field would be useless, and answering it automatically would
    // defeat the check that exists to catch a machine-in-the-middle.
    const auto request = parseAskpassPrompt(
        "The authenticity of host 'github.com (140.82.121.4)' can't be established.\n"
        "ED25519 key fingerprint is SHA256:+DiY3wvvV6TuJJhbpZisF/zLDA0zPMSvHdkr4UvCOqU.\n"
        "Are you sure you want to continue connecting (yes/no/[fingerprint])? ");
    EXPECT_EQ(request.kind, AskpassKind::HostKey);
    EXPECT_TRUE(request.echo());
}

TEST(AskpassPrompt, AnUnrecognisedPromptIsMaskedRatherThanShown) {
    // Guessing "not a secret" for unfamiliar wording is the mistake that
    // leaks; guessing the other way only inconveniences.
    const auto request = parseAskpassPrompt("Enter your one-time code: ");
    EXPECT_EQ(request.kind, AskpassKind::Unknown);
    EXPECT_FALSE(request.echo());
}

TEST(AskpassPrompt, APromptWithoutASubjectStillClassifies) {
    EXPECT_EQ(parseAskpassPrompt("Password: ").kind, AskpassKind::Password);
    EXPECT_TRUE(parseAskpassPrompt("Password: ").subject.empty());
}
