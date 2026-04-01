#pragma once

#include "channel_audio_stream.h"
#include "mini_mpv_client.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>

namespace libmpv_zero {

class AudioBridge {
public:
	struct Diagnostics {
		int sample_rate = 48000;
		int channel_count = 0;
		int max_queued_frames = 0;
		int total_queued_frames = 0;
		int underrun_count = 0;
		std::vector<int> queued_frames_per_channel;
		std::vector<int> pull_calls_per_channel;
		std::vector<int> pulled_frames_per_channel;
		std::vector<int> consumed_frames_per_channel;
		std::vector<bool> playing_per_channel;
	};

	void reset();
	void clear_queued_audio();
	void set_source_active(bool p_active);
	void set_playback_active(bool p_active);
	void set_channel_playing(int p_channel_index, bool p_playing);
	void note_channel_pull(int p_channel_index, int p_frame_count);
	void enqueue_interleaved_f32(const float *p_samples, int p_frame_count, int p_channel_count, int p_sample_rate);
	int pull_channel_frames(int p_channel_index, godot::AudioFrame *p_buffer, int p_frame_count);
	bool consume_configuration_changed();
	int get_audio_channel_count() const;
	godot::Ref<godot::AudioStream> get_audio_stream_for_channel(int p_channel_index) const;
	Diagnostics get_diagnostics() const;

	static void mpv_audio_callback(void *p_user_data, const mpv_godot_audio_frame *p_frame);

private:
	struct ChannelState {
		godot::Ref<godot::ChannelAudioStream> stream;
		std::deque<float> queued_samples;
		bool playing = false;
		int pull_calls = 0;
		int pulled_frames = 0;
		int consumed_frames = 0;
	};

	void reconfigure_locked(int p_channel_count, int p_sample_rate);
	void update_playback_active_locked();
	bool is_primed_locked() const;

	mutable std::mutex mutex;
	std::vector<ChannelState> channels;
	int sample_rate = 48000;
	bool configuration_changed = false;
	int underrun_count = 0;
	bool source_active = false;
	bool playback_active = false;
	bool priming_complete = false;
	int startup_buffer_frames = 4800;
};

} // namespace libmpv_zero
