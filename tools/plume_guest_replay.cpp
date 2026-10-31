#include "render/guest_draw.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <CommonCrypto/CommonDigest.h>
#include <iomanip>
#include <sstream>

namespace fs=std::filesystem;
int main(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--self-test") {
        try {
            blur::render::guest::Renderer renderer; renderer.verify_synthetic_readback();
            std::cout<<"Plume 1x/4x synthetic readback: all RGBA samples exact on "<<renderer.device_name()<<'\n'; return 0;
        } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
    }
    if(argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"Plume guest UI replay (no CPU/game execution)\nUsage: blur-plume-guest PACKET.bpg NEW_OUTPUT_DIRECTORY [--diagnostic-no-cull|--diagnostic-clear-red]\n"
                   "       blur-plume-guest --self-test  Synthetic 1x/4x GPU readback regression\n";
        return 0;
    }
    if(argc<3 || argc>4 || (argc==4 && std::string_view(argv[3])!="--diagnostic-no-cull" && std::string_view(argv[3])!="--diagnostic-clear-red")) {
        std::cerr<<"Expected PACKET.bpg NEW_OUTPUT_DIRECTORY [--diagnostic-no-cull]\n"; return 2;
    }
    try {
        const fs::path input=argv[1],output=argv[2];
        const auto size=fs::file_size(input);
        if(size>(32<<20)) throw std::runtime_error("Packet exceeds 32 MiB");
        std::ifstream file(input,std::ios::binary); std::vector<std::uint8_t> bytes(size);
        file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));
        if(!file) throw std::runtime_error("Cannot read packet");
        const auto packet=blur::render::guest::decode(bytes);
        if(fs::exists(output)) throw std::runtime_error("Output directory already exists; refusing to overwrite evidence");
        blur::render::guest::Renderer renderer;
        std::cout<<"Guest UI adapter: Plume / Metal / "<<renderer.device_name()<<'\n';
        auto diagnostic=blur::render::guest::Diagnostic::none;
        if(argc==4) diagnostic=std::string_view(argv[3])=="--diagnostic-clear-red"?
            blur::render::guest::Diagnostic::clear_red:blur::render::guest::Diagnostic::no_cull;
        const auto result=renderer.draw(packet,diagnostic);
        fs::create_directories(output);
        auto save=[&](const char* name,const void* data,std::size_t count) {
            std::ofstream out(output/name,std::ios::binary); out.write(static_cast<const char*>(data),std::streamsize(count));
            if(!out) throw std::runtime_error("Output write failed");
        };
        save("samples.rgba8",result.rgba_samples.data(),result.rgba_samples.size());
        save("vertex.metal",result.vertex_msl.data(),result.vertex_msl.size());
        save("pixel.metal",result.pixel_msl.data(),result.pixel_msl.size());
        std::ofstream metadata(output/"result.txt");
        unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(bytes.data(),CC_LONG(bytes.size()),digest);
        std::ostringstream packet_sha;
        for(auto b:digest) packet_sha<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
        metadata<<"width="<<result.width<<" height="<<result.height<<" samples="<<result.samples
                <<" bytes="<<result.rgba_samples.size()<<"\ndevice="<<renderer.device_name()
                <<"\npacket_sha256="<<packet_sha.str()<<"\ndiagnostic="<<(argc==4?argv[3]:"none")<<'\n';
        if(!metadata) throw std::runtime_error("Metadata write failed");
        std::cout<<"Rendered indexed guest UI pass: "<<packet.indices.size()<<" indices; "<<result.width<<'x'<<result.height
                 <<"; "<<result.samples<<" samples; "<<result.rgba_samples.size()<<" GPU readback bytes\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
