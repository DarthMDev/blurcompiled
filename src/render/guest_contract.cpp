#include "render/guest_draw.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace blur::render::guest {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw UnsupportedDraw(message);
}
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) throw std::invalid_argument("Short UI constant block");
    return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset+1]) << 8 |
           std::uint32_t(bytes[offset+2]) << 16 | std::uint32_t(bytes[offset+3]) << 24;
}
std::string sha(std::span<const std::uint8_t> words) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(words.data(), CC_LONG(words.size_bytes()), digest);
    std::ostringstream out;
    for (auto b : digest) out << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
    return out.str();
}
}
bool supported_ui_shader_pair(std::span<const std::uint8_t> vertex,
                              std::span<const std::uint8_t> pixel) {
    auto bounded=[](auto bytes){return bytes.size()>=20 && bytes.size()<=(1u<<20) && bytes.size()%4==0;};
    return bounded(vertex) && bounded(pixel) &&
        sha(vertex)=="e77b9b0228285837f7b86f0fd8aad7cc5c82c574d20ac43d948212d592ef898f" &&
        sha(pixel)=="a67b801f0d3b45683d796b599781fa0174e55149458cd36f9b982c08e3005a13";
}
bool supported_composite_shader_pair(std::span<const std::uint8_t> vertex,
                                     std::span<const std::uint8_t> pixel) {
    auto bounded=[](auto bytes){return bytes.size()>=20 && bytes.size()<=(1u<<20) && bytes.size()%4==0;};
    return bounded(vertex) && bounded(pixel) &&
        sha(vertex)=="4c6429b012d34ca9406886f3cc419e008b101d12af81e33afec2b4c26df64315" &&
        sha(pixel)=="d95daf40fc979139cd8f74d6b002d46cbfca60342d9f453b9c86aca60ad1263e";
}
void validate_supported(const Packet& p) {
    validate(p);
    require(supported_ui_shader_pair(
            {reinterpret_cast<const std::uint8_t*>(p.vertex_spirv.data()),p.vertex_spirv.size()*4},
            {reinterpret_cast<const std::uint8_t*>(p.pixel_spirv.data()),p.pixel_spirv.size()*4}),
            "Unsupported UI shader pair: first adapter only accepts the captured modules");
    constexpr std::array<std::size_t,5> sizes{512,64,96,160,768};
    for (std::size_t i = 0; i < 5; ++i)
        require(p.uniforms[i].size() == sizes[i], "UI constant layout differs from selected shader contract");
    require((word(p.uniforms[4],190*4) & ~3u) == p.vertex_address, "UI fetch and supplied physical vertex address differ");
    require(word(p.uniforms[0],8) == 0 && word(p.uniforms[0],16) == 0 && word(p.uniforms[0],496) == 0,
            "UI vertex index endian/base/min are unsupported");
    require(word(p.uniforms[0],500) >= *std::max_element(p.indices.begin(),p.indices.end()), "UI index clamp differs");
    require((word(p.uniforms[0],176) & 0x3f) == 0, "Signed texture branch is unsupported in the first UI adapter");
}
void validate_supported_composite(const CompositePacket& p) {
    validate_composite(p);
    require(supported_composite_shader_pair(
            {reinterpret_cast<const std::uint8_t*>(p.vertex_spirv.data()),p.vertex_spirv.size()*4},
            {reinterpret_cast<const std::uint8_t*>(p.pixel_spirv.data()),p.pixel_spirv.size()*4}),
            "Unsupported composite shader pair: adapter only accepts the verified composite modules");
    constexpr std::array<std::size_t,5> sizes{512,16,16,160,768};
    for (std::size_t i = 0; i < 5; ++i)
        require(p.uniforms[i].size() == sizes[i], "Composite constant layout differs from selected shader contract");
    require((word(p.uniforms[4],190*4) & ~3u) == p.vertex_address, "Composite fetch and supplied physical vertex address differ");
    require(word(p.uniforms[0],8) == 0 && word(p.uniforms[0],16) == 0 && word(p.uniforms[0],496) == 0,
            "Composite vertex index endian/base/min are unsupported");
    require(word(p.uniforms[0],500) + 1u >= p.vertex_count, "Composite index clamp differs");
    require((word(p.uniforms[0],176) & 0xffffu) == 0, "Signed texture branch is unsupported in the composite adapter");
    require(p.textures[0].width == 1280 && p.textures[0].height == 720 &&
            p.textures[1].width == 320 && p.textures[1].height == 180,
            "Composite texture dimensions differ from selected shader contract");
}
}
