#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace blur::render::guest {
// New diagnostic policy, not recovered guest behavior. The saved menu frame
// has two 1280x720 copies with source pitch 320. Test that interpretation only
// when the independent destination, scissor and vertex rectangle agree.
inline std::optional<std::uint32_t> fullscreen_resolve_pitch_trial(
    std::span<const std::uint32_t> words, std::span<const float> vertices) {
    if (words.size() <= 0x231b || vertices.size() != 6) return {};
    const auto surface = words[0x2000];
    const auto control = words[0x2318];
    if ((surface & 0x3fff) != 320 || ((surface >> 16) & 3) != 0 ||
        (control != 4 && control != 0x100000) ||
        words[0x231a] != 0x02d00500 || (words[0x231b] & 8) ||
        words[0x2080] != 0 || words[0x2081] != 0 ||
        words[0x2082] != 0x02d00500) return {};
    for (const float value : vertices) if (!std::isfinite(value)) return {};
    const auto min_x = std::min({vertices[0], vertices[2], vertices[4]});
    const auto max_x = std::max({vertices[0], vertices[2], vertices[4]});
    const auto min_y = std::min({vertices[1], vertices[3], vertices[5]});
    const auto max_y = std::max({vertices[1], vertices[3], vertices[5]});
    if (min_x != -0.5f || max_x != 1279.5f ||
        min_y != -0.5f || max_y != 719.5f) return {};
    return (surface & ~std::uint32_t(0x3fff)) | 1280;
}
}  // namespace blur::render::guest
