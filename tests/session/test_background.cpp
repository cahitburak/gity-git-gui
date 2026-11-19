// Work run on a thread of its own (Background.h).
#include "session/Background.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>

#include <atomic>

using gity::session::runInBackground;

TEST(Background, TheThreadIsCleanedUpThoughTheThreadThatAskedHasEnded) {
    // A repository's worker asks for a keyring lookup, and the repository is
    // closed before the lookup ends. The thread object's deleteLater went to
    // the worker's event loop, which was gone by then.
    QThread asker;
    asker.start();
    auto* context = new QObject;
    context->moveToThread(&asker);

    std::atomic<QThread*> running{nullptr};
    QMetaObject::invokeMethod(
        context,
        [context, &running] {
            runInBackground(
                context,
                [&running] {
                    running = QThread::currentThread();
                    QThread::msleep(200);
                },
                [] {});
        },
        Qt::BlockingQueuedConnection);
    asker.quit();
    asker.wait();
    delete context;

    while (running.load() == nullptr) {
        QThread::msleep(1);
    }
    const QPointer<QThread> thread(running.load());
    QEventLoop loop;
    QTimer::singleShot(800, &loop, &QEventLoop::quit);
    loop.exec();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(thread.isNull()) << "the finished thread's object was never deleted";
}
