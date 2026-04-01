#include "audio_bridge.h"

using namespace godot;

namespace libmpv_zero {

void AudioBridge::configure_stereo_channels() {
	channel_streams.clear();

	for (int i = 0; i < 2; ++i) {
		Ref<AudioStreamGenerator> stream;
		stream.instantiate();
		stream->set_mix_rate(48000.0);
		stream->set_buffer_length(0.25);
		channel_streams.push_back(stream);
	}
}

int AudioBridge::get_audio_channel_count() const {
	return static_cast<int>(channel_streams.size());
}

Ref<AudioStream> AudioBridge::get_audio_stream_for_channel(int p_channel_index) const {
	if (p_channel_index < 0 || p_channel_index >= static_cast<int>(channel_streams.size())) {
		return Ref<AudioStream>();
	}

	return channel_streams[static_cast<size_t>(p_channel_index)];
}

} // namespace libmpv_zero
