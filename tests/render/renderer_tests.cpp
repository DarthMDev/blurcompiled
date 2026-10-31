#include "render/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace blur::render;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid input was accepted");
}
void pixel(const Pixels& pixels, unsigned x, unsigned y, std::array<int, 4> expected, int tolerance = 0) {
    const auto offset = (std::size_t(y) * pixels.width + x) * 4;
    for (unsigned c = 0; c < 4; ++c)
        if (std::abs(int(pixels.rgba.at(offset + c)) - expected[c]) > tolerance) {
            std::cerr << "Pixel " << x << ',' << y << " channel " << c << " got " << int(pixels.rgba[offset+c])
                      << " expected " << expected[c] << '\n';
            throw std::runtime_error("GPU pixel mismatch");
        }
}
void cpu() {
    auto vertices = quad();
    Draw draw{1, vertices, quad_indices};
    Frame frame{17, 11, {0, 0, 0, 1}, {&draw, 1}};
    validate_frame(frame);
    check(rgba_size(17, 11) == 748, "Wrong byte size");
    rejects([] { rgba_size(0, 1); });
    rejects([] { rgba_size(1, std::numeric_limits<std::uint32_t>::max()); });
    auto indices = quad_indices;
    indices[5] = 4; draw.indices = indices;
    rejects([&] { validate_frame(frame); });
    draw.indices = quad_indices;
    vertices[0].position[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { validate_frame(frame); });
    vertices = quad();
    draw.scissor = {16, 0, 2, 1}; rejects([&] { validate_frame(frame); });
    draw.scissor = {0, 1, 0, 1}; rejects([&] { validate_frame(frame); });
    draw.scissor = {0, 0, 17, 11}; validate_frame(frame);
    draw.scissor = {}; draw.texture = 0; rejects([&] { validate_frame(frame); });
    draw.texture = 1; draw.indices = std::span(quad_indices).first(5);
    rejects([&] { validate_frame(frame); });
    draw.indices = quad_indices; frame.clear[0] = 2;
    rejects([&] { validate_frame(frame); });
    std::cout << "Native render contract: passed\n";
}
void gpu() {
    Renderer renderer;
    std::cout << "Plume / Metal device: " << renderer.device_name() << '\n';
    auto vertices = quad();
    // Asymmetric four-corner fixture checks orientation, channel order, all
    // indexed triangles and nearest sampling without relying on a screenshot.
    const std::array<std::uint8_t, 16> corners{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};
    auto texture = renderer.create_texture(2, 2, corners);
    Draw draw{texture, vertices, quad_indices};
    Frame frame{18, 14, {0,0,0,1}, {&draw, 1}};
    auto pixels = renderer.render(frame);
    for (unsigned y = 0; y < 14; ++y)
        for (unsigned x = 0; x < 18; ++x) {
            const auto corner = ((y >= 7) * 2 + (x >= 9)) * 4;
            pixel(pixels, x, y, {corners[corner],corners[corner+1],corners[corner+2],255});
        }
    // Re-upload, odd row pitch and target resize; compare every channel of
    // every texel. Source is deliberately not row-aligned for the GPU copy.
    constexpr unsigned width = 67, height = 19;
    std::vector<std::uint8_t> pattern(width * height * 4);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        auto i = (y * width + x) * 4;
        pattern[i] = x * 3; pattern[i+1] = y * 11; pattern[i+2] = (x ^ y) * 2; pattern[i+3] = 255;
    }
    auto odd = renderer.create_texture(width, height, pattern);
    draw.texture = odd; frame.width = width; frame.height = height;
    check(renderer.render(frame).rgba == pattern, "Odd-pitch texture round trip differs");
    for (auto& byte : pattern) byte = 255 - byte;
    renderer.update_texture(odd, pattern);
    check(renderer.render(frame).rgba == pattern, "Updated texture differs");
    // Blend over a clear with known UNORM rounding, restricted by a scissor.
    const std::array<std::uint8_t, 4> red{255,0,0,128};
    auto translucent = renderer.create_texture(1, 1, red);
    draw.texture = translucent; draw.blend = Blend::alpha; draw.scissor = {3,2,7,5};
    frame.clear = {0,0,1,1};
    pixels = renderer.render(frame);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
        pixel(pixels, x, y, x >= 3 && x < 10 && y >= 2 && y < 7 ?
              std::array<int,4>{128,0,127,255} : std::array<int,4>{0,0,255,255}, 1);
    // Sequential draws must preserve earlier attachment contents when state
    // changes; the second one covers only the right half of the first.
    const std::array<std::uint8_t, 4> green{0,255,0,255};
    auto solid = renderer.create_texture(1,1,green);
    auto half = quad(0,1,1,-1);
    std::array<Draw,2> layered{{{solid,vertices,quad_indices}, {translucent,half,quad_indices,Blend::alpha}}};
    frame = {18,14,{0,0,0,1},layered}; pixels = renderer.render(frame);
    pixel(pixels,2,5,{0,255,0,255}); pixel(pixels,15,5,{128,127,0,255},1);
    // Linear sampling at a 2x2 image's center averages all four corners.
    auto linear = renderer.create_texture(2,2,corners,Filter::linear);
    draw = {linear,vertices,quad_indices}; frame = {1,1,{0,0,0,1},{&draw,1}};
    pixel(renderer.render(frame),0,0,{128,128,64,255},1);
    renderer.destroy_texture(texture);
    rejects([&] { renderer.update_texture(texture,corners); });
    draw.texture = texture; rejects([&] { renderer.render(frame); });
    draw.texture = solid;
    pixel(renderer.render(frame),0,0,{0,255,0,255}); // rejection left renderer usable
    rejects([&] { renderer.update_texture(solid,corners); });
    std::cout << "Native Plume GPU upload/draw/blend/scissor/resize/lifetime/readback: passed\n";
}
}
int main(int argc, char** argv) {
    try {
        cpu();
        if (argc < 2 || std::string_view(argv[1]) != "--cpu-only") gpu();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
