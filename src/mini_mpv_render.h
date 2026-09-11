#pragma once

#include <mpv/render.h>
#include <mpv/render_vk.h>

using PFN_mpv_render_context_create = decltype(&mpv_render_context_create);
using PFN_mpv_render_context_set_update_callback = decltype(&mpv_render_context_set_update_callback);
using PFN_mpv_render_context_update = decltype(&mpv_render_context_update);
using PFN_mpv_render_context_render = decltype(&mpv_render_context_render);
using PFN_mpv_render_context_free = decltype(&mpv_render_context_free);
