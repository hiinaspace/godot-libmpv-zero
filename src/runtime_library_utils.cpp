#include "runtime_library_utils.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

namespace libmpv_zero {

bool resolve_windows_runtime_library_path(const String &p_library_name, String &r_path, String &r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		r_error = "project settings unavailable";
		return false;
	}

	PackedStringArray candidate_paths;
	candidate_paths.push_back("res://addons/libmpv_zero/bin/windows/" + p_library_name);
	candidate_paths.push_back("res://bin/windows/" + p_library_name);

	for (int i = 0; i < candidate_paths.size(); ++i) {
		const String candidate = project_settings->globalize_path(candidate_paths[i]);
		if (FileAccess::file_exists(candidate)) {
			r_path = candidate;
			return true;
		}
	}

	r_error = "failed to locate " + p_library_name + " in addon or sample-project runtime paths";
	return false;
}

} // namespace libmpv_zero
