#include "render/renderer.hpp"
#include "plume_render_interface.h"
#include "native_shaders.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace plume { std::unique_ptr<RenderInterface> CreateMetalInterface(); }
namespace blur::render {
using namespace plume;
namespace {
constexpr auto format = RenderFormat::R8G8B8A8_UNORM;
constexpr auto swap_format = RenderFormat::B8G8R8A8_UNORM;
// Keep allocations and uint32 Plume vertex/index views bounded for this slice.
constexpr std::uint32_t max_extent = 8192;
constexpr std::size_t max_vertices = 1 << 20;
constexpr std::size_t max_indices = 3 << 20;
constexpr std::size_t max_draws = 4096;
std::uint32_t row_pixels(std::uint32_t width) { return (width + 63) & ~63u; }
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool finite(const Vertex& vertex) {
    const auto valid = [](float x) { return std::isfinite(x); };
    return std::all_of(vertex.position.begin(), vertex.position.end(), valid) &&
           std::all_of(vertex.uv.begin(), vertex.uv.end(), valid) &&
           std::all_of(vertex.color.begin(), vertex.color.end(), valid);
}
}

std::uint64_t rgba_size(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > max_extent || height > max_extent)
        throw std::invalid_argument("Render dimensions must be 1..8192");
    return std::uint64_t(width) * height * 4;
}
void validate_frame(const Frame& frame) {
    rgba_size(frame.width, frame.height);
    for (float c : frame.clear)
        if (!std::isfinite(c) || c < 0 || c > 1) throw std::invalid_argument("Invalid clear color");
    if (frame.draws.size() > max_draws) throw std::invalid_argument("Too many draws");
    std::size_t vertices = 0, indices = 0;
    for (const auto& draw : frame.draws) {
        if (!draw.texture || draw.vertices.empty() || draw.indices.empty() ||
            draw.vertices.size() > max_vertices || draw.indices.size() > max_indices || draw.indices.size() % 3)
            throw std::invalid_argument("Expected a bounded indexed triangle list with a texture");
        vertices += draw.vertices.size(); indices += draw.indices.size();
        if (vertices > max_vertices || indices > max_indices) throw std::invalid_argument("Frame upload budget exceeded");
        if (draw.blend != Blend::copy && draw.blend != Blend::alpha) throw std::invalid_argument("Unsupported blend");
        if (!std::all_of(draw.vertices.begin(), draw.vertices.end(), finite)) throw std::invalid_argument("Nonfinite vertex");
        for (auto index : draw.indices)
            if (index >= draw.vertices.size()) throw std::invalid_argument("Index outside vertex buffer");
        const auto& r = draw.scissor;
        if (r.width || r.height || r.x || r.y) {
            if (!r.width || !r.height || r.x >= frame.width || r.y >= frame.height ||
                r.width > frame.width - r.x || r.height > frame.height - r.y)
                throw std::invalid_argument("Scissor outside render target");
        }
    }
}
std::array<Vertex, 4> quad(float left, float top, float right, float bottom) {
    return {{{{left, top, 0}, {0, 0}}, {{left, bottom, 0}, {0, 1}},
             {{right, top, 0}, {1, 0}}, {{right, bottom, 0}, {1, 1}}}};
}

struct Renderer::Impl {
    struct Texture {
        std::uint32_t width, height;
        std::unique_ptr<RenderTexture> gpu;
        std::unique_ptr<RenderSampler> sampler;
        std::unique_ptr<RenderDescriptorSet> descriptors;
    };
    std::unique_ptr<RenderInterface> interface;
    std::unique_ptr<RenderDevice> device;
    std::unique_ptr<RenderCommandQueue> queue;
    std::unique_ptr<RenderCommandList> commands;
    std::unique_ptr<RenderCommandFence> fence;
    std::unique_ptr<RenderShader> vertex_shader, pixel_shader;
    std::unique_ptr<RenderPipelineLayout> layout;
    std::array<std::unique_ptr<RenderPipeline>, 4> pipelines;
    std::unordered_map<TextureId, Texture> textures;
    TextureId next_texture = 1;
    std::unique_ptr<RenderTexture> target;
    std::unique_ptr<RenderFramebuffer> framebuffer;
    std::uint32_t target_width = 0, target_height = 0;
    std::unique_ptr<RenderSwapChain> swapchain;
    std::unique_ptr<RenderCommandSemaphore> acquired, released;
    std::string name;

    static RenderDescriptorSetDesc descriptor_desc() {
        static const RenderDescriptorRange ranges[]{
            {RenderDescriptorRangeType::TEXTURE, 0, 1}, {RenderDescriptorRangeType::SAMPLER, 1, 1}};
        return {ranges, 2};
    }
    Impl() {
        interface = CreateMetalInterface();
        require(interface && !interface->getDeviceNames().empty(), "Plume: no Metal device available");
        device = interface->createDevice();
        require(bool(device), "Plume: device creation failed");
        name = device->getDescription().name;
        queue = device->createCommandQueue(RenderCommandListType::DIRECT);
        require(bool(queue), "Plume: graphics queue unavailable");
        commands = queue->createCommandList();
        fence = device->createCommandFence();
        require(queue && commands && fence, "Plume: command resources unavailable");
        vertex_shader = device->createShader(native_msl, sizeof(native_msl), "native_vertex", RenderShaderFormat::MSL);
        pixel_shader = device->createShader(native_msl, sizeof(native_msl), "native_fragment", RenderShaderFormat::MSL);
        const auto descriptors = descriptor_desc();
        RenderPipelineLayoutDesc desc;
        desc.allowInputLayout = true;
        desc.descriptorSetDescs = &descriptors;
        desc.descriptorSetDescsCount = 1;
        layout = device->createPipelineLayout(desc);
        require(vertex_shader && pixel_shader && layout, "Plume: shader/layout creation failed");
        for (unsigned i = 0; i < pipelines.size(); ++i) {
            const RenderInputSlot slot(0, sizeof(Vertex));
            const RenderInputElement elements[]{
                {"POSITION", 0, 0, RenderFormat::R32G32B32_FLOAT, 0, offsetof(Vertex, position)},
                {"TEXCOORD", 0, 1, RenderFormat::R32G32_FLOAT, 0, offsetof(Vertex, uv)},
                {"COLOR", 0, 2, RenderFormat::R32G32B32A32_FLOAT, 0, offsetof(Vertex, color)}};
            RenderGraphicsPipelineDesc pipeline;
            pipeline.pipelineLayout = layout.get();
            pipeline.vertexShader = vertex_shader.get();
            pipeline.pixelShader = pixel_shader.get();
            pipeline.inputSlots = &slot;
            pipeline.inputSlotsCount = 1;
            pipeline.inputElements = elements;
            pipeline.inputElementsCount = 3;
            pipeline.renderTargetCount = 1;
            pipeline.renderTargetFormat[0] = i < 2 ? format : swap_format;
            pipeline.renderTargetBlend[0] = (i % 2) ? RenderBlendDesc::AlphaBlend() : RenderBlendDesc::Copy();
            pipelines[i] = device->createGraphicsPipeline(pipeline);
            require(bool(pipelines[i]), "Plume: graphics pipeline creation failed");
        }
    }
    Texture& texture(TextureId id) {
        const auto found = textures.find(id);
        if (found == textures.end()) throw std::invalid_argument("Unknown or destroyed texture");
        return found->second;
    }
    void submit() {
        commands->end();
        queue->executeCommandLists(commands.get(), fence.get());
        queue->waitForCommandFence(fence.get());
    }
    std::unique_ptr<RenderBuffer> upload(const void* bytes, std::size_t size, bool index = false) {
        auto buffer = device->createBuffer(index ? RenderBufferDesc::IndexBuffer(size, RenderHeapType::UPLOAD) :
                                                   RenderBufferDesc::VertexBuffer(size, RenderHeapType::UPLOAD));
        require(bool(buffer), "Plume: upload allocation failed");
        auto* data = buffer->map();
        require(data != nullptr, "Plume: upload mapping failed");
        std::memcpy(data, bytes, size);
        buffer->unmap();
        return buffer;
    }
    void update(Texture& texture, std::span<const std::uint8_t> rgba) {
        if (rgba.size() != rgba_size(texture.width, texture.height)) throw std::invalid_argument("RGBA upload size mismatch");
        const auto pitch = row_pixels(texture.width);
        auto staging = device->createBuffer(RenderBufferDesc::UploadBuffer(std::uint64_t(pitch) * texture.height * 4));
        require(bool(staging), "Plume: staging allocation failed");
        auto* mapped = static_cast<std::uint8_t*>(staging->map());
        require(mapped != nullptr, "Plume: staging map failed");
        for (std::uint32_t y = 0; y < texture.height; ++y)
            std::memcpy(mapped + std::size_t(y) * pitch * 4, rgba.data() + std::size_t(y) * texture.width * 4,
                        std::size_t(texture.width) * 4);
        staging->unmap();
        commands->begin();
        commands->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.gpu.get(), RenderTextureLayout::COPY_DEST));
        commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(texture.gpu.get()),
            RenderTextureCopyLocation::PlacedFootprint(staging.get(), format, texture.width, texture.height, 1, pitch));
        commands->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture.gpu.get(), RenderTextureLayout::SHADER_READ));
        submit();
    }
    void ensure_target(std::uint32_t width, std::uint32_t height) {
        if (target && width == target_width && height == target_height) return;
        framebuffer.reset();
        target = device->createTexture(RenderTextureDesc::ColorTarget(width, height, format));
        require(bool(target), "Plume: render target allocation failed");
        const RenderTexture* attachment = target.get();
        RenderFramebufferDesc desc;
        desc.colorAttachments = &attachment; desc.colorAttachmentsCount = 1;
        framebuffer = device->createFramebuffer(desc);
        require(bool(framebuffer), "Plume: framebuffer allocation failed");
        target_width = width; target_height = height;
    }
    struct Uploads {
        std::unique_ptr<RenderBuffer> vertices, indices;
    };
    std::vector<Uploads> prepare(const Frame& frame) {
        validate_frame(frame);
        // Resolve every ID before recording begins. A bad later draw must never
        // leave a partially submitted frame or an open command list.
        for (const auto& draw : frame.draws) texture(draw.texture);
        std::vector<Uploads> uploads;
        uploads.reserve(frame.draws.size());
        for (const auto& draw : frame.draws)
            uploads.push_back({upload(draw.vertices.data(), draw.vertices.size_bytes()),
                               upload(draw.indices.data(), draw.indices.size_bytes(), true)});
        return uploads;
    }
    void encode(const Frame& frame, const RenderFramebuffer* fb, bool swap, const std::vector<Uploads>& uploads) {
        commands->setFramebuffer(fb);
        commands->setViewports(RenderViewport(0, 0, float(frame.width), float(frame.height)));
        commands->setScissors(RenderRect(0, 0, frame.width, frame.height));
        commands->clearColor(0, RenderColor(frame.clear[0], frame.clear[1], frame.clear[2], frame.clear[3]));
        commands->setGraphicsPipelineLayout(layout.get());
        const RenderInputSlot slot(0, sizeof(Vertex));
        for (std::size_t i = 0; i < frame.draws.size(); ++i) {
            const auto& draw = frame.draws[i];
            auto& material = texture(draw.texture);
            const RenderVertexBufferView vb(uploads[i].vertices.get(), std::uint32_t(draw.vertices.size_bytes()));
            const RenderIndexBufferView ib(uploads[i].indices.get(), std::uint32_t(draw.indices.size_bytes()), RenderFormat::R32_UINT);
            commands->setPipeline(pipelines[(swap ? 2 : 0) + (draw.blend == Blend::alpha ? 1 : 0)].get());
            const auto r = draw.scissor.width ? draw.scissor : Rect{0, 0, frame.width, frame.height};
            commands->setScissors(RenderRect(r.x, r.y, r.x + r.width, r.y + r.height));
            commands->setGraphicsDescriptorSet(material.descriptors.get(), 0);
            commands->setVertexBuffers(0, &vb, 1, &slot);
            commands->setIndexBuffer(&ib);
            commands->drawIndexedInstanced(std::uint32_t(draw.indices.size()), 1, 0, 0, 0);
        }
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
const std::string& Renderer::device_name() const { return impl_->name; }
TextureId Renderer::create_texture(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba, Filter filter) {
    if (rgba.size() != rgba_size(width, height)) throw std::invalid_argument("RGBA upload size mismatch");
    if (filter != Filter::nearest && filter != Filter::linear) throw std::invalid_argument("Unsupported filter");
    Impl::Texture texture{width, height, {}, {}, {}};
    texture.gpu = impl_->device->createTexture(RenderTextureDesc::Texture2D(width, height, 1, format));
    RenderSamplerDesc sampler;
    sampler.minFilter = sampler.magFilter = filter == Filter::nearest ? RenderFilter::NEAREST : RenderFilter::LINEAR;
    sampler.mipmapMode = RenderMipmapMode::NEAREST;
    sampler.addressU = sampler.addressV = sampler.addressW = RenderTextureAddressMode::CLAMP;
    sampler.maxLOD = 0;
    texture.sampler = impl_->device->createSampler(sampler);
    texture.descriptors = impl_->device->createDescriptorSet(Impl::descriptor_desc());
    require(texture.gpu && texture.sampler && texture.descriptors, "Plume: texture resources unavailable");
    impl_->update(texture, rgba);
    texture.descriptors->setTexture(0, texture.gpu.get(), RenderTextureLayout::SHADER_READ);
    texture.descriptors->setSampler(1, texture.sampler.get());
    if (impl_->next_texture == std::numeric_limits<TextureId>::max()) throw std::overflow_error("Texture ID exhausted");
    const auto id = impl_->next_texture++;
    impl_->textures.emplace(id, std::move(texture));
    return id;
}
void Renderer::update_texture(TextureId id, std::span<const std::uint8_t> rgba) { impl_->update(impl_->texture(id), rgba); }
void Renderer::destroy_texture(TextureId id) {
    impl_->texture(id);
    impl_->textures.erase(id);
}
Pixels Renderer::render(const Frame& frame) {
    auto uploads = impl_->prepare(frame);
    impl_->ensure_target(frame.width, frame.height);
    const auto pitch = row_pixels(frame.width);
    auto readback = impl_->device->createBuffer(RenderBufferDesc::ReadbackBuffer(std::uint64_t(pitch) * frame.height * 4));
    require(bool(readback), "Plume: readback allocation failed");
    impl_->commands->begin();
    impl_->commands->barriers(RenderBarrierStage::GRAPHICS,
        RenderTextureBarrier(impl_->target.get(), RenderTextureLayout::COLOR_WRITE));
    impl_->encode(frame, impl_->framebuffer.get(), false, uploads);
    impl_->commands->barriers(RenderBarrierStage::COPY,
        RenderTextureBarrier(impl_->target.get(), RenderTextureLayout::COPY_SOURCE));
    impl_->commands->copyTextureRegion(
        RenderTextureCopyLocation::PlacedFootprint(readback.get(), format, frame.width, frame.height, 1, pitch),
        RenderTextureCopyLocation::Subresource(impl_->target.get()));
    impl_->submit();
    Pixels pixels{frame.width, frame.height, std::vector<std::uint8_t>(rgba_size(frame.width, frame.height))};
    const auto* mapped = static_cast<const std::uint8_t*>(readback->map());
    require(mapped != nullptr, "Plume: readback map failed");
    for (std::uint32_t y = 0; y < frame.height; ++y)
        std::memcpy(pixels.rgba.data() + std::size_t(y) * frame.width * 4, mapped + std::size_t(y) * pitch * 4,
                    std::size_t(frame.width) * 4);
    readback->unmap();
    return pixels;
}
void Renderer::attach_window(void* window, void* metal_layer) {
    require(window && metal_layer, "Plume: missing native window/layer");
    detach_window();
    impl_->acquired = impl_->device->createCommandSemaphore();
    impl_->released = impl_->device->createCommandSemaphore();
    impl_->swapchain = impl_->queue->createSwapChain(RenderSwapChainDesc({window, metal_layer}, swap_format, 3));
    require(impl_->acquired && impl_->released && impl_->swapchain && impl_->swapchain->resize(), "Plume: swapchain creation failed");
    impl_->swapchain->setVsyncEnabled(true);
}
void Renderer::detach_window() {
    // Every submitted frame is fence-complete before this method can run.
    impl_->swapchain.reset(); impl_->released.reset(); impl_->acquired.reset();
}
bool Renderer::present(const Frame& frame) {
    auto uploads = impl_->prepare(frame);
    require(bool(impl_->swapchain), "Plume: no window attached");
    auto& swap = impl_->swapchain;
    if (swap->needsResize() && !swap->resize()) return false;
    if (swap->isEmpty()) return false;
    // Window drawable size is authoritative. A caller may use a different
    // offscreen extent, but a presented scissor must still fit the drawable.
    Frame actual{swap->getWidth(), swap->getHeight(), frame.clear, frame.draws};
    validate_frame(actual);
    std::uint32_t index = 0;
    if (!swap->acquireTexture(impl_->acquired.get(), &index)) return false;
    auto* target = swap->getTexture(index);
    const RenderTexture* attachment = target;
    RenderFramebufferDesc desc;
    desc.colorAttachments = &attachment; desc.colorAttachmentsCount = 1;
    auto framebuffer = impl_->device->createFramebuffer(desc);
    require(bool(framebuffer), "Plume: swap framebuffer creation failed");
    impl_->commands->begin();
    impl_->commands->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(target, RenderTextureLayout::COLOR_WRITE));
    impl_->encode(actual, framebuffer.get(), true, uploads);
    impl_->commands->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(target, RenderTextureLayout::PRESENT));
    impl_->commands->end();
    const RenderCommandList* commands = impl_->commands.get();
    auto* acquired = impl_->acquired.get(); auto* released = impl_->released.get();
    impl_->queue->executeCommandLists(&commands, 1, &acquired, 1, &released, 1, impl_->fence.get());
    impl_->queue->waitForCommandFence(impl_->fence.get());
    return swap->present(index, &released, 1);
}
}  // namespace blur::render
