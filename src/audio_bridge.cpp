#include "audio_bridge.h"

#include <algorithm>

#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

namespace libmpv_zero {

void AudioBridge::reset() {
	std::lock_guard<std::mutex> lock(mutex);
	channels.clear();
	sample_rate = 48000;
	configuration_changed = true;
	underrun_count = 0;
}

void AudioBridge::reconfigure_locked(int p_channel_count, int p_sample_rate) {
	channels.clear();
	sample_rate = p_sample_rate;

	for (int i = 0; i < p_channel_count; ++i) {
		ChannelState channel;
		channel.stream.instantiate();
		channel.stream->set_mix_rate(static_cast<double>(sample_rate));
		channel.stream->set_buffer_length(0.25);
		channels.push_back(channel);
	}

	configuration_changed = true;
}

void AudioBridge::enqueue_interleaved_f32(const float *p_samples, int p_frame_count, int p_channel_count, int p_sample_rate) {
	if (!p_samples || p_frame_count <= 0 || p_channel_count <= 0 || p_sample_rate <= 0) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex);
	if (static_cast<int>(channels.size()) != p_channel_count || sample_rate != p_sample_rate) {
		reconfigure_locked(p_channel_count, p_sample_rate);
	}

	const size_t max_samples_per_channel = static_cast<size_t>(sample_rate);
	for (int frame_index = 0; frame_index < p_frame_count; ++frame_index) {
		for (int channel_index = 0; channel_index < p_channel_count; ++channel_index) {
			ChannelState &channel = channels[static_cast<size_t>(channel_index)];
			channel.queued_samples.push_back(p_samples[frame_index * p_channel_count + channel_index]);
			if (channel.queued_samples.size() > max_samples_per_channel) {
				channel.queued_samples.pop_front();
			}
		}
	}
}

void AudioBridge::flush_to_playbacks() {
	std::lock_guard<std::mutex> lock(mutex);

	for (ChannelState &channel : channels) {
		if (!channel.playback.is_valid()) {
			continue;
		}

		const int frames_available = channel.playback->get_frames_available();
		const int queued_frames = static_cast<int>(channel.queued_samples.size());
		const int frames_to_push = std::min(frames_available, queued_frames);
		if (frames_to_push <= 0) {
			if (frames_available > 0 && queued_frames == 0) {
				underrun_count += 1;
			}
			continue;
		}

		PackedVector2Array frames;
		frames.resize(frames_to_push);
		for (int i = 0; i < frames_to_push; ++i) {
			const float sample = channel.queued_samples.front();
			channel.queued_samples.pop_front();
			frames.set(i, Vector2(sample, sample));
		}

		channel.playback->push_buffer(frames);
	}
}

AudioBridge::Diagnostics AudioBridge::get_diagnostics() const {
	std::lock_guard<std::mutex> lock(mutex);

	Diagnostics diagnostics;
	diagnostics.sample_rate = sample_rate;
	diagnostics.channel_count = static_cast<int>(channels.size());
	diagnostics.underrun_count = underrun_count;
	for (const ChannelState &channel : channels) {
		const int queued_frames = static_cast<int>(channel.queued_samples.size());
		diagnostics.total_queued_frames += queued_frames;
		diagnostics.max_queued_frames = std::max(diagnostics.max_queued_frames, queued_frames);
	}

	return diagnostics;
}

bool AudioBridge::consume_configuration_changed() {
	std::lock_guard<std::mutex> lock(mutex);
	const bool changed = configuration_changed;
	configuration_changed = false;
	return changed;
}

int AudioBridge::get_audio_channel_count() const {
	std::lock_guard<std::mutex> lock(mutex);
	return static_cast<int>(channels.size());
}

Ref<AudioStream> AudioBridge::get_audio_stream_for_channel(int p_channel_index) const {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_channel_index < 0 || p_channel_index >= static_cast<int>(channels.size())) {
		return Ref<AudioStream>();
	}

	return channels[static_cast<size_t>(p_channel_index)].stream;
}

void AudioBridge::set_channel_playback(int p_channel_index, const Ref<AudioStreamGeneratorPlayback> &p_playback) {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_channel_index < 0 || p_channel_index >= static_cast<int>(channels.size())) {
		return;
	}

	channels[static_cast<size_t>(p_channel_index)].playback = p_playback;
}

void AudioBridge::mpv_audio_callback(void *p_user_data, const mpv_godot_audio_frame *p_frame) {
	if (!p_user_data || !p_frame || !p_frame->data || p_frame->samples <= 0 || p_frame->channels <= 0) {
		return;
	}

	AudioBridge *bridge = static_cast<AudioBridge *>(p_user_data);
	bridge->enqueue_interleaved_f32(
			static_cast<const float *>(p_frame->data),
			p_frame->samples,
			p_frame->channels,
			p_frame->sample_rate);
}

} // namespace libmpv_zero
