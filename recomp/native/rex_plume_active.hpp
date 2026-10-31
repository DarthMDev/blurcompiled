#pragma once
#include "render/plume_bridge.h"
#include "rex_plume_draw.hpp"
#include "rex_plume_embedded_shaders.hpp"
#if __has_include(<rex/logging.h>)
#include <rex/logging.h>
#else
#include <iostream>
#include <format>
#define REXLOG_INFO(...) std::cout << "[info] " << std::format(__VA_ARGS__) << "\n"
#define REXLOG_WARN(...) std::cout << "[warn] " << std::format(__VA_ARGS__) << "\n"
#define REXLOG_ERROR(...) std::cerr << "[error] " << std::format(__VA_ARGS__) << "\n"
#endif
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <vector>

namespace blur::render::guest {

// Bridge draw identities belong to the host context, not the SDK draw-ring
// slot (which is reused). UI and composite submissions share this sequence.
struct RexPlumeSubmissionSequence {
    std::uint64_t last = 0;
    std::uint64_t next() {
        if (last == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Plume submission identity exhausted");
        return ++last;
    }
};

struct RexPlumeRuntimeState {
    void* bridge_library = nullptr;
    BlurPlumeApiV2 api{};
    BlurPlumeContext* context = nullptr;
    bool initialized = false;
    bool available = false;
    RexPlumeSubmissionSequence submission_ids;

    // Intermediate state buffers
    std::vector<std::uint8_t> resolved_ui_linear{std::vector<std::uint8_t>(1280u * 720u * 4u, 0u)};
    std::vector<std::uint8_t> active_hdr_target{};
    std::vector<std::uint8_t> swap_source_linear{};

    // Statistics
    std::uint64_t draws_examined = 0;
    std::uint64_t ui_shader_matches = 0;
    std::uint64_t composite_shader_matches = 0;
    std::uint64_t ui_draws_submitted = 0;
    std::uint64_t composite_draws_submitted = 0;
    std::uint64_t msaa4_strips_resolved = 0;
    std::uint64_t terminal_resolves = 0;
    std::uint64_t swaps_presented = 0;
    std::uint64_t rejections = 0;
    std::uint64_t composite_preload_rejections = 0;

    ~RexPlumeRuntimeState() {
        if (context && api.destroy) {
            api.destroy(context, nullptr);
            context = nullptr;
        }
        if (bridge_library) {
            dlclose(bridge_library);
            bridge_library = nullptr;
        }
    }
};

inline RexPlumeRuntimeState& get_rex_plume_runtime() {
    static RexPlumeRuntimeState state;
    return state;
}

inline bool ensure_rex_plume_bridge(const char* library_path = nullptr) {
    auto& r = get_rex_plume_runtime();
    if (r.initialized) return r.available;
    r.initialized = true;

    std::string sibling_path;
    Dl_info dlinfo{};
    if (dladdr(reinterpret_cast<const void*>(&ensure_rex_plume_bridge), &dlinfo) && dlinfo.dli_fname) {
        std::string fname(dlinfo.dli_fname);
        auto pos = fname.find_last_of('/');
        if (pos != std::string::npos) {
            sibling_path = fname.substr(0, pos) + "/libblur-plume-bridge.dylib";
        }
    }

    const char* paths[] = {
        library_path,
        sibling_path.empty() ? nullptr : sibling_path.c_str(),
        "libblur-plume-bridge.dylib",
        "./libblur-plume-bridge.dylib",
        "@loader_path/libblur-plume-bridge.dylib",
        "./build-plume/libblur-plume-bridge.dylib"
    };

    for (const char* path : paths) {
        if (!path) continue;
        r.bridge_library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (r.bridge_library) {
            REXLOG_INFO("Loaded Plume host bridge from: {}", path);
            break;
        }
    }

    if (!r.bridge_library) {
        REXLOG_WARN("Could not load libblur-plume-bridge.dylib; Plume runtime will fall back to CPU state tracking");
        return false;
    }

    auto get_api = reinterpret_cast<BlurPlumeGetApi>(dlsym(r.bridge_library, "blur_plume_get_api"));
    if (!get_api) {
        REXLOG_ERROR("libblur-plume-bridge.dylib missing blur_plume_get_api symbol");
        dlclose(r.bridge_library);
        r.bridge_library = nullptr;
        return false;
    }

    BlurPlumeStatus s = get_api(BLUR_PLUME_ABI_V2, sizeof(r.api), &r.api);
    if (s != BLUR_PLUME_OK || r.api.abi_version != BLUR_PLUME_ABI_V2) {
        REXLOG_ERROR("Plume bridge ABI V2 negotiation failed: code={}", s);
        dlclose(r.bridge_library);
        r.bridge_library = nullptr;
        return false;
    }

    BlurPlumeError err{};
    s = r.api.create(&r.context, &err);
    if (s != BLUR_PLUME_OK || !r.context) {
        REXLOG_ERROR("Failed to create Plume context: {}", err.message);
        dlclose(r.bridge_library);
        r.bridge_library = nullptr;
        return false;
    }

    REXLOG_INFO("Plume native renderer initialized successfully (ABI V2, capabilities=0x{:x})", r.api.capabilities);
    r.available = true;
    return true;
}

template<class Map>
inline bool handle_rex_plume_ui_draw(
    std::span<const std::uint32_t> words,
    std::span<const std::uint8_t> vertex_binary,
    std::span<const std::uint8_t> pixel_binary,
    const Map& vertex_map,
    const Map& pixel_map,
    std::span<const std::uint8_t> system_constants,
    const std::uint8_t* xbox_ram_base,
    std::uint64_t frame_id,
    std::uint64_t draw_id) {

    auto& r = get_rex_plume_runtime();
    if (!r.available && !ensure_rex_plume_bridge()) return false;

    try {
        if (!supported_ui_shader_pair(vertex_binary, pixel_binary)) return false;

        auto convert = [](const Map& src) {
            RexFloatConstantMap m;
            std::copy_n(src.float_bitmap, 4, m.bitmap.begin());
            m.count = src.float_count;
            m.dynamic_addressing = src.float_dynamic_addressing;
            return m;
        };

        RexUiShaderState shader_state{
            words, vertex_binary, pixel_binary, system_constants,
            convert(vertex_map), convert(pixel_map)
        };

        const auto vrange = rex_vertex_range(words, 95);
        if (!xbox_ram_base || vrange.bytes == 0) return false;

        UiSnapshot snapshot;
        snapshot.vertex_address = vrange.address;
        snapshot.vertex_bytes = std::span<const std::uint8_t>(xbox_ram_base + vrange.address, vrange.bytes);

        const auto trange = rex_ui_texture_guest_ranges(words, 0);
        RexGuestTextureData guest_tex{
            trange.base_address, trange.mips_address,
            trange.base_bytes > 0 ? std::span<const std::uint8_t>(xbox_ram_base + trange.base_address, trange.base_bytes) : std::span<const std::uint8_t>{},
            trange.mips_bytes > 0 ? std::span<const std::uint8_t>(xbox_ram_base + trange.mips_address, trange.mips_bytes) : std::span<const std::uint8_t>{}
        };

        // Packet construction owns the single decode of the raw guest texture.
        // Supplying decoded mips as well violates its resource ownership contract.

        RexTextureView tex_view{
            RexNativeTextureFormat::bc1_unorm, RexNativeTextureKind::array_2d,
            Toggle::enabled, 0x688, trange.width, trange.height, 1, 0, trange.mip_count,
            trange.base_address, trange.mips_address
        };

        RexRenderTargetView rt_view{
            ColorFormat::rgba8_unorm, Toggle::enabled, 1280, 512, 4, 0
        };

        auto packet = make_rex_ui_packet(shader_state, snapshot, tex_view, rt_view, guest_tex);
        auto encoded = encode(packet);
        const auto submission_id = r.submission_ids.next();

        BlurPlumeSubmission sub{
            sizeof(sub),
            r.ui_draws_submitted == 0 ? BLUR_PLUME_LOAD_CLEAR_ZERO : BLUR_PLUME_LOAD_PRESERVE,
            frame_id, submission_id,
            encoded.data(), encoded.size()
        };

        BlurPlumeOutput out{};
        BlurPlumeError err{};
        BlurPlumeStatus s = r.api.submit(r.context, &sub, sizeof(out), &out, &err);
        if (s == BLUR_PLUME_OK) {
            ++r.ui_draws_submitted;
            if (r.ui_draws_submitted == 1 || r.ui_draws_submitted % 256 == 0) {
                REXLOG_INFO("Plume UI draw submitted: id={} frame={} w={} h={} samples={} sdk_draw={}",
                            submission_id, frame_id, out.width, out.height, out.samples, draw_id);
            }
            return true;
        } else {
            ++r.rejections;
            REXLOG_WARN("Plume UI submit rejected: {}", err.message);
            return false;
        }
    } catch (const std::exception& e) {
        ++r.rejections;
        if (r.rejections == 1 || r.rejections % 256 == 0)
            REXLOG_WARN("Plume draw rejected before submission: {}", e.what());
        return false;
    }
}

template<class Map>
inline bool handle_rex_plume_composite_draw(
    std::span<const std::uint32_t> words,
    std::span<const std::uint8_t> vertex_binary,
    std::span<const std::uint8_t> pixel_binary,
    const Map& vertex_map,
    const Map& pixel_map,
    std::span<const std::uint8_t> system_constants,
    const std::uint8_t* xbox_ram_base,
    std::uint64_t frame_id,
    std::uint64_t draw_id,
    std::span<const std::uint8_t> preload_rgba16f) {

    auto& r = get_rex_plume_runtime();
    if (!r.available && !ensure_rex_plume_bridge()) return false;

    try {
        if (!supported_composite_shader_pair(vertex_binary, pixel_binary)) return false;
        // A UI target is not a preserved HDR scene. Reject this unsupported
        // live call before it can fault the shared bridge GPU context.
        if (preload_rgba16f.empty()) {
            ++r.rejections;
            ++r.composite_preload_rejections;
            if (r.composite_preload_rejections == 1 || r.composite_preload_rejections % 256 == 0)
                REXLOG_WARN("Plume composite deferred: synchronized RGBA16F scene preload unavailable");
            return false;
        }

        auto convert = [](const Map& src) {
            RexFloatConstantMap m;
            std::copy_n(src.float_bitmap, 4, m.bitmap.begin());
            m.count = src.float_count;
            m.dynamic_addressing = src.float_dynamic_addressing;
            return m;
        };

        RexUiShaderState shader_state{
            words, vertex_binary, pixel_binary, system_constants,
            convert(vertex_map), convert(pixel_map)
        };

        const auto vrange = rex_vertex_range(words, 95);
        if (!xbox_ram_base || vrange.bytes == 0) return false;

        const auto comp_vtx = std::span<const std::uint8_t>(xbox_ram_base + vrange.address, vrange.bytes);
        // composite input 0 is the resolved UI buffer (1280x720)
        // composite input 1 is 320x180 bloom/blur from guest memory
        constexpr std::uint32_t kIn1Base = 271482880u;
        const auto in1_span = std::span<const std::uint8_t>(xbox_ram_base + kIn1Base, 320u * 180u * 4u);

        RexCompositeResources comp_res{
            vrange.address, comp_vtx,
            {r.resolved_ui_linear, in1_span}
        };

        std::array<RexTextureView,2> tex_views{{
            {RexNativeTextureFormat::rgba8_unorm, RexNativeTextureKind::array_2d, Toggle::disabled, 0x60au, 1280, 720, 1, 0, 1, 271814656u, 0u},
            {RexNativeTextureFormat::rgba8_unorm, RexNativeTextureKind::array_2d, Toggle::disabled, 0x60au, 320, 180, 1, 0, 1, kIn1Base, 0u}
        }};

        RexRenderTargetView f16_target{ColorFormat::rgba16_float, Toggle::disabled, 1280, 2048, 1, 0};
        auto comp_packet = make_rex_composite_packet(shader_state, comp_res, tex_views, f16_target);
        auto encoded = encode_composite(comp_packet);
        const auto submission_id = r.submission_ids.next();

        BlurPlumeCompositeSubmission comp_sub{
            sizeof(comp_sub),
            BLUR_PLUME_LOAD_PRESERVE,
            frame_id, submission_id,
            encoded.data(), encoded.size(),
            preload_rgba16f.data(), preload_rgba16f.size(),
            1280, 720
        };

        BlurPlumeCompositeOutput comp_out{};
        BlurPlumeError err{};
        BlurPlumeStatus s = r.api.submit_composite(r.context, &comp_sub, sizeof(comp_out), &comp_out, &err);
        if (s == BLUR_PLUME_OK && comp_out.rgba16f_bytes && comp_out.byte_count) {
            ++r.composite_draws_submitted;
            r.active_hdr_target.assign(comp_out.rgba16f_bytes, comp_out.rgba16f_bytes + comp_out.byte_count);
            REXLOG_INFO("Plume composite draw executed: frame={} draw={} bytes={} sdk_draw={}",
                        frame_id, submission_id, comp_out.byte_count, draw_id);
            return true;
        } else {
            ++r.rejections;
            REXLOG_WARN("Plume composite submit rejected: {}", err.message);
            return false;
        }
    } catch (const std::exception& e) {
        ++r.rejections;
        if (r.rejections == 1 || r.rejections % 256 == 0)
            REXLOG_WARN("Plume draw rejected before submission: {}", e.what());
        return false;
    }
}

inline bool handle_rex_plume_copy(
    std::uint32_t rb_copy_dest_info,
    std::uint32_t sample_select,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t source_height,
    std::uint32_t dest_row_offset,
    std::span<const std::uint8_t> msaa4_source,
    std::uint8_t* guest_ram_dest,
    std::uint32_t dest_address) {

    auto& r = get_rex_plume_runtime();

    // Check if this is a 4x MSAA UI tile resolve strip
    const std::uint32_t dest_format = (rb_copy_dest_info >> 7) & 0x1f;
    const std::uint32_t copy_dest_endian = rb_copy_dest_info & 7;
    const std::uint32_t swap_rb = (rb_copy_dest_info >> 24) & 1;

    if (dest_format == 6 && copy_dest_endian == 2 && sample_select == 6 && swap_rb == 1 &&
        width == 1280 && msaa4_source.size() == std::size_t(width) * height * 16u) {
        RexMsaa4ResolveStrip strip{
            rb_copy_dest_info, sample_select, width, height, source_height, dest_row_offset, msaa4_source
        };
        resolve_rex_rgba8_msaa4_strip(strip, 720u, r.resolved_ui_linear);
        ++r.msaa4_strips_resolved;
        if (dest_row_offset + height >= 720u) {
            REXLOG_INFO("Plume UI MSAA resolve completed (3 strips -> 1280x720 RGBA8)");
        }
        return true;
    }

    // Check if this is the terminal RGBA16F -> 7e3 HDR resolve
    if (!r.active_hdr_target.empty() && dest_format == 6 && swap_rb == 1 && width == 1280 && height == 720) {
        std::vector<std::uint16_t> halfs(r.active_hdr_target.size() / 2u);
        std::memcpy(halfs.data(), r.active_hdr_target.data(), r.active_hdr_target.size());
        RexHdr7e3ResolveSpec spec{width, height, rb_copy_dest_info, 1280u, guest_ram_dest != nullptr};
        auto out = resolve_rex_rgba16f_to_7e3_rgba8(spec, halfs);
        r.swap_source_linear = std::move(out.linear_rgba8);
        if (guest_ram_dest && !out.tiled_destination.empty()) {
            std::memcpy(guest_ram_dest + dest_address, out.tiled_destination.data(), out.tiled_destination.size());
        }
        ++r.terminal_resolves;
        REXLOG_INFO("Plume terminal HDR resolve completed: linear_bytes={} tiled_bytes={}",
                    r.swap_source_linear.size(), out.tiled_destination.size());
        return true;
    }

    return false;
}

inline void handle_rex_plume_swap() {
    auto& r = get_rex_plume_runtime();
    ++r.swaps_presented;
    if (r.swaps_presented == 1 || r.swaps_presented % 60 == 0) {
        REXLOG_INFO("Plume presenter status: swaps={} ui_draws={} comp_draws={} msaa_resolves={} term_resolves={} rejects={}",
                    r.swaps_presented, r.ui_draws_submitted, r.composite_draws_submitted,
                    r.msaa4_strips_resolved, r.terminal_resolves, r.rejections);
    }
}

template<class ShaderType>
inline bool is_rex_ui_shader_pair(const ShaderType* vs, const ShaderType* ps) {
    if (!vs || !ps) return false;
    if (vs->ucode_dword_count() != kRexUiVsUcode.size() ||
        ps->ucode_dword_count() != kRexUiPsUcode.size()) return false;
    return std::equal(vs->ucode_dwords(), vs->ucode_dwords() + vs->ucode_dword_count(), kRexUiVsUcode.begin()) &&
           std::equal(ps->ucode_dwords(), ps->ucode_dwords() + ps->ucode_dword_count(), kRexUiPsUcode.begin());
}

template<class ShaderType>
inline bool is_rex_composite_shader_pair(const ShaderType* vs, const ShaderType* ps) {
    if (!vs || !ps) return false;
    if (vs->ucode_dword_count() != kRexCompositeVsUcode.size() ||
        ps->ucode_dword_count() != kRexCompositePsUcode.size()) return false;
    return std::equal(vs->ucode_dwords(), vs->ucode_dwords() + vs->ucode_dword_count(), kRexCompositeVsUcode.begin()) &&
           std::equal(ps->ucode_dwords(), ps->ucode_dwords() + ps->ucode_dword_count(), kRexCompositePsUcode.begin());
}

template<class ShaderType, class Map>
inline bool dispatch_rex_plume_draw(
    const ShaderType* vs,
    const ShaderType* ps,
    std::span<const std::uint32_t> regs,
    const Map& vs_map,
    const Map& ps_map,
    std::span<const std::uint8_t> system_constants,
    const std::uint8_t* xbox_ram_base,
    std::uint64_t swap_count,
    std::uint64_t draw_index,
    const char* backend = "unknown") {

    auto& runtime = get_rex_plume_runtime();
    ++runtime.draws_examined;
    const bool ui_match = is_rex_ui_shader_pair(vs, ps);
    const bool composite_match = is_rex_composite_shader_pair(vs, ps);
    runtime.ui_shader_matches += ui_match;
    runtime.composite_shader_matches += composite_match;
    if (runtime.draws_examined == 1 || runtime.draws_examined % 65536 == 0 ||
        (ui_match && runtime.ui_shader_matches == 1) ||
        (composite_match && runtime.composite_shader_matches == 1)) {
        REXLOG_INFO("Plume dispatch: backend={} examined={} ui_matches={} composite_matches={} vs_words={} ps_words={}",
                    backend, runtime.draws_examined, runtime.ui_shader_matches,
                    runtime.composite_shader_matches, vs ? vs->ucode_dword_count() : 0,
                    ps ? ps->ucode_dword_count() : 0);
    }
    if (ui_match) {
        return handle_rex_plume_ui_draw(
            regs,
            kRexUiVsSpv,
            kRexUiPsSpv,
            vs_map,
            ps_map,
            system_constants,
            xbox_ram_base,
            swap_count + 1,
            draw_index + 1);
    }

    if (composite_match) {
        return handle_rex_plume_composite_draw(
            regs,
            kRexCompositeVsSpv,
            kRexCompositePsSpv,
            vs_map,
            ps_map,
            system_constants,
            xbox_ram_base,
            swap_count + 1,
            draw_index + 1,
            {});
    }

    return false;
}

} // namespace blur::render::guest
