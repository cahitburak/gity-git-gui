#include "BrowserSignIn.h"

#include "session/GitProcess.h"

#include <QDesktopServices>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <memory>

namespace gity::session {
namespace {

QNetworkRequest formRequest(const QString& url) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    // Ask for JSON; the parser copes if the provider ignores this, which some
    // do, but asking costs nothing.
    request.setRawHeader("Accept", "application/json");
    // TLS is not negotiable here: the reply carries a credential.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QByteArray formBody(const QList<QPair<QString, QString>>& fields) {
    QUrlQuery query;
    for (const auto& [key, value] : fields) {
        query.addQueryItem(key, value);
    }
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

} // namespace

BrowserSignIn::BrowserSignIn(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)),
      pollTimer_(new QTimer(this)) {
    pollTimer_->setSingleShot(true);
    connect(pollTimer_, &QTimer::timeout, this, &BrowserSignIn::poll);
}

BrowserSignIn::~BrowserSignIn() = default;

void BrowserSignIn::setEndpointsOverride(const git::DeviceFlowEndpoints& endpoints) {
    override_ = endpoints;
}

void BrowserSignIn::cancel() {
    cancelled_ = true;
    pollTimer_->stop();
}

void BrowserSignIn::start(git::Provider provider, const QString& host, const QString& username,
                          const QString& clientId) {
    host_ = host;
    username_ = username;
    clientId_ = clientId;
    elapsedSeconds_ = 0;
    cancelled_ = false;

    endpoints_ = override_.valid() ? override_
                                   : git::endpointsFor(provider, host.toStdString());
    if (!endpoints_.valid()) {
        emit finished(false, tr("%1 does not offer the device sign-in flow, or this client "
                                "does not know its endpoints. Add the account and let git ask "
                                "for a token instead.")
                                 .arg(host));
        return;
    }
    if (clientId_.isEmpty()) {
        emit finished(false, tr("No client ID is configured for %1.").arg(host));
        return;
    }

    QNetworkReply* reply = network_->post(
        formRequest(QString::fromStdString(endpoints_.codeUrl)),
        formBody({{QStringLiteral("client_id"), clientId_},
                  {QStringLiteral("scope"), QString::fromStdString(endpoints_.scope)}}));

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (cancelled_) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(false, tr("Could not reach %1: %2").arg(host_, reply->errorString()));
            return;
        }
        code_ = git::parseDeviceCode(reply->readAll().toStdString());
        if (!code_.valid()) {
            // Deliberately not echoing the body: a failed exchange can still
            // contain material that should not be shown or logged.
            emit finished(false, tr("%1 did not return a sign-in code.").arg(host_));
            return;
        }

        const QString verification = QString::fromStdString(code_.verificationUrl);
        // Opened only if it is a web page. The address comes from the
        // server's reply, and handing an arbitrary scheme to the desktop —
        // file:, or a registered application handler — is not what "open the
        // sign-in page" means.
        const QUrl page(verification);
        if (page.scheme() != QLatin1String("https") && page.scheme() != QLatin1String("http")) {
            emit finished(false, tr("%1 returned a sign-in address that is not a web page.")
                                     .arg(host_));
            return;
        }
        emit codeReady(QString::fromStdString(code_.userCode), verification);
        QDesktopServices::openUrl(page);
        pollTimer_->start(code_.intervalSeconds * 1000);
    });
}

void BrowserSignIn::poll() {
    if (cancelled_) {
        return;
    }
    elapsedSeconds_ += code_.intervalSeconds;
    if (code_.expiresInSeconds > 0 && elapsedSeconds_ > code_.expiresInSeconds) {
        emit finished(false, tr("The sign-in code expired before it was approved."));
        return;
    }

    QNetworkReply* reply = network_->post(
        formRequest(QString::fromStdString(endpoints_.tokenUrl)),
        formBody({{QStringLiteral("client_id"), clientId_},
                  {QStringLiteral("device_code"), QString::fromStdString(code_.deviceCode)},
                  {QStringLiteral("grant_type"),
                   QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")}}));

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (cancelled_) {
            return;
        }
        // A poll that fails at the transport level is retried rather than
        // abandoned: a dropped packet should not lose an approval the user has
        // already given.
        if (reply->error() != QNetworkReply::NoError && reply->bytesAvailable() == 0) {
            pollTimer_->start(code_.intervalSeconds * 1000);
            return;
        }

        const git::PollResult result = git::parsePollResponse(reply->readAll().toStdString());
        switch (result.state) {
        case git::PollState::Granted:
            storeCredential(QString::fromStdString(result.accessToken));
            return;
        case git::PollState::Pending:
            pollTimer_->start(code_.intervalSeconds * 1000);
            return;
        case git::PollState::SlowDown:
            // The provider has just refused this rate; continuing at it would
            // be ignoring an explicit instruction. Capped, so a provider that
            // answers slow_down every time cannot walk the interval up until
            // the multiplication overflows.
            code_.intervalSeconds = std::min(code_.intervalSeconds + 5, 60);
            pollTimer_->start(code_.intervalSeconds * 1000);
            return;
        case git::PollState::Denied:
            emit finished(false, tr("Sign-in was declined."));
            return;
        case git::PollState::Expired:
            emit finished(false, tr("The sign-in code expired before it was approved."));
            return;
        case git::PollState::Failed:
            break;
        }
        emit finished(false, result.detail.empty()
                                 ? tr("Sign-in failed.")
                                 : tr("Sign-in failed: %1")
                                       .arg(QString::fromStdString(result.detail)));
    });
}

void BrowserSignIn::storeCredential(const QString& token) {
    // ADR-010: the token goes to git's credential helper and nowhere else, so
    // it lands in the keychain the user already configured and git on the
    // command line can use it too. It is written to git's stdin rather than a
    // command line, where it would be visible in the process list.
    QByteArray input;
    input += "protocol=https\n";
    input += "host=" + host_.toUtf8() + "\n";
    input += "username=" + username_.toUtf8() + "\n";
    input += "password=" + token.toUtf8() + "\n\n";

    // On a thread of its own. The helper can take as long as a person does:
    // a keychain that is locked asks to be unlocked, and run here on the UI
    // thread that froze the whole window until the prompt was answered.
    auto result = std::make_shared<GitResult>();
    QThread* store = QThread::create([result, input] {
        *result = GitProcess::runWithInput(
            QDir::homePath(), {QStringLiteral("credential"), QStringLiteral("approve")}, input);
    });
    // The thread object cleans itself up; the report goes through `this` as
    // context, so if the panel has gone by then it is simply not delivered.
    connect(store, &QThread::finished, store, &QObject::deleteLater);
    connect(store, &QThread::finished, this, [this, result] {
        // A cancel this late cannot take the token back — the helper already
        // has it — but there is nobody left waiting for the message.
        if (cancelled_) {
            return;
        }
        if (!result->ok()) {
            emit finished(false, tr("Signed in, but the credential could not be stored: %1")
                                     .arg(result->firstProblemLine()));
            return;
        }
        emit finished(true, tr("Signed in as %1 on %2. The token is held by your credential "
                               "helper, not by Gity.")
                                .arg(username_, host_));
    });
    store->start();
}

} // namespace gity::session
