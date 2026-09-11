#include "audio_bridge.h"

#include <algorithm>
#include <cmath>

using namespace godot;

namespace libmpv_zero {

const mpv_godot_audio_sink AudioBridge::sink = {
	&AudioBridge::configure_sink, &AudioBridge::reset_sink,
	&AudioBridge::play_sink, &AudioBridge::write_sink, &AudioBridge::state_sink,
};

void AudioBridge::clear_locked() {
	++generation;
	sink_playing = false;
	underrun_count = 0;
	for (ChannelState &channel : channels) {
		channel.stream.unref();
		channel.read_index = 0;
		channel.queued = 0;
		channel.consumers = 0;
		channel.pull_calls = channel.pulled_frames = channel.consumed_frames = 0;
		channel.output_end = {};
	}
	// New playback objects flush Godot's resampler history after a seek. Older
	// playback objects can outlive a reset but cannot consume this generation.
	configuration_changed = true;
}

void AudioBridge::reset() {
	std::lock_guard<std::mutex> lock(mutex);
	clear_locked();
	channels.clear();
	capacity = 0;
}

int AudioBridge::configure_sink(void *p_ctx, int p_rate, int p_channels, int p_capacity) {
	if (p_rate <= 0 || p_channels <= 0 || p_channels > 64 || p_capacity <= 0) {
		return -1;
	}
	auto &bridge = *static_cast<AudioBridge *>(p_ctx);
	std::lock_guard<std::mutex> lock(bridge.mutex);
	bridge.clear_locked();
	bridge.channels.clear();
	bridge.sample_rate = p_rate;
	bridge.capacity = p_capacity;
	bridge.channels.resize(p_channels);
	for (ChannelState &channel : bridge.channels) {
		channel.samples.resize(p_capacity);
	}
	return 0;
}

void AudioBridge::reset_sink(void *p_ctx) {
	auto &bridge = *static_cast<AudioBridge *>(p_ctx);
	std::lock_guard<std::mutex> lock(bridge.mutex);
	bridge.clear_locked();
}

void AudioBridge::play_sink(void *p_ctx, int p_playing) {
	auto &bridge = *static_cast<AudioBridge *>(p_ctx);
	std::lock_guard<std::mutex> lock(bridge.mutex);
	bridge.sink_playing = p_playing != 0;
}

void AudioBridge::set_output_latency(double p_seconds) {
	std::lock_guard<std::mutex> lock(mutex);
	output_latency = std::isfinite(p_seconds) ? std::max(0.0, p_seconds) : 0.0;
}

void AudioBridge::set_channel_playing(int p_channel, uint64_t p_generation, bool p_playing) {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_generation != generation || p_channel < 0 || p_channel >= int(channels.size())) {
		return;
	}
	ChannelState &channel = channels[p_channel];
	if (p_playing) {
		// A newly attached channel joins the existing consumers' timeline.
		int queued = channel.queued;
		for (const ChannelState &other : channels) {
			if (other.consumers > 0 && other.consumed_frames > capacity) {
				queued = std::min(queued, other.queued);
			}
		}
		channel.read_index = (channel.read_index + channel.queued - queued) % capacity;
		channel.queued = queued;
		++channel.consumers;
	} else {
		channel.consumers = std::max(0, channel.consumers - 1);
	}
}

mpv_godot_audio_sink_state AudioBridge::state_locked() const {
	bool has_consumers = false;
	for (const ChannelState &channel : channels) {
		has_consumers |= channel.consumers > 0;
	}
	int queued = 0;
	double downstream = 0;
	const auto now = Clock::now();
	for (const ChannelState &channel : channels) {
		if (has_consumers && channel.consumers == 0) {
			continue;
		}
		queued = std::max(queued, channel.queued);
		downstream = std::max(downstream, std::chrono::duration<double>(channel.output_end - now).count());
	}
	mpv_godot_audio_sink_state state{};
	state.queued_frames = queued;
	// Avoid tiny writes while still permitting short final writes at EOF.
	state.free_frames = (capacity - queued) / 128 * 128;
	state.delay_seconds = double(queued) / sample_rate + downstream;
	state.playing = sink_playing && (queued > 0 || downstream > 0);
	return state;
}

void AudioBridge::state_sink(void *p_ctx, mpv_godot_audio_sink_state *p_state) {
	auto &bridge = *static_cast<AudioBridge *>(p_ctx);
	std::lock_guard<std::mutex> lock(bridge.mutex);
	*p_state = bridge.state_locked();
}

int AudioBridge::write_sink(void *p_ctx, const float *p_data, int p_frames) {
	auto &bridge = *static_cast<AudioBridge *>(p_ctx);
	std::lock_guard<std::mutex> lock(bridge.mutex);
	if (!p_data || p_frames < 0 || p_frames > bridge.state_locked().free_frames) {
		return -1;
	}
	for (size_t i = 0; i < bridge.channels.size(); ++i) {
		ChannelState &channel = bridge.channels[i];
		// Unrouted channels never hold up the audible ones; retain at most one
		// ring of recent PCM for later attachment. Active channels never drop.
		const int discard = std::max(0, channel.queued + p_frames - bridge.capacity);
		channel.read_index = (channel.read_index + discard) % bridge.capacity;
		channel.queued -= discard;
		for (int frame = 0; frame < p_frames; ++frame) {
			const size_t index = (channel.read_index + channel.queued + frame) % bridge.capacity;
			channel.samples[index] = p_data[frame * bridge.channels.size() + i];
		}
		channel.queued += p_frames;
	}
	return 0;
}

int AudioBridge::pull_channel_frames(int p_channel, uint64_t p_generation, AudioFrame *p_buffer, int p_frames) {
	std::fill_n(p_buffer, p_frames, AudioFrame{});
	std::lock_guard<std::mutex> lock(mutex);
	if (p_generation != generation || p_channel < 0 || p_channel >= int(channels.size())) {
		return p_frames;
	}
	ChannelState &channel = channels[p_channel];
	++channel.pull_calls;
	channel.pulled_frames += p_frames;
	if (!sink_playing) {
		return p_frames;
	}
	const int count = std::min(p_frames, channel.queued);
	for (int frame = 0; frame < count; ++frame) {
		const float sample = channel.samples[(channel.read_index + frame) % capacity];
		p_buffer[frame].left = sample;
		p_buffer[frame].right = sample;
	}
	channel.read_index = (channel.read_index + count) % capacity;
	channel.queued -= count;
	channel.consumed_frames += count;
	if (count > 0) {
		// Godot's reported device latency is an estimate, not a hardware cursor.
		// Include the current source block; validate end-to-end A/V separately.
		channel.output_end = Clock::now() + std::chrono::duration_cast<Clock::duration>(
			std::chrono::duration<double>(output_latency + double(count) / sample_rate));
	}
	if (count < p_frames) {
		++underrun_count; // Includes normal drain at EOF; the AO distinguishes it.
	}
	return p_frames;
}

AudioBridge::Diagnostics AudioBridge::get_diagnostics() const {
	std::lock_guard<std::mutex> lock(mutex);
	Diagnostics d;
	d.sample_rate = sample_rate;
	d.channel_count = int(channels.size());
	d.capacity_frames = capacity;
	d.generation = generation;
	d.underrun_count = underrun_count;
	d.estimated_delay_seconds = state_locked().delay_seconds;
	for (const ChannelState &channel : channels) {
		d.total_queued_frames += channel.queued;
		d.max_queued_frames = std::max(d.max_queued_frames, channel.queued);
		d.queued_frames_per_channel.push_back(channel.queued);
		d.pull_calls_per_channel.push_back(channel.pull_calls);
		d.pulled_frames_per_channel.push_back(channel.pulled_frames);
		d.consumed_frames_per_channel.push_back(channel.consumed_frames);
		d.playing_per_channel.push_back(channel.consumers > 0);
	}
	return d;
}

bool AudioBridge::consume_configuration_changed() {
	std::lock_guard<std::mutex> lock(mutex);
	const bool changed = configuration_changed;
	configuration_changed = false;
	return changed;
}

int AudioBridge::get_sample_rate() const {
	std::lock_guard<std::mutex> lock(mutex);
	return sample_rate;
}

int AudioBridge::get_audio_channel_count() const {
	std::lock_guard<std::mutex> lock(mutex);
	return int(channels.size());
}

Ref<AudioStream> AudioBridge::get_audio_stream_for_channel(int p_channel) {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_channel < 0 || p_channel >= int(channels.size())) {
		return {};
	}
	ChannelState &channel = channels[p_channel];
	if (channel.stream.is_null()) {
		// Called on the main thread. The mpv/AO and mixer callbacks never create
		// Godot resources or allocate PCM storage during normal playback.
		channel.stream.instantiate();
		channel.stream->configure(weak_from_this(), p_channel, generation);
	}
	return channel.stream;
}

} // namespace libmpv_zero
