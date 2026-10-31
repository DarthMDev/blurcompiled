#include "assets.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace blur::assets {
namespace {
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) {
    if (at > b.size() || b.size() - at < 4) fail("Truncated integer");
    return std::uint32_t(b[at]) | std::uint32_t(b[at+1]) << 8 |
           std::uint32_t(b[at+2]) << 16 | std::uint32_t(b[at+3]) << 24;
}
std::uint64_t u64(std::span<const std::uint8_t> b, std::size_t at) {
    return std::uint64_t(u32(b, at)) | std::uint64_t(u32(b, at+4)) << 32;
}
std::uint64_t stream_size(std::istream& input) {
    input.clear(); input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) fail("Input must be seekable");
    return static_cast<std::uint64_t>(size);
}
Bytes read(std::istream& input, std::uint64_t file_size, std::uint64_t offset, std::size_t size) {
    if (offset > file_size || size > file_size - offset ||
        offset > std::uint64_t(std::numeric_limits<std::streamoff>::max())) fail("File range out of bounds");
    Bytes data(size);
    input.clear(); input.seekg(static_cast<std::streamoff>(offset));
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (!input || input.gcount() != static_cast<std::streamsize>(size)) fail("Short file read");
    return data;
}
std::uint32_t crc(std::span<const std::uint8_t> data) {
    return static_cast<std::uint32_t>(crc32(0, data.data(), static_cast<uInt>(data.size()))) ^ 0xffffffffu;
}
Bytes inflate_exact(std::span<const std::uint8_t> stored, std::size_t expected, int bits) {
    Bytes result(expected + 1);
    z_stream stream{};
    if (inflateInit2(&stream, bits) != Z_OK) fail("Could not initialize zlib");
    stream.next_in = const_cast<Bytef*>(stored.data());
    stream.avail_in = static_cast<uInt>(stored.size());
    stream.next_out = result.data(); stream.avail_out = static_cast<uInt>(result.size());
    const int status = inflate(&stream, Z_FINISH);
    const bool valid = status == Z_STREAM_END && stream.total_out == expected && stream.avail_in == 0;
    inflateEnd(&stream);
    if (!valid) fail("Incomplete, oversized, corrupt or trailing compressed stream");
    result.resize(expected);
    return result;
}
} // namespace

std::uint32_t path_crc(std::string_view name) {
    return crc({reinterpret_cast<const std::uint8_t*>(name.data()), name.size()});
}
std::uint32_t content_checksum(std::span<const std::uint8_t> data) {
    std::uint32_t sum = 0;
    for (std::size_t offset = 0; offset < data.size(); offset += 0x8000)
        sum += crc(data.subspan(offset, std::min<std::size_t>(0x8000, data.size() - offset)));
    return sum;
}
Bytes decode_payload(std::uint8_t selector, std::span<const std::uint8_t> stored,
                     std::uint32_t logical_size, std::uint32_t checksum) {
    if (stored.empty() || stored.size() > max_stored || logical_size == 0 || logical_size > max_decoded)
        fail("Resource size outside supported bounds");
    Bytes result;
    if (selector == 1) {
        if (stored.size() != logical_size) fail("Raw resource size mismatch");
        result.assign(stored.begin(), stored.end());
    } else if (selector == 2) {
        result = inflate_exact(stored, logical_size, 15);
    } else if (selector == 4) {
        if (u32(stored, 0) != 0x10000) fail("Unsupported chunk header");
        std::size_t offset = 4;
        result.reserve(logical_size);
        while (result.size() < logical_size) {
            const auto encoded_size = u32(stored, offset); offset += 4;
            const bool raw = encoded_size == 0xffffffffu;
            const auto size = raw ? 0x10000u : encoded_size;
            if (size == 0 || size > 0x10000 || offset > stored.size() || size > stored.size() - offset)
                fail("Invalid chunk range");
            const auto expected = std::min<std::size_t>(0x10000, logical_size - result.size());
            const auto block = stored.subspan(offset, size);
            if (raw) {
                if (size != expected) fail("Raw chunk size mismatch");
                result.insert(result.end(), block.begin(), block.end());
            } else {
                const auto decoded = inflate_exact(block, expected, -15);
                result.insert(result.end(), decoded.begin(), decoded.end());
            }
            offset += size;
        }
        if (offset != stored.size()) fail("Trailing chunk bytes");
    } else fail("Unsupported storage selector");
    if (content_checksum(result) != checksum) fail("Decoded content checksum mismatch");
    return result;
}

Archive read_index(std::istream& input) {
    Archive archive;
    archive.file_size = stream_size(input);
    const auto header = read(input, archive.file_size, 0, 60);
    if (u32(header, 0) != 0x50414b32 || u32(header, 4) != 2 || u32(header, 8) != 2048)
        fail("Only observed PC 2KAP v2 / 2048 alignment supported");
    archive.selector = header[16];
    if (archive.selector != 1 && archive.selector != 2 && archive.selector != 4)
        fail("Unsupported storage selector");
    const auto stride = header[17] == 0 ? 40u : header[17] == 1 ? 64u : 0u;
    if (!stride) fail("Unsupported record selector");
    // This port supports the observed v2 external names table, not speculative layouts.
    if (u32(header, 56) != 0) fail("Unsupported v2 inline names table");
    const auto count_bytes = read(input, archive.file_size, 2048, 4);
    const auto names_size = u32(count_bytes, 0);
    if (!names_size || names_size > 16 * 1024 * 1024) fail("Name table exceeds bounds");
    auto names = read(input, archive.file_size, 2052, names_size);
    constexpr std::array<std::uint8_t, 14> key{0x56,0x58,0x6f,0x34,0x30,0x6a,0x33,0x40,0x24,0x25,0x5c,0x25,0x60,0x78};
    for (std::size_t i = 0; i < names.size(); ++i) names[i] ^= key[i % key.size()];
    // Mark terminated string starts once; avoid repeated scans of attacker-controlled offsets.
    std::vector<std::uint32_t> lengths(names.size(), std::numeric_limits<std::uint32_t>::max());
    std::size_t start = 0, strings = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i - start > 4096) fail("Name length exceeds bounds");
        if (names[i] == 0) {
            if (++strings > 200000) fail("Too many name strings");
            lengths[start] = static_cast<std::uint32_t>(i - start); start = i + 1;
        }
    }
    if (start != names.size()) fail("Unterminated name table");
    const auto index_offset = (2052ull + names_size + 2047) / 2048 * 2048;
    const auto prefix = read(input, archive.file_size, index_offset, 8);
    const auto count = u32(prefix, 0), bytes = u32(prefix, 4);
    if (!count || count > 100000 || bytes > 16 * 1024 * 1024 || std::uint64_t(count) * stride != bytes)
        fail("Invalid index size or count");
    const auto index = read(input, archive.file_size, index_offset + 8, bytes);
    archive.metadata_end = index_offset + 8 + bytes;
    archive.entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto at = i * stride;
        const auto name_at = u32(index, at+4);
        if (name_at >= names.size() || lengths[name_at] == std::numeric_limits<std::uint32_t>::max())
            fail("Name offset is not a terminated string start");
        Entry entry;
        entry.name.assign(reinterpret_cast<const char*>(names.data() + name_at), lengths[name_at]);
        if (std::any_of(entry.name.begin(), entry.name.end(), [](unsigned char c) { return c > 127; }))
            fail("Non-ASCII name outside observed profile");
        entry.name_crc = u32(index, at); entry.logical_size = u32(index, at+12);
        entry.sector = u64(index, at+24); entry.checksum = u32(index, at+32);
        entry.stored_size = u32(index, at+36);
        if (entry.sector != std::numeric_limits<std::uint64_t>::max()) {
            if (entry.sector > std::numeric_limits<std::uint64_t>::max() / 2048) fail("Sector overflow");
            const auto offset = entry.sector * 2048;
            if (offset < archive.metadata_end || offset >= archive.file_size) fail("Invalid payload start");
        }
        archive.entries.push_back(std::move(entry));
    }
    return archive;
}
Bytes read_resource(std::istream& input, const Archive& archive, std::size_t index) {
    if (index >= archive.entries.size()) fail("Entry index out of range");
    const auto& entry = archive.entries[index];
    if (entry.sector == std::numeric_limits<std::uint64_t>::max()) fail("Absent resource");
    if (path_crc(entry.name) != entry.name_crc) fail("Name CRC mismatch");
    if (!entry.stored_size || entry.stored_size > max_stored || !entry.logical_size || entry.logical_size > max_decoded)
        fail("Resource size outside supported bounds");
    const auto offset = entry.sector * 2048;
    auto next = archive.file_size;
    for (const auto& e : archive.entries)
        if (e.sector != std::numeric_limits<std::uint64_t>::max() && e.sector > entry.sector)
            next = std::min(next, e.sector * 2048);
    if (stream_size(input) != archive.file_size || offset < archive.metadata_end || offset > next || entry.stored_size > next - offset)
        fail("Stored resource crosses file or following sector boundary");
    const auto stored = read(input, archive.file_size, offset, entry.stored_size);
    return decode_payload(archive.selector, stored, entry.logical_size, entry.checksum);
}

Atlas dds_volume_atlas(std::span<const std::uint8_t> dds) {
    if (u32(dds, 0) != 0x20534444 || u32(dds, 4) != 124 || dds.size() < 128 ||
        u32(dds, 76) != 32 || u32(dds, 80) != 0x40 || u32(dds, 84) != 0 ||
        u32(dds, 88) != 32 || u32(dds, 92) != 0xff0000 || u32(dds, 96) != 0xff00 ||
        u32(dds, 100) != 0xff || u32(dds, 104) != 0 || u32(dds, 112) != 0x200000 ||
        !(u32(dds, 8) & 0x800000) || u32(dds, 28) != 0)
        fail("Unsupported DDS RGB32 volume profile");
    Atlas result;
    result.volume_width = u32(dds, 16); result.volume_height = u32(dds, 12); result.volume_depth = u32(dds, 24);
    const auto w = result.volume_width, h = result.volume_height, d = result.volume_depth;
    if (!w || !h || !d || w > 256 || h > 256 || d > 64 ||
        (u32(dds, 20) != 0 && u32(dds, 20) != w*4)) fail("Unsupported volume dimensions or pitch");
    if (dds.size() != 128ull + std::uint64_t(w) * h * d * 4) fail("DDS data size mismatch");
    const auto columns = std::min(8u, d);
    result.width = w * columns; result.height = h * ((d + columns - 1) / columns);
    if (std::uint64_t(result.width) * result.height * 4 > max_decoded) fail("Atlas exceeds bounds");
    result.rgba.resize(std::size_t(result.width) * result.height * 4, 0);
    for (std::uint32_t z = 0; z < d; ++z)
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) {
                const auto source = 128 + ((std::size_t(z) * h + y) * w + x) * 4;
                const auto target = ((std::size_t(z / columns) * h + y) * result.width + (z % columns) * w + x) * 4;
                result.rgba[target] = dds[source+2]; result.rgba[target+1] = dds[source+1];
                result.rgba[target+2] = dds[source]; result.rgba[target+3] = 255;
            }
    return result;
}
} // namespace blur::assets
