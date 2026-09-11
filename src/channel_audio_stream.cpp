#include "channel_audio_stream.h"

#include "audio_bridge.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void ChannelAudioStream::_bind_methods() {}

Ref<AudioStreamPlayback> ChannelAudioStream::_instantiate_playback() const {
	Ref<ChannelAudioStreamPlayback> playback;
	playback.instantiate();
	playback->configure(bridge.lock(), channel_index, generation);
	return playback;
}

void ChannelAudioStream::configure(const std::weak_ptr<libmpv_zero::AudioBridge> &p_bridge, int p_channel_index, uint64_t p_generation) {
	bridge = p_bridge;
	channel_index = p_channel_index;
	generation = p_generation;
}

int ChannelAudioStream::get_channel_index() const {
	return channel_index;
}

String ChannelAudioStream::_get_stream_name() const {
	return vformat("MPV Channel %d", channel_index);
}

double ChannelAudioStream::_get_length() const {
	return 0.0;
}

bool ChannelAudioStream::_is_monophonic() const {
	return true;
}

void ChannelAudioStreamPlayback::_bind_methods() {}

void ChannelAudioStreamPlayback::configure(const std::shared_ptr<libmpv_zero::AudioBridge> &p_bridge, int p_channel_index, uint64_t p_generation) {
	bridge = p_bridge;
	channel_index = p_channel_index;
	generation = p_generation;
}

int32_t ChannelAudioStreamPlayback::_mix_resampled(AudioFrame *p_buffer, int32_t p_frames) {
	if (!p_buffer || p_frames <= 0) {
		return 0;
	}

	for (int32_t frame_index = 0; frame_index < p_frames; ++frame_index) {
		p_buffer[frame_index].left = 0.0f;
		p_buffer[frame_index].right = 0.0f;
	}

	if (!is_active.load() || bridge == nullptr || channel_index < 0) {
		return p_frames;
	}

	return bridge->pull_channel_frames(channel_index, generation, p_buffer, p_frames);
}

float ChannelAudioStreamPlayback::_get_stream_sampling_rate() const {
	if (bridge == nullptr) {
		return 48000.0f;
	}

	return static_cast<float>(bridge->get_sample_rate());
}

void ChannelAudioStreamPlayback::_start(double /*p_from_pos*/) {
	if (!is_active.exchange(true) && bridge != nullptr && channel_index >= 0) {
		bridge->set_channel_playing(channel_index, generation, true);
	}
	begin_resample();
}

void ChannelAudioStreamPlayback::_stop() {
	if (is_active.exchange(false) && bridge != nullptr && channel_index >= 0) {
		bridge->set_channel_playing(channel_index, generation, false);
	}
}

bool ChannelAudioStreamPlayback::_is_playing() const {
	return is_active.load();
}

ChannelAudioStreamPlayback::~ChannelAudioStreamPlayback() {
	_stop();
}
