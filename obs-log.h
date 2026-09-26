#pragma once
/*
 * Logging shim.
 *
 * Inside OBS (OBS_BUILD defined) this is just obs-module.h, so blog() goes to
 * the OBS log as usual. In the standalone harness / CLI there is no libobs, so
 * blog() is redirected to stderr. Source files include this instead of
 * <obs-module.h> so the exact same translation units build both ways.
 */

#ifdef OBS_BUILD

#include <obs-module.h>

#else

#include <cstdio>

#define LOG_ERROR   100
#define LOG_WARNING 200
#define LOG_INFO    300
#define LOG_DEBUG   400

static inline const char* obs_log_level_name(int level) {
    switch (level) {
    case LOG_ERROR:   return "error";
    case LOG_WARNING: return "warning";
    case LOG_DEBUG:   return "debug";
    default:          return "info";
    }
}

#define blog(level, format, ...)                                     \
    do {                                                             \
        std::fprintf(stderr, "[%s] " format "\n",                    \
                     obs_log_level_name(level), ##__VA_ARGS__);      \
        std::fflush(stderr);                                         \
    } while (0)

#endif
