#pragma once
#include <cstdint>
#include <cstddef>
#include <istream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blur::assets {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t max_stored = 16 * 1024 * 1024;
constexpr std::size_t max_decoded = 32 * 1024 * 1024;
std::uint32_t path_crc(std::string_view name);
std::uint32_t content_checksum(std::span<const std::uint8_t> data);
Bytes decode_payload(std::uint8_t selector, std::span<const std::uint8_t> stored,
                     std::uint32_t logical_size, std::uint32_t checksum);
struct Entry {
    std::string name;
    std::uint32_t name_crc{}, logical_size{}, checksum{}, stored_size{};
    std::uint64_t sector{};
};
struct Archive {
    std::uint64_t file_size{}, metadata_end{};
    std::uint8_t selector{};
    std::vector<Entry> entries;
};
Archive read_index(std::istream& input);
Bytes read_resource(std::istream& input, const Archive& archive, std::size_t index);
struct Atlas {
    std::uint32_t volume_width{}, volume_height{}, volume_depth{}, width{}, height{};
    Bytes rgba;
};
// New implementation of the observed legacy DDS RGB32 volume profile.
Atlas dds_volume_atlas(std::span<const std::uint8_t> dds);
} // namespace blur::assets
