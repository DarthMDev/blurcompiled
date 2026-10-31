#include "rex_plume_active.hpp"
#include "rex_resolve_pitch_trial.hpp"
#include <iostream>
#include <bit>
#include <stdexcept>

using namespace blur::render::guest;

int main(int argc, char** argv) {
    try {
        {
            // Synthetic independent full-screen and downsample rectangles.
            std::array<std::uint32_t, rex_register_word_count> regs{};
            regs[0x2000] = 0x05000140;
            regs[0x2318] = 0x100000;
            regs[0x231a] = regs[0x2082] = 0x02d00500;
            const std::array<float, 6> full{-0.5f,-0.5f,1279.5f,-0.5f,1279.5f,719.5f};
            if (fullscreen_resolve_pitch_trial(regs, full) != 0x05000500)
                throw std::runtime_error("Fullscreen pitch trial lost unrelated surface bits");
            regs[0x2318] = 4;
            if (!fullscreen_resolve_pitch_trial(regs, full))
                throw std::runtime_error("Fullscreen depth-copy trial was not eligible");
            const auto baseline = regs;
            for (auto [index, value] : std::array<std::pair<unsigned,unsigned>,6>{{
                     {0x2000,0x05000500}, {0x2000,0x05010140}, {0x2318,0x100100},
                     {0x231a,0x00b40140}, {0x2082,0x00b80140}, {0x2080,1}}}) {
                regs[index] = value;
                if (fullscreen_resolve_pitch_trial(regs, full))
                    throw std::runtime_error("Pitch trial accepted incompatible state");
                regs = baseline;
            }
            const std::array<float,6> small{-0.5f,-0.5f,319.5f,-0.5f,319.5f,183.5f};
            if (fullscreen_resolve_pitch_trial(regs, small) ||
                fullscreen_resolve_pitch_trial({}, full))
                throw std::runtime_error("Pitch trial accepted downsample or absent state");
        }
        const char* bridge_path = argc > 1 ? argv[1] : "./build-plume/libblur-plume-bridge.dylib";
        bool bridge_loaded = ensure_rex_plume_bridge(bridge_path);
        if (!bridge_loaded) {
            std::cout << "Bridge not loaded from default path; skipping live bridge calls\n";
        } else {
            auto& r = get_rex_plume_runtime();
            if (!r.available || !r.context) throw std::runtime_error("Plume runtime context not initialized");
            if (r.api.abi_version != BLUR_PLUME_ABI_V2) throw std::runtime_error("Plume runtime did not negotiate ABI V2");
            std::cout << "Plume active runtime connected to bridge (ABI V2, capabilities=0x"
                      << std::hex << r.api.capabilities << std::dec << ")\n";
        }

        // Test embedded SPIR-V shader contracts
        if (!supported_ui_shader_pair(kRexUiVsSpv, kRexUiPsSpv)) {
            throw std::runtime_error("Embedded UI SPIR-V pair does not satisfy supported_ui_shader_pair contract");
        }
        if (!supported_composite_shader_pair(kRexCompositeVsSpv, kRexCompositePsSpv)) {
            throw std::runtime_error("Embedded composite SPIR-V pair does not satisfy supported_composite_shader_pair contract");
        }

        struct MockShader {
            const std::uint32_t* dwords = nullptr;
            std::size_t count = 0;
            const std::uint32_t* ucode_dwords() const { return dwords; }
            std::size_t ucode_dword_count() const { return count; }
        };

        MockShader ui_vs{kRexUiVsUcode.data(), kRexUiVsUcode.size()};
        MockShader ui_ps{kRexUiPsUcode.data(), kRexUiPsUcode.size()};
        MockShader comp_vs{kRexCompositeVsUcode.data(), kRexCompositeVsUcode.size()};
        MockShader comp_ps{kRexCompositePsUcode.data(), kRexCompositePsUcode.size()};
        MockShader dummy{nullptr, 0};

        // Independent byte-order check against the first captured bytes
        // 03 30 15 70: the SDK's native word is 0x70153003, not 0x03301570.
        if (kRexUiVsUcode.front() != 0x70153003u ||
            kRexUiPsUcode.front() != 0x00011002u ||
            kRexCompositeVsUcode.front() != 0x30052003u ||
            kRexCompositePsUcode.front() != 0x00052002u)
            throw std::runtime_error("Captured shader identity has the wrong native word order");
        auto reversed_ui = kRexUiVsUcode;
        for (auto& w : reversed_ui)
            w = (w >> 24) | ((w >> 8) & 0xff00u) |
                ((w << 8) & 0xff0000u) | (w << 24);
        MockShader wrong_endian{reversed_ui.data(), reversed_ui.size()};
        if (is_rex_ui_shader_pair(&wrong_endian, &ui_ps))
            throw std::runtime_error("Byte-swapped microcode identity was accepted");

        if (!is_rex_ui_shader_pair(&ui_vs, &ui_ps)) throw std::runtime_error("is_rex_ui_shader_pair rejected valid UI shaders");
        if (is_rex_ui_shader_pair(&comp_vs, &ui_ps)) throw std::runtime_error("is_rex_ui_shader_pair accepted mismatched vertex shader");
        if (is_rex_ui_shader_pair(&ui_vs, &dummy)) throw std::runtime_error("is_rex_ui_shader_pair accepted null pixel shader");

        if (!is_rex_composite_shader_pair(&comp_vs, &comp_ps)) throw std::runtime_error("is_rex_composite_shader_pair rejected valid composite shaders");
        if (is_rex_composite_shader_pair(&ui_vs, &comp_ps)) throw std::runtime_error("is_rex_composite_shader_pair accepted mismatched vertex shader");
        if (is_rex_composite_shader_pair(&comp_vs, &dummy)) throw std::runtime_error("is_rex_composite_shader_pair accepted null pixel shader");

        RexPlumeSubmissionSequence clock;
        if (clock.next() != 1 || clock.next() != 2)
            throw std::runtime_error("Host submission sequence did not advance");
        clock.last = std::numeric_limits<std::uint64_t>::max();
        try {
            clock.next();
            throw std::runtime_error("Host submission sequence wrapped");
        } catch (const std::overflow_error&) {}

        // Exercise the active adapter with synthetic registers/RAM and a CPU
        // bridge sink. This must reach submit; a double raw/decoded texture
        // supply used to fail before the bridge was called.
        {
            auto& runtime = get_rex_plume_runtime();
            const auto saved_api = runtime.api;
            const bool saved_available = runtime.available;
            runtime.available = true;
            runtime.api.submit = +[](BlurPlumeContext*, const BlurPlumeSubmission* sub,
                                    std::uint32_t, BlurPlumeOutput*, BlurPlumeError*) -> BlurPlumeStatus {
                static std::uint64_t last_id = 0;
                if (!sub->draw_id || sub->draw_id <= last_id)
                    throw std::runtime_error("Reused SDK ring index reached the bridge as a host identity");
                last_id = sub->draw_id;
                const auto packet = decode({sub->packet, std::size_t(sub->packet_bytes)});
                if (packet.vertex_address != 0x100 || packet.indices.size() != 6 ||
                    packet.texture_mips.size() != 4 || packet.texture_mips[0].bytes[0] != 0x11)
                    throw std::runtime_error("Active UI adapter lost synthetic resource data");
                return BLUR_PLUME_OK;
            };
            std::vector<std::uint32_t> regs(rex_register_word_count);
            regs[0x2104]=15; regs[0x2208]=4; regs[0x2200]=4;
            regs[0x2201]=0x07060706; regs[0x2205]=6;
            regs[0x21FC]=0x0004008d; regs[0x2100]=0x00ffffff;
            regs[0x2000]=1280|(2u<<16); regs[0x2204]=1u<<19;
            regs[0x2206]=0x43f; regs[0x2302]=4;
            regs[0x210F]=std::bit_cast<std::uint32_t>(640.0f);
            regs[0x2110]=std::bit_cast<std::uint32_t>(640.0f);
            regs[0x2111]=std::bit_cast<std::uint32_t>(-360.0f);
            regs[0x2112]=std::bit_cast<std::uint32_t>(360.0f);
            regs[0x2113]=std::bit_cast<std::uint32_t>(1.0f);
            regs[0x200F]=(8192u<<16)|8192u; regs[0x2082]=(256u<<16)|1280u;
            regs[0x4800]=2|(1u<<22); regs[0x4801]=18|(1u<<12);
            regs[0x4802]=7|(3u<<13);
            regs[0x4803]=(0x688u<<1)|(1u<<19)|(1u<<21)|(1u<<23);
            regs[0x4804]=3u<<6; regs[0x4805]=(1u<<9)|(2u<<12);
            regs[0x4800+95*2]=0x103; regs[0x4801+95*2]=96;
            const auto ranges=rex_ui_texture_guest_ranges(regs,0);
            std::vector<std::uint8_t> ram(ranges.mips_address+ranges.mips_bytes);
            ram[ranges.base_address]=0x11;
            std::array<std::uint8_t,512> system{};
            struct SyntheticMap {
                std::uint64_t float_bitmap[4];
                std::uint32_t float_count;
                bool float_dynamic_addressing=false;
            };
            const SyntheticMap vs{{15,0,0,0},4}, ps{{63,0,0,0},6};
            const auto before=runtime.ui_draws_submitted;
            const bool zero_clamp_accepted=handle_rex_plume_ui_draw(regs,kRexUiVsSpv,kRexUiPsSpv,
                vs,ps,system,ram.data(),1,1);
            if (zero_clamp_accepted || runtime.ui_draws_submitted != before)
                throw std::runtime_error("Zeroed vertex-index clamp reached the bridge sink");
            complete_rex_spirv_index_limits(regs,system);
            const bool submitted=handle_rex_plume_ui_draw(regs,kRexUiVsSpv,kRexUiPsSpv,
                vs,ps,system,ram.data(),1,1);
            // The SDK ring slot repeats both within a frame and after swap.
            // A rejected/interleaved host submission may also leave a gap.
            const bool repeated=handle_rex_plume_ui_draw(regs,kRexUiVsSpv,kRexUiPsSpv,
                vs,ps,system,ram.data(),1,1);
            runtime.submission_ids.next();
            const bool next_frame=handle_rex_plume_ui_draw(regs,kRexUiVsSpv,kRexUiPsSpv,
                vs,ps,system,ram.data(),2,1);
            const auto before_deferred=runtime.submission_ids.last;
            const bool missing_preload=handle_rex_plume_composite_draw(regs,kRexCompositeVsSpv,
                kRexCompositePsSpv,vs,ps,system,nullptr,2,1,{});
            runtime.api=saved_api;
            runtime.available=saved_available;
            if (!submitted || !repeated || !next_frame || runtime.ui_draws_submitted != before+3)
                throw std::runtime_error("Synthetic active UI draw never reached bridge submit");
            if (missing_preload || runtime.submission_ids.last != before_deferred ||
                runtime.composite_preload_rejections != 1)
                throw std::runtime_error("Missing HDR scene reached composite submission");
        }

        // Test 4x MSAA strip copy resolve handling
        constexpr std::uint32_t kAuxCopyDestInfo = 16777986u; // endian=2, format=6, swap=1
        std::vector<std::uint8_t> fake_msaa(1280u * 256u * 16u, 0x40u);
        bool copy_handled = handle_rex_plume_copy(
            kAuxCopyDestInfo, 6u, 1280u, 256u, 256u, 0u,
            fake_msaa, nullptr, 0u);
        if (!copy_handled) throw std::runtime_error("MSAA 4x strip resolve was not recognized by copy handler");

        auto& r = get_rex_plume_runtime();
        if (r.msaa4_strips_resolved != 1) throw std::runtime_error("MSAA strip resolve counter did not advance");

        // Test swap handler
        handle_rex_plume_swap();
        if (r.swaps_presented != 1) throw std::runtime_error("Swap counter did not advance");

        std::cout << "rex_plume_active synthetic test passed: bridge negotiation, MSAA resolve, swap counters verified\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
