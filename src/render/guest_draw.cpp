#include "render/guest_draw.hpp"
#include "plume_render_interface.h"
#include <spirv_cross/spirv_msl.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace plume { std::unique_ptr<RenderInterface> CreateMetalInterface(); }
namespace blur::render::guest {
using namespace plume;
namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
std::uint32_t word(std::span<const std::uint8_t> bytes,std::size_t offset) {
    if(offset+4>bytes.size()) throw std::invalid_argument("Short UI constant block");
    return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset+1])<<8 |
           std::uint32_t(bytes[offset+2])<<16 | std::uint32_t(bytes[offset+3])<<24;
}
std::string translate(const std::vector<std::uint32_t>& words,spv::ExecutionModel stage,bool composite=false) {
    spirv_cross::CompilerMSL compiler(words);
    auto options=compiler.get_msl_options();
    options.msl_version=spirv_cross::CompilerMSL::Options::make_msl_version(3,0);
    options.argument_buffers=true;
    options.argument_buffers_tier=spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2;
    options.enable_decoration_binding=true;
    options.force_active_argument_buffer_resources=true;
    options.pad_argument_buffer_resources=true;
    compiler.set_msl_options(options);
    auto common=compiler.get_common_options();
    // Vulkan's positive viewport height and Metal have opposite clip Y axes.
    common.vertex.flip_vert_y=true;
    compiler.set_common_options(common);
    auto bind=[&](std::uint32_t set,std::uint32_t binding,spirv_cross::SPIRType::BaseType type) {
        spirv_cross::MSLResourceBinding b;
        b.stage=stage; b.desc_set=set; b.binding=binding; b.basetype=type; b.count=1;
        b.msl_buffer=b.msl_texture=b.msl_sampler=binding;
        compiler.add_msl_resource_binding(b);
    };
    // Padding describes the pointed-to scalar type, not the UBO struct type.
    // Declared resources keep their original SPIR-V type; holes become pointers.
    bind(0,0,spirv_cross::SPIRType::UInt);
    for(unsigned i=0;i<5;++i) bind(1,i,spirv_cross::SPIRType::UInt);
    if(composite) {
        for(unsigned i=0;i<4;++i) bind(3,i,spirv_cross::SPIRType::Image);
        bind(3,4,spirv_cross::SPIRType::Sampler); bind(3,5,spirv_cross::SPIRType::Sampler);
    } else {
        bind(3,0,spirv_cross::SPIRType::Image); bind(3,1,spirv_cross::SPIRType::Image);
        bind(3,2,spirv_cross::SPIRType::Sampler);
    }
    compiler.set_entry_point("main",stage);
    return compiler.compile();
}
constexpr const char* read_samples=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct R1 { texture2d<float,access::read> image [[id(0)]]; device uint* bytes [[id(1)]]; };
struct R4 { texture2d_ms<float,access::read> image [[id(0)]]; device uint* bytes [[id(1)]]; };
struct R1F16 { texture2d<half,access::read> image [[id(0)]]; device ushort4* pixels [[id(1)]]; };
uint rgba(float4 color) { uint4 p=uint4(round(clamp(color,0.0,1.0)*255.0)); return p.x|(p.y<<8)|(p.z<<16)|(p.w<<24); }
kernel void samples1(constant R1& r [[buffer(0)]],uint2 xy [[thread_position_in_grid]]) {
 if(xy.x<r.image.get_width() && xy.y<r.image.get_height()) r.bytes[xy.y*r.image.get_width()+xy.x]=rgba(r.image.read(xy));
}
kernel void samples4(constant R4& r [[buffer(0)]],uint2 xy [[thread_position_in_grid]]) {
 if(xy.x<r.image.get_width() && xy.y<r.image.get_height()) for(uint s=0;s<4;++s)
  r.bytes[(xy.y*r.image.get_width()+xy.x)*4+s]=rgba(r.image.read(xy,s));
}
kernel void samples1_f16(constant R1F16& r [[buffer(0)]],uint2 xy [[thread_position_in_grid]]) {
 if(xy.x<r.image.get_width() && xy.y<r.image.get_height())
  r.pixels[xy.y*r.image.get_width()+xy.x]=as_type<ushort4>(r.image.read(xy));
}
)MSL";
}
struct Renderer::Impl {
    std::unique_ptr<RenderInterface> interface;
    std::unique_ptr<RenderDevice> device;
    std::unique_ptr<RenderCommandQueue> queue;
    std::unique_ptr<RenderCommandList> commands;
    std::unique_ptr<RenderCommandFence> fence;
    std::unique_ptr<RenderBuffer> shared;
    std::size_t shared_bytes=0;
    std::unique_ptr<RenderTexture> target;
    std::unique_ptr<RenderFramebuffer> framebuffer;
    std::uint32_t target_width=0,target_height=0,target_samples=0;
    RenderFormat target_format=RenderFormat::UNKNOWN;
    std::string name;
    Impl() {
        interface=CreateMetalInterface();
        require(interface && !interface->getDeviceNames().empty(),"Plume guest adapter: no Metal device");
        device=interface->createDevice(); require(bool(device),"Plume guest adapter: device unavailable");
        name=device->getDescription().name;
        queue=device->createCommandQueue(RenderCommandListType::DIRECT); require(bool(queue),"Plume guest queue unavailable");
        commands=queue->createCommandList(); fence=device->createCommandFence();
        require(commands && fence,"Plume guest command resources unavailable");
    }
    std::unique_ptr<RenderBuffer> upload(std::span<const std::uint8_t> bytes,RenderBufferFlags flags) {
        auto buffer=device->createBuffer(RenderBufferDesc::UploadBuffer(bytes.size(),flags));
        require(bool(buffer),"Plume guest upload allocation failed");
        void* mapped=buffer->map(); require(mapped,"Plume guest map failed");
        std::memcpy(mapped,bytes.data(),bytes.size()); buffer->unmap(); return buffer;
    }
    void submit() {
        commands->end(); queue->executeCommandLists(commands.get(),fence.get()); queue->waitForCommandFence(fence.get());
    }
    std::vector<std::uint8_t> read_target(RenderTexture* target,std::uint32_t width,std::uint32_t height,std::uint32_t samples) {
        auto& d=*device;
        const auto output_bytes=std::uint64_t(width)*height*samples*4;
        auto gpu_output=d.createBuffer(RenderBufferDesc::DefaultBuffer(output_bytes,RenderBufferFlag::STORAGE|RenderBufferFlag::UNORDERED_ACCESS));
        auto readback=d.createBuffer(RenderBufferDesc::ReadbackBuffer(output_bytes)); require(gpu_output && readback,"UI output allocation failed");
        const std::array<RenderDescriptorRange,2> ranges{{{RenderDescriptorRangeType::TEXTURE,0,1},
            {RenderDescriptorRangeType::READ_WRITE_BYTE_ADDRESS_BUFFER,1,1}}};
        const RenderDescriptorSetDesc set_desc{ranges.data(),2};
        RenderPipelineLayoutDesc desc;
        desc.descriptorSetDescs=&set_desc; desc.descriptorSetDescsCount=1;
        auto layout=d.createPipelineLayout(desc); auto set=d.createDescriptorSet(set_desc);
        auto shader=d.createShader(read_samples,std::strlen(read_samples),samples==4?"samples4":"samples1",RenderShaderFormat::MSL);
        require(layout && set && shader,"UI sample readback resources failed");
        auto pipeline=d.createComputePipeline(RenderComputePipelineDesc(layout.get(),shader.get(),8,8,1));
        require(bool(pipeline),"UI readback pipeline failed");
        set->setTexture(0,target,RenderTextureLayout::SHADER_READ); set->setBuffer(1,gpu_output.get(),output_bytes);
        commands->begin();
        // Both accesses matter. Without marking the indirect output buffer as
        // a compute writer, the later blit can copy before the kernel finishes.
        commands->barriers(RenderBarrierStage::COMPUTE,RenderBufferBarrier(gpu_output.get(),RenderBufferAccess::WRITE),
                           RenderTextureBarrier(target,RenderTextureLayout::SHADER_READ));
        commands->setComputePipelineLayout(layout.get()); commands->setPipeline(pipeline.get());
        commands->setComputeDescriptorSet(set.get(),0); commands->dispatch((width+7)/8,(height+7)/8,1);
        commands->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(gpu_output.get(),RenderBufferAccess::READ));
        commands->copyBuffer(readback.get(),gpu_output.get()); submit();
        std::vector<std::uint8_t> result(output_bytes);
        auto* mapped=readback->map(); require(mapped,"UI output mapping failed");
        std::memcpy(result.data(),mapped,output_bytes); readback->unmap(); return result;
    }
    std::vector<std::uint8_t> read_target_f16(RenderTexture* target,std::uint32_t width,std::uint32_t height) {
        auto& d=*device;
        const auto output_bytes=std::uint64_t(width)*height*8;
        auto gpu_output=d.createBuffer(RenderBufferDesc::DefaultBuffer(output_bytes,RenderBufferFlag::STORAGE|RenderBufferFlag::UNORDERED_ACCESS));
        auto readback=d.createBuffer(RenderBufferDesc::ReadbackBuffer(output_bytes)); require(gpu_output && readback,"RGBA16F output allocation failed");
        const std::array<RenderDescriptorRange,2> ranges{{{RenderDescriptorRangeType::TEXTURE,0,1},
            {RenderDescriptorRangeType::READ_WRITE_BYTE_ADDRESS_BUFFER,1,1}}};
        const RenderDescriptorSetDesc set_desc{ranges.data(),2};
        RenderPipelineLayoutDesc desc;
        desc.descriptorSetDescs=&set_desc; desc.descriptorSetDescsCount=1;
        auto layout=d.createPipelineLayout(desc); auto set=d.createDescriptorSet(set_desc);
        auto shader=d.createShader(read_samples,std::strlen(read_samples),"samples1_f16",RenderShaderFormat::MSL);
        require(layout && set && shader,"RGBA16F sample readback resources failed");
        auto pipeline=d.createComputePipeline(RenderComputePipelineDesc(layout.get(),shader.get(),8,8,1));
        require(bool(pipeline),"RGBA16F readback pipeline failed");
        set->setTexture(0,target,RenderTextureLayout::SHADER_READ); set->setBuffer(1,gpu_output.get(),output_bytes);
        commands->begin();
        commands->barriers(RenderBarrierStage::COMPUTE,RenderBufferBarrier(gpu_output.get(),RenderBufferAccess::WRITE),
                           RenderTextureBarrier(target,RenderTextureLayout::SHADER_READ));
        commands->setComputePipelineLayout(layout.get()); commands->setPipeline(pipeline.get());
        commands->setComputeDescriptorSet(set.get(),0); commands->dispatch((width+7)/8,(height+7)/8,1);
        commands->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(gpu_output.get(),RenderBufferAccess::READ));
        commands->copyBuffer(readback.get(),gpu_output.get()); submit();
        std::vector<std::uint8_t> result(output_bytes);
        auto* mapped=readback->map(); require(mapped,"RGBA16F output mapping failed");
        std::memcpy(result.data(),mapped,output_bytes); readback->unmap(); return result;
    }
};
Renderer::Renderer():impl_(std::make_unique<Impl>()){}
Renderer::~Renderer()=default;
const std::string& Renderer::device_name() const {return impl_->name;}
void Renderer::verify_synthetic_readback() {
    for(std::uint32_t samples:{1u,4u}) {
        auto target=impl_->device->createTexture(RenderTextureDesc::ColorTarget(7,5,RenderFormat::R8G8B8A8_UNORM,RenderMultisampling(samples)));
        require(bool(target),"Synthetic MSAA allocation failed");
        const RenderTexture* color=target.get(); RenderFramebufferDesc desc;
        desc.colorAttachments=&color; desc.colorAttachmentsCount=1;
        auto fb=impl_->device->createFramebuffer(desc); require(bool(fb),"Synthetic framebuffer failed");
        impl_->commands->begin();
        impl_->commands->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(target.get(),RenderTextureLayout::COLOR_WRITE));
        impl_->commands->setFramebuffer(fb.get());
        impl_->commands->clearColor(0,RenderColor(17/255.0f,33/255.0f,201/255.0f,129/255.0f));
        impl_->submit();
        const auto bytes=impl_->read_target(target.get(),7,5,samples);
        constexpr std::array<std::uint8_t,4> expected{17,33,201,129};
        for(std::size_t i=0;i<bytes.size();++i) require(bytes[i]==expected[i%4],"Synthetic sample readback differs");
        // Verify that a subsequent pass without full-target clearColor preserves prior contents.
        const RenderRect partial{0,0,1,1};
        impl_->commands->begin();
        impl_->commands->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(target.get(),RenderTextureLayout::COLOR_WRITE));
        impl_->commands->setFramebuffer(fb.get());
        impl_->commands->clearColor(0,RenderColor(250/255.0f,10/255.0f,20/255.0f,255/255.0f),&partial,1);
        impl_->submit();
        const auto preserved=impl_->read_target(target.get(),7,5,samples);
        constexpr std::array<std::uint8_t,4> updated{250,10,20,255};
        for(std::size_t i=0;i<preserved.size();++i) {
            const auto& ref=(i<std::size_t(samples)*4)?updated:expected;
            require(preserved[i]==ref[i%4],"Synthetic preserved target readback differs");
        }
    }
}
Result Renderer::draw(const Packet& p,Diagnostic diagnostic,LoadAction load_action) {
    validate_supported(p);
    const auto target_format=RenderFormat::R8G8B8A8_UNORM;
    if(load_action==LoadAction::preserve) {
        if(diagnostic==Diagnostic::clear_red)
            throw std::invalid_argument("Diagnostic red clear conflicts with target preservation");
        require(impl_->target && impl_->framebuffer &&
                impl_->target_width==p.width && impl_->target_height==p.height &&
                impl_->target_samples==p.samples && impl_->target_format==target_format,
                "Plume guest preserve load requires an existing matching target");
    } else if(load_action!=LoadAction::clear_zero) {
        throw std::invalid_argument("Unknown Plume guest target load action");
    }
    auto& d=*impl_->device;
    Result result{p.width,p.height,p.samples,{},translate(p.vertex_spirv,spv::ExecutionModelVertex),translate(p.pixel_spirv,spv::ExecutionModelFragment)};
    const auto needed=(std::size_t(p.vertex_address)+p.vertex_bytes.size()+255)&~std::size_t(255);
    if(needed>impl_->shared_bytes) {
        impl_->shared=d.createBuffer(RenderBufferDesc::UploadBuffer(needed,RenderBufferFlag::STORAGE));
        require(bool(impl_->shared),"Plume guest physical buffer allocation failed");
        auto* bytes=impl_->shared->map(); require(bytes,"Plume guest physical map failed");
        std::memset(bytes,0,needed); impl_->shared->unmap(); impl_->shared_bytes=needed;
    }
    auto* physical=static_cast<std::uint8_t*>(impl_->shared->map()); require(physical,"Physical upload mapping failed");
    std::memcpy(physical+p.vertex_address,p.vertex_bytes.data(),p.vertex_bytes.size()); impl_->shared->unmap();
    std::array<std::unique_ptr<RenderBuffer>,5> uniforms;
    for(unsigned i=0;i<5;++i) uniforms[i]=impl_->upload(p.uniforms[i],RenderBufferFlag::CONSTANT);
    auto indices=impl_->upload({reinterpret_cast<const std::uint8_t*>(p.indices.data()),p.indices.size()*2},RenderBufferFlag::INDEX);
    auto vs=d.createShader(result.vertex_msl.data(),result.vertex_msl.size(),"main0",RenderShaderFormat::MSL);
    auto ps=d.createShader(result.pixel_msl.data(),result.pixel_msl.size(),"main0",RenderShaderFormat::MSL);
    require(vs && ps,"Plume guest shader compilation failed");
    const RenderDescriptorRange memory_range{RenderDescriptorRangeType::BYTE_ADDRESS_BUFFER,0,1};
    std::array<RenderDescriptorRange,5> uniform_ranges;
    for(unsigned i=0;i<5;++i) uniform_ranges[i]={RenderDescriptorRangeType::CONSTANT_BUFFER,i,1};
    const std::array<RenderDescriptorRange,3> texture_ranges{{{RenderDescriptorRangeType::TEXTURE,0,1},
        {RenderDescriptorRangeType::TEXTURE,1,1},{RenderDescriptorRangeType::SAMPLER,2,1}}};
    // Set 2 is deliberately empty: SPIR-V's original set numbers are preserved.
    const std::array<RenderDescriptorSetDesc,4> sets{{{&memory_range,1},{uniform_ranges.data(),5},{},{texture_ranges.data(),3}}};
    RenderPipelineLayoutDesc layout_desc;
    layout_desc.descriptorSetDescs=sets.data(); layout_desc.descriptorSetDescsCount=4;
    auto layout=d.createPipelineLayout(layout_desc); require(bool(layout),"Guest pipeline layout failed");
    auto memory_set=d.createDescriptorSet(sets[0]); auto uniform_set=d.createDescriptorSet(sets[1]);
    auto texture_set=d.createDescriptorSet(sets[3]); require(memory_set && uniform_set && texture_set,"Guest descriptor allocation failed");
    memory_set->setBuffer(0,impl_->shared.get(),impl_->shared_bytes);
    for(unsigned i=0;i<5;++i) uniform_set->setBuffer(i,uniforms[i].get(),p.uniforms[i].size());
    auto texture_desc=RenderTextureDesc::Texture(RenderTextureDimension::TEXTURE_2D_ARRAY,
        p.texture_mips[0].width,p.texture_mips[0].height,1,std::uint32_t(p.texture_mips.size()),1,RenderFormat::BC1_UNORM);
    auto texture=d.createTexture(texture_desc); require(bool(texture),"BC1 texture allocation failed");
    RenderSamplerDesc sampler_desc;
    sampler_desc.minFilter=sampler_desc.magFilter=RenderFilter::LINEAR; sampler_desc.mipmapMode=RenderMipmapMode::LINEAR;
    sampler_desc.addressU=sampler_desc.addressV=RenderTextureAddressMode::WRAP; sampler_desc.addressW=RenderTextureAddressMode::CLAMP;
    sampler_desc.anisotropyEnabled=true; sampler_desc.maxAnisotropy=4; sampler_desc.maxLOD=1000;
    auto sampler=d.createSampler(sampler_desc); require(bool(sampler),"UI sampler failed");
    // The selected shader's unsigned branch consumes the observed BC1 UNORM
    // texture. The signed resource is inactive for the recorded sign flags.
    require((word(p.uniforms[0],176)&0x3f)==0,"Signed texture branch is unsupported in the first UI adapter");
    texture_set->setTexture(0,texture.get(),RenderTextureLayout::SHADER_READ);
    texture_set->setTexture(1,texture.get(),RenderTextureLayout::SHADER_READ);
    texture_set->setSampler(2,sampler.get());
    std::vector<std::unique_ptr<RenderBuffer>> staging;
    impl_->commands->begin();
    impl_->commands->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(texture.get(),RenderTextureLayout::COPY_DEST));
    for(std::uint32_t i=0;i<p.texture_mips.size();++i) {
        const auto& mip=p.texture_mips[i];
        const auto row_bytes=((mip.width+3)/4)*8, rows=(mip.height+3)/4, pitch=(row_bytes+255)&~255u;
        std::vector<std::uint8_t> padded(std::size_t(pitch)*rows);
        for(std::uint32_t y=0;y<rows;++y) std::memcpy(padded.data()+std::size_t(y)*pitch,mip.bytes.data()+std::size_t(y)*row_bytes,row_bytes);
        staging.push_back(impl_->upload(padded,RenderBufferFlag::NONE));
        impl_->commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(texture.get(),i),
            RenderTextureCopyLocation::PlacedFootprint(staging.back().get(),RenderFormat::BC1_UNORM,mip.width,mip.height,1,(pitch/8)*4));
    }
    impl_->commands->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(texture.get(),RenderTextureLayout::SHADER_READ));
    impl_->submit();
    if(!impl_->target || !impl_->framebuffer ||
       impl_->target_width!=p.width || impl_->target_height!=p.height ||
       impl_->target_samples!=p.samples || impl_->target_format!=target_format) {
        impl_->framebuffer.reset();
        impl_->target=d.createTexture(RenderTextureDesc::ColorTarget(p.width,p.height,target_format,RenderMultisampling(p.samples)));
        require(bool(impl_->target),"UI target allocation failed");
        const RenderTexture* color=impl_->target.get(); RenderFramebufferDesc fb_desc;
        fb_desc.colorAttachments=&color; fb_desc.colorAttachmentsCount=1;
        impl_->framebuffer=d.createFramebuffer(fb_desc); require(bool(impl_->framebuffer),"UI framebuffer failed");
        impl_->target_width=p.width; impl_->target_height=p.height; impl_->target_samples=p.samples;
        impl_->target_format=target_format;
    }
    RenderGraphicsPipelineDesc pipeline_desc;
    pipeline_desc.pipelineLayout=layout.get(); pipeline_desc.vertexShader=vs.get(); pipeline_desc.pixelShader=ps.get();
    pipeline_desc.renderTargetCount=1; pipeline_desc.renderTargetFormat[0]=target_format;
    pipeline_desc.renderTargetBlend[0]=RenderBlendDesc::AlphaBlend();
    pipeline_desc.renderTargetBlend[0].srcBlendAlpha=RenderBlend::SRC_ALPHA;
    pipeline_desc.multisampling=RenderMultisampling(p.samples);
    pipeline_desc.cullMode=RenderCullMode::BACK; pipeline_desc.frontFace=RenderFrontFace::CLOCKWISE;
    if(diagnostic==Diagnostic::no_cull) pipeline_desc.cullMode=RenderCullMode::NONE;
    auto pipeline=d.createGraphicsPipeline(pipeline_desc); require(bool(pipeline),"UI graphics pipeline failed");
    impl_->commands->begin();
    impl_->commands->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(impl_->target.get(),RenderTextureLayout::COLOR_WRITE));
    impl_->commands->setFramebuffer(impl_->framebuffer.get());
    impl_->commands->setViewports(RenderViewport(0,0,p.viewport_width,p.viewport_height));
    if(load_action==LoadAction::clear_zero) {
        impl_->commands->setScissors(RenderRect(0,0,p.width,p.height));
        impl_->commands->clearColor(0,diagnostic==Diagnostic::clear_red?RenderColor(1,0,0,1):RenderColor(0,0,0,0));
    }
    impl_->commands->setScissors(RenderRect(p.scissor[0],p.scissor[1],p.scissor[0]+p.scissor[2],p.scissor[1]+p.scissor[3]));
    impl_->commands->setGraphicsPipelineLayout(layout.get()); impl_->commands->setPipeline(pipeline.get());
    impl_->commands->setGraphicsDescriptorSet(memory_set.get(),0); impl_->commands->setGraphicsDescriptorSet(uniform_set.get(),1);
    impl_->commands->setGraphicsDescriptorSet(texture_set.get(),3);
    const RenderIndexBufferView ib(indices.get(),std::uint32_t(p.indices.size()*2),RenderFormat::R16_UINT);
    impl_->commands->setIndexBuffer(&ib); impl_->commands->drawIndexedInstanced(std::uint32_t(p.indices.size()),1,0,0,0);
    impl_->submit();
    result.rgba_samples=impl_->read_target(impl_->target.get(),p.width,p.height,p.samples);
    return result;
}
CompositeResult Renderer::draw_composite(const CompositePacket& p,
                                         std::span<const std::uint8_t> preload_rgba16f,
                                         std::uint32_t preload_width,
                                         std::uint32_t preload_height,
                                         LoadAction load_action) {
    validate_supported_composite(p);
    const auto target_format=RenderFormat::R16G16B16A16_FLOAT;
    if(!preload_rgba16f.empty()) {
        if(!preload_width || !preload_height || preload_width>p.width || preload_height>p.height ||
           preload_rgba16f.size()!=std::size_t(preload_width)*preload_height*8)
            throw std::invalid_argument("Invalid composite RGBA16F preload dimensions or byte count");
    } else if(load_action==LoadAction::preserve) {
        require(impl_->target && impl_->framebuffer &&
                impl_->target_width==p.width && impl_->target_height==p.height &&
                impl_->target_samples==p.samples && impl_->target_format==target_format,
                "Plume composite preserve load requires an existing matching RGBA16F target or preload");
    } else if(load_action!=LoadAction::clear_zero) {
        throw std::invalid_argument("Unknown Plume composite target load action");
    }
    auto& d=*impl_->device;
    CompositeResult result{p.width,p.height,p.samples,{},
        translate(p.vertex_spirv,spv::ExecutionModelVertex,true),
        translate(p.pixel_spirv,spv::ExecutionModelFragment,true)};
    const auto needed=(std::size_t(p.vertex_address)+p.vertex_bytes.size()+255)&~std::size_t(255);
    if(needed>impl_->shared_bytes) {
        impl_->shared=d.createBuffer(RenderBufferDesc::UploadBuffer(needed,RenderBufferFlag::STORAGE));
        require(bool(impl_->shared),"Plume composite physical buffer allocation failed");
        auto* bytes=impl_->shared->map(); require(bytes,"Plume composite physical map failed");
        std::memset(bytes,0,needed); impl_->shared->unmap(); impl_->shared_bytes=needed;
    }
    auto* physical=static_cast<std::uint8_t*>(impl_->shared->map()); require(physical,"Physical upload mapping failed");
    std::memcpy(physical+p.vertex_address,p.vertex_bytes.data(),p.vertex_bytes.size()); impl_->shared->unmap();
    std::array<std::unique_ptr<RenderBuffer>,5> uniforms;
    for(unsigned i=0;i<5;++i) uniforms[i]=impl_->upload(p.uniforms[i],RenderBufferFlag::CONSTANT);
    auto vs=d.createShader(result.vertex_msl.data(),result.vertex_msl.size(),"main0",RenderShaderFormat::MSL);
    auto ps=d.createShader(result.pixel_msl.data(),result.pixel_msl.size(),"main0",RenderShaderFormat::MSL);
    require(vs && ps,"Plume composite shader compilation failed");
    const RenderDescriptorRange memory_range{RenderDescriptorRangeType::BYTE_ADDRESS_BUFFER,0,1};
    std::array<RenderDescriptorRange,5> uniform_ranges;
    for(unsigned i=0;i<5;++i) uniform_ranges[i]={RenderDescriptorRangeType::CONSTANT_BUFFER,i,1};
    // In composite-ps.spv, fetch slot 1 is bound at (3,0)/(3,1)/(3,4) and fetch slot 0 at (3,2)/(3,3)/(3,5).
    const std::array<RenderDescriptorRange,6> texture_ranges{{
        {RenderDescriptorRangeType::TEXTURE,0,1},
        {RenderDescriptorRangeType::TEXTURE,1,1},
        {RenderDescriptorRangeType::TEXTURE,2,1},
        {RenderDescriptorRangeType::TEXTURE,3,1},
        {RenderDescriptorRangeType::SAMPLER,4,1},
        {RenderDescriptorRangeType::SAMPLER,5,1}}};
    const std::array<RenderDescriptorSetDesc,4> sets{{{&memory_range,1},{uniform_ranges.data(),5},{},{texture_ranges.data(),6}}};
    RenderPipelineLayoutDesc layout_desc;
    layout_desc.descriptorSetDescs=sets.data(); layout_desc.descriptorSetDescsCount=4;
    auto layout=d.createPipelineLayout(layout_desc); require(bool(layout),"Composite pipeline layout failed");
    auto memory_set=d.createDescriptorSet(sets[0]); auto uniform_set=d.createDescriptorSet(sets[1]);
    auto texture_set=d.createDescriptorSet(sets[3]); require(memory_set && uniform_set && texture_set,"Composite descriptor allocation failed");
    memory_set->setBuffer(0,impl_->shared.get(),impl_->shared_bytes);
    for(unsigned i=0;i<5;++i) uniform_set->setBuffer(i,uniforms[i].get(),p.uniforms[i].size());
    std::array<std::unique_ptr<RenderTexture>,2> textures;
    std::array<std::unique_ptr<RenderSampler>,2> samplers;
    std::vector<std::unique_ptr<RenderBuffer>> staging;
    impl_->commands->begin();
    for(std::size_t i=0;i<2;++i) {
        const auto& src=p.textures[i];
        auto tex_desc=RenderTextureDesc::Texture(RenderTextureDimension::TEXTURE_2D_ARRAY,
            src.width,src.height,1,1,1,RenderFormat::R8G8B8A8_UNORM);
        textures[i]=d.createTexture(tex_desc); require(bool(textures[i]),"Composite RGBA8 texture allocation failed");
        RenderSamplerDesc samp_desc;
        samp_desc.minFilter=samp_desc.magFilter=RenderFilter::LINEAR;
        samp_desc.mipmapMode=RenderMipmapMode::NEAREST;
        const auto addr=(i==0)?RenderTextureAddressMode::CLAMP:RenderTextureAddressMode::WRAP;
        samp_desc.addressU=samp_desc.addressV=addr;
        samp_desc.addressW=RenderTextureAddressMode::CLAMP;
        samp_desc.anisotropyEnabled=false; samp_desc.maxAnisotropy=1;
        samp_desc.minLOD=0.0f; samp_desc.maxLOD=0.0f;
        samplers[i]=d.createSampler(samp_desc); require(bool(samplers[i]),"Composite sampler allocation failed");
        impl_->commands->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(textures[i].get(),RenderTextureLayout::COPY_DEST));
        const auto row_bytes=src.width*4u, pitch=(row_bytes+255u)&~255u;
        std::vector<std::uint8_t> padded(std::size_t(pitch)*src.height);
        for(std::uint32_t y=0;y<src.height;++y)
            std::memcpy(padded.data()+std::size_t(y)*pitch,src.rgba8_bytes.data()+std::size_t(y)*row_bytes,row_bytes);
        staging.push_back(impl_->upload(padded,RenderBufferFlag::NONE));
        impl_->commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(textures[i].get(),0),
            RenderTextureCopyLocation::PlacedFootprint(staging.back().get(),RenderFormat::R8G8B8A8_UNORM,src.width,src.height,1,pitch/4u));
        impl_->commands->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(textures[i].get(),RenderTextureLayout::SHADER_READ));
    }
    if(!impl_->target || !impl_->framebuffer ||
       impl_->target_width!=p.width || impl_->target_height!=p.height ||
       impl_->target_samples!=p.samples || impl_->target_format!=target_format) {
        impl_->framebuffer.reset();
        impl_->target=d.createTexture(RenderTextureDesc::ColorTarget(p.width,p.height,target_format,RenderMultisampling(p.samples)));
        require(bool(impl_->target),"Composite RGBA16F target allocation failed");
        const RenderTexture* color=impl_->target.get(); RenderFramebufferDesc fb_desc;
        fb_desc.colorAttachments=&color; fb_desc.colorAttachmentsCount=1;
        impl_->framebuffer=d.createFramebuffer(fb_desc); require(bool(impl_->framebuffer),"Composite framebuffer failed");
        impl_->target_width=p.width; impl_->target_height=p.height; impl_->target_samples=p.samples;
        impl_->target_format=target_format;
    }
    if(!preload_rgba16f.empty()) {
        impl_->commands->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(impl_->target.get(),RenderTextureLayout::COPY_DEST));
        const auto row_bytes=preload_width*8u, pitch=(row_bytes+255u)&~255u;
        std::vector<std::uint8_t> padded(std::size_t(pitch)*preload_height);
        for(std::uint32_t y=0;y<preload_height;++y)
            std::memcpy(padded.data()+std::size_t(y)*pitch,preload_rgba16f.data()+std::size_t(y)*row_bytes,row_bytes);
        staging.push_back(impl_->upload(padded,RenderBufferFlag::NONE));
        impl_->commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(impl_->target.get(),0),
            RenderTextureCopyLocation::PlacedFootprint(staging.back().get(),target_format,preload_width,preload_height,1,pitch/8u));
    }
    impl_->submit();
    // Bind fetch 1 to (3,0), (3,1), (3,4) and fetch 0 to (3,2), (3,3), (3,5).
    texture_set->setTexture(0,textures[1].get(),RenderTextureLayout::SHADER_READ);
    texture_set->setTexture(1,textures[1].get(),RenderTextureLayout::SHADER_READ);
    texture_set->setTexture(2,textures[0].get(),RenderTextureLayout::SHADER_READ);
    texture_set->setTexture(3,textures[0].get(),RenderTextureLayout::SHADER_READ);
    texture_set->setSampler(4,samplers[1].get());
    texture_set->setSampler(5,samplers[0].get());
    RenderGraphicsPipelineDesc pipeline_desc;
    pipeline_desc.pipelineLayout=layout.get(); pipeline_desc.vertexShader=vs.get(); pipeline_desc.pixelShader=ps.get();
    pipeline_desc.renderTargetCount=1; pipeline_desc.renderTargetFormat[0]=target_format;
    RenderBlendDesc blend_desc;
    blend_desc.blendEnabled=true;
    blend_desc.srcBlend=RenderBlend::ONE; blend_desc.dstBlend=RenderBlend::INV_SRC_ALPHA; blend_desc.blendOp=RenderBlendOperation::ADD;
    blend_desc.srcBlendAlpha=RenderBlend::ONE; blend_desc.dstBlendAlpha=RenderBlend::INV_SRC_ALPHA; blend_desc.blendOpAlpha=RenderBlendOperation::ADD;
    blend_desc.renderTargetWriteMask=static_cast<std::uint8_t>(
        std::uint8_t(RenderColorWriteEnable::RED)|std::uint8_t(RenderColorWriteEnable::GREEN)|std::uint8_t(RenderColorWriteEnable::BLUE));
    pipeline_desc.renderTargetBlend[0]=blend_desc;
    pipeline_desc.multisampling=RenderMultisampling(p.samples);
    pipeline_desc.cullMode=RenderCullMode::NONE; pipeline_desc.frontFace=RenderFrontFace::CLOCKWISE;
    auto pipeline=d.createGraphicsPipeline(pipeline_desc); require(bool(pipeline),"Composite graphics pipeline failed");
    impl_->commands->begin();
    impl_->commands->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(impl_->target.get(),RenderTextureLayout::COLOR_WRITE));
    impl_->commands->setFramebuffer(impl_->framebuffer.get());
    impl_->commands->setViewports(RenderViewport(0,0,p.viewport_width,p.viewport_height));
    if(load_action==LoadAction::clear_zero && preload_rgba16f.empty()) {
        impl_->commands->setScissors(RenderRect(0,0,p.width,p.height));
        impl_->commands->clearColor(0,RenderColor(0,0,0,0));
    }
    impl_->commands->setScissors(RenderRect(p.scissor[0],p.scissor[1],p.scissor[0]+p.scissor[2],p.scissor[1]+p.scissor[3]));
    impl_->commands->setGraphicsPipelineLayout(layout.get()); impl_->commands->setPipeline(pipeline.get());
    impl_->commands->setGraphicsDescriptorSet(memory_set.get(),0); impl_->commands->setGraphicsDescriptorSet(uniform_set.get(),1);
    impl_->commands->setGraphicsDescriptorSet(texture_set.get(),3);
    impl_->commands->drawInstanced(p.vertex_count,1,0,0);
    impl_->submit();
    const std::uint32_t read_w=preload_width?preload_width:p.width;
    const std::uint32_t read_h=preload_height?preload_height:p.height;
    result.width=read_w; result.height=read_h;
    result.rgba16f_bytes=impl_->read_target_f16(impl_->target.get(),read_w,read_h);
    return result;
}
}
