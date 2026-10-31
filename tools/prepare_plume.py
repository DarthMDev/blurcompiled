"""Make a build-local Plume copy with explicit runtime MSL shader support.

The dependency pin is verified by CMake before this runs. Original dependency
files remain unchanged. This extension is Metal-only and adds a distinct format;
the existing METAL format continues to require a compiled metallib.
"""
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
for name in ("plume_metal.cpp", "plume_metal.h", "plume_apple.mm", "plume_apple.h",
             "plume_render_interface.h", "plume_render_interface_types.h", "plume_render_interface_builders.h"):
    text = (source / name).read_text()
    changes = []
    if name == "plume_render_interface_types.h":
        changes = [("        SPIRV,\n        METAL\n", "        SPIRV,\n        METAL,\n        MSL // Blur: explicit runtime Metal source format\n"),
                   ("        TEXTURE_2D,\n        TEXTURE_3D\n", "        TEXTURE_2D,\n        TEXTURE_3D,\n        TEXTURE_2D_ARRAY\n"),
                   ("        TEXTURE_3D,\n        TEXTURE_CUBE\n", "        TEXTURE_3D,\n        TEXTURE_CUBE,\n        TEXTURE_2D_ARRAY\n")]
    elif name == "plume_metal.h":
        changes = [
            ("        float depthBiasConstantFactor;\n        float depthBiasClamp;\n        float depthBiasSlopeFactor;\n        bool dynamicDepthBiasEnabled;",
             "        float depthBiasConstantFactor = 0.0f;\n        float depthBiasClamp = 0.0f;\n        float depthBiasSlopeFactor = 0.0f;\n        bool dynamicDepthBiasEnabled = false;"),
            ("        dispatch_semaphore_t semaphore;",
             "        dispatch_semaphore_t semaphore;\n        std::mutex errorMutex;\n        std::string error;")]
    elif name == "plume_metal.cpp":
        changes = [
            ("#include <mutex>", "#include <mutex>\n#include <stdexcept>"),
            ("            case RenderTextureDimension::TEXTURE_3D:\n                assert(sampleCount <= 1",
             "            case RenderTextureDimension::TEXTURE_2D_ARRAY:\n                return sampleCount > 1 ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DArray;\n"
             "            case RenderTextureDimension::TEXTURE_3D:\n                assert(sampleCount <= 1"),
            ("            case RenderTextureViewDimension::TEXTURE_3D:\n                assert(sampleCount <= 1",
             "            case RenderTextureViewDimension::TEXTURE_2D_ARRAY:\n                return sampleCount > 1 ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DArray;\n"
             "            case RenderTextureViewDimension::TEXTURE_3D:\n                assert(sampleCount <= 1"),
            ("assert(format == RenderShaderFormat::METAL);",
             "assert(format == RenderShaderFormat::METAL || format == RenderShaderFormat::MSL);"),
            ("        const dispatch_data_t dispatchData = dispatch_data_create(data, size, dispatch_get_main_queue(), ^{});\n"
             "        library = device->mtl->newLibrary(dispatchData, &error);",
             """        if (format == RenderShaderFormat::MSL) {
            const std::string source(static_cast<const char *>(data), size);
            MTL::CompileOptions *options = MTL::CompileOptions::alloc()->init();
            options->setLanguageVersion(MTL::LanguageVersion3_0);
            options->setFastMathEnabled(false);
            library = device->mtl->newLibrary(NS::String::string(source.c_str(), NS::UTF8StringEncoding), options, &error);
            options->release();
        } else {
            const dispatch_data_t dispatchData = dispatch_data_create(data, size, dispatch_get_main_queue(), ^{});
            library = device->mtl->newLibrary(dispatchData, &error);
            dispatch_release(dispatchData);
        }"""),
            ("        return std::make_unique<MetalShader>(this, data, size, entryPointName, format);",
             """        auto shader = std::make_unique<MetalShader>(this, data, size, entryPointName, format);
        if (!shader->library) return nullptr;
        return shader;"""),
            ("        return std::make_unique<MetalGraphicsPipeline>(this, desc);",
             "        auto pipeline = std::make_unique<MetalGraphicsPipeline>(this, desc);\n"
             "        if (!pipeline->state.renderPipelineState || !pipeline->state.depthStencilState) return nullptr;\n"
             "        return pipeline;"),
            ("        return std::make_unique<MetalBuffer>(this, nullptr, desc);",
             "        auto buffer = std::make_unique<MetalBuffer>(this, nullptr, desc);\n"
             "        if (!buffer->mtl) return nullptr;\n        return buffer;"),
            ("        return std::make_unique<MetalTexture>(this, nullptr, desc);",
             "        auto texture = std::make_unique<MetalTexture>(this, nullptr, desc);\n"
             "        if (!texture->mtl) return nullptr;\n        return texture;"),
            ("                dispatch_semaphore_signal(static_cast<MetalCommandFence *>(signalFence)->semaphore);",
             """                auto *fence = static_cast<MetalCommandFence *>(signalFence);
                {
                    std::lock_guard lock(fence->errorMutex);
                    fence->error.clear();
                    if (cmdBuffer->status() == MTL::CommandBufferStatusError) {
                        auto *error = cmdBuffer->error();
                        fence->error = error ? error->localizedDescription()->utf8String() : "Metal command buffer failed";
                    }
                }
                dispatch_semaphore_signal(fence->semaphore);"""),
            ("        const MetalCommandFence *metalFence = static_cast<MetalCommandFence *>(fence);\n"
             "        dispatch_semaphore_wait(metalFence->semaphore, DISPATCH_TIME_FOREVER);",
             """        auto *metalFence = static_cast<MetalCommandFence *>(fence);
        dispatch_semaphore_wait(metalFence->semaphore, DISPATCH_TIME_FOREVER);
        std::lock_guard lock(metalFence->errorMutex);
        if (!metalFence->error.empty()) throw std::runtime_error(metalFence->error);"""),
            ("        presentBuffer->commit();\n\n        return true;",
             """        presentBuffer->commit();
        // Blur's bring-up renderer is synchronous. Complete the callback that
        // owns this swapchain before another acquire or destruction can race it.
        presentBuffer->waitUntilCompleted();
        return presentBuffer->status() != MTL::CommandBufferStatusError;"""),
        ]
    for old, new in changes:
        if text.count(old) != 1:
            raise SystemExit(f"Pinned Plume extension context mismatch: {name}")
        text = text.replace(old, new)
    destination = output / name
    if not destination.exists() or destination.read_text() != text:
        destination.write_text(text)
