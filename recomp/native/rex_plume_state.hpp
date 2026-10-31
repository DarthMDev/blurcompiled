#pragma once
#include "render/guest_frontend.hpp"

namespace blur::render::guest {
inline constexpr std::size_t rex_register_word_count=0x5003;
// New native frontend mapping, compiled against the hash-pinned SDK headers.
// Supply current RegisterFile::values in native word order, not XEX bytes.
// The caller must separately resolve attachments, primitive/index processing,
// sampler/view settings, vertex layout and zero-initialization provenance.
// These resource-dependent fields remain unknown unless the caller supplies
// them; a matching register tuple alone never establishes draw eligibility.
UiState map_rex_ui_register_state(std::span<const std::uint32_t> register_words,
                                  UiState resolved_resources);
}
