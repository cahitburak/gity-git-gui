// A temporary directory of the test's own, for the core tests, which build
// without Qt and so without QTemporaryDir.
#pragma once

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gity::test {

/// Creates a new, empty directory under the system's temporary folder and
/// returns its path: `gity-<kind>-<random>`.
///
/// ctest runs every test in a process of its own, so a name made from the
/// test's seed or a counter was the same in all of them, and tests run in
/// parallel removed each other's repositories. Creating the directory is
/// what claims the name: one that already exists is passed over.
inline std::filesystem::path freshTempDir(std::string_view kind) {
    namespace fs = std::filesystem;
    static std::mt19937_64 random{std::random_device{}()};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const fs::path path = fs::temp_directory_path() /
                              ("gity-" + std::string(kind) + "-" + std::to_string(random()));
        if (fs::create_directory(path)) {
            return path;
        }
    }
    throw std::runtime_error("could not create a temporary directory");
}

} // namespace gity::test
