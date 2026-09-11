#include "runtime_library_utils.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace godot;

namespace libmpv_zero {

namespace {
void *open_runtime(const String &p_path, String &r_error) {
#ifdef _WIN32
	const Char16String path = p_path.utf16();
	const DWORD flags = p_path.is_absolute_path() ? LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS : 0;
	void *library = LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.get_data()), nullptr, flags);
	if (!library) {
		r_error = vformat("failed to load %s (Win32 error %d)", p_path, static_cast<int>(GetLastError()));
	}
#else
	void *library = dlopen(p_path.utf8().get_data(), RTLD_NOW | RTLD_LOCAL);
	if (!library) {
		r_error = "failed to load " + p_path + ": " + String(dlerror());
	}
#endif
	return library;
}
} // namespace

void *open_mpv_runtime(String &r_path, String &r_error) {
#ifdef _WIN32
	const String platform = "windows";
	const String name = "libmpv-2.dll";
#else
	const String platform = "linux";
	const String name = "libmpv.so.2";
#endif
	// Both the core and render API must resolve the same forked library.
	static String selected_path;
	if (!selected_path.is_empty()) {
		r_path = selected_path;
		return open_runtime(r_path, r_error);
	}
	OS *os = OS::get_singleton();
	String override_path = os->get_environment("LIBMPV_ZERO_MPV_LIBRARY");
	if (!override_path.is_empty()) {
		if (!override_path.is_absolute_path()) {
			r_error = "LIBMPV_ZERO_MPV_LIBRARY must be an absolute path";
			return nullptr;
		}
		r_path = override_path;
	} else {
		PackedStringArray candidates;
		ProjectSettings *settings = ProjectSettings::get_singleton();
		candidates.push_back(settings->globalize_path("res://addons/libmpv_zero/bin/" + platform + "/" + name));
		candidates.push_back(settings->globalize_path("res://bin/" + platform + "/" + name));
		const String executable_dir = os->get_executable_path().get_base_dir();
		candidates.push_back(executable_dir.path_join("addons/libmpv_zero/bin/" + platform + "/" + name));
		candidates.push_back(executable_dir.path_join("bin/" + platform + "/" + name));
		for (int i = 0; i < candidates.size(); ++i) {
			if (FileAccess::file_exists(candidates[i])) {
				r_path = candidates[i];
				break;
			}
		}
	}
	if (r_path.is_empty()) {
		r_error = "custom libmpv runtime not found; stage " + name + " or set LIBMPV_ZERO_MPV_LIBRARY";
		return nullptr;
	}
	void *library = open_runtime(r_path, r_error);
	if (library) {
		selected_path = r_path;
	}
	return library;
}

void *open_vulkan_runtime(String &r_error) {
	String path = OS::get_singleton()->get_environment("LIBMPV_ZERO_VULKAN_LIBRARY");
	if (path.is_empty()) {
#ifdef _WIN32
		path = "vulkan-1.dll";
#else
		path = "libvulkan.so.1";
#endif
	}
	return open_runtime(path, r_error);
}

void *runtime_symbol(void *p_library, const char *p_name) {
#ifdef _WIN32
	return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(p_library), p_name));
#else
	return dlsym(p_library, p_name);
#endif
}

void close_runtime(void *p_library) {
	if (!p_library) {
		return;
	}
#ifdef _WIN32
	FreeLibrary(static_cast<HMODULE>(p_library));
#else
	dlclose(p_library);
#endif
}


} // namespace libmpv_zero
