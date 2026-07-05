#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace NextAI
{
    // Resolve a user-supplied relative path against repoRoot.
    // Returns the absolute path string only if it stays inside the repo root
    // (no .. escape, no absolute input, no symlink escape after canonicalization).
    // The returned string uses the host OS path separators.
    std::optional<std::string> SafeRepoPath(const std::string& repoRoot, const std::string& rel);

    // Heuristic: treat content as binary if the first 4 KiB contains a NUL byte.
    bool IsProbablyBinary(const std::string& data);

    // Source-like file extension (C/C++ family).
    bool IsSourceLikePath(std::string_view path);

    // Convert any backslashes to forward slashes for display.
    std::string ToSlash(std::string path);
}
