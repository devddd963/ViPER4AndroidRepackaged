/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_AUDIO_EFFECT_H
#define ANDROID_AUDIO_EFFECT_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_FORMAT_PCM_16_BIT 0x1
#define AUDIO_FORMAT_PCM_FLOAT  0x5

#define AUDIO_CHANNEL_OUT_MONO   0x1
#define AUDIO_CHANNEL_OUT_STEREO 0x3

typedef struct effect_uuid_s {
    uint32_t timeLow;
    uint16_t timeMid;
    uint16_t timeHiAndVersion;
    uint16_t clockSeq;
    uint8_t  node[6];
} effect_uuid_t;

typedef struct effect_descriptor_s {
    effect_uuid_t type;
    effect_uuid_t uuid;
    uint32_t      apiVersion;
    uint32_t      flags;
    uint16_t      cpuLoad;
    uint16_t      memoryUsage;
    char          name[64];
    char          implementor[64];
} effect_descriptor_t;

#define EFFECT_FLAG_TYPE_INSERT     0x00000000
#define EFFECT_FLAG_INSERT_FIRST    0x00000100

typedef struct audio_buffer_s {
    size_t frameCount;
    union {
        void*    raw;
        int32_t* s32;
        int16_t* s16;
        uint8_t* u8;
        float*   f32;
    };
} audio_buffer_t;

typedef struct effect_buffer_config_s {
    union {
        audio_buffer_t buffer;
        void* raw;
    };
    uint32_t samplingRate;
    uint32_t channels;
    union {
        void* bufferProvider;
        void* reserved;
    };
    uint8_t format;
    uint8_t accessMode;
    uint16_t mask;
} effect_buffer_config_t;

typedef struct effect_config_s {
    effect_buffer_config_t inputCfg;
    effect_buffer_config_t outputCfg;
} effect_config_t;

enum {
    EFFECT_BUFFER_ACCESS_WRITE = 0,
    EFFECT_BUFFER_ACCESS_READ = 1,
    EFFECT_BUFFER_ACCESS_ACCUMULATE = 2
};

enum {
    EFFECT_CMD_INIT = 0,
    EFFECT_CMD_SET_CONFIG = 1,
    EFFECT_CMD_RESET = 2,
    EFFECT_CMD_ENABLE = 3,
    EFFECT_CMD_DISABLE = 4,
    EFFECT_CMD_SET_PARAM = 5,
    EFFECT_CMD_GET_PARAM = 6,
    EFFECT_CMD_SET_DEVICE = 7,
    EFFECT_CMD_SET_VOLUME = 8,
    EFFECT_CMD_SET_AUDIO_MODE = 9,
    EFFECT_CMD_SET_CONFIG_REVERSE = 10,
    EFFECT_CMD_SET_INPUT_DEVICE = 11,
    EFFECT_CMD_GET_CONFIG = 12,
    EFFECT_CMD_GET_CONFIG_REVERSE = 13,
    EFFECT_CMD_GET_FEATURE_SUPPORTED_CONFIGS = 14,
    EFFECT_CMD_GET_FEATURE_CONFIG = 15,
    EFFECT_CMD_SET_FEATURE_CONFIG = 16,
    EFFECT_CMD_OFFLOAD = 17,
    EFFECT_CMD_FIRST_PROPRIETARY = 0x10000
};

typedef struct effect_interface_s **effect_handle_t;

struct effect_interface_s {
    int32_t (*process)(effect_handle_t self, audio_buffer_t *inBuffer, audio_buffer_t *outBuffer);
    int32_t (*command)(effect_handle_t self, uint32_t cmdCode, uint32_t cmdSize, void *pCmdData, uint32_t *replySize, void *pReplyData);
    int32_t (*get_descriptor)(effect_handle_t self, effect_descriptor_t *pDescriptor);
    int32_t (*process_reverse)(effect_handle_t self, audio_buffer_t *inBuffer, audio_buffer_t *outBuffer);
};

#define AUDIO_EFFECT_LIBRARY_TAG ((('A') << 24) | (('O') << 16) | (('F') << 8) | ('X'))

typedef struct audio_effect_library_s {
    uint32_t tag;
    uint32_t version;
    const char *name;
    const char *implementor;
    int32_t (*create_effect)(const effect_uuid_t *uuid, int32_t sessionId, int32_t ioId, effect_handle_t *pHandle);
    int32_t (*release_effect)(effect_handle_t handle);
    int32_t (*get_descriptor)(const effect_uuid_t *uuid, effect_descriptor_t *pDescriptor);
} audio_effect_library_t;

#define AUDIO_EFFECT_LIBRARY_INFO_SYM AudioEffectLibraryInfo

#ifdef __cplusplus
}
#endif

#endif // ANDROID_AUDIO_EFFECT_H
