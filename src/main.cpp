#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "assets/assets.hpp"
#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

bool verify_lut(SDL_Renderer* renderer, const blur::assets::Atlas& atlas) {
    const SDL_Rect rect{56, 360, static_cast<int>(atlas.width * 2), static_cast<int>(atlas.height * 2)};
    auto* captured = SDL_RenderReadPixels(renderer, &rect);
    if (!captured) { std::cerr << SDL_GetError() << '\n'; return false; }
    auto* rgba = SDL_ConvertSurface(captured, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(captured);
    if (!rgba) { std::cerr << SDL_GetError() << '\n'; return false; }
    bool match = rgba->w == rect.w && rgba->h == rect.h;
    for (std::uint32_t y = 0; match && y < atlas.height; ++y)
        for (std::uint32_t x = 0; match && x < atlas.width; ++x) {
            const auto* pixel = static_cast<const Uint8*>(rgba->pixels) + (y * 2 + 1) * rgba->pitch + (x * 2 + 1) * 4;
            const auto source = (std::size_t(y) * atlas.width + x) * 4;
            for (int c = 0; c < 4; ++c)
                if (std::abs(int(pixel[c]) - int(atlas.rgba[source + c])) > 1) match = false;
        }
    SDL_DestroySurface(rgba);
    std::cout << "LUT framebuffer check: " << (match ? "matched " : "FAILED ")
              << atlas.width * atlas.height << " texels (tolerance 1/255)\n";
    return match;
}

int main(int argc, char** argv) {
    fs::path assets;
    int frame_limit = 0;
    bool probe = false;
    bool show_lut = false, verify_lut_render = false;
    fs::path snapshot;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--help") {
            std::cout << "Blur Native research host (no gameplay yet)\n"
                         "  --assets PATH   Existing PC installation, read only\n"
                         "  --probe         Validate bootstrap inputs without opening a window\n"
                         "  --lut           Decode and display a supplied DDS LUT volume as slices\n"
                         "  --verify-lut-render  Check the LUT framebuffer on the first frame\n"
                         "  --lut-snapshot PATH  Save first LUT frame as BMP outside the installation\n"
                         "  --frames N      Exit after N rendered frames\n";
            return 0;
        } else if (arg == "--probe") {
            probe = true;
        } else if (arg == "--lut") {
            show_lut = true;
        } else if (arg == "--verify-lut-render") {
            show_lut = verify_lut_render = true;
        } else if (arg == "--lut-snapshot" && i + 1 < argc) {
            snapshot = argv[++i];
            show_lut = true;
        } else if (arg == "--assets" && i + 1 < argc) {
            assets = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            std::string_view value(argv[++i]);
            const auto result = std::from_chars(value.data(), value.data() + value.size(), frame_limit);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || frame_limit <= 0) {
                std::cerr << "--frames needs a positive integer\n";
                return 2;
            }
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n';
            return 2;
        }
    }
    if (assets.empty()) {
        std::cerr << "Provide --assets PATH to the PC installation\n";
        return 2;
    }
    std::array<char, 4> magic{};
    std::ifstream input(assets / "cache" / "boot.pak", std::ios::binary);
    input.read(magic.data(), magic.size());
    if (!input || std::string_view(magic.data(), magic.size()) != "2KAP") {
        std::cerr << "Expected PC cache/boot.pak with 2KAP signature\n";
        return 1;
    }
    std::cout << "Read-only asset probe: PC boot.pak signature verified\n"
              << "Host pointer width: " << sizeof(void*) * 8 << " bits; SDL " << SDL_GetVersion() << '\n';
    std::optional<blur::assets::Atlas> atlas;
    if (show_lut) {
        try {
            std::ifstream archive_input(assets / "cache" / "lut_rampak.pak", std::ios::binary);
            const auto archive = blur::assets::read_index(archive_input);
            constexpr std::string_view name = "\\graphics\\textures\\postprocess\\showroom_lut_02.dds";
            std::optional<std::size_t> index;
            for (std::size_t i = 0; i < archive.entries.size(); ++i)
                if (archive.entries[i].name == name) {
                    if (index) throw std::runtime_error("Ambiguous LUT name in archive");
                    index = i;
                }
            if (!index) throw std::runtime_error("LUT resource not found");
            atlas = blur::assets::dds_volume_atlas(blur::assets::read_resource(archive_input, archive, *index));
            std::cout << "Native LUT: " << atlas->volume_width << 'x' << atlas->volume_height << 'x'
                      << atlas->volume_depth << "; atlas " << atlas->width << 'x' << atlas->height << '\n';
            if (!snapshot.empty()) {
                snapshot = fs::weakly_canonical(snapshot);
                const auto root = fs::weakly_canonical(assets);
                const auto rel = snapshot.lexically_relative(root);
                if (rel.empty() || *rel.begin() != "..") throw std::runtime_error("Snapshot must be outside the reference installation");
            }
        } catch (const std::exception& e) { std::cerr << "LUT load failed: " << e.what() << '\n'; return 1; }
    }
    if (probe) return 0;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    auto* window = SDL_CreateWindow("Blur Native | research bootstrap", 1100, 680,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
#ifdef __APPLE__
    const char* backend = "metal";
#else
    const char* backend = nullptr;
#endif
    auto* renderer = SDL_CreateRenderer(window, backend);
    if (!renderer) {
        std::cerr << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::cout << "Renderer: " << SDL_GetRendererName(renderer) << '\n';
    SDL_Texture* lut_texture = nullptr;
    if (atlas) {
        lut_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                        static_cast<int>(atlas->width), static_cast<int>(atlas->height));
        if (!lut_texture || !SDL_UpdateTexture(lut_texture, nullptr, atlas->rgba.data(), static_cast<int>(atlas->width * 4)) ||
            !SDL_SetTextureScaleMode(lut_texture, SDL_SCALEMODE_NEAREST)) {
            std::cerr << SDL_GetError() << '\n';
            SDL_DestroyTexture(lut_texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
            return 1;
        }
    }
    SDL_SetRenderVSync(renderer, 1);
    bool running = true;
    bool render_ok = true;
    int frames = 0;
    while (running && (!frame_limit || frames < frame_limit)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) running = false;
        }
        if (!running) break;
        render_ok = SDL_SetRenderDrawColor(renderer, 9, 13, 25, 255) && SDL_RenderClear(renderer);
        render_ok = render_ok && SDL_SetRenderScale(renderer, 2.0f, 2.0f);
        render_ok = render_ok && SDL_SetRenderDrawColor(renderer, 79, 226, 241, 255);
        render_ok = render_ok && SDL_RenderDebugText(renderer, 28, 30, "BLUR / NATIVE");
        render_ok = render_ok && SDL_SetRenderDrawColor(renderer, 218, 224, 236, 255);
        render_ok = render_ok && SDL_RenderDebugText(renderer, 28, 65, "SDL3 platform bootstrap");
        render_ok = render_ok && SDL_RenderDebugText(renderer, 28, 88,
                    atlas ? "Native archive decode / DDS volume slice atlas" : "PC asset signature verified");
        render_ok = render_ok && SDL_RenderDebugText(renderer, 28, 122, "Game reconstruction: not implemented");
        render_ok = render_ok && SDL_RenderDebugText(renderer, 28, 145, "Amax / Discord: research pending");
        if (atlas) {
            render_ok = render_ok && SDL_SetRenderScale(renderer, 1.0f, 1.0f);
            const SDL_FRect destination{56, 360, float(atlas->width * 2), float(atlas->height * 2)};
            render_ok = render_ok && SDL_RenderTexture(renderer, lut_texture, nullptr, &destination);
            if (render_ok && frames == 0 && verify_lut_render) render_ok = verify_lut(renderer, *atlas);
            if (render_ok && frames == 0 && !snapshot.empty()) {
                auto* surface = SDL_RenderReadPixels(renderer, nullptr);
                render_ok = surface && SDL_SaveBMP(surface, snapshot.c_str());
                SDL_DestroySurface(surface);
            }
        }
        render_ok = render_ok && SDL_RenderPresent(renderer);
        if (!render_ok) { std::cerr << SDL_GetError() << '\n'; break; }
        ++frames;
        SDL_Delay(1);
    }
    std::cout << "Presented " << frames << " frames\n";
    SDL_DestroyTexture(lut_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return render_ok && (!frame_limit || frames == frame_limit) ? 0 : 1;
}
