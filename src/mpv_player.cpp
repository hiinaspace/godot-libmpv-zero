#include "mpv_player.h"

#include "audio_bridge.h"
#include "mpv_core.h"
#include "mpv_texture.h"
#include "video_output_backend.h"
#include "vk_video_output.h"

#include <chrono>

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

using Clock = std::chrono::steady_clock;

bool is_load_trace_enabled() {
	static const bool enabled = []() {
		OS *os = OS::get_singleton();
		return os && os->get_environment("LIBMPV_ZERO_TRACE_LOAD") == "1";
	}();
	return enabled;
}

int64_t load_trace_millis_since_start() {
	static const Clock::time_point start = Clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void trace_load_log(const String &p_message) {
	if (!is_load_trace_enabled()) {
		return;
	}
	UtilityFunctions::print(vformat("[load-trace %dms] %s", load_trace_millis_since_start(), p_message));
}

} // namespace

MPVPlayer::MPVPlayer() :
		mpv_core(std::make_unique<libmpv_zero::MpvCore>()),
		audio_bridge(std::make_unique<libmpv_zero::AudioBridge>()) {
	_recreate_video_output_backend();
}

MPVPlayer::~MPVPlayer() = default;

void MPVPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source", "source"), &MPVPlayer::set_source);
	ClassDB::bind_method(D_METHOD("get_source"), &MPVPlayer::get_source);
	ClassDB::bind_method(D_METHOD("set_autoplay", "enabled"), &MPVPlayer::set_autoplay);
	ClassDB::bind_method(D_METHOD("is_autoplay_enabled"), &MPVPlayer::is_autoplay_enabled);
	ClassDB::bind_method(D_METHOD("set_output_texture", "output_texture"), &MPVPlayer::set_output_texture);
	ClassDB::bind_method(D_METHOD("get_output_texture"), &MPVPlayer::get_output_texture);
	ClassDB::bind_method(D_METHOD("set_left_audio_target", "path"), &MPVPlayer::set_left_audio_target);
	ClassDB::bind_method(D_METHOD("get_left_audio_target"), &MPVPlayer::get_left_audio_target);
	ClassDB::bind_method(D_METHOD("set_right_audio_target", "path"), &MPVPlayer::set_right_audio_target);
	ClassDB::bind_method(D_METHOD("get_right_audio_target"), &MPVPlayer::get_right_audio_target);
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &MPVPlayer::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &MPVPlayer::is_paused);
	ClassDB::bind_method(D_METHOD("load", "source"), &MPVPlayer::load);
	ClassDB::bind_method(D_METHOD("load_file", "path"), &MPVPlayer::load_file);
	ClassDB::bind_method(D_METHOD("play"), &MPVPlayer::play);
	ClassDB::bind_method(D_METHOD("pause"), &MPVPlayer::pause);
	ClassDB::bind_method(D_METHOD("stop"), &MPVPlayer::stop);
	ClassDB::bind_method(D_METHOD("seek", "seconds"), &MPVPlayer::seek);
	ClassDB::bind_method(D_METHOD("get_playback_position"), &MPVPlayer::get_playback_position);
	ClassDB::bind_method(D_METHOD("get_time_pos"), &MPVPlayer::get_time_pos);
	ClassDB::bind_method(D_METHOD("get_duration"), &MPVPlayer::get_duration);
	ClassDB::bind_method(D_METHOD("get_video_width"), &MPVPlayer::get_video_width);
	ClassDB::bind_method(D_METHOD("get_video_height"), &MPVPlayer::get_video_height);
	ClassDB::bind_method(D_METHOD("is_playing"), &MPVPlayer::is_playing);
	ClassDB::bind_method(D_METHOD("get_texture"), &MPVPlayer::get_texture);
	ClassDB::bind_method(D_METHOD("get_audio_channel_count"), &MPVPlayer::get_audio_channel_count);
	ClassDB::bind_method(D_METHOD("get_audio_stream_for_channel", "channel_index"), &MPVPlayer::get_audio_stream_for_channel);
	ClassDB::bind_method(D_METHOD("get_audio_diagnostics"), &MPVPlayer::get_audio_diagnostics);
	ClassDB::bind_method(D_METHOD("get_status"), &MPVPlayer::get_status);
	ClassDB::bind_method(D_METHOD("get_video_status"), &MPVPlayer::get_video_status);
	ClassDB::bind_method(D_METHOD("get_mpv_status"), &MPVPlayer::get_mpv_status);

	ADD_SIGNAL(MethodInfo("file_loaded"));
	ADD_SIGNAL(MethodInfo("playback_finished"));
	ADD_SIGNAL(MethodInfo("position_changed", PropertyInfo(Variant::FLOAT, "time")));
	ADD_SIGNAL(MethodInfo("video_size_changed", PropertyInfo(Variant::INT, "width"), PropertyInfo(Variant::INT, "height")));
	ADD_SIGNAL(MethodInfo("texture_changed"));
	ADD_SIGNAL(MethodInfo("audio_channels_changed", PropertyInfo(Variant::INT, "count")));
	ADD_SIGNAL(MethodInfo("playback_error", PropertyInfo(Variant::STRING, "message")));

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source"), "set_source", "get_source");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "is_autoplay_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "output_texture", PROPERTY_HINT_RESOURCE_TYPE, "MPVTexture"), "set_output_texture", "get_output_texture");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "left_audio_target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AudioStreamPlayer,AudioStreamPlayer3D,SteamAudioPlayer"), "set_left_audio_target", "get_left_audio_target");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "right_audio_target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AudioStreamPlayer,AudioStreamPlayer3D,SteamAudioPlayer"), "set_right_audio_target", "get_right_audio_target");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "set_paused", "is_paused");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "playback_position", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_playback_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_duration");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "video_width", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_video_width");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "video_height", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_video_height");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "playing", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "is_playing");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "status", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_status");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "video_status", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_video_status");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mpv_status", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_mpv_status");
}

void MPVPlayer::_ready() {
	_initialize_runtime();
	_sync_output_texture();
	_sync_audio_targets();
	_apply_source_autoload();
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

void MPVPlayer::set_source(const String &p_source) {
	source = p_source;
}

String MPVPlayer::get_source() const {
	return source;
}

void MPVPlayer::set_autoplay(bool p_autoplay) {
	autoplay = p_autoplay;
}

bool MPVPlayer::is_autoplay_enabled() const {
	return autoplay;
}

void MPVPlayer::set_output_texture(const Ref<MPVTexture> &p_output_texture) {
	output_texture = p_output_texture;
	_sync_output_texture();
}

Ref<MPVTexture> MPVPlayer::get_output_texture() const {
	return output_texture;
}

void MPVPlayer::set_left_audio_target(const NodePath &p_path) {
	left_audio_target_path = p_path;
	_sync_audio_targets();
}

NodePath MPVPlayer::get_left_audio_target() const {
	return left_audio_target_path;
}

void MPVPlayer::set_right_audio_target(const NodePath &p_path) {
	right_audio_target_path = p_path;
	_sync_audio_targets();
}

NodePath MPVPlayer::get_right_audio_target() const {
	return right_audio_target_path;
}

void MPVPlayer::set_paused(bool p_paused) {
	paused_requested = p_paused;
	if (!is_inside_tree() || !mpv_core || !mpv_core->is_initialized()) {
		return;
	}
	if (p_paused) {
		pause();
	} else if (mpv_core->has_loaded_file()) {
		play();
	}
}

bool MPVPlayer::is_paused() const {
	if (!mpv_core || !mpv_core->is_initialized()) {
		return paused_requested;
	}
	return mpv_core->get_playback_state() == libmpv_zero::MpvCore::PlaybackState::PAUSED;
}

void MPVPlayer::load(const String &p_source) {
	set_source(p_source);
	load_file(p_source);
}

void MPVPlayer::load_file(const String &p_path) {
	trace_load_log(vformat("MPVPlayer::load_file requested %s", p_path));
	source = p_path;
	audio_bridge->clear_queued_audio();
	audio_bridge->set_playback_active(false);
	paused_requested = false;
	_sync_output_texture();
	if (video_output_backend && !video_output_backend->is_ready_for_playback()) {
		pending_load_path = p_path;
		video_status = "waiting for video backend";
		return;
	}

	pending_load_path = "";
	pending_play = false;
	waiting_for_playback_restart = false;
	mpv_core->load_file(p_path);
}

void MPVPlayer::play() {
	paused_requested = false;
	audio_bridge->set_playback_active(true);
	_sync_output_texture();
	if ((video_output_backend && !video_output_backend->is_ready_for_playback()) || (mpv_core && mpv_core->is_loading())) {
		pending_play = true;
		video_status = mpv_core && mpv_core->is_loading() ? "waiting for file load" : "waiting for video backend";
		trace_load_log("MPVPlayer::play deferred");
		return;
	}

	trace_load_log("MPVPlayer::play issued");
	mpv_core->play();
	waiting_for_playback_restart = false;
	_sync_audio_targets();
}

void MPVPlayer::pause() {
	paused_requested = true;
	mpv_core->pause();
	audio_bridge->set_playback_active(false);
	_sync_output_texture();
	_sync_audio_targets();
}

void MPVPlayer::stop() {
	paused_requested = false;
	mpv_core->stop();
	audio_bridge->clear_queued_audio();
	audio_bridge->set_playback_active(false);
	audio_bridge->set_source_active(false);
	waiting_for_playback_restart = false;
	video_texture.unref();
	_sync_output_texture();
	_sync_audio_targets();
}

void MPVPlayer::seek(double p_seconds) {
	mpv_core->seek(p_seconds);
}

double MPVPlayer::get_playback_position() const {
	return get_time_pos();
}

double MPVPlayer::get_time_pos() const {
	return mpv_core->get_time_pos();
}

double MPVPlayer::get_duration() const {
	return mpv_core->get_duration();
}

int MPVPlayer::get_video_width() const {
	return last_video_width;
}

int MPVPlayer::get_video_height() const {
	return last_video_height;
}

bool MPVPlayer::is_playing() const {
	return mpv_core->is_playing();
}

Ref<Texture2D> MPVPlayer::get_texture() const {
	if (output_texture.is_valid()) {
		return output_texture;
	}
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

Dictionary MPVPlayer::get_audio_diagnostics() const {
	const libmpv_zero::AudioBridge::Diagnostics diagnostics = audio_bridge->get_diagnostics();

	Dictionary result;
	result["sample_rate"] = diagnostics.sample_rate;
	result["channel_count"] = diagnostics.channel_count;
	result["max_queued_frames"] = diagnostics.max_queued_frames;
	result["total_queued_frames"] = diagnostics.total_queued_frames;
	result["underrun_count"] = diagnostics.underrun_count;
	PackedInt32Array per_channel;
	for (int queued_frames : diagnostics.queued_frames_per_channel) {
		per_channel.push_back(queued_frames);
	}
	result["queued_frames_per_channel"] = per_channel;
	PackedInt32Array pull_calls;
	for (int pull_count : diagnostics.pull_calls_per_channel) {
		pull_calls.push_back(pull_count);
	}
	result["pull_calls_per_channel"] = pull_calls;
	PackedInt32Array pulled_frames;
	for (int frame_count : diagnostics.pulled_frames_per_channel) {
		pulled_frames.push_back(frame_count);
	}
	result["pulled_frames_per_channel"] = pulled_frames;
	PackedInt32Array consumed_frames;
	for (int frame_count : diagnostics.consumed_frames_per_channel) {
		consumed_frames.push_back(frame_count);
	}
	result["consumed_frames_per_channel"] = consumed_frames;
	Array playing_per_channel;
	for (bool channel_playing : diagnostics.playing_per_channel) {
		playing_per_channel.push_back(channel_playing);
	}
	result["playing_per_channel"] = playing_per_channel;
	return result;
}

String MPVPlayer::get_status() const {
	const String mpv_status = get_mpv_status();
	if (mpv_status.contains("failed") || mpv_status.contains("ignored because")) {
		return mpv_status;
	}
	return get_video_status();
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

void MPVPlayer::_apply_source_autoload() {
	if (source.is_empty()) {
		return;
	}

	load_file(source);
	if (autoplay && !paused_requested) {
		play();
	}
}

void MPVPlayer::_sync_output_texture() {
	if (output_texture.is_null()) {
		return;
	}

	output_texture->set_live_texture(video_texture);
	output_texture->set_playback_active(is_playing());
}

void MPVPlayer::_on_video_texture_ready(const Ref<Texture2D> &p_texture) {
	video_texture = p_texture;
	video_status = "texture ready";
	_sync_output_texture();
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
	if (output_texture.is_valid()) {
		output_texture->set_playback_active(false);
	}
	UtilityFunctions::push_error("MPVPlayer video probe failed: " + p_reason);
	emit_signal("playback_error", p_reason);
}

void MPVPlayer::_sync_mpv_state() {
	if (!mpv_core || !mpv_core->is_initialized()) {
		return;
	}

	const libmpv_zero::MpvCore::PollResult poll_result = mpv_core->poll();
	const bool transitioning_now = mpv_core->is_seeking() || mpv_core->is_loading() || waiting_for_playback_restart;
	if (mpv_core->is_loading()) {
		trace_load_log("sync: loading active");
	}
	if (video_output_backend && !transitioning_now) {
		const Clock::time_point step_start = Clock::now();
		video_output_backend->update();
		const int64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - step_start).count();
		if (duration_ms >= 5 && mpv_core->is_loading()) {
			trace_load_log(vformat("sync: backend_update took %dms", duration_ms));
		}
		video_status = video_output_backend->get_status();
	}
	if (video_output_backend && video_output_backend->is_ready_for_playback()) {
		if (!pending_load_path.is_empty()) {
			const String deferred_path = pending_load_path;
			const bool deferred_play = pending_play;
			pending_load_path = "";
			pending_play = false;
			trace_load_log("sync: issuing deferred load");
			mpv_core->load_file(deferred_path);
			if (deferred_play && !mpv_core->is_loading()) {
				trace_load_log("sync: issuing deferred play immediately");
				mpv_core->play();
			} else if (deferred_play) {
				trace_load_log("sync: keeping deferred play pending");
				pending_play = true;
			}
		} else if (pending_play && !mpv_core->is_loading()) {
			pending_play = false;
			trace_load_log("sync: issuing pending play");
			mpv_core->play();
			waiting_for_playback_restart = true;
		}
	}

	if (poll_result.file_loaded) {
		trace_load_log("sync: file_loaded");
		audio_bridge->set_source_active(true);
		emit_signal("file_loaded");
	}
	if (poll_result.position_changed) {
		emit_signal("position_changed", mpv_core->get_time_pos());
	}
	if (poll_result.playback_finished || poll_result.eof_reached) {
		audio_bridge->set_playback_active(false);
		audio_bridge->set_source_active(false);
		paused_requested = false;
		waiting_for_playback_restart = false;
		_sync_output_texture();
		_sync_audio_targets();
		if (!poll_result.playback_finished || mpv_core->get_playback_state() == libmpv_zero::MpvCore::PlaybackState::STOPPED) {
			emit_signal("playback_finished");
		}
	}
	if (poll_result.video_reconfigured || poll_result.file_loaded) {
		const Clock::time_point step_start = Clock::now();
		const int width = mpv_core->get_video_width();
		const int height = mpv_core->get_video_height();
		if (width > 0 && height > 0 && (width != last_video_width || height != last_video_height)) {
			last_video_width = width;
			last_video_height = height;
			emit_signal("video_size_changed", width, height);
		}
		const int64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - step_start).count();
		if (duration_ms >= 5 && is_load_trace_enabled()) {
			trace_load_log(vformat("sync: video size branch took %dms", duration_ms));
		}
	}

	if (poll_result.playback_restarted) {
		waiting_for_playback_restart = false;
	}

	if (!transitioning_now) {
		const Clock::time_point step_start = Clock::now();
		const double duration = mpv_core->get_duration();
		if (duration != last_known_duration) {
			last_known_duration = duration;
		}
		const int64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - step_start).count();
		if (duration_ms >= 5 && is_load_trace_enabled()) {
			trace_load_log(vformat("sync: get_duration took %dms", duration_ms));
		}
	}

	if (poll_result.failed) {
		UtilityFunctions::push_warning("MPVPlayer: " + poll_result.status);
		emit_signal("playback_error", poll_result.status);
	}
	if (audio_bridge->consume_configuration_changed()) {
		const Clock::time_point step_start = Clock::now();
		_sync_audio_targets();
		emit_signal("audio_channels_changed", audio_bridge->get_audio_channel_count());
		const int64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - step_start).count();
		if (duration_ms >= 5 && is_load_trace_enabled()) {
			trace_load_log(vformat("sync: audio_config branch took %dms", duration_ms));
		}
	}
	if (poll_result.playback_state_changed) {
		const Clock::time_point step_start = Clock::now();
		_sync_output_texture();
		_sync_audio_targets();
		const int64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - step_start).count();
		if (duration_ms >= 5 && is_load_trace_enabled()) {
			trace_load_log(vformat("sync: playback_state branch took %dms", duration_ms));
		}
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
	video_texture.unref();
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
	_sync_output_texture();
}

void MPVPlayer::_recreate_video_output_backend() {
	video_output_backend = std::make_unique<libmpv_zero::VkVideoOutput>();
	video_status = "vulkan video backend selected";
}

Node *MPVPlayer::_resolve_target(const NodePath &p_path) const {
	if (p_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	return get_node_or_null(p_path);
}

void MPVPlayer::_sync_audio_targets() {
	if (!is_inside_tree()) {
		return;
	}

	_configure_audio_target(_resolve_target(left_audio_target_path), 0);
	_configure_audio_target(_resolve_target(right_audio_target_path), 1);
}

void MPVPlayer::_configure_audio_target(Node *p_target, int p_channel_index) {
	if (p_target == nullptr) {
		return;
	}

	const int channel_count = audio_bridge ? audio_bridge->get_audio_channel_count() : 0;
	if (p_channel_index < 0 || p_channel_index >= channel_count) {
		_set_audio_target_stream(p_target, Ref<AudioStream>(), false);
		_stop_audio_target(p_target);
		return;
	}

	const Ref<AudioStream> stream = audio_bridge->get_audio_stream_for_channel(p_channel_index);
	_set_audio_target_stream(p_target, stream, is_playing());
	if (is_playing()) {
		_set_audio_target_paused(p_target, false);
	} else {
		_set_audio_target_paused(p_target, true);
	}
}

void MPVPlayer::_set_audio_target_stream(Node *p_target, const Ref<AudioStream> &p_stream, bool p_restart) {
	if (p_target == nullptr) {
		return;
	}

	const bool supports_play_stream = p_target->has_method("play_stream");
	const bool supports_set_stream = p_target->has_method("set_stream");

	if (p_stream.is_null()) {
		if (supports_set_stream && !supports_play_stream) {
			p_target->call("set_stream", p_stream);
		}
		return;
	}

	if (!p_restart) {
		if (supports_set_stream && !supports_play_stream) {
			p_target->call("set_stream", p_stream);
		}
		return;
	}

	if (supports_play_stream) {
		p_target->call("play_stream", p_stream, 0.0, 0.0, 1.0);
		return;
	}

	if (supports_set_stream) {
		p_target->call("set_stream", p_stream);
	}

	if (p_target->has_method("is_playing")) {
		const Variant value = p_target->call("is_playing");
		if (value.get_type() != Variant::NIL && (bool)value) {
			return;
		}
	}

	if (p_target->has_method("play")) {
		p_target->call("play");
	}
}

void MPVPlayer::_set_audio_target_paused(Node *p_target, bool p_paused) {
	if (p_target == nullptr) {
		return;
	}

	if (p_target->has_method("set_stream_paused")) {
		p_target->call("set_stream_paused", p_paused);
		return;
	}

	if (p_paused) {
		if (p_target->has_method("stop")) {
			p_target->call("stop");
		}
		return;
	}

	if (p_target->has_method("is_playing")) {
		const Variant value = p_target->call("is_playing");
		if (value.get_type() != Variant::NIL && (bool)value) {
			return;
		}
	}

	if (p_target->has_method("play")) {
		p_target->call("play");
	}
}

void MPVPlayer::_stop_audio_target(Node *p_target) {
	if (p_target && p_target->has_method("stop")) {
		p_target->call("stop");
	}
}
