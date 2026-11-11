// The window's repository sessions (ADR-004): one per open repository, by
// absolute path, and the relay the window is wired to.
//
// The window connects to relay() once, at construction. Whichever session is
// on screen forwards every signal to it, so a result from a repository in
// another tab never reaches the window — a preview asked for in one
// repository is never acted on in another. Requests go to current(), which is
// the relay itself while no repository is on screen.
#pragma once

#include "RepoSession.h"

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <vector>

class QTimer;

namespace gity::session {

class SessionHub : public QObject {
    Q_OBJECT

public:
    explicit SessionHub(QObject* parent = nullptr);
    ~SessionHub() override;

    SessionHub(const SessionHub&) = delete;
    SessionHub& operator=(const SessionHub&) = delete;

    /// What the window is wired to.
    [[nodiscard]] RepoSession* relay() noexcept { return &relay_; }

    /// The session requests go to: the one on screen, or the relay.
    [[nodiscard]] RepoSession* current() const noexcept { return current_; }

    /// Makes `path`'s session the one on screen, creating it on first use.
    /// False when it already was.
    bool activate(const QString& path);
    /// Drops `path`'s session: a network command is stopped, anything else
    /// is let finish first, and the session is destroyed once idle.
    void retire(const QString& path);

    /// The session on screen has opened its repository.
    void markOpened();
    /// The session on screen is a repository's — not the relay — and that
    /// repository has never opened.
    [[nodiscard]] bool currentNeverOpened() const;
    /// The absolute path of the session on screen; empty for the relay.
    [[nodiscard]] QString currentPath() const;

    /// Repositories git is still changing — open ones by folder name, closed
    /// ones as "a closed tab" — for asking before quitting.
    [[nodiscard]] QStringList busyRepositories() const;

    /// How sessions are keyed: the same directory reaches here spelled more
    /// than one way. The rule RepositoryTabs uses for its tabs.
    [[nodiscard]] static QString keyFor(const QString& path);

signals:
    /// A repository not on screen finished a fetch, pull or push.
    void backgroundNetworkFinished(const QString& path, const QString& verb, bool ok,
                                   const QString& summary);
    /// A clone started from a repository no longer on screen finished.
    void backgroundCloneFinished(const QString& clonedPath);

private:
    void stopForwarding();

    RepoSession relay_;
    std::map<QString, std::unique_ptr<RepoSession>> sessions_;
    /// Closed while still busy; destroyed once they are idle.
    std::vector<std::unique_ptr<RepoSession>> retiring_;
    QTimer* reaper_ = nullptr;
    RepoSession* current_ = &relay_;
    /// Sessions that have opened their repository at least once.
    QSet<const RepoSession*> opened_;
    QList<QMetaObject::Connection> forwarding_;
};

} // namespace gity::session
