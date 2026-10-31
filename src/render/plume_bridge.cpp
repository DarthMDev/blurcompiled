#include "render/plume_bridge.h"
#include "render/guest_draw.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <memory>
#include <thread>

namespace guest = blur::render::guest;
struct BlurPlumeContext {
    const std::thread::id owner = std::this_thread::get_id();
    std::unique_ptr<guest::Renderer> renderer;
    guest::Result result{};
    guest::CompositeResult composite_result{};
    BlurPlumeStats counters{};
    uint32_t target_width = 0, target_height = 0, target_samples = 0;
    bool has_target = false;
    bool faulted = false;
};
namespace {
static_assert(sizeof(void*) == 8 && std::endian::native == std::endian::little);
static_assert(sizeof(BlurPlumeError) == 512 && sizeof(BlurPlumeSubmission) == 40);
static_assert(sizeof(BlurPlumeCompositeSubmission) == 64);
static_assert(sizeof(BlurPlumeOutput) == 176 && sizeof(BlurPlumeCompositeOutput) == 176);
static_assert(sizeof(BlurPlumeStats) == 40);
static_assert(sizeof(BlurPlumeApi) == 56 && sizeof(BlurPlumeApiV2) == 72);
BlurPlumeStatus status(BlurPlumeError* error, BlurPlumeStatus code, const char* message = "") noexcept {
    if (error) {
        *error = {};
        error->code = code;
        const auto length = std::min(std::strlen(message), sizeof(error->message)-1);
        std::memcpy(error->message, message, length);
    }
    return code;
}
bool owner(const BlurPlumeContext* context) noexcept {
    return context && context->owner == std::this_thread::get_id();
}
guest::Packet packet(const uint8_t* data, uint64_t size) {
    if (!data || size == 0 || size > (32u << 20)) throw std::invalid_argument("Invalid bridge packet span");
    auto decoded = guest::decode({data, static_cast<std::size_t>(size)});
    guest::validate_supported(decoded);
    return decoded;
}
BlurPlumeStatus probe(const uint8_t* data, uint64_t size, BlurPlumeError* error) noexcept {
    try {
        packet(data,size);
        return status(error,BLUR_PLUME_OK);
    } catch (const guest::UnsupportedDraw& e) { return status(error,BLUR_PLUME_UNSUPPORTED,e.what()); }
      catch (const std::invalid_argument& e) { return status(error,BLUR_PLUME_INVALID_INPUT,e.what()); }
      catch (const std::exception& e) { return status(error,BLUR_PLUME_INTERNAL_FAILURE,e.what()); }
      catch (...) { return status(error,BLUR_PLUME_INTERNAL_FAILURE,"Unknown bridge probe failure"); }
}
BlurPlumeStatus create(BlurPlumeContext** output, BlurPlumeError* error) noexcept {
    if (!output) return status(error,BLUR_PLUME_INVALID_INPUT,"Missing context output");
    *output = nullptr;
    try { *output = new BlurPlumeContext; return status(error,BLUR_PLUME_OK); }
    catch (...) { return status(error,BLUR_PLUME_INTERNAL_FAILURE,"Context allocation failed"); }
}
BlurPlumeStatus submit(BlurPlumeContext* context, const BlurPlumeSubmission* input,
                       uint32_t output_size, BlurPlumeOutput* output, BlurPlumeError* error) noexcept {
    if (output && output_size == sizeof(*output)) *output = {};
    if (!context) return status(error,BLUR_PLUME_INVALID_INPUT,"Missing context");
    if (!owner(context)) return status(error,BLUR_PLUME_WRONG_THREAD,"Submit must use the creating thread");
    // Invalidating the borrowed view is part of the documented submit boundary.
    context->result = {};
    auto reject = [&](BlurPlumeStatus code, const char* message) {
        ++context->counters.rejected;
        return status(error,code,message);
    };
    if (!input || input->struct_size != sizeof(*input) || !output || output_size != sizeof(*output))
        return reject(BLUR_PLUME_INVALID_INPUT,"Bridge submission/output layout mismatch");
    if (context->faulted) return reject(BLUR_PLUME_INVALID_STATE,"Context faulted; destroy it before retrying GPU work");
    if (input->load_action != BLUR_PLUME_LOAD_CLEAR_ZERO && input->load_action != BLUR_PLUME_LOAD_PRESERVE)
        return reject(BLUR_PLUME_UNSUPPORTED,"Unsupported target load action");
    if (!input->frame_id || !input->draw_id || input->frame_id < context->counters.last_frame_id ||
        input->draw_id <= context->counters.last_draw_id)
        return reject(BLUR_PLUME_INVALID_INPUT,"Host submission identities must advance monotonically");
    if (input->load_action == BLUR_PLUME_LOAD_PRESERVE && !context->has_target)
        return reject(BLUR_PLUME_INVALID_STATE,"Preserve load requires an initialized target");
    guest::Packet decoded;
    try { decoded = packet(input->packet,input->packet_bytes); }
    catch (const guest::UnsupportedDraw& e) { return reject(BLUR_PLUME_UNSUPPORTED,e.what()); }
    catch (const std::invalid_argument& e) { return reject(BLUR_PLUME_INVALID_INPUT,e.what()); }
    catch (...) { return reject(BLUR_PLUME_INTERNAL_FAILURE,"Bridge packet preparation failed"); }
    if (input->load_action == BLUR_PLUME_LOAD_PRESERVE &&
        (context->target_width != decoded.width ||
         context->target_height != decoded.height || context->target_samples != decoded.samples))
        return reject(BLUR_PLUME_INVALID_STATE,"Preserve load target dimensions or sample count changed");
    try {
        if (!context->renderer) context->renderer = std::make_unique<guest::Renderer>();
        const auto load = input->load_action == BLUR_PLUME_LOAD_PRESERVE ?
            guest::LoadAction::preserve : guest::LoadAction::clear_zero;
        context->result = context->renderer->draw(decoded,guest::Diagnostic::none,load);
        auto& r = context->result;
        context->has_target = true;
        context->target_width = r.width; context->target_height = r.height; context->target_samples = r.samples;
        output->struct_size = sizeof(*output);
        output->width = r.width; output->height = r.height; output->samples = r.samples;
        output->frame_id = input->frame_id; output->draw_id = input->draw_id;
        output->rgba_samples = r.rgba_samples.data(); output->byte_count = r.rgba_samples.size();
        const auto& name = context->renderer->device_name();
        std::memcpy(output->device_name,name.data(),std::min(name.size(),sizeof(output->device_name)-1));
        ++context->counters.accepted;
        context->counters.last_frame_id = input->frame_id; context->counters.last_draw_id = input->draw_id;
        return status(error,BLUR_PLUME_OK);
    } catch (const std::exception& e) {
        context->faulted = true; ++context->counters.gpu_failures;
        return reject(BLUR_PLUME_GPU_FAILURE,e.what());
    } catch (...) {
        context->faulted = true; ++context->counters.gpu_failures;
        return reject(BLUR_PLUME_GPU_FAILURE,"Unknown GPU failure");
    }
}
guest::CompositePacket composite_packet(const uint8_t* data, uint64_t size) {
    if (!data || size == 0 || size > (32u << 20)) throw std::invalid_argument("Invalid composite packet span");
    auto decoded = guest::decode_composite({data, static_cast<std::size_t>(size)});
    guest::validate_supported_composite(decoded);
    return decoded;
}
BlurPlumeStatus probe_composite(const uint8_t* data, uint64_t size, BlurPlumeError* error) noexcept {
    try {
        composite_packet(data, size);
        return status(error, BLUR_PLUME_OK);
    } catch (const guest::UnsupportedDraw& e) { return status(error, BLUR_PLUME_UNSUPPORTED, e.what()); }
      catch (const std::invalid_argument& e) { return status(error, BLUR_PLUME_INVALID_INPUT, e.what()); }
      catch (const std::exception& e) { return status(error, BLUR_PLUME_INTERNAL_FAILURE, e.what()); }
      catch (...) { return status(error, BLUR_PLUME_INTERNAL_FAILURE, "Unknown bridge composite probe failure"); }
}
BlurPlumeStatus submit_composite(BlurPlumeContext* context, const BlurPlumeCompositeSubmission* input,
                                uint32_t output_size, BlurPlumeCompositeOutput* output, BlurPlumeError* error) noexcept {
    if (output && output_size == sizeof(*output)) *output = {};
    if (!context) return status(error, BLUR_PLUME_INVALID_INPUT, "Missing context");
    if (!owner(context)) return status(error, BLUR_PLUME_WRONG_THREAD, "Submit must use the creating thread");
    context->composite_result = {};
    auto reject = [&](BlurPlumeStatus code, const char* message) {
        ++context->counters.rejected;
        return status(error, code, message);
    };
    if (!input || input->struct_size != sizeof(*input) || !output || output_size != sizeof(*output))
        return reject(BLUR_PLUME_INVALID_INPUT, "Bridge composite submission/output layout mismatch");
    if (context->faulted) return reject(BLUR_PLUME_INVALID_STATE, "Context faulted; destroy it before retrying GPU work");
    if (input->load_action != BLUR_PLUME_LOAD_CLEAR_ZERO && input->load_action != BLUR_PLUME_LOAD_PRESERVE)
        return reject(BLUR_PLUME_UNSUPPORTED, "Unsupported target load action");
    if (!input->frame_id || !input->draw_id || input->frame_id < context->counters.last_frame_id ||
        input->draw_id <= context->counters.last_draw_id)
        return reject(BLUR_PLUME_INVALID_INPUT, "Host submission identities must advance monotonically");
    guest::CompositePacket decoded;
    try { decoded = composite_packet(input->packet, input->packet_bytes); }
    catch (const guest::UnsupportedDraw& e) { return reject(BLUR_PLUME_UNSUPPORTED, e.what()); }
    catch (const std::invalid_argument& e) { return reject(BLUR_PLUME_INVALID_INPUT, e.what()); }
    catch (...) { return reject(BLUR_PLUME_INTERNAL_FAILURE, "Bridge composite packet preparation failed"); }
    std::span<const uint8_t> preload{};
    if (input->preload_rgba16f && input->preload_bytes) {
        if (input->preload_bytes != std::size_t(input->preload_width) * input->preload_height * 8u)
            return reject(BLUR_PLUME_INVALID_INPUT, "Preload byte count does not match preload width/height");
        preload = {input->preload_rgba16f, static_cast<std::size_t>(input->preload_bytes)};
    }
    try {
        if (!context->renderer) context->renderer = std::make_unique<guest::Renderer>();
        const auto load = input->load_action == BLUR_PLUME_LOAD_PRESERVE ?
            guest::LoadAction::preserve : guest::LoadAction::clear_zero;
        context->composite_result = context->renderer->draw_composite(
            decoded, preload, input->preload_width, input->preload_height, load);
        auto& r = context->composite_result;
        output->struct_size = sizeof(*output);
        output->width = r.width; output->height = r.height; output->samples = r.samples;
        output->frame_id = input->frame_id; output->draw_id = input->draw_id;
        output->rgba16f_bytes = r.rgba16f_bytes.data(); output->byte_count = r.rgba16f_bytes.size();
        const auto& name = context->renderer->device_name();
        std::memcpy(output->device_name, name.data(), std::min(name.size(), sizeof(output->device_name)-1));
        ++context->counters.accepted;
        context->counters.last_frame_id = input->frame_id; context->counters.last_draw_id = input->draw_id;
        return status(error, BLUR_PLUME_OK);
    } catch (const std::exception& e) {
        context->faulted = true; ++context->counters.gpu_failures;
        return reject(BLUR_PLUME_GPU_FAILURE, e.what());
    } catch (...) {
        context->faulted = true; ++context->counters.gpu_failures;
        return reject(BLUR_PLUME_GPU_FAILURE, "Unknown GPU failure");
    }
}
BlurPlumeStatus stats(BlurPlumeContext* context, uint32_t output_size, BlurPlumeStats* output, BlurPlumeError* error) noexcept {
    if (!context || !output || output_size != sizeof(*output)) return status(error,BLUR_PLUME_INVALID_INPUT,"Stats layout mismatch");
    if (!owner(context)) return status(error,BLUR_PLUME_WRONG_THREAD,"Stats must use the creating thread");
    *output = context->counters;
    return status(error,BLUR_PLUME_OK);
}
BlurPlumeStatus destroy(BlurPlumeContext* context, BlurPlumeError* error) noexcept {
    if (!context) return status(error,BLUR_PLUME_INVALID_INPUT,"Missing context");
    if (!owner(context)) return status(error,BLUR_PLUME_WRONG_THREAD,"Destroy must use the creating thread");
    delete context;
    return status(error,BLUR_PLUME_OK);
}
}
extern "C" BlurPlumeStatus blur_plume_get_api(uint32_t version, uint32_t size, void* api_out) {
    if (!api_out) return BLUR_PLUME_INVALID_INPUT;
    if (version == BLUR_PLUME_ABI_V1) {
        if (size != sizeof(BlurPlumeApi)) return BLUR_PLUME_INVALID_INPUT;
        auto* api = static_cast<BlurPlumeApi*>(api_out);
        *api = {BLUR_PLUME_ABI_V1, sizeof(*api),
                BLUR_PLUME_CAP_ZERO_INITIALIZED_UI | BLUR_PLUME_CAP_PERSISTENT_TARGET,
                create, probe, submit, stats, destroy};
        return BLUR_PLUME_OK;
    } else if (version == BLUR_PLUME_ABI_V2) {
        if (size != sizeof(BlurPlumeApiV2)) return BLUR_PLUME_INVALID_INPUT;
        auto* api = static_cast<BlurPlumeApiV2*>(api_out);
        *api = {BLUR_PLUME_ABI_V2, sizeof(*api),
                BLUR_PLUME_CAP_ZERO_INITIALIZED_UI | BLUR_PLUME_CAP_PERSISTENT_TARGET | BLUR_PLUME_CAP_COMPOSITE,
                create, probe, submit, stats, destroy, probe_composite, submit_composite};
        return BLUR_PLUME_OK;
    }
    return BLUR_PLUME_ABI_MISMATCH;
}
