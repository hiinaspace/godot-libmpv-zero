#include "channel_audio_stream.h"

#include "audio_bridge.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void ChannelAudioStream::_bind_methods() {}

Ref<AudioStreamPlayback> ChannelAudioStream::_instantiate_playback() const {
	Ref<ChannelAudioStreamPlayback> playback;
	playback.instantiate();
	playback->configure(bridge, channel_index);
	return playback;
}

void ChannelAudioStream::configure(libmpv_zero::AudioBridge *p_bridge, int p_channel_index) {
	bridge = p_bridge;
	channel_index = p_channel_index;
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

void ChannelAudioStreamPlayback::configure(libmpv_zero::AudioBridge *p_bridge, int p_channel_index) {
	bridge = p_bridge;
	channel_index = p_channel_index;
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

	bridge->note_channel_pull(channel_index, p_frames);
	return bridge->pull_channel_frames(channel_index, p_buffer, p_frames);
}

float ChannelAudioStreamPlayback::_get_stream_sampling_rate() const {
	if (bridge == nullptr) {
		return 48000.0f;
	}

	return static_cast<float>(bridge->get_diagnostics().sample_rate);
}

void ChannelAudioStreamPlayback::_start(double /*p_from_pos*/) {
	is_active.store(true);
	begin_resample();
	if (bridge != nullptr && channel_index >= 0) {
		bridge->set_channel_playing(channel_index, true);
	}
}

void ChannelAudioStreamPlayback::_stop() {
	is_active.store(false);
	if (bridge != nullptr && channel_index >= 0) {
		bridge->set_channel_playing(channel_index, false);
	}
}

bool ChannelAudioStreamPlayback::_is_playing() const {
	return is_active.load();
}
