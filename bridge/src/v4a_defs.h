#ifndef V4A_DEFS_H
#define V4A_DEFS_H

#include <hardware/audio_effect.h>

// ViPER4Android Effect UUID: 41d3c987-e6cf-11e3-a88a-11aba5d5c51b
static const effect_uuid_t V4A_FX_UUID = {
    0x41d3c987, 0xe6cf, 0x11e3, 0xa88a,
    { 0x11, 0xab, 0xa5, 0xd5, 0xc5, 0x1b }
};

// ViPER4Android Driver Status Query Codes (GET_PARAM)
// As queried by com.wstxda.viper4android / com.pittvandewitt.viperfx StatusDialog
enum {
    V4A_PARAM_GET_ENABLED                = 1,   // Enabled: 1 = Yes, 0 = No
    V4A_PARAM_GET_CONFIGURE              = 2,   // Status: 1 = Normal, 0 = Abnormal
    V4A_PARAM_GET_STREAMING              = 3,   // Processing: 1 = Yes, 0 = No
    V4A_PARAM_GET_SAMPLING_RATE          = 4,   // e.g. 48000
    V4A_PARAM_GET_CONVOLUTION_KERNEL_ID  = 5,
    V4A_PARAM_GET_DRIVER_VERSION_CODE    = 6,   // e.g. 2504
    V4A_PARAM_GET_DRIVER_VERSION_NAME    = 7,   // e.g. "2.5.0.4"
    V4A_PARAM_GET_DISABLE_REASON         = 8,   // 0 = NONE
    V4A_PARAM_GET_DISABLE_REASON_MESSAGE = 9,
    V4A_PARAM_GET_CONFIG                 = 10,  // 40 bytes config
    V4A_PARAM_GET_ARCHITECTURE           = 11   // "NEON"
};

// Paths to search for the original ViPER binary driver
static const char* REAL_V4A_LIB_PATHS[] = {
    "libv4a_fx_real.so",
    "/vendor/lib/soundfx/libv4a_fx_real.so",
    "/system/vendor/lib/soundfx/libv4a_fx_real.so",
    "/vendor/lib64/soundfx/libv4a_fx_real.so",
    "/system/vendor/lib64/soundfx/libv4a_fx_real.so",
    "/system/lib/soundfx/libv4a_fx_real.so",
    "/system/lib64/soundfx/libv4a_fx_real.so",
    "/data/adb/modules/ViPER4AndroidFX/system/vendor/lib/soundfx/libv4a_fx_real.so",
    "/data/adb/modules/ViPER4AndroidFX/vendor/lib/soundfx/libv4a_fx_real.so",
    nullptr
};

#endif // V4A_DEFS_H
