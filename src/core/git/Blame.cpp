#include "Blame.h"

#include <charconv>
#include <unordered_map>
#include <utility>

namespace gity::git {
namespace {

bool isHex(std::string_view text) {
    return text.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

std::uint32_t number(std::string_view text) {
    std::uint32_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

/// The next field of `line`, separated by one space, and the rest after it.
std::string_view field(std::string_view& line) {
    const auto space = line.find(' ');
    const std::string_view word = line.substr(0, space);
    line = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
    return word;
}

} // namespace

Blame parseBlamePorcelain(std::string_view output) {
    Blame blame;
    std::unordered_map<std::string, std::uint32_t> known;
    std::uint32_t current = 0;
    std::uint32_t originalLine = 0;
    bool inEntry = false;

    while (!output.empty()) {
        const auto end = output.find('\n');
        std::string_view line = output.substr(0, end);
        output = end == std::string_view::npos ? std::string_view() : output.substr(end + 1);

        if (!inEntry) {
            // "<id> <original line> <final line> [<lines in group>]"
            std::string_view rest = line;
            const std::string_view id = field(rest);
            if (id.size() < 40 || !isHex(id)) {
                break;
            }
            originalLine = number(field(rest));
            const auto [it, added] = known.try_emplace(
                std::string(id), static_cast<std::uint32_t>(blame.commits.size()));
            if (added) {
                BlameCommit commit;
                commit.id = id;
                blame.commits.push_back(std::move(commit));
            }
            current = it->second;
            inEntry = true;
            continue;
        }
        if (!line.empty() && line.front() == '\t') {
            blame.lines.push_back(BlameLine{.commit = current,
                                            .originalLine = originalLine,
                                            .text = std::string(line.substr(1))});
            inEntry = false;
            continue;
        }
        // A header about the commit; only the first entry for each has them.
        BlameCommit& commit = blame.commits[current];
        std::string_view rest = line;
        const std::string_view key = field(rest);
        if (key == "author") {
            commit.author = rest;
        } else if (key == "author-time") {
            std::int64_t when = 0;
            std::from_chars(rest.data(), rest.data() + rest.size(), when);
            commit.when = when;
        } else if (key == "summary") {
            commit.summary = rest;
        } else if (key == "filename") {
            commit.path = rest;
        } else if (key == "previous") {
            commit.previousId = field(rest);
            commit.previousPath = rest;
        }
    }
    return blame;
}

} // namespace gity::git
