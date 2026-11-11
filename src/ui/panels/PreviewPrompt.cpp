#include "PreviewPrompt.h"

#include "Confirm.h"
#include "PreviewDialog.h"

#include <QMessageBox>

namespace gity::ui {
namespace {

using Kind = gity::session::Preview::Kind;

int toId(PreviewChoice choice) {
    return static_cast<int>(choice);
}

/// Lays out what `p` will do, and the choices it allows.
void describe(PreviewDialog& dialog, const gity::session::Preview& p) {
    // Shared: the conflict prediction and what would stop git outright.
    const auto addPrediction = [&dialog, &p] {
        if (p.predicted) {
            if (p.conflicts.isEmpty()) {
                dialog.addText(PreviewDialog::tr("No conflicts expected."), QStringLiteral("note"));
            } else {
                dialog.addList(PreviewDialog::tr("%n file(s) would conflict:", nullptr,
                                                 static_cast<int>(p.conflicts.size())),
                               p.conflicts, -1, QStringLiteral("warn"));
            }
        }
        if (!p.predictionNote.isEmpty()) {
            dialog.addText(p.predictionNote, QStringLiteral("note"));
        }
        if (!p.blockedByLocal.isEmpty()) {
            dialog.addList(
                p.kind == Kind::Rebase
                    ? PreviewDialog::tr(
                          "A rebase needs a clean working copy; commit or stash these first:")
                    : PreviewDialog::tr("Uncommitted changes git would refuse to overwrite:"),
                p.blockedByLocal, -1, QStringLiteral("warn"));
        }
    };

    switch (p.kind) {
    case Kind::Pull:
        if (p.upToDate) {
            dialog.setHeadline(
                PreviewDialog::tr("%1 is up to date with %2.").arg(p.branch, p.target));
            if (p.outgoing > 0) {
                dialog.addText(PreviewDialog::tr("You have %n commit(s) not pushed yet.", nullptr,
                                                 p.outgoing));
            }
            break;
        }
        if (p.fastForward) {
            dialog.setHeadline(
                PreviewDialog::tr("Pull brings in %n commit(s) from %1.", nullptr, p.incoming)
                    .arg(p.target));
            dialog.addText(
                PreviewDialog::tr("A fast-forward: %1 simply moves ahead; no merge commit.")
                    .arg(p.branch),
                QStringLiteral("note"));
            dialog.addList(PreviewDialog::tr("Coming in:"), p.incomingCommits, p.incoming);
            addPrediction();
            dialog.setCommand(QStringLiteral("pull --ff-only"));
            dialog.addChoice(PreviewDialog::tr("Pull"), toId(PreviewChoice::Go),
                             QStringLiteral("primary"));
            break;
        }
        dialog.setHeadline(
            PreviewDialog::tr("%1 and %2 have both moved on.").arg(p.branch, p.target));
        dialog.addText(
            PreviewDialog::tr("%1 has %n new commit(s)", nullptr, p.incoming).arg(p.target) +
            PreviewDialog::tr(
                " and %1 has %n of its own. Pull only fast-forwards, so choose how to "
                "combine them.",
                nullptr, p.outgoing)
                .arg(p.branch));
        dialog.addList(PreviewDialog::tr("Coming in:"), p.incomingCommits, p.incoming);
        addPrediction();
        dialog.setCommand(QStringLiteral("merge %1  ·  rebase %1").arg(p.target));
        dialog.addChoice(PreviewDialog::tr("Rebase onto %1").arg(p.target),
                         toId(PreviewChoice::RebaseInstead));
        dialog.addChoice(PreviewDialog::tr("Merge %1").arg(p.target),
                         toId(PreviewChoice::MergeInstead), QStringLiteral("primary"));
        break;

    case Kind::Merge:
        if (p.upToDate) {
            dialog.setHeadline(
                PreviewDialog::tr("%1 already contains everything on %2.").arg(p.branch, p.target));
            break;
        }
        dialog.setHeadline(
            p.fastForward
                ? PreviewDialog::tr("Merging %1 moves %2 ahead by %n commit(s).", nullptr,
                                    p.incoming)
                      .arg(p.target, p.branch)
                : PreviewDialog::tr("Merge brings %n commit(s) from %1 into %2, as a merge commit.",
                                    nullptr, p.incoming)
                      .arg(p.target, p.branch));
        if (p.fastForward) {
            dialog.addText(PreviewDialog::tr("A fast-forward: no merge commit is made."),
                           QStringLiteral("note"));
        }
        dialog.addList(PreviewDialog::tr("Coming in:"), p.incomingCommits, p.incoming);
        addPrediction();
        dialog.setCommand(QStringLiteral("merge --no-edit %1").arg(p.target));
        dialog.addChoice(PreviewDialog::tr("Merge"), toId(PreviewChoice::Go),
                         QStringLiteral("primary"));
        break;

    case Kind::Rebase:
        if (p.upToDate) {
            dialog.setHeadline(
                PreviewDialog::tr("%1 is already on top of %2.").arg(p.branch, p.target));
            break;
        }
        dialog.setHeadline(
            p.outgoing == 0
                ? PreviewDialog::tr("%1 has no commits of its own, so it simply moves to %2.")
                      .arg(p.branch, p.target)
                : PreviewDialog::tr("Rebase replays %n commit(s) of %1 on top of %2.", nullptr,
                                    p.outgoing)
                      .arg(p.branch, p.target));
        if (p.outgoing > 0) {
            dialog.addText(
                PreviewDialog::tr("Each is replaced by a new commit. If any were pushed, everyone "
                                  "else keeps the originals and the histories diverge. Undo "
                                  "(Ctrl+Z) puts the branch back."),
                QStringLiteral("note"));
        }
        dialog.addList(PreviewDialog::tr("Replayed:"), p.outgoingCommits, p.outgoing);
        addPrediction();
        dialog.setCommand(QStringLiteral("rebase %1").arg(p.target));
        dialog.addChoice(PreviewDialog::tr("Rebase"), toId(PreviewChoice::Go),
                         QStringLiteral("primary"));
        break;

    case Kind::Push:
        if (p.newBranch) {
            dialog.setHeadline(
                PreviewDialog::tr("Publishes %1 to %2 as a new branch.").arg(p.branch, p.remote));
            dialog.addList(PreviewDialog::tr("Commits not on any remote yet:"), p.outgoingCommits,
                           p.outgoing);
            dialog.setCommand(QStringLiteral("push --set-upstream %1 %2").arg(p.remote, p.branch));
            dialog.addChoice(PreviewDialog::tr("Push"), toId(PreviewChoice::Go),
                             QStringLiteral("primary"));
            break;
        }
        if (p.upToDate) {
            dialog.setHeadline(
                PreviewDialog::tr("Nothing to push: %1 already has everything on %2.")
                    .arg(p.target, p.branch));
            break;
        }
        if (p.incoming == 0) {
            dialog.setHeadline(
                PreviewDialog::tr("Push sends %n commit(s) to %1.", nullptr, p.outgoing)
                    .arg(p.target));
            dialog.addList(PreviewDialog::tr("Going out:"), p.outgoingCommits, p.outgoing);
            dialog.setCommand(QStringLiteral("push %1 %2").arg(p.remote, p.branch));
            dialog.addChoice(PreviewDialog::tr("Push"), toId(PreviewChoice::Go),
                             QStringLiteral("primary"));
            break;
        }
        // The remote has work this branch lacks — the usual case after a
        // rebase. Pushing plainly is rejected; forcing removes that work.
        dialog.setHeadline(
            PreviewDialog::tr("%1 has %n commit(s) that %2 does not.", nullptr, p.incoming)
                .arg(p.target, p.branch));
        dialog.addText(
            PreviewDialog::tr("A normal push will be rejected. To keep that work, pull or rebase "
                              "first. A force push replaces %1 with %2 and removes those commits "
                              "from the remote — right after rebasing your own branch, and wrong "
                              "if someone else pushed them.")
                .arg(p.target, p.branch));
        dialog.addList(PreviewDialog::tr("Would be removed from %1:").arg(p.target),
                       p.incomingCommits, p.incoming, QStringLiteral("warn"));
        dialog.addList(PreviewDialog::tr("Going out:"), p.outgoingCommits, p.outgoing);
        dialog.addText(
            PreviewDialog::tr("As of the last fetch. The push is leased: if %1 has moved since, it "
                              "fails instead of overwriting.")
                .arg(p.target),
            QStringLiteral("note"));
        dialog.setCommand(QStringLiteral("push --force-with-lease=refs/heads/%1:%2 %3 %4:%1")
                              .arg(p.remoteBranch, p.leaseOid.left(8), p.remote, p.branch));
        dialog.addChoice(PreviewDialog::tr("Force Push"), toId(PreviewChoice::Force),
                         QStringLiteral("destructive"));
        break;
    }
}

} // namespace

QString previewVerb(gity::session::Preview::Kind kind) {
    return kind == Kind::Pull     ? PreviewDialog::tr("Pull")
           : kind == Kind::Merge  ? PreviewDialog::tr("Merge")
           : kind == Kind::Rebase ? PreviewDialog::tr("Rebase")
                                  : PreviewDialog::tr("Push");
}

PreviewChoice choosePreviewAction(const gity::session::Preview& p, QWidget* parent) {
    const QString verb = previewVerb(p.kind);
    if (!p.problem.isEmpty()) {
        QMessageBox::information(parent, verb, p.problem);
        return PreviewChoice::Cancel;
    }
    PreviewDialog dialog(verb, parent);
    describe(dialog, p);
    const int chosen = dialog.choose();
    if (chosen < toId(PreviewChoice::Go) || chosen > toId(PreviewChoice::Force)) {
        return PreviewChoice::Cancel;
    }
    const auto choice = static_cast<PreviewChoice>(chosen);
    // A shared branch is asked about twice: it is where a force push costs
    // other people their work.
    if (choice == PreviewChoice::Force && p.shared &&
        !confirmDestructive(
            parent, PreviewDialog::tr("Force push to %1?").arg(p.target),
            PreviewDialog::tr("%1 is a branch other people build on.").arg(p.remoteBranch),
            PreviewDialog::tr("Force pushing it removes %n commit(s) from the remote, and anyone "
                              "who has them will find their history no longer matches.",
                              nullptr, p.incoming),
            PreviewDialog::tr("Force Push"))) {
        return PreviewChoice::Cancel;
    }
    return choice;
}

} // namespace gity::ui
