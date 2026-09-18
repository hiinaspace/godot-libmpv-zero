#pragma once

#include "mini_mpv_client.h"

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace libmpv_zero {

class MpvCore {
public:
	enum class VideoOutputMode {
		LIBMPV,
		NULL_OUTPUT,
	};

	struct PollResult {
		bool tracks_changed = false;
		bool file_loaded = false;
		bool playback_finished = false;
		bool position_changed = false;
		bool playback_state_changed = false;
		bool playback_restarted = false;
		bool video_reconfigured = false;
		bool audio_reconfigured = false;
		bool eof_reached = false;
		bool failed = false;
		godot::String status;
	};

	enum class PlaybackState {
		STOPPED,
		PLAYING,
		PAUSED,
	};

	godot::Array get_subtitle_tracks() const { return subtitle_tracks; }
	int64_t get_subtitle_track() const { return subtitle_track; }
	bool set_subtitle_track(int64_t p_id);
	void load_file(const godot::String &p_path);
	void play();
	void pause();
	void stop();
	void seek(double p_seconds);
	bool set_speed(double p_speed);
	double get_speed() const { return speed; }
	bool is_seekable() const { return seekable; }
	bool is_partially_seekable() const { return partially_seekable; }
	bool is_buffering() const { return buffering; }
	bool is_eof() const { return eof_reached; }
	void set_video_output_mode(VideoOutputMode p_mode);
	void set_audio_sink(void *p_user_data, const mpv_godot_audio_sink *p_sink);
	bool initialize();
	void shutdown();
	PollResult poll();

	const godot::String &get_loaded_path() const;
	double get_time_pos() const;
	double get_duration() const;
	int get_video_width() const;
	int get_video_height() const;
	bool is_playing() const;
	bool is_seeking() const;
	bool is_loading() const;
	PlaybackState get_playback_state() const;
	const godot::String &get_status() const;
	bool is_initialized() const;
	void *get_native_handle() const;
	bool has_loaded_file() const;
	VideoOutputMode get_video_output_mode() const;

private:
	godot::Array subtitle_tracks;
	int64_t subtitle_track = -1;
	mpv_handle *handle = nullptr;
	bool initialized = false;
	godot::String loaded_path;
	double time_pos = 0.0;
	double duration = 0.0;
	double speed = 1.0;
	bool buffering = false;
	bool seekable = false;
	bool partially_seekable = false;
	godot::String status = "mpv not initialized";
	PlaybackState playback_state = PlaybackState::STOPPED;
	bool file_loaded = false;
	bool eof_reached = false;
	bool observed_paused = true;
	bool seeking = false;
	bool loading = false;
	int video_width = 0;
	int video_height = 0;
	VideoOutputMode video_output_mode = VideoOutputMode::LIBMPV;
	void *audio_sink_user_data = nullptr;
	const mpv_godot_audio_sink *audio_sink = nullptr;
};

} // namespace libmpv_zero
