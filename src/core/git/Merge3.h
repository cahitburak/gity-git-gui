// A conflicted file split into the regions a conflict editor works with.
//
// git leaves a conflicted file as three versions in the index — the common
// ancestor (base), ours and theirs — and a working file with markers. The
// editor works from the three versions rather than the markers: a file the
// user already half-edited, or one written with diff3 markers, is read the
// same way, and the Ours and Theirs columns show those files whole.
//
// The split is the classic three-way merge: each side is diffed against the
// base, and the base is walked from one change to the next. Lines no side
// changed are Same; a change on one side only is taken from that side; the
// same change on both is taken once; anything else is a Conflict. Changes
// that touch or abut are one region, as git counts them.
//
// A resolution picks, for each region, what goes into the result. Regions
// that are not conflicts start resolved to their changed side, as git merges
// them; conflicts start unresolved.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gity::git {

/// Lines with their endings, so that what is written back has the file's own
/// CR LF or LF, and a last line without one stays without one.
using Lines = std::vector<std::string>;

[[nodiscard]] Lines splitLines(std::string_view text);
[[nodiscard]] std::string joinLines(const Lines& lines);

struct MergeRegion {
    enum class Kind {
        Same,     ///< No side changed it.
        Ours,     ///< Only ours changed it.
        Theirs,   ///< Only theirs changed it.
        Both,     ///< Both made the same change.
        Conflict, ///< Both changed it, differently.
    };

    Kind kind = Kind::Same;
    Lines base;
    Lines ours;
    Lines theirs;
};

struct Merge3 {
    std::vector<MergeRegion> regions;

    [[nodiscard]] std::size_t conflictCount() const noexcept;
};

/// Splits three versions of a text file into regions. Every line of each
/// version is in exactly one region, in order.
[[nodiscard]] Merge3 mergeLines(std::string_view base, std::string_view ours,
                                std::string_view theirs);

/// What one region contributes to the result.
enum class Pick {
    Unresolved, ///< A conflict nothing has been chosen for yet.
    Ours,
    Theirs,
    OursThenTheirs, ///< Both sides, ours first.
    TheirsThenOurs,
    Base,   ///< Neither side's change: the ancestor's lines.
    Edited, ///< Text the user wrote.
};

class MergeResolution {
public:
    MergeResolution() = default;
    explicit MergeResolution(Merge3 merge);

    [[nodiscard]] const Merge3& merge() const noexcept { return merge_; }

    [[nodiscard]] std::size_t regionCount() const noexcept { return merge_.regions.size(); }

    [[nodiscard]] const MergeRegion& region(std::size_t i) const { return merge_.regions[i]; }

    [[nodiscard]] Pick pick(std::size_t i) const { return picks_[i]; }

    void setPick(std::size_t i, Pick pick);
    /// Replaces the region's result with `text` and marks it Edited.
    void setEdited(std::size_t i, std::string_view text);
    /// Every conflict to the same side; regions that are not conflicts keep
    /// their pick.
    void pickAllConflicts(Pick pick);
    /// Back to how it started: every conflict unresolved, nothing edited.
    void reset();

    /// The lines region `i` puts in the result; none while unresolved.
    [[nodiscard]] Lines result(std::size_t i) const;
    [[nodiscard]] std::size_t unresolvedCount() const noexcept;
    /// The region of the first unresolved conflict after `from`, wrapping
    /// round; nullopt when none is left.
    [[nodiscard]] std::optional<std::size_t> nextUnresolved(std::size_t from) const;

    /// The whole result. A conflict still unresolved is written as git writes
    /// one, between markers labelled `oursLabel` and `theirsLabel`, so a file
    /// saved part way through can be finished in any editor.
    [[nodiscard]] std::string text(std::string_view oursLabel = "ours",
                                   std::string_view theirsLabel = "theirs") const;

private:
    Merge3 merge_;
    std::vector<Pick> picks_;
    std::vector<Lines> edited_;
    std::string eol_ = "\n";
};

} // namespace gity::git
