#include "render/plume_bridge.h"
#include <dlfcn.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void check(bool ok,const char* text) {if(!ok)throw std::runtime_error(text);}
void word(std::vector<uint8_t>& bytes,uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));
}
void blob(std::vector<uint8_t>& bytes,unsigned size) {word(bytes,size);bytes.insert(bytes.end(),size,0);}
std::vector<uint8_t> synthetic() {
    std::vector<uint8_t> bytes;
    for(auto n:{0x31475042u,1u,8u,8u,8u,8u,0u,0u,8u,8u,4u,0u})word(bytes,n);
    blob(bytes,24);
    for(unsigned i=0;i<2;++i) {word(bytes,20);for(auto n:{0x07230203u,0x10000u,0u,1u,0u})word(bytes,n);}
    for(unsigned i=0;i<5;++i)blob(bytes,16);
    word(bytes,3);for(unsigned i=0;i<3;++i)word(bytes,0);
    word(bytes,1);word(bytes,4);word(bytes,4);blob(bytes,8);
    return bytes;
}
}
int main(int argc,char** argv) {
    void* library=nullptr; BlurPlumeContext* context=nullptr; BlurPlumeApi api{};
    try {
        check(argc==2,"Expected bridge library path");
        library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);check(library,dlerror()?"Bridge load failed":"Bridge unavailable");
        auto get=reinterpret_cast<BlurPlumeGetApi>(dlsym(library,"blur_plume_get_api"));check(get,"Missing ABI entry");
        check(get(99,sizeof(api),&api)==BLUR_PLUME_ABI_MISMATCH && !api.create,"Wrong ABI was accepted");
        check(get(1,sizeof(api)-1,&api)==BLUR_PLUME_INVALID_INPUT,"Short ABI layout was accepted");
        check(get(1,sizeof(api),&api)==BLUR_PLUME_OK && api.abi_version==1 && api.struct_size==sizeof(api),"ABI negotiation failed");
        check(api.capabilities==(BLUR_PLUME_CAP_ZERO_INITIALIZED_UI|BLUR_PLUME_CAP_PERSISTENT_TARGET),"Unexpected capabilities");
        BlurPlumeError error{};
        check(api.create(nullptr,&error)==BLUR_PLUME_INVALID_INPUT,"Null context output accepted");
        check(api.create(&context,&error)==BLUR_PLUME_OK && context,"Lazy context creation failed");
        check(api.probe(nullptr,0,&error)==BLUR_PLUME_INVALID_INPUT && error.code==BLUR_PLUME_INVALID_INPUT,"Invalid span accepted");
        const auto bytes=synthetic();
        check(api.probe(bytes.data(),bytes.size(),&error)==BLUR_PLUME_UNSUPPORTED && error.message[0],"Unknown shader contract accepted");
        BlurPlumeSubmission submit{sizeof(submit),BLUR_PLUME_LOAD_CLEAR_ZERO,1,1,bytes.data(),bytes.size()};
        BlurPlumeOutput output{};std::memset(&output,0xff,sizeof(output));
        check(api.submit(context,&submit,sizeof(output),&output,&error)==BLUR_PLUME_UNSUPPORTED,"Unsupported draw accepted");
        check(!output.rgba_samples && output.byte_count==0,"Failure exposed a successful output view");
        submit.load_action=BLUR_PLUME_LOAD_PRESERVE;
        check(api.submit(context,&submit,sizeof(output),&output,&error)==BLUR_PLUME_INVALID_STATE,"Uninitialized preserve load was accepted");
        submit.load_action=2;
        check(api.submit(context,&submit,sizeof(output),&output,&error)==BLUR_PLUME_UNSUPPORTED,"Unknown load action was accepted");
        submit.load_action=0;submit.struct_size=0;
        check(api.submit(context,&submit,sizeof(output),&output,&error)==BLUR_PLUME_INVALID_INPUT,"Short submission accepted");
        submit.struct_size=sizeof(submit);
        BlurPlumeStats counters{};
        check(api.stats(context,sizeof(counters),&counters,&error)==BLUR_PLUME_OK && counters.accepted==0 && counters.rejected==4 && counters.gpu_failures==0,"Rejection counters differ");
        unsigned wrong_thread_results=0;
        std::thread other([&] {
            BlurPlumeError e{};BlurPlumeOutput o{};BlurPlumeStats s{};
            wrong_thread_results+=(api.submit(context,&submit,sizeof(o),&o,&e)==BLUR_PLUME_WRONG_THREAD);
            wrong_thread_results+=(api.stats(context,sizeof(s),&s,&e)==BLUR_PLUME_WRONG_THREAD);
            wrong_thread_results+=(api.destroy(context,&e)==BLUR_PLUME_WRONG_THREAD);
        });other.join();
        check(wrong_thread_results==3,"Thread ownership was not enforced");
        check(api.stats(context,sizeof(counters),&counters,&error)==BLUR_PLUME_OK && counters.rejected==4,"Foreign thread mutated owner counters");
        check(api.destroy(context,&error)==BLUR_PLUME_OK,"Context shutdown failed");context=nullptr;
        check(api.destroy(nullptr,&error)==BLUR_PLUME_INVALID_INPUT,"Null destroy accepted");

        BlurPlumeApiV2 api_v2{};
        check(get(2,sizeof(api_v2)-1,&api_v2)==BLUR_PLUME_INVALID_INPUT,"Short ABI V2 layout was accepted");
        check(get(2,sizeof(api_v2),&api_v2)==BLUR_PLUME_OK && api_v2.abi_version==2 && api_v2.struct_size==sizeof(api_v2),
              "ABI V2 negotiation failed");
        check(api_v2.capabilities==(BLUR_PLUME_CAP_ZERO_INITIALIZED_UI|BLUR_PLUME_CAP_PERSISTENT_TARGET|BLUR_PLUME_CAP_COMPOSITE),
              "Unexpected ABI V2 capabilities");
        check(api_v2.probe_composite && api_v2.submit_composite,"ABI V2 composite functions missing");
        check(api_v2.probe_composite(nullptr,0,&error)==BLUR_PLUME_INVALID_INPUT,"Null composite span accepted");
        check(api_v2.create(&context,&error)==BLUR_PLUME_OK && context,"ABI V2 context creation failed");
        BlurPlumeCompositeSubmission comp_submit{sizeof(comp_submit),BLUR_PLUME_LOAD_CLEAR_ZERO,1,1,bytes.data(),bytes.size()};
        BlurPlumeCompositeOutput comp_output{};
        check(api_v2.submit_composite(context,&comp_submit,sizeof(comp_output),&comp_output,&error)==BLUR_PLUME_INTERNAL_FAILURE ||
              api_v2.submit_composite(context,&comp_submit,sizeof(comp_output),&comp_output,&error)==BLUR_PLUME_UNSUPPORTED ||
              api_v2.submit_composite(context,&comp_submit,sizeof(comp_output),&comp_output,&error)==BLUR_PLUME_INVALID_INPUT,
              "Invalid composite submission accepted");
        check(api_v2.destroy(context,&error)==BLUR_PLUME_OK,"ABI V2 context shutdown failed");context=nullptr;

        dlclose(library);library=nullptr;
        std::cout<<"Dynamic C bridge ABI, lazy CPU eligibility, rejection, ownership and shutdown: passed\n";
        return 0;
    } catch(const std::exception& error) {
        if(context && api.destroy)api.destroy(context,nullptr);
        if(library)dlclose(library);
        std::cerr<<error.what()<<'\n';return 1;
    }
}
