#include "WorkdirPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace gity::session {

bool deletableInWorkdir(const QString& workdir, const QString& path) {
    if (path.isEmpty() || QDir::isAbsolutePath(path)) {
        return false;
    }
    // By component, before anything touches the disk. ".git" in any case:
    // on a case-insensitive file system ".GIT/hooks" is the same directory.
    const QStringList parts = QDir::fromNativeSeparators(path).split(QChar('/'));
    for (const QString& part : parts) {
        if (part == QStringLiteral("..") ||
            part.compare(QStringLiteral(".git"), Qt::CaseInsensitive) == 0) {
            return false;
        }
    }

    const QString root = QDir(workdir).canonicalPath();
    if (root.isEmpty()) {
        return false;
    }
    const QString full = QDir::cleanPath(QDir(root).filePath(path));
    if (!full.startsWith(root + QChar('/'))) {
        return false;
    }
    // The nearest directory that exists, with every link in it resolved,
    // must still be in the working copy: "docs/x" where docs is a link to
    // somewhere else is a path out of it.
    QString directory = QFileInfo(full).absolutePath();
    while (!QFileInfo::exists(directory) && directory.startsWith(root + QChar('/'))) {
        directory = QFileInfo(directory).absolutePath();
    }
    const QString resolved = QFileInfo(directory).canonicalFilePath();
    return resolved == root || resolved.startsWith(root + QChar('/'));
}

bool writableInWorkdir(const QString& workdir, const QString& path) {
    // The file itself a link: writing would replace or follow it, and either
    // way the result is not the file the path names.
    return deletableInWorkdir(workdir, path) &&
           !QFileInfo(QDir(workdir).filePath(path)).isSymLink();
}

} // namespace gity::session
