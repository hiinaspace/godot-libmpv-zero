#pragma once

#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {

class MpvCore {
public:
	struct PollResult {
		bool file_loaded = false;
		bool playback_finished = false;
		bool position_changed = false;
		bool playback_state_changed = false;
		bool video_reconfigured = false;
		bool audio_reconfigured = false;
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
	bool initialize();
	void shutdown();
	PollResult poll();

	const godot::String &get_loaded_path() const;
	double get_time_pos() const;
	double get_duration() const;
	bool is_playing() const;
	PlaybackState get_playback_state() const;
	const godot::String &get_status() const;
	bool is_initialized() const;

private:
	bool initialized = false;
	godot::String loaded_path;
	double time_pos = 0.0;
	double duration = 0.0;
	godot::String status = "mpv not initialized";
	PlaybackState playback_state = PlaybackState::STOPPED;
	bool file_loaded = false;
};

} // namespace libmpv_zero
