#pragma once

#include <memory>

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_generator_playback.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {
class AudioBridge;
class MpvCore;
class VideoOutputBackend;
} //namespace libmpv_zero

namespace godot {

class MPVPlayer : public Node {
	GDCLASS(MPVPlayer, Node)

public:
	enum VideoBackendKind {
		VIDEO_BACKEND_SOFTWARE = 0,
		VIDEO_BACKEND_VULKAN = 1,
	};

private:
	static void _bind_methods();

	void _on_video_texture_ready(const Ref<Texture2D> &p_texture);
	void _on_video_probe_failed(const String &p_reason);
	void _sync_mpv_state();
	void _configure_mpv_core_for_backend();
	bool _initialize_runtime();
	void _shutdown_runtime();
	void _recreate_video_output_backend();

	std::unique_ptr<libmpv_zero::MpvCore> mpv_core;
	std::unique_ptr<libmpv_zero::AudioBridge> audio_bridge;
	std::unique_ptr<libmpv_zero::VideoOutputBackend> video_output_backend;
	Ref<Texture2D> video_texture;
	String video_status = "idle";
	double last_known_duration = 0.0;
	int last_video_width = 0;
	int last_video_height = 0;
	VideoBackendKind video_backend = VIDEO_BACKEND_SOFTWARE;

public:
	MPVPlayer();
	~MPVPlayer() override;

	void _ready() override;
	void _process(double p_delta) override;
	void _exit_tree() override;

	void load_file(const String &p_path);
	void play();
	void pause();
	void stop();
	void seek(double p_seconds);
	double get_time_pos() const;
	double get_duration() const;
	bool is_playing() const;
	Ref<Texture2D> get_texture() const;
	int get_audio_channel_count() const;
	Ref<AudioStream> get_audio_stream_for_channel(int p_channel_index) const;
	void attach_audio_playback(int p_channel_index, const Ref<AudioStreamGeneratorPlayback> &p_playback);
	String get_video_status() const;
	String get_mpv_status() const;
	void set_video_backend(int p_backend);
	int get_video_backend() const;
};

} // namespace godot

VARIANT_ENUM_CAST(godot::MPVPlayer::VideoBackendKind);
