// Example native host integration. Linked only to dlopen and the C ABI header;
// no Plume/SPIRV-Cross C++ type or guest pointer crosses this boundary.
#include "render/plume_bridge.h"
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fs=std::filesystem;
namespace {
void check(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
struct Session {
    void* library=nullptr;
    BlurPlumeApi api{};
    BlurPlumeContext* context=nullptr;
    ~Session() {
        if(context && api.destroy && api.destroy(context,nullptr)!=BLUR_PLUME_OK)return;
        if(library)dlclose(library);
    }
};
std::string sha(const std::vector<uint8_t>& data) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.data(),CC_LONG(data.size()),digest);
    std::ostringstream out;for(auto b:digest)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return out.str();
}
}
int main(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"Native Plume bridge host (no guest CPU/game execution)\n"
                   "Usage: blur-plume-bridge-host LIBRARY PACKET.bpg NEW_OUTPUT_DIRECTORY\n";return 0;
    }
    if(argc!=4) {std::cerr<<"Expected LIBRARY PACKET.bpg NEW_OUTPUT_DIRECTORY\n";return 2;}
    try {
        const fs::path path=argv[2],output=argv[3];
        check(!fs::exists(output),"Output exists; refusing to overwrite evidence");
        const auto size=fs::file_size(path);check(size>0 && size<=(32<<20),"Packet size is outside bounds");
        std::vector<uint8_t> bytes(size);std::ifstream input(path,std::ios::binary);
        input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));check(bool(input),"Cannot read packet");
        Session session;session.library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
        if(!session.library) {const char* error=dlerror();throw std::runtime_error(error?error:"Bridge load failed");}
        auto get=reinterpret_cast<BlurPlumeGetApi>(dlsym(session.library,"blur_plume_get_api"));check(get,"Bridge API entry missing");
        check(get(1,sizeof(session.api),&session.api)==BLUR_PLUME_OK,"Bridge ABI mismatch");
        check(session.api.struct_size==sizeof(session.api) && session.api.abi_version==1 &&
              (session.api.capabilities&(BLUR_PLUME_CAP_ZERO_INITIALIZED_UI|BLUR_PLUME_CAP_PERSISTENT_TARGET))==
                  (BLUR_PLUME_CAP_ZERO_INITIALIZED_UI|BLUR_PLUME_CAP_PERSISTENT_TARGET) &&
              session.api.create && session.api.probe &&
              session.api.submit && session.api.stats && session.api.destroy,"Incomplete/unknown bridge API");
        BlurPlumeError error{};
        auto status=session.api.probe(bytes.data(),bytes.size(),&error);check(status==BLUR_PLUME_OK,error.message);
        status=session.api.create(&session.context,&error);check(status==BLUR_PLUME_OK,error.message);
        BlurPlumeSubmission submit{sizeof(submit),BLUR_PLUME_LOAD_CLEAR_ZERO,1,1,bytes.data(),bytes.size()};
        BlurPlumeOutput view{};std::vector<uint8_t> copied;
        std::string device;uint32_t width=0,height=0,samples=0;
        for(unsigned i=0;i<2;++i) {
            submit.draw_id=i+1;
            status=session.api.submit(session.context,&submit,sizeof(view),&view,&error);check(status==BLUR_PLUME_OK,error.message);
            check(view.struct_size==sizeof(view) && view.frame_id==1 && view.draw_id==i+1 && view.rgba_samples &&
                  view.width && view.height && (view.samples==1 || view.samples==4) &&
                  view.byte_count==uint64_t(view.width)*view.height*view.samples*4,"Invalid successful output view");
            std::vector<uint8_t> next(view.rgba_samples,view.rgba_samples+view.byte_count);
            if(i)check(next==copied,"Repeated native submission differs");
            copied=std::move(next);device=view.device_name;width=view.width;height=view.height;samples=view.samples;
        }
        // The caller has copied the borrowed view before a rejected call.
        status=session.api.submit(session.context,&submit,sizeof(view),&view,&error);
        check(status==BLUR_PLUME_INVALID_INPUT && !view.rgba_samples && !view.byte_count,"Stale identity accepted or stale output exposed");
        BlurPlumeStats stats{};status=session.api.stats(session.context,sizeof(stats),&stats,&error);
        check(status==BLUR_PLUME_OK && stats.accepted==2 && stats.rejected==1 && !stats.gpu_failures,"Bridge coverage counters differ");
        status=session.api.destroy(session.context,&error);check(status==BLUR_PLUME_OK,error.message);session.context=nullptr;
        // The independent caller copy must remain valid after context shutdown.
        fs::create_directories(output);
        std::ofstream raw(output/"samples.rgba8",std::ios::binary);
        raw.write(reinterpret_cast<const char*>(copied.data()),std::streamsize(copied.size()));check(bool(raw),"Readback write failed");
        std::ofstream metadata(output/"result.txt");
        metadata<<"width="<<width<<" height="<<height<<" samples="<<samples<<" bytes="<<copied.size()
                <<"\ndevice="<<device<<"\npacket_sha256="<<sha(bytes)<<"\ndiagnostic=none\n";
        check(bool(metadata),"Metadata write failed");
        std::ofstream receipt(output/"bridge.txt");
        receipt<<"abi=1 accepted="<<stats.accepted<<" rejected="<<stats.rejected<<" gpu_failures="<<stats.gpu_failures
               <<" last_frame_id="<<stats.last_frame_id<<" last_draw_id="<<stats.last_draw_id
               <<"\nborrowed_view_copied=1 context_destroyed=1 repeated_output_exact=1 stale_submission_rejected=1\n";
        check(bool(receipt),"Receipt write failed");
        std::cout<<"Dynamic Plume bridge: 2 identical GPU submissions; stale submission rejected; context destroyed; "
                 <<copied.size()<<" caller-owned bytes saved on "<<device<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
