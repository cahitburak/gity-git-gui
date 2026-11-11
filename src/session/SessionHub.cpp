#include "SessionHub.h"

#include <QDir>
#include <QMetaMethod>
#include <QTimer>

namespace gity::session {

SessionHub::SessionHub(QObject* parent) : QObject(parent) {
    // Checks the closed sessions still finishing their work.
    reaper_ = new QTimer(this);
    reaper_->setInterval(500);
    connect(reaper_, &QTimer::timeout, this, [this] {
        std::erase_if(retiring_, [](const auto& session) { return !session->busy(); });
        if (retiring_.empty()) {
            reaper_->stop();
        }
    });
}

QString SessionHub::keyFor(const QString& path) {
    return path.isEmpty() ? path : QDir(path).absolutePath();
}

SessionHub::~SessionHub() {
    stopForwarding();
}

bool SessionHub::activate(const QString& path) {
    const QString key = keyFor(path);
    auto found = sessions_.find(key);
    if (found == sessions_.end()) {
        auto created = std::make_unique<RepoSession>();
        const RepoSession* raw = created.get();
        // Work started in a repository keeps going when another tab is
        // brought forward; its outcome is still worth a word.
        connect(
            created.get(), &RepoSession::networkFinished, this,
            [this, raw, key](const QString& verb, bool ok, const QString& summary, const QString&) {
                if (raw != current_) {
                    emit backgroundNetworkFinished(key, verb, ok, summary);
                }
            });
        connect(created.get(), &RepoSession::cloneFinished, this,
                [this, raw](const QString& cloned) {
                    if (raw != current_) {
                        emit backgroundCloneFinished(cloned);
                    }
                });
        found = sessions_.emplace(key, std::move(created)).first;
    }
    if (found->second.get() == current_) {
        return false;
    }

    stopForwarding();
    current_ = found->second.get();
    // Every signal, forwarded to the relay the window is wired to. Results
    // still on their way from the repository being left are not forwarded:
    // a preview for one repository must never be acted on in another.
    const QMetaObject& meta = RepoSession::staticMetaObject;
    for (int i = meta.methodOffset(); i < meta.methodCount(); ++i) {
        const QMetaMethod method = meta.method(i);
        if (method.methodType() == QMetaMethod::Signal) {
            forwarding_ << connect(current_, method, &relay_, method);
        }
    }
    return true;
}

void SessionHub::retire(const QString& path) {
    const auto found = sessions_.find(keyFor(path));
    if (found == sessions_.end()) {
        return;
    }
    std::unique_ptr<RepoSession> session = std::move(found->second);
    sessions_.erase(found);
    opened_.remove(session.get());
    if (session.get() == current_) {
        stopForwarding();
        current_ = &relay_;
    }
    // A fetch is stopped; a commit or rebase step finishes first. Destroying
    // a busy session would block the window until it did.
    session->cancelNetwork();
    if (!session->busy()) {
        return; // destroyed here, idle
    }
    retiring_.push_back(std::move(session));
    reaper_->start();
}

void SessionHub::markOpened() {
    opened_.insert(current_);
}

bool SessionHub::currentNeverOpened() const {
    return current_ != &relay_ && !opened_.contains(current_);
}

QString SessionHub::currentPath() const {
    for (const auto& [path, session] : sessions_) {
        if (session.get() == current_) {
            return path;
        }
    }
    return {};
}

QStringList SessionHub::busyRepositories() const {
    QStringList busy;
    for (const auto& [path, session] : sessions_) {
        if (session->busy()) {
            busy << QDir(path).dirName();
        }
    }
    for (const auto& session : retiring_) {
        if (session->busy()) {
            busy << tr("a closed tab");
        }
    }
    return busy;
}

void SessionHub::stopForwarding() {
    for (const QMetaObject::Connection& connection : forwarding_) {
        disconnect(connection);
    }
    forwarding_.clear();
}

} // namespace gity::session
