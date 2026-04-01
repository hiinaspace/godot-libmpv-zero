#pragma once

#include <cstdint>

struct mpv_handle;
struct mpv_event;

using mpv_format = int;

constexpr mpv_format MPV_FORMAT_NONE = 0;
constexpr mpv_format MPV_FORMAT_STRING = 1;
constexpr mpv_format MPV_FORMAT_DOUBLE = 5;

using PFN_mpv_create = mpv_handle *(*)();
using PFN_mpv_initialize = int (*)(mpv_handle *ctx);
using PFN_mpv_terminate_destroy = void (*)(mpv_handle *ctx);
using PFN_mpv_set_option_string = int (*)(mpv_handle *ctx, const char *name, const char *data);
using PFN_mpv_command = int (*)(mpv_handle *ctx, const char **args);
using PFN_mpv_set_property_string = int (*)(mpv_handle *ctx, const char *name, const char *data);
using PFN_mpv_get_property = int (*)(mpv_handle *ctx, const char *name, mpv_format format, void *data);
using PFN_mpv_get_property_string = char *(*)(mpv_handle *ctx, const char *name);
using PFN_mpv_free = void (*)(void *data);
using PFN_mpv_wait_event = mpv_event *(*)(mpv_handle *ctx, double timeout);
