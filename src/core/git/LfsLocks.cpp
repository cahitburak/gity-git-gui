#include "LfsLocks.h"

#include "JsonScanner.h"

#include <algorithm>

namespace gity::git {
namespace {

// The response is an array of flat objects with one nested object; the
// scanner understands exactly that shape and refuses anything else rather
// than guessing.
using Scanner = JsonScanner;

/// A string field that may also be null. Servers leave out what they do not
/// know as null — a lock taken before owners were recorded — and that is one
/// field missing, not a malformed list.
bool readStringOrNull(Scanner& scanner, std::string* out) {
    if (scanner.takeNull()) {
        out->clear();
        return true;
    }
    return scanner.readString(out);
}

/// The owner object, of which only the name is wanted. Null and `{}` are an
/// owner nobody named.
bool readOwner(Scanner& scanner, std::string* name) {
    name->clear(); // a later "owner" replaces an earlier one, null included
    if (scanner.takeNull()) {
        return true;
    }
    if (!scanner.take('{')) {
        return false;
    }
    if (scanner.take('}')) {
        return true;
    }
    while (true) {
        std::string key;
        if (!scanner.readString(&key)) {
            return false;
        }
        if (!scanner.take(':')) {
            return false;
        }
        if (key == "name") {
            if (!readStringOrNull(scanner, name)) {
                return false;
            }
        } else if (!scanner.skipValue()) {
            return false;
        }
        if (scanner.take(',')) {
            continue;
        }
        break;
    }
    return scanner.take('}');
}

/// One `{ ... }` element. The owner's name lives in a nested object and is
/// read by descending into it, never by scanning for "name" across the whole
/// element — that is how a name gets attached to the wrong lock.
bool readLock(Scanner& scanner, LfsLock* out) {
    if (!scanner.take('{')) {
        return false;
    }
    if (scanner.take('}')) {
        return true; // empty object; nothing to fill in
    }

    while (true) {
        std::string key;
        if (!scanner.readString(&key)) {
            return false;
        }
        if (!scanner.take(':')) {
            return false;
        }

        if (key == "id") {
            if (!scanner.readString(&out->id)) {
                return false;
            }
        } else if (key == "path") {
            if (!scanner.readString(&out->path)) {
                return false;
            }
        } else if (key == "locked_at") {
            if (!readStringOrNull(scanner, &out->lockedAt)) {
                return false;
            }
        } else if (key == "owner") {
            if (!readOwner(scanner, &out->owner)) {
                return false;
            }
        } else if (!scanner.skipValue()) {
            return false;
        }

        if (scanner.take(',')) {
            continue;
        }
        break;
    }
    return scanner.take('}');
}

} // namespace

std::vector<LfsLock> parseLfsLocks(std::string_view json) {
    std::vector<LfsLock> locks;
    Scanner scanner(json);
    if (!scanner.take('[')) {
        return locks;
    }
    if (scanner.take(']')) {
        return locks;
    }

    while (true) {
        LfsLock lock;
        if (!readLock(scanner, &lock)) {
            // A malformed element means the rest cannot be trusted either. An
            // advisory list is better empty than half wrong.
            return {};
        }
        if (lock.valid()) {
            locks.push_back(std::move(lock));
        }
        if (scanner.take(',')) {
            continue;
        }
        break;
    }
    if (!scanner.take(']')) {
        return {};
    }
    return locks;
}

std::optional<LfsLock> lockFor(const std::vector<LfsLock>& locks, std::string_view path) {
    const auto found = std::find_if(locks.begin(), locks.end(),
                                    [&](const LfsLock& lock) { return lock.path == path; });
    if (found == locks.end()) {
        return std::nullopt;
    }
    return *found;
}

} // namespace gity::git
