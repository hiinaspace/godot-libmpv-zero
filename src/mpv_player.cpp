#include "mpv_player.h"

#include "audio_bridge.h"
#include "mpv_core.h"
#include "video_output_backend.h"
#include "vk_video_output.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

MPVPlayer::MPVPlayer() :
		mpv_core(std::make_unique<libmpv_zero::MpvCore>()),
		audio_bridge(std::make_unique<libmpv_zero::AudioBridge>()) {
	_recreate_video_output_backend();
}

MPVPlayer::~MPVPlayer() = default;

void MPVPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_file", "path"), &MPVPlayer::load_file);
	ClassDB::bind_method(D_METHOD("play"), &MPVPlayer::play);
	ClassDB::bind_method(D_METHOD("pause"), &MPVPlayer::pause);
	ClassDB::bind_method(D_METHOD("stop"), &MPVPlayer::stop);
	ClassDB::bind_method(D_METHOD("seek", "seconds"), &MPVPlayer::seek);
	ClassDB::bind_method(D_METHOD("get_time_pos"), &MPVPlayer::get_time_pos);
	ClassDB::bind_method(D_METHOD("get_duration"), &MPVPlayer::get_duration);
	ClassDB::bind_method(D_METHOD("is_playing"), &MPVPlayer::is_playing);
	ClassDB::bind_method(D_METHOD("get_texture"), &MPVPlayer::get_texture);
	ClassDB::bind_method(D_METHOD("get_audio_channel_count"), &MPVPlayer::get_audio_channel_count);
	ClassDB::bind_method(D_METHOD("get_audio_stream_for_channel", "channel_index"), &MPVPlayer::get_audio_stream_for_channel);
	ClassDB::bind_method(D_METHOD("attach_audio_playback", "channel_index", "playback"), &MPVPlayer::attach_audio_playback);
	ClassDB::bind_method(D_METHOD("get_audio_diagnostics"), &MPVPlayer::get_audio_diagnostics);
	ClassDB::bind_method(D_METHOD("get_video_status"), &MPVPlayer::get_video_status);
	ClassDB::bind_method(D_METHOD("get_mpv_status"), &MPVPlayer::get_mpv_status);

	ADD_SIGNAL(MethodInfo("file_loaded"));
	ADD_SIGNAL(MethodInfo("playback_finished"));
	ADD_SIGNAL(MethodInfo("position_changed", PropertyInfo(Variant::FLOAT, "time")));
	ADD_SIGNAL(MethodInfo("video_size_changed", PropertyInfo(Variant::INT, "width"), PropertyInfo(Variant::INT, "height")));
	ADD_SIGNAL(MethodInfo("texture_changed"));
	ADD_SIGNAL(MethodInfo("audio_channels_changed", PropertyInfo(Variant::INT, "count")));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "video_status"), "", "get_video_status");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mpv_status"), "", "get_mpv_status");
}

void MPVPlayer::_ready() {
	_initialize_runtime();
	set_process(true);
}

void MPVPlayer::_process(double /*p_delta*/) {
	_sync_mpv_state();
}

void MPVPlayer::_exit_tree() {
	set_process(false);
	_shutdown_runtime();
	video_texture.unref();
}

void MPVPlayer::load_file(const String &p_path) {
	audio_bridge->clear_queued_audio();
	audio_bridge->set_playback_active(false);
	if (video_output_backend && !video_output_backend->is_ready_for_playback()) {
		pending_load_path = p_path;
		video_status = "waiting for video backend";
		return;
	}

	pending_load_path = "";
	pending_play = false;
	mpv_core->load_file(p_path);
}

void MPVPlayer::play() {
	audio_bridge->set_playback_active(true);
	if (video_output_backend && !video_output_backend->is_ready_for_playback()) {
		pending_play = true;
		video_status = "waiting for video backend";
		return;
	}

	mpv_core->play();
}

void MPVPlayer::pause() {
	mpv_core->pause();
	audio_bridge->set_playback_active(false);
}

void MPVPlayer::stop() {
	mpv_core->stop();
	audio_bridge->clear_queued_audio();
	audio_bridge->set_playback_active(false);
}

void MPVPlayer::seek(double p_seconds) {
	mpv_core->seek(p_seconds);
}

double MPVPlayer::get_time_pos() const {
	return mpv_core->get_time_pos();
}

double MPVPlayer::get_duration() const {
	return mpv_core->get_duration();
}

bool MPVPlayer::is_playing() const {
	return mpv_core->is_playing();
}

Ref<Texture2D> MPVPlayer::get_texture() const {
	if (video_texture.is_valid()) {
		return video_texture;
	}

	return video_output_backend ? video_output_backend->get_texture() : Ref<Texture2D>();
}

int MPVPlayer::get_audio_channel_count() const {
	return audio_bridge->get_audio_channel_count();
}

Ref<AudioStream> MPVPlayer::get_audio_stream_for_channel(int p_channel_index) const {
	return audio_bridge->get_audio_stream_for_channel(p_channel_index);
}

void MPVPlayer::attach_audio_playback(int p_channel_index, const Ref<AudioStreamGeneratorPlayback> &p_playback) {
	audio_bridge->set_channel_playback(p_channel_index, p_playback);
}

Dictionary MPVPlayer::get_audio_diagnostics() const {
	const libmpv_zero::AudioBridge::Diagnostics diagnostics = audio_bridge->get_diagnostics();

	Dictionary result;
	result["sample_rate"] = diagnostics.sample_rate;
	result["channel_count"] = diagnostics.channel_count;
	result["max_queued_frames"] = diagnostics.max_queued_frames;
	result["total_queued_frames"] = diagnostics.total_queued_frames;
	result["underrun_count"] = diagnostics.underrun_count;
	return result;
}

String MPVPlayer::get_video_status() const {
	if (video_output_backend) {
		return video_output_backend->get_status();
	}

	return video_status;
}

String MPVPlayer::get_mpv_status() const {
	return mpv_core ? mpv_core->get_status() : String("mpv unavailable");
}

void MPVPlayer::_on_video_texture_ready(const Ref<Texture2D> &p_texture) {
	video_texture = p_texture;
	video_status = "texture ready";
	emit_signal("texture_changed");

	int width = 0;
	int height = 0;
	if (mpv_core) {
		width = mpv_core->get_video_width();
		height = mpv_core->get_video_height();
	}
	if ((width <= 0 || height <= 0) && p_texture.is_valid()) {
		width = p_texture->get_width();
		height = p_texture->get_height();
	}
	if (width > 0 && height > 0) {
		if (width != last_video_width || height != last_video_height) {
			last_video_width = width;
			last_video_height = height;
			emit_signal("video_size_changed", width, height);
		}
	}
}

void MPVPlayer::_on_video_probe_failed(const String &p_reason) {
	video_status = p_reason;
	UtilityFunctions::push_error("MPVPlayer video probe failed: " + p_reason);
}

void MPVPlayer::_sync_mpv_state() {
	if (!mpv_core || !mpv_core->is_initialized()) {
		return;
	}

	const libmpv_zero::MpvCore::PollResult poll_result = mpv_core->poll();
	audio_bridge->flush_to_playbacks();
	if (video_output_backend) {
		video_output_backend->update();
		video_status = video_output_backend->get_status();
	}
	if (video_output_backend && video_output_backend->is_ready_for_playback()) {
		if (!pending_load_path.is_empty()) {
			const String deferred_path = pending_load_path;
			const bool deferred_play = pending_play;
			pending_load_path = "";
			pending_play = false;
			mpv_core->load_file(deferred_path);
			if (deferred_play) {
				mpv_core->play();
			}
		} else if (pending_play) {
			pending_play = false;
			mpv_core->play();
		}
	}

	if (poll_result.file_loaded) {
		audio_bridge->set_source_active(true);
		emit_signal("file_loaded");
	}
	if (poll_result.position_changed) {
		emit_signal("position_changed", mpv_core->get_time_pos());
	}
	if (poll_result.playback_finished || poll_result.eof_reached) {
		audio_bridge->set_playback_active(false);
		audio_bridge->set_source_active(false);
		if (!poll_result.playback_finished || mpv_core->get_playback_state() == libmpv_zero::MpvCore::PlaybackState::STOPPED) {
			emit_signal("playback_finished");
		}
	}
	if (poll_result.video_reconfigured || poll_result.file_loaded) {
		const int width = mpv_core->get_video_width();
		const int height = mpv_core->get_video_height();
		if (width > 0 && height > 0 && (width != last_video_width || height != last_video_height)) {
			last_video_width = width;
			last_video_height = height;
			emit_signal("video_size_changed", width, height);
		}
	}

	const double duration = mpv_core->get_duration();
	if (duration != last_known_duration) {
		last_known_duration = duration;
	}

	if (poll_result.failed) {
		UtilityFunctions::push_warning("MPVPlayer: " + poll_result.status);
	}
	if (audio_bridge->consume_configuration_changed()) {
		emit_signal("audio_channels_changed", audio_bridge->get_audio_channel_count());
	}
}

void MPVPlayer::_configure_mpv_core_for_backend() {
	if (!mpv_core) {
		return;
	}

	const libmpv_zero::MpvCore::VideoOutputMode output_mode = libmpv_zero::MpvCore::VideoOutputMode::LIBMPV;
	mpv_core->set_video_output_mode(output_mode);
}

bool MPVPlayer::_initialize_runtime() {
	_configure_mpv_core_for_backend();
	audio_bridge->reset();
	mpv_core->set_audio_callback(audio_bridge.get(), &libmpv_zero::AudioBridge::mpv_audio_callback);

	if (!mpv_core->initialize()) {
		UtilityFunctions::push_warning("MPVPlayer: " + mpv_core->get_status());
		return false;
	}

	UtilityFunctions::print("MPVPlayer: ", mpv_core->get_status());

	if (video_output_backend) {
		video_output_backend->attach(
				this,
				mpv_core.get(),
				callable_mp(this, &MPVPlayer::_on_video_texture_ready),
				callable_mp(this, &MPVPlayer::_on_video_probe_failed));
		video_status = video_output_backend->get_status();
	}

	return true;
}

void MPVPlayer::_shutdown_runtime() {
	if (video_output_backend) {
		video_output_backend->detach();
	}
	pending_load_path = "";
	pending_play = false;
	if (mpv_core) {
		mpv_core->shutdown();
	}
	if (audio_bridge) {
		audio_bridge->reset();
	}
}

void MPVPlayer::_recreate_video_output_backend() {
	video_output_backend = std::make_unique<libmpv_zero::VkVideoOutput>();
	video_status = "vulkan video backend selected";
}
