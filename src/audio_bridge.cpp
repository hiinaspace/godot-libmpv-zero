#include "audio_bridge.h"

#include <algorithm>

using namespace godot;

namespace libmpv_zero {

void AudioBridge::reset() {
	std::lock_guard<std::mutex> lock(mutex);
	channels.clear();
	sample_rate = 48000;
	configuration_changed = true;
	underrun_count = 0;
	source_active = false;
	playback_active = false;
	priming_complete = false;
}

void AudioBridge::clear_queued_audio() {
	std::lock_guard<std::mutex> lock(mutex);
	for (ChannelState &channel : channels) {
		channel.queued_samples.clear();
	}
	underrun_count = 0;
	source_active = false;
	playback_active = false;
	priming_complete = false;
}

void AudioBridge::set_source_active(bool p_active) {
	std::lock_guard<std::mutex> lock(mutex);
	source_active = p_active;
	if (!source_active) {
		priming_complete = false;
	}
}

void AudioBridge::set_playback_active(bool p_active) {
	std::lock_guard<std::mutex> lock(mutex);
	playback_active = p_active;
}

void AudioBridge::set_channel_playing(int p_channel_index, bool p_playing) {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_channel_index < 0 || p_channel_index >= static_cast<int>(channels.size())) {
		return;
	}

	channels[static_cast<size_t>(p_channel_index)].playing = p_playing;
	update_playback_active_locked();
}

void AudioBridge::note_channel_pull(int p_channel_index, int p_frame_count) {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_channel_index < 0 || p_channel_index >= static_cast<int>(channels.size()) || p_frame_count <= 0) {
		return;
	}

	ChannelState &channel = channels[static_cast<size_t>(p_channel_index)];
	channel.pull_calls += 1;
	channel.pulled_frames += p_frame_count;
}

void AudioBridge::reconfigure_locked(int p_channel_count, int p_sample_rate) {
	channels.clear();
	sample_rate = p_sample_rate;

	for (int i = 0; i < p_channel_count; ++i) {
		ChannelState channel;
		channel.stream.instantiate();
		channel.stream->configure(this, i);
		channels.push_back(channel);
	}

	configuration_changed = true;
	update_playback_active_locked();
	priming_complete = false;
}

void AudioBridge::update_playback_active_locked() {
	playback_active = false;
	for (const ChannelState &channel : channels) {
		if (channel.playing) {
			playback_active = true;
			return;
		}
	}
}

bool AudioBridge::is_primed_locked() const {
	if (channels.empty()) {
		return false;
	}

	for (const ChannelState &channel : channels) {
		if (static_cast<int>(channel.queued_samples.size()) < startup_buffer_frames) {
			return false;
		}
	}

	return true;
}

void AudioBridge::enqueue_interleaved_f32(const float *p_samples, int p_frame_count, int p_channel_count, int p_sample_rate) {
	if (!p_samples || p_frame_count <= 0 || p_channel_count <= 0 || p_sample_rate <= 0) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex);
	if (static_cast<int>(channels.size()) != p_channel_count || sample_rate != p_sample_rate) {
		reconfigure_locked(p_channel_count, p_sample_rate);
	}

	const size_t max_samples_per_channel = static_cast<size_t>(sample_rate) * 2;
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

int AudioBridge::pull_channel_frames(int p_channel_index, AudioFrame *p_buffer, int p_frame_count) {
	if (!p_buffer || p_frame_count <= 0) {
		return 0;
	}

	std::lock_guard<std::mutex> lock(mutex);
	if (p_channel_index < 0 || p_channel_index >= static_cast<int>(channels.size())) {
		return p_frame_count;
	}

	ChannelState &channel = channels[static_cast<size_t>(p_channel_index)];
	if (source_active && !priming_complete) {
		if (!is_primed_locked()) {
			for (int frame_index = 0; frame_index < p_frame_count; ++frame_index) {
				p_buffer[frame_index].left = 0.0f;
				p_buffer[frame_index].right = 0.0f;
			}
			return p_frame_count;
		}
		priming_complete = true;
	}

	const int queued_frames = static_cast<int>(channel.queued_samples.size());
	const int frames_to_mix = std::min(p_frame_count, queued_frames);
	for (int frame_index = 0; frame_index < frames_to_mix; ++frame_index) {
		const float sample = channel.queued_samples.front();
		channel.queued_samples.pop_front();
		p_buffer[frame_index].left = sample;
		p_buffer[frame_index].right = sample;
	}
	channel.consumed_frames += frames_to_mix;

	if (frames_to_mix < p_frame_count && playback_active && source_active) {
		underrun_count += 1;
	}

	for (int frame_index = frames_to_mix; frame_index < p_frame_count; ++frame_index) {
		p_buffer[frame_index].left = 0.0f;
		p_buffer[frame_index].right = 0.0f;
	}

	return p_frame_count;
}

AudioBridge::Diagnostics AudioBridge::get_diagnostics() const {
	std::lock_guard<std::mutex> lock(mutex);

	Diagnostics diagnostics;
	diagnostics.sample_rate = sample_rate;
	diagnostics.channel_count = static_cast<int>(channels.size());
	diagnostics.underrun_count = underrun_count;
	diagnostics.queued_frames_per_channel.reserve(channels.size());
	diagnostics.pull_calls_per_channel.reserve(channels.size());
	diagnostics.pulled_frames_per_channel.reserve(channels.size());
	diagnostics.consumed_frames_per_channel.reserve(channels.size());
	diagnostics.playing_per_channel.reserve(channels.size());
	for (const ChannelState &channel : channels) {
		const int queued_frames = static_cast<int>(channel.queued_samples.size());
		diagnostics.total_queued_frames += queued_frames;
		diagnostics.max_queued_frames = std::max(diagnostics.max_queued_frames, queued_frames);
		diagnostics.queued_frames_per_channel.push_back(queued_frames);
		diagnostics.pull_calls_per_channel.push_back(channel.pull_calls);
		diagnostics.pulled_frames_per_channel.push_back(channel.pulled_frames);
		diagnostics.consumed_frames_per_channel.push_back(channel.consumed_frames);
		diagnostics.playing_per_channel.push_back(channel.playing);
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
