// Signing in through a browser — ADR-010, the device flow.
//
// The client asks the provider for a code, the user approves it on a web page,
// and a token comes back. **This class holds that token only long enough to
// hand it to git's credential helper**, which is where every other credential
// in this application already lives. Nothing is written to a log, a status
// bar, an error message, or to Gity's own settings.
#pragma once

#include "core/git/DeviceFlow.h"
#include "core/git/Provider.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace gity::session {

class BrowserSignIn : public QObject {
    Q_OBJECT

public:
    explicit BrowserSignIn(QObject* parent = nullptr);
    ~BrowserSignIn() override;

    /// Begins the flow. `username` is the account the resulting credential is
    /// filed under, which is what lets a personal and a work account coexist
    /// on one host.
    void start(git::Provider provider, const QString& host, const QString& username,
               const QString& clientId);
    /// Stops the flow for good. Replies already in flight are ignored when
    /// they land — otherwise one arriving after Cancel restarted the polling
    /// and could still store a token the user had just declined to create.
    void cancel();

    /// Overrides the provider's endpoints. Exists so the flow can be driven
    /// against a local stub in a test rather than only against a real service.
    void setEndpointsOverride(const git::DeviceFlowEndpoints& endpoints);

signals:
    /// Show `userCode` and send the user to `verificationUrl`.
    void codeReady(const QString& userCode, const QString& verificationUrl);
    void finished(bool ok, const QString& message);

private:
    void poll();
    void storeCredential(const QString& token);

    QNetworkAccessManager* network_ = nullptr;
    QTimer* pollTimer_ = nullptr;
    git::DeviceFlowEndpoints endpoints_;
    git::DeviceFlowEndpoints override_;
    git::DeviceCode code_;
    QString host_;
    QString username_;
    QString clientId_;
    int elapsedSeconds_ = 0;
    bool cancelled_ = false;
};

} // namespace gity::session
