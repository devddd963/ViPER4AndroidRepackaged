#ifndef V4A_DEFS_H
#define V4A_DEFS_H

#include <hardware/audio_effect.h>

// ViPER4Android Effect UUID: 41d3c987-e6cf-11e3-a88a-11aba5d5c51b
static const effect_uuid_t V4A_FX_UUID = {
    0x41d3c987, 0xe6cf, 0x11e3, 0xa88a,
    { 0x11, 0xab, 0xa5, 0xd5, 0xc5, 0x1b }
};

// ViPER4Android Driver Status Query Codes (GET_PARAM)
enum {
    V4A_PARAM_GET_DRIVER_VERSION = 0x8001, // e.g. "2.5.0.4"
    V4A_PARAM_GET_ENABLED        = 0x8002, // 1 = Yes, 0 = No
    V4A_PARAM_GET_NEON           = 0x8003, // 1 = Yes, 0 = No
    V4A_PARAM_GET_SUPPORTED      = 0x8004, // 1 = Supported
    V4A_PARAM_GET_STATUS         = 0x8005, // 1 = Normal, 0 = Abnormal
    V4A_PARAM_GET_PROCESSING     = 0x8006, // 1 = Yes, 0 = No
    V4A_PARAM_GET_SAMPLING_RATE  = 0x8007  // e.g. 48000
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
