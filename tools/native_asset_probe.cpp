#include "assets/assets.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
using namespace blur::assets;
std::uint32_t number(const char* value) {
    const std::string_view v(value); std::uint32_t n{};
    const auto r = std::from_chars(v.data(), v.data()+v.size(), n);
    if (r.ec != std::errc{} || r.ptr != v.data()+v.size()) throw std::runtime_error("Invalid number");
    return n;
}
int main(int argc, char** argv) {
    try {
        Bytes result;
        const std::string_view mode = argc > 1 ? argv[1] : "";
        if (mode == "--resource" && argc == 4) {
            std::ifstream input(argv[2], std::ios::binary);
            const auto archive = read_index(input);
            result = read_resource(input, archive, number(argv[3]));
        } else if ((mode == "--codec" && argc == 5) || (mode == "--dds" && argc == 2)) {
            Bytes input;
            char c;
            while (std::cin.get(c)) {
                if (input.size() >= max_decoded) throw std::runtime_error("Input exceeds bounds");
                input.push_back(static_cast<std::uint8_t>(c));
            }
            if (mode == "--dds") result = dds_volume_atlas(input).rgba;
            else {
                const auto selector = number(argv[2]);
                if (selector > 255) throw std::runtime_error("Invalid selector");
                result = decode_payload(static_cast<std::uint8_t>(selector), input, number(argv[3]), number(argv[4]));
            }
        } else throw std::runtime_error("Usage: blur-asset-probe --resource ARCHIVE INDEX | --codec SELECTOR SIZE CHECKSUM | --dds (binary stdin/stdout)");
        std::cout.write(reinterpret_cast<const char*>(result.data()), static_cast<std::streamsize>(result.size()));
        return std::cout ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
