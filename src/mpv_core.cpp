#include "mpv_core.h"
#include "mini_mpv_client.h"

#include <algorithm>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/char_string.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace godot;

namespace libmpv_zero {

namespace {

struct MpvDispatch {
#ifdef _WIN32
	HMODULE library = nullptr;
#endif
	PFN_mpv_create create = nullptr;
	PFN_mpv_initialize initialize = nullptr;
	PFN_mpv_terminate_destroy terminate_destroy = nullptr;
	PFN_mpv_error_string error_string = nullptr;
	PFN_mpv_set_option_string set_option_string = nullptr;
	PFN_mpv_command command = nullptr;
	PFN_mpv_set_property_string set_property_string = nullptr;
	PFN_mpv_get_property get_property = nullptr;
	PFN_mpv_get_property_string get_property_string = nullptr;
	PFN_mpv_free free_fn = nullptr;
	PFN_mpv_wait_event wait_event = nullptr;
	PFN_mpv_godot_audio_set_callback set_godot_audio_callback = nullptr;
};

MpvDispatch &get_dispatch() {
	static MpvDispatch dispatch;
	return dispatch;
}

mpv_handle *&get_handle() {
	static mpv_handle *handle = nullptr;
	return handle;
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

bool get_mpv_double_property(const char *p_name, double &r_value) {
	MpvDispatch &dispatch = get_dispatch();
	mpv_handle *handle = get_handle();
	if (!dispatch.get_property || !handle) {
		return false;
	}

	double value = 0.0;
	if (dispatch.get_property(handle, p_name, MPV_FORMAT_DOUBLE, &value) >= 0) {
		r_value = value;
		return true;
	}

	return false;
}

bool get_mpv_flag_property(const char *p_name, bool &r_value) {
	MpvDispatch &dispatch = get_dispatch();
	mpv_handle *handle = get_handle();
	if (!dispatch.get_property || !handle) {
		return false;
	}

	int flag = 0;
	if (dispatch.get_property(handle, p_name, MPV_FORMAT_FLAG, &flag) >= 0) {
		r_value = flag != 0;
		return true;
	}

	return false;
}

bool get_mpv_int64_property(const char *p_name, int64_t &r_value) {
	MpvDispatch &dispatch = get_dispatch();
	mpv_handle *handle = get_handle();
	if (!dispatch.get_property || !handle) {
		return false;
	}

	int64_t value = 0;
	if (dispatch.get_property(handle, p_name, MPV_FORMAT_INT64, &value) >= 0) {
		r_value = value;
		return true;
	}

	return false;
}

bool load_mpv_dispatch(String &r_error) {
#ifdef _WIN32
	MpvDispatch &dispatch = get_dispatch();
	if (dispatch.library) {
		return true;
	}

	String dll_path = ProjectSettings::get_singleton()->globalize_path("res://bin/windows/libmpv-2.dll");
	String dll_dir = dll_path.get_base_dir();
	const Char16String dll_dir_utf16 = dll_dir.utf16();
	const Char16String dll_path_utf16 = dll_path.utf16();
	SetDllDirectoryW(reinterpret_cast<LPCWSTR>(dll_dir_utf16.get_data()));
	dispatch.library = LoadLibraryW(reinterpret_cast<LPCWSTR>(dll_path_utf16.get_data()));
	String loaded_from = dll_path;
	if (!dispatch.library) {
		dispatch.library = LoadLibraryW(L"libmpv-2.dll");
		loaded_from = "PATH/libmpv-2.dll";
	}
	if (!dispatch.library) {
		const DWORD error_code = GetLastError();
		r_error = vformat("failed to load libmpv-2.dll (Win32 error %d)", static_cast<int>(error_code));
		return false;
	}

	dispatch.create = reinterpret_cast<PFN_mpv_create>(GetProcAddress(dispatch.library, "mpv_create"));
	dispatch.initialize = reinterpret_cast<PFN_mpv_initialize>(GetProcAddress(dispatch.library, "mpv_initialize"));
	dispatch.terminate_destroy = reinterpret_cast<PFN_mpv_terminate_destroy>(GetProcAddress(dispatch.library, "mpv_terminate_destroy"));
	dispatch.error_string = reinterpret_cast<PFN_mpv_error_string>(GetProcAddress(dispatch.library, "mpv_error_string"));
	dispatch.set_option_string = reinterpret_cast<PFN_mpv_set_option_string>(GetProcAddress(dispatch.library, "mpv_set_option_string"));
	dispatch.command = reinterpret_cast<PFN_mpv_command>(GetProcAddress(dispatch.library, "mpv_command"));
	dispatch.set_property_string = reinterpret_cast<PFN_mpv_set_property_string>(GetProcAddress(dispatch.library, "mpv_set_property_string"));
	dispatch.get_property = reinterpret_cast<PFN_mpv_get_property>(GetProcAddress(dispatch.library, "mpv_get_property"));
	dispatch.get_property_string = reinterpret_cast<PFN_mpv_get_property_string>(GetProcAddress(dispatch.library, "mpv_get_property_string"));
	dispatch.free_fn = reinterpret_cast<PFN_mpv_free>(GetProcAddress(dispatch.library, "mpv_free"));
	dispatch.wait_event = reinterpret_cast<PFN_mpv_wait_event>(GetProcAddress(dispatch.library, "mpv_wait_event"));
	dispatch.set_godot_audio_callback = reinterpret_cast<PFN_mpv_godot_audio_set_callback>(GetProcAddress(dispatch.library, "mpv_godot_audio_set_callback"));

	if (!dispatch.create || !dispatch.initialize || !dispatch.terminate_destroy || !dispatch.error_string || !dispatch.set_option_string || !dispatch.command || !dispatch.set_property_string || !dispatch.get_property || !dispatch.free_fn || !dispatch.wait_event || !dispatch.set_godot_audio_callback) {
		String missing;
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
		if (!dispatch.command) {
			missing += " mpv_command";
		}
		if (!dispatch.set_property_string) {
			missing += " mpv_set_property_string";
		}
		if (!dispatch.get_property) {
			missing += " mpv_get_property";
		}
		if (!dispatch.free_fn) {
			missing += " mpv_free";
		}
		if (!dispatch.wait_event) {
			missing += " mpv_wait_event";
		}
		if (!dispatch.set_godot_audio_callback) {
			missing += " mpv_godot_audio_set_callback";
		}
		r_error = "failed to resolve required libmpv symbols from " + loaded_from + ":" + missing;
		FreeLibrary(dispatch.library);
		dispatch = MpvDispatch();
		return false;
	}

	return true;
#else
	r_error = "libmpv smoke test currently only supports Windows";
	return false;
#endif
}

void unload_mpv_dispatch() {
#ifdef _WIN32
	MpvDispatch &dispatch = get_dispatch();
	if (dispatch.library) {
		FreeLibrary(dispatch.library);
	}
	dispatch = MpvDispatch();
#endif
}

} // namespace

void MpvCore::set_audio_callback(void *p_user_data, mpv_godot_audio_callback_fn p_callback) {
	audio_callback_user_data = p_user_data;
	audio_callback = p_callback;
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
	mpv_handle *&handle = get_handle();
	handle = dispatch.create();
	if (!handle) {
		status = "mpv_create failed";
		return false;
	}

	dispatch.set_option_string(handle, "terminal", "no");
	dispatch.set_option_string(handle, "msg-level", "all=warn");
	dispatch.set_option_string(handle, "idle", "yes");
	dispatch.set_option_string(handle, "keep-open", "yes");
	const char *video_output_name = video_output_mode == VideoOutputMode::LIBMPV ? "libmpv" : "null";
	dispatch.set_option_string(handle, "vo", video_output_name);
	dispatch.set_option_string(handle, "ao", "godot");
	dispatch.set_option_string(handle, "audio-format", "float");
	dispatch.set_option_string(handle, "pause", "yes");

	if (dispatch.set_godot_audio_callback(handle, audio_callback_user_data, audio_callback) < 0) {
		dispatch.terminate_destroy(handle);
		handle = nullptr;
		status = "failed to configure godot audio callback";
		return false;
	}

	const int init_result = dispatch.initialize(handle);
	if (init_result < 0) {
		dispatch.terminate_destroy(handle);
		handle = nullptr;
		status = "mpv_initialize failed: " + format_mpv_error(init_result);
		return false;
	}

	initialized = true;
	file_loaded = false;
	eof_reached = false;
	status = "libmpv initialized";
	return true;
}

void MpvCore::shutdown() {
	mpv_handle *&handle = get_handle();
	if (handle) {
		get_dispatch().terminate_destroy(handle);
		handle = nullptr;
	}

	initialized = false;
	playback_state = PlaybackState::STOPPED;
	file_loaded = false;
	eof_reached = false;
	loaded_path = "";
	time_pos = 0.0;
	duration = 0.0;
	video_width = 0;
	video_height = 0;
	unload_mpv_dispatch();
	status = "mpv shut down";
}

void MpvCore::load_file(const String &p_path) {
	if (!initialized) {
		status = "load_file ignored because libmpv is not initialized";
		return;
	}

	loaded_path = p_path;
	time_pos = 0.0;
	duration = 0.0;
	video_width = 0;
	video_height = 0;
	playback_state = PlaybackState::STOPPED;
	file_loaded = false;
	eof_reached = false;
	status = "loading file";

	CharString utf8_path = p_path.utf8();
	const char *command[] = { "loadfile", utf8_path.get_data(), nullptr };
	const int result = get_dispatch().command(get_handle(), command);
	if (result < 0) {
		status = "loadfile failed: " + format_mpv_error(result);
	}
}

void MpvCore::play() {
	if (!initialized) {
		status = "play ignored because libmpv is not initialized";
		return;
	}

	const int result = get_dispatch().set_property_string(get_handle(), "pause", "no");
	if (result < 0) {
		status = "play failed: " + format_mpv_error(result);
		return;
	}

	playback_state = PlaybackState::PLAYING;
	status = "play issued";
}

void MpvCore::pause() {
	if (!initialized) {
		status = "pause ignored because libmpv is not initialized";
		return;
	}

	if (playback_state == PlaybackState::PLAYING) {
		const int result = get_dispatch().set_property_string(get_handle(), "pause", "yes");
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
		const int result = get_dispatch().command(get_handle(), command);
		if (result < 0) {
			status = "stop failed: " + format_mpv_error(result);
			return;
		}
	}
	time_pos = 0.0;
	playback_state = PlaybackState::STOPPED;
	file_loaded = false;
	eof_reached = false;
	status = "stop issued";
}

void MpvCore::seek(double p_seconds) {
	if (!initialized) {
		status = "seek ignored because libmpv is not initialized";
		return;
	}

	time_pos = std::max(0.0, p_seconds);
	const String seconds_string = String::num(time_pos);
	const CharString seconds_utf8 = seconds_string.utf8();
	const char *command[] = { "seek", seconds_utf8.get_data(), "absolute", nullptr };
	const int result = get_dispatch().command(get_handle(), command);
	if (result < 0) {
		status = "seek failed: " + format_mpv_error(result);
		return;
	}
	status = "seek issued";
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
	while ((event = get_dispatch().wait_event(get_handle(), 0.0)) != nullptr) {
		if (event->event_id == MPV_EVENT_NONE) {
			break;
		}

		switch (event->event_id) {
			case MPV_EVENT_START_FILE:
				status = "starting file";
				break;
			case MPV_EVENT_FILE_LOADED:
				file_loaded = true;
				eof_reached = false;
				status = "file loaded";
				result.file_loaded = true;
				break;
			case MPV_EVENT_END_FILE: {
				playback_state = PlaybackState::STOPPED;
				file_loaded = false;
				eof_reached = true;
				time_pos = 0.0;
				video_width = 0;
				video_height = 0;
				const mpv_event_end_file *end_file = static_cast<const mpv_event_end_file *>(event->data);
				if (end_file && end_file->reason == MPV_END_FILE_REASON_ERROR) {
					status = "playback ended with error: " + format_mpv_error(end_file->error);
					result.failed = true;
				} else {
					status = "playback finished";
				}
				result.playback_finished = true;
			} break;
			case MPV_EVENT_SEEK:
				status = "seeking";
				break;
			case MPV_EVENT_PLAYBACK_RESTART:
				status = "playback restarted";
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

	double new_time_pos = time_pos;
	if (get_mpv_double_property("time-pos", new_time_pos) && new_time_pos != time_pos) {
		time_pos = new_time_pos;
		result.position_changed = true;
	}

	double new_duration = duration;
	if (get_mpv_double_property("duration", new_duration)) {
		duration = new_duration;
	}

	int64_t new_width = video_width;
	if (get_mpv_int64_property("width", new_width)) {
		video_width = static_cast<int>(new_width);
	}

	int64_t new_height = video_height;
	if (get_mpv_int64_property("height", new_height)) {
		video_height = static_cast<int>(new_height);
	}

	bool paused = playback_state == PlaybackState::PAUSED;
	if (get_mpv_flag_property("pause", paused)) {
		const PlaybackState new_state = file_loaded ? (paused ? PlaybackState::PAUSED : PlaybackState::PLAYING) : PlaybackState::STOPPED;
		if (new_state != playback_state) {
			playback_state = new_state;
			result.playback_state_changed = true;
		}
	}

	bool new_eof_reached = eof_reached;
	if (get_mpv_flag_property("eof-reached", new_eof_reached) && new_eof_reached != eof_reached) {
		eof_reached = new_eof_reached;
		result.eof_reached = eof_reached;
		if (eof_reached) {
			status = "playback finished";
		}
	}

	result.status = status;
	return result;
}

const String &MpvCore::get_loaded_path() const {
	return loaded_path;
}

double MpvCore::get_time_pos() const {
	return time_pos;
}

double MpvCore::get_duration() const {
	if (initialized) {
		double duration_value = 0.0;
		if (get_dispatch().get_property(get_handle(), "duration", MPV_FORMAT_DOUBLE, &duration_value) >= 0) {
			return duration_value;
		}
	}

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
	return get_handle();
}

bool MpvCore::has_loaded_file() const {
	return file_loaded;
}

MpvCore::VideoOutputMode MpvCore::get_video_output_mode() const {
	return video_output_mode;
}

} // namespace libmpv_zero
