#pragma once

#include "channel_audio_stream.h"
#include "mini_mpv_client.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>

namespace libmpv_zero {

class AudioBridge : public std::enable_shared_from_this<AudioBridge> {
public:
	struct Diagnostics {
		int sample_rate = 48000;
		int channel_count = 0;
		int capacity_frames = 0;
		int max_queued_frames = 0;
		int total_queued_frames = 0;
		int underrun_count = 0;
		uint64_t generation = 0;
		double estimated_delay_seconds = 0;
		std::vector<int> queued_frames_per_channel;
		std::vector<int64_t> pull_calls_per_channel;
		std::vector<int64_t> pulled_frames_per_channel;
		std::vector<int64_t> consumed_frames_per_channel;
		std::vector<bool> playing_per_channel;
	};

	void reset(); // Only after mpv has stopped/joined its AO.
	void set_output_latency(double p_seconds);
	void set_targets_updating(bool p_updating);
	void set_channel_playing(int p_channel, uint64_t p_generation, bool p_playing);
	int pull_stereo_frames(uint64_t p_generation, godot::AudioFrame *p_buffer, int p_frames);
	int pull_channel_frames(int p_channel, uint64_t p_generation, godot::AudioFrame *p_buffer, int p_frames);
	bool consume_configuration_changed();
	int get_audio_channel_count() const;
	int get_sample_rate() const;
	godot::Ref<godot::AudioStream> get_audio_stream_for_channel(int p_channel);
	Diagnostics get_diagnostics() const;
	static const mpv_godot_audio_sink sink;

private:
	using Clock = std::chrono::steady_clock;
	struct ChannelState {
		godot::Ref<godot::ChannelAudioStream> stream;
		std::vector<float> samples;
		size_t read_index = 0;
		int queued = 0;
		int consumers = 0;
		int64_t pull_calls = 0;
		int64_t pulled_frames = 0;
		int64_t consumed_frames = 0;
		Clock::time_point output_end{};
	};

	static int configure_sink(void *p_ctx, int p_rate, int p_channels, int p_capacity);
	static void reset_sink(void *p_ctx);
	static void play_sink(void *p_ctx, int p_playing);
	static int write_sink(void *p_ctx, const float *p_data, int p_frames);
	static void state_sink(void *p_ctx, mpv_godot_audio_sink_state *p_state);
	void clear_locked();
	mpv_godot_audio_sink_state state_locked() const;

	mutable std::mutex mutex;
	std::vector<ChannelState> channels;
	godot::Ref<godot::ChannelAudioStream> stereo_stream;
	int sample_rate = 48000;
	int capacity = 0;
	uint64_t generation = 0;
	bool configuration_changed = false;
	int underrun_count = 0;
	bool sink_playing = false;
	bool targets_updating = false;
	double output_latency = 0;
};

} // namespace libmpv_zero
