// A cursor over a JSON document, for the few small responses core reads.
//
// Deliberately small: no tree is built, and each caller descends into the
// shape it expects and refuses anything else rather than guessing. It knows
// enough of the grammar to keep a key that appears inside a string, or inside
// a nested object, from being mistaken for one of the caller's — which is
// what scanning the text for "key": gets wrong.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace gity::git {

class JsonScanner {
public:
    explicit JsonScanner(std::string_view text) : text_(text) {}

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

    /// Steps over a `null`, if that is what comes next.
    bool takeNull() {
        skipSpace();
        if (text_.substr(pos_, 4) == "null") {
            pos_ += 4;
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
            case 'n':
                out->push_back('\n');
                break;
            case 't':
                out->push_back('\t');
                break;
            case 'r':
                out->push_back('\r');
                break;
            case 'b':
                out->push_back('\b');
                break;
            case 'f':
                out->push_back('\f');
                break;
            case '"':
                out->push_back('"');
                break;
            case '\\':
                out->push_back('\\');
                break;
            case '/':
                out->push_back('/');
                break;
            case 'u': {
                std::uint32_t code = 0;
                if (!hex4At(pos_, &code)) {
                    return false;
                }
                pos_ += 4;
                // A character outside the BMP comes as a surrogate pair.
                // Written as two halves it would be bytes no UTF-8 reader
                // accepts; a half without the other is the replacement
                // character rather than silently dropped.
                std::uint32_t low = 0;
                if (code >= 0xD800 && code <= 0xDBFF && text_.substr(pos_, 2) == "\\u" &&
                    hex4At(pos_ + 2, &low) && low >= 0xDC00 && low <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10U) + (low - 0xDC00);
                    pos_ += 6;
                } else if (code >= 0xD800 && code <= 0xDFFF) {
                    code = 0xFFFD;
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

    /// A string, or a bare literal — number, true or false — as written.
    /// `null` is no value and reads as empty. False for an object or array,
    /// which is stepped over.
    bool readScalar(std::string* out) {
        skipSpace();
        out->clear();
        if (peek() == '"') {
            return readString(out);
        }
        if (peek() == '{' || peek() == '[') {
            static_cast<void>(skipValue());
            return false;
        }
        if (takeNull()) {
            return true;
        }
        while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' &&
               text_[pos_] != ']' && text_[pos_] != ' ' && text_[pos_] != '\t' &&
               text_[pos_] != '\n' && text_[pos_] != '\r') {
            out->push_back(text_[pos_++]);
        }
        return !out->empty();
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
                if (c == '{' || c == '[') {
                    ++depth;
                } else if (c == '}' || c == ']') {
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
    /// The four hex digits at `at`, without moving.
    [[nodiscard]] bool hex4At(std::size_t at, std::uint32_t* code) const {
        if (at + 4 > text_.size()) {
            return false;
        }
        *code = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const char digit = text_[at + i];
            *code <<= 4U;
            if (digit >= '0' && digit <= '9') {
                *code |= static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                *code |= static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                *code |= static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    static void appendUtf8(std::string* out, std::uint32_t code) {
        if (code < 0x80) {
            out->push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out->push_back(static_cast<char>(0xC0U | (code >> 6U)));
            out->push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else if (code < 0x10000) {
            out->push_back(static_cast<char>(0xE0U | (code >> 12U)));
            out->push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else {
            out->push_back(static_cast<char>(0xF0U | (code >> 18U)));
            out->push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace gity::git
