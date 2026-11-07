#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <hardware/audio_effect.h>
#include "v4a_defs.h"
#include "neon_math.h"

#define LOG_TAG "V4A_Bridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define SCRATCH_SAMPLE_CAPACITY (16384) // 8192 stereo frames = 32 KB

typedef struct {
    const struct effect_interface_s* itfe; // MUST BE FIRST FIELD
    effect_handle_t real_handle;
    bool is_float_mode;
    uint8_t access_mode;
    uint32_t sample_rate;
    uint32_t channels;
    int16_t* s16_scratch;
    bool is_active;
    bool is_processing;
} v4a_bridge_context_t;

// Real library handles and function pointers
static void* g_real_lib_handle = nullptr;
static audio_effect_library_t* g_real_lib = nullptr;

static bool load_real_library() {
    if (g_real_lib != nullptr) return true;

    for (int i = 0; REAL_V4A_LIB_PATHS[i] != nullptr; i++) {
        g_real_lib_handle = dlopen(REAL_V4A_LIB_PATHS[i], RTLD_NOW | RTLD_LOCAL);
        if (g_real_lib_handle != nullptr) {
            LOGI("Loaded real ViPER driver from: %s", REAL_V4A_LIB_PATHS[i]);
            break;
        }
    }

    if (g_real_lib_handle == nullptr) {
        LOGE("Failed to dlopen libv4a_fx_real.so from all known paths!");
        return false;
    }

    g_real_lib = (audio_effect_library_t*)dlsym(g_real_lib_handle, "AudioEffectLibraryInfo");
    if (g_real_lib == nullptr) {
        LOGE("Failed to find AudioEffectLibraryInfo in real driver: %s", dlerror());
        dlclose(g_real_lib_handle);
        g_real_lib_handle = nullptr;
        return false;
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

    // Passthrough mode if stream is already 16-bit PCM
    if (!ctx->is_float_mode) {
        return (*ctx->real_handle)->process(ctx->real_handle, inBuffer, outBuffer);
    }

    // 32-bit Float mode: Blazing fast ARM NEON conversion
    size_t total_samples = inBuffer->frameCount * ctx->channels;
    if (total_samples > SCRATCH_SAMPLE_CAPACITY) {
        total_samples = SCRATCH_SAMPLE_CAPACITY;
    }

    // 1. Float32 -> Int16 NEON down-conversion
    float_to_s16_neon(inBuffer->f32, ctx->s16_scratch, total_samples);

    // 2. Invoke real ViPER processing in 16-bit
    audio_buffer_t temp_buf;
    temp_buf.frameCount = inBuffer->frameCount;
    temp_buf.s16 = ctx->s16_scratch;
    int32_t ret = (*ctx->real_handle)->process(ctx->real_handle, &temp_buf, &temp_buf);

    // 3. Int16 -> Float32 NEON up-conversion
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
        case EFFECT_CMD_SET_CONFIG: {
            if (pCmdData == nullptr || cmdSize < sizeof(effect_config_t)) return -1;
            effect_config_t* cfg = (effect_config_t*)pCmdData;

            ctx->is_float_mode = (cfg->inputCfg.format == AUDIO_FORMAT_PCM_FLOAT);
            ctx->access_mode = cfg->outputCfg.accessMode;
            ctx->sample_rate = cfg->inputCfg.samplingRate;
            ctx->channels = (cfg->inputCfg.channels == AUDIO_CHANNEL_OUT_MONO) ? 1 : 2;

            LOGI("EFFECT_CMD_SET_CONFIG: rate=%u, channels=%u, is_float=%d",
                 ctx->sample_rate, ctx->channels, ctx->is_float_mode);

            // Translate config for real 16-bit driver
            effect_config_t real_cfg = *cfg;
            real_cfg.inputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
            real_cfg.outputCfg.format = AUDIO_FORMAT_PCM_16_BIT;

            int32_t ret = (*ctx->real_handle)->command(ctx->real_handle, cmdCode, sizeof(effect_config_t),
                                                       &real_cfg, replySize, pReplyData);
            if (pReplyData && replySize && *replySize >= sizeof(int32_t)) {
                *(int32_t*)pReplyData = 0; // Success to AudioFlinger
            }
            return ret;
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
            return (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
        }

        case EFFECT_CMD_DISABLE: {
            ctx->is_active = false;
            ctx->is_processing = false;
            return (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
        }

        case EFFECT_CMD_RESET: {
            ctx->is_processing = false;
            return (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
        }

        case EFFECT_CMD_GET_PARAM: {
            int32_t ret = (*ctx->real_handle)->command(ctx->real_handle, cmdCode, cmdSize, pCmdData, replySize, pReplyData);
            return ret;
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
    ctx->channels = 2;
    ctx->sample_rate = 48000;
    ctx->is_float_mode = true;
    ctx->is_active = false;
    ctx->is_processing = false;
    ctx->access_mode = EFFECT_BUFFER_ACCESS_WRITE;

    // Allocate fixed scratch buffer (32 KB) - zero malloc during processing
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
    LOGI("ViPER4Android 32-bit Float Bridge created successfully for session %d", sessionId);
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
    if (!load_real_library()) return -1;
    return g_real_lib->get_descriptor(uuid, pDescriptor);
}

// Exported standard symbol for AudioFlinger
extern "C" {
    audio_effect_library_t AudioEffectLibraryInfo = {
        AUDIO_EFFECT_LIBRARY_TAG,
        0x0100, // Version 1.0
        "ViPER4Android 32-bit Float NEON Bridge",
        "ViPER520 & devddd963",
        bridge_lib_create_effect,
        bridge_lib_release_effect,
        bridge_lib_get_descriptor
    };
}
