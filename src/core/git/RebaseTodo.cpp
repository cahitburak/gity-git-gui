#include "RebaseTodo.h"

#include <algorithm>

namespace gity::git {

std::string_view actionKeyword(RebaseAction action) {
    switch (action) {
    case RebaseAction::Pick:
    case RebaseAction::Reword:
        return "pick";
    case RebaseAction::Edit:
        return "edit";
    case RebaseAction::Squash:
        return "squash";
    case RebaseAction::Fixup:
        return "fixup";
    case RebaseAction::Drop:
        return "drop";
    }
    return "pick";
}

std::string serializeTodo(const std::vector<RebaseStep>& steps) {
    std::string todo;
    // Reversed: the caller holds newest first because that is how history is
    // read, and git applies the file top to bottom starting from the oldest.
    for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
        todo += actionKeyword(step->action);
        todo += ' ';
        todo += step->oid;
        if (!step->subject.empty()) {
            // git writes the subject after the id and ignores it; keeping it
            // makes the file readable if anyone ever looks at it mid-rebase.
            todo += ' ';
            todo += step->subject;
        }
        todo += '\n';
        if (step->action == RebaseAction::Reword && !step->messageFile.empty()) {
            // Amends the message only (--only with no paths commits nothing
            // from the index), keeps the author, allows an empty commit to
            // stay empty. The path is single-quoted for the shell git runs
            // exec lines with.
            std::string quoted = "'";
            for (const char c : step->messageFile) {
                quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
            }
            quoted += "'";
            todo += "exec git commit --amend --only --allow-empty --cleanup=strip --file=" + quoted +
                    "\n";
        }
    }
    return todo;
}

TodoProblem validateTodo(const std::vector<RebaseStep>& steps) {
    if (steps.empty()) {
        return TodoProblem::Empty;
    }

    const bool anyKept = std::any_of(steps.begin(), steps.end(), [](const RebaseStep& step) {
        return step.action != RebaseAction::Drop;
    });
    if (!anyKept) {
        return TodoProblem::EverythingDropped;
    }

    // The first line git will execute is the *oldest* non-dropped step. If it
    // folds, there is nothing before it in this rebase to fold into.
    for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
        if (step->action == RebaseAction::Drop) {
            continue;
        }
        if (step->action == RebaseAction::Squash || step->action == RebaseAction::Fixup) {
            return TodoProblem::LeadingFold;
        }
        break;
    }
    return TodoProblem::None;
}

} // namespace gity::git
