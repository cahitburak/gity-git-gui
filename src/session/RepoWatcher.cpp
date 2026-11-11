#include "RepoWatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSocketNotifier>
#include <QTimer>

#include <cstdint>
#include <limits>

#ifdef Q_OS_LINUX
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace gity::session {
namespace {

/// The key under which the refs fingerprint is kept among the file stamps.
const QString kRefsKey = QStringLiteral("<refs>");

qint64 stampOf(const QString& path) {
    const QFileInfo info(path);
    // Absent and present-but-old must differ: a MERGE_HEAD appearing is the
    // news, whatever its time.
    return info.exists() ? info.lastModified().toMSecsSinceEpoch() + info.size() : -1;
}

/// Gity's own bookkeeping — undo snapshots — lives under refs/gity and must
/// not look like someone else moving a branch.
bool isGityRef(const QString& path) {
    return path.contains(QStringLiteral("/refs/gity"));
}

} // namespace

RepoWatcher::RepoWatcher(const QString& workdir, const QString& gitDir, const QString& commonDir,
                         IgnoreTest ignored, QObject* parent)
    : QObject(parent), workdir_(QDir(workdir).absolutePath()),
      gitDir_(QDir(gitDir).absolutePath()), commonDir_(QDir(commonDir).absolutePath()),
      ignored_(std::move(ignored)) {
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(kDebounceMs);
    connect(debounce_, &QTimer::timeout, this, [this] {
        const int kinds = pending_;
        pending_ = 0;
        if (kinds != 0) {
            emit changed(kinds);
        }
    });

    // --- metadata ---------------------------------------------------------
    const QStringList headFiles{QStringLiteral("HEAD"),        QStringLiteral("MERGE_HEAD"),
                                QStringLiteral("CHERRY_PICK_HEAD"),
                                QStringLiteral("REVERT_HEAD"), QStringLiteral("BISECT_LOG"),
                                QStringLiteral("rebase-merge"), QStringLiteral("rebase-apply"),
                                QStringLiteral("sequencer")};
    for (const QString& name : headFiles) {
        stampKinds_.insert(QDir(gitDir_).filePath(name), Head);
    }
    stampKinds_.insert(QDir(gitDir_).filePath(QStringLiteral("index")), Index);
    stampKinds_.insert(QDir(commonDir_).filePath(QStringLiteral("packed-refs")), Refs);
    stampKinds_.insert(kRefsKey, Refs);

    metadata_ = new QFileSystemWatcher(this);
    metadata_->addPath(gitDir_);
    if (commonDir_ != gitDir_) {
        metadata_->addPath(commonDir_);
    }
    watchRefs(QDir(commonDir_).filePath(QStringLiteral("refs")));
    connect(metadata_, &QFileSystemWatcher::directoryChanged, this,
            &RepoWatcher::metadataChanged);
    static_cast<void>(rescanMetadata());

    // --- working tree -----------------------------------------------------
#ifdef Q_OS_LINUX
    inotify_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_ >= 0) {
        notifier_ = new QSocketNotifier(inotify_, QSocketNotifier::Read, this);
        connect(notifier_, &QSocketNotifier::activated, this,
                [this] { readWorktreeEvents(true); });
        addTree(workdir_);
    }
#endif
}

RepoWatcher::~RepoWatcher() {
#ifdef Q_OS_LINUX
    if (inotify_ >= 0) {
        delete notifier_;
        notifier_ = nullptr;
        ::close(inotify_);
    }
#endif
}

void RepoWatcher::note(int kinds) {
    if (kinds == 0) {
        return;
    }
    pending_ |= kinds;
    debounce_->start(); // restarted by every change, so a burst reports once
}

void RepoWatcher::absorb() {
    static_cast<void>(rescanMetadata());
    readWorktreeEvents(false);
    pending_ = 0;
    debounce_->stop();
}

void RepoWatcher::watchRefs(const QString& directory) {
    if (!QFileInfo(directory).isDir() || isGityRef(directory)) {
        return;
    }
    QStringList directories{directory};
    QDirIterator it(directory, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!isGityRef(path)) {
            directories << path;
        }
    }
    const QStringList watched = metadata_->directories();
    QStringList missing;
    for (const QString& path : std::as_const(directories)) {
        if (!watched.contains(path)) {
            missing << path;
        }
    }
    if (!missing.isEmpty()) {
        metadata_->addPaths(missing);
    }
}

int RepoWatcher::rescanMetadata() {
    QHash<QString, qint64> now;
    for (auto it = stampKinds_.cbegin(); it != stampKinds_.cend(); ++it) {
        if (it.key() != kRefsKey) {
            now.insert(it.key(), stampOf(it.key()));
        }
    }
    // Loose refs, as one fingerprint: every ref file's name, time and size.
    qint64 refs = 0;
    QDirIterator it(QDir(commonDir_).filePath(QStringLiteral("refs")), QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (isGityRef(path)) {
            continue;
        }
        refs = refs * 31 + static_cast<qint64>(qHash(path)) + stampOf(path);
    }
    now.insert(kRefsKey, refs);

    int kinds = 0;
    for (auto it2 = now.cbegin(); it2 != now.cend(); ++it2) {
        if (stamps_.value(it2.key(), std::numeric_limits<qint64>::min()) != it2.value()) {
            kinds |= stampKinds_.value(it2.key());
        }
    }
    const bool first = stamps_.isEmpty();
    stamps_ = std::move(now);
    return first ? 0 : kinds;
}

void RepoWatcher::metadataChanged(const QString& directory) {
    if (directory.startsWith(QDir(commonDir_).filePath(QStringLiteral("refs")))) {
        // A branch in a new folder (feature/x) makes a directory to watch.
        watchRefs(QDir(commonDir_).filePath(QStringLiteral("refs")));
    }
    note(rescanMetadata());
}

void RepoWatcher::addTree(const QString& root) {
#ifdef Q_OS_LINUX
    if (inotify_ < 0) {
        return;
    }
    QStringList pending{root};
    while (!pending.isEmpty()) {
        const QString directory = pending.takeLast();
        constexpr std::uint32_t mask = IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                                       IN_MOVED_TO | IN_ATTRIB | IN_DELETE_SELF | IN_ONLYDIR |
                                       IN_DONT_FOLLOW | IN_EXCL_UNLINK;
        const int wd = inotify_add_watch(inotify_, QFile::encodeName(directory).constData(), mask);
        if (wd < 0) {
            if (errno == ENOSPC && !warnedLimit_) {
                // Said once, plainly: past the system's limit, edits in the
                // directories not reached are noticed only when the window is
                // brought back to the front.
                warnedLimit_ = true;
                qWarning("gity: the system's inotify watch limit was reached while watching %s; "
                         "raise fs.inotify.max_user_watches to watch all of it",
                         qPrintable(workdir_));
            }
            if (errno == ENOSPC) {
                return;
            }
            continue;
        }
        watches_.insert(wd, directory);

        const QFileInfoList children =
            QDir(directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden |
                                          QDir::NoSymLinks);
        for (const QFileInfo& child : children) {
            if (child.fileName() == QStringLiteral(".git")) {
                continue; // the metadata watcher's, and churns on every read
            }
            // A nested repository — a submodule — is a repository of its own,
            // watched when it is opened as one.
            if (QFileInfo::exists(child.filePath() + QStringLiteral("/.git"))) {
                continue;
            }
            const QString relative = QDir(workdir_).relativeFilePath(child.filePath());
            if (ignored_ && ignored_(relative, true)) {
                continue; // build output and dependencies: often most of the tree
            }
            pending << child.filePath();
        }
    }
#else
    Q_UNUSED(root)
#endif
}

void RepoWatcher::readWorktreeEvents(bool report) {
#ifdef Q_OS_LINUX
    if (inotify_ < 0) {
        return;
    }
    alignas(inotify_event) char buffer[16384];
    int kinds = 0;
    while (true) {
        const ssize_t length = ::read(inotify_, buffer, sizeof(buffer));
        if (length <= 0) {
            break; // EAGAIN: nothing more for now
        }
        for (ssize_t offset = 0; offset < length;) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
            offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);

            if ((event->mask & IN_Q_OVERFLOW) != 0) {
                kinds |= Worktree; // too much to say what: just look again
                continue;
            }
            if ((event->mask & IN_IGNORED) != 0) {
                watches_.remove(event->wd);
                continue;
            }
            const QString directory = watches_.value(event->wd);
            if (directory.isEmpty() || event->len == 0) {
                continue;
            }
            const QString name = QFile::decodeName(event->name);
            const QString full = directory + QChar('/') + name;
            const QString relative = QDir(workdir_).relativeFilePath(full);
            const bool isDirectory = (event->mask & IN_ISDIR) != 0;
            if (name == QStringLiteral(".git") || (ignored_ && ignored_(relative, isDirectory))) {
                continue;
            }
            // A directory that appears is watched at once — even while
            // absorbing, since the files about to land in it are not yet
            // anything Gity has read.
            if (isDirectory && (event->mask & (IN_CREATE | IN_MOVED_TO)) != 0) {
                addTree(full);
            }
            kinds |= Worktree;
        }
    }
    if (report) {
        note(kinds);
    }
#else
    Q_UNUSED(report)
#endif
}

} // namespace gity::session
