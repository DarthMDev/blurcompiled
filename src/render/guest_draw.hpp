#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace blur::render::guest {
// New native adapter input, not guest ABI. Physical addresses are explicit
// offsets in a bounded GPU byte buffer, never host pointers.
struct TextureMip {
    std::uint32_t width, height;
    std::vector<std::uint8_t> bytes; // native BC1 blocks, no guest tiling
};
struct Packet {
    std::uint32_t width = 0, height = 0;
    std::uint32_t viewport_width = 0, viewport_height = 0;
    std::array<std::uint32_t,4> scissor{};
    std::uint32_t samples = 4;
    std::uint32_t vertex_address = 0;
    std::vector<std::uint8_t> vertex_bytes;
    std::vector<std::uint32_t> vertex_spirv, pixel_spirv;
    std::array<std::vector<std::uint8_t>,5> uniforms;
    std::vector<std::uint16_t> indices;
    std::vector<TextureMip> texture_mips;
};
// Bounded first schema: translated vertex + pixel stages, shared physical
// memory, five constant blocks, one BC1 array texture/sampler, indexed triangle
// lists, alpha blending, depth disabled and 1x/4x RGBA8 targets.
void validate(const Packet& packet);
class UnsupportedDraw : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
// CPU-only eligibility check, also used by the host bridge before GPU setup.
void validate_supported(const Packet& packet);
// Exact translated-module gates shared by replay and the live SDK frontend.
bool supported_ui_shader_pair(std::span<const std::uint8_t> vertex,
                              std::span<const std::uint8_t> pixel);
bool supported_composite_shader_pair(std::span<const std::uint8_t> vertex,
                                     std::span<const std::uint8_t> pixel);
struct CompositeTexture {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba8_bytes; // linear RGBA8 texel bytes after host swizzle
};
struct CompositePacket {
    std::uint32_t width = 0, height = 0;
    std::uint32_t viewport_width = 0, viewport_height = 0;
    std::array<std::uint32_t,4> scissor{};
    std::uint32_t samples = 1;
    std::uint32_t vertex_address = 0;
    std::uint32_t vertex_count = 0;
    std::vector<std::uint8_t> vertex_bytes;
    std::vector<std::uint32_t> vertex_spirv, pixel_spirv;
    std::array<std::vector<std::uint8_t>,5> uniforms;
    std::array<CompositeTexture,2> textures; // fetch slot 0 (1280x720) and fetch slot 1 (320x180)
};
void validate_composite(const CompositePacket& packet);
void validate_supported_composite(const CompositePacket& packet);
struct CompositeResult {
    std::uint32_t width = 0, height = 0, samples = 1;
    std::vector<std::uint8_t> rgba16f_bytes; // pixel-major IEEE 754 binary16 RGBA
    std::string vertex_msl, pixel_msl;
};
// New portable interchange format BPG1. All integers are little endian.
Packet decode(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode(const Packet& packet);
// Portable interchange format BPG2 for composite passes. All integers are little endian.
CompositePacket decode_composite(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_composite(const CompositePacket& packet);
struct Result {
    std::uint32_t width, height, samples;
    // pixel-major, then sample index, then RGBA8. Sample indices are Metal's
    // indices; equality to a different API's sample positions is not assumed.
    std::vector<std::uint8_t> rgba_samples;
    std::string vertex_msl, pixel_msl;
};
enum class Diagnostic { none, no_cull, clear_red };
enum class LoadAction { clear_zero, preserve };
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    const std::string& device_name() const;
    void verify_synthetic_readback();
    Result draw(const Packet& packet, Diagnostic diagnostic = Diagnostic::none,
                LoadAction load_action = LoadAction::clear_zero);
    CompositeResult draw_composite(const CompositePacket& packet,
                                   std::span<const std::uint8_t> preload_rgba16f = {},
                                   std::uint32_t preload_width = 0,
                                   std::uint32_t preload_height = 0,
                                   LoadAction load_action = LoadAction::preserve);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
