// Response shapes from the providers' own documentation for RFC 8628. Not
// captured from a live exchange: reaching one needs a registered OAuth
// application, which is the user's to create — noted in ROADMAP.md so the
// distinction is not lost.
#include "core/git/DeviceFlow.h"

#include <gtest/gtest.h>

using namespace gity::git;

TEST(ParseDeviceCode, TheJsonForm) {
    const DeviceCode code = parseDeviceCode(
        R"({"device_code":"3584d83530557fdd1f46af8289938c8ef79f9dc5",)"
        R"("user_code":"WDJB-MJHT",)"
        R"("verification_uri":"https://github.com/login/device",)"
        R"("expires_in":900,"interval":5})");
    EXPECT_TRUE(code.valid());
    EXPECT_EQ(code.deviceCode, "3584d83530557fdd1f46af8289938c8ef79f9dc5");
    EXPECT_EQ(code.userCode, "WDJB-MJHT");
    EXPECT_EQ(code.verificationUrl, "https://github.com/login/device");
    EXPECT_EQ(code.expiresInSeconds, 900);
    EXPECT_EQ(code.intervalSeconds, 5);
}

TEST(ParseDeviceCode, TheFormEncodedForm) {
    // GitHub answers this endpoint with form encoding unless asked for JSON,
    // and has ignored the Accept header before. Handling both is why the flow
    // does not break on a provider that does the same.
    const DeviceCode code = parseDeviceCode(
        "device_code=3584d835&user_code=WDJB-MJHT"
        "&verification_uri=https://github.com/login/device&expires_in=900&interval=5");
    EXPECT_TRUE(code.valid());
    EXPECT_EQ(code.userCode, "WDJB-MJHT");
    EXPECT_EQ(code.verificationUrl, "https://github.com/login/device");
    EXPECT_EQ(code.intervalSeconds, 5);
}

TEST(ParseDeviceCode, AMissingIntervalFallsBackAndNeverToZero) {
    // A zero interval would poll as fast as the network allows and be rate
    // limited immediately.
    EXPECT_EQ(parseDeviceCode(R"({"device_code":"a","user_code":"b",)"
                              R"("verification_uri":"c"})")
                  .intervalSeconds,
              5);
    EXPECT_EQ(parseDeviceCode(R"({"device_code":"a","user_code":"b",)"
                              R"("verification_uri":"c","interval":0})")
                  .intervalSeconds,
              1);
}

TEST(ParseDeviceCode, AnErrorResponseIsNotAValidCode) {
    const DeviceCode code =
        parseDeviceCode(R"({"error":"unauthorized_client","error_description":"no"})");
    EXPECT_FALSE(code.valid());
}

TEST(ParsePollResponse, PendingWhileTheUserIsStillApproving) {
    const PollResult result = parsePollResponse(
        R"({"error":"authorization_pending","error_description":"pending"})");
    EXPECT_EQ(result.state, PollState::Pending);
    EXPECT_TRUE(result.accessToken.empty());
}

TEST(ParsePollResponse, SlowDownIsItsOwnAnswer) {
    // Distinct from pending: the interval has to grow, and treating it as
    // pending keeps polling at a rate the provider has just refused.
    EXPECT_EQ(parsePollResponse(R"({"error":"slow_down","interval":10})").state,
              PollState::SlowDown);
}

TEST(ParsePollResponse, DeniedAndExpiredAreToldApart) {
    EXPECT_EQ(parsePollResponse(R"({"error":"access_denied"})").state, PollState::Denied);
    EXPECT_EQ(parsePollResponse(R"({"error":"expired_token"})").state, PollState::Expired);
}

TEST(ParsePollResponse, AGrantCarriesTheToken) {
    const PollResult result = parsePollResponse(
        R"({"access_token":"gho_exampleNotARealToken0000000000000000",)"
        R"("token_type":"bearer","scope":"repo"})");
    EXPECT_EQ(result.state, PollState::Granted);
    EXPECT_EQ(result.accessToken, "gho_exampleNotARealToken0000000000000000");
}

TEST(ParsePollResponse, AGrantInFormEncodingToo) {
    const PollResult result =
        parsePollResponse("access_token=gho_abc123&scope=repo&token_type=bearer");
    EXPECT_EQ(result.state, PollState::Granted);
    EXPECT_EQ(result.accessToken, "gho_abc123");
}

TEST(ParsePollResponse, GarbageIsAFailureNotAGrant) {
    // The consequence of getting this wrong is handing an empty string to the
    // credential helper as though it were a token.
    EXPECT_EQ(parsePollResponse("").state, PollState::Failed);
    EXPECT_EQ(parsePollResponse("<html>502 Bad Gateway</html>").state, PollState::Failed);
    EXPECT_TRUE(parsePollResponse("<html>502</html>").accessToken.empty());
}

TEST(EndpointsFor, KnownProvidersAndSelfHostedInstances) {
    const DeviceFlowEndpoints github = endpointsFor(Provider::GitHub, "github.com");
    EXPECT_EQ(github.codeUrl, "https://github.com/login/device/code");
    EXPECT_EQ(github.tokenUrl, "https://github.com/login/oauth/access_token");

    // Self-hosted serves the same grant from its own domain.
    const DeviceFlowEndpoints enterprise = endpointsFor(Provider::GitHub, "github.studio.example");
    EXPECT_EQ(enterprise.codeUrl, "https://github.studio.example/login/device/code");

    EXPECT_TRUE(endpointsFor(Provider::GitLab, "gitlab.com").valid());
    EXPECT_TRUE(endpointsFor(Provider::Gitea, "gitea.com").valid());
}

TEST(EndpointsFor, ProvidersWithoutADeviceGrantSaySoRatherThanGuessing) {
    // Guessing a URL here would fail at the request with a confusing error
    // instead of a clear "this provider is not supported".
    EXPECT_FALSE(endpointsFor(Provider::Bitbucket, "bitbucket.org").valid());
    EXPECT_FALSE(endpointsFor(Provider::AzureDevOps, "dev.azure.com").valid());
    EXPECT_FALSE(endpointsFor(Provider::Other, "git.studio.example").valid());
}

TEST(ParseDeviceCode, AnAbsurdIntervalIsClamped) {
    // Multiplied out to milliseconds this overflows, and a negative timer
    // interval fires immediately — turning a slow poll into a tight loop
    // against someone else's service. RFC 8628 intervals are single digits.
    EXPECT_EQ(parseDeviceCode(R"({"device_code":"a","user_code":"b",)"
                              R"("verification_uri":"c","interval":999999999})")
                  .intervalSeconds,
              60);
    EXPECT_EQ(parseDeviceCode(R"({"device_code":"a","user_code":"b",)"
                              R"("verification_uri":"c","interval":-5})")
                  .intervalSeconds,
              1);
}
