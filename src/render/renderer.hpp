#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace blur::render {
// New host-side contract. Guest pointers, byte order, tiling and shader decoding
// belong in a separately verified adapter, never in this rendering interface.
struct Vertex {
    std::array<float, 3> position;
    std::array<float, 2> uv;
    std::array<float, 4> color{1, 1, 1, 1};
};
struct Rect { std::uint32_t x, y, width, height; };
enum class Blend { copy, alpha };
enum class Filter { nearest, linear };
using TextureId = std::uint64_t;
struct Draw {
    TextureId texture;
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;
    Blend blend = Blend::copy;
    // Empty means the full render target. Top-left origin, in target pixels.
    Rect scissor{};
};
struct Frame {
    std::uint32_t width, height;
    std::array<float, 4> clear{0, 0, 0, 1};
    std::span<const Draw> draws;
};
struct Pixels {
    std::uint32_t width, height;
    std::vector<std::uint8_t> rgba;
};
// Throws before GPU recording on unsupported or malformed host input.
void validate_frame(const Frame& frame);
std::uint64_t rgba_size(std::uint32_t width, std::uint32_t height);

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    const std::string& device_name() const;
    TextureId create_texture(std::uint32_t width, std::uint32_t height,
                             std::span<const std::uint8_t> rgba, Filter filter = Filter::nearest);
    void update_texture(TextureId texture, std::span<const std::uint8_t> rgba);
    void destroy_texture(TextureId texture);
    // Synchronous single-frame ownership makes resource destruction/reupload
    // deterministic during bring-up. No unbounded frames in flight.
    Pixels render(const Frame& frame);
    // Native macOS NSWindow/CAMetalLayer supplied by the SDL platform shell.
    void attach_window(void* window, void* metal_layer);
    void detach_window();
    bool present(const Frame& frame);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::array<Vertex, 4> quad(float left = -1, float top = 1, float right = 1, float bottom = -1);
inline constexpr std::array<std::uint32_t, 6> quad_indices{0, 1, 2, 2, 1, 3};
}  // namespace blur::render
