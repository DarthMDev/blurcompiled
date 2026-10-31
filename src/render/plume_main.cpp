#include "render/renderer.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

using namespace blur::render;
namespace {
unsigned number(std::string_view text) {
    unsigned result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !result)
        throw std::invalid_argument("Expected a positive integer");
    return result;
}
struct Window {
    SDL_Window* window = nullptr;
    SDL_MetalView view = nullptr;
    Window() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("Blur | Plume native renderer validation",960,540,
            SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window) view = SDL_Metal_CreateView(window);
        if (!window || !view) {
            auto error = std::string(SDL_GetError());
            if (window) SDL_DestroyWindow(window);
            SDL_Quit();
            throw std::runtime_error(error);
        }
    }
    ~Window() { SDL_Metal_DestroyView(view); SDL_DestroyWindow(window); SDL_Quit(); }
};
}
int main(int argc, char** argv) {
    try {
        unsigned limit = 0, width = 256, height = 256;
        std::optional<std::string> input;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg == "--help") {
                std::cout << "Blur Plume/Metal native renderer validation host (no guest execution)\n"
                             "  --frames N  Exit after N presented frames (default: user closes)\n"
                             "  --rgba PATH --width N --height N  Display a raw host RGBA8 image\n"
                             "Default input is a synthetic color grid. Escape exits.\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::invalid_argument("Incomplete argument");
            if (arg == "--frames") limit = number(argv[++i]);
            else if (arg == "--width") width = number(argv[++i]);
            else if (arg == "--height") height = number(argv[++i]);
            else if (arg == "--rgba") input = argv[++i];
            else throw std::invalid_argument("Unknown argument");
        }
        std::vector<std::uint8_t> rgba(rgba_size(width,height));
        if (input) {
            std::ifstream file(*input,std::ios::binary);
            file.read(reinterpret_cast<char*>(rgba.data()),std::streamsize(rgba.size()));
            if (!file || file.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Expected exactly width*height*4 RGBA bytes");
        } else {
            for (unsigned y=0; y<height; ++y) for (unsigned x=0; x<width; ++x) {
                const auto i=(std::size_t(y)*width+x)*4;
                rgba[i]=std::uint8_t(x*255/std::max(1u,width-1));
                rgba[i+1]=std::uint8_t(y*255/std::max(1u,height-1));
                rgba[i+2]=((x/16)^(y/16))&1 ? 220 : 30; rgba[i+3]=255;
            }
        }
        Window window;
        // Renderer and swapchain are destroyed before the native layer/window.
        Renderer renderer;
        renderer.attach_window(SDL_GetPointerProperty(SDL_GetWindowProperties(window.window),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr),
                               SDL_Metal_GetLayer(window.view));
        std::cout << "Native renderer: Plume / Metal / " << renderer.device_name() << '\n';
        const auto texture=renderer.create_texture(width,height,rgba);
        const auto vertices=quad();
        const Draw draw{texture,vertices,quad_indices};
        unsigned frames=0;
        bool running=true;
        while (running && (!limit || frames<limit)) {
            SDL_Event event;
            while (SDL_PollEvent(&event))
                if (event.type==SDL_EVENT_QUIT || (event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE)) running=false;
            if (!running) break;
            // A minimized window has no drawable. Keep pumping events until it
            // can present again; do not ask CAMetalLayer for a zero-size image.
            if (SDL_GetWindowFlags(window.window) & SDL_WINDOW_MINIMIZED) { SDL_Delay(20); continue; }
            if (!renderer.present({960,540,{0,0,0,1},{&draw,1}})) throw std::runtime_error("Plume presentation failed");
            ++frames;
        }
        std::cout << "Presented " << frames << " native Plume frames\n";
        renderer.detach_window();
        return limit && frames != limit ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
