// Work that would block the thread it was asked from — a git command, a
// keyring lookup — run on a thread of its own, with the result handed back
// on `context`'s thread.
//
// For the few callers that are not a repository's session: the Settings
// panels and the credential pages. A repository's own work runs on its
// session's worker (ADR-004).
#pragma once

#include <QObject>
#include <QThread>

#include <functional>
#include <memory>

namespace gity::session {

/// Runs `work` on a new thread, then `done` on `context`'s thread. If
/// `context` is destroyed first, `done` is simply not called.
inline void runInBackground(QObject* context, std::function<void()> work,
                            std::function<void()> done) {
    QThread* thread = QThread::create(std::move(work));
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    QObject::connect(thread, &QThread::finished, context, std::move(done));
    thread->start();
}

/// As above, carrying a value from `work` to `done`.
template <typename T>
void runInBackground(QObject* context, std::function<T()> work, std::function<void(T)> done) {
    auto result = std::make_shared<T>();
    runInBackground(
        context, [result, work = std::move(work)] { *result = work(); },
        [result, done = std::move(done)] { done(std::move(*result)); });
}

} // namespace gity::session
