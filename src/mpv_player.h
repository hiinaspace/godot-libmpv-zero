#pragma once

#include <memory>

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {
class AudioBridge;
class MpvCore;
class VideoOutputBackend;
}

namespace godot {

class MPVPlayer : public Node {
	GDCLASS(MPVPlayer, Node)

	static void _bind_methods();

	void _on_video_texture_ready(const Ref<Texture2D> &p_texture);
	void _on_video_probe_failed(const String &p_reason);

	std::unique_ptr<libmpv_zero::MpvCore> mpv_core;
	std::unique_ptr<libmpv_zero::AudioBridge> audio_bridge;
	std::unique_ptr<libmpv_zero::VideoOutputBackend> video_output_backend;
	Ref<Texture2D> video_texture;
	String video_status = "idle";

public:
	MPVPlayer();
	~MPVPlayer() override;

	void _ready() override;
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
	String get_video_status() const;
	String get_mpv_status() const;
};

} // namespace godot
