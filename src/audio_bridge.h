#pragma once

#include "mini_mpv_client.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/classes/audio_stream_generator_playback.hpp>

namespace libmpv_zero {

class AudioBridge {
public:
	struct Diagnostics {
		int sample_rate = 48000;
		int channel_count = 0;
		int max_queued_frames = 0;
		int total_queued_frames = 0;
		int underrun_count = 0;
	};

	void reset();
	void enqueue_interleaved_f32(const float *p_samples, int p_frame_count, int p_channel_count, int p_sample_rate);
	void flush_to_playbacks();
	bool consume_configuration_changed();
	int get_audio_channel_count() const;
	godot::Ref<godot::AudioStream> get_audio_stream_for_channel(int p_channel_index) const;
	void set_channel_playback(int p_channel_index, const godot::Ref<godot::AudioStreamGeneratorPlayback> &p_playback);
	Diagnostics get_diagnostics() const;

	static void mpv_audio_callback(void *p_user_data, const mpv_godot_audio_frame *p_frame);

private:
	struct ChannelState {
		godot::Ref<godot::AudioStreamGenerator> stream;
		godot::Ref<godot::AudioStreamGeneratorPlayback> playback;
		std::deque<float> queued_samples;
	};

	void reconfigure_locked(int p_channel_count, int p_sample_rate);

	mutable std::mutex mutex;
	std::vector<ChannelState> channels;
	int sample_rate = 48000;
	bool configuration_changed = false;
	int underrun_count = 0;
};

} // namespace libmpv_zero
