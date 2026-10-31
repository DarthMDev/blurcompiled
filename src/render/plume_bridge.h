#ifndef BLUR_PLUME_BRIDGE_H
#define BLUR_PLUME_BRIDGE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* New native host ABI. No guest addresses are represented by these pointers.
 * All structures use the host C ABI; V1 supports 64-bit little-endian hosts.
 * Load the library with local symbol scope and keep it loaded until every
 * context is destroyed. Every context call must use its creating thread. */
#define BLUR_PLUME_ABI_V1 1u
#define BLUR_PLUME_ABI_V2 2u
#define BLUR_PLUME_CAP_ZERO_INITIALIZED_UI 1ull
#define BLUR_PLUME_CAP_PERSISTENT_TARGET 2ull
#define BLUR_PLUME_CAP_COMPOSITE 4ull
#define BLUR_PLUME_LOAD_CLEAR_ZERO 0u
#define BLUR_PLUME_LOAD_PRESERVE 1u

typedef uint32_t BlurPlumeStatus;
enum {
    BLUR_PLUME_OK = 0,
    BLUR_PLUME_INVALID_INPUT = 1,
    BLUR_PLUME_UNSUPPORTED = 2,
    BLUR_PLUME_WRONG_THREAD = 3,
    BLUR_PLUME_GPU_FAILURE = 4,
    BLUR_PLUME_INVALID_STATE = 5,
    BLUR_PLUME_ABI_MISMATCH = 6,
    BLUR_PLUME_INTERNAL_FAILURE = 7
};
typedef struct BlurPlumeContext BlurPlumeContext;
typedef struct BlurPlumeError {
    uint32_t code;
    char message[508];
} BlurPlumeError;
typedef struct BlurPlumeSubmission {
    uint32_t struct_size;
    uint32_t load_action;
    /* Host submission identities only; these do not establish guest events. */
    uint64_t frame_id;
    uint64_t draw_id;
    const uint8_t* packet;
    uint64_t packet_bytes;
} BlurPlumeSubmission;
typedef struct BlurPlumeOutput {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t samples;
    uint64_t frame_id;
    uint64_t draw_id;
    const uint8_t* rgba_samples;
    uint64_t byte_count;
    char device_name[128];
    /* Pixel-major, sample-index-major RGBA8; GPU-complete on success.
     * Borrowed until the next owner-thread submit (even a rejected submit)
     * or context destruction. Copy it before that boundary. */
} BlurPlumeOutput;
typedef struct BlurPlumeCompositeSubmission {
    uint32_t struct_size;
    uint32_t load_action;
    uint64_t frame_id;
    uint64_t draw_id;
    const uint8_t* packet;
    uint64_t packet_bytes;
    const uint8_t* preload_rgba16f;
    uint64_t preload_bytes;
    uint32_t preload_width;
    uint32_t preload_height;
} BlurPlumeCompositeSubmission;
typedef struct BlurPlumeCompositeOutput {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t samples;
    uint64_t frame_id;
    uint64_t draw_id;
    const uint8_t* rgba16f_bytes;
    uint64_t byte_count;
    char device_name[128];
} BlurPlumeCompositeOutput;
typedef struct BlurPlumeStats {
    uint64_t accepted;
    uint64_t rejected;
    uint64_t gpu_failures;
    uint64_t last_frame_id;
    uint64_t last_draw_id;
} BlurPlumeStats;

typedef struct BlurPlumeApi {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t capabilities;
    /* create and probe do not initialize Metal. No exceptions cross this ABI. */
    BlurPlumeStatus (*create)(BlurPlumeContext**, BlurPlumeError*);
    BlurPlumeStatus (*probe)(const uint8_t*, uint64_t, BlurPlumeError*);
    BlurPlumeStatus (*submit)(BlurPlumeContext*, const BlurPlumeSubmission*,
                             uint32_t output_size, BlurPlumeOutput*, BlurPlumeError*);
    BlurPlumeStatus (*stats)(BlurPlumeContext*, uint32_t stats_size, BlurPlumeStats*, BlurPlumeError*);
    BlurPlumeStatus (*destroy)(BlurPlumeContext*, BlurPlumeError*);
} BlurPlumeApi;

typedef struct BlurPlumeApiV2 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t capabilities;
    BlurPlumeStatus (*create)(BlurPlumeContext**, BlurPlumeError*);
    BlurPlumeStatus (*probe)(const uint8_t*, uint64_t, BlurPlumeError*);
    BlurPlumeStatus (*submit)(BlurPlumeContext*, const BlurPlumeSubmission*,
                             uint32_t output_size, BlurPlumeOutput*, BlurPlumeError*);
    BlurPlumeStatus (*stats)(BlurPlumeContext*, uint32_t stats_size, BlurPlumeStats*, BlurPlumeError*);
    BlurPlumeStatus (*destroy)(BlurPlumeContext*, BlurPlumeError*);
    BlurPlumeStatus (*probe_composite)(const uint8_t*, uint64_t, BlurPlumeError*);
    BlurPlumeStatus (*submit_composite)(BlurPlumeContext*, const BlurPlumeCompositeSubmission*,
                                        uint32_t output_size, BlurPlumeCompositeOutput*, BlurPlumeError*);
} BlurPlumeApiV2;

typedef BlurPlumeStatus (*BlurPlumeGetApi)(uint32_t requested_abi, uint32_t api_size, void*);
#if defined(__GNUC__)
__attribute__((visibility("default")))
#endif
BlurPlumeStatus blur_plume_get_api(uint32_t requested_abi, uint32_t api_size, void* api);

#ifdef __cplusplus
}
#endif
#endif
