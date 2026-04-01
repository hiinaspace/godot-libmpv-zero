#pragma once

#include <atomic>

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback_resampled.hpp>

namespace libmpv_zero {
class AudioBridge;
}

namespace godot {

class ChannelAudioStreamPlayback;

class ChannelAudioStream : public AudioStream {
	GDCLASS(ChannelAudioStream, AudioStream)
	friend class ChannelAudioStreamPlayback;

private:
	libmpv_zero::AudioBridge *bridge = nullptr;
	int channel_index = -1;

protected:
	static void _bind_methods();

public:
	ChannelAudioStream() = default;
	~ChannelAudioStream() override = default;

	Ref<AudioStreamPlayback> _instantiate_playback() const override;
	void configure(libmpv_zero::AudioBridge *p_bridge, int p_channel_index);
	int get_channel_index() const;
	String _get_stream_name() const override;
	double _get_length() const override;
	bool _is_monophonic() const override;
};

class ChannelAudioStreamPlayback : public AudioStreamPlaybackResampled {
	GDCLASS(ChannelAudioStreamPlayback, AudioStreamPlaybackResampled)

private:
	libmpv_zero::AudioBridge *bridge = nullptr;
	int channel_index = -1;
	std::atomic<bool> is_active{ false };

protected:
	static void _bind_methods();

public:
	ChannelAudioStreamPlayback() = default;
	~ChannelAudioStreamPlayback() override = default;

	void configure(libmpv_zero::AudioBridge *p_bridge, int p_channel_index);
	int32_t _mix_resampled(AudioFrame *p_buffer, int32_t p_frames) override;
	float _get_stream_sampling_rate() const override;
	void _start(double p_from_pos) override;
	void _stop() override;
	bool _is_playing() const override;
};

} // namespace godot
