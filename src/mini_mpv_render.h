#pragma once

#include <cstddef>
#include <cstdint>

struct mpv_handle;
struct mpv_render_context;
struct mpv_render_param;

using mpv_render_param_type = int;
using mpv_render_update_fn = void (*)(void *cb_ctx);

constexpr mpv_render_param_type MPV_RENDER_PARAM_INVALID = 0;
constexpr mpv_render_param_type MPV_RENDER_PARAM_API_TYPE = 1;
constexpr mpv_render_param_type MPV_RENDER_PARAM_SW_SIZE = 17;
constexpr mpv_render_param_type MPV_RENDER_PARAM_SW_FORMAT = 18;
constexpr mpv_render_param_type MPV_RENDER_PARAM_SW_STRIDE = 19;
constexpr mpv_render_param_type MPV_RENDER_PARAM_SW_POINTER = 20;

constexpr uint64_t MPV_RENDER_UPDATE_FRAME = 1u << 0;

#define MPV_RENDER_API_TYPE_SW "sw"

struct mpv_render_param {
	mpv_render_param_type type;
	void *data;
};

using PFN_mpv_render_context_create = int (*)(mpv_render_context **res, mpv_handle *mpv, mpv_render_param *params);
using PFN_mpv_render_context_set_update_callback = void (*)(mpv_render_context *ctx, mpv_render_update_fn callback, void *callback_ctx);
using PFN_mpv_render_context_update = uint64_t (*)(mpv_render_context *ctx);
using PFN_mpv_render_context_render = int (*)(mpv_render_context *ctx, mpv_render_param *params);
using PFN_mpv_render_context_free = void (*)(mpv_render_context *ctx);
