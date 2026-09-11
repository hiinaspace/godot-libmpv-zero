#pragma once

#include <mpv/client.h>
#include <mpv/godot_audio.h>

using PFN_mpv_create = decltype(&mpv_create);
using PFN_mpv_initialize = decltype(&mpv_initialize);
using PFN_mpv_terminate_destroy = decltype(&mpv_terminate_destroy);
using PFN_mpv_error_string = decltype(&mpv_error_string);
using PFN_mpv_set_option_string = decltype(&mpv_set_option_string);
using PFN_mpv_command_async = decltype(&mpv_command_async);
using PFN_mpv_set_property_async = decltype(&mpv_set_property_async);
using PFN_mpv_wait_event = decltype(&mpv_wait_event);
using PFN_mpv_godot_audio_set_sink = decltype(&mpv_godot_audio_set_sink);

using PFN_mpv_observe_property = decltype(&mpv_observe_property);
using PFN_mpv_request_log_messages = decltype(&mpv_request_log_messages);
