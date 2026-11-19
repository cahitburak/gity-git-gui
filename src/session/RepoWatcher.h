// Noticing changes made outside Gity (ADR-004): an edit in an editor, a commit
// or a checkout in a terminal, a branch moved by another tool.
//
// Two parts, because they differ in size and in what is portable:
//
//   * The repository's own metadata — HEAD, the index, refs, and the markers
//     of a merge or rebase in progress. A handful of directories, watched on
//     every platform with QFileSystemWatcher. git writes each of these files
//     by renaming a lock file over it, so a change always shows up as a change
//     to the directory that holds it.
//
//   * The working tree, which can hold tens of thousands of directories.
//     QFileSystemWatcher needs a handle per directory (on macOS, a file
//     descriptor), which is why ADR-004 rules it out here. On Linux the tree
//     is watched with inotify directly, skipping ignored directories. On
//     macOS and Windows a native recursive watcher (FSEvents,
//     ReadDirectoryChangesW) is still to come; until then the window re-reads
//     the status when it is brought back to the front.
//
// Changes are debounced — a checkout touches thousands of files — and
// reported as kinds, so a saved file costs a status read and a moved branch a
// history walk, not the other way round.
//
// Lives on the repository's worker thread: deciding whether a path is ignored
// asks libgit2, and the repository handle belongs to that thread.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class QFileSystemWatcher;
class QSocketNotifier;
class QTimer;

namespace gity::session {

class RepoWatcher : public QObject {
    Q_OBJECT

public:
    /// What changed, as flags.
    enum Change : int {
        Worktree = 1, ///< A file in the working tree.
        Index = 2,    ///< What is staged.
        Refs = 4,     ///< A branch, tag or remote-tracking branch.
        Head = 8, ///< The checked-out branch or commit, or a merge or rebase starting or ending.
    };

    /// True when the path, relative to the working tree, is ignored by git.
    using IgnoreTest = std::function<bool(const QString& relative, bool isDirectory)>;

    /// `gitDir` holds HEAD and the index; `commonDir` holds refs — the same
    /// directory except in a linked worktree.
    RepoWatcher(const QString& workdir, const QString& gitDir, const QString& commonDir,
                IgnoreTest ignored, QObject* parent = nullptr);
    ~RepoWatcher() override;

    RepoWatcher(const RepoWatcher&) = delete;
    RepoWatcher& operator=(const RepoWatcher&) = delete;

    [[nodiscard]] QString workdir() const { return workdir_; }

    /// The working tree is watched, not only the metadata.
    [[nodiscard]] bool watchesWorktree() const noexcept { return inotify_ >= 0; }

    /// Forgets every change seen so far. Called just before Gity reads the
    /// repository itself: whatever happened up to now is in what it is about
    /// to read, and reporting it again would read it twice. Changes after
    /// this are reported as usual.
    void absorb();

    static constexpr int kDebounceMs = 250;

signals:
    /// Some time after the last change in a burst; `kinds` is Change flags.
    void changed(int kinds);

private:
    void note(int kinds);
    void metadataChanged(const QString& directory);
    /// Re-reads the modification times of the metadata files; returns which
    /// kinds differ from what was last seen.
    int rescanMetadata();
    void watchRefs(const QString& directory);

    /// Gity's own bookkeeping refs, under refs/gity, which must not look like
    /// someone else moving a branch.
    [[nodiscard]] bool isGityRef(const QString& path) const;

    void addTree(const QString& directory);
    /// Stops watching `directory` and everything below it: moved out of the
    /// tree, or become a repository of its own.
    void unwatchTree(const QString& directory);
    void readWorktreeEvents(bool report);

    QString workdir_;
    QString gitDir_;
    QString commonDir_;
    IgnoreTest ignored_;

    QFileSystemWatcher* metadata_ = nullptr;
    /// Last seen modification time of each metadata file, by path; and which
    /// kind of change a difference in it means.
    QHash<QString, qint64> stamps_;
    QHash<QString, int> stampKinds_;

    int inotify_ = -1;
    QSocketNotifier* notifier_ = nullptr;
    QHash<int, QString> watches_; ///< inotify watch descriptor → directory
    bool warnedLimit_ = false;

    QTimer* debounce_ = nullptr;
    int pending_ = 0;
};

} // namespace gity::session
