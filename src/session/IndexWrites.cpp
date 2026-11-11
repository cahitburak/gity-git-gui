#include "IndexWrites.h"

namespace gity::session::index {
namespace {

/// Long enough for a clean filter working through a large binary — LFS hashes
/// and copies every staged file — and short enough that a hung filter is
/// reported rather than waited on forever.
constexpr int kTimeoutMs = 600000;

GitResult runOnPaths(const QString& workdir, QStringList args, const QStringList& paths) {
    if (paths.isEmpty()) {
        GitResult nothing;
        nothing.started = true;
        nothing.exitCode = 0;
        return nothing;
    }
    // A path, never a pattern: as a glob `[id].tsx` also names `d.tsx`.
    args.prepend(QStringLiteral("--literal-pathspecs"));
    args << QStringLiteral("--");
    args += paths;
    return GitProcess::run(workdir, args, kTimeoutMs);
}

} // namespace

GitResult stagePaths(const QString& workdir, const QStringList& paths) {
    // -A so a deleted path stages its deletion rather than failing on a
    // file that is not there.
    return runOnPaths(workdir, {QStringLiteral("add"), QStringLiteral("-A")}, paths);
}

GitResult unstagePaths(const QString& workdir, const QStringList& paths) {
    // `reset` rather than `restore --staged`: it also works before the first
    // commit, where there is no HEAD to restore from.
    return runOnPaths(workdir, {QStringLiteral("reset"), QStringLiteral("-q")}, paths);
}

GitResult applyToIndex(const QString& workdir, const QByteArray& patch) {
    return GitProcess::runWithInput(workdir,
                                    {QStringLiteral("apply"), QStringLiteral("--cached"),
                                     QStringLiteral("--whitespace=nowarn"), QStringLiteral("-")},
                                    patch, kTimeoutMs);
}

GitResult applyToWorktree(const QString& workdir, const QByteArray& patch) {
    return GitProcess::runWithInput(
        workdir,
        {QStringLiteral("apply"), QStringLiteral("--whitespace=nowarn"), QStringLiteral("-")},
        patch, kTimeoutMs);
}

} // namespace gity::session::index
