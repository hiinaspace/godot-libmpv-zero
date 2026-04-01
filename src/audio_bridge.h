#pragma once

#include <vector>

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/core/object.hpp>

namespace libmpv_zero {

class AudioBridge {
public:
	void configure_stereo_channels();
	int get_audio_channel_count() const;
	godot::Ref<godot::AudioStream> get_audio_stream_for_channel(int p_channel_index) const;

private:
	std::vector<godot::Ref<godot::AudioStreamGenerator>> channel_streams;
};

} // namespace libmpv_zero
