#include "WorkdirPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>

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
    // somewhere else is a path out of it. A link counts as existing even
    // when what it points at does not, so a dangling one is not stepped over
    // — it resolves to nothing, and is refused.
    const auto present = [](const QString& at) {
        const QFileInfo info(at);
        return info.exists() || info.isSymLink();
    };
    QString directory = QFileInfo(full).absolutePath();
    while (!present(directory) && directory.startsWith(root + QChar('/'))) {
        directory = QFileInfo(directory).absolutePath();
    }
    const QString resolved = QFileInfo(directory).canonicalFilePath();
    if (resolved.isEmpty() || (resolved != root && !resolved.startsWith(root + QChar('/')))) {
        return false;
    }
    // Inside the working copy, but perhaps through a link into git's own
    // directory: "hooks/x" where hooks leads to .git/hooks.
    const QStringList inside = QDir(root).relativeFilePath(resolved).split(QChar('/'));
    return std::none_of(inside.begin(), inside.end(), [](const QString& part) {
        return part.compare(QStringLiteral(".git"), Qt::CaseInsensitive) == 0;
    });
}

bool writableInWorkdir(const QString& workdir, const QString& path) {
    // The file itself a link: writing would replace or follow it, and either
    // way the result is not the file the path names.
    return deletableInWorkdir(workdir, path) &&
           !QFileInfo(QDir(workdir).filePath(path)).isSymLink();
}

} // namespace gity::session
