#include "mpv_player.h"

#include "audio_bridge.h"
#include "mpv_core.h"
#include "sw_video_output.h"
#include "video_output_backend.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

MPVPlayer::MPVPlayer() :
		mpv_core(std::make_unique<libmpv_zero::MpvCore>()),
		audio_bridge(std::make_unique<libmpv_zero::AudioBridge>()),
		video_output_backend(std::make_unique<libmpv_zero::SwVideoOutput>()) {
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
	ClassDB::bind_method(D_METHOD("get_video_status"), &MPVPlayer::get_video_status);
	ClassDB::bind_method(D_METHOD("get_mpv_status"), &MPVPlayer::get_mpv_status);

	ADD_SIGNAL(MethodInfo("file_loaded"));
	ADD_SIGNAL(MethodInfo("playback_finished"));
	ADD_SIGNAL(MethodInfo("position_changed", PropertyInfo(Variant::FLOAT, "time")));
	ADD_SIGNAL(MethodInfo("video_size_changed", PropertyInfo(Variant::INT, "width"), PropertyInfo(Variant::INT, "height")));
	ADD_SIGNAL(MethodInfo("audio_channels_changed", PropertyInfo(Variant::INT, "count")));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "video_status"), "", "get_video_status");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mpv_status"), "", "get_mpv_status");
}

void MPVPlayer::_ready() {
	audio_bridge->configure_stereo_channels();
	emit_signal("audio_channels_changed", audio_bridge->get_audio_channel_count());

	if (!mpv_core->initialize()) {
		UtilityFunctions::push_warning("MPVPlayer: " + mpv_core->get_status());
	}
	UtilityFunctions::print("MPVPlayer: ", mpv_core->get_status());

	video_output_backend->attach(
			this,
			mpv_core.get(),
			callable_mp(this, &MPVPlayer::_on_video_texture_ready),
			callable_mp(this, &MPVPlayer::_on_video_probe_failed));
	video_status = video_output_backend->get_status();
	set_process(true);
}

void MPVPlayer::_process(double /*p_delta*/) {
	_sync_mpv_state();
}

void MPVPlayer::_exit_tree() {
	set_process(false);
	if (video_output_backend) {
		video_output_backend->detach();
	}
	if (mpv_core) {
		mpv_core->shutdown();
	}
	video_texture.unref();
}

void MPVPlayer::load_file(const String &p_path) {
	mpv_core->load_file(p_path);
}

void MPVPlayer::play() {
	mpv_core->play();
}

void MPVPlayer::pause() {
	mpv_core->pause();
}

void MPVPlayer::stop() {
	mpv_core->stop();
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
		last_video_width = width;
		last_video_height = height;
		emit_signal("video_size_changed", width, height);
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
	if (video_output_backend) {
		video_output_backend->update();
		video_status = video_output_backend->get_status();
	}

	if (poll_result.file_loaded) {
		emit_signal("file_loaded");
	}
	if (poll_result.position_changed) {
		emit_signal("position_changed", mpv_core->get_time_pos());
	}
	if (poll_result.playback_finished) {
		emit_signal("playback_finished");
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
}
