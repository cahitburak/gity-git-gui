// The instruction list `git rebase -i` would have opened an editor to collect.
//
// A GUI supplies it directly instead, which removes the editor but not the
// two ways this goes wrong:
//
//   * **Order.** git's todo file runs oldest first. Every history view,
//     including this one, shows newest first. Handing git the list in display
//     order rebases the commits backwards, and the result is a plausible-
//     looking history that is wrong.
//   * **Validity.** A list starting with squash or fixup has nothing to squash
//     into; git aborts after the rebase has already begun, leaving the
//     repository mid-operation for a mistake that was visible before it
//     started.
//
// Both are checked here, where they can be tested, rather than discovered by
// running git.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

enum class RebaseAction {
    Pick,   ///< Keep the commit as it is.
    /// Keep the changes, replace the message. git's own `reword` opens an
    /// editor Gity cannot show, so this is written as a pick followed by an
    /// amend of the message from a file (see serializeTodo).
    Reword,
    Edit,   ///< Apply it, then stop so the working copy can be amended.
    Squash, ///< Fold into the previous commit, combining the messages.
    Fixup,  ///< Fold into the previous commit, discarding this message.
    Drop,   ///< Leave it out entirely.
};

struct RebaseStep {
    RebaseAction action = RebaseAction::Pick;
    std::string oid;
    std::string subject; ///< For the comment git writes after the id.
    /// Reword only: the new message, as typed.
    std::string message;
    /// Reword only: a file holding the new message. It has to outlive the
    /// command that starts the rebase — a stop before this step (a conflict,
    /// an edit) resumes later, and the step reads the file then.
    std::string messageFile;
};

/// git's keyword for the action, as it appears in the todo file.
[[nodiscard]] std::string_view actionKeyword(RebaseAction action);

/// The todo file's contents. `steps` is given **newest first**, as the history
/// view holds it, and is reversed here — that inversion is the whole point of
/// routing this through one function.
[[nodiscard]] std::string serializeTodo(const std::vector<RebaseStep>& steps);

enum class TodoProblem {
    None,
    /// No steps at all.
    Empty,
    /// Every commit dropped. git accepts this, but it means "delete all of
    /// this work", which is worth asking about rather than performing.
    EverythingDropped,
    /// The oldest kept commit folds into a previous one that is not in the
    /// list. git aborts partway for this.
    LeadingFold,
};

[[nodiscard]] TodoProblem validateTodo(const std::vector<RebaseStep>& steps);

} // namespace gity::git
