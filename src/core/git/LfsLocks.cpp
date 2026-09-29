#include "LfsLocks.h"

#include <algorithm>
#include <cstdint>

namespace gity::git {
namespace {

/// A cursor over the response. Deliberately small: it understands exactly the
/// shape `git lfs locks --json` produces — an array of flat objects with one
/// nested object — and refuses anything else rather than guessing.
class Scanner {
public:
    explicit Scanner(std::string_view text) : text_(text) {}

    void skipSpace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    [[nodiscard]] bool eof() const { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    bool take(char c) {
        skipSpace();
        if (peek() == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    /// A JSON string, escapes resolved. A path contains spaces and, on
    /// occasion, characters that arrive escaped; taking the text between the
    /// first two quotes would truncate at the first escaped one.
    bool readString(std::string* out) {
        skipSpace();
        if (peek() != '"') {
            return false;
        }
        ++pos_;
        out->clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) {
                return false;
            }
            const char escape = text_[pos_++];
            switch (escape) {
            case 'n': out->push_back('\n'); break;
            case 't': out->push_back('\t'); break;
            case 'r': out->push_back('\r'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case '"': out->push_back('"'); break;
            case '\\': out->push_back('\\'); break;
            case '/': out->push_back('/'); break;
            case 'u': {
                // Only the BMP escapes git-lfs would emit. Anything else is
                // passed through as the replacement character rather than
                // silently dropping bytes from a path.
                if (pos_ + 4 > text_.size()) {
                    return false;
                }
                std::uint32_t code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char digit = text_[pos_++];
                    code <<= 4U;
                    if (digit >= '0' && digit <= '9') {
                        code |= static_cast<std::uint32_t>(digit - '0');
                    } else if (digit >= 'a' && digit <= 'f') {
                        code |= static_cast<std::uint32_t>(digit - 'a' + 10);
                    } else if (digit >= 'A' && digit <= 'F') {
                        code |= static_cast<std::uint32_t>(digit - 'A' + 10);
                    } else {
                        return false;
                    }
                }
                appendUtf8(out, code);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    /// Steps over any value without interpreting it, so an unfamiliar field
    /// does not derail the element it sits in.
    bool skipValue() {
        skipSpace();
        if (peek() == '"') {
            std::string discard;
            return readString(&discard);
        }
        if (peek() == '{' || peek() == '[') {
            const char open = peek();
            const char close = open == '{' ? '}' : ']';
            int depth = 0;
            while (pos_ < text_.size()) {
                const char c = text_[pos_];
                if (c == '"') {
                    std::string discard;
                    if (!readString(&discard)) {
                        return false;
                    }
                    continue;
                }
                ++pos_;
                if (c == open) {
                    ++depth;
                } else if (c == close) {
                    if (--depth == 0) {
                        return true;
                    }
                }
            }
            return false;
        }
        // A bare literal: number, true, false, null.
        while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' &&
               text_[pos_] != ']') {
            ++pos_;
        }
        return true;
    }

private:
    static void appendUtf8(std::string* out, std::uint32_t code) {
        if (code < 0x80) {
            out->push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out->push_back(static_cast<char>(0xC0U | (code >> 6U)));
            out->push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else {
            out->push_back(static_cast<char>(0xE0U | (code >> 12U)));
            out->push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

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
            if (!scanner.readString(&out->lockedAt)) {
                return false;
            }
        } else if (key == "owner") {
            if (!scanner.take('{')) {
                return false;
            }
            while (true) {
                std::string ownerKey;
                if (!scanner.readString(&ownerKey)) {
                    return false;
                }
                if (!scanner.take(':')) {
                    return false;
                }
                if (ownerKey == "name") {
                    if (!scanner.readString(&out->owner)) {
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
            if (!scanner.take('}')) {
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
