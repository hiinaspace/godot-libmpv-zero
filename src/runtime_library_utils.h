#pragma once

#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {

bool resolve_windows_runtime_library_path(const godot::String &p_library_name, godot::String &r_path, godot::String &r_error);

} // namespace libmpv_zero
