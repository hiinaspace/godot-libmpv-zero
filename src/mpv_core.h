#pragma once

#include "mini_mpv_client.h"

#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {

class MpvCore {
public:
	enum class VideoOutputMode {
		LIBMPV,
		NULL_OUTPUT,
	};

	struct PollResult {
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

	void load_file(const godot::String &p_path);
	void play();
	void pause();
	void stop();
	void seek(double p_seconds);
	void set_video_output_mode(VideoOutputMode p_mode);
	void set_audio_callback(void *p_user_data, mpv_godot_audio_callback_fn p_callback);
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
	bool initialized = false;
	godot::String loaded_path;
	double time_pos = 0.0;
	double duration = 0.0;
	godot::String status = "mpv not initialized";
	PlaybackState playback_state = PlaybackState::STOPPED;
	bool file_loaded = false;
	bool eof_reached = false;
	bool seeking = false;
	bool loading = false;
	bool awaiting_playback_restart = false;
	int video_width = 0;
	int video_height = 0;
	VideoOutputMode video_output_mode = VideoOutputMode::LIBMPV;
	void *audio_callback_user_data = nullptr;
	mpv_godot_audio_callback_fn audio_callback = nullptr;
};

} // namespace libmpv_zero
