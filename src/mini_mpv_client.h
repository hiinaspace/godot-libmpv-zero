#pragma once

#include <cstddef>
#include <cstdint>

struct mpv_handle;
struct mpv_event;
struct mpv_event_end_file;
struct mpv_event_property;

using mpv_format = int;
using mpv_event_id = int;
using mpv_end_file_reason = int;

constexpr mpv_format MPV_FORMAT_NONE = 0;
constexpr mpv_format MPV_FORMAT_STRING = 1;
constexpr mpv_format MPV_FORMAT_FLAG = 3;
constexpr mpv_format MPV_FORMAT_INT64 = 4;
constexpr mpv_format MPV_FORMAT_DOUBLE = 5;

constexpr mpv_event_id MPV_EVENT_NONE = 0;
constexpr mpv_event_id MPV_EVENT_SHUTDOWN = 1;
constexpr mpv_event_id MPV_EVENT_START_FILE = 6;
constexpr mpv_event_id MPV_EVENT_END_FILE = 7;
constexpr mpv_event_id MPV_EVENT_FILE_LOADED = 8;
constexpr mpv_event_id MPV_EVENT_IDLE = 11;
constexpr mpv_event_id MPV_EVENT_VIDEO_RECONFIG = 17;
constexpr mpv_event_id MPV_EVENT_AUDIO_RECONFIG = 18;
constexpr mpv_event_id MPV_EVENT_SEEK = 20;
constexpr mpv_event_id MPV_EVENT_PLAYBACK_RESTART = 21;
constexpr mpv_event_id MPV_EVENT_PROPERTY_CHANGE = 22;
constexpr mpv_event_id MPV_EVENT_QUEUE_OVERFLOW = 24;

constexpr mpv_end_file_reason MPV_END_FILE_REASON_EOF = 0;
constexpr mpv_end_file_reason MPV_END_FILE_REASON_STOP = 2;
constexpr mpv_end_file_reason MPV_END_FILE_REASON_QUIT = 3;
constexpr mpv_end_file_reason MPV_END_FILE_REASON_ERROR = 4;

struct mpv_event_property {
	const char *name;
	mpv_format format;
	void *data;
};

struct mpv_event_end_file {
	mpv_end_file_reason reason;
	int error;
	int64_t playlist_entry_id;
	int64_t playlist_insert_id;
	int playlist_insert_num_entries;
};

struct mpv_event {
	mpv_event_id event_id;
	int error;
	uint64_t reply_userdata;
	void *data;
};

struct mpv_godot_audio_frame {
	int format;
	int samples;
	int sample_rate;
	int channels;
	int64_t end_time_ns;
	const void *data;
	size_t size;
};

using mpv_godot_audio_callback_fn = void (*)(void *user_data, const mpv_godot_audio_frame *frame);

using PFN_mpv_create = mpv_handle *(*)();
using PFN_mpv_initialize = int (*)(mpv_handle *ctx);
using PFN_mpv_terminate_destroy = void (*)(mpv_handle *ctx);
using PFN_mpv_error_string = const char *(*)(int error);
using PFN_mpv_set_option_string = int (*)(mpv_handle *ctx, const char *name, const char *data);
using PFN_mpv_command = int (*)(mpv_handle *ctx, const char **args);
using PFN_mpv_set_property_string = int (*)(mpv_handle *ctx, const char *name, const char *data);
using PFN_mpv_get_property = int (*)(mpv_handle *ctx, const char *name, mpv_format format, void *data);
using PFN_mpv_get_property_string = char *(*)(mpv_handle *ctx, const char *name);
using PFN_mpv_free = void (*)(void *data);
using PFN_mpv_wait_event = mpv_event *(*)(mpv_handle *ctx, double timeout);
using PFN_mpv_godot_audio_set_callback = int (*)(mpv_handle *ctx, void *user_data, mpv_godot_audio_callback_fn cb);
