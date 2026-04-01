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
	PFN_mpv_set_option_string set_option_string = nullptr;
	PFN_mpv_command command = nullptr;
	PFN_mpv_set_property_string set_property_string = nullptr;
	PFN_mpv_get_property get_property = nullptr;
	PFN_mpv_get_property_string get_property_string = nullptr;
	PFN_mpv_free free_fn = nullptr;
	PFN_mpv_wait_event wait_event = nullptr;
};

MpvDispatch &get_dispatch() {
	static MpvDispatch dispatch;
	return dispatch;
}

mpv_handle *&get_handle() {
	static mpv_handle *handle = nullptr;
	return handle;
}

bool load_mpv_dispatch(String &r_error) {
#ifdef _WIN32
	MpvDispatch &dispatch = get_dispatch();
	if (dispatch.library) {
		return true;
	}

	String dll_path = ProjectSettings::get_singleton()->globalize_path("res://bin/windows/libmpv-2.dll");
	const Char16String dll_path_utf16 = dll_path.utf16();
	dispatch.library = LoadLibraryW(reinterpret_cast<LPCWSTR>(dll_path_utf16.get_data()));
	if (!dispatch.library) {
		dispatch.library = LoadLibraryW(L"libmpv-2.dll");
	}
	if (!dispatch.library) {
		r_error = "failed to load libmpv-2.dll";
		return false;
	}

	dispatch.create = reinterpret_cast<PFN_mpv_create>(GetProcAddress(dispatch.library, "mpv_create"));
	dispatch.initialize = reinterpret_cast<PFN_mpv_initialize>(GetProcAddress(dispatch.library, "mpv_initialize"));
	dispatch.terminate_destroy = reinterpret_cast<PFN_mpv_terminate_destroy>(GetProcAddress(dispatch.library, "mpv_terminate_destroy"));
	dispatch.set_option_string = reinterpret_cast<PFN_mpv_set_option_string>(GetProcAddress(dispatch.library, "mpv_set_option_string"));
	dispatch.command = reinterpret_cast<PFN_mpv_command>(GetProcAddress(dispatch.library, "mpv_command"));
	dispatch.set_property_string = reinterpret_cast<PFN_mpv_set_property_string>(GetProcAddress(dispatch.library, "mpv_set_property_string"));
	dispatch.get_property = reinterpret_cast<PFN_mpv_get_property>(GetProcAddress(dispatch.library, "mpv_get_property"));
	dispatch.get_property_string = reinterpret_cast<PFN_mpv_get_property_string>(GetProcAddress(dispatch.library, "mpv_get_property_string"));
	dispatch.free_fn = reinterpret_cast<PFN_mpv_free>(GetProcAddress(dispatch.library, "mpv_free"));
	dispatch.wait_event = reinterpret_cast<PFN_mpv_wait_event>(GetProcAddress(dispatch.library, "mpv_wait_event"));

	if (!dispatch.create || !dispatch.initialize || !dispatch.terminate_destroy || !dispatch.set_option_string || !dispatch.command || !dispatch.set_property_string || !dispatch.get_property || !dispatch.free_fn) {
		r_error = "failed to resolve required libmpv symbols";
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

	dispatch.set_option_string(handle, "terminal", "yes");
	dispatch.set_option_string(handle, "msg-level", "all=status");
	dispatch.set_option_string(handle, "idle", "yes");
	dispatch.set_option_string(handle, "keep-open", "yes");
	dispatch.set_option_string(handle, "vo", "null");
	dispatch.set_option_string(handle, "ao", "null");

	const int init_result = dispatch.initialize(handle);
	if (init_result < 0) {
		dispatch.terminate_destroy(handle);
		handle = nullptr;
		status = "mpv_initialize failed";
		return false;
	}

	initialized = true;
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
	playback_state = PlaybackState::STOPPED;
	status = "file loaded";

	CharString utf8_path = p_path.utf8();
	const char *command[] = { "loadfile", utf8_path.get_data(), nullptr };
	get_dispatch().command(get_handle(), command);
}

void MpvCore::play() {
	if (!initialized) {
		status = "play ignored because libmpv is not initialized";
		return;
	}

	get_dispatch().set_property_string(get_handle(), "pause", "no");
	playback_state = PlaybackState::PLAYING;
	status = "playing";
}

void MpvCore::pause() {
	if (!initialized) {
		status = "pause ignored because libmpv is not initialized";
		return;
	}

	if (playback_state == PlaybackState::PLAYING) {
		get_dispatch().set_property_string(get_handle(), "pause", "yes");
		playback_state = PlaybackState::PAUSED;
		status = "paused";
	}
}

void MpvCore::stop() {
	if (initialized) {
		const char *command[] = { "stop", nullptr };
		get_dispatch().command(get_handle(), command);
	}
	time_pos = 0.0;
	playback_state = PlaybackState::STOPPED;
	status = "stopped";
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
	get_dispatch().command(get_handle(), command);
	status = "seek issued";
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

} // namespace libmpv_zero
