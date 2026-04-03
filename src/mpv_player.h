#pragma once

#include <memory>

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {
class AudioBridge;
class MpvCore;
class VideoOutputBackend;
} //namespace libmpv_zero

namespace godot {

class MPVTexture;

class MPVPlayer : public Node {
	GDCLASS(MPVPlayer, Node)

private:
	static void _bind_methods();

	void _apply_source_autoload();
	void _sync_output_texture();
	void _on_video_texture_ready(const Ref<Texture2D> &p_texture);
	void _on_video_probe_failed(const String &p_reason);
	void _sync_mpv_state();
	void _sync_audio_targets();
	void _configure_audio_target(Node *p_target, int p_channel_index);
	void _set_audio_target_stream(Node *p_target, const Ref<AudioStream> &p_stream, bool p_restart);
	void _set_audio_target_paused(Node *p_target, bool p_paused);
	void _stop_audio_target(Node *p_target);
	Node *_resolve_target(const NodePath &p_path) const;
	void _configure_mpv_core_for_backend();
	bool _initialize_runtime();
	void _shutdown_runtime();
	void _recreate_video_output_backend();

	std::unique_ptr<libmpv_zero::MpvCore> mpv_core;
	std::unique_ptr<libmpv_zero::AudioBridge> audio_bridge;
	std::unique_ptr<libmpv_zero::VideoOutputBackend> video_output_backend;
	Ref<Texture2D> video_texture;
	Ref<MPVTexture> output_texture;
	String source;
	String video_status = "idle";
	String pending_load_path;
	bool pending_play = false;
	bool autoplay = false;
	bool paused_requested = false;
	double last_known_duration = 0.0;
	int last_video_width = 0;
	int last_video_height = 0;
	NodePath left_audio_target_path;
	NodePath right_audio_target_path;

public:
	MPVPlayer();
	~MPVPlayer() override;

	void _ready() override;
	void _process(double p_delta) override;
	void _exit_tree() override;

	void set_source(const String &p_source);
	String get_source() const;
	void set_autoplay(bool p_autoplay);
	bool is_autoplay_enabled() const;
	void set_output_texture(const Ref<MPVTexture> &p_output_texture);
	Ref<MPVTexture> get_output_texture() const;
	void set_left_audio_target(const NodePath &p_path);
	NodePath get_left_audio_target() const;
	void set_right_audio_target(const NodePath &p_path);
	NodePath get_right_audio_target() const;
	void set_paused(bool p_paused);
	bool is_paused() const;
	void load(const String &p_source);
	void load_file(const String &p_path);
	void play();
	void pause();
	void stop();
	void seek(double p_seconds);
	double get_playback_position() const;
	double get_time_pos() const;
	double get_duration() const;
	int get_video_width() const;
	int get_video_height() const;
	bool is_playing() const;
	Ref<Texture2D> get_texture() const;
	int get_audio_channel_count() const;
	Ref<AudioStream> get_audio_stream_for_channel(int p_channel_index) const;
	Dictionary get_audio_diagnostics() const;
	String get_status() const;
	String get_video_status() const;
	String get_mpv_status() const;
};

} // namespace godot
