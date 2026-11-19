#include "mpv_core.h"

#include <cstdlib>
#include "mini_mpv_client.h"
#include "runtime_library_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>


using namespace godot;

namespace libmpv_zero {

namespace {

using Clock = std::chrono::steady_clock;

enum ObservedProperty : uint64_t { TIME_POS = 1, DURATION, WIDTH, HEIGHT, PAUSED, EOF_REACHED, BUFFERING, SPEED, TRACKS, SUBTITLE, SEEKABLE, PARTIALLY_SEEKABLE };
struct PropertyObservation { ObservedProperty id; const char *name; mpv_format format; };
constexpr PropertyObservation observed_properties[] = {
	{SEEKABLE, "seekable", MPV_FORMAT_FLAG},
	{PARTIALLY_SEEKABLE, "partially-seekable", MPV_FORMAT_FLAG},
	{TRACKS, "track-list", MPV_FORMAT_NODE}, {SUBTITLE, "sid", MPV_FORMAT_NODE},
	{TIME_POS, "time-pos", MPV_FORMAT_DOUBLE}, {DURATION, "duration", MPV_FORMAT_DOUBLE},
	{WIDTH, "width", MPV_FORMAT_INT64}, {HEIGHT, "height", MPV_FORMAT_INT64},
	{BUFFERING, "paused-for-cache", MPV_FORMAT_FLAG}, {SPEED, "speed", MPV_FORMAT_DOUBLE},
	{PAUSED, "pause", MPV_FORMAT_FLAG}, {EOF_REACHED, "eof-reached", MPV_FORMAT_FLAG},
};

struct MpvDispatch {
	void *library = nullptr;
	PFN_mpv_create create = nullptr;
	PFN_mpv_initialize initialize = nullptr;
	PFN_mpv_terminate_destroy terminate_destroy = nullptr;
	PFN_mpv_error_string error_string = nullptr;
	PFN_mpv_set_option_string set_option_string = nullptr;
	PFN_mpv_command_async command_async = nullptr;
	PFN_mpv_set_property_async set_property_async = nullptr;
	PFN_mpv_wait_event wait_event = nullptr;
	PFN_mpv_observe_property observe_property = nullptr;
	PFN_mpv_request_log_messages request_log_messages = nullptr;
	PFN_mpv_godot_audio_set_sink set_godot_audio_sink = nullptr;
};

MpvDispatch &get_dispatch() {
	static MpvDispatch dispatch;
	return dispatch;
}

int &get_dispatch_ref_count() {
	static int ref_count = 0;
	return ref_count;
}

String format_mpv_error(int p_error) {
	MpvDispatch &dispatch = get_dispatch();
	if (dispatch.error_string) {
		const char *message = dispatch.error_string(p_error);
		if (message) {
			return String(message);
		}
	}

	return vformat("mpv error %d", p_error);
}

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

bool load_mpv_dispatch(String &r_error) {
	MpvDispatch &dispatch = get_dispatch();
	if (dispatch.library) {
		return true;
	}

	String loaded_from;
	dispatch.library = open_mpv_runtime(loaded_from, r_error);
	if (!dispatch.library) {
		return false;
	}
	UtilityFunctions::print("libmpv runtime: " + loaded_from);

	dispatch.create = reinterpret_cast<PFN_mpv_create>(runtime_symbol(dispatch.library, "mpv_create"));
	dispatch.initialize = reinterpret_cast<PFN_mpv_initialize>(runtime_symbol(dispatch.library, "mpv_initialize"));
	dispatch.terminate_destroy = reinterpret_cast<PFN_mpv_terminate_destroy>(runtime_symbol(dispatch.library, "mpv_terminate_destroy"));
	dispatch.error_string = reinterpret_cast<PFN_mpv_error_string>(runtime_symbol(dispatch.library, "mpv_error_string"));
	dispatch.set_option_string = reinterpret_cast<PFN_mpv_set_option_string>(runtime_symbol(dispatch.library, "mpv_set_option_string"));
	dispatch.command_async = reinterpret_cast<PFN_mpv_command_async>(runtime_symbol(dispatch.library, "mpv_command_async"));
	dispatch.set_property_async = reinterpret_cast<PFN_mpv_set_property_async>(runtime_symbol(dispatch.library, "mpv_set_property_async"));
	dispatch.wait_event = reinterpret_cast<PFN_mpv_wait_event>(runtime_symbol(dispatch.library, "mpv_wait_event"));
	dispatch.observe_property = reinterpret_cast<PFN_mpv_observe_property>(runtime_symbol(dispatch.library, "mpv_observe_property"));
	dispatch.request_log_messages = reinterpret_cast<PFN_mpv_request_log_messages>(runtime_symbol(dispatch.library, "mpv_request_log_messages"));
	dispatch.set_godot_audio_sink = reinterpret_cast<PFN_mpv_godot_audio_set_sink>(runtime_symbol(dispatch.library, "mpv_godot_audio_set_sink"));

	if (!dispatch.create || !dispatch.initialize || !dispatch.terminate_destroy || !dispatch.error_string || !dispatch.set_option_string || !dispatch.command_async || !dispatch.wait_event || !dispatch.set_godot_audio_sink || !dispatch.set_property_async || !dispatch.observe_property || !dispatch.request_log_messages) {
		String missing;
		if (!dispatch.set_property_async) {
			missing += " mpv_set_property_async";
		}
		if (!dispatch.observe_property) {
			missing += " mpv_observe_property";
		}
		if (!dispatch.request_log_messages) {
			missing += " mpv_request_log_messages";
		}
		if (!dispatch.create) {
			missing += " mpv_create";
		}
		if (!dispatch.initialize) {
			missing += " mpv_initialize";
		}
		if (!dispatch.terminate_destroy) {
			missing += " mpv_terminate_destroy";
		}
		if (!dispatch.error_string) {
			missing += " mpv_error_string";
		}
		if (!dispatch.set_option_string) {
			missing += " mpv_set_option_string";
		}
		if (!dispatch.command_async) {
			missing += " mpv_command_async";
		}
		if (!dispatch.wait_event) {
			missing += " mpv_wait_event";
		}
		if (!dispatch.set_godot_audio_sink) {
			missing += " mpv_godot_audio_set_sink";
		}
		r_error = "failed to resolve required libmpv symbols from " + loaded_from + ":" + missing;
		close_runtime(dispatch.library);
		dispatch = MpvDispatch();
		return false;
	}

	return true;
}

void unload_mpv_dispatch() {
	MpvDispatch &dispatch = get_dispatch();
	if (dispatch.library) {
		close_runtime(dispatch.library);
	}
	dispatch = MpvDispatch();
}

} // namespace

void MpvCore::set_audio_sink(void *p_user_data, const mpv_godot_audio_sink *p_sink) {
	audio_sink_user_data = p_user_data;
	audio_sink = p_sink;
}

bool MpvCore::initialize() {
	if (initialized) {
		return true;
	}

	String error;
	if (!load_mpv_dispatch(error)) {
		status = error;
		return false;
	}

	MpvDispatch &dispatch = get_dispatch();
	handle = dispatch.create();
	if (!handle) {
		status = "mpv_create failed";
		return false;
	}

	dispatch.set_option_string(handle, "terminal", is_load_trace_enabled() ? "yes" : "no");
	dispatch.set_option_string(handle, "msg-level", is_load_trace_enabled() ? "all=v" : "all=warn");
	dispatch.set_option_string(handle, "idle", "yes");
	dispatch.set_option_string(handle, "rtsp-transport", "tcp");
	dispatch.set_option_string(handle, "keep-open", "yes");
	const char *video_output_name = video_output_mode == VideoOutputMode::LIBMPV ? "libmpv" : "null";
	dispatch.set_option_string(handle, "vo", video_output_name);
	dispatch.set_option_string(handle, "ao", "godot");
	dispatch.set_option_string(handle, "audio-format", "float");
	// The sample routes two screen speakers. Let mpv downmix center/surround
	// content instead of silently leaving those channels without consumers.
	dispatch.set_option_string(handle, "audio-channels", "stereo");
	// The render thread never waits for mpv's target time. Request frames when
	// due instead of immediately publishing frames rendered ahead of audio.
	dispatch.set_option_string(handle, "video-timing-offset", "0");
	dispatch.set_option_string(handle, "pause", "yes");
	// Pin decoding explicitly. mpv 0.42 no longer treats Vulkan hardware decode
	// as safe to auto-select, and the zero-copy render path has only been
	// verified with software-decoded frames. LIBMPV_ZERO_HWDEC overrides this
	// for experiments (for example "vulkan" or "auto-safe").
	{
		const char *hwdec = std::getenv("LIBMPV_ZERO_HWDEC");
		dispatch.set_option_string(handle, "hwdec", (hwdec && *hwdec) ? hwdec : "no");
	}

	if (dispatch.set_godot_audio_sink(handle, audio_sink_user_data, audio_sink) < 0) {
		dispatch.terminate_destroy(handle);
		handle = nullptr;
		status = "failed to configure godot audio sink";
		return false;
	}

	const int init_result = dispatch.initialize(handle);
	if (init_result < 0) {
		dispatch.terminate_destroy(handle);
		handle = nullptr;
		status = "mpv_initialize failed: " + format_mpv_error(init_result);
		return false;
	}

	// Synchronous property reads can block the same thread that must service
	// rendering. Observations arrive on the nonblocking client event queue.
	for (const auto &property : observed_properties) {
		if (dispatch.observe_property(handle, property.id, property.name, property.format) < 0) {
			dispatch.terminate_destroy(handle);
			handle = nullptr;
			status = "failed to observe mpv playback properties";
			return false;
		}
	}
	dispatch.request_log_messages(handle, "warn");
	observed_paused = true;
	initialized = true;
	get_dispatch_ref_count() += 1;
	file_loaded = false;
	eof_reached = false;
	seeking = false;
	loading = false;
	status = "libmpv initialized";
	return true;
}

void MpvCore::shutdown() {
	if (handle) {
		get_dispatch().terminate_destroy(handle);
		handle = nullptr;
		if (get_dispatch_ref_count() > 0) {
			get_dispatch_ref_count() -= 1;
		}
	}

	initialized = false;
	playback_state = PlaybackState::STOPPED;
	file_loaded = false;
	eof_reached = false;
	seeking = false;
	loading = false;
	loaded_path = "";
	time_pos = 0.0;
	duration = 0.0;
	seekable = false;
	partially_seekable = false;
	video_width = 0;
	video_height = 0;
	if (get_dispatch_ref_count() == 0) {
		unload_mpv_dispatch();
	}
	status = "mpv shut down";
}

void MpvCore::load_file(const String &p_path) {
	if (!initialized) {
		status = "load_file ignored because libmpv is not initialized";
		return;
	}

	subtitle_tracks.clear();
	subtitle_track = -1;
	const char *subtitle_auto[] = { "set", "sid", "auto", nullptr };
	get_dispatch().command_async(handle, 0, subtitle_auto);
	loaded_path = p_path;
	time_pos = 0.0;
	duration = 0.0;
	seekable = false;
	partially_seekable = false;
	video_width = 0;
	video_height = 0;
	playback_state = PlaybackState::STOPPED;
	file_loaded = false;
	eof_reached = false;
	seeking = false;
	loading = true;
	status = "loading file";

	CharString utf8_path = p_path.utf8();
	const char *command[] = { "loadfile", utf8_path.get_data(), nullptr };
	const Clock::time_point start = Clock::now();
	const int result = get_dispatch().command_async(handle, 0, command);
	const int64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
	if (result < 0) {
		loading = false;
		status = "loadfile failed: " + format_mpv_error(result);
		trace_load_log(vformat("loadfile command_async failed in %dms: %s", duration_ms, status));
		return;
	}
	status = "loadfile queued";
	trace_load_log(vformat("loadfile command_async queued in %dms", duration_ms));
}

void MpvCore::play() {
	if (!initialized) {
		status = "play ignored because libmpv is not initialized";
		return;
	}

	int pause_flag = 0;
	const int result = get_dispatch().set_property_async(handle, 0, "pause", MPV_FORMAT_FLAG, &pause_flag);
	if (result < 0) {
		status = "play failed: " + format_mpv_error(result);
		return;
	}

	if (file_loaded && !loading) {
		playback_state = PlaybackState::PLAYING;
	}
	status = "play issued";
}

void MpvCore::pause() {
	if (!initialized) {
		status = "pause ignored because libmpv is not initialized";
		return;
	}

	if (playback_state == PlaybackState::PLAYING) {
		int pause_flag = 1;
		const int result = get_dispatch().set_property_async(handle, 0, "pause", MPV_FORMAT_FLAG, &pause_flag);
		if (result < 0) {
			status = "pause failed: " + format_mpv_error(result);
			return;
		}
		playback_state = PlaybackState::PAUSED;
		status = "pause issued";
	}
}

void MpvCore::stop() {
	if (initialized) {
		const char *command[] = { "stop", nullptr };
		const int result = get_dispatch().command_async(handle, 0, command);
		if (result < 0) {
			status = "stop failed: " + format_mpv_error(result);
			return;
		}
	}
	time_pos = 0.0;
	playback_state = PlaybackState::STOPPED;
	file_loaded = false;
	eof_reached = false;
	seeking = false;
	loading = false;
	status = "stop issued";
}

bool MpvCore::set_speed(double p_speed) {
	if (!initialized || !std::isfinite(p_speed) || p_speed < 0.5 || p_speed > 2.0) return false;
	return get_dispatch().set_property_async(handle, 0, "speed", MPV_FORMAT_DOUBLE, &p_speed) >= 0;
}

bool MpvCore::set_subtitle_track(int64_t p_id) {
	if (!initialized || !file_loaded) return false;
	bool found = p_id == -1;
	for (int i = 0; i < subtitle_tracks.size(); ++i) {
		Dictionary track = subtitle_tracks[i];
		found |= int64_t(track["id"]) == p_id;
	}
	if (!found) return false;
	String value = p_id == -1 ? String("no") : String::num_int64(p_id);
	CharString utf8 = value.utf8();
	const char *command[] = { "set", "sid", utf8.get_data(), nullptr };
	return get_dispatch().command_async(handle, 0, command) >= 0;
}

void MpvCore::seek(double p_seconds) {
	if (!std::isfinite(p_seconds)) return;
	if (!initialized) {
		status = "seek ignored because libmpv is not initialized";
		return;
	}

	time_pos = std::max(0.0, p_seconds);
	seeking = true;
	const String seconds_string = String::num(time_pos);
	const CharString seconds_utf8 = seconds_string.utf8();
	const char *command[] = { "seek", seconds_utf8.get_data(), "absolute+exact", nullptr };
	const int result = get_dispatch().command_async(handle, 0, command);
	if (result < 0) {
		status = "seek failed: " + format_mpv_error(result);
		return;
	}
	status = "seek queued";
}

void MpvCore::set_video_output_mode(VideoOutputMode p_mode) {
	if (initialized) {
		return;
	}

	video_output_mode = p_mode;
}

MpvCore::PollResult MpvCore::poll() {
	PollResult result;
	result.status = status;

	if (!initialized) {
		return result;
	}

	mpv_event *event = nullptr;
	while ((event = get_dispatch().wait_event(handle, 0.0)) != nullptr) {
		if (event->event_id == MPV_EVENT_NONE) {
			break;
		}

		switch (event->event_id) {
			case MPV_EVENT_PROPERTY_CHANGE: {
				const auto *property = static_cast<const mpv_event_property *>(event->data);
				if (!property || !property->data) break;
				switch (event->reply_userdata) {
					case TRACKS: {
						subtitle_tracks.clear();
						const auto *node = static_cast<const mpv_node *>(property->data);
						if (property->format == MPV_FORMAT_NODE && node->format == MPV_FORMAT_NODE_ARRAY) {
							for (int i = 0; i < node->u.list->num; ++i) {
								const auto &entry = node->u.list->values[i];
								if (entry.format != MPV_FORMAT_NODE_MAP) continue;
								Dictionary track;
								for (int j = 0; j < entry.u.list->num; ++j) {
									String key(entry.u.list->keys[j]);
									const auto &v = entry.u.list->values[j];
									if (key == "id" && v.format == MPV_FORMAT_INT64) track[key] = v.u.int64;
									if ((key == "type" || key == "lang" || key == "title") && v.format == MPV_FORMAT_STRING) track[key] = String::utf8(v.u.string);
								}
								if (track.get("type", "") == Variant("sub") && track.has("id")) subtitle_tracks.append(track);
							}
						}
						result.tracks_changed = true;
						break;
					}
					case SUBTITLE: {
						const auto *node = static_cast<const mpv_node *>(property->data);
						subtitle_track = property->format == MPV_FORMAT_NODE && node->format == MPV_FORMAT_INT64 ? node->u.int64 : -1;
						result.tracks_changed = true;
						break;
					}
					case TIME_POS:
						if (property->format == MPV_FORMAT_DOUBLE) {
							const double value = *static_cast<double *>(property->data);
							result.position_changed |= time_pos != value;
							time_pos = value;
						}
						break;
					case DURATION:
						if (property->format == MPV_FORMAT_DOUBLE) duration = *static_cast<double *>(property->data);
						break;
					case WIDTH:
					case HEIGHT:
						if (property->format == MPV_FORMAT_INT64) {
							int &size = event->reply_userdata == WIDTH ? video_width : video_height;
							const int value = int(*static_cast<int64_t *>(property->data));
							result.video_reconfigured |= size != value;
							size = value;
						}
						break;
					case PARTIALLY_SEEKABLE:
						if (property->format == MPV_FORMAT_FLAG) partially_seekable = *static_cast<int *>(property->data) != 0;
						break;
					case SEEKABLE:
						if (property->format == MPV_FORMAT_FLAG) seekable = *static_cast<int *>(property->data) != 0;
						break;
					case BUFFERING:
						if (property->format == MPV_FORMAT_FLAG) buffering = *static_cast<int *>(property->data) != 0;
						break;
					case SPEED:
						if (property->format == MPV_FORMAT_DOUBLE) speed = *static_cast<double *>(property->data);
						break;
					case PAUSED:
						if (property->format == MPV_FORMAT_FLAG) observed_paused = *static_cast<int *>(property->data) != 0;
						break;
					case EOF_REACHED:
						if (property->format == MPV_FORMAT_FLAG) {
							const bool value = *static_cast<int *>(property->data) != 0;
							result.eof_reached |= value && !eof_reached;
							eof_reached = value;
						}
						break;
				}
			} break;
			case MPV_EVENT_LOG_MESSAGE: {
				const auto *message = static_cast<const mpv_event_log_message *>(event->data);
				if (message) {
					const String text = "mpv [" + String(message->prefix) + "]: " + String(message->text).strip_edges();
					if (message->log_level <= MPV_LOG_LEVEL_ERROR) {
						status = text;
						result.failed = true;
						result.status = status;
					} else {
						UtilityFunctions::push_warning(text);
					}
				}
			} break;
			case MPV_EVENT_COMMAND_REPLY:
			case MPV_EVENT_SET_PROPERTY_REPLY:
				if (event->error < 0) {
					status = "mpv command failed: " + format_mpv_error(event->error);
					result.failed = true;
					result.status = status;
				}
				break;
			case MPV_EVENT_START_FILE:
				subtitle_tracks.clear();
				subtitle_track = -1;
				result.tracks_changed = true;
				loading = true;
				status = "starting file";
				trace_load_log("event start_file");
				break;
			case MPV_EVENT_FILE_LOADED:
				file_loaded = true;
				eof_reached = false;
				seeking = false;
				loading = false;
				status = "file loaded";
				trace_load_log("event file_loaded");
				result.file_loaded = true;
				break;
			case MPV_EVENT_END_FILE: {
				subtitle_tracks.clear();
				subtitle_track = -1;
				result.tracks_changed = true;
				const mpv_event_end_file *end_file = static_cast<const mpv_event_end_file *>(event->data);
				const bool ended_at_eof = end_file && end_file->reason == MPV_END_FILE_REASON_EOF;
				playback_state = PlaybackState::STOPPED;
				file_loaded = false;
				eof_reached = ended_at_eof;
				seeking = false;
				time_pos = 0.0;
				video_width = 0;
				video_height = 0;
				if (end_file && end_file->reason == MPV_END_FILE_REASON_ERROR) {
					loading = false;
					status = "playback ended with error: " + format_mpv_error(end_file->error);
					result.failed = true;
					result.status = status;
				} else {
					status = ended_at_eof ? "playback finished" : "playback stopped";
				}
				trace_load_log("event end_file");
				// Replacing or stopping a file is not natural completion. In
				// keep-open mode, eof-reached supplies the completion notification.
				result.playback_finished = ended_at_eof;
			} break;
			case MPV_EVENT_SEEK:
				seeking = true;
				status = "seeking";
				break;
			case MPV_EVENT_PLAYBACK_RESTART:
				seeking = false;
				status = "playback restarted";
				result.playback_restarted = true;
				break;
			case MPV_EVENT_VIDEO_RECONFIG:
				result.video_reconfigured = true;
				break;
			case MPV_EVENT_AUDIO_RECONFIG:
				result.audio_reconfigured = true;
				break;
			case MPV_EVENT_QUEUE_OVERFLOW:
				status = "mpv event queue overflow";
				result.failed = true;
				result.status = status;
				break;
			case MPV_EVENT_SHUTDOWN:
				status = "mpv shutdown";
				initialized = false;
				playback_state = PlaybackState::STOPPED;
				break;
			default:
				break;
		}
	}

	const PlaybackState new_state = file_loaded && !eof_reached
		? (observed_paused ? PlaybackState::PAUSED : PlaybackState::PLAYING) : PlaybackState::STOPPED;
	if (new_state != playback_state) {
		playback_state = new_state;
		result.playback_state_changed = true;
	}

	if (!result.failed) {
		result.status = status;
	}
	return result;
}

const String &MpvCore::get_loaded_path() const {
	return loaded_path;
}

double MpvCore::get_time_pos() const {
	return time_pos;
}

double MpvCore::get_duration() const {
	return duration;
}

int MpvCore::get_video_width() const {
	return video_width;
}

int MpvCore::get_video_height() const {
	return video_height;
}

bool MpvCore::is_playing() const {
	return playback_state == PlaybackState::PLAYING;
}

bool MpvCore::is_seeking() const {
	return seeking;
}

bool MpvCore::is_loading() const {
	return loading;
}

MpvCore::PlaybackState MpvCore::get_playback_state() const {
	return playback_state;
}

const String &MpvCore::get_status() const {
	return status;
}

bool MpvCore::is_initialized() const {
	return initialized;
}

void *MpvCore::get_native_handle() const {
	return handle;
}

bool MpvCore::has_loaded_file() const {
	return file_loaded;
}

MpvCore::VideoOutputMode MpvCore::get_video_output_mode() const {
	return video_output_mode;
}

} // namespace libmpv_zero
