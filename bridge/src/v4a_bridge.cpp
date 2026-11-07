#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <android/log.h>
#include <hardware/audio_effect.h>
#include "v4a_defs.h"
#include "neon_math.h"

#define LOG_TAG "V4A_Bridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define SCRATCH_SAMPLE_CAPACITY (16384) // 8192 stereo frames = 32 KB

static inline uint64_t get_time_ms() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

typedef struct {
    const struct effect_interface_s* itfe; // MUST BE FIRST FIELD
    effect_handle_t real_handle;
    int32_t session_id;
    bool is_float_mode;
    uint8_t access_mode;
    uint32_t sample_rate;
    uint32_t channels;
    int16_t* s16_scratch;
    bool is_active;
    bool is_processing;
    uint64_t last_process_time_ms;
} v4a_bridge_context_t;

// Real library handles and function pointers
static void* g_real_lib_handle = nullptr;
static audio_effect_library_t* g_real_lib = nullptr;

static bool load_real_library() {
    if (g_real_lib != nullptr) return true;

    for (int i = 0; REAL_V4A_LIB_PATHS[i] != nullptr; i++) {
        g_real_lib_handle = dlopen(REAL_V4A_LIB_PATHS[i], RTLD_NOW | RTLD_LOCAL);
        if (g_real_lib_handle != nullptr) {
            LOGI("Successfully loaded real ViPER driver from: %s", REAL_V4A_LIB_PATHS[i]);
            break;
        }
    }

    if (g_real_lib_handle == nullptr) {
        LOGE("Failed to dlopen libv4a_fx_real.so! Error: %s", dlerror());
        return false;
    }

    // Try AELI first (standard symbol in ViPER4Android and AOSP HAL), then AudioEffectLibraryInfo
    g_real_lib = (audio_effect_library_t*)dlsym(g_real_lib_handle, "AELI");
    if (g_real_lib == nullptr) {
        g_real_lib = (audio_effect_library_t*)dlsym(g_real_lib_handle, "AudioEffectLibraryInfo");
    }

    if (g_real_lib == nullptr) {
        // Fall back to legacy entry points: EffectCreate, EffectRelease, EffectGetDescriptor
        auto fn_create = (int32_t (*)(const effect_uuid_t *, int32_t, int32_t, effect_handle_t *))
            dlsym(g_real_lib_handle, "EffectCreate");
        auto fn_release = (int32_t (*)(effect_handle_t))
            dlsym(g_real_lib_handle, "EffectRelease");
        auto fn_desc = (int32_t (*)(const effect_uuid_t *, effect_descriptor_t *))
            dlsym(g_real_lib_handle, "EffectGetDescriptor");

        if (fn_create && fn_release && fn_desc) {
            static audio_effect_library_t s_synth_lib = {
                AUDIO_EFFECT_LIBRARY_TAG,
                0x0100,
                "ViPER4Android Real (Legacy)",
                "ViPER520",
                fn_create,
                fn_release,
                fn_desc
            };
            g_real_lib = &s_synth_lib;
            LOGI("Loaded real ViPER driver via legacy symbols (EffectCreate / EffectRelease / EffectGetDescriptor)");
        } else {
            LOGE("Failed to find AELI, AudioEffectLibraryInfo, or legacy symbols in real driver: %s", dlerror());
            dlclose(g_real_lib_handle);
            g_real_lib_handle = nullptr;
            return false;
        }
    }

    return true;
}

// =============================================================================
// Interface Implementation: Process
// =============================================================================
static int32_t bridge_process(effect_handle_t self, audio_buffer_t *inBuffer, audio_buffer_t *outBuffer) {
    v4a_bridge_context_t* ctx = (v4a_bridge_context_t*)self;
    if (ctx == nullptr || inBuffer == nullptr || outBuffer == nullptr || inBuffer->raw == nullptr) {
        return -1;
    }

    if (!ctx->is_active) {
        ctx->is_processing = false;
        if (inBuffer->raw != outBuffer->raw) {
            size_t bytes = inBuffer->frameCount * ctx->channels * (ctx->is_float_mode ? sizeof(float) : sizeof(int16_t));
            memcpy(outBuffer->raw, inBuffer->raw, bytes);
        }
        return 0;
    }

    ctx->is_processing = true;
    ctx->last_process_time_ms = get_time_ms();

    // 1. Direct Passthrough mode if stream is 16-bit PCM (e.g. Session 0 Global Output)
    if (!ctx->is_float_mode) {
        return (*ctx->real_handle)->process(ctx->real_handle, inBuffer, outBuffer);
    }

    // 2. 32-bit Float mode: Dynamic SIMD conversion (e.g. Modern Per-App Track Session)
    size_t total_samples = inBuffer->frameCount * ctx->channels;
    if (total_samples > SCRATCH_SAMPLE_CAPACITY) {
        total_samples = SCRATCH_SAMPLE_CAPACITY;
    }

    // Float32 -> Int16 SIMD down-conversion with saturation clamping
    float_to_s16_neon(inBuffer->f32, ctx->s16_scratch, total_samples);

    // Invoke real ViPER processing in 16-bit
    audio_buffer_t temp_buf;
    temp_buf.frameCount = inBuffer->frameCount;
    temp_buf.s16 = ctx->s16_scratch;
    int32_t ret = (*ctx->real_handle)->process(ctx->real_handle, &temp_buf, &temp_buf);

    // Int16 -> Float32 SIMD up-conversion
    bool accumulate = (ctx->access_mode == EFFECT_BUFFER_ACCESS_ACCUMULATE);
    s16_to_float_neon(ctx->s16_scratch, outBuffer->f32, total_samples, accumulate);

    return ret;
}

// =============================================================================
// Interface Implementation: Command
// =============================================================================
static int32_t bridge_command(effect_handle_t self, uint32_t cmdCode, uint32_t cmdSize,
                              void *pCmdData, uint32_t *replySize, void *pReplyData) {
    v4a_bridge_context_t* ctx = (v4a_bridge_context_t*)self;
    if (ctx == nullptr) return -1;

    switch (cmdCode) {
        case EFFECT_CMD_INIT: {
            if (pReplyData && replySize && *replySize >= sizeof(int32_t)) {
                *(int32_t*)pReplyData = 0;
            }
            if (ctx->real_handle) {
                (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
                if (pReplyData && replySize && *replySize >= sizeof(int32_t)) {
                    *(int32_t*)pReplyData = 0;
                }
            }
            return 0;
        }

        case EFFECT_CMD_SET_CONFIG: {
            if (pCmdData == nullptr || cmdSize < sizeof(effect_config_t)) return -1;
            effect_config_t* cfg = (effect_config_t*)pCmdData;

            ctx->is_float_mode = (cfg->inputCfg.format == AUDIO_FORMAT_PCM_FLOAT);
            ctx->access_mode = cfg->outputCfg.accessMode;
            ctx->sample_rate = cfg->inputCfg.samplingRate ? cfg->inputCfg.samplingRate : 48000;
            ctx->channels = (cfg->inputCfg.channels == AUDIO_CHANNEL_OUT_MONO) ? 1 : 2;

            LOGI("EFFECT_CMD_SET_CONFIG: session=%d, rate=%u, channels=%u, is_float=%d",
                 ctx->session_id, ctx->sample_rate, ctx->channels, ctx->is_float_mode);

            if (!ctx->is_float_mode) {
                // Direct native 16-bit PCM pass through to real driver!
                (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
                if (pReplyData && replySize && *replySize >= sizeof(int32_t)) {
                    *(int32_t*)pReplyData = 0;
                }
                return 0; // Always report NO_ERROR to AudioFlinger
            } else {
                // Incoming is 32-bit Float (per-app track session or float session 0)
                // Configure real driver using internal 16-bit buffers
                effect_config_t real_cfg = *cfg;
                real_cfg.inputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
                real_cfg.outputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
                real_cfg.inputCfg.buffer.s16 = ctx->s16_scratch;
                real_cfg.outputCfg.buffer.s16 = ctx->s16_scratch;
                real_cfg.outputCfg.accessMode = EFFECT_BUFFER_ACCESS_WRITE;
                real_cfg.inputCfg.mask = 0x001F;
                real_cfg.outputCfg.mask = 0x001F;

                (*ctx->real_handle)->command(ctx->real_handle, cmdCode, sizeof(effect_config_t),
                                            &real_cfg, replySize, pReplyData);
                if (pReplyData && replySize && *replySize >= sizeof(int32_t)) {
                    *(int32_t*)pReplyData = 0;
                }
                return 0; // Always report NO_ERROR to AudioFlinger
            }
        }

        case EFFECT_CMD_GET_CONFIG: {
            int32_t ret = (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize,
                                                       pCmdData, replySize, pReplyData);
            if (pReplyData && replySize && *replySize >= sizeof(effect_config_t)) {
                effect_config_t* cfg = (effect_config_t*)pReplyData;
                if (ctx->is_float_mode) {
                    cfg->inputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
                    cfg->outputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
                }
            }
            return ret;
        }

        case EFFECT_CMD_ENABLE: {
            ctx->is_active = true;
            int32_t ret = (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
            if (pReplyData && replySize && *replySize >= sizeof(int32_t)) {
                *(int32_t*)pReplyData = 0;
            }
            return 0;
        }

        case EFFECT_CMD_DISABLE: {
            ctx->is_active = false;
            ctx->is_processing = false;
            int32_t ret = (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
            if (pReplyData && replySize && *replySize >= sizeof(int32_t)) {
                *(int32_t*)pReplyData = 0;
            }
            return 0;
        }

        case EFFECT_CMD_RESET: {
            ctx->is_processing = false;
            int32_t ret = (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
            if (pReplyData && replySize && *replySize >= sizeof(int32_t)) {
                *(int32_t*)pReplyData = 0;
            }
            return 0;
        }

        case EFFECT_CMD_GET_PARAM: {
            if (pCmdData == nullptr || cmdSize < sizeof(effect_param_t) || pReplyData == nullptr || replySize == nullptr) {
                return -EINVAL;
            }

            effect_param_t *pCmdParam = (effect_param_t *)pCmdData;
            effect_param_t *pReplyParam = (effect_param_t *)pReplyData;

            // Offset of parameter value in data[]: rounded up to 32-bit alignment
            uint32_t vOffset = ((pCmdParam->psize + sizeof(int32_t) - 1) / sizeof(int32_t)) * sizeof(int32_t);

            if (pCmdParam->psize >= sizeof(uint32_t)) {
                uint32_t paramCode = *(uint32_t *)pCmdParam->data;
                memcpy(pReplyParam, pCmdParam, sizeof(effect_param_t) + pCmdParam->psize);

                switch (paramCode) {
                    case V4A_PARAM_GET_ENABLED: { // 1
                        pReplyParam->status = 0;
                        pReplyParam->vsize = sizeof(int32_t);
                        *(int32_t *)(pReplyParam->data + vOffset) = ctx->is_active ? 1 : 0;
                        *replySize = sizeof(effect_param_t) + vOffset + pReplyParam->vsize;
                        return 0;
                    }
                    case V4A_PARAM_GET_CONFIGURE: { // 2: Status Normal (1) / Abnormal (0)
                        pReplyParam->status = 0;
                        pReplyParam->vsize = sizeof(int32_t);
                        *(int32_t *)(pReplyParam->data + vOffset) = 1; // Always Normal when initialized
                        *replySize = sizeof(effect_param_t) + vOffset + pReplyParam->vsize;
                        return 0;
                    }
                    case V4A_PARAM_GET_STREAMING: { // 3: Is Processing
                        bool is_proc = ctx->is_active && ((get_time_ms() - ctx->last_process_time_ms) < 5000);
                        pReplyParam->status = 0;
                        pReplyParam->vsize = sizeof(int32_t);
                        *(int32_t *)(pReplyParam->data + vOffset) = is_proc ? 1 : 0;
                        *replySize = sizeof(effect_param_t) + vOffset + pReplyParam->vsize;
                        return 0;
                    }
                    case V4A_PARAM_GET_SAMPLING_RATE: { // 4
                        pReplyParam->status = 0;
                        pReplyParam->vsize = sizeof(uint32_t);
                        *(uint32_t *)(pReplyParam->data + vOffset) = ctx->sample_rate ? ctx->sample_rate : 48000;
                        *replySize = sizeof(effect_param_t) + vOffset + pReplyParam->vsize;
                        return 0;
                    }
                    case V4A_PARAM_GET_DRIVER_VERSION_CODE: { // 6
                        pReplyParam->status = 0;
                        pReplyParam->vsize = sizeof(uint32_t);
                        *(uint32_t *)(pReplyParam->data + vOffset) = 2504;
                        *replySize = sizeof(effect_param_t) + vOffset + pReplyParam->vsize;
                        return 0;
                    }
                    case V4A_PARAM_GET_DRIVER_VERSION_NAME: { // 7
                        const char* ver = "2.5.0.4";
                        pReplyParam->status = 0;
                        pReplyParam->vsize = strlen(ver);
                        memcpy(pReplyParam->data + vOffset, ver, pReplyParam->vsize);
                        *replySize = sizeof(effect_param_t) + vOffset + pReplyParam->vsize;
                        return 0;
                    }
                    case V4A_PARAM_GET_DISABLE_REASON: { // 8
                        pReplyParam->status = 0;
                        pReplyParam->vsize = sizeof(int32_t);
                        *(int32_t *)(pReplyParam->data + vOffset) = 0; // NONE
                        *replySize = sizeof(effect_param_t) + vOffset + pReplyParam->vsize;
                        return 0;
                    }
                    case V4A_PARAM_GET_DISABLE_REASON_MESSAGE: { // 9
                        pReplyParam->status = 0;
                        pReplyParam->vsize = 0;
                        *replySize = sizeof(effect_param_t) + vOffset;
                        return 0;
                    }
                    case V4A_PARAM_GET_ARCHITECTURE: { // 11: NEON!
                        const char* arch = "NEON";
                        pReplyParam->status = 0;
                        pReplyParam->vsize = strlen(arch);
                        memcpy(pReplyParam->data + vOffset, arch, pReplyParam->vsize);
                        *replySize = sizeof(effect_param_t) + vOffset + pReplyParam->vsize;
                        return 0;
                    }
                    default:
                        break;
                }
            }

            // For all other parameter queries (Convolver, EQ curves, DDC, etc.), pass directly to real driver
            return (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
        }

        default:
            // Forward all other commands (SET_PARAM, Bass, Clarity, Convolver, EQ) directly
            return (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
    }
}

// =============================================================================
// Interface Implementation: Get Descriptor
// =============================================================================
static int32_t bridge_get_descriptor(effect_handle_t self, effect_descriptor_t *pDescriptor) {
    v4a_bridge_context_t* ctx = (v4a_bridge_context_t*)self;
    if (ctx == nullptr || ctx->real_handle == nullptr) return -1;
    return (*ctx->real_handle)->get_descriptor(ctx->real_handle, pDescriptor);
}

// Static function table
static const struct effect_interface_s g_bridge_interface = {
    bridge_process,
    bridge_command,
    bridge_get_descriptor,
    nullptr
};

// =============================================================================
// Library API Entry Points
// =============================================================================
static int32_t bridge_lib_create_effect(const effect_uuid_t *uuid, int32_t sessionId,
                                        int32_t ioId, effect_handle_t *pHandle) {
    if (!load_real_library()) return -1;

    v4a_bridge_context_t* ctx = (v4a_bridge_context_t*)calloc(1, sizeof(v4a_bridge_context_t));
    if (ctx == nullptr) return -1;

    ctx->itfe = &g_bridge_interface;
    ctx->session_id = sessionId;
    ctx->channels = 2;
    ctx->sample_rate = 48000;
    ctx->is_float_mode = false; // Default to 16-bit unless AudioFlinger specifies Float
    ctx->is_active = false;
    ctx->is_processing = false;
    ctx->access_mode = EFFECT_BUFFER_ACCESS_WRITE;
    ctx->last_process_time_ms = 0;

    // Allocate fixed scratch buffer (32 KB) for float translation
    ctx->s16_scratch = (int16_t*)malloc(SCRATCH_SAMPLE_CAPACITY * sizeof(int16_t));
    if (ctx->s16_scratch == nullptr) {
        free(ctx);
        return -1;
    }

    int32_t ret = g_real_lib->create_effect(uuid, sessionId, ioId, &ctx->real_handle);
    if (ret != 0 || ctx->real_handle == nullptr) {
        LOGE("Real driver failed to create effect: %d", ret);
        free(ctx->s16_scratch);
        free(ctx);
        return -1;
    }

    *pHandle = (effect_handle_t)ctx;
    LOGI("ViPER4Android Dynamic SIMD Bridge created successfully for session %d", sessionId);
    return 0;
}

static int32_t bridge_lib_release_effect(effect_handle_t handle) {
    v4a_bridge_context_t* ctx = (v4a_bridge_context_t*)handle;
    if (ctx == nullptr) return -1;

    if (g_real_lib != nullptr && ctx->real_handle != nullptr) {
        g_real_lib->release_effect(ctx->real_handle);
    }

    if (ctx->s16_scratch != nullptr) {
        free(ctx->s16_scratch);
    }
    free(ctx);
    return 0;
}

static int32_t bridge_lib_get_descriptor(const effect_uuid_t *uuid, effect_descriptor_t *pDescriptor) {
    if (pDescriptor == nullptr) return -1;

    // Fallback Descriptor matching modern ViPER/JamesDSP standards
    memset(pDescriptor, 0, sizeof(effect_descriptor_t));
    pDescriptor->uuid = V4A_FX_UUID;
    pDescriptor->apiVersion = 0x00020000;
    pDescriptor->flags = EFFECT_FLAG_OUTPUT_DIRECT | EFFECT_FLAG_INPUT_DIRECT |
                         EFFECT_FLAG_INSERT_LAST | EFFECT_FLAG_TYPE_INSERT;
    pDescriptor->cpuLoad = 8;
    pDescriptor->memoryUsage = 1;
    strncpy(pDescriptor->name, "ViPER4Android [2.5.0.4]", sizeof(pDescriptor->name) - 1);
    strncpy(pDescriptor->implementor, "ViPER.WYF", sizeof(pDescriptor->implementor) - 1);

    return 0;
}

// Exported symbols for Android EffectsFactory (AELI, AudioEffectLibraryInfo, and legacy C functions)
extern "C" {
    // 1. Primary AOSP HAL symbol: AELI
    __attribute__((visibility("default")))
    audio_effect_library_t AELI = {
        AUDIO_EFFECT_LIBRARY_TAG,
        EFFECT_LIBRARY_API_VERSION, // 0x00030000 (Major version 3, required by EffectsFactory)
        "ViPER4Android Dynamic SIMD Bridge",
        "ViPER520 & devddd963",
        bridge_lib_create_effect,
        bridge_lib_release_effect,
        bridge_lib_get_descriptor
    };

    // 2. Extended symbol: AudioEffectLibraryInfo
    __attribute__((visibility("default")))
    audio_effect_library_t AudioEffectLibraryInfo = {
        AUDIO_EFFECT_LIBRARY_TAG,
        EFFECT_LIBRARY_API_VERSION, // 0x00030000 (Major version 3, required by EffectsFactory)
        "ViPER4Android Dynamic SIMD Bridge",
        "ViPER520 & devddd963",
        bridge_lib_create_effect,
        bridge_lib_release_effect,
        bridge_lib_get_descriptor
    };

    // 3. Legacy C entry points queried by EffectsFactory when AELI lookup falls back
    __attribute__((visibility("default")))
    int32_t EffectCreate(const effect_uuid_t *uuid, int32_t sessionId, int32_t ioId, effect_handle_t *pHandle) {
        return bridge_lib_create_effect(uuid, sessionId, ioId, pHandle);
    }

    __attribute__((visibility("default")))
    int32_t EffectRelease(effect_handle_t handle) {
        return bridge_lib_release_effect(handle);
    }

    __attribute__((visibility("default")))
    int32_t EffectGetDescriptor(const effect_uuid_t *uuid, effect_descriptor_t *pDescriptor) {
        return bridge_lib_get_descriptor(uuid, pDescriptor);
    }

    // 4. ViPER version query helpers
    __attribute__((visibility("default")))
    uint32_t GetLibraryVersionMain() { return 2; }

    __attribute__((visibility("default")))
    uint32_t GetLibraryVersionSub() { return 5; }

    __attribute__((visibility("default")))
    uint32_t GetLibraryVersionExt() { return 0; }

    __attribute__((visibility("default")))
    uint32_t GetLibraryVersionBuild() { return 4; }

    __attribute__((visibility("default")))
    const char* GetLibraryVersionCodename() { return "ViPER520 & devddd963"; }
}
