// Paths in the working copy that Gity is about to write or delete itself,
// rather than through git.
//
// git refuses to write into its own directory or through a symbolic link
// that leads out of the working copy; a file Gity writes directly has to be
// held to the same rule, or a path read from a repository becomes a way to
// write anywhere the user can.
#pragma once

#include <QString>

namespace gity::session {

/// True when `path`, relative to `workdir`, names something that may be
/// deleted there: no absolute path, no ".." or ".git" component, and every
/// directory of it that exists already inside the working copy once links
/// are resolved. The path itself may be a link: deleting one removes the
/// link, not what it points at.
[[nodiscard]] bool deletableInWorkdir(const QString& workdir, const QString& path);

/// As deletableInWorkdir, and the path is not itself a link, which writing
/// would follow or replace.
[[nodiscard]] bool writableInWorkdir(const QString& workdir, const QString& path);

} // namespace gity::session
