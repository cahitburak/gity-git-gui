#include "SwitchContext.h"

#include <QStringList>

namespace gity::session {

SwitchContext switchContext(const gity::git::RefSet* refs,
                            const gity::git::WorkingCopyStatus* status, const QString& ref) {
    SwitchContext context;
    QStringList submodulePaths;
    QString headOid;
    QString refOid;
    const auto hex = [](const git_oid& oid) {
        char buffer[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr(buffer, sizeof(buffer), &oid);
        return QString::fromLatin1(buffer);
    };
    if (refs != nullptr) {
        for (const auto& branch : refs->localBranches) {
            if (branch.isHead) {
                headOid = hex(branch.target);
            }
            if (QString::fromStdString(branch.name) == ref) {
                context.kind = RefKind::Local;
                refOid = hex(branch.target);
            }
        }
        for (const auto& branch : refs->remoteBranches) {
            if (QString::fromStdString(branch.name) == ref) {
                context.kind = RefKind::Remote;
                refOid = hex(branch.target);
            }
        }
        for (const auto& submodule : refs->submodules) {
            if (submodule.initialised) {
                submodulePaths << QString::fromStdString(submodule.path);
            }
        }
    }
    context.hasSubmodules = !submodulePaths.isEmpty();
    context.sameCommit = ref.isEmpty() || (!headOid.isEmpty() && refOid == headOid);
    if (status != nullptr) {
        for (const auto& entry : status->entries) {
            if (submodulePaths.contains(QString::fromStdString(entry.path))) {
                ++context.submoduleChanges;
            }
        }
    }
    return context;
}

} // namespace gity::session
