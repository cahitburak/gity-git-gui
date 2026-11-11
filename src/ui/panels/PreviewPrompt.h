// What a pull, merge, rebase or push will do, put to the user before it
// runs: the wording for each kind of preview, and the choices it allows.
// PreviewDialog lays it out; the window acts on the answer.
#pragma once

#include "session/SessionTypes.h"

#include <QString>

class QWidget;

namespace gity::ui {

enum class PreviewChoice {
    Cancel = 0,
    /// What was previewed.
    Go,
    /// A pull that cannot fast-forward, merged or rebased instead.
    MergeInstead,
    RebaseInstead,
    /// A push over commits the remote has, leased — confirmed twice for a
    /// branch other people build on.
    Force,
};

/// "Pull", "Merge", "Rebase" or "Push".
[[nodiscard]] QString previewVerb(gity::session::Preview::Kind kind);

/// Shows `preview` and asks; a preview that could not be made is explained
/// and answers Cancel.
[[nodiscard]] PreviewChoice choosePreviewAction(const gity::session::Preview& preview,
                                                QWidget* parent);

} // namespace gity::ui
